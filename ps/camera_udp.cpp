#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// Linux C++17; XRT C API is loaded at runtime so host transport tests need no XRT.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <linux/videodev2.h>
#include <memory>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <condition_variable>
#include <mutex>
#include "color_boxes.hpp"

namespace {
constexpr unsigned W=640, H=480, PIXELS=W*H, RGB_BYTES=PIXELS*3;
constexpr unsigned DMA_BYTES=PIXELS*4, PAYLOAD=1200, PACKETS=(RGB_BYTES+PAYLOAD-1)/PAYLOAD;
volatile sig_atomic_t running=1;
void stop(int) { running=0; }
using Clock=std::chrono::steady_clock;
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
int xioctl(int fd,unsigned long op,void* arg) {
    int r; do { r=ioctl(fd,op,arg); } while(r<0 && errno==EINTR); return r;
}
struct Fd {
    int value=-1;
    explicit Fd(int n=-1):value(n) {}
    ~Fd(){ if(value>=0) close(value); }
    Fd(const Fd&)=delete; Fd& operator=(const Fd&)=delete;
};
uint8_t clip(int n){ return static_cast<uint8_t>(std::clamp(n,0,255)); }
// Limited-range BT.601 YUYV, the explicitly supported initial conversion.
void yuv(uint8_t y,uint8_t u,uint8_t v,uint8_t* p) {
    const int c=std::max(0,int(y)-16), d=int(u)-128,e=int(v)-128;
    p[0]=clip((298*c+409*e+128)>>8);
    p[1]=clip((298*c-100*d-208*e+128)>>8);
    p[2]=clip((298*c+516*d+128)>>8);
}
struct Camera {
    Fd fd;
    struct Mapping { void* p; size_t n; };
    std::vector<Mapping> maps;
    unsigned stride=0, format=0;
    bool streaming=false;
    explicit Camera(const std::string& path,unsigned fps):fd(open(path.c_str(),O_RDWR|O_NONBLOCK)) {
        try {
            require(fd.value>=0,"open camera: "+std::string(strerror(errno)));
            v4l2_capability cap{};
            require(xioctl(fd.value,VIDIOC_QUERYCAP,&cap)==0,"VIDIOC_QUERYCAP failed");
            unsigned caps=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
            require((caps&V4L2_CAP_VIDEO_CAPTURE)&&(caps&V4L2_CAP_STREAMING),"need single-plane V4L2 streaming capture");
            v4l2_format f{}; f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
            f.fmt.pix.width=W; f.fmt.pix.height=H; f.fmt.pix.field=V4L2_FIELD_NONE;
            f.fmt.pix.pixelformat=V4L2_PIX_FMT_RGB24;
            int result=xioctl(fd.value,VIDIOC_S_FMT,&f);
            if(result<0 || (f.fmt.pix.pixelformat!=V4L2_PIX_FMT_RGB24 && f.fmt.pix.pixelformat!=V4L2_PIX_FMT_YUYV)) {
                f={}; f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
                f.fmt.pix.width=W; f.fmt.pix.height=H; f.fmt.pix.field=V4L2_FIELD_NONE;
                f.fmt.pix.pixelformat=V4L2_PIX_FMT_YUYV;
                require(xioctl(fd.value,VIDIOC_S_FMT,&f)==0,"camera supports neither RGB24 nor YUYV");
            }
            require(f.fmt.pix.width==W && f.fmt.pix.height==H,"camera did not accept 640x480");
            format=f.fmt.pix.pixelformat;
            require(format==V4L2_PIX_FMT_RGB24 || format==V4L2_PIX_FMT_YUYV,"unsupported negotiated camera format");
            require(f.fmt.pix.field==V4L2_FIELD_NONE,"interlaced capture is not supported");
            if(format==V4L2_PIX_FMT_YUYV) {
                unsigned enc=f.fmt.pix.ycbcr_enc;
                if(enc==V4L2_YCBCR_ENC_DEFAULT) enc=V4L2_MAP_YCBCR_ENC_DEFAULT(f.fmt.pix.colorspace);
                unsigned quant=f.fmt.pix.quantization;
                if(quant==V4L2_QUANTIZATION_DEFAULT)
                    quant=V4L2_MAP_QUANTIZATION_DEFAULT(false,f.fmt.pix.colorspace,enc);
                require(enc==V4L2_YCBCR_ENC_601 && quant==V4L2_QUANTIZATION_LIM_RANGE,
                    "initial YUYV converter requires limited-range BT.601; camera reports another colorimetry");
            }
            stride=f.fmt.pix.bytesperline;
            require(stride>=W*(format==V4L2_PIX_FMT_RGB24?3u:2u),"invalid camera stride");
            v4l2_streamparm timing{}; timing.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
            timing.parm.capture.timeperframe.numerator=1;
            timing.parm.capture.timeperframe.denominator=fps;
            require(xioctl(fd.value,VIDIOC_S_PARM,&timing)==0,"camera frame-rate request failed");
            std::cout<<"Camera requested="<<fps<<" negotiated="
                     <<timing.parm.capture.timeperframe.denominator<<"/"
                     <<timing.parm.capture.timeperframe.numerator<<" FPS\n";
            v4l2_requestbuffers req{}; req.count=4; req.type=f.type; req.memory=V4L2_MEMORY_MMAP;
            require(xioctl(fd.value,VIDIOC_REQBUFS,&req)==0 && req.count>=2,"VIDIOC_REQBUFS failed");
            for(unsigned i=0;i<req.count;++i) {
                v4l2_buffer b{}; b.type=f.type; b.memory=V4L2_MEMORY_MMAP; b.index=i;
                require(xioctl(fd.value,VIDIOC_QUERYBUF,&b)==0,"VIDIOC_QUERYBUF failed");
                void* p=mmap(nullptr,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd.value,b.m.offset);
                require(p!=MAP_FAILED,"camera mmap failed"); maps.push_back({p,b.length});
                require(xioctl(fd.value,VIDIOC_QBUF,&b)==0,"VIDIOC_QBUF failed");
            }
            v4l2_buf_type t=V4L2_BUF_TYPE_VIDEO_CAPTURE;
            require(xioctl(fd.value,VIDIOC_STREAMON,&t)==0,"VIDIOC_STREAMON failed"); streaming=true;
            std::cout<<"Camera 640x480 "<<(format==V4L2_PIX_FMT_RGB24?"RGB24":"YUYV BT.601 limited")<<" stride="<<stride<<'\n';
        } catch(...) { cleanup(); throw; }
    }
    void cleanup() {
        if(streaming) { v4l2_buf_type t=V4L2_BUF_TYPE_VIDEO_CAPTURE; xioctl(fd.value,VIDIOC_STREAMOFF,&t); streaming=false; }
        for(auto m:maps) munmap(m.p,m.n);
        maps.clear();
    }
    ~Camera(){cleanup();}
    bool capture(std::vector<uint8_t>& rgb) {
        pollfd p{fd.value,POLLIN,0};
        int r; do {r=poll(&p,1,2000);}while(r<0 && errno==EINTR && running);
        if(!running) return false;
        require(r>0 && !(p.revents&(POLLERR|POLLHUP|POLLNVAL)),"camera poll failed or timed out");
        v4l2_buffer b{}; b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory=V4L2_MEMORY_MMAP;
        if(xioctl(fd.value,VIDIOC_DQBUF,&b)<0) {
            if(errno==EAGAIN) return false;
            throw std::runtime_error("VIDIOC_DQBUF failed");
        }
        // If processing fell behind, discard queued older captures before
        // converting pixels. This bounds live latency without growing a queue.
        while(true){
            v4l2_buffer newest{};newest.type=b.type;newest.memory=b.memory;
            if(xioctl(fd.value,VIDIOC_DQBUF,&newest)<0){
                require(errno==EAGAIN,"camera queue drain failed");break;
            }
            require(xioctl(fd.value,VIDIOC_QBUF,&b)==0,"camera stale-frame requeue failed");b=newest;
        }
        require(b.index<maps.size(),"invalid camera buffer index");
        size_t needed=size_t(H-1)*stride+W*(format==V4L2_PIX_FMT_RGB24?3u:2u);
        bool valid=!(b.flags&V4L2_BUF_FLAG_ERROR) && b.bytesused>=needed && b.bytesused<=maps[b.index].n;
        if(valid) for(unsigned row=0;row<H;++row) {
            auto src=static_cast<const uint8_t*>(maps[b.index].p)+row*stride;
            auto dst=rgb.data()+row*W*3;
            if(format==V4L2_PIX_FMT_RGB24) std::memcpy(dst,src,W*3);
            else for(unsigned x=0;x<W;x+=2) {
                yuv(src[0],src[1],src[3],dst); yuv(src[2],src[1],src[3],dst+3);
                src+=4; dst+=6;
            }
        }
        require(xioctl(fd.value,VIDIOC_QBUF,&b)==0,"camera requeue failed");
        return valid;
    }
};
// Public XRT 2022.2 C ABI: opaque handles, no private driver structures.
// Loaded dynamically only for actual PL operation; no CPU fallback on DMA errors.
struct Xrt {
    void* lib=nullptr; void* device=nullptr;
    void* (*device_open)(unsigned)=nullptr;
    int (*device_close)(void*)=nullptr;
    void* (*alloc)(void*,size_t,uint64_t,unsigned)=nullptr;
    int (*free_bo)(void*)=nullptr;
    void* (*map)(void*)=nullptr;
    uint64_t (*address)(void*)=nullptr;
    int (*sync)(void*,int,size_t,size_t)=nullptr;
    template<class T> void symbol(T& fn,const char* name) {
        fn=reinterpret_cast<T>(dlsym(lib,name)); require(fn!=nullptr,std::string("missing XRT symbol: ")+name);
    }
    Xrt(){
        try {
            lib=dlopen("libxrt_coreutil.so.2",RTLD_NOW|RTLD_GLOBAL);
            if(!lib) lib=dlopen("libxrt_coreutil.so",RTLD_NOW|RTLD_GLOBAL);
            require(lib!=nullptr,"XRT runtime missing (libxrt_coreutil.so); install/source board XRT environment");
            symbol(device_open,"xrtDeviceOpen"); symbol(device_close,"xrtDeviceClose");
            symbol(alloc,"xrtBOAlloc"); symbol(free_bo,"xrtBOFree"); symbol(map,"xrtBOMap");
            symbol(address,"xrtBOAddress"); symbol(sync,"xrtBOSync");
            device=device_open(0); require(device!=nullptr,"cannot open XRT device 0");
        } catch(...) { if(lib) dlclose(lib); throw; }
    }
    ~Xrt(){if(device)device_close(device); if(lib)dlclose(lib);}
};
struct Buffer {
    Xrt& x; void* bo=nullptr; uint8_t* data=nullptr; uint64_t addr=0;
    explicit Buffer(Xrt& runtime):x(runtime) {
        bo=x.alloc(x.device,DMA_BYTES,0,0);
        require(bo!=nullptr,"XRT allocation failed (requires compatible zocl/XRT platform)");
        data=static_cast<uint8_t*>(x.map(bo)); addr=x.address(bo);
        if(!data || addr==0 || addr+DMA_BYTES>0x80000000ULL || addr%64) {
            x.free_bo(bo); bo=nullptr;
            throw std::runtime_error("XRT buffer must be aligned and entirely in HP0 low DDR [0,2GiB)");
        }
    }
    ~Buffer(){if(bo)x.free_bo(bo);}
    void sync(int direction){require(x.sync(bo,direction,DMA_BYTES,0)==0,"XRT cache synchronization failed");}
};
void io_barrier(){
#if defined(__aarch64__)
    asm volatile("dsb sy" ::: "memory");
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}
struct Dma {
    Fd lock, mem;
    volatile uint32_t* regs=nullptr;
    explicit Dma(uint64_t base):lock(open("/tmp/kv260_color_detector_dma.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600)),mem(open("/dev/mem",O_RDWR|O_SYNC)) {
        require(lock.value>=0 && flock(lock.value,LOCK_EX|LOCK_NB)==0,"another color detector process owns DMA");
        require(mem.value>=0,"/dev/mem unavailable; run on board with required permissions");
        require(base%4096==0,"DMA base must be page aligned");
        void* p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,mem.value,static_cast<off_t>(base));
        require(p!=MAP_FAILED,"DMA register mmap failed"); regs=static_cast<volatile uint32_t*>(p);
        try {reset(); wr(0x00,1); wr(0x30,1);} catch(...) {munmap(p,4096); regs=nullptr; throw;}
    }
    uint32_t rd(unsigned off){io_barrier(); auto v=regs[off/4]; io_barrier(); return v;}
    void wr(unsigned off,uint32_t v){io_barrier(); regs[off/4]=v; io_barrier();}
    void reset(){
        wr(0,4);
        auto end=Clock::now()+std::chrono::milliseconds(500);
        while(rd(0)&4){require(Clock::now()<end,"DMA reset timed out"); std::this_thread::sleep_for(std::chrono::microseconds(50));}
    }
    ~Dma(){if(regs){try{reset();}catch(...){std::cerr<<"DMA reset failed; reload hardware before reuse\n";} munmap(const_cast<uint32_t*>(regs),4096);}}
    void process(Buffer& input,Buffer& output){
        input.sync(0); output.sync(0);
        wr(0x04,0x7000); wr(0x34,0x7000); // clear old W1C completion/error IRQ status
        wr(0x48,uint32_t(output.addr)); wr(0x4c,0); wr(0x58,DMA_BYTES); // receive first
        wr(0x18,uint32_t(input.addr)); wr(0x1c,0); wr(0x28,DMA_BYTES);
        auto end=Clock::now()+std::chrono::seconds(2);
        while(true){
            auto tx=rd(4),rx=rd(0x34);
            require(((tx|rx)&0x4070)==0,"DMA bus/internal error");
            if((tx&0x1000)&&(rx&0x1000)) break; // fresh IOC, not a stale idle bit
            require(Clock::now()<end,"DMA frame timeout (check TLAST, clock/reset, hardware package)");
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        require(rd(0x58)==DMA_BYTES,"DMA returned an incomplete frame"); output.sync(1);
    }
};
void put16(uint8_t* p,uint16_t n){p[0]=uint8_t(n>>8);p[1]=uint8_t(n);}
void put32(uint8_t* p,uint32_t n){put16(p,uint16_t(n>>16));put16(p+2,uint16_t(n));}
struct Sender {
    Fd fd{socket(AF_INET,SOCK_DGRAM,0)}; sockaddr_in target{};
    Sender(const std::string& ip,unsigned port){
        require(fd.value>=0,"UDP socket failed"); target.sin_family=AF_INET; target.sin_port=htons(uint16_t(port));
        require(inet_pton(AF_INET,ip.c_str(),&target.sin_addr)==1,"use numeric PC IPv4 address");
    }
    void send(const std::vector<uint8_t>& rgb,uint32_t frame){
        // One syscall per 16 packets. Payload iovecs reference the original RGB
        // frame directly; no extra frame-sized packet-copy buffer is needed.
        constexpr unsigned BATCH=16;
        std::array<std::array<uint8_t,10>,BATCH> headers{};
        std::array<iovec,2*BATCH> iov{};
        std::array<mmsghdr,BATCH> msgs{};
        for(unsigned base=0;base<PACKETS;base+=BATCH){
            unsigned count=std::min(BATCH,PACKETS-base);
            for(unsigned j=0;j<count;++j){
                unsigned i=base+j,n=std::min(PAYLOAD,RGB_BYTES-i*PAYLOAD);
                auto h=headers[j].data();
                put32(h,frame);put16(h+4,uint16_t(i));put16(h+6,uint16_t(PACKETS));put16(h+8,uint16_t(n));
                iov[2*j]={h,10};iov[2*j+1]={const_cast<uint8_t*>(rgb.data()+i*PAYLOAD),n};
                msgs[j]={};msgs[j].msg_hdr.msg_name=&target;msgs[j].msg_hdr.msg_namelen=sizeof(target);
                msgs[j].msg_hdr.msg_iov=&iov[2*j];msgs[j].msg_hdr.msg_iovlen=2;
            }
            unsigned sent=0;
            while(sent<count){
                int n=sendmmsg(fd.value,msgs.data()+sent,count-sent,0);
                if(n<0 && errno==EINTR)continue;
                require(n>0,"UDP batch send failed: "+std::string(strerror(errno)));sent+=unsigned(n);
            }
            // Gentle batches without catch-up bursts after scheduler delays.
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }
};
// Keep capture/PL independent of network scheduling, with only one pending frame.
// A slow network replaces the pending frame instead of increasing latency.
struct VideoSender {
    Sender sender;
    std::mutex mutex;std::condition_variable cv;
    std::vector<uint8_t> pending=std::vector<uint8_t>(RGB_BYTES);
    bool ready=false,done=false;uint32_t pending_id=0;
    std::exception_ptr error;
    std::atomic<unsigned> sent{0},dropped{0};
    std::thread worker;
    VideoSender(const std::string& ip,unsigned port):sender(ip,port),worker([this]{
        std::vector<uint8_t> current(RGB_BYTES);
        try{
            while(true){
                uint32_t id;
                {std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return ready||done;});
                 if(!ready && done)break;
                 current.swap(pending);id=pending_id;ready=false;}
                sender.send(current,id);++sent;
            }
        }catch(...){std::lock_guard<std::mutex> lock(mutex);error=std::current_exception();}
    }){}
    void submit(const std::vector<uint8_t>& rgb,uint32_t id){
        std::lock_guard<std::mutex> lock(mutex);
        if(error)std::rethrow_exception(error);
        if(ready)++dropped;
        std::copy(rgb.begin(),rgb.end(),pending.begin());pending_id=id;ready=true;cv.notify_one();
    }
    void finish(){
        {std::lock_guard<std::mutex> lock(mutex);done=true;cv.notify_one();}
        if(worker.joinable())worker.join();
        if(error)std::rethrow_exception(error);
    }
    ~VideoSender(){try{finish();}catch(...){}}
};
uint32_t reference_edge(const std::vector<uint8_t>& rgb,unsigned i){
    auto difference=[&](unsigned j){
        unsigned d=0;
        for(unsigned c=0;c<3;++c)d=std::max(d,unsigned(std::abs(int(rgb[i*3+c])-int(rgb[j*3+c]))));
        return d;
    };
    const unsigned left=(i%W)?difference(i-1):255;
    const unsigned up=(i>=W)?difference(i-W):255;
    return left|(up<<8);
}
void pattern(std::vector<uint8_t>& rgb){
    const uint8_t colors[6][3]={{255,0,0},{120,10,10},{0,255,0},{0,0,255},{255,255,255},{0,0,0}};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)
        std::memcpy(&rgb[(y*W+x)*3],colors[x*6/W],3);
}
void blob_pattern(std::vector<uint8_t>& rgb){
    for(unsigned i=0;i<PIXELS;++i){rgb[3*i]=60;rgb[3*i+1]=70;rgb[3*i+2]=85;}
    struct Shape {unsigned x,y,w,h;std::array<uint8_t,3> c;};
    const Shape shapes[]={
        {40,40,100,80,{35,190,210}}, {220,60,100,80,{150,65,180}},
        {400,40,120,100,{215,125,45}}, {60,280,120,100,{190,190,190}},
        {360,280,120,100,{15,18,20}}
    };
    for(const auto& s:shapes)for(unsigned y=s.y;y<s.y+s.h;++y)for(unsigned x=s.x;x<s.x+s.w;++x)
        std::copy(s.c.begin(),s.c.end(),rgb.begin()+(y*W+x)*3);
}

}
int main(int argc,char** argv) try {
    std::string ip, device="/dev/video0"; unsigned port=5000, frames=0, min_area=200, tolerance=36, edge_threshold=24; double fps=30;
    uint64_t base=0; bool test_pattern=false, transport=false, primary_only=false;
    for(int i=1;i<argc;++i){
        std::string a=argv[i];
        auto value=[&](){require(i+1<argc,"missing value for "+a);return std::string(argv[++i]);};
        if(a=="--ip")ip=value(); else if(a=="--port")port=std::stoul(value());
        else if(a=="--device")device=value(); else if(a=="--dma-base")base=std::stoull(value(),nullptr,0);
        else if(a=="--frames")frames=std::stoul(value()); else if(a=="--fps")fps=std::stod(value());
        else if(a=="--min-area")min_area=std::stoul(value());
        else if(a=="--tolerance")tolerance=std::stoul(value());
        else if(a=="--edge-threshold")edge_threshold=std::stoul(value());
        else if(a=="--primary-only")primary_only=true;
        else if(a=="--test-pattern")test_pattern=true; else if(a=="--transport-test")transport=true;
        else if(a=="--help"){
            std::cout<<"camera_udp --ip PC_IP --dma-base 0xa0000000 [--device /dev/video0] [--port 5000] [--fps 30] [--min-area 200] [--tolerance 36] [--edge-threshold 24] [--primary-only] [--frames N] [--test-pattern]\n"
                     <<"Host UDP test only: camera_udp --ip 127.0.0.1 --transport-test --frames 10\n";return 0;
        }else throw std::runtime_error("unknown argument: "+a);
    }
    require(!ip.empty() && port>0 && port<=65535 && fps>0 && fps<=120,"need --ip, valid port, and FPS in (0,120]");
    require(min_area>0 && min_area<=PIXELS && tolerance<255 && edge_threshold<255,"invalid blob tolerance/area");
    require(transport || base!=0,"--dma-base is required: use only the new color-detector hardware address map");
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    VideoSender sender(ip,port); std::vector<uint8_t> rgb(RGB_BYTES);
    auto begin=Clock::now(); unsigned count=0;
    uint32_t frame=uint32_t(std::chrono::system_clock::now().time_since_epoch().count());
    if(transport){
        std::cout<<"TRANSPORT TEST ONLY: synthetic RGB, no camera or FPGA processing\n";
        pattern(rgb);
        while(running && (!frames || count<frames)){
            auto next=Clock::now()+std::chrono::duration<double>(1/fps);
            sender.submit(rgb,frame++);++count;std::this_thread::sleep_until(next);
        }
    }else{
        Xrt xrt; Buffer input(xrt),output(xrt);
        Dma dma(base); // destroyed/reset BEFORE freeing either DMA buffer
        std::unique_ptr<Camera> camera;
        if(!test_pattern)camera=std::make_unique<Camera>(device,unsigned(fps));
        else pattern(rgb);
        std::vector<uint32_t> packed(PIXELS),flags(PIXELS);
        BlobBoxes detector;
        auto period=Clock::now();unsigned interval_count=0;
        std::array<double,5> stage_ms{};
        while(running && (!frames || count<frames)){
            auto t0=Clock::now();
            auto next=t0+std::chrono::duration<double>(1/fps);
            if(camera && !camera->capture(rgb))continue;
            if(test_pattern)blob_pattern(rgb);
            auto t1=Clock::now();
            for(unsigned i=0;i<PIXELS;++i){const auto p=rgb.data()+3*i;packed[i]=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);}
            std::memcpy(input.data,packed.data(),DMA_BYTES);
            auto t2=Clock::now();
            dma.process(input,output);
            std::memcpy(flags.data(),output.data,DMA_BYTES);
            auto t3=Clock::now();
            if(test_pattern)for(unsigned i=0;i<PIXELS;++i)
                if(flags[i]!=reference_edge(rgb,i))
                    throw std::runtime_error("PL RGB-neighbor difference mismatch at pixel "+std::to_string(i));
            auto boxes=detector.detect(rgb,flags.data(),min_area,tolerance,edge_threshold,primary_only);
            if(test_pattern){
                require(boxes.size()==5,"synthetic scene must yield five arbitrary-color blobs");
                const unsigned expected[5][4]={{40,40,139,119},{220,60,319,139},{400,40,519,139},{60,280,179,379},{360,280,479,379}};
                for(const auto& e:expected)
                    require(std::any_of(boxes.begin(),boxes.end(),[&](const BlobBox& b){return b.x0==e[0] && b.y0==e[1] && b.x1==e[2] && b.y1==e[3];}),"synthetic blob bounds mismatch");
            }
            BlobBoxes::draw(rgb,boxes); // Retain the original camera pixels inside every box.
            auto t4=Clock::now();
            sender.submit(rgb,frame++);++count;++interval_count;
            auto t5=Clock::now();
            const Clock::time_point times[]={t0,t1,t2,t3,t4,t5};
            for(unsigned i=0;i<5;++i)stage_ms[i]+=std::chrono::duration<double,std::milli>(times[i+1]-times[i]).count();
            if(std::chrono::duration<double>(t5-period).count()>=1){
                std::cout<<"PL_OVERLAY fps="<<interval_count/std::chrono::duration<double>(t5-period).count()
                         <<" capture_ms="<<stage_ms[0]/interval_count<<" pack_ms="<<stage_ms[1]/interval_count
                         <<" dma_ms="<<stage_ms[2]/interval_count<<" boxes_ms="<<stage_ms[3]/interval_count
                         <<" queue_ms="<<stage_ms[4]/interval_count<<" boxes="<<boxes.size()
                         <<" sent="<<sender.sent<<" queue_drops="<<sender.dropped<<std::endl;
                interval_count=0;stage_ms.fill(0);period=t5;
            }
            std::this_thread::sleep_until(next);
        }
    }
    sender.finish();
    std::cout<<"Sent "<<sender.sent<<" frames, "<<count/std::chrono::duration<double>(Clock::now()-begin).count()<<" FPS; queued="<<count<<" replaced="<<sender.dropped<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}

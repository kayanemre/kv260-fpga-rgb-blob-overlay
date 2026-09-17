#define main camera_application_main
#include "../ps/camera_udp.cpp"
#undef main
#include <cassert>
int main() {
    uint8_t p[3];
    yuv(16,128,128,p); assert(p[0]==0 && p[1]==0 && p[2]==0);
    yuv(235,128,128,p); assert(p[0]==255 && p[1]==255 && p[2]==255);
    yuv(81,90,240,p); assert(p[0]>=250 && p[1]<=3 && p[2]<=3);
    yuv(145,54,34,p); assert(p[0]<=3 && p[1]>=250 && p[2]<=3);
    yuv(41,240,110,p); assert(p[0]<=3 && p[1]<=3 && p[2]>=250);
    uint8_t h[10]; put32(h,0x12345678);put16(h+4,767);put16(h+6,768);put16(h+8,1200);
    const uint8_t expected[]={0x12,0x34,0x56,0x78,0x02,0xff,0x03,0x00,0x04,0xb0};
    assert(std::memcmp(h,expected,sizeof(h))==0);
    std::cout<<"PASS: BT.601 reference colors and network byte order\n";
}

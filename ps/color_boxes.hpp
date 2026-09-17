#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

struct BlobBox {
    unsigned x0,y0,x1,y1,pixels;
    std::array<uint8_t,3> mean;
    int primary=-1; // 0=red, 1=green, 2=blue; -1=unrestricted blob
};
// Region growing on 4x4 RGB cells; no named colors or fixed color palette.
// PL supplies horizontal/vertical RGB edge strengths. A region must also stay
// close to its seed color, preventing gradual gradients from joining the scene.
class BlobBoxes {
    static constexpr unsigned width=640,height=480,step=4,cols=width/step,rows=height/step;
    struct Cell {std::array<uint8_t,3> rgb; uint8_t left,up;};
    std::vector<Cell> cells=std::vector<Cell>(cols*rows);
    std::vector<uint8_t> visited=std::vector<uint8_t>(cols*rows);
    std::vector<unsigned> queue;
public:
    BlobBoxes(){queue.reserve(cols*rows);}
    static int primary_channel(const std::array<uint8_t,3>& c){
        const unsigned r=c[0],g=c[1],b=c[2];
        if(r>=65 && r>=2*g && 2*r>=3*b)return 0;
        if(g>=65 && 2*g>=3*r && 2*g>=3*b)return 1;
        if(b>=65 && 2*b>=3*r && 2*b>=3*g)return 2;
        return -1;
    }
    std::vector<BlobBox> detect(const std::vector<uint8_t>& rgb,const uint32_t* edges,
                               unsigned min_pixels=200,unsigned tolerance=36,unsigned edge_threshold=24,
                               bool primary_only=false){
        for(unsigned cy=0;cy<rows;++cy)for(unsigned cx=0;cx<cols;++cx){
            unsigned sums[3]={0,0,0},left=0,up=0;
            for(unsigned dy=0;dy<step;++dy)for(unsigned dx=0;dx<step;++dx){
                unsigned p=(cy*step+dy)*width+cx*step+dx;
                for(unsigned c=0;c<3;++c)sums[c]+=rgb[p*3+c];
                if(dx==0)left+=edges[p]&255;
                if(dy==0)up+=(edges[p]>>8)&255;
            }
            auto& cell=cells[cy*cols+cx];
            for(unsigned c=0;c<3;++c)cell.rgb[c]=uint8_t(sums[c]/16);
            cell.left=uint8_t(left/step);cell.up=uint8_t(up/step);
        }
        std::fill(visited.begin(),visited.end(),0);
        std::vector<BlobBox> boxes;
        for(unsigned seed=0;seed<cells.size();++seed){
            if(visited[seed])continue;
            unsigned xmin=seed%cols,xmax=xmin,ymin=seed/cols,ymax=ymin;
            std::array<unsigned,3> sums{};
            queue.clear();queue.push_back(seed);visited[seed]=1;
            const auto seed_rgb=cells[seed].rgb;
            for(size_t head=0;head<queue.size();++head){
                unsigned i=queue[head],x=i%cols,y=i/cols;
                for(unsigned c=0;c<3;++c)sums[c]+=cells[i].rgb[c];
                xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);
                auto visit=[&](unsigned n,unsigned edge){
                    if(visited[n] || edge>edge_threshold)return;
                    for(unsigned c=0;c<3;++c)
                        if(unsigned(std::abs(int(cells[n].rgb[c])-int(seed_rgb[c])))>tolerance)return;
                    visited[n]=1;queue.push_back(n);
                };
                if(x>0)visit(i-1,cells[i].left);
                if(x+1<cols)visit(i+1,cells[i+1].left);
                if(y>0)visit(i-cols,cells[i].up);
                if(y+1<rows)visit(i+cols,cells[i+cols].up);
            }
            unsigned area=unsigned(queue.size())*16;
            // Very large/border-spanning regions are normally the background.
            unsigned borders=(xmin==0)+(ymin==0)+(xmax==cols-1)+(ymax==rows-1);
            if(area<min_pixels || area>width*height*3/5 || (borders>=3 && area>width*height/10) || xmax==xmin || ymax==ymin)continue;
            std::array<uint8_t,3> mean{};
            for(unsigned c=0;c<3;++c)mean[c]=uint8_t(sums[c]/queue.size());
            int primary=primary_channel(mean);
            if(primary_only && primary<0)continue;
            boxes.push_back({xmin*step,ymin*step,(xmax+1)*step-1,(ymax+1)*step-1,area,mean,primary});
        }
        std::sort(boxes.begin(),boxes.end(),[](const BlobBox& a,const BlobBox& b){return a.pixels>b.pixels;});
        if(boxes.size()>24)boxes.resize(24);
        return boxes;
    }
    static void draw(std::vector<uint8_t>& rgb,const std::vector<BlobBox>& boxes){
        auto pixel=[&](unsigned x,unsigned y,const std::array<uint8_t,3>& color){
            std::copy(color.begin(),color.end(),rgb.begin()+(y*width+x)*3);
        };
        for(const auto& b:boxes){
            auto accent=b.mean;
            if(b.primary>=0){accent={0,0,0};accent[unsigned(b.primary)]=255;}
            else {
                unsigned peak=*std::max_element(accent.begin(),accent.end());
                if(peak<48)accent={0,220,255};
                else for(auto& c:accent)c=uint8_t(unsigned(c)*255/peak);
            }
            for(unsigned inset=0;inset<3;++inset){
                if(b.x0+inset>b.x1-inset || b.y0+inset>b.y1-inset)break;
                const auto color=inset?accent:std::array<uint8_t,3>{0,0,0};
                for(unsigned x=b.x0+inset;x<=b.x1-inset;++x){pixel(x,b.y0+inset,color);pixel(x,b.y1-inset,color);}
                for(unsigned y=b.y0+inset;y<=b.y1-inset;++y){pixel(b.x0+inset,y,color);pixel(b.x1-inset,y,color);}
            }
        }
    }
};

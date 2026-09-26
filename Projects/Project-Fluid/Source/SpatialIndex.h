#pragma once
#include "PbfFluid.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace Frontier::ProjectFluid {
// Rebuilt for each position set. Sorted query results preserve the reference
// accumulation order and capped-neighbour selection, not hash iteration order.
class SpatialIndex {
    using Cell=std::array<int,3>;
    struct Hash { size_t operator()(const Cell& c)const noexcept {
        size_t h=0;for(int v:c)h^=std::hash<int>{}(v)+0x9e3779b9u+(h<<6)+(h>>2);return h;
    }};
    float Width;
    std::vector<Vec3> Positions;
    Cell Minimum{},Dimensions{};
    std::vector<size_t> Offsets;
    std::vector<uint32_t> Ordered;
    size_t Slot(Cell c)const {return (size_t(c[2]-Minimum[2])*Dimensions[1]+c[1]-Minimum[1])*Dimensions[0]+c[0]-Minimum[0];}
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> Cells;
    Cell Key(Vec3 p)const {return {int(std::floor(p.x/Width)),int(std::floor(p.y/Width)),int(std::floor(p.z/Width))};}
public:
    SpatialIndex(const std::vector<Vec3>& positions,float radius):Width(radius),Positions(positions) {
        if(positions.empty())return;
        Minimum=Key(positions.front());auto maximum=Minimum;
        for(auto p:positions){auto k=Key(p);for(int a=0;a<3;++a){Minimum[a]=std::min(Minimum[a],k[a]);maximum[a]=std::max(maximum[a],k[a]);}}
        size_t cells=1;for(int a=0;a<3;++a){Dimensions[a]=maximum[a]-Minimum[a]+1;if(size_t(Dimensions[a])>1000000/cells){cells=0;break;}cells*=Dimensions[a];}
        if(cells){
            Offsets.assign(cells+1,0);for(auto p:positions)++Offsets[Slot(Key(p))+1];
            for(size_t i=1;i<Offsets.size();++i)Offsets[i]+=Offsets[i-1];
            auto cursor=Offsets;Ordered.resize(positions.size());
            for(uint32_t i=0;i<positions.size();++i)Ordered[cursor[Slot(Key(positions[i]))]++]=i;
        }else for(uint32_t i=0;i<positions.size();++i)Cells[Key(positions[i])].push_back(i);
    }
    void Query(Vec3 p,std::vector<uint32_t>& ids,bool exhaustive=false)const {
        if(exhaustive){ids.resize(Positions.size());for(uint32_t i=0;i<ids.size();++i)ids[i]=i;return;}
        ids.clear();const auto c=Key(p);
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            const Cell key={c[0]+x,c[1]+y,c[2]+z};
            auto accept=[&](uint32_t id){const Vec3 d=Positions[id]-p;if(Dot(d,d)<Width*Width)ids.push_back(id);};
            if(!Offsets.empty()){
                bool inside=true;for(int a=0;a<3;++a)inside&=key[a]>=Minimum[a]&&key[a]-Minimum[a]<Dimensions[a];
                if(inside){const auto at=Slot(key);for(size_t j=Offsets[at];j<Offsets[at+1];++j)accept(Ordered[j]);}
            }else{auto it=Cells.find(key);if(it!=Cells.end())for(auto id:it->second)accept(id);}
        }
        std::sort(ids.begin(),ids.end());
    }
};
}

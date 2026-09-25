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
    const std::vector<Vec3>& Positions;
    std::unordered_map<Cell,std::vector<uint32_t>,Hash> Cells;
    Cell Key(Vec3 p)const {return {int(std::floor(p.x/Width)),int(std::floor(p.y/Width)),int(std::floor(p.z/Width))};}
public:
    SpatialIndex(const std::vector<Vec3>& positions,float radius):Width(radius),Positions(positions) {
        for(uint32_t i=0;i<positions.size();++i)Cells[Key(positions[i])].push_back(i);
    }
    void Query(Vec3 p,std::vector<uint32_t>& ids,bool exhaustive=false)const {
        if(exhaustive){ids.resize(Positions.size());for(uint32_t i=0;i<ids.size();++i)ids[i]=i;return;}
        ids.clear();const auto c=Key(p);
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            auto it=Cells.find({c[0]+x,c[1]+y,c[2]+z});
            if(it!=Cells.end())for(auto id:it->second){const Vec3 d=Positions[id]-p;if(Dot(d,d)<Width*Width)ids.push_back(id);}
        }
        std::sort(ids.begin(),ids.end());
    }
};
}

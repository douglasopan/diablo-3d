#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace devilution::target_selection {
inline constexpr int AssistRadius = 4; // Final logical pixels, independent of AA.
struct Bounds {
    int left=std::numeric_limits<int>::max(),top=std::numeric_limits<int>::max(),right=-1,bottom=-1;
    void Include(int x,int y){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);}
    bool empty() const{return right<left || bottom<top;}
};
// Read visible ownership only. Never dilate or modify the authoritative pick/depth
// buffers. Callers retain exact non-ground ownership and validate native slots.
template<class Eligible,class Depth>
std::optional<size_t> Nearby(int width,int height,int x,int y,Eligible eligible,Depth depth)
{
    if(width<=0 || height<=0 || width>8192 || height>8192 || x<0 || y<0 || x>=width || y>=height)return std::nullopt;
    std::optional<size_t> best;int bestDistance=AssistRadius*AssistRadius+1;float bestDepth=std::numeric_limits<float>::infinity();
    for(int dy=-AssistRadius;dy<=AssistRadius;++dy)for(int dx=-AssistRadius;dx<=AssistRadius;++dx){
        const int distance=dx*dx+dy*dy,nx=x+dx,ny=y+dy;
        if(distance>AssistRadius*AssistRadius || nx<0 || ny<0 || nx>=width || ny>=height)continue;
        const size_t i=static_cast<size_t>(ny)*width+nx;
        if(!eligible(i))continue;const float z=depth(i);if(!std::isfinite(z) || z<=0)continue;
        if(distance<bestDistance || (distance==bestDistance && (z<bestDepth || (z==bestDepth && (!best || i<*best))))){best=i;bestDistance=distance;bestDepth=z;}
    }
    return best;
}
// One-pixel outer contour of the visible silhouette. Architecture, other actors,
// UI/viewmodel occlusion and native identity validation remain caller-owned.
template<class Owned,class Writable,class Paint>
size_t Contour(int width,int height,Bounds bounds,Owned owned,Writable writable,Paint paint)
{
    if(width<=0 || height<=0 || width>8192 || height>8192 || bounds.empty())return 0;
    size_t count=0;
    for(int y=std::max(0,bounds.top-1);y<=std::min(height-1,bounds.bottom+1);++y)
        for(int x=std::max(0,bounds.left-1);x<=std::min(width-1,bounds.right+1);++x){
            const size_t i=static_cast<size_t>(y)*width+x;if(owned(i))continue;
            const bool edge=(x>0 && owned(i-1)) || (x+1<width && owned(i+1)) || (y>0 && owned(i-width)) || (y+1<height && owned(i+width));
            if(edge && writable(i)){paint(x,y);++count;}
        }
    return count;
}
}

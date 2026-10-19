#include "levels/surface_region_halo.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace devilution::surface {
namespace {
constexpr uint64_t MaximumPatches=MaximumSurfaceHaloTriangles/2;
struct Point { int32_t x,z; constexpr bool operator==(const Point &) const=default; };
struct Rect { int32_t x0,z0,x1,z1; };
struct Patch { Rect r; uint32_t step; };
struct Plan {
	SurfaceHaloExtent extent;
	std::vector<Patch> patches;
	std::vector<Point> points;
	std::vector<uint32_t> horizontal;
};
void Fail(SurfaceHaloBuildFailure *out,SurfaceHaloBuildFailure value) noexcept { if(out)*out=value; }
bool Less(Point a,Point b) noexcept { return a.x<b.x||(a.x==b.x&&a.z<b.z); }
int32_t FloorDivide(int32_t x,int32_t step) noexcept { return x/step-(x%step<0?1:0); }
bool Area(Rect r) noexcept { return r.x0<r.x1&&r.z0<r.z1; }
bool Overlap(Rect a,Rect b) noexcept { return a.x0<b.x1&&a.x1>b.x0&&a.z0<b.z1&&a.z1>b.z0; }
Rect Intersect(Rect a,Rect b) noexcept { return {std::max(a.x0,b.x0),std::max(a.z0,b.z0),std::min(a.x1,b.x1),std::min(a.z1,b.z1)}; }
bool Contains(Rect outer,Rect inner) noexcept { return outer.x0<=inner.x0&&outer.z0<=inner.z0&&outer.x1>=inner.x1&&outer.z1>=inner.z1; }
Rect Active(RegionId id) noexcept { const auto o=WorldOffset(id);return {o.x-1,o.z-1,o.x+111,o.z+111}; }
constexpr Rect Town{-1,-1,111,111};
Rect RoundOut(Rect r) noexcept { return {FloorDivide(r.x0,32)*32,FloorDivide(r.z0,32)*32,-FloorDivide(-r.x1,32)*32,-FloorDivide(-r.z1,32)*32}; }
Rect Expand(Rect r,int32_t n) noexcept { return {r.x0-n,r.z0-n,r.x1+n,r.z1+n}; }
bool ValidBounds(SurfaceHaloWorldBounds b) noexcept {
	return std::isfinite(b.minX)&&std::isfinite(b.minZ)&&std::isfinite(b.maxX)&&std::isfinite(b.maxZ)
	    &&b.minX<b.maxX&&b.minZ<b.maxZ;
}
bool AddBytes(uint64_t &bytes,uint64_t count,uint64_t size) noexcept {
	if(bytes>MaximumSurfaceHaloBytes||count>(MaximumSurfaceHaloBytes-bytes)/size)return false;
	bytes+=count*size;return true;
}
uint64_t WorkBytes(const Plan &p) noexcept {
	return sizeof(Plan)+p.patches.capacity()*sizeof(Patch)+p.points.capacity()*sizeof(Point)+p.horizontal.capacity()*sizeof(uint32_t);
}
std::optional<uint64_t> SceneBytes(const SurfaceHaloGeometry &h) noexcept {
	uint64_t n=sizeof(SurfaceHaloGeometry);
	if(!AddBytes(n,h.vertices.capacity(),sizeof(SurfaceRegionVertex))||!AddBytes(n,h.triangles.capacity(),sizeof(SurfaceRegionTriangle))
	    ||!AddBytes(n,h.clusters.capacity(),sizeof(SurfaceHaloCluster)))return std::nullopt;
	return n;
}
int32_t ClusterX(const Patch &p) noexcept { return FloorDivide(p.r.x0,8*static_cast<int32_t>(p.step)); }
int32_t ClusterZ(const Patch &p) noexcept { return FloorDivide(p.r.z0,8*static_cast<int32_t>(p.step)); }
bool SameCluster(const Patch &a,const Patch &b) noexcept { return a.step==b.step&&ClusterX(a)==ClusterX(b)&&ClusterZ(a)==ClusterZ(b); }

uint32_t Subtract(Rect r,Rect hole,std::array<Rect,4> &out) noexcept {
	const Rect i=Intersect(r,hole);if(!Area(i)){out[0]=r;return 1;}
	const std::array<Rect,4> candidates{{{r.x0,r.z0,i.x0,r.z1},{i.x1,r.z0,r.x1,r.z1},{i.x0,r.z0,i.x1,i.z0},{i.x0,i.z1,i.x1,r.z1}}};
	uint32_t count=0;for(auto c:candidates)if(Area(c))out[count++]=c;return count;
}
bool AddPatch(Plan &plan,Rect r,uint32_t step,Rect active,SurfaceHaloBuildFailure *failure) {
	std::array<Rect,4> first{},second{};const auto n=Subtract(r,active,first);
	for(uint32_t i=0;i<n;++i) {
		const auto m=Subtract(first[i],Town,second);
		for(uint32_t j=0;j<m;++j) {
			if(plan.patches.size()>=MaximumPatches){Fail(failure,SurfaceHaloBuildFailure::TriangleBudget);return false;}
			plan.patches.push_back({second[j],step});
		}
	}
	return true;
}

bool Boundary(const Plan &plan,const Patch &patch,std::array<uint32_t,128> &out,uint32_t &count) noexcept {
	count=0;const auto r=patch.r;
	const auto push=[&](uint32_t index){if(count>=out.size())return false;out[count++]=index;return true;};
	const auto vertical=[&](int32_t x,int32_t z0,int32_t z1,bool reverse,bool skipFirst,bool skipLast){
		const auto begin=std::lower_bound(plan.points.begin(),plan.points.end(),Point{x,z0},Less);
		const auto end=std::upper_bound(plan.points.begin(),plan.points.end(),Point{x,z1},Less);
		if(!reverse){for(auto i=begin;i!=end;++i)if(!(skipFirst&&i->z==z0)&&!(skipLast&&i->z==z1))if(!push(static_cast<uint32_t>(i-plan.points.begin())))return false;}
		else{for(auto i=end;i!=begin;){--i;if(!(skipFirst&&i->z==z0)&&!(skipLast&&i->z==z1))if(!push(static_cast<uint32_t>(i-plan.points.begin())))return false;}}
		return true;
	};
	const auto horizontal=[&](int32_t z,int32_t x0,int32_t x1,bool reverse,bool skipFirst,bool skipLast){
		const auto less=[&](uint32_t index,Point key){const auto p=plan.points[index];return p.z<key.z||(p.z==key.z&&p.x<key.x);};
		const auto greater=[&](Point key,uint32_t index){const auto p=plan.points[index];return key.z<p.z||(key.z==p.z&&key.x<p.x);};
		const auto begin=std::lower_bound(plan.horizontal.begin(),plan.horizontal.end(),Point{x0,z},less);
		const auto end=std::upper_bound(plan.horizontal.begin(),plan.horizontal.end(),Point{x1,z},greater);
		if(!reverse){for(auto i=begin;i!=end;++i){const auto p=plan.points[*i];if(!(skipFirst&&p.x==x0)&&!(skipLast&&p.x==x1))if(!push(*i))return false;}}
		else{for(auto i=end;i!=begin;){--i;const auto p=plan.points[*i];if(!(skipFirst&&p.x==x0)&&!(skipLast&&p.x==x1))if(!push(*i))return false;}}
		return true;
	};
	// CCW outline, retaining every collinear registry point on each subedge.
	return horizontal(r.z0,r.x0,r.x1,false,false,false)&&vertical(r.x1,r.z0,r.z1,false,true,false)
	    &&horizontal(r.z1,r.x0,r.x1,true,false,true)&&vertical(r.x0,r.z0,r.z1,true,true,true)&&count>=4;
}
int64_t Cross(Point a,Point b,Point c) noexcept { return int64_t(b.x-a.x)*(c.z-a.z)-int64_t(b.z-a.z)*(c.x-a.x); }
bool InTriangle(Point p,Point a,Point b,Point c) noexcept { return Cross(a,b,p)>=0&&Cross(b,c,p)>=0&&Cross(c,a,p)>=0; }
template<class Emit> bool Triangulate(const Plan &plan,const Patch &patch,Emit emit) {
	std::array<uint32_t,128> polygon{};uint32_t n=0;if(!Boundary(plan,patch,polygon,n))return false;
	if(n==4)return emit({{polygon[0],polygon[2],polygon[1]}})&&emit({{polygon[0],polygon[3],polygon[2]}});
	while(n>3) {
		bool found=false;
		for(uint32_t i=0;i<n;++i) {
			const auto a=polygon[(i+n-1)%n],b=polygon[i],c=polygon[(i+1)%n];
			if(Cross(plan.points[a],plan.points[b],plan.points[c])<=0)continue;
			bool occupied=false;
			for(uint32_t j=0;j<n;++j)if(polygon[j]!=a&&polygon[j]!=b&&polygon[j]!=c)
				if(InTriangle(plan.points[polygon[j]],plan.points[a],plan.points[b],plan.points[c])){occupied=true;break;}
			if(occupied)continue;
			if(!emit({{a,c,b}}))return false;
			for(uint32_t j=i+1;j<n;++j)polygon[j-1]=polygon[j];--n;found=true;break;
		}
		if(!found)return false;
	}
	return Cross(plan.points[polygon[0]],plan.points[polygon[1]],plan.points[polygon[2]])>0
	    &&emit({{polygon[0],polygon[2],polygon[1]}});
}

std::optional<Plan> MakePlan(const SurfaceHaloRequest &request,SurfaceHaloBuildFailure *failure) {
	Fail(failure,SurfaceHaloBuildFailure::None);
	if(!ValidRegionId(request.activeRegion)||!std::isfinite(request.farMetres)||request.farMetres<=0
	    ||!std::isfinite(request.maximumCameraOffsetMetres)||request.maximumCameraOffsetMetres<0){Fail(failure,SurfaceHaloBuildFailure::InvalidRequest);return std::nullopt;}
	if(!request.requiredWorldBounds){Fail(failure,SurfaceHaloBuildFailure::MissingWorldBounds);return std::nullopt;}
	const auto b=*request.requiredWorldBounds;const double camera=request.maximumCameraOffsetMetres/CellMetres;
	if(!ValidBounds(b)){Fail(failure,SurfaceHaloBuildFailure::InvalidRequest);return std::nullopt;}
	if(b.minX-camera < -MaximumSurfaceWorldNative||b.minZ-camera < -MaximumSurfaceWorldNative
	    ||b.maxX+camera > MaximumSurfaceWorldNative||b.maxZ+camera > MaximumSurfaceWorldNative){Fail(failure,SurfaceHaloBuildFailure::VisualSamplingBounds);return std::nullopt;}
	const auto active=Active(request.activeRegion);
	const Rect full=RoundOut({std::min(active.x0,static_cast<int32_t>(std::floor(b.minX-camera))),std::min(active.z0,static_cast<int32_t>(std::floor(b.minZ-camera))),
	    std::max(active.x1,static_cast<int32_t>(std::ceil(b.maxX+camera))),std::max(active.z1,static_cast<int32_t>(std::ceil(b.maxZ+camera)))});
	if(full.x0 < -MaximumSurfaceWorldNative||full.z0 < -MaximumSurfaceWorldNative||full.x1>MaximumSurfaceWorldNative||full.z1>MaximumSurfaceWorldNative){Fail(failure,SurfaceHaloBuildFailure::VisualSamplingBounds);return std::nullopt;}
	const std::array<Rect,6> outer{{Intersect(full,RoundOut(Expand(active,64))),Intersect(full,RoundOut(Expand(active,128))),Intersect(full,RoundOut(Expand(active,256))),Intersect(full,RoundOut(Expand(active,512))),Intersect(full,RoundOut(Expand(active,4096))),full}};
	constexpr std::array<uint32_t,6> steps{{1,2,4,8,16,32}};
	uint64_t leaves=0;
	for(std::size_t i=0;i<outer.size();++i){const auto r=outer[i];uint64_t area=uint64_t(r.x1-r.x0)*(r.z1-r.z0);if(i){const auto q=outer[i-1];area-=uint64_t(q.x1-q.x0)*(q.z1-q.z0);}leaves+=area/(steps[i]*steps[i]);}
	// At least two triangles/positive rectangle. Bound before reserves/products.
	if(leaves>MaximumPatches+StorageSize*StorageSize){Fail(failure,SurfaceHaloBuildFailure::TriangleBudget);return std::nullopt;}
	Plan plan;plan.patches.reserve(static_cast<std::size_t>(std::min(leaves+512,MaximumPatches)));
	for(std::size_t i=0;i<outer.size();++i)for(int32_t z=outer[i].z0;z<outer[i].z1;z+=static_cast<int32_t>(steps[i]))for(int32_t x=outer[i].x0;x<outer[i].x1;x+=static_cast<int32_t>(steps[i])) {
		const Rect r{x,z,x+static_cast<int32_t>(steps[i]),z+static_cast<int32_t>(steps[i])};if(i&&Contains(outer[i-1],r))continue;
		if(!AddPatch(plan,r,steps[i],active,failure))return std::nullopt;
	}
	std::sort(plan.patches.begin(),plan.patches.end(),[](const Patch&a,const Patch&b){
		if(a.step!=b.step)return a.step<b.step;if(ClusterZ(a)!=ClusterZ(b))return ClusterZ(a)<ClusterZ(b);if(ClusterX(a)!=ClusterX(b))return ClusterX(a)<ClusterX(b);
		if(a.r.z0!=b.r.z0)return a.r.z0<b.r.z0;if(a.r.x0!=b.r.x0)return a.r.x0<b.r.x0;if(a.r.z1!=b.r.z1)return a.r.z1<b.r.z1;return a.r.x1<b.r.x1;});
	plan.points.reserve(plan.patches.size()*4+8*StorageSize);
	for(const auto &p:plan.patches)for(auto v:std::array<Point,4>{{{p.r.x0,p.r.z0},{p.r.x1,p.r.z0},{p.r.x1,p.r.z1},{p.r.x0,p.r.z1}}})plan.points.push_back(v);
	// Both companion surfaces have integer-grid boundary vertices. Retain every
	// such point even when Town falls in a distant coarse LOD ring.
	for(const auto hole:std::array<Rect,2>{{active,Town}})for(int32_t i=0;i<StorageSize;++i)
		for(const auto p:std::array<Point,4>{{{hole.x0+i,hole.z0},{hole.x1,hole.z0+i},{hole.x1-i,hole.z1},{hole.x0,hole.z1-i}}})
			if(p.x>=full.x0&&p.x<=full.x1&&p.z>=full.z0&&p.z<=full.z1)plan.points.push_back(p);
	std::sort(plan.points.begin(),plan.points.end(),Less);plan.points.erase(std::unique(plan.points.begin(),plan.points.end()),plan.points.end());
	if(plan.points.size()>MaximumSurfaceHaloVertices){Fail(failure,SurfaceHaloBuildFailure::VertexBudget);return std::nullopt;}
	const uint64_t compactPeak=WorkBytes(plan)+plan.points.size()*sizeof(Point);
	std::vector<Point>(plan.points).swap(plan.points);
	plan.horizontal.reserve(plan.points.size());for(uint32_t i=0;i<plan.points.size();++i)plan.horizontal.push_back(i);
	std::sort(plan.horizontal.begin(),plan.horizontal.end(),[&](uint32_t a,uint32_t b){return plan.points[a].z<plan.points[b].z||(plan.points[a].z==plan.points[b].z&&plan.points[a].x<plan.points[b].x);});
	auto &e=plan.extent;e.minWorldCellX=full.x0+1;e.minWorldCellZ=full.z0+1;e.maxWorldCellX=full.x1;e.maxWorldCellZ=full.z1;
	e.widthCells=full.x1-full.x0;e.heightCells=full.z1-full.z0;e.vertexCount=plan.points.size();e.townHoleInExtent=Overlap(full,Town);
	for(std::size_t i=0;i<plan.patches.size();++i){std::array<uint32_t,128> polygon{};uint32_t count=0;if(!Boundary(plan,plan.patches[i],polygon,count)){Fail(failure,SurfaceHaloBuildFailure::InvalidGeneratedGeometry);return std::nullopt;}
		e.triangleCount+=count-2;if(!i||!SameCluster(plan.patches[i-1],plan.patches[i]))++e.clusterCapacity;}
	if(e.triangleCount>MaximumSurfaceHaloTriangles){Fail(failure,SurfaceHaloBuildFailure::TriangleBudget);return std::nullopt;}
	uint64_t bytes=sizeof(SurfaceHaloGeometry);
	if(!AddBytes(bytes,e.vertexCount,sizeof(SurfaceRegionVertex))||!AddBytes(bytes,e.triangleCount,sizeof(SurfaceRegionTriangle))||!AddBytes(bytes,e.clusterCapacity,sizeof(SurfaceHaloCluster))){Fail(failure,SurfaceHaloBuildFailure::MemoryBudget);return std::nullopt;}
	e.plannedPayloadBytes=bytes;e.plannedPeakPayloadBytes=std::max(compactPeak,bytes+WorkBytes(plan));
	if(e.plannedPeakPayloadBytes>MaximumSurfaceHaloBytes){Fail(failure,SurfaceHaloBuildFailure::MemoryBudget);return std::nullopt;}
	return plan;
}

SurfaceHaloBounds EmptyBounds() noexcept {const double i=std::numeric_limits<double>::infinity();return {i,i,i,-i,-i,-i};}
void ExpandBounds(SurfaceHaloBounds &b,const SurfaceRegionVertex &v) noexcept {
	b.minX=std::min(b.minX,v.worldNativeX);b.maxX=std::max(b.maxX,v.worldNativeX);b.minZ=std::min(b.minZ,v.worldNativeZ);b.maxZ=std::max(b.maxZ,v.worldNativeZ);
	b.minHeightMetres=std::min(b.minHeightMetres,v.heightMetres);b.maxHeightMetres=std::max(b.maxHeightMetres,v.heightMetres);
}
bool EqualBounds(SurfaceHaloBounds a,SurfaceHaloBounds b) noexcept {return a.minX==b.minX&&a.maxX==b.maxX&&a.minZ==b.minZ&&a.maxZ==b.maxZ&&a.minHeightMetres==b.minHeightMetres&&a.maxHeightMetres==b.maxHeightMetres;}
bool VertexValid(const SurfaceRegionVertex &v,Point p) noexcept {
	return v.worldNativeX==p.x&&v.worldNativeZ==p.z&&std::isfinite(v.heightMetres)&&std::abs(v.heightMetres)<=MaximumGeometryHeightMetres
	    &&std::isfinite(v.normal.x)&&std::isfinite(v.normal.height)&&std::isfinite(v.normal.z)&&std::abs(v.normal.x)<=1&&v.normal.height>0&&v.normal.height<=1&&std::abs(v.normal.z)<=1
	    &&std::abs(std::hypot(v.normal.x,v.normal.height,v.normal.z)-1)<=1e-10&&v.worldTextureMetres0U==p.x*CellMetres&&v.worldTextureMetres0V==p.z*CellMetres
	    &&v.regionalMetres56U==(p.x-RegionalOrigin)*CellMetres&&v.regionalMetres56V==(p.z-RegionalOrigin)*CellMetres;
}
bool FastValid(const SurfaceHaloGeometry &h) noexcept {
	return ValidRegionId(h.request.activeRegion)&&h.request.requiredWorldBounds&&ValidBounds(*h.request.requiredWorldBounds)
	    &&std::isfinite(h.request.farMetres)&&h.request.farMetres>0&&std::isfinite(h.request.maximumCameraOffsetMetres)&&h.request.maximumCameraOffsetMetres>=0
	    &&h.activeWorldOffset==WorldOffset(h.request.activeRegion)&&h.seed==VisualSeed&&h.generatorRevision==GeneratorRevision&&h.heightRevision==HeightRevision&&h.geometryRevision==GeometryRevision&&h.haloRevision==SurfaceHaloRevision
	    &&h.extent.minWorldCellX<=h.extent.maxWorldCellX&&h.extent.minWorldCellZ<=h.extent.maxWorldCellZ
	    &&h.extent.vertexCount<=MaximumSurfaceHaloVertices&&h.extent.triangleCount<=MaximumSurfaceHaloTriangles&&h.extent.plannedPeakPayloadBytes<=MaximumSurfaceHaloBytes
	    &&h.vertices.size()==h.extent.vertexCount&&h.triangles.size()==h.extent.triangleCount&&h.clusters.size()==h.extent.clusterCapacity&&h.retainedPayloadBytes<=MaximumSurfaceHaloBytes;
}
bool ValidatePlan(const SurfaceHaloGeometry &h,const Plan &plan) {
	if(!FastValid(h)||h.extent!=plan.extent)return false;const auto bytes=SceneBytes(h);
	if(!bytes||*bytes!=h.retainedPayloadBytes||*bytes+WorkBytes(plan)>MaximumSurfaceHaloBytes)return false;
	for(std::size_t i=0;i<h.vertices.size();++i)if(!VertexValid(h.vertices[i],plan.points[i]))return false;
	std::size_t triangle=0,cluster=0;SurfaceHaloBounds bounds=EmptyBounds();std::size_t first=0;
	for(std::size_t i=0;i<plan.patches.size();++i) {
		if(!i||!SameCluster(plan.patches[i-1],plan.patches[i])){bounds=EmptyBounds();first=triangle;}
		if(!Triangulate(plan,plan.patches[i],[&](SurfaceRegionTriangle t){if(triangle>=h.triangles.size()||h.triangles[triangle++].vertices!=t.vertices)return false;for(auto id:t.vertices)ExpandBounds(bounds,h.vertices[id]);return true;}))return false;
		if(i+1==plan.patches.size()||!SameCluster(plan.patches[i],plan.patches[i+1])){
			if(cluster>=h.clusters.size())return false;const auto &c=h.clusters[cluster++];
			if(c.gridStep!=plan.patches[i].step||c.worldClusterX!=ClusterX(plan.patches[i])||c.worldClusterZ!=ClusterZ(plan.patches[i])||c.firstTriangle!=first||c.triangleCount!=triangle-first||!EqualBounds(c.bounds,bounds))return false;
		}
	}
	return triangle==h.triangles.size()&&cluster==h.clusters.size();
}
} // namespace

std::optional<SurfaceHaloExtent> PreflightSurfaceHalo(const SurfaceHaloRequest &request,SurfaceHaloBuildFailure *failure) {
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	try {
#endif
		const auto plan=MakePlan(request,failure);return plan?std::optional<SurfaceHaloExtent>(plan->extent):std::nullopt;
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	}catch(...){Fail(failure,SurfaceHaloBuildFailure::AllocationFailed);return std::nullopt;}
#endif
}
std::optional<SurfaceHaloGeometry> BuildSurfaceHalo(const SurfaceHaloRequest &request,SurfaceHaloBuildFailure *failure) {
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	try {
#endif
		auto plan=MakePlan(request,failure);if(!plan)return std::nullopt;
		SurfaceHaloGeometry h;h.request=request;h.extent=plan->extent;h.activeWorldOffset=WorldOffset(request.activeRegion);
		h.vertices.reserve(plan->points.size());h.triangles.reserve(static_cast<std::size_t>(h.extent.triangleCount));h.clusters.reserve(static_cast<std::size_t>(h.extent.clusterCapacity));
		const auto bytes=SceneBytes(h);if(!bytes||*bytes+WorkBytes(*plan)>MaximumSurfaceHaloBytes){Fail(failure,SurfaceHaloBuildFailure::MemoryBudget);return std::nullopt;}
		for(auto p:plan->points){const auto v=SampleSurfaceWorldVertex(p.x,p.z);if(!v){Fail(failure,SurfaceHaloBuildFailure::SamplingFailed);return std::nullopt;}h.vertices.push_back(*v);}
		for(std::size_t i=0;i<plan->patches.size();++i){const auto &p=plan->patches[i];
			if(!i||!SameCluster(plan->patches[i-1],p))h.clusters.push_back({ClusterX(p),ClusterZ(p),p.step,static_cast<uint32_t>(h.triangles.size()),0,EmptyBounds()});
			if(!Triangulate(*plan,p,[&](SurfaceRegionTriangle t){h.triangles.push_back(t);for(auto id:t.vertices)ExpandBounds(h.clusters.back().bounds,h.vertices[id]);return true;})){Fail(failure,SurfaceHaloBuildFailure::InvalidGeneratedGeometry);return std::nullopt;}
			h.clusters.back().triangleCount=static_cast<uint32_t>(h.triangles.size())-h.clusters.back().firstTriangle;
		}
		h.retainedPayloadBytes=*SceneBytes(h);
		if(!ValidatePlan(h,*plan)){Fail(failure,SurfaceHaloBuildFailure::InvalidGeneratedGeometry);return std::nullopt;}
		Fail(failure,SurfaceHaloBuildFailure::None);return h;
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	}catch(...){Fail(failure,SurfaceHaloBuildFailure::AllocationFailed);return std::nullopt;}
#endif
}
bool ValidateSurfaceHalo(const SurfaceHaloGeometry &halo) noexcept {
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	try {
#endif
		const auto plan=MakePlan(halo.request,nullptr);return plan&&ValidatePlan(halo,*plan);
#if defined(__cpp_exceptions)||defined(_CPPUNWIND)
	}catch(...){return false;}
#endif
}
bool SurfaceHaloCoversWorldBounds(const SurfaceHaloGeometry &h,double minX,double minZ,double maxX,double maxZ,SurfaceHaloCompanions companions) noexcept {
	const SurfaceHaloWorldBounds b{minX,minZ,maxX,maxZ};if(!FastValid(h)||!ValidBounds(b)||!companions.activeSurfaceReady)return false;
	if(minX<double(h.extent.minWorldCellX)-1||minZ<double(h.extent.minWorldCellZ)-1||maxX>h.extent.maxWorldCellX||maxZ>h.extent.maxWorldCellZ)return false;
	const bool townVisible=minX<111&&maxX> -1&&minZ<111&&maxZ> -1;
	return !townVisible||companions.townSurfaceReady;
}
bool SurfaceHaloCoversCamera(const SurfaceHaloGeometry &h,const SurfaceHaloWorldBounds &b,SurfaceHaloCompanions companions) noexcept {
	return SurfaceHaloCoversWorldBounds(h,b.minX,b.minZ,b.maxX,b.maxZ,companions);
}
} // namespace devilution::surface

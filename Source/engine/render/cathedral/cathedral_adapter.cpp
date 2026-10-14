#include "cathedral_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace devilution::cathedral {
namespace {
constexpr float Height = 2.8F;
constexpr float Thickness = 0.18F;
constexpr uint64_t HashOffset = 14695981039346656037ULL;
constexpr uint64_t HashPrime = 1099511628211ULL;
void Mix(uint64_t &h, uint64_t value)
{
	for (int i = 0; i < 8; ++i) { h ^= (value >> (i * 8)) & 255; h *= HashPrime; }
}
Bounds EmptyBounds()
{
	const float inf = std::numeric_limits<float>::infinity();
	return { { inf, inf, inf }, { -inf, -inf, -inf } };
}
void Include(Bounds &b, Vec3 v)
{
	b.min = { std::min(b.min.x, v.x), std::min(b.min.y, v.y), std::min(b.min.z, v.z) };
	b.max = { std::max(b.max.x, v.x), std::max(b.max.y, v.y), std::max(b.max.z, v.z) };
}
void Quad(Module &m, Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
	const auto index = static_cast<uint32_t>(m.vertices.size());
	m.vertices.insert(m.vertices.end(), { {a,0,0}, {b,1,0}, {c,1,1}, {d,0,1} });
	m.indices.insert(m.indices.end(), {index,index+1,index+2,index,index+2,index+3});
}
void Box(Module &m, Vec3 a, Vec3 b)
{
	Quad(m,{a.x,a.y,a.z},{a.x,b.y,a.z},{b.x,b.y,a.z},{b.x,a.y,a.z});
	Quad(m,{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z},{a.x,a.y,b.z});
	Quad(m,{a.x,a.y,b.z},{a.x,b.y,b.z},{a.x,b.y,a.z},{a.x,a.y,a.z});
	Quad(m,{b.x,a.y,a.z},{b.x,b.y,a.z},{b.x,b.y,b.z},{b.x,a.y,b.z});
	Quad(m,{a.x,b.y,a.z},{a.x,b.y,b.z},{b.x,b.y,b.z},{b.x,b.y,a.z});
	Quad(m,{a.x,a.y,b.z},{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,a.y,b.z});
}
std::vector<Module> BuildModules()
{
	constexpr std::string_view Names[] = { "technical.cathedral.floor", "technical.cathedral.wall", "technical.cathedral.corner", "technical.cathedral.wall-end", "technical.cathedral.junction-3", "technical.cathedral.junction-4", "technical.cathedral.door-frame", "technical.cathedral.door-leaf", "technical.cathedral.stairs-up", "technical.cathedral.stairs-down", "technical.cathedral.native-detail", "technical.cathedral.arch-frame", "technical.cathedral.pillar", "technical.cathedral.fence", "technical.cathedral.wall-fragment", "technical.cathedral.join-fragment" };
	std::vector<Module> out;
	for (unsigned k = 0; k < static_cast<unsigned>(ModuleKind::Count); ++k) {
		Module m { static_cast<ModuleKind>(k), Names[k], 1, {}, {}, EmptyBounds() };
		switch (m.kind) {
		case ModuleKind::Floor: Quad(m,{-0.5F,0,-0.5F},{-0.5F,0,0.5F},{0.5F,0,0.5F},{0.5F,0,-0.5F}); break;
		case ModuleKind::Wall: Box(m,{-0.5F,0,-Thickness/2},{0.5F,Height,Thickness/2}); break;
		case ModuleKind::DoorFrame:
			Box(m,{-0.6F,0,-0.14F},{-0.5F,Height,0.14F});
			Box(m,{0.5F,0,-0.14F},{0.6F,Height,0.14F});
			Box(m,{-0.6F,2.25F,-0.14F},{0.6F,Height,0.14F}); break;
		case ModuleKind::DoorLeaf: Box(m,{0,0,-0.05F},{1,2.2F,0.05F}); break; // Hinge-local.
		case ModuleKind::ArchFrame:
			Box(m,{-1.1F,0,-0.14F},{-0.95F,Height,0.14F});
			Box(m,{0.95F,0,-0.14F},{1.1F,Height,0.14F});
			Box(m,{-1.1F,2.4F,-0.14F},{1.1F,Height,0.14F}); break;
		case ModuleKind::Pillar: Box(m,{-0.25F,0,-0.25F},{0.25F,Height,0.25F}); break;
		case ModuleKind::Fence:
			Box(m,{-1,0.75F,-0.06F},{1,0.9F,0.06F});
			Box(m,{-1,1.25F,-0.06F},{1,1.4F,0.06F});
			for(int bar=0;bar<5;++bar){const float x=-0.96F+bar*0.48F;Box(m,{x,0,-0.04F},{x+0.08F,1.5F,0.04F});} break;
		case ModuleKind::StairsUp:
		case ModuleKind::StairsDown:
			for (int step = 0; step < 8; ++step) {
				const float y = static_cast<float>(step+1)*0.3F;
				const float z = -static_cast<float>(step)*0.5F;
				if (m.kind == ModuleKind::StairsUp) Box(m,{-1,0,z-0.5F},{1,y,z});
				else Box(m,{-1,-y-0.3F,z-0.5F},{1,-y,z});
			} break;
		case ModuleKind::NativeDetail: Box(m,{-0.15F,0,-0.15F},{0.15F,0.5F,0.15F}); break;
		case ModuleKind::WallFragment:
		case ModuleKind::JoinFragment:
			m.revision=2; Box(m,{0,0,0},{1,1,1}); break;
		default: Box(m,{-Thickness/2,0,-Thickness/2},{Thickness/2,Height,Thickness/2}); break;
		}
		for (const auto &v : m.vertices) Include(m.bounds,v.position);
		out.push_back(std::move(m));
	}
	return out;
}
bool Active(int x, int z) { return x >= ActiveMin && z >= ActiveMin && x < ActiveMax && z < ActiveMax; }
const Door *DoorAt(const Snapshot &s, int x, int z)
{
	for (const Door &d : s.doors) if (d.x == x && d.z == z) return &d;
	return nullptr;
}
bool Space(const Snapshot &s, int x, int z)
{
	return Active(x,z) && ((s.At(x,z).properties & Solid) == 0 || DoorAt(s,x,z) != nullptr);
}
uint64_t Identity(ModuleKind kind, int x, int z, int sub)
{
	return (static_cast<uint64_t>(kind)+1) << 48 | static_cast<uint64_t>(z+128) << 24 | static_cast<uint64_t>(x+128) << 8 | static_cast<uint64_t>(sub);
}
void Add(Region &r, const Snapshot &s, ModuleKind kind, int ownerX, int ownerZ, Vec3 at, int rotation, int sub, PickBinding pick)
{
	const Cell &cell = s.At(ownerX,ownerZ);
	Instance i { Identity(kind,ownerX,ownerZ,sub), kind, at, static_cast<uint8_t>(rotation%4), pick,
		cell.transparency, cell.piece, s.mega[static_cast<std::size_t>((ownerZ-16)/2)*40+(ownerX-16)/2], EmptyBounds() };
	i.sourceModule=kind; i.sourceInstanceId=i.id;
	const Bounds b = TechnicalModules()[static_cast<std::size_t>(kind)].bounds;
	for (int bits=0;bits<8;++bits) Include(i.bounds,Transform(i,{bits&1?b.max.x:b.min.x,bits&2?b.max.y:b.min.y,bits&4?b.max.z:b.min.z}));
	Include(r.bounds,i.bounds.min); Include(r.bounds,i.bounds.max);
	r.instances.push_back(i);
}
bool PositiveOverlap(const Bounds &a, const Bounds &b)
{
	return a.min.x<b.max.x&&a.max.x>b.min.x&&a.min.y<b.max.y&&a.max.y>b.min.y&&a.min.z<b.max.z&&a.max.z>b.min.z;
}
void AppendBox(std::vector<Bounds> &out, Bounds b)
{
	if(b.min.x<b.max.x&&b.min.y<b.max.y&&b.min.z<b.max.z) out.push_back(b);
}
// Six closed, non-overlapping boxes exactly partition original minus cut. Each
// shared cube supplies the newly exposed faces; there are no uncapped wall ends.
void SubtractBox(std::vector<Bounds> &out, Bounds original, const Bounds &cut)
{
	if(!PositiveOverlap(original,cut)) {out.push_back(original);return;}
	const Bounds intersection {{std::max(original.min.x,cut.min.x),std::max(original.min.y,cut.min.y),std::max(original.min.z,cut.min.z)},
		{std::min(original.max.x,cut.max.x),std::min(original.max.y,cut.max.y),std::min(original.max.z,cut.max.z)}};
	AppendBox(out,{original.min,{intersection.min.x,original.max.y,original.max.z}});
	AppendBox(out,{{intersection.max.x,original.min.y,original.min.z},original.max});
	original.min.x=intersection.min.x;original.max.x=intersection.max.x;
	AppendBox(out,{original.min,{original.max.x,intersection.min.y,original.max.z}});
	AppendBox(out,{{original.min.x,intersection.max.y,original.min.z},original.max});
	original.min.y=intersection.min.y;original.max.y=intersection.max.y;
	AppendBox(out,{original.min,{original.max.x,original.max.y,intersection.min.z}});
	AppendBox(out,{{original.min.x,original.min.y,intersection.max.z},original.max});
}
void ReserveOpenDoorSockets(Region &region, const Snapshot &snapshot)
{
	std::vector<Instance> fitted;
	for(const Instance &original:region.instances) {
		if(original.module<ModuleKind::Wall||original.module>ModuleKind::Junction4) {fitted.push_back(original);continue;}
		std::vector<Bounds> pieces {original.bounds};bool changed=false;
		for(const Door &door:snapshot.doors) {
			if(door.state==DoorState::Closed) continue;
			const bool left=door.orientation==DoorOrientation::Left;
			// The actual leaf defines eligibility. The wider technical collar must
			// never recruit a neighboring primitive that did not hide that leaf.
			const Bounds leaf=left?Bounds{{door.x-1.0F,0,door.z-0.55F},{static_cast<float>(door.x),2.2F,door.z-0.45F}}
				:Bounds{{door.x-0.55F,0,static_cast<float>(door.z)},{door.x-0.45F,2.2F,door.z+1.0F}};
			if(!PositiveOverlap(original.bounds,leaf)) continue;
			Bounds socket=left?Bounds{{leaf.min.x-0.02F,0,door.z-0.75F},{leaf.max.x+0.02F,2.25F,door.z-0.25F}}
				:Bounds{{door.x-0.75F,0,leaf.min.z-0.02F},{door.x-0.25F,2.25F,leaf.max.z+0.02F}};
			// A parallel segment or join loses its entire transverse thickness.
			// A perpendicular segment keeps its longitudinal part beyond the pocket.
			if(left&&original.bounds.max.z-original.bounds.min.z<=Thickness+0.0001F) {
				socket.min.z=std::min(socket.min.z,original.bounds.min.z);socket.max.z=std::max(socket.max.z,original.bounds.max.z);
			} else if(!left&&original.bounds.max.x-original.bounds.min.x<=Thickness+0.0001F) {
				socket.min.x=std::min(socket.min.x,original.bounds.min.x);socket.max.x=std::max(socket.max.x,original.bounds.max.x);
			}
			std::vector<Bounds> remainder;
			for(const Bounds &piece:pieces) SubtractBox(remainder,piece,socket);
			pieces=std::move(remainder);changed=true;
		}
		if(!changed) {fitted.push_back(original);continue;}
		if(pieces.size()>256) throw std::runtime_error("Technical socket fragment identity capacity exceeded");
		const ModuleKind kind=original.module==ModuleKind::Wall?ModuleKind::WallFragment:ModuleKind::JoinFragment;
		for(std::size_t part=0;part<pieces.size();++part) {
			const Bounds &b=pieces[part];Instance fragment=original;
			fragment.id=Identity(kind,original.pick.x,original.pick.z,static_cast<int>(original.id&255))
				|static_cast<uint64_t>(original.module)<<40|static_cast<uint64_t>(part)<<32;
			fragment.module=kind;fragment.quarterTurns=0;
			// A minimum-anchored unit box reconstructs both float endpoints exactly;
			// a center plus half-size can move a thin wall cap by one coordinate ULP.
			fragment.translation=b.min;
			fragment.scale={b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z};
			// Recompute from the actual scaled vertices, including float rounding.
			fragment.bounds=EmptyBounds();
			for(const Vertex &vertex:TechnicalModules()[static_cast<std::size_t>(kind)].vertices) Include(fragment.bounds,Transform(fragment,vertex.position));
			fitted.push_back(fragment);
		}
	}
	region.instances=std::move(fitted);
}
uint64_t Signature(const Snapshot &s, int rx, int rz)
{
	uint64_t h = HashOffset;
	Mix(h,s.gameRevision); Mix(h,s.level); Mix(h,2); // Technical socket geometry revision.
	// Two-cell halo includes door leaf neighbor, native DoorSet mutation and endpoint joins.
	for (int z=std::max(0,rz-2);z<std::min(GridSize,rz+RegionSize+2);++z)
		for (int x=std::max(0,rx-2);x<std::min(GridSize,rx+RegionSize+2);++x) {
			const Cell &c=s.At(x,z);
			Mix(h,c.piece); Mix(h,c.properties); Mix(h,c.transparency); Mix(h,static_cast<uint8_t>(c.special)); Mix(h,static_cast<uint8_t>(c.object));
			if (Active(x,z)) Mix(h,s.mega[static_cast<std::size_t>((z-16)/2)*40+(x-16)/2]);
		}
	for (const Door &d:s.doors) if(d.x>=rx-2&&d.x<rx+10&&d.z>=rz-2&&d.z<rz+10) {
		Mix(h,d.slot); Mix(h,d.x); Mix(h,d.z); Mix(h,static_cast<uint8_t>(d.orientation)); Mix(h,static_cast<uint8_t>(d.state)); Mix(h,d.selectable); Mix(h,d.animationFrame);
	}
	// Trigger indices are native bindings. Reordering even a distant entry can
	// change a local stair's slot, so hash the ordered list, not just nearby values.
	for (std::size_t slot=0;slot<s.triggers.size();++slot) {const auto &t=s.triggers[slot];Mix(h,slot);Mix(h,t.x);Mix(h,t.z);Mix(h,t.message);Mix(h,t.targetLevel);}
	// Stair holes cross region bounds by four cells; changing stair topology is
	// rare and deliberately invalidates all regions rather than leaving a floor cap.
	for (std::size_t n=0;n<s.mega.size();++n) if(s.mega[n]==64||s.mega[n]==59) {Mix(h,n);Mix(h,s.mega[n]);}
	return h;
}
struct Stair { int x,z; bool up; PickBinding pick; };
std::vector<Stair> Stairs(const Snapshot &s)
{
	std::vector<Stair> out;
	for(int z=0;z<40;++z) for(int x=0;x<40;++x) {
		const uint8_t id=s.mega[static_cast<std::size_t>(z)*40+x];
		if(id!=64&&id!=59) continue;
		// Explicit Cathedral miniset center IDs; don't classify arbitrary SOL cells as stairs.
		const bool up=id==64;
		const int wx=16+x*2, wz=16+z*2;
		PickBinding pick {PickKind::Architecture,wx,wz,-1};
		int nearest=9999;
		for(std::size_t slot=0;slot<s.triggers.size();++slot) {
			const Trigger &t=s.triggers[slot];
			const int distance=std::abs(t.x-wx)+std::abs(t.z-wz);
			if(distance<nearest&&distance<=8) {nearest=distance;pick={PickKind::Trigger,t.x,t.z,static_cast<int>(slot)};}
		}
		out.push_back({wx,wz,up,pick});
	}
	return out;
}
bool StairHole(const std::vector<Stair> &stairs,int x,int z)
{
	// Remove every unit floor square with positive-area overlap with the full
	// technical descent bounds. A two-cell cut caps the right/first tread at y=0.
	for(const Stair &st:stairs) if(!st.up&&x+0.5F>st.x-1&&x-0.5F<st.x+1&&z+0.5F>st.z-4&&z-0.5F<st.z) return true;
	return false;
}
std::shared_ptr<const Region> BuildRegion(const Snapshot &s, int rx, int rz, uint64_t signature)
{
	auto r=std::make_shared<Region>(); r->x=rx;r->z=rz;r->geometrySignature=signature;r->bounds=EmptyBounds();
	const auto stairs=Stairs(s);
	// Canonical wall graph: keys are twice the native coordinate (half-cell nodes).
	std::map<std::pair<int,int>,unsigned> nodes;
	constexpr int DX[]={0,1,0,-1}, DZ[]={-1,0,1,0};
	for(int z=std::max(16,rz-1);z<std::min(96,rz+9);++z) for(int x=std::max(16,rx-1);x<std::min(96,rx+9);++x) {
		if(!Space(s,x,z)) continue;
		const bool own=x>=rx&&x<rx+8&&z>=rz&&z<rz+8;
		if(own&&!StairHole(stairs,x,z)) Add(*r,s,ModuleKind::Floor,x,z,{static_cast<float>(x),0,static_cast<float>(z)},0,0,{PickKind::Ground,x,z,-1});
		for(int edge=0;edge<4;++edge) if(!Space(s,x+DX[edge],z+DZ[edge])) {
			const int cx=2*x+DX[edge],cz=2*z+DZ[edge];
			if(edge%2==0) {nodes[{cx-1,cz}]|=2;nodes[{cx+1,cz}]|=8;}
			else {nodes[{cx,cz-1}]|=4;nodes[{cx,cz+1}]|=1;}
			if(own) Add(*r,s,ModuleKind::Wall,x,z,{cx/2.0F,0,cz/2.0F},edge%2,edge,{PickKind::Architecture,x,z,-1});
		}
	}
	for(const auto &[key,mask]:nodes) {
		const int ownerX=std::clamp((key.first+1)/2,16,95),ownerZ=std::clamp((key.second+1)/2,16,95);
		if(ownerX<rx||ownerX>=rx+8||ownerZ<rz||ownerZ>=rz+8) continue;
		const int degree=((mask&1)!=0)+((mask&2)!=0)+((mask&4)!=0)+((mask&8)!=0);
		if(degree==2&&(mask==5||mask==10)) continue;
		const ModuleKind kind=degree==1?ModuleKind::WallEnd:degree==2?ModuleKind::Corner:degree==3?ModuleKind::Junction3:ModuleKind::Junction4;
		const int sub=(key.first==2*ownerX+1?1:0)+(key.second==2*ownerZ+1?2:0);
		Add(*r,s,kind,ownerX,ownerZ,{key.first/2.0F,0,key.second/2.0F},0,sub,{PickKind::Architecture,ownerX,ownerZ,-1});
	}
	ReserveOpenDoorSockets(*r,s);
	for(const Door &d:s.doors) if(d.x>=rx&&d.x<rx+8&&d.z>=rz&&d.z<rz+8) {
		const int rotation=d.orientation==DoorOrientation::Left?1:0;
		const PickBinding framePick {PickKind::Architecture,d.x,d.z,-1};
		const PickBinding leafPick=d.selectable?PickBinding{PickKind::Object,d.x,d.z,d.slot}:framePick;
		Add(*r,s,ModuleKind::DoorFrame,d.x,d.z,{static_cast<float>(d.x),0,static_cast<float>(d.z)},rotation,0,framePick);
		const Vec3 hinge=rotation==0?Vec3{d.x-0.5F,0,static_cast<float>(d.z)}:Vec3{static_cast<float>(d.x),0,d.z-0.5F};
		Add(*r,s,ModuleKind::DoorLeaf,d.x,d.z,hinge,rotation+(d.state==DoorState::Closed?0:1),0,leafPick);
	}
	// Exact raw tile semantics only. Decoration/shadow tables deliberately aren't
	// used to infer doors/fences: those tables collapse distinct native structures.
	for(int z=rz;z<rz+8;z+=2)for(int x=rx;x<rx+8;x+=2) {
		const auto raw=s.mega[static_cast<std::size_t>((z-16)/2)*40+(x-16)/2];
		const PickBinding pick {PickKind::Architecture,x,z,-1};
		if(raw==15) Add(*r,s,ModuleKind::Pillar,x,z,{x+0.5F,0,z+0.5F},0,0,pick);
		else if(raw==11||raw==12) Add(*r,s,ModuleKind::ArchFrame,x,z,{x+0.5F,0,z+0.5F},raw==11?1:0,0,pick);
		else if(raw==35||raw==36) Add(*r,s,ModuleKind::Fence,x,z,{x+0.5F,0,z+0.5F},raw==35?1:0,0,pick);
	}
	for(const Stair &st:stairs) if(st.x>=rx&&st.x<rx+8&&st.z>=rz&&st.z<rz+8)
		Add(*r,s,st.up?ModuleKind::StairsUp:ModuleKind::StairsDown,st.x,st.z,{static_cast<float>(st.x),0,static_cast<float>(st.z)},0,0,st.pick);
	r->bounds=EmptyBounds();
	for(const Instance &instance:r->instances) {Include(r->bounds,instance.bounds.min);Include(r->bounds,instance.bounds.max);}
	if(r->instances.empty()) r->bounds={{static_cast<float>(rx),0,static_cast<float>(rz)},{static_cast<float>(rx+8),0,static_cast<float>(rz+8)}};
	return r;
}
void Validate(const Snapshot &s)
{
	if(s.level!=1||s.hellfire) throw std::invalid_argument("Technical pilot supports retail Cathedral level 1 only");
	if(s.levelType!=1||s.setLevel||s.setLevelId!=0||s.minX!=16||s.minZ!=16||s.maxX!=96||s.maxZ!=96||s.gameRevision==0)
		throw std::invalid_argument("Ineligible native map type, set level, active bounds or epoch");
	if(s.micros.size()>1379) throw std::invalid_argument("Invalid native micros count");
	for(const Cell &c:s.cells) if(c.piece>=1379||c.object==-128) throw std::invalid_argument("Invalid native cell");
	for(uint8_t raw:s.mega) if(raw>206) throw std::invalid_argument("Invalid Cathedral mega tile");
	std::set<int> slots;std::set<std::pair<int,int>> positions;
	for(const Door &d:s.doors) {
		if(!Active(d.x,d.z)||d.slot<0||d.slot>=127||!slots.insert(d.slot).second||!positions.insert({d.x,d.z}).second||static_cast<unsigned>(d.orientation)>1||static_cast<unsigned>(d.state)>2)
			throw std::invalid_argument("Invalid native door binding");
		if(std::abs(static_cast<int>(s.At(d.x,d.z).object))!=d.slot+1) throw std::invalid_argument("Door does not match native occupancy");
	}
	for(const Trigger &t:s.triggers) if(!Active(t.x,t.z)) throw std::invalid_argument("Invalid native trigger binding");
}
Vec3 Sub(Vec3 a,Vec3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Cross(Vec3 a,Vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
} // namespace

const std::vector<Module> &TechnicalModules(){static const auto modules=BuildModules();return modules;}
Vec3 Transform(const Instance &i, Vec3 v)
{
	v={v.x*i.scale.x,v.y*i.scale.y,v.z*i.scale.z};
	for(unsigned q=0;q<i.quarterTurns;++q) {const float x=v.x;v.x=-v.z;v.z=x;}
	return{v.x+i.translation.x,v.y+i.translation.y,v.z+i.translation.z};
}
Scene Adapter::Update(const Snapshot &s)
{
	Validate(s); Scene out;out.gameRevision=s.gameRevision;out.presentationSignature=HashOffset;
	for(const Cell &c:s.cells){Mix(out.presentationSignature,c.light);Mix(out.presentationSignature,c.flags);}
	for(bool active:s.activeTransparency)Mix(out.presentationSignature,active);
	std::size_t slot=0;
	for(int z=16;z<96;z+=8)for(int x=16;x<96;x+=8,++slot) {
		const uint64_t signature=Signature(s,x,z);
		if(previous_.gameRevision==s.gameRevision&&slot<previous_.regions.size()&&previous_.regions[slot]->geometrySignature==signature)
			out.regions.push_back(previous_.regions[slot]);
		else {out.regions.push_back(BuildRegion(s,x,z,signature));++out.rebuiltRegions;}
	}
	out.revision=out.rebuiltRegions||previous_.regions.empty()?nextRevision_++:previous_.revision;
	previous_=out;return out;
}
void Adapter::Reset(){previous_=Scene{};}
bool Intersects(const Bounds &a,const Bounds &b)
{
	for(const Bounds *v:{&a,&b})if(!std::isfinite(v->min.x)||!std::isfinite(v->min.y)||!std::isfinite(v->min.z)||!std::isfinite(v->max.x)||!std::isfinite(v->max.y)||!std::isfinite(v->max.z)||v->min.x>v->max.x||v->min.y>v->max.y||v->min.z>v->max.z)return false;
	return a.min.x<=b.max.x&&a.max.x>=b.min.x&&a.min.y<=b.max.y&&a.max.y>=b.min.y&&a.min.z<=b.max.z&&a.max.z>=b.min.z;
}
std::vector<const Instance *> VisibleInstances(const Scene &s,const Bounds &b)
{
	std::vector<const Instance *> out;
	for(const auto &r:s.regions) if(Intersects(r->bounds,b)) for(const auto &i:r->instances)if(Intersects(i.bounds,b))out.push_back(&i);
	return out;
}
std::optional<PickResult> RayPick(const Scene &s,Vec3 origin,Vec3 direction)
{
	if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z)||!std::isfinite(direction.x)||!std::isfinite(direction.y)||!std::isfinite(direction.z))return std::nullopt;
	const float length=std::sqrt(Dot(direction,direction));if(!std::isfinite(length)||length<=1e-10F)return std::nullopt;
	direction={direction.x/length,direction.y/length,direction.z/length};
	std::optional<PickResult> best;
	for(const auto &r:s.regions)for(const Instance &i:r->instances) {
		const Module &m=TechnicalModules()[static_cast<std::size_t>(i.module)];
		for(std::size_t j=0;j<m.indices.size();j+=3) {
			const Vec3 a=Transform(i,m.vertices[m.indices[j]].position),b=Transform(i,m.vertices[m.indices[j+1]].position),c=Transform(i,m.vertices[m.indices[j+2]].position);
			const Vec3 e1=Sub(b,a),e2=Sub(c,a),p=Cross(direction,e2);const float determinant=Dot(e1,p);
			if(!std::isfinite(determinant)||std::abs(determinant)<1e-7F)continue;
			const float inv=1/determinant;const Vec3 t=Sub(origin,a);const float u=Dot(t,p)*inv;if(!std::isfinite(u)||u<0||u>1)continue;
			const Vec3 q=Cross(t,e1);const float v=Dot(direction,q)*inv;if(!std::isfinite(v)||v<0||u+v>1)continue;
			const float distance=Dot(e2,q)*inv;if(!std::isfinite(distance)||distance<0||(best&&distance>=best->distance))continue;
			best=PickResult{i.pick,i.id,distance};
		}
	}
	return best;
}
Snapshot ReadSnapshot(const char *path)
{
	std::ifstream f(path);f.exceptions(std::ios::failbit|std::ios::badbit);
	std::string magic;int version=0;f>>magic>>version;
	if(magic!="D3D_CATHEDRAL_SNAPSHOT"||version!=2)throw std::runtime_error("Invalid snapshot header");
	Snapshot s;int level,entry;f>>s.seed>>s.rngState>>s.gameRevision>>level>>entry>>s.originalCathedral>>s.fullQuests>>s.hellfire;
	f>>s.levelType>>s.setLevel>>s.setLevelId>>s.minX>>s.minZ>>s.maxX>>s.maxZ;
	if(level<0||level>255||entry<0||entry>7)throw std::runtime_error("Invalid snapshot context");s.level=static_cast<uint8_t>(level);s.entry=static_cast<uint8_t>(entry);
	for(auto &v:s.mega){int n;f>>n;if(n<0||n>206)throw std::runtime_error("Invalid mega ID");v=static_cast<uint8_t>(n);}
	for(auto &c:s.cells){int piece,properties,trans,special,light,flags,object;f>>piece>>properties>>trans>>special>>light>>flags>>object;
		if(piece<0||piece>=1379||properties<0||properties>255||trans<0||trans>255||special<-128||special>127||light<0||light>255||flags<0||flags>255||object<-127||object>127)throw std::runtime_error("Invalid cell data");
		c={static_cast<uint16_t>(piece),static_cast<uint8_t>(properties),static_cast<uint8_t>(trans),static_cast<int8_t>(special),static_cast<uint8_t>(light),static_cast<uint8_t>(flags),static_cast<int8_t>(object)};
	}
	std::size_t count;f>>count;if(count>1379)throw std::runtime_error("Invalid micros count");s.micros.resize(count);
	for(auto &micros:s.micros)for(auto &v:micros){unsigned n;f>>n;if(n>65535)throw std::runtime_error("Invalid micro word");v=static_cast<uint16_t>(n);}
	for(auto &active:s.activeTransparency)f>>active;
	f>>count;if(count>127)throw std::runtime_error("Invalid door count");s.doors.resize(count);
	for(auto &d:s.doors){int orientation,state;f>>d.slot>>d.x>>d.z>>orientation>>state>>d.selectable>>d.animationFrame>>d.originalPiece>>d.originalNeighborPiece;
		if(orientation<0||orientation>1||state<0||state>2)throw std::runtime_error("Invalid door state");d.orientation=static_cast<DoorOrientation>(orientation);d.state=static_cast<DoorState>(state);}
	f>>count;if(count>256)throw std::runtime_error("Invalid trigger count");s.triggers.resize(count);
	for(auto &t:s.triggers)f>>t.x>>t.z>>t.message>>t.targetLevel;
	f.exceptions(std::ios::badbit);std::string extra;if(f>>extra)throw std::runtime_error("Trailing snapshot content");Validate(s);return s;
}
} // namespace devilution::cathedral

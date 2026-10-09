#include "cathedral_frame.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace devilution::cathedral {
namespace {
constexpr uint8_t NativeVisible = 1U << 1;
constexpr uint8_t NativeTransparent = 1U << 3;
constexpr uint8_t NativeTransparentLeft = 1U << 4;
constexpr uint8_t NativeTransparentRight = 1U << 5;
constexpr float BoundsTolerance = 0.00002F;
constexpr uint64_t HashOffset = 14695981039346656037ULL;
constexpr uint64_t HashPrime = 1099511628211ULL;

void Require(bool condition, const char *reason)
{
	if (!condition) throw std::invalid_argument(reason);
}
bool Active(int x, int z)
{
	return x >= ActiveMin && x < ActiveMax && z >= ActiveMin && z < ActiveMax;
}
bool Finite(Vec3 v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool ValidBounds(const Bounds &b)
{
	return Finite(b.min) && Finite(b.max) && b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z;
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
bool Near(Vec3 a, Vec3 b)
{
	return std::abs(a.x - b.x) <= BoundsTolerance && std::abs(a.y - b.y) <= BoundsTolerance && std::abs(a.z - b.z) <= BoundsTolerance;
}
bool Near(const Bounds &a, const Bounds &b)
{
	return ValidBounds(a) && ValidBounds(b) && Near(a.min, b.min) && Near(a.max, b.max);
}
void Mix(uint64_t &hash, uint64_t value)
{
	for (int i = 0; i < 8; ++i) { hash ^= (value >> (i * 8)) & 255; hash *= HashPrime; }
}
uint64_t PresentationSignature(const Snapshot &s)
{
	uint64_t hash = HashOffset;
	for (const Cell &cell : s.cells) { Mix(hash, cell.light); Mix(hash, cell.flags); }
	for (bool active : s.activeTransparency) Mix(hash, active);
	return hash;
}
void ValidateSnapshot(const Snapshot &s)
{
	Require(s.level == 1 && !s.hellfire && s.levelType == 1 && !s.setLevel && s.setLevelId == 0,
	    "Frame requires retail Cathedral level 1");
	Require(s.gameRevision != 0 && s.minX == ActiveMin && s.minZ == ActiveMin && s.maxX == ActiveMax && s.maxZ == ActiveMax,
	    "Frame snapshot active bounds or epoch invalid");
	Require(s.micros.size() <= 1379, "Frame snapshot micros count invalid");
	for (const Cell &cell : s.cells) {
		Require(cell.piece < 1379 && cell.object != -128, "Frame snapshot native cell invalid");
		Require(cell.light <= 15, "Frame snapshot native light outside 0..15");
	}
	for (uint8_t raw : s.mega) Require(raw <= 206, "Frame snapshot mega tile invalid");
	std::set<int> slots;
	std::set<std::pair<int, int>> positions;
	for (const Door &door : s.doors) {
		Require(Active(door.x, door.z) && door.slot >= 0 && door.slot < 127
		        && slots.insert(door.slot).second && positions.emplace(door.x, door.z).second
		        && static_cast<unsigned>(door.orientation) <= 1 && static_cast<unsigned>(door.state) <= 2,
		    "Frame snapshot native door invalid");
		Require(std::abs(static_cast<int>(s.At(door.x, door.z).object)) == door.slot + 1,
		    "Frame snapshot door occupancy mismatch");
	}
	for (const Trigger &trigger : s.triggers) Require(Active(trigger.x, trigger.z), "Frame snapshot native trigger invalid");
}
void ValidateModules(const std::vector<Module> &modules)
{
	Require(modules.size() == static_cast<size_t>(ModuleKind::Count), "Frame technical module count invalid");
	for (size_t n = 0; n < modules.size(); ++n) {
		const Module &module = modules[n];
		Require(static_cast<size_t>(module.kind) == n && module.revision == (n < 14 ? 1U : 2U)
		        && !module.technicalId.empty() && !module.vertices.empty() && !module.indices.empty() && module.indices.size() % 3 == 0,
		    "Frame technical module contract invalid");
		Bounds actual = EmptyBounds();
		for (const Vertex &vertex : module.vertices) {
			Require(Finite(vertex.position) && std::isfinite(vertex.u) && std::isfinite(vertex.v), "Frame module vertex nonfinite");
			Include(actual, vertex.position);
		}
		for (uint32_t index : module.indices) Require(index < module.vertices.size(), "Frame module index out of range");
		Require(Near(actual, module.bounds), "Frame module bounds disagree with vertices");
	}
}
std::pair<int, int> Owner(const Instance &instance)
{
	// The accepted adapter identity encodes the native owner. Stair picks may
	// point at a nearby native trigger rather than the stair module's own cell.
	return { static_cast<int>((instance.sourceInstanceId >> 8) & 65535) - 128,
		static_cast<int>((instance.sourceInstanceId >> 24) & 255) - 128 };
}
void ValidatePick(const Snapshot &s, const Instance &instance, int ownerX, int ownerZ)
{
	const PickBinding &pick = instance.pick;
	Require(static_cast<unsigned>(pick.kind) <= static_cast<unsigned>(PickKind::Trigger) && Active(pick.x, pick.z),
	    "Frame native pick kind or coordinate invalid");
	if (pick.kind == PickKind::Ground || pick.kind == PickKind::Architecture) {
		Require(pick.nativeSlot == -1, "Frame ground/architecture pick must not carry a native slot");
	} else if (pick.kind == PickKind::Object) {
		const auto door = std::find_if(s.doors.begin(), s.doors.end(), [&](const Door &d) {
			return d.slot == pick.nativeSlot && d.x == pick.x && d.z == pick.z && d.selectable;
		});
		Require(instance.module == ModuleKind::DoorLeaf && door != s.doors.end(), "Frame object pick does not match selectable native door");
	} else {
		Require((instance.module == ModuleKind::StairsUp || instance.module == ModuleKind::StairsDown)
		        && pick.nativeSlot >= 0 && static_cast<size_t>(pick.nativeSlot) < s.triggers.size(),
		    "Frame trigger pick slot or module invalid");
		const Trigger &trigger = s.triggers[static_cast<size_t>(pick.nativeSlot)];
		Require(pick.x == trigger.x && pick.z == trigger.z, "Frame trigger pick differs from native target");
	}
	Require((instance.module == ModuleKind::Floor) == (pick.kind == PickKind::Ground), "Frame floor/ground pick contract invalid");
	if (pick.kind != PickKind::Trigger) Require(pick.x == ownerX && pick.z == ownerZ, "Frame pick differs from native owner");
	if (instance.module == ModuleKind::DoorFrame || instance.module == ModuleKind::DoorLeaf) {
		const auto door = std::find_if(s.doors.begin(), s.doors.end(), [&](const Door &d) { return d.x == ownerX && d.z == ownerZ; });
		Require(door != s.doors.end(), "Frame door geometry missing native door");
		const unsigned quarter = door->orientation == DoorOrientation::Left ? 1U : 0U;
		if (instance.module == ModuleKind::DoorFrame) {
			Require(pick.kind == PickKind::Architecture && instance.quarterTurns == quarter
			        && Near(instance.translation, { static_cast<float>(ownerX), 0, static_cast<float>(ownerZ) }),
			    "Frame door frame transform differs from native orientation");
		} else {
			const Vec3 hinge = quarter == 0 ? Vec3 { ownerX - 0.5F, 0, static_cast<float>(ownerZ) }
			                              : Vec3 { static_cast<float>(ownerX), 0, ownerZ - 0.5F };
			Require(instance.quarterTurns == quarter + (door->state == DoorState::Closed ? 0U : 1U)
			        && Near(instance.translation, hinge)
			        && pick.kind == (door->selectable ? PickKind::Object : PickKind::Architecture),
			    "Frame door leaf transform/pick differs from native state");
		}
	}
}
Bounds ValidateInstance(const Snapshot &s, const Region &region, const Instance &instance, const std::vector<Module> &modules)
{
	Require(static_cast<size_t>(instance.module) < modules.size() && static_cast<size_t>(instance.sourceModule) < modules.size(),
	    "Frame instance module out of range");
	Require(instance.id != 0 && instance.sourceInstanceId != 0 && instance.quarterTurns < 4 && Finite(instance.translation)
	        && Finite(instance.scale) && instance.scale.x > 0 && instance.scale.y > 0 && instance.scale.z > 0,
	    "Frame instance identity, transform or scale invalid");
	const bool fragment = instance.module == ModuleKind::WallFragment || instance.module == ModuleKind::JoinFragment;
	if (fragment) {
		Require(instance.quarterTurns == 0 && instance.sourceInstanceId != instance.id
		        && (instance.module == ModuleKind::WallFragment ? instance.sourceModule == ModuleKind::Wall
		                                                       : instance.sourceModule >= ModuleKind::Corner && instance.sourceModule <= ModuleKind::Junction4)
		        && (instance.id & 0xFFFFFFFFULL) == (instance.sourceInstanceId & 0xFFFFFFFFULL)
		        && ((instance.id >> 40) & 255) == static_cast<uint64_t>(instance.sourceModule),
		    "Frame fragment source identity invalid");
	} else {
		Require(instance.sourceModule == instance.module && instance.sourceInstanceId == instance.id
		        && instance.scale.x == 1 && instance.scale.y == 1 && instance.scale.z == 1,
		    "Frame original module source identity or scale invalid");
	}
	Require(instance.id >> 48 == static_cast<uint64_t>(instance.module) + 1
	        && instance.sourceInstanceId >> 48 == static_cast<uint64_t>(instance.sourceModule) + 1
	        && ((instance.sourceInstanceId >> 32) & 65535) == 0, "Frame instance identity namespace invalid");
	const auto [ownerX, ownerZ] = Owner(instance);
	Require(Active(ownerX, ownerZ) && ownerX >= region.x && ownerX < region.x + RegionSize
	        && ownerZ >= region.z && ownerZ < region.z + RegionSize, "Frame instance owner outside region");
	ValidatePick(s, instance, ownerX, ownerZ);
	const Cell &cell = s.At(ownerX, ownerZ);
	Require(instance.nativePiece == cell.piece && instance.transparency == cell.transparency
	        && instance.nativeMega == s.mega[static_cast<size_t>((ownerZ - ActiveMin) / 2) * MegaSize + (ownerX - ActiveMin) / 2],
	    "Frame instance differs from native owner cell");
	Bounds actual = EmptyBounds();
	for (const Vertex &vertex : modules[static_cast<size_t>(instance.module)].vertices) {
		const Vec3 world = Transform(instance, vertex.position);
		Require(Finite(world), "Frame transformed vertex nonfinite");
		Include(actual, world);
	}
	Require(Near(actual, instance.bounds), "Frame instance bounds disagree with transformed vertices");
	return actual;
}
TownSceneTriangle CollisionTriangle(const FrameTriangle &triangle)
{
	TownSceneTriangle out {};
	out.material = TownSceneMaterial::Stone;
	out.sourceTile = out.pickTile = { triangle.pick.x, triangle.pick.z };
	for (size_t n = 0; n < 3; ++n) {
		const Vertex &v = triangle.vertices[n];
		out.vertices[n] = { v.position.x, v.position.y, v.position.z, v.u, v.v };
	}
	const Vec3 a = triangle.vertices[0].position, b = triangle.vertices[1].position, c = triangle.vertices[2].position;
	const Vec3 ab { b.x - a.x, b.y - a.y, b.z - a.z }, ac { c.x - a.x, c.y - a.y, c.z - a.z };
	const Vec3 normal { ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
	const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
	Require(std::isfinite(length) && length > 0, "Frame triangle is degenerate");
	out.normal = { normal.x / length, normal.y / length, normal.z / length };
	return out;
}

bool NativeSpace(const Snapshot &snapshot, int x, int z)
{
	if (!Active(x, z)) return false;
	if ((snapshot.At(x, z).properties & Solid) == 0) return true;
	return std::any_of(snapshot.doors.begin(), snapshot.doors.end(), [&](const Door &door) { return door.x == x && door.z == z; });
}

NativeSurfaceSource NativeSource(const Snapshot &snapshot, int x, int z)
{
	const Cell &cell = snapshot.At(x, z);
	NativeSurfaceSource source;
	source.x = x; source.z = z; source.piece = cell.piece;
	source.properties = cell.properties; source.transparencyGroup = cell.transparency;
	source.groupActive = snapshot.activeTransparency[cell.transparency];
	// Left/Right alone do nothing in native DrawCell. dSpecial has its own gate.
	source.blendActive = source.groupActive && (cell.properties & NativeTransparent) != 0;
	const uint8_t potentialMask = static_cast<uint8_t>(cell.properties & (NativeTransparentLeft | NativeTransparentRight));
	if (cell.piece < snapshot.micros.size()) {
		const auto &micros = snapshot.micros[cell.piece];
		const unsigned leftType = (micros[0] >> 12) & 7U, rightType = (micros[1] >> 12) & 7U;
		source.microMasksKnown = leftType <= 5 && rightType <= 5;
		if (source.microMasksKnown) {
			// Native masks apply only to the lowest Left/RightTrapezoid or
			// TransparentSquare. A lowest triangle remains opaque instead.
			if (micros[0] != 0 && (leftType == 1 || leftType == 4)) source.partialMask = static_cast<uint8_t>(source.partialMask | (potentialMask & NativeTransparentLeft));
			if (micros[1] != 0 && (rightType == 1 || rightType == 5)) source.partialMask = static_cast<uint8_t>(source.partialMask | (potentialMask & NativeTransparentRight));
		}
	}
	if (!source.microMasksKnown) source.partialMask = potentialMask;
	return source;
}

void AddSource(NativeSurfaceRecord &record, const Snapshot &snapshot, int x, int z)
{
	if (std::none_of(record.sources.begin(), record.sources.end(), [&](const NativeSurfaceSource &source) { return source.x == x && source.z == z; }))
		record.sources.push_back(NativeSource(snapshot, x, z));
}

Bounds SourceBounds(const Instance &instance, const std::vector<Module> &modules, Vec3 translation, uint8_t quarterTurns)
{
	Instance original;
	original.module = instance.sourceModule;
	original.translation = translation;
	original.quarterTurns = quarterTurns;
	Bounds bounds = EmptyBounds();
	for (const Vertex &vertex : modules[static_cast<size_t>(original.module)].vertices) Include(bounds, Transform(original, vertex.position));
	return bounds;
}

bool SourceShapeMatches(const Instance &instance, const Bounds &sourceBounds, Vec3 translation, uint8_t quarterTurns)
{
	if (instance.module == ModuleKind::WallFragment || instance.module == ModuleKind::JoinFragment) {
		return instance.bounds.min.x >= sourceBounds.min.x - BoundsTolerance && instance.bounds.max.x <= sourceBounds.max.x + BoundsTolerance
		    && instance.bounds.min.y >= sourceBounds.min.y - BoundsTolerance && instance.bounds.max.y <= sourceBounds.max.y + BoundsTolerance
		    && instance.bounds.min.z >= sourceBounds.min.z - BoundsTolerance && instance.bounds.max.z <= sourceBounds.max.z + BoundsTolerance;
	}
	return Near(instance.translation, translation) && instance.quarterTurns == quarterTurns && Near(instance.bounds, sourceBounds);
}

NativeSurfaceRecord SurfaceRecord(const Snapshot &snapshot, const Instance &instance, const std::vector<Module> &modules)
{
	NativeSurfaceRecord record;
	record.instanceId = instance.id; record.sourceInstanceId = instance.sourceInstanceId;
	record.module = instance.module; record.sourceModule = instance.sourceModule;
	const auto [ownerX, ownerZ] = Owner(instance);
	const unsigned sub = static_cast<unsigned>(instance.sourceInstanceId & 255);
	bool applyWallBlending = false;
	if (instance.sourceModule == ModuleKind::Wall) {
		constexpr int Dx[] = { 0, 1, 0, -1 }, Dz[] = { -1, 0, 1, 0 };
		if (sub >= 4) { record.provenanceResolved = false; return record; }
		const int sourceX = ownerX + Dx[sub], sourceZ = ownerZ + Dz[sub];
		const Vec3 at { static_cast<float>(ownerX) + static_cast<float>(Dx[sub]) * 0.5F, 0,
			static_cast<float>(ownerZ) + static_cast<float>(Dz[sub]) * 0.5F };
		const uint8_t quarter = static_cast<uint8_t>(sub % 2);
		record.provenanceResolved = NativeSpace(snapshot, ownerX, ownerZ) && !NativeSpace(snapshot, sourceX, sourceZ)
		    && SourceShapeMatches(instance, SourceBounds(instance, modules, at, quarter), at, quarter);
		AddSource(record, snapshot, sourceX, sourceZ);
		applyWallBlending = true;
	} else if (instance.sourceModule >= ModuleKind::Corner && instance.sourceModule <= ModuleKind::Junction4) {
		if (sub >= 4) { record.provenanceResolved = false; return record; }
		// All wall-graph nodes lie on two half-cell axes. The source identity
		// retains the clamped owner and the two positive-boundary sub bits.
		const Vec3 at { static_cast<float>(ownerX) - 0.5F + ((sub & 1U) != 0 ? 1.0F : 0.0F), 0,
			static_cast<float>(ownerZ) - 0.5F + ((sub & 2U) != 0 ? 1.0F : 0.0F) };
		record.provenanceResolved = SourceShapeMatches(instance, SourceBounds(instance, modules, at, 0), at, 0);
		const int leftX = static_cast<int>(std::floor(at.x)), topZ = static_cast<int>(std::floor(at.z));
		for (int dz = 0; dz < 2; ++dz) {
			for (int dx = 0; dx < 2; ++dx) {
				const int x = leftX + dx, z = topZ + dz;
				if (!NativeSpace(snapshot, x, z)
				    && (NativeSpace(snapshot, leftX + 1 - dx, z) || NativeSpace(snapshot, x, topZ + 1 - dz)))
					AddSource(record, snapshot, x, z);
			}
		}
		record.provenanceResolved = record.provenanceResolved && !record.sources.empty();
		applyWallBlending = true;
	} else if (instance.module == ModuleKind::ArchFrame || instance.module == ModuleKind::Pillar || instance.module == ModuleKind::Fence) {
		// These three technical structures span their original native mega.
		// Preserve each of its four SOL cells rather than trusting one owner.
		for (int dz = 0; dz < 2; ++dz)
			for (int dx = 0; dx < 2; ++dx) AddSource(record, snapshot, ownerX + dx, ownerZ + dz);
		applyWallBlending = true;
	} else {
		// DrawFloorTile is opaque; native door sprites use DrawObject without
		// TransList. Stair surfaces and the unused NativeDetail placeholder
		// likewise do not acquire alpha from their native room number.
		AddSource(record, snapshot, ownerX, ownerZ);
	}
	if (applyWallBlending) {
		for (const NativeSurfaceSource &source : record.sources) {
			if (!source.blendActive) continue;
			if (source.partialMask != 0) record.policy = FrameSurfacePolicy::ConservativeNativeCutaway;
			else if (record.policy == FrameSurfacePolicy::Opaque) record.policy = FrameSurfacePolicy::NativePaletteBlend;
		}
	}
	return record;
}

Vec3 TextureFaceNormal(const FrameTriangle &triangle)
{
	const Vec3 a = triangle.vertices[0].position, b = triangle.vertices[1].position, c = triangle.vertices[2].position;
	const Vec3 ab { b.x - a.x, b.y - a.y, b.z - a.z }, ac { c.x - a.x, c.y - a.y, c.z - a.z };
	return { ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x };
}

Vec3 TextureFaceCenter(const FrameTriangle &triangle)
{
	// Both triangles of an axis-aligned box face have the same bounds center.
	// Avoid choosing different native pieces for the two halves of one face.
	Bounds bounds = EmptyBounds();
	for (const Vertex &vertex : triangle.vertices) Include(bounds, vertex.position);
	return { (bounds.min.x + bounds.max.x) * 0.5F, (bounds.min.y + bounds.max.y) * 0.5F,
		(bounds.min.z + bounds.max.z) * 0.5F };
}

const NativeSurfaceSource &TextureSource(const NativeSurfaceRecord &surface, Vec3 center, Vec3 normal)
{
	const bool alongX = std::abs(normal.z) >= std::abs(normal.x);
	const bool vertical = std::abs(normal.y) < std::max(std::abs(normal.x), std::abs(normal.z));
	const float facing = alongX ? normal.z : normal.x;
	const auto score = [&](const NativeSurfaceSource &source) {
		const float dx = static_cast<float>(source.x) - center.x;
		const float dz = static_cast<float>(source.z) - center.z;
		const float depth = alongX ? dz : dx;
		const float tangent = alongX ? dx : dz;
		// Prefer a SOL source behind this face, then the closest native plane
		// and tangent. Caps have no native facing: choose the nearest source.
		return std::array<float, 3> { vertical && depth * facing > 0 ? 1.0F : 0.0F,
			vertical ? std::abs(depth) : dx * dx + dz * dz, vertical ? tangent * tangent : 0.0F };
	};
	return *std::min_element(surface.sources.begin(), surface.sources.end(), [&](const NativeSurfaceSource &a, const NativeSurfaceSource &b) {
		const auto aScore = score(a), bScore = score(b);
		if (aScore != bScore) return aScore < bScore;
		return a.z != b.z ? a.z < b.z : a.x < b.x;
	});
}

NativeTextureAxis TextureCapAxis(const Instance &instance, const NativeSurfaceSource &source, Vec3 center)
{
	if (instance.sourceModule == ModuleKind::Wall)
		return (instance.sourceInstanceId & 1U) == 0 ? NativeTextureAxis::AlongX : NativeTextureAxis::AlongZ;
	if (instance.module == ModuleKind::DoorFrame)
		return instance.quarterTurns % 2 == 0 ? NativeTextureAxis::AlongX : NativeTextureAxis::AlongZ;
	return std::abs(static_cast<float>(source.z) - center.z) >= std::abs(static_cast<float>(source.x) - center.x)
	    ? NativeTextureAxis::AlongX : NativeTextureAxis::AlongZ;
}

void BindNativeTexture(FrameTriangle &triangle, const Snapshot &snapshot, const Instance &instance,
    const NativeSurfaceRecord &surface, const Module &module, size_t first)
{
	triangle.textureVertices = triangle.vertices;
	if (!surface.provenanceResolved || surface.sources.empty()) return;
	const bool masonry = instance.sourceModule == ModuleKind::Wall
	    || (instance.sourceModule >= ModuleKind::Corner && instance.sourceModule <= ModuleKind::Junction4)
	    || instance.module == ModuleKind::DoorFrame;
	if (instance.module != ModuleKind::Floor && instance.module != ModuleKind::DoorLeaf && !masonry) return;
	const Vec3 normal = TextureFaceNormal(triangle), center = TextureFaceCenter(triangle);
	const NativeSurfaceSource &source = TextureSource(surface, center, normal);
	NativeTextureBinding &binding = triangle.nativeTexture;
	binding.x = source.x; binding.z = source.z; binding.piece = source.piece;
	if (instance.module == ModuleKind::Floor) {
		binding.kind = NativeTextureKind::FloorDiamond;
		for (size_t corner = 0; corner < 3; ++corner) {
			const Vertex &local = module.vertices[module.indices[first + corner]];
			triangle.textureVertices[corner] = { NativeFloorPixelVertex(source.x, source.z, local.u, local.v), local.u, local.v };
		}
		return;
	}
	if (instance.module == ModuleKind::DoorLeaf) {
		const auto [ownerX, ownerZ] = Owner(instance);
		const auto door = std::find_if(snapshot.doors.begin(), snapshot.doors.end(), [&](const Door &value) { return value.x == ownerX && value.z == ownerZ; });
		if (door == snapshot.doors.end()) return;
		binding.kind = NativeTextureKind::DoorWood;
		binding.nativeSlot = door->slot;
		binding.axis = std::abs(normal.z) >= std::abs(normal.x) ? NativeTextureAxis::AlongX : NativeTextureAxis::AlongZ;
		// A small closed native CLX patch is a material sample, not a complete
		// native leaf facade. All faces, including backs and caps, disclose it.
		binding.approximate = true;
		const size_t quad = static_cast<size_t>(module.indices[first]) / 4 * 4;
		const Vec3 a = module.vertices[quad].position, b = module.vertices[quad + 1].position, d = module.vertices[quad + 3].position;
		const auto edgeLength = [&](Vec3 end) {
			const float dx = (end.x - a.x) * instance.scale.x, dy = (end.y - a.y) * instance.scale.y, dz = (end.z - a.z) * instance.scale.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		};
		const float width = edgeLength(d), height = edgeLength(b);
		const float down = b.y != a.y ? -1.0F : 1.0F;
		for (size_t corner = 0; corner < 3; ++corner) {
			const Vertex &local = module.vertices[module.indices[first + corner]];
			triangle.textureVertices[corner].u = local.v * width;
			triangle.textureVertices[corner].v = down * local.u * height;
		}
		return;
	}
	binding.kind = NativeTextureKind::Masonry;
	const bool cap = std::abs(normal.y) >= std::max(std::abs(normal.x), std::abs(normal.z));
	binding.axis = cap ? TextureCapAxis(instance, source, center)
	                   : std::abs(normal.z) >= std::abs(normal.x) ? NativeTextureAxis::AlongX : NativeTextureAxis::AlongZ;
	// Native MIN donor column: Hwall runs along X in column 1; Vwall runs
	// along Z in column 0. This is separate from the band's shear selector.
	binding.column = binding.axis == NativeTextureAxis::AlongX ? 1 : 0;
	const float towardSource = normal.x * (static_cast<float>(source.x) - center.x)
	    + normal.z * (static_cast<float>(source.z) - center.z);
	binding.approximate = cap || surface.sources.size() != 1 || towardSource > 0 || instance.module == ModuleKind::DoorFrame;
	if (instance.sourceModule == ModuleKind::Wall)
		binding.approximate = binding.approximate || binding.axis != TextureCapAxis(instance, source, center);
	for (Vertex &vertex : triangle.textureVertices) {
		const auto uv = NativePlanarTextureUv(vertex.position, binding);
		vertex.u = uv[0];
		// A horizontal cap has no vertical extent: use the other world axis
		// instead of stretching a constant masonry row across the whole cap.
		vertex.v = cap ? 2.0F * (binding.axis == NativeTextureAxis::AlongX
			        ? vertex.position.z - static_cast<float>(binding.z)
			        : vertex.position.x - static_cast<float>(binding.x))
		               : uv[1];
	}
}

void SetFallback(PilotFrame &frame, FrameFallbackReason reason)
{
	frame.requiresNativeFallback = true;
	frame.fallbackReasons |= static_cast<uint32_t>(reason);
}
} // namespace

PilotFrame BuildPilotFrame(const Snapshot &snapshot, const Scene &scene, int focusX, int focusZ)
{
	ValidateSnapshot(snapshot);
	Require(Active(focusX, focusZ), "Frame focus outside native active map");
	Require(scene.gameRevision == snapshot.gameRevision && scene.revision != 0 && scene.regions.size() == 100
	        && scene.rebuiltRegions <= scene.regions.size(), "Frame scene epoch, revision or region count invalid");
	Require(scene.presentationSignature == PresentationSignature(snapshot), "Frame scene presentation differs from native snapshot");
	const auto &modules = TechnicalModules();
	ValidateModules(modules);
	PilotFrame out;
	out.regionX = focusX / RegionSize * RegionSize;
	out.regionZ = focusZ / RegionSize * RegionSize;
	out.geometryRevision = scene.revision;
	out.presentationSignature = scene.presentationSignature;
	out.gameRevision = scene.gameRevision;
	std::set<uint64_t> identities;
	for (size_t slot = 0; slot < scene.regions.size(); ++slot) {
		Require(scene.regions[slot] != nullptr, "Frame scene null region");
		const Region &region = *scene.regions[slot];
		Require(region.x == ActiveMin + static_cast<int>(slot % 10) * RegionSize
		        && region.z == ActiveMin + static_cast<int>(slot / 10) * RegionSize && region.geometrySignature != 0
		        && ValidBounds(region.bounds), "Frame scene region grid, signature or bounds invalid");
		const bool selected = std::abs(region.x - out.regionX) <= RegionSize && std::abs(region.z - out.regionZ) <= RegionSize;
		TownSceneModel collision {};
		if (selected) {
			++out.regions;
			// Native DrawDungeon submits every dSpecial CLX independently of
			// SOL and independently of technical module ownership.
			for (int z = region.z; z < region.z + RegionSize; ++z) {
				for (int x = region.x; x < region.x + RegionSize; ++x) {
					const Cell &cell = snapshot.At(x, z);
					if ((cell.flags & NativeVisible) == 0 || cell.special <= 0) continue;
					// InitDungeonPieces produces 1..6; SetDoorStateOpen also
					// produces 7/8 for native Left/Right doors. Blocked doors
					// retain that open arch frame. Caller checks actual CLX size.
					if (cell.special > 8) {
						SetFallback(out, FrameFallbackReason::UnsupportedNativeSpecial);
						continue;
					}
					out.nativeSpecialOverlays.push_back({ x, z, cell.special - 1, cell.light, cell.transparency,
						snapshot.activeTransparency[cell.transparency] });
				}
			}
			collision.kind = TownSceneKind::Cathedral;
			collision.minTile = { region.x, region.z };
			collision.maxTile = { region.x + RegionSize - 1, region.z + RegionSize - 1 };
			collision.physicalBounds = { region.bounds.min.x, region.bounds.min.z, region.bounds.max.x, region.bounds.max.z, region.bounds.max.y, false };
			collision.runtimeAudit.sourceKind = std::string(PilotFrameTechnicalNamespace);
			collision.runtimeAudit.revision = std::to_string(PilotFrameTechnicalRevision);
		}
		Bounds actualRegion = EmptyBounds();
		for (const Instance &instance : region.instances) {
			Require(identities.insert(instance.id).second, "Frame scene duplicate instance identity");
			const Bounds actual = ValidateInstance(snapshot, region, instance, modules);
			Include(actualRegion, actual.min); Include(actualRegion, actual.max);
			if (!selected) continue;
			const auto [ownerX, ownerZ] = Owner(instance);
			const Cell &cell = snapshot.At(ownerX, ownerZ);
			const bool visible = (cell.flags & NativeVisible) != 0;
			const NativeSurfaceRecord surface = SurfaceRecord(snapshot, instance, modules);
			if (visible) {
				out.nativeSurfaces.push_back(surface);
				if (!surface.provenanceResolved) SetFallback(out, FrameFallbackReason::AmbiguousSurfaceProvenance);
			}
			const bool transparent = surface.policy != FrameSurfacePolicy::Opaque;
			const Module &module = modules[static_cast<size_t>(instance.module)];
			for (size_t first = 0; first < module.indices.size(); first += 3) {
				FrameTriangle triangle { {}, instance.pick, instance.module, instance.id, cell.light, transparent, surface.policy };
				for (size_t corner = 0; corner < 3; ++corner) {
					const Vertex &local = module.vertices[module.indices[first + corner]];
					triangle.vertices[corner] = { Transform(instance, local.position), local.u, local.v };
				}
				collision.triangles.push_back(CollisionTriangle(triangle));
				BindNativeTexture(triangle, snapshot, instance, surface, module, first);
				if (visible) out.triangles.push_back(triangle);
			}
		}
		if (region.instances.empty()) actualRegion = { { static_cast<float>(region.x), 0, static_cast<float>(region.z) },
			{ static_cast<float>(region.x + RegionSize), 0, static_cast<float>(region.z + RegionSize) } };
		Require(Near(actualRegion, region.bounds), "Frame region bounds disagree with transformed vertices");
		if (selected) out.collisionModels.push_back(std::move(collision));
	}
	return out;
}

} // namespace devilution::cathedral

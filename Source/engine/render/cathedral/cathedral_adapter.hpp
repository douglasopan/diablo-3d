#pragma once

#include "cathedral_snapshot.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace devilution::cathedral {

struct Vec3 { float x, y, z; };
struct Bounds { Vec3 min, max; };
struct Vertex { Vec3 position; float u, v; };

enum class ModuleKind : uint8_t {
	Floor, Wall, Corner, WallEnd, Junction3, Junction4,
	DoorFrame, DoorLeaf, StairsUp, StairsDown, NativeDetail, ArchFrame, Pillar, Fence,
	WallFragment, JoinFragment, Count
};
enum class PickKind : uint8_t { Ground, Architecture, Object, Trigger };
struct PickBinding {
	PickKind kind = PickKind::Ground;
	int x = 0, z = 0, nativeSlot = -1;
	friend bool operator==(const PickBinding &, const PickBinding &) = default;
};

/** Local, immutable technical mesh; not a registry asset or final art. */
struct Module {
	ModuleKind kind;
	std::string_view technicalId;
	uint32_t revision = 1;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	Bounds bounds;
};

struct Instance {
	uint64_t id = 0;
	ModuleKind module = ModuleKind::Floor;
	Vec3 translation {};
	uint8_t quarterTurns = 0;
	PickBinding pick;
	uint8_t transparency = 0; // Native room group, not an opacity value.
	uint16_t nativePiece = 0;
	uint8_t nativeMega = 0;
	Bounds bounds;
	// Only axis-aligned technical box fragments use non-unit scale. Authored assets
	// are outside this pilot. Source identity survives the local socket surgery.
	Vec3 scale { 1, 1, 1 };
	ModuleKind sourceModule = ModuleKind::Floor;
	uint64_t sourceInstanceId = 0;
};

struct Region {
	int x = 0, z = 0;
	uint64_t geometrySignature = 0;
	Bounds bounds;
	std::vector<Instance> instances;
};
struct Scene {
	uint64_t revision = 0;
	uint64_t gameRevision = 0;
	uint64_t presentationSignature = 0;
	uint32_t rebuiltRegions = 0;
	std::vector<std::shared_ptr<const Region>> regions;
};

/** Read-only adapter. Geometry and light/vision updates have separate identities.
 * Eight-cell regions borrow immutable reusable meshes; never one floor mesh.
 */
class Adapter {
public:
	Scene Update(const Snapshot &snapshot);
	void Reset();
private:
	Scene previous_;
	uint64_t nextRevision_ = 1;
};

const std::vector<Module> &TechnicalModules();
Vec3 Transform(const Instance &instance, Vec3 local);
bool Intersects(const Bounds &a, const Bounds &b);
std::vector<const Instance *> VisibleInstances(const Scene &scene, const Bounds &conservativeViewBounds);
struct PickResult { PickBinding binding; uint64_t instanceId; float distance; };
std::optional<PickResult> RayPick(const Scene &scene, Vec3 origin, Vec3 unitDirection);
Snapshot ReadSnapshot(const char *path);

} // namespace devilution::cathedral

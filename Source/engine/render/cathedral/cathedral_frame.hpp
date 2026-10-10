#pragma once

#include "cathedral_adapter.hpp"
#include "cathedral_native_texture.hpp"
#include "engine/render/town_scene.hpp"

namespace devilution::cathedral {

inline constexpr std::string_view PilotFrameTechnicalNamespace = "technical.cathedral.runtime-pilot";
inline constexpr uint32_t PilotFrameTechnicalRevision = 2;

enum class FrameSurfacePolicy : uint8_t {
	Opaque,
	NativePaletteBlend,
	ConservativeNativeCutaway
};

/** Native SOL source, which can differ from an adapter instance's walkable owner.
 * partialMask retains the native 0x10/0x20 bits actually used by microtiles 0/1.
 * Missing micro data keeps a conservative mask; it never invents texture UVs.
 */
struct NativeSurfaceSource {
	int x = 0, z = 0;
	uint16_t piece = 0;
	uint8_t properties = 0, transparencyGroup = 0, partialMask = 0;
	bool groupActive = false, blendActive = false, microMasksKnown = false;
};

struct NativeSurfaceRecord {
	uint64_t instanceId = 0, sourceInstanceId = 0;
	ModuleKind module = ModuleKind::Floor, sourceModule = ModuleKind::Floor;
	FrameSurfacePolicy policy = FrameSurfacePolicy::Opaque;
	bool provenanceResolved = true;
	std::vector<NativeSurfaceSource> sources;
};

/** Native special CLX coverage is submitted separately by the existing renderer.
 * The pure bridge neither reads sprite pixels nor invents an arch module owner.
 * Native values 1..6 are generator arches; 7/8 are Left/Right open or blocked
 * door frames (objects.cpp SetDoorStateOpen). Caller validates the actual CLX
 * sprite count before using this zero-based frameIndex.
 */
struct NativeSpecialOverlay {
	int x = 0, z = 0, frameIndex = 0;
	uint8_t light = 0, transparencyGroup = 0;
	bool blendActive = false;
};

enum class FrameFallbackReason : uint32_t {
	None = 0,
	AmbiguousSurfaceProvenance = 1U << 0,
	UnsupportedNativeSpecial = 1U << 1
};

struct FrameTriangle {
	std::array<Vertex, 3> vertices;
	PickBinding pick;
	ModuleKind module;
	uint64_t instanceId;
	uint8_t light;
	bool transparent;
	FrameSurfacePolicy policy = FrameSurfacePolicy::Opaque;
	NativeTextureBinding nativeTexture;
	// Visual bitmap footprint/UV only. vertices remains the original physical
	// triangle and must remain the source of collision and geometric picking.
	std::array<Vertex, 3> textureVertices {};
	// Presentation only: verified native replacement may hide a technical
	// surface while physical triangles/collision/native provenance remain.
	bool visualSuppressed = false;
};

/** Pure presentation bridge over the accepted live-map adapter. Camera collision
 * retains every selected physical triangle, including geometry hidden by fog.
 * Full active SOL blending uses the native palette operator over the technical
 * geometry. Native diagonal MIN masks have no corresponding module UVs: their
 * complete technical wall volume is conservatively omitted from drawing, while
 * every physical triangle and its native provenance remain in this frame.
 * Floors, stairs and door objects remain opaque. Special CLX overlays retain
 * their independent native TransList gate and are not inferred from SOL.
 */
struct PilotFrame {
	std::vector<FrameTriangle> triangles;
	// Separate native art panels; never collision or geometric pick authority.
	// Empty by default. Owner-verified evidence and atomic preparation are required.
	std::vector<FrameTriangle> nativeArtPanels;
	std::vector<TownSceneModel> collisionModels;
	std::vector<NativeSurfaceRecord> nativeSurfaces;
	std::vector<NativeSpecialOverlay> nativeSpecialOverlays;
	int regionX = 0, regionZ = 0;
	uint64_t geometryRevision = 0, presentationSignature = 0, gameRevision = 0;
	size_t regions = 0;
	bool requiresNativeFallback = false;
	uint32_t fallbackReasons = 0;
};

/** Select at most nine eight-cell regions around the native focus. Throws
 * invalid_argument for an ineligible snapshot or inconsistent scene contract.
 * Does not generate a map, consume RNG, mutate either input, or rasterize.
 */
PilotFrame BuildPilotFrame(const Snapshot &snapshot, const Scene &scene, int focusX, int focusZ);

} // namespace devilution::cathedral

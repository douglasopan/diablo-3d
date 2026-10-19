#pragma once


#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/render/town_scene.hpp"
#include "engine/render/town_visual_selection_snapshot.hpp"
#include "engine/render/town_visual_objects.hpp"

namespace devilution {

inline constexpr std::string_view TownVisualSceneManifestPath = "d3d-scenes/tristram.ini";

/** Visual-only edit about an immutable source pivot. Native anchors never move. */
struct TownVisualSceneTransform {
	TownLightVector pivot;
	TownLightVector translation;
	float yawDegrees = 0;
	float uniformScale = 1;
	// Explicit authored axes; masters and source UV/projection coordinates remain immutable.
	TownLightVector authoredScale { 1, 1, 1 };
};

struct TownVisualSceneObjectAudit {
	size_t sceneIndex = 0;
	std::string stableId;
	std::string name;
	std::string objectClass;
	std::string assetPath;
	std::string sha256;
	std::string sourceKind;
	std::string eventBinding;
	std::string eventState;
	Point nativeMin { 0, 0 };
	Point nativeMax { 0, 0 };
	TownVisualSceneTransform transform;
};

struct TownVisualSceneAudit {
	std::string assetPath = std::string(TownVisualSceneManifestPath);
	std::string sourceKind;
	std::string sha256;
	std::string revision;
	std::string status = "not-loaded";
	std::string failure;
	std::string staticData;
	std::string staticSha256;
	std::vector<TownVisualSceneObjectAudit> objects;
};

/** Bounded test/host resolver. Success returns the exact bytes and provenance. */
using TownVisualSceneAssetResolver = std::function<bool(std::string_view,
    std::vector<std::byte> &, std::string &, std::string &)>;
/** Called only after every architecture object has been completely validated.
 * Must commit the static archive only on success. No fallible work follows it.
 * This keeps the entire scene transactional without coupling the two codecs. */
using TownVisualSceneStaticCommit = std::function<bool(std::string_view, std::string_view, std::string &)>;

/** All-or-nothing full visual architecture source; false leaves models intact.
 * A present staticData is required to pass staticCommit before committing any
 * architectural model. Edition is Diablo retail/shareware, never Hellfire. */
bool ParseTownVisualScene(std::vector<TownSceneModel> &models, std::span<const std::byte> manifest,
    const TownVisualSceneAssetResolver &resolver, const TownVisualSceneStaticCommit &staticCommit = {});
bool LoadTownVisualScene(std::vector<TownSceneModel> &models, const TownVisualSceneStaticCommit &staticCommit = {});

// Explicit frozen presentation state from the real Town/host companion. A
// regional preview never observes Quests, IsWarpOpen or the regional carrier.
struct TownVisualPreviewEvents {
	bool wellWaterPoisoned = false;
	bool catacombsAccessOpen = false;
	bool cavesAccessOpen = false;
	bool hellAccessOpen = false;
	constexpr bool operator==(const TownVisualPreviewEvents &) const = default;
};

// Resource source, not an admitted renderer frame. All geometry, source
// artwork references, archive pixels/coverage and visual event state are OWNED;
// no span into the live Scene/SourceModels/static cache survives a reset here.
struct TownVisualPreviewSource {
	std::vector<TownSceneModel> models;
	std::vector<TownSceneModel> materialSources;
	TownVisualObjectSet statics;
	TownVisualSceneAudit sceneAudit;
	TownVisualObjectAudit staticAudit;
	TownVisualPreviewEvents events;
	std::string worldPatchSha256; // Source reference only; never applied by preview.
};

// Real cold load, with the same manifest/model/static hashes, transforms and
// codecs as Town. Requires the selected manifest's explicit expected SHA.
// Does not publish SceneAudit/SourceModels/statics, apply a native world patch,
// generate Town, or read native map/quest/warp state. The immutable edition
// resource family is still retail/shareware (Hellfire remains unsupported).
// Success atomically replaces output; failure leaves it intact. Renderer
// materials/ground9 + frozen-map admission remain separate mandatory steps.
bool LoadTownVisualPreviewSource(std::string_view expectedManifestSha256,
    const TownVisualPreviewEvents &events, TownVisualPreviewSource &output, std::string &failure);

// Cold preview follows the exact selection captured in the companion/CRC.
// NotAdmitted, missing selected bytes or mismatched hashes refuse; no optional
// asset probe may silently replace a captured Canonical/Specific decision.
bool LoadTownVisualPreviewSource(const TownVisualSelectionSnapshot &selection,
    const TownVisualPreviewEvents &events, TownVisualPreviewSource &output, std::string &failure);

/** Full architecture/manifest validation before a native world patch commits.
 * Performs the same model/hash/transform checks as Load, without publishing
 * models, audit, static data, or requiring the native patch to be applied yet.
 * The caller has already validated the complete paired static archive. */
bool ValidateTownVisualSceneForLogic(std::span<const std::byte> manifest,
    const TownVisualSceneAssetResolver &resolver, std::string &failure);

/** Pure exact D3DVIS1 codec. No struct padding or native collision data.
 * Interior geometry/lights/openings are preserved; borrowed aperture spans
 * are stored as checked indexes. Rotation of axis-aligned adjuncts is refused;
 * their identity, translation and uniform scale are supported.
 *
 * Wire contract, little endian, float32 IEEE754, i32 points, u32 enums/bools:
 * magic[8]="D3DVIS1\\0"; version=1,kind,flags(external=1,texture=2);
 * nativeMin[2i],nativeMax[2i]; physicalBounds[5f],closed;
 * artwork reference[2i],pixelOrigin[2i],pixelSize[2i],enabled,min[2i],max[2i],
 * fringeMin,fringeMax; triangleCount,patchCount,width,height,adjunct(0 or 1);
 * seven length-prefixed UTF8 strings: original assetPath,sourceKind,sha256,
 * expectedSha256,revision,status,failure. Then triangleCount*104 bytes,
 * patchCount*112 bytes, then width*height*3 RGB bytes (top row first).
 * Triangle: three vertices[5f x,height,z,u,v],material,source[2i],pick[2i],
 * nativeProjection,normal[3f],surfaceRole,surfaceDetail.
 * Patch: surfaceDetail,material,three vertices[5f],uvMin[2f],uvMax[2f],
 * repeatWorldSize[2f],repeat,sourceMin[2i],sourceMax[2i].
 * Adjunct=1 adds counts exterior,interior,opening,aperture,occluder,fire and
 * clippedSourceTriangles; roomMin[3f],roomMax[3f]; four RGB[3f] colors:
 * fireColor,fireEmissionLinear,fireCoreEmissionLinear,fireTipEmissionLinear;
 * exterior/interior triangles[104]; openings[52]; apertures[28];
 * occluders[32]; fire records[44 header + emissiveCount*104].
 * Aperture: plane,coordinate,minU,maxU,minV,maxV[5f],polygonSides.
 * Opening: kind,aperture,outerCoordinate,clipMin,clipMax[3f],muntins,material.
 * Occluder: min[3f],max[3f],apertureFirst,apertureCount (checked index span).
 * Fire: kind,position[3f],color[3f],radius,intensity[2f],seed,emissiveCount.
 * No trailing bytes. All bounds/enums/finite values are validated before
 * committing; exact normals/UVs/provenance survive serialization unchanged. */
bool ParseTownVisualSceneModel(TownSceneModel &model, std::span<const std::byte> data, std::string &failure);
bool SerializeTownVisualSceneModel(const TownSceneModel &model, std::vector<std::byte> &data, std::string &failure);

struct TownVisualSceneStaticReference {
	std::string assetPath;
	std::string sha256;
};
/** Explicit diagnostic conversion only, never called during normal load.
 * Writes d3d-scenes/objects/*.d3dvis and finally tristram.ini below directory.
 * Refuses existing target files. The caller exports the static archive first.
 * Private original artwork/textures remain in this local directory. */
bool ExportTownVisualScene(std::span<const TownSceneModel> models, const std::filesystem::path &directory,
    std::string_view revision, std::string &failure, const TownVisualSceneStaticReference &staticReference = {});

/** Observe native quest/warp state by stable object identity. Geometry,
 * transforms, masks, source/pick anchors, save state and RNG are untouched.
 * True requests native-artwork/material cache invalidation, not scene rebuild. */
bool UpdateTownVisualSceneEvents();
/** Draw-only material state for the identified well (also useful before a
 * persistent scene is installed). Never changes the native quest or geometry. */
bool TownVisualSceneWaterPoisoned(size_t sceneIndex);
/** Immutable pre-transform geometry for original-art projection/material
 * sampling. Drawing uses GetTownScene; sampling original pixels uses this
 * source span so translating a model never translates its source artwork. */
std::span<const TownSceneModel> GetTownVisualSceneSourceModels();
const TownVisualSceneAudit &GetTownVisualSceneAudit();
void ResetTownVisualSceneAudit();

} // namespace devilution

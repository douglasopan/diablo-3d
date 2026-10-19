#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace devilution {

/** Shared admission limits for the native isometric adapter's draw expansion. */
inline constexpr int TownSurroundingsNativeMaxExtraRows = 128;
inline constexpr int TownSurroundingsNativeMaxExtraColumns = 32;

/** Visual-only positions in native cell units; one unit is 1.458 metres. */
struct TownSurroundingsPoint {
	float x = 0;
	float height = 0;
	float z = 0;
};

/** Linear RGB, independent of the native palette and collision masks. */
struct TownSurroundingsColor {
	float red = 0;
	float green = 0;
	float blue = 0;
};

struct TownSurroundingsTriangle {
	std::array<TownSurroundingsPoint, 3> vertices;
	TownSurroundingsColor color;
};

/**
 * A contiguous visual draw range. Anchors are external coordinates, never
 * indices into dPiece/dFlags/SOL. Kind 0 = ground, 1 = prop, 2 = border skirt,
 * 3 = distant background apron, 4 = complete lowered native padding underlay.
 */
struct TownSurroundingsCell {
	int32_t x = 0;
	int32_t z = 0;
	uint32_t firstTriangle = 0;
	uint32_t triangleCount = 0;
	uint32_t kind = 0;
};

struct TownSurroundingsVisualScene {
	std::vector<TownSurroundingsTriangle> triangles;
	std::vector<TownSurroundingsCell> cells;
	bool valid = false;
	/** True only after the complete canonical kind-4 ring passes admission. */
	bool hasNativePaddingUnderlay = false;
	/** SHA-256 of the exact checked D3DENV1 bytes. */
	std::string identity;
};

/**
 * Authenticates and decodes explicit pinned ENV bytes into a lifetime-owning
 * immutable scene. No asset lookup, cache publication or native state access.
 * Failure leaves all existing owners/cache untouched and returns null.
 */
std::shared_ptr<const TownSurroundingsVisualScene> DecodeTownSurroundingsVisualSceneOwned(
    std::span<const std::byte> bytes, std::string &failure) noexcept;

/**
 * Renderer-thread-only immutable cache shared by the 2D and 3D adapters.
 * Attempts the pinned asset once until Clear. Null means an empty visual
 * fallback; malformed/missing data never publishes a partial scene.
 * Returned pointers remain valid only until Clear.
 */
const TownSurroundingsVisualScene *GetTownSurroundingsVisualScene() noexcept;

/**
 * Conservative native floor-quad bounds wholly inside the underlay ring.
 * This predicate alone never authorizes suppressing native artwork: adapters
 * also require a successfully drawn/admitted underlay and authenticated art.
 */
bool IsTownSurroundingsNativePaddingCell(int x, int z, uint16_t piece) noexcept;

/** Authenticates the observed 64x32, 1024-covered-pixel native void artwork. */
bool AuthenticateTownSurroundingsNativePaddingArtwork(std::span<const uint8_t> pixels,
    std::span<const uint8_t> opacity, int width, int height) noexcept;

/** Visual-resource reset only; does not change camera, level or simulation. */
void ClearTownSurroundingsVisualScene() noexcept;

} // namespace devilution

#pragma once

#include <span>

namespace devilution {

struct TownLightVector {
	float x = 0;
	float height = 0;
	float z = 0;
};

/** RGB components in linear light, not palette indices or 0..255 sRGB bytes. */
struct TownLightColor {
	float red = 0;
	float green = 0;
	float blue = 0;
};

struct TownLightingConfig {
	TownLightColor ambient { 0.48F, 0.48F, 0.50F };
	TownLightColor directional { 0.52F, 0.50F, 0.46F };
	TownLightVector toLight { 0.23F, 1.0F, 0.23F };
	float directionalIntensity = 1;
};

struct TownPointLight {
	TownLightVector position;
	TownLightColor color { 1.0F, 0.70F, 0.08F };
	float radius = 5;
	float intensity = 2;
};

enum class TownLightPlane { X, Height, Z };

/**
 * An actual opening in an otherwise opaque room shell. On X planes, u=z and
 * v=height; on Z planes, u=x and v=height; on Height planes, u=x and v=z.
 * coordinate must lie on the corresponding shell boundary. Declaring a warm
 * painted window alone is not an opening: use emission without an aperture.
 */
struct TownLightAperture {
	TownLightPlane plane = TownLightPlane::X;
	float coordinate = 0;
	float minU = 0;
	float maxU = 0;
	float minV = 0;
	float maxV = 0;
	// Zero keeps a rectangular opening. 3..32 uses a regular polygon inscribed
	// in the ellipse defined by the same bounds; vertex zero points along +u.
	unsigned polygonSides = 0;
};

/** Closed axis-aligned opaque room shell, not a solid filled interior volume. */
struct TownLightOccluder {
	TownLightVector minimum;
	TownLightVector maximum;
	std::span<const TownLightAperture> apertures;
};

struct TownLightingSample {
	TownLightColor ambient;
	TownLightColor directional;
	TownLightColor point;

	TownLightColor total() const;
};

inline constexpr unsigned TownMaxPointLights = 8;
inline constexpr unsigned TownMaxLightOccluders = 32;
inline constexpr unsigned TownMaxLightApertures = 8;

/** Orient only the shading normal of a two-sided surface toward its viewer.
 * Finite source length is preserved; mesh geometry and authored normals are
 * not modified. Invalid source normals return zero; an invalid/zero viewing
 * direction leaves a finite source normal unchanged. One-sided normals stay
 * unchanged regardless of viewer orientation. */
TownLightVector OrientTownLightingNormal(TownLightVector normal, TownLightVector towardViewer, bool doubleSided = true);

/**
 * Pure world-space diffuse math; no camera, palette, game-state access or
 * allocation. normal and config.toLight are normalized internally. Shadow
 * occlusion 0..1 attenuates directional light only. Point lights are optional;
 * at most the first eight are sampled. Too many or malformed occluders fail
 * closed for point visibility, rather than leaking light through walls.
 */
TownLightingSample SampleTownLighting(TownLightVector normal, TownLightVector position,
	float directionalShadow, const TownLightingConfig &config,
	std::span<const TownPointLight> lights = {}, std::span<const TownLightOccluder> occluders = {});

/** Exact segment/shell visibility: both interior points are allowed; every
 * entry/exit crossing needs a matching aperture. Output is 0 blocked or 1 lit. */
float TownPointLightVisibility(TownLightVector light, TownLightVector receiver,
	std::span<const TownLightOccluder> occluders);

/** Keep unlit base color, light factors, and self-emission separate. Result is
 * linear RGB and may exceed 1; clamp/tone-map only when encoding to a palette. */
TownLightColor ComposeTownLitColor(TownLightColor baseLinear, const TownLightingSample &lighting,
	TownLightColor emissionLinear = {});

/** Standard sRGB transfer functions: input and encoded output are normalized
 * 0..1. Divide source palette bytes by 255 before decoding. These helpers can
 * prepare palette LUTs once; avoid three pow calls in every rasterized pixel. */
float TownSrgbToLinear(float normalizedSrgb);
float TownLinearToSrgb(float linear);
TownLightColor TownSrgbToLinear(TownLightColor normalizedSrgb);
TownLightColor TownLinearToSrgb(TownLightColor linear);

} // namespace devilution

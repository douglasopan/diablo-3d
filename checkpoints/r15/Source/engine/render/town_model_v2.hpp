#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devilution {

// Static visual resources only. Native collision, interaction and simulation
// metadata belong to the caller and are never inferred from this packet.
struct TownModelV2Vertex {
	std::array<float, 3> position;
	std::array<float, 3> normal;
	std::array<float, 2> uv;
	std::array<float, 4> tangent;
	std::array<float, 4> color;
};

struct TownModelV2Primitive {
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t material;
};

struct TownModelV2TextureInfo {
	int32_t texture = -1;
	uint32_t texCoord = 0;
	std::array<float, 2> offset { 0, 0 };
	std::array<float, 2> scale { 1, 1 };
	float rotation = 0;
};

struct TownModelV2Material {
	std::array<float, 4> baseColor { 1, 1, 1, 1 };
	std::array<float, 3> emissive { 0, 0, 0 };
	float metallic = 1;
	float roughness = 1;
	float alphaCutoff = 0.5;
	float normalScale = 1;
	float occlusionStrength = 1;
	uint32_t alphaMode = 0; // 0 OPAQUE, 1 MASK, 2 BLEND; consumer must support it.
	bool doubleSided = false;
	// baseColor (sRGB), metallicRoughness (linear G/B), normal (linear),
	// occlusion (linear R), emissive (sRGB). Encoded bytes remain unmodified.
	std::array<TownModelV2TextureInfo, 5> textures;
};

struct TownModelV2Texture {
	uint32_t mime = 0; // 0 PNG, 1 JPEG
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t wrapS = 10497;
	uint32_t wrapT = 10497;
	uint32_t minFilter = 9987;
	uint32_t magFilter = 9729;
	std::array<uint8_t, 32> sha256 {};
	std::vector<uint8_t> encoded;
};

struct TownModelV2 {
	std::string assetId;
	std::string variant;
	std::string revision;
	std::string openingsPipeline;
	std::string sha256;
	std::array<uint8_t, 32> sourceSha256 {};
	std::array<float, 3> anchor {};
	std::array<float, 3> boundsMin {};
	std::array<float, 3> boundsMax {};
	uint32_t openingsMode = 0; // native-unchanged, external-code-required, baked-reviewed
	uint32_t tangentMode = 0; // 1 source, 2 generated UV tangent (not MikkTSpace)
	std::vector<TownModelV2Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<TownModelV2Primitive> primitives;
	std::vector<TownModelV2Material> materials;
	std::vector<TownModelV2Texture> textures;
};

// Exact byte identity is mandatory. No file I/O, texture decoding, renderer,
// fallback conversion or selection changes occur here. On failure output stays
// unchanged. See docs/TOWN-MODEL-V2.md for bounds and compatibility requirements.
bool ParseTownModelV2(std::span<const std::byte> bytes, std::string_view expectedSha256,
    TownModelV2 &output, std::string &error);

} // namespace devilution

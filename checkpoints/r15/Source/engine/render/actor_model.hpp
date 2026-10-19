#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace devilution {

struct ActorVec2 {
	float x = 0;
	float y = 0;
};

struct ActorVec3 {
	float x = 0;
	float y = 0;
	float z = 0;
};

struct ActorVec4 {
	float x = 0;
	float y = 0;
	float z = 0;
	float w = 1;
};

// Column-major affine matrices, preserving the source skeleton's units.
using ActorMatrix = std::array<float, 16>;

struct ActorTransform {
	ActorVec3 translation;
	ActorVec4 rotation;
	ActorVec3 scale { 1, 1, 1 };
};

struct ActorNode {
	std::int32_t parent = -1;
	std::string name;
	ActorTransform rest;
};

struct ActorJoint {
	std::uint32_t node = 0;
	ActorMatrix inverseBind {};
};

struct ActorSkinVertex {
	ActorVec3 position;
	ActorVec3 normal;
	ActorVec2 uv;
	std::array<std::uint16_t, 4> joints {};
	std::array<float, 4> weights {};
};

enum class ActorTrackPath : std::uint8_t {
	Translation = 0,
	Rotation = 1,
	Scale = 2,
};

enum class ActorTrackInterpolation : std::uint8_t {
	Linear = 0,
	Step = 1,
};

struct ActorTrack {
	std::uint32_t node = 0;
	ActorTrackPath path = ActorTrackPath::Translation;
	ActorTrackInterpolation interpolation = ActorTrackInterpolation::Linear;
	std::vector<float> times;
	// XYZ for translation/scale; XYZW for rotation. Unused fourth slot is zero.
	std::vector<std::array<float, 4>> values;
};

struct ActorClip {
	std::string name;
	float startTime = 0;
	float endTime = 0;
	std::vector<ActorTrack> tracks;
};

struct ActorModel {
	std::string assetId;
	std::string variant;
	std::string revision;
	std::string sourceSha256;
	std::vector<ActorNode> nodes;
	std::vector<ActorJoint> joints;
	std::vector<ActorSkinVertex> vertices;
	std::vector<std::uint32_t> indices;
	std::vector<ActorClip> clips;
	std::uint32_t textureWidth = 0;
	std::uint32_t textureHeight = 0;
	std::vector<std::uint8_t> textureRgb;
};

// D3DACTR1 is separate from the static scenery format. On failure output is
// unchanged and error describes the rejected field; no simulation is touched.
bool LoadActorModel(std::span<const std::uint8_t> bytes, ActorModel &output, std::string &error);

} // namespace devilution

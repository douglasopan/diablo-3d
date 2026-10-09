// Standalone authorial fixtures. Compile only with actor_model.cpp,
// actor_pose.cpp and actor_visual_snapshot.cpp; no engine globals or game data.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include "engine/render/actor_visual_snapshot.hpp"

namespace {
using namespace devilution;

int Checks = 0;
int Failures = 0;

void Check(bool condition, const std::string &description)
{
	++Checks;
	if (!condition) {
		++Failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}

bool Near(float actual, float expected, float tolerance = 0.00001F)
{
	return std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= tolerance;
}

bool Near(ActorVec3 actual, ActorVec3 expected, float tolerance = 0.00001F)
{
	return Near(actual.x, expected.x, tolerance) && Near(actual.y, expected.y, tolerance) && Near(actual.z, expected.z, tolerance);
}

ActorMatrix Identity()
{
	return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

struct Bytes {
	std::vector<std::uint8_t> data;
	void U8(std::uint8_t value) { data.push_back(value); }
	void U16(std::uint16_t value)
	{
		U8(static_cast<std::uint8_t>(value));
		U8(static_cast<std::uint8_t>(value >> 8));
	}
	void U32(std::uint32_t value)
	{
		for (unsigned shift = 0; shift < 32; shift += 8)
			U8(static_cast<std::uint8_t>(value >> shift));
	}
	void I32(std::int32_t value) { U32(std::bit_cast<std::uint32_t>(value)); }
	void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
	void String(const std::string &value)
	{
		U16(static_cast<std::uint16_t>(value.size()));
		data.insert(data.end(), value.begin(), value.end());
	}
	void Vec3(ActorVec3 value)
	{
		F32(value.x);
		F32(value.y);
		F32(value.z);
	}
};

std::vector<std::uint8_t> Serialize(const ActorModel &model)
{
	Bytes bytes;
	for (char value : std::string("D3DACTR1"))
		bytes.U8(static_cast<std::uint8_t>(value));
	bytes.U32(1);
	bytes.U32(static_cast<std::uint32_t>(model.nodes.size()));
	bytes.U32(static_cast<std::uint32_t>(model.joints.size()));
	bytes.U32(static_cast<std::uint32_t>(model.vertices.size()));
	bytes.U32(static_cast<std::uint32_t>(model.indices.size()));
	bytes.U32(static_cast<std::uint32_t>(model.clips.size()));
	bytes.U32(model.textureWidth);
	bytes.U32(model.textureHeight);
	bytes.String(model.assetId);
	bytes.String(model.variant);
	bytes.String(model.revision);
	bytes.String(model.sourceSha256);
	for (const ActorNode &node : model.nodes) {
		bytes.I32(node.parent);
		bytes.String(node.name);
		bytes.Vec3(node.rest.translation);
		bytes.F32(node.rest.rotation.x);
		bytes.F32(node.rest.rotation.y);
		bytes.F32(node.rest.rotation.z);
		bytes.F32(node.rest.rotation.w);
		bytes.Vec3(node.rest.scale);
	}
	for (const ActorJoint &joint : model.joints) {
		bytes.U32(joint.node);
		for (float value : joint.inverseBind)
			bytes.F32(value);
	}
	for (const ActorSkinVertex &vertex : model.vertices) {
		bytes.Vec3(vertex.position);
		bytes.Vec3(vertex.normal);
		bytes.F32(vertex.uv.x);
		bytes.F32(vertex.uv.y);
		for (std::uint16_t joint : vertex.joints)
			bytes.U16(joint);
		for (float weight : vertex.weights)
			bytes.F32(weight);
	}
	for (std::uint32_t index : model.indices)
		bytes.U32(index);
	for (const ActorClip &clip : model.clips) {
		bytes.String(clip.name);
		bytes.F32(clip.startTime);
		bytes.F32(clip.endTime);
		bytes.U32(static_cast<std::uint32_t>(clip.tracks.size()));
		for (const ActorTrack &track : clip.tracks) {
			bytes.U32(track.node);
			bytes.U8(static_cast<std::uint8_t>(track.path));
			bytes.U8(static_cast<std::uint8_t>(track.interpolation));
			bytes.U32(static_cast<std::uint32_t>(track.times.size()));
			for (float time : track.times)
				bytes.F32(time);
			for (const auto &value : track.values)
				for (size_t component = 0; component < (track.path == ActorTrackPath::Rotation ? 4U : 3U); ++component)
					bytes.F32(value[component]);
		}
	}
	bytes.data.insert(bytes.data.end(), model.textureRgb.begin(), model.textureRgb.end());
	return bytes.data;
}

void SetU32(std::vector<std::uint8_t> &bytes, size_t offset, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

ActorModel Fixture()
{
	ActorModel model;
	model.assetId = "test.authorial.actor";
	model.variant = "two-node-triangle";
	model.revision = "synthetic-r1";
	model.sourceSha256 = std::string(64, 'a');
	ActorNode child;
	child.parent = 1; // Parent follows child in the stored graph.
	child.name = "child";
	child.rest.translation = { 1, 0, 0 };
	ActorNode root;
	root.name = "root";
	root.rest.translation = { 10, 0, 0 };
	model.nodes = { child, root };
	ActorJoint joint;
	joint.node = 0;
	joint.inverseBind = Identity();
	joint.inverseBind[12] = -11;
	model.joints = { joint };
	for (ActorVec3 position : std::array<ActorVec3, 3> { ActorVec3 { 0, 0, 0 }, ActorVec3 { 1, 0, 0 }, ActorVec3 { 0, 1, 0 } }) {
		ActorSkinVertex vertex;
		vertex.position = position;
		vertex.normal = { 1, 0, 0 };
		vertex.uv = { position.x, position.y };
		vertex.weights = { 1, 0, 0, 0 };
		model.vertices.push_back(vertex);
	}
	model.indices = { 0, 1, 2 };
	ActorTrack translation;
	translation.node = 0;
	translation.path = ActorTrackPath::Translation;
	translation.times = { 0, 1 };
	translation.values = { std::array<float, 4> { 1, 0, 0, 0 }, std::array<float, 4> { 3, 0, 0, 0 } };
	ActorClip clip;
	clip.name = "authorial-translation";
	clip.endTime = 1;
	clip.tracks = { translation };
	model.clips = { clip };
	model.textureWidth = 1;
	model.textureHeight = 1;
	model.textureRgb = { 100, 120, 140 };
	return model;
}

std::vector<std::uint8_t> PoseBytes(const ActorPose &pose)
{
	Bytes bytes;
	bytes.U32(static_cast<std::uint32_t>(pose.worldNodes.size()));
	for (const ActorMatrix &matrix : pose.worldNodes)
		for (float value : matrix)
			bytes.F32(value);
	bytes.U32(static_cast<std::uint32_t>(pose.jointPalette.size()));
	for (const ActorMatrix &matrix : pose.jointPalette)
		for (float value : matrix)
			bytes.F32(value);
	bytes.U32(static_cast<std::uint32_t>(pose.positions.size()));
	for (ActorVec3 position : pose.positions)
		bytes.Vec3(position);
	bytes.U32(static_cast<std::uint32_t>(pose.normals.size()));
	for (ActorVec3 normal : pose.normals)
		bytes.Vec3(normal);
	return bytes.data;
}

void RejectLoad(const std::vector<std::uint8_t> &bytes, const std::string &description)
{
	ActorModel output = Fixture();
	output.revision = "preserve-on-rejection";
	const auto before = Serialize(output);
	std::string error;
	Check(!LoadActorModel(bytes, output, error), "loader rejects " + description);
	Check(!error.empty(), "loader reports reason for " + description);
	Check(Serialize(output) == before, "loader failure is atomic: " + description);
}

void LoaderChecks()
{
	const ActorModel fixture = Fixture();
	const auto good = Serialize(fixture);
	ActorModel loaded;
	std::string error = "old diagnostic";
	Check(LoadActorModel(good, loaded, error), "D3DACTR1 authorial model loads");
	Check(error.empty(), "successful load clears stale diagnostic");
	Check(Serialize(loaded) == good, "all authorial model fields survive binary load");
	Check(loaded.nodes.size() == 2 && loaded.nodes[0].parent == 1 && loaded.joints[0].node == 0, "loader preserves graph order and palette/node distinction");
	Check(loaded.textureRgb == fixture.textureRgb, "RGB texture bytes survive load");
	ActorVisualInput composedInput;
	composedInput.assetId = loaded.assetId;
	composedInput.variant = loaded.variant;
	composedInput.revision = loaded.revision;
	composedInput.sourceSha256 = loaded.sourceSha256;
	ActorVisualSnapshot composedSnapshot;
	Check(MakeActorVisualSnapshot(composedInput, composedSnapshot, error), "canonical loaded SHA composes directly into validated DTO");
	Check(composedSnapshot.sourceSha256 == loaded.sourceSha256 && error.empty(), "Load to DTO preserves canonical SHA without case conversion");
	auto bad = good;
	bad.resize(10);
	RejectLoad(bad, "truncated header");
	bad = good;
	bad.pop_back();
	RejectLoad(bad, "truncated payload");
	bad = good;
	bad[0] = 'X';
	RejectLoad(bad, "wrong magic");
	bad = good;
	SetU32(bad, 8, 2);
	RejectLoad(bad, "unsupported version");
	bad = good;
	SetU32(bad, 12, 0);
	RejectLoad(bad, "zero node count");
	bad = good;
	SetU32(bad, 20, std::numeric_limits<std::uint32_t>::max());
	RejectLoad(bad, "oversized vertex count");
	bad = good;
	SetU32(bad, 24, 4);
	RejectLoad(bad, "non-triangle index count");
	bad = good;
	SetU32(bad, 32, 2049);
	RejectLoad(bad, "oversized texture dimension");
	bad = good;
	bad.push_back(0);
	RejectLoad(bad, "trailing bytes");
	ActorModel invalid = fixture;
	invalid.nodes[1].parent = 0;
	RejectLoad(Serialize(invalid), "cyclic graph");
	invalid = fixture;
	invalid.nodes[0].parent = 2;
	RejectLoad(Serialize(invalid), "invalid parent index");
	invalid = fixture;
	invalid.joints[0].node = 2;
	RejectLoad(Serialize(invalid), "invalid joint node");
	invalid = fixture;
	invalid.vertices[0].joints[0] = 1;
	RejectLoad(Serialize(invalid), "invalid vertex palette index");
	invalid = fixture;
	invalid.indices[2] = 3;
	RejectLoad(Serialize(invalid), "invalid triangle vertex index");
	invalid = fixture;
	invalid.vertices[0].weights = { -0.1F, 1.1F, 0, 0 };
	RejectLoad(Serialize(invalid), "negative skin weight");
	invalid = fixture;
	invalid.vertices[0].weights[0] = 0.5F;
	RejectLoad(Serialize(invalid), "unnormalized skin weights");
	invalid = fixture;
	invalid.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
	RejectLoad(Serialize(invalid), "nonfinite vertex");
	invalid = fixture;
	invalid.vertices[0].normal = {};
	RejectLoad(Serialize(invalid), "zero vertex normal");
	invalid = fixture;
	invalid.joints[0].inverseBind[0] = 0;
	RejectLoad(Serialize(invalid), "singular inverse bind");
	invalid = fixture;
	invalid.joints[0].inverseBind[3] = 0.5F;
	RejectLoad(Serialize(invalid), "projective inverse bind");
	invalid = fixture;
	invalid.nodes[0].rest.scale.x = 0;
	RejectLoad(Serialize(invalid), "singular local scale");
	invalid = fixture;
	invalid.nodes[0].rest.rotation.w = 2;
	RejectLoad(Serialize(invalid), "nonunit rest quaternion");
	invalid = fixture;
	invalid.clips[0].tracks[0].times[1] = 0;
	RejectLoad(Serialize(invalid), "nonincreasing key times");
	invalid = fixture;
	invalid.clips[0].tracks[0].times[1] = 2;
	RejectLoad(Serialize(invalid), "key outside clip range");
	invalid = fixture;
	invalid.clips[0].tracks[0].times[1] = std::numeric_limits<float>::infinity();
	RejectLoad(Serialize(invalid), "nonfinite key time");
	invalid = fixture;
	invalid.clips[0].tracks.push_back(invalid.clips[0].tracks[0]);
	RejectLoad(Serialize(invalid), "duplicate transform path");
	invalid = fixture;
	invalid.clips[0].tracks[0].interpolation = static_cast<ActorTrackInterpolation>(2);
	RejectLoad(Serialize(invalid), "unsupported interpolation");
	invalid = fixture;
	invalid.sourceSha256 = std::string(64, 'A');
	RejectLoad(Serialize(invalid), "uppercase source identity cannot compose with canonical DTO");
	invalid = fixture;
	invalid.sourceSha256[0] = 'g';
	RejectLoad(Serialize(invalid), "nonhex source identity");
}

void RejectPose(const ActorModel &model, const std::string &description, bool bind = true,
    size_t clip = 0, float seconds = 0.5F)
{
	ActorPose output;
	output.worldNodes = { Identity() };
	output.jointPalette = { Identity() };
	output.positions = { ActorVec3 { 37, 41, 43 } };
	output.normals = { ActorVec3 { 0, 1, 0 } };
	const auto before = PoseBytes(output);
	std::string error;
	const bool accepted = bind ? EvaluateActorBindPose(model, output, error) : EvaluateActorPose(model, clip, seconds, output, error);
	Check(!accepted, "pose rejects " + description);
	Check(!error.empty(), "pose reports reason for " + description);
	Check(PoseBytes(output) == before, "pose failure is atomic: " + description);
}

void PoseChecks()
{
	const ActorModel fixture = Fixture();
	const auto source = Serialize(fixture);
	ActorPose bind;
	std::string error = "old diagnostic";
	Check(EvaluateActorBindPose(fixture, bind, error), "bind pose evaluates");
	Check(error.empty(), "successful pose clears stale diagnostic");
	Check(bind.worldNodes.size() == 2 && bind.jointPalette.size() == 1 && bind.positions.size() == 3 && bind.normals.size() == 3, "pose output has correct independent array sizes");
	if (bind.positions.size() != 3 || bind.worldNodes.size() != 2 || bind.jointPalette.size() != 1)
		return;
	Check(Near(bind.worldNodes[0][12], 11) && Near(bind.worldNodes[1][12], 10), "child-before-parent graph composes parent transform");
	Check(Near(bind.jointPalette[0][12], 0), "world joint and translated inverse bind cancel in rest pose");
	for (size_t i = 0; i < fixture.vertices.size(); ++i) {
		Check(Near(bind.positions[i], fixture.vertices[i].position), "bind pose restores original vertex " + std::to_string(i));
		Check(Near(bind.normals[i], { 1, 0, 0 }), "bind pose preserves normal " + std::to_string(i));
	}
	ActorPose early;
	ActorPose late;
	Check(EvaluateActorPose(fixture, 0, 0.25F, early, error), "first translation sample evaluates");
	Check(EvaluateActorPose(fixture, 0, 0.75F, late, error), "second translation sample evaluates");
	if (early.positions.size() == 3 && late.positions.size() == 3) {
		Check(Near(early.positions[0], { 0.5F, 0, 0 }) && Near(late.positions[0], { 1.5F, 0, 0 }), "LINEAR poses use authored translation and inverse bind");
		Check(!Near(early.positions[0], late.positions[0]), "different clip samples deform geometry differently");
	}
	ActorPose clamped;
	Check(EvaluateActorPose(fixture, 0, -100, clamped, error) && Near(clamped.positions[0], { 0, 0, 0 }), "sample before clip holds first key");
	Check(EvaluateActorPose(fixture, 0, 100, clamped, error) && Near(clamped.positions[0], { 2, 0, 0 }), "sample after clip holds last key without looping");
	ActorModel step = fixture;
	step.clips[0].tracks[0].interpolation = ActorTrackInterpolation::Step;
	Check(EvaluateActorPose(step, 0, 0.999F, clamped, error) && Near(clamped.positions[0], { 0, 0, 0 }), "STEP holds preceding value before next key");
	Check(EvaluateActorPose(step, 0, 1, clamped, error) && Near(clamped.positions[0], { 2, 0, 0 }), "STEP changes at exact next key");
	ActorModel single = fixture;
	single.clips[0].tracks[0].times = { 0.3F };
	single.clips[0].tracks[0].values = { std::array<float, 4> { 2, 0, 0, 0 } };
	Check(EvaluateActorPose(single, 0, 0, clamped, error) && Near(clamped.positions[0], { 1, 0, 0 }), "one-key track holds before its timestamp");
	Check(EvaluateActorPose(single, 0, 1, clamped, error) && Near(clamped.positions[0], { 1, 0, 0 }), "one-key track holds after its timestamp");
	ActorModel rotation = fixture;
	auto &rotationTrack = rotation.clips[0].tracks[0];
	rotationTrack.path = ActorTrackPath::Rotation;
	rotationTrack.values = { std::array<float, 4> { 0, 0, 0, 1 }, std::array<float, 4> { 0, 0, 1, 0 } };
	Check(EvaluateActorPose(rotation, 0, 0.5F, clamped, error) && Near(clamped.normals[0], { 0, 1, 0 }), "slerp identity to Z180 rotates normal X to Y halfway");
	rotationTrack.values[1] = { 0, 0, 0, -1 };
	Check(EvaluateActorPose(rotation, 0, 0.5F, clamped, error) && Near(clamped.normals[0], { 1, 0, 0 }), "antipodal quaternions interpolate without zero/long-arc rotation");
	const float diagonal = std::sqrt(0.5F);
	rotationTrack.values[1] = { 0, 0, -diagonal, -diagonal };
	Check(EvaluateActorPose(rotation, 0, 0.5F, clamped, error) && Near(clamped.normals[0], { diagonal, diagonal, 0 }), "negative representation takes shortest physical arc");
	ActorModel scale = fixture;
	for (ActorSkinVertex &vertex : scale.vertices)
		vertex.normal = { diagonal, diagonal, 0 };
	auto &scaleTrack = scale.clips[0].tracks[0];
	scaleTrack.path = ActorTrackPath::Scale;
	scaleTrack.times = { 0 };
	scaleTrack.values = { std::array<float, 4> { 2, 1, 1, 0 } };
	Check(EvaluateActorPose(scale, 0, 0.5F, clamped, error) && Near(clamped.normals[0], { 0.4472135955F, 0.894427191F, 0 }), "nonuniform scale uses inverse-transpose normals, not position matrix");
	ActorModel rootMotion = fixture;
	rootMotion.clips[0].tracks[0].node = 1;
	rootMotion.clips[0].tracks[0].values = { std::array<float, 4> { 10, 0, 0, 0 }, std::array<float, 4> { 12, 0, 0, 0 } };
	Check(EvaluateActorPose(rootMotion, 0, 0.5F, clamped, error) && Near(clamped.positions[0], { 1, 0, 0 }), "pose retains explicit source root motion for later presentation policy");
	Check(Serialize(fixture) == source, "bind and sampled evaluation never mutate the source model");
	ActorModel invalid = fixture;
	invalid.nodes[1].parent = 0;
	RejectPose(invalid, "cyclic hierarchy");
	invalid = fixture;
	invalid.vertices[0].weights[0] = 0.1F;
	RejectPose(invalid, "invalid weight total");
	invalid = fixture;
	invalid.vertices[0].joints[0] = 1;
	RejectPose(invalid, "invalid joint palette reference");
	invalid = fixture;
	invalid.joints[0].inverseBind[0] = 0;
	RejectPose(invalid, "singular inverse bind");
	invalid = fixture;
	invalid.joints[0].inverseBind[3] = 0.5F;
	RejectPose(invalid, "projective inverse bind");
	invalid = fixture;
	invalid.nodes[0].rest.scale.x = 0;
	RejectPose(invalid, "singular transform scale");
	RejectPose(fixture, "invalid clip index", false, 1);
	RejectPose(fixture, "nonfinite sample time", false, 0, std::numeric_limits<float>::quiet_NaN());
}

ActorVisualInput VisualFixture()
{
	ActorVisualInput input;
	input.nativeIndex = 37;
	input.action = ActorVisualAction::Attack;
	input.assetId = "test.authorial.actor";
	input.variant = "two-node-triangle";
	input.revision = "synthetic-r1";
	input.sourceSha256 = std::string(64, 'a');
	input.authoritativeFootpoint = { 72.25F, 0, 69.5F };
	input.nativeDirection = 4;
	input.logicalFrame = 3;
	input.displayedFrame = 5;
	input.frameCount = 16;
	input.ticksPerFrame = 2;
	input.tickCounter = 1;
	input.progressToNextTick128 = 64;
	input.nativeImpactFrame = 8;
	input.paused = true;
	input.frozen = true;
	input.reversed = true;
	input.terminalHold = true;
	input.localPlayer = true;
	return input;
}

std::vector<std::uint8_t> VisualBytes(const ActorVisualInput &input)
{
	Bytes bytes;
	bytes.U8(static_cast<std::uint8_t>(input.kind));
	bytes.U32(input.nativeIndex);
	bytes.U8(static_cast<std::uint8_t>(input.action));
	bytes.String(input.assetId);
	bytes.String(input.variant);
	bytes.String(input.revision);
	bytes.String(input.sourceSha256);
	bytes.Vec3(input.authoritativeFootpoint);
	bytes.U8(input.nativeDirection);
	bytes.I32(input.logicalFrame);
	bytes.I32(input.displayedFrame);
	bytes.U32(input.frameCount);
	bytes.U32(input.ticksPerFrame);
	bytes.U32(input.tickCounter);
	bytes.U16(input.progressToNextTick128);
	bytes.I32(input.nativeImpactFrame);
	bytes.I32(input.nativeEmissionFrame);
	bytes.U32(input.sequenceIndex);
	bytes.U32(input.sequenceLength);
	for (bool value : { input.paused, input.frozen, input.reversed, input.terminalHold, input.hidden, input.localPlayer })
		bytes.U8(value ? 1 : 0);
	return bytes.data;
}

void RejectVisual(const ActorVisualInput &input, const std::string &description)
{
	ActorVisualSnapshot output;
	static_cast<ActorVisualInput &>(output) = VisualFixture();
	output.nativeYawRadians = 1.2345F;
	const auto before = VisualBytes(output);
	const float yaw = output.nativeYawRadians;
	std::string error;
	Check(!MakeActorVisualSnapshot(input, output, error), "snapshot rejects " + description);
	Check(!error.empty(), "snapshot reports reason for " + description);
	Check(VisualBytes(output) == before && output.nativeYawRadians == yaw, "snapshot failure is atomic: " + description);
}

void SnapshotChecks()
{
	const auto input = VisualFixture();
	const auto source = VisualBytes(input);
	ActorVisualSnapshot snapshot;
	std::string error = "old diagnostic";
	Check(MakeActorVisualSnapshot(input, snapshot, error), "read-only native snapshot validates");
	Check(error.empty() && VisualBytes(snapshot) == source, "snapshot preserves every native/identity/phase flag and clears old error");
	Check(snapshot.logicalFrame == 3 && snapshot.displayedFrame == 5, "displayed frame remains independent from logical frame");
	Check(snapshot.nativeIndex == 37 && snapshot.kind == ActorVisualKind::Player, "native picking identity survives copy");
	Check(Near(snapshot.authoritativeFootpoint, input.authoritativeFootpoint), "snapshot never applies model root motion to footpoint");
	const float diagonal = std::sqrt(0.5F);
	const std::array<ActorVec3, 8> facing {
		ActorVec3 { diagonal, 0, diagonal }, ActorVec3 { 0, 0, 1 }, ActorVec3 { -diagonal, 0, diagonal }, ActorVec3 { -1, 0, 0 },
		ActorVec3 { -diagonal, 0, -diagonal }, ActorVec3 { 0, 0, -1 }, ActorVec3 { diagonal, 0, -diagonal }, ActorVec3 { 1, 0, 0 },
	};
	for (std::uint8_t direction = 0; direction < 8; ++direction) {
		auto directed = input;
		directed.nativeDirection = direction;
		Check(MakeActorVisualSnapshot(directed, snapshot, error), "native facing validates: " + std::to_string(direction));
		const float yaw = ActorNativeDirectionYaw(direction);
		Check(Near(yaw, snapshot.nativeYawRadians) && Near(ActorVec3 { std::sin(yaw), 0, std::cos(yaw) }, facing[direction]), "eight-direction yaw agrees with native map axes: " + std::to_string(direction));
	}
	MakeActorVisualSnapshot(input, snapshot, error);
	Check(!ActorVisibleInCamera(snapshot, true, true), "first-person hides local player body when requested");
	Check(ActorVisibleInCamera(snapshot, true, false), "first-person can retain local body");
	Check(ActorVisibleInCamera(snapshot, false, true), "third-person retains local body");
	const auto beforeVisibility = VisualBytes(snapshot);
	snapshot.localPlayer = false;
	Check(ActorVisibleInCamera(snapshot, true, true), "first-person never hides another player");
	for (auto kind : { ActorVisualKind::Towner, ActorVisualKind::Monster, ActorVisualKind::ChargeMissile }) {
		snapshot.kind = kind;
		snapshot.localPlayer = true;
		Check(ActorVisibleInCamera(snapshot, true, true), "first-person body policy leaves non-player actors visible");
	}
	MakeActorVisualSnapshot(input, snapshot, error);
	ActorVisibleInCamera(snapshot, true, true);
	Check(VisualBytes(snapshot) == beforeVisibility && snapshot.nativeIndex == input.nativeIndex, "camera visibility does not modify native picking or footpoint");
	snapshot.hidden = true;
	Check(!ActorVisibleInCamera(snapshot, false, false), "explicit visual hidden flag applies to third-person");
	Check(!ActorVisibleInCamera(snapshot, true, false), "explicit visual hidden flag applies to first-person");
	ActorVisualInput sequence = input;
	sequence.sequenceLength = 130;
	sequence.sequenceIndex = 129;
	Check(MakeActorVisualSnapshot(sequence, snapshot, error) && snapshot.sequenceIndex == 129 && snapshot.displayedFrame == 5, "NPC sequence phase is independent from current sprite frame");
	ActorVisualInput invalid = input;
	invalid.nativeDirection = 8;
	RejectVisual(invalid, "native NoDirection");
	invalid = input;
	invalid.nativeDirection = 255;
	RejectVisual(invalid, "invalid native direction");
	invalid = input;
	invalid.authoritativeFootpoint.z = std::numeric_limits<float>::infinity();
	RejectVisual(invalid, "nonfinite footpoint");
	invalid = input;
	invalid.frameCount = 0;
	RejectVisual(invalid, "zero native frames");
	invalid = input;
	invalid.frameCount = 100001;
	RejectVisual(invalid, "unbounded native frames");
	invalid = input;
	invalid.ticksPerFrame = 0;
	RejectVisual(invalid, "zero frame delay");
	invalid = input;
	invalid.tickCounter = invalid.ticksPerFrame;
	RejectVisual(invalid, "tick counter outside frame delay");
	invalid = input;
	invalid.progressToNextTick128 = 129;
	RejectVisual(invalid, "invalid render tick fraction");
	invalid = input;
	invalid.logicalFrame = -9;
	RejectVisual(invalid, "logical frame before supported startup preview");
	invalid = input;
	invalid.displayedFrame = 16;
	RejectVisual(invalid, "unreachable displayed frame");
	invalid = input;
	invalid.nativeImpactFrame = 16;
	RejectVisual(invalid, "unreachable impact marker");
	invalid = input;
	invalid.nativeEmissionFrame = -2;
	RejectVisual(invalid, "invalid emission sentinel");
	invalid = input;
	invalid.sequenceIndex = 1;
	RejectVisual(invalid, "sequence phase without sequence");
	invalid = sequence;
	invalid.sequenceIndex = invalid.sequenceLength;
	RejectVisual(invalid, "sequence phase at exclusive end");
	invalid = input;
	invalid.kind = static_cast<ActorVisualKind>(255);
	RejectVisual(invalid, "unknown actor visual kind");
	invalid = input;
	invalid.action = static_cast<ActorVisualAction>(255);
	RejectVisual(invalid, "unknown actor visual action");
	invalid = input;
	invalid.revision.clear();
	RejectVisual(invalid, "missing explicit revision");
	invalid = input;
	invalid.sourceSha256[0] = 'A';
	RejectVisual(invalid, "noncanonical source hash");
	Check(VisualBytes(input) == source, "snapshot validation and cameras never mutate native input");
}

struct Bounds {
	ActorVec3 minimum;
	ActorVec3 maximum;
	float Height() const { return maximum.y - minimum.y; }
};

Bounds PoseBounds(const ActorPose &pose)
{
	Bounds bounds;
	if (pose.positions.empty())
		return bounds;
	bounds.minimum = bounds.maximum = pose.positions.front();
	for (ActorVec3 value : pose.positions) {
		bounds.minimum.x = std::min(bounds.minimum.x, value.x);
		bounds.minimum.y = std::min(bounds.minimum.y, value.y);
		bounds.minimum.z = std::min(bounds.minimum.z, value.z);
		bounds.maximum.x = std::max(bounds.maximum.x, value.x);
		bounds.maximum.y = std::max(bounds.maximum.y, value.y);
		bounds.maximum.z = std::max(bounds.maximum.z, value.z);
	}
	return bounds;
}

bool ValidNormals(const ActorPose &pose)
{
	return std::all_of(pose.normals.begin(), pose.normals.end(), [](ActorVec3 normal) {
		return Near(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z, 1, 0.0001F);
	});
}

void RealAssetChecks(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	Check(static_cast<bool>(file), "explicit private actor file is readable");
	if (!file)
		return;
	file.seekg(0, std::ios::end);
	const auto size = file.tellg();
	Check(size > 0 && size <= 128 * 1024 * 1024, "private actor file is within importer byte limit");
	if (size <= 0 || size > 128 * 1024 * 1024)
		return;
	file.seekg(0, std::ios::beg);
	const std::istreambuf_iterator<char> begin(file);
	const std::istreambuf_iterator<char> end;
	const std::vector<std::uint8_t> bytes(begin, end);
	ActorModel model;
	std::string error;
	Check(LoadActorModel(bytes, model, error), "explicit private actor loads: " + error);
	if (!error.empty() || model.vertices.empty())
		return;
	Check(std::any_of(model.nodes.begin(), model.nodes.end(), [](const ActorNode &node) {
		return Near(node.rest.scale.x, 0.01F, 0.000001F) && Near(node.rest.scale.y, 0.01F, 0.000001F) && Near(node.rest.scale.z, 0.01F, 0.000001F);
	}), "source 0.01 skeleton root scale is preserved");
	ActorPose bind;
	Check(EvaluateActorBindPose(model, bind, error), "private actor bind pose evaluates: " + error);
	if (!error.empty() || bind.positions.empty())
		return;
	const Bounds bindBounds = PoseBounds(bind);
	Check(bindBounds.Height() > 1.75F && bindBounds.Height() < 1.85F, "worldJoint * IBM keeps private bind height near 1.8, without 100x inflation");
	Check(ValidNormals(bind), "private bind normals are finite and normalized");
	Check(!model.clips.empty(), "private actor retains an animation clip");
	if (model.clips.empty())
		return;
	const ActorClip &clip = model.clips[0];
	ActorPose first;
	ActorPose second;
	Check(EvaluateActorPose(model, 0, clip.startTime + (clip.endTime - clip.startTime) * 0.25F, first, error), "private first clip sample evaluates: " + error);
	Check(EvaluateActorPose(model, 0, clip.startTime + (clip.endTime - clip.startTime) * 0.75F, second, error), "private second clip sample evaluates: " + error);
	if (first.positions.empty() || first.positions.size() != second.positions.size())
		return;
	const Bounds firstBounds = PoseBounds(first);
	const Bounds secondBounds = PoseBounds(second);
	Check(firstBounds.Height() > 1.7F && firstBounds.Height() < 2.2F && secondBounds.Height() > 1.7F && secondBounds.Height() < 2.2F, "private idle sampled height stays within observed deformation bounds");
	Check(firstBounds.minimum.y > -0.25F && firstBounds.minimum.y < 0.25F && secondBounds.minimum.y > -0.25F && secondBounds.minimum.y < 0.25F, "private idle feet remain near source ground, without hiding penetration");
	Check(ValidNormals(first) && ValidNormals(second), "private sampled normals are finite and normalized");
	bool different = false;
	for (size_t index = 0; index < first.positions.size(); ++index)
		different = different || !Near(first.positions[index], second.positions[index], 0.0001F);
	Check(different, "private idle clip visibly changes skinned geometry between samples");
	std::cout << "REAL_ACTOR source_sha256=" << model.sourceSha256
	          << " vertices=" << model.vertices.size() << " joints=" << model.joints.size()
	          << " bind_height=" << bindBounds.Height() << " sampled_heights=" << firstBounds.Height() << ',' << secondBounds.Height() << '\n';
}

} // namespace

int main(int argc, char **argv)
{
	try {
		LoaderChecks();
		PoseChecks();
		SnapshotChecks();
		if (argc == 2)
			RealAssetChecks(argv[1]);
		else if (argc > 2)
			Check(false, "usage: actor_pose_smoke [explicit-private-actor-file]");
	} catch (const std::exception &error) {
		Check(false, std::string("unexpected diagnostic exception: ") + error.what());
	}
	std::cout << "ACTOR_POSE_SMOKE CHECKS=" << Checks << " FAILURES=" << Failures << '\n';
	return Failures == 0 ? 0 : 1;
}

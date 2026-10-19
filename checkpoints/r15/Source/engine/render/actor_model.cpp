#include "engine/render/actor_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace devilution {
namespace {

constexpr std::size_t MaxActorBytes = 128U * 1024U * 1024U;
constexpr double UnitTolerance = 1.0e-3;
constexpr double AffineTolerance = 1.0e-5;

struct ParseError : std::runtime_error {
	using std::runtime_error::runtime_error;
};

[[noreturn]] void Reject(const char *field, const char *reason)
{
	throw ParseError(std::string(field) + ": " + reason);
}

bool IsUtf8(std::span<const std::uint8_t> bytes)
{
	for (std::size_t i = 0; i < bytes.size();) {
		const std::uint8_t first = bytes[i++];
		if (first <= 0x7F) {
			// Names and identities must be safe to display and compare as strings.
			if (first < 0x20 || first == 0x7F)
				return false;
			continue;
		}
		std::uint32_t value;
		std::uint32_t minimum;
		std::size_t extra;
		if (first >= 0xC2 && first <= 0xDF) {
			value = first & 0x1F;
			minimum = 0x80;
			extra = 1;
		} else if (first >= 0xE0 && first <= 0xEF) {
			value = first & 0x0F;
			minimum = 0x800;
			extra = 2;
		} else if (first >= 0xF0 && first <= 0xF4) {
			value = first & 0x07;
			minimum = 0x10000;
			extra = 3;
		} else {
			return false;
		}
		if (extra > bytes.size() - i)
			return false;
		for (std::size_t j = 0; j < extra; ++j) {
			const std::uint8_t continuation = bytes[i++];
			if ((continuation & 0xC0) != 0x80)
				return false;
			value = (value << 6) | (continuation & 0x3F);
		}
		if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
			return false;
	}
	return true;
}

class Reader {
public:
	explicit Reader(std::span<const std::uint8_t> bytes)
	    : bytes_(bytes)
	{
	}

	void Require(std::size_t count, const char *field) const
	{
		if (count > bytes_.size() - position_)
			Reject(field, "truncated input");
	}

	std::span<const std::uint8_t> Take(std::size_t count, const char *field)
	{
		Require(count, field);
		const auto result = bytes_.subspan(position_, count);
		position_ += count;
		return result;
	}

	std::uint8_t U8(const char *field)
	{
		return Take(1, field)[0];
	}

	std::uint16_t U16(const char *field)
	{
		const auto bytes = Take(2, field);
		return static_cast<std::uint16_t>(bytes[0] | (std::uint16_t { bytes[1] } << 8));
	}

	std::uint32_t U32(const char *field)
	{
		const auto bytes = Take(4, field);
		return std::uint32_t { bytes[0] } | (std::uint32_t { bytes[1] } << 8)
		    | (std::uint32_t { bytes[2] } << 16) | (std::uint32_t { bytes[3] } << 24);
	}

	std::int32_t I32(const char *field)
	{
		return std::bit_cast<std::int32_t>(U32(field));
	}

	float F32(const char *field)
	{
		const float value = std::bit_cast<float>(U32(field));
		if (!std::isfinite(value))
			Reject(field, "non-finite number");
		return value;
	}

	std::string String(const char *field)
	{
		const auto size = U16(field);
		if (size == 0 || size > 127)
			Reject(field, "UTF-8 byte length must be 1..127");
		const auto bytes = Take(size, field);
		if (!IsUtf8(bytes))
			Reject(field, "invalid UTF-8 or control character");
		return { reinterpret_cast<const char *>(bytes.data()), bytes.size() };
	}

	bool AtEnd() const
	{
		return position_ == bytes_.size();
	}

private:
	std::span<const std::uint8_t> bytes_;
	std::size_t position_ = 0;
};

ActorVec3 ReadVec3(Reader &reader, const char *field)
{
	return { reader.F32(field), reader.F32(field), reader.F32(field) };
}

void RequireScale(const ActorVec3 &scale, const char *field)
{
	if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
		Reject(field, "scale must be positive");
}

void RequireQuaternion(float x, float y, float z, float w, const char *field)
{
	const double norm = std::sqrt(double { x } * x + double { y } * y + double { z } * z + double { w } * w);
	if (std::abs(norm - 1.0) > UnitTolerance)
		Reject(field, "quaternion must have unit length within 1e-3");
}

void RequireInverseBind(const ActorMatrix &matrix)
{
	if (std::abs(matrix[3]) > AffineTolerance || std::abs(matrix[7]) > AffineTolerance
	    || std::abs(matrix[11]) > AffineTolerance || std::abs(double { matrix[15] } - 1.0) > AffineTolerance)
		Reject("joint.inverseBind", "matrix must be affine");
	const double determinant = double { matrix[0] } * (double { matrix[5] } * matrix[10] - double { matrix[9] } * matrix[6])
	    - double { matrix[4] } * (double { matrix[1] } * matrix[10] - double { matrix[9] } * matrix[2])
	    + double { matrix[8] } * (double { matrix[1] } * matrix[6] - double { matrix[5] } * matrix[2]);
	if (determinant == 0 || !std::isfinite(determinant))
		Reject("joint.inverseBind", "singular matrix");
}

std::uint32_t ReadCount(Reader &reader, const char *field, std::uint32_t maximum, bool allowZero = false)
{
	const auto count = reader.U32(field);
	if (count > maximum || (!allowZero && count == 0))
		Reject(field, "count outside format limits");
	return count;
}

void ReadNodes(Reader &reader, ActorModel &model, std::uint32_t count)
{
	reader.Require(std::size_t { count } * 47, "nodes");
	model.nodes.resize(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		auto &node = model.nodes[i];
		node.parent = reader.I32("node.parent");
		if (node.parent < -1 || node.parent >= static_cast<std::int32_t>(count) || node.parent == static_cast<std::int32_t>(i))
			Reject("node.parent", "invalid parent index");
		node.name = reader.String("node.name");
		node.rest.translation = ReadVec3(reader, "node.translation");
		node.rest.rotation = { reader.F32("node.rotation"), reader.F32("node.rotation"), reader.F32("node.rotation"), reader.F32("node.rotation") };
		const auto &rotation = node.rest.rotation;
		RequireQuaternion(rotation.x, rotation.y, rotation.z, rotation.w, "node.rotation");
		node.rest.scale = ReadVec3(reader, "node.scale");
		RequireScale(node.rest.scale, "node.scale");
	}
	// Parent order is unrestricted. Every chain must terminate at a root.
	for (std::uint32_t i = 0; i < count; ++i) {
		std::int32_t current = static_cast<std::int32_t>(i);
		for (std::uint32_t depth = 0; current != -1; ++depth) {
			if (depth >= count)
				Reject("node.parent", "hierarchy contains a cycle");
			current = model.nodes[static_cast<std::size_t>(current)].parent;
		}
	}
}

void ReadJoints(Reader &reader, ActorModel &model, std::uint32_t count)
{
	reader.Require(std::size_t { count } * 68, "joints");
	model.joints.resize(count);
	std::array<bool, 256> seen {};
	for (auto &joint : model.joints) {
		joint.node = reader.U32("joint.node");
		if (joint.node >= model.nodes.size() || seen[joint.node])
			Reject("joint.node", "invalid or duplicate node index");
		seen[joint.node] = true;
		for (auto &value : joint.inverseBind)
			value = reader.F32("joint.inverseBind");
		RequireInverseBind(joint.inverseBind);
	}
}

void ReadVertices(Reader &reader, ActorModel &model, std::uint32_t count)
{
	reader.Require(std::size_t { count } * 56, "vertices");
	model.vertices.resize(count);
	for (auto &vertex : model.vertices) {
		vertex.position = ReadVec3(reader, "vertex.position");
		vertex.normal = ReadVec3(reader, "vertex.normal");
		if (vertex.normal.x == 0 && vertex.normal.y == 0 && vertex.normal.z == 0)
			Reject("vertex.normal", "normal must be nonzero");
		vertex.uv = { reader.F32("vertex.uv"), reader.F32("vertex.uv") };
		for (auto &joint : vertex.joints) {
			joint = reader.U16("vertex.joints");
			if (joint >= model.joints.size())
				Reject("vertex.joints", "index outside joint palette");
		}
		double sum = 0;
		for (auto &weight : vertex.weights) {
			weight = reader.F32("vertex.weights");
			if (weight < 0)
				Reject("vertex.weights", "negative influence");
			sum += weight;
		}
		if (std::abs(sum - 1.0) > UnitTolerance)
			Reject("vertex.weights", "influences must sum to one within 1e-3");
	}
}

void ReadClips(Reader &reader, ActorModel &model, std::uint32_t count)
{
	reader.Require(std::size_t { count } * 15, "clips");
	model.clips.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		ActorClip clip;
		clip.name = reader.String("clip.name");
		if (std::any_of(model.clips.begin(), model.clips.end(), [&](const ActorClip &other) { return other.name == clip.name; }))
			Reject("clip.name", "duplicate clip name");
		clip.startTime = reader.F32("clip.startTime");
		clip.endTime = reader.F32("clip.endTime");
		if (clip.startTime < 0 || clip.endTime < clip.startTime)
			Reject("clip.range", "expected nonnegative start and end >= start");
		const auto trackCount = ReadCount(reader, "clip.trackCount", 768, true);
		reader.Require(std::size_t { trackCount } * 26, "clip.tracks");
		clip.tracks.reserve(trackCount);
		std::array<std::array<bool, 3>, 256> seen {};
		for (std::uint32_t j = 0; j < trackCount; ++j) {
			ActorTrack track;
			track.node = reader.U32("track.node");
			if (track.node >= model.nodes.size())
				Reject("track.node", "invalid node index");
			const auto path = reader.U8("track.path");
			if (path > static_cast<std::uint8_t>(ActorTrackPath::Scale))
				Reject("track.path", "unsupported target path");
			if (seen[track.node][path])
				Reject("track.path", "duplicate node/path target");
			seen[track.node][path] = true;
			track.path = static_cast<ActorTrackPath>(path);
			const auto interpolation = reader.U8("track.interpolation");
			if (interpolation > static_cast<std::uint8_t>(ActorTrackInterpolation::Step))
				Reject("track.interpolation", "unsupported interpolation");
			track.interpolation = static_cast<ActorTrackInterpolation>(interpolation);
			const auto keyCount = ReadCount(reader, "track.keyCount", 100000);
			const std::size_t dimensions = track.path == ActorTrackPath::Rotation ? 4 : 3;
			reader.Require(std::size_t { keyCount } * (dimensions + 1) * 4, "track.keys");
			track.times.resize(keyCount);
			for (std::size_t k = 0; k < keyCount; ++k) {
				const float time = reader.F32("track.times");
				if (time < clip.startTime || time > clip.endTime || (k != 0 && time <= track.times[k - 1]))
					Reject("track.times", "times must increase strictly within clip range");
				track.times[k] = time;
			}
			track.values.resize(keyCount);
			for (auto &value : track.values) {
				for (std::size_t k = 0; k < dimensions; ++k)
					value[k] = reader.F32("track.values");
				if (track.path == ActorTrackPath::Rotation)
					RequireQuaternion(value[0], value[1], value[2], value[3], "track.rotation");
				else if (track.path == ActorTrackPath::Scale)
					RequireScale({ value[0], value[1], value[2] }, "track.scale");
			}
			clip.tracks.push_back(std::move(track));
		}
		model.clips.push_back(std::move(clip));
	}
}

ActorModel ParseActor(std::span<const std::uint8_t> bytes)
{
	if (bytes.size() > MaxActorBytes)
		Reject("file", "exceeds 128 MiB limit");
	Reader reader(bytes);
	const auto magic = reader.Take(8, "header.magic");
	if (std::memcmp(magic.data(), "D3DACTR1", 8) != 0)
		Reject("header.magic", "expected D3DACTR1");
	if (reader.U32("header.version") != 1)
		Reject("header.version", "unsupported version");
	const auto nodeCount = ReadCount(reader, "header.nodeCount", 256);
	const auto jointCount = ReadCount(reader, "header.jointCount", 96);
	const auto vertexCount = ReadCount(reader, "header.vertexCount", 100000);
	const auto indexCount = ReadCount(reader, "header.indexCount", 300000);
	if (indexCount % 3 != 0)
		Reject("header.indexCount", "triangle index count must be divisible by three");
	const auto clipCount = ReadCount(reader, "header.clipCount", 128, true);
	ActorModel model;
	model.textureWidth = ReadCount(reader, "header.textureWidth", 2048);
	model.textureHeight = ReadCount(reader, "header.textureHeight", 2048);
	model.assetId = reader.String("assetId");
	model.variant = reader.String("variant");
	model.revision = reader.String("revision");
	model.sourceSha256 = reader.String("sourceSha256");
	if (model.sourceSha256.size() != 64 || !std::all_of(model.sourceSha256.begin(), model.sourceSha256.end(), [](unsigned char c) {
		    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
	    }))
		Reject("sourceSha256", "expected 64 lowercase hexadecimal characters");
	ReadNodes(reader, model, nodeCount);
	ReadJoints(reader, model, jointCount);
	ReadVertices(reader, model, vertexCount);
	reader.Require(std::size_t { indexCount } * 4, "indices");
	model.indices.resize(indexCount);
	for (auto &index : model.indices) {
		index = reader.U32("index");
		if (index >= vertexCount)
			Reject("index", "invalid vertex index");
	}
	ReadClips(reader, model, clipCount);
	const auto texture = reader.Take(std::size_t { model.textureWidth } * model.textureHeight * 3, "textureRgb");
	if (!reader.AtEnd())
		Reject("file", "unexpected trailing bytes");
	model.textureRgb.assign(texture.begin(), texture.end());
	return model;
}

} // namespace

bool LoadActorModel(std::span<const std::uint8_t> bytes, ActorModel &output, std::string &error)
{
	try {
		ActorModel model = ParseActor(bytes);
		output = std::move(model);
		error.clear();
		return true;
	} catch (const ParseError &failure) {
		error = failure.what();
	} catch (const std::bad_alloc &) {
		error = "file: allocation failed";
	} catch (const std::length_error &) {
		error = "file: allocation length exceeds container limits";
	}
	return false;
}

} // namespace devilution

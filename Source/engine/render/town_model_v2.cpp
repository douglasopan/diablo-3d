#include "engine/render/town_model_v2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include <picosha2.h>

namespace devilution {
namespace {

constexpr size_t MaxBytes = 512ULL * 1024 * 1024;
using Digest = std::array<uint8_t, 32>;

Digest Hash(std::span<const std::byte> bytes)
{
	picosha2::hash256_one_by_one hash;
	const auto *first = reinterpret_cast<const uint8_t *>(bytes.data());
	hash.process(first, first + bytes.size());
	hash.finish();
	Digest result;
	hash.get_hash_bytes(result.begin(), result.end());
	return result;
}

bool DecodeHash(std::string_view text, Digest &digest)
{
	if (text.size() != 64)
		return false;
	for (size_t i = 0; i < digest.size(); ++i) {
		uint8_t value = 0;
		for (size_t j = 0; j < 2; ++j) {
			const char c = text[2 * i + j];
			if (c >= '0' && c <= '9')
				value = static_cast<uint8_t>(16 * value + c - '0');
			else if (c >= 'a' && c <= 'f')
				value = static_cast<uint8_t>(16 * value + c - 'a' + 10);
			else
				return false;
		}
		digest[i] = value;
	}
	return true;
}

struct Reader {
	std::span<const std::byte> data;
	size_t offset = 0;
	bool ok = true;

	std::span<const std::byte> Take(size_t size)
	{
		if (!ok || size > data.size() - offset) {
			ok = false;
			return {};
		}
		auto result = data.subspan(offset, size);
		offset += size;
		return result;
	}

	uint32_t U32()
	{
		auto bytes = Take(4);
		if (!ok)
			return 0;
		return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
	}

	float F32() { return std::bit_cast<float>(U32()); }

	template <size_t N>
	std::array<float, N> Floats()
	{
		std::array<float, N> result;
		for (auto &value : result)
			value = F32();
		return result;
	}

	std::string Text()
	{
		const uint32_t size = U32();
		if (size < 1 || size > 256) {
			ok = false;
			return {};
		}
		const auto bytes = Take(size);
		if (!ok)
			return {};
		for (const auto byte : bytes)
			if (byte < std::byte { 32 } || byte > std::byte { 126 }) {
				ok = false;
				return {};
			}
		return { reinterpret_cast<const char *>(bytes.data()), bytes.size() };
	}

	Digest HashBytes()
	{
		Digest result {};
		auto bytes = Take(result.size());
		if (ok)
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = uint8_t(bytes[i]);
		return result;
	}
};

template <size_t N>
bool Finite(const std::array<float, N> &values, float limit = 1'000'000)
{
	return std::all_of(values.begin(), values.end(), [limit](float x) { return std::isfinite(x) && std::abs(x) <= limit; });
}

template <size_t N>
bool UnitRange(const std::array<float, N> &values)
{
	return Finite(values, 1) && std::all_of(values.begin(), values.end(), [](float x) { return x >= 0; });
}

bool Unit(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }

bool Wrap(uint32_t value) { return value == 10497 || value == 33071 || value == 33648; }
bool MinFilter(uint32_t value) { return value == 9728 || value == 9729 || (value >= 9984 && value <= 9987); }

bool JpegDimensions(std::span<const std::byte> bytes, uint32_t width, uint32_t height)
{
	const auto *raw = reinterpret_cast<const uint8_t *>(bytes.data());
	if (bytes.size() < 4 || raw[0] != 255 || raw[1] != 216)
		return false;
	size_t at = 2;
	while (at < bytes.size()) {
		if (raw[at++] != 255)
			return false;
		while (at < bytes.size() && raw[at] == 255)
			++at;
		if (at >= bytes.size())
			return false;
		const uint8_t marker = raw[at++];
		if (marker == 0 || marker == 0xda || marker == 0xd9 || marker == 0xd8)
			return false; // No scan/data before a validated frame header.
		if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
			continue;
		if (bytes.size() - at < 2)
			return false;
		const size_t length = size_t(raw[at]) * 256 + raw[at + 1];
		if (length < 2 || length > bytes.size() - at)
			return false;
		if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
			if (length < 8)
				return false;
			const uint32_t actualHeight = uint32_t(raw[at + 3]) * 256 + raw[at + 4];
			const uint32_t actualWidth = uint32_t(raw[at + 5]) * 256 + raw[at + 6];
			return actualWidth == width && actualHeight == height;
		}
		at += length;
	}
	return false;
}

bool Parse(std::span<const std::byte> bytes, TownModelV2 &model, std::string &error)
{
	auto fail = [&error](const char *message) {
		error = message;
		return false;
	};
	Reader read { bytes };
	read.Take(8); // Magic checked before SHA/allocation.
	model.tangentMode = read.U32();
	const uint32_t vertices = read.U32(), indices = read.U32(), primitives = read.U32(), materials = read.U32(), textures = read.U32();
	model.anchor = read.Floats<3>();
	model.boundsMin = read.Floats<3>();
	model.boundsMax = read.Floats<3>();
	model.openingsMode = read.U32();
	if (!read.ok || model.tangentMode < 1 || model.tangentMode > 2 || vertices < 3 || vertices > 8'000'000 || indices < 3 || indices > 24'000'000 || indices % 3 != 0
	    || primitives < 1 || primitives > 4096 || materials < 1 || materials > 256 || textures > 64 || model.openingsMode > 2)
		return fail("v2 header/count/compatibility limit");
	if (!Finite(model.anchor) || !Finite(model.boundsMin) || !Finite(model.boundsMax))
		return fail("v2 nonfinite/out-of-range anchor/bounds");
	for (size_t axis = 0; axis < 3; ++axis)
		if (model.boundsMin[axis] > model.boundsMax[axis])
			return fail("v2 inverted bounds");
	model.assetId = read.Text();
	model.variant = read.Text();
	model.revision = read.Text();
	model.openingsPipeline = read.Text();
	model.sourceSha256 = read.HashBytes();
	if (!read.ok || std::all_of(model.sourceSha256.begin(), model.sourceSha256.end(), [](uint8_t b) { return b == 0; }))
		return fail("v2 missing/invalid source identity");
	const uint64_t minimumBytes = uint64_t(vertices) * 64 + uint64_t(indices) * 4 + uint64_t(primitives) * 12 + uint64_t(materials) * 196 + uint64_t(textures) * 64;
	if (minimumBytes > bytes.size() - read.offset)
		return fail("v2 truncated resource tables");
	model.vertices.resize(vertices);
	std::array<float, 3> actualMin { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
	std::array<float, 3> actualMax { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
	for (auto &vertex : model.vertices) {
		vertex.position = read.Floats<3>();
		vertex.normal = read.Floats<3>();
		vertex.uv = read.Floats<2>();
		vertex.tangent = read.Floats<4>();
		vertex.color = read.Floats<4>();
		if (!read.ok || !Finite(vertex.position) || !Finite(vertex.normal, 1.001F) || !Finite(vertex.uv) || !Finite(vertex.tangent, 1.001F) || !UnitRange(vertex.color))
			return fail("v2 invalid vertex attribute");
		double normalLength = 0, tangentLength = 0, normalTangent = 0;
		for (size_t axis = 0; axis < 3; ++axis) {
			normalLength += double(vertex.normal[axis]) * vertex.normal[axis];
			tangentLength += double(vertex.tangent[axis]) * vertex.tangent[axis];
			normalTangent += double(vertex.normal[axis]) * vertex.tangent[axis];
			actualMin[axis] = std::min(actualMin[axis], vertex.position[axis]);
			actualMax[axis] = std::max(actualMax[axis], vertex.position[axis]);
		}
		if (std::abs(normalLength - 1) > .003 || std::abs(tangentLength - 1) > .003 || std::abs(normalTangent) > .003 || std::abs(std::abs(vertex.tangent[3]) - 1) > .001)
			return fail("v2 invalid normal/tangent frame");
	}
	if (actualMin != model.boundsMin || actualMax != model.boundsMax)
		return fail("v2 bounds differ from indexed vertices");
	model.indices.resize(indices);
	for (auto &index : model.indices) {
		index = read.U32();
		if (index >= vertices)
			return fail("v2 index out of range");
	}
	for (size_t i = 0; i < indices; i += 3) {
		const auto &a = model.vertices[model.indices[i]].position;
		const auto &b = model.vertices[model.indices[i + 1]].position;
		const auto &c = model.vertices[model.indices[i + 2]].position;
		std::array<double, 3> ab, ac;
		for (size_t axis = 0; axis < 3; ++axis) {
			ab[axis] = double(b[axis]) - a[axis];
			ac[axis] = double(c[axis]) - a[axis];
		}
		const double x = ab[1] * ac[2] - ab[2] * ac[1], y = ab[2] * ac[0] - ab[0] * ac[2], z = ab[0] * ac[1] - ab[1] * ac[0];
		if (x * x + y * y + z * z == 0)
			return fail("v2 degenerate triangle");
	}
	model.primitives.resize(primitives);
	uint64_t nextIndex = 0;
	for (auto &primitive : model.primitives) {
		primitive = { read.U32(), read.U32(), read.U32() };
		if (primitive.firstIndex != nextIndex || primitive.indexCount < 3 || primitive.indexCount % 3 != 0 || primitive.material >= materials)
			return fail("v2 invalid primitive/material partition");
		nextIndex += primitive.indexCount;
		if (nextIndex > indices)
			return fail("v2 primitive exceeds index buffer");
	}
	if (nextIndex != indices)
		return fail("v2 incomplete primitive partition");
	model.materials.resize(materials);
	for (auto &material : model.materials) {
		material.baseColor = read.Floats<4>();
		material.emissive = read.Floats<3>();
		material.metallic = read.F32();
		material.roughness = read.F32();
		material.alphaCutoff = read.F32();
		material.normalScale = read.F32();
		material.occlusionStrength = read.F32();
		material.alphaMode = read.U32();
		const uint32_t sided = read.U32();
		material.doubleSided = sided != 0;
		if (!UnitRange(material.baseColor) || !UnitRange(material.emissive) || !Unit(material.metallic) || !Unit(material.roughness) || !Unit(material.alphaCutoff)
		    || !std::isfinite(material.normalScale) || material.normalScale < 0 || material.normalScale > 1'000'000 || !Unit(material.occlusionStrength) || material.alphaMode > 2 || sided > 1)
			return fail("v2 invalid material factors");
		for (auto &info : material.textures) {
			info.texture = std::bit_cast<int32_t>(read.U32());
			info.texCoord = read.U32();
			info.offset = read.Floats<2>();
			info.scale = read.Floats<2>();
			info.rotation = read.F32();
			if (info.texture < -1 || (info.texture >= 0 && uint32_t(info.texture) >= textures) || info.texCoord != 0 || !Finite(info.offset) || !Finite(info.scale) || !std::isfinite(info.rotation) || std::abs(info.rotation) > 1'000'000)
				return fail("v2 invalid material texture reference/transform");
		}
	}
	model.textures.resize(textures);
	for (auto &texture : model.textures) {
		texture.mime = read.U32();
		texture.width = read.U32();
		texture.height = read.U32();
		texture.wrapS = read.U32();
		texture.wrapT = read.U32();
		texture.minFilter = read.U32();
		texture.magFilter = read.U32();
		const uint32_t size = read.U32();
		texture.sha256 = read.HashBytes();
		if (!read.ok || texture.mime > 1 || texture.width < 1 || texture.height < 1 || texture.width > 8192 || texture.height > 8192
		    || !Wrap(texture.wrapS) || !Wrap(texture.wrapT) || !MinFilter(texture.minFilter) || (texture.magFilter != 9728 && texture.magFilter != 9729) || size < 16 || size > 64 * 1024 * 1024)
			return fail("v2 invalid texture metadata");
		const auto payload = read.Take(size);
		if (!read.ok || Hash(payload) != texture.sha256)
			return fail("v2 encoded texture hash/truncation");
		const auto *raw = reinterpret_cast<const uint8_t *>(payload.data());
		if (texture.mime == 0) {
			constexpr std::array<uint8_t, 8> Signature { 137, 80, 78, 71, 13, 10, 26, 10 };
			if (size < 24 || !std::equal(Signature.begin(), Signature.end(), raw) || raw[12] != 'I' || raw[13] != 'H' || raw[14] != 'D' || raw[15] != 'R')
				return fail("v2 PNG signature/header");
			auto bigU32 = [raw](size_t at) { return uint32_t(raw[at]) << 24 | uint32_t(raw[at + 1]) << 16 | uint32_t(raw[at + 2]) << 8 | uint32_t(raw[at + 3]); };
			if (bigU32(16) != texture.width || bigU32(20) != texture.height)
				return fail("v2 PNG dimensions disagree");
		} else if (!JpegDimensions(payload, texture.width, texture.height)) {
			return fail("v2 JPEG frame dimensions/signature");
		}
		texture.encoded.assign(raw, raw + payload.size());
	}
	if (!read.ok || read.offset != bytes.size())
		return fail("v2 trailing/truncated bytes");
	return true;
}

} // namespace

bool ParseTownModelV2(std::span<const std::byte> bytes, std::string_view expectedSha256, TownModelV2 &output, std::string &error)
{
	error.clear();
	constexpr std::string_view Magic = "D3DMESH2";
	if (bytes.size() < 120 || bytes.size() > MaxBytes || !std::equal(Magic.begin(), Magic.end(), reinterpret_cast<const char *>(bytes.data()))) {
		error = "v2 file size/magic (v1 must use its own loader)";
		return false;
	}
	Digest expected;
	if (!DecodeHash(expectedSha256, expected) || Hash(bytes) != expected) {
		error = "v2 exact packet SHA-256 required/mismatch";
		return false;
	}
	try {
		TownModelV2 candidate;
		if (!Parse(bytes, candidate, error))
			return false;
		candidate.sha256 = expectedSha256;
		output = std::move(candidate);
		return true;
	} catch (const std::bad_alloc &) {
		error = "v2 resource allocation failed";
		return false;
	} catch (const std::length_error &) {
		error = "v2 allocation length invalid";
		return false;
	}
}

} // namespace devilution

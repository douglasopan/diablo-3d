#include "engine/render/town_model_v2.hpp"

#include <bit>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <picosha2.h>

namespace {
using Bytes = std::vector<std::byte>;
int Checks = 0;
int Failures = 0;

void Check(bool ok, const char *name)
{
	++Checks;
	if (!ok) {
		++Failures;
		std::fprintf(stderr, "FAIL: %s\n", name);
	}
}

void U32(Bytes &bytes, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		bytes.push_back(std::byte((value >> (i * 8)) & 255));
}

void SetU32(Bytes &bytes, size_t offset, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		bytes[offset + i] = std::byte((value >> (i * 8)) & 255);
}

void Float(Bytes &bytes, float value) { U32(bytes, std::bit_cast<uint32_t>(value)); }

void Text(Bytes &bytes, const std::string &text)
{
	U32(bytes, static_cast<uint32_t>(text.size()));
	for (char c : text)
		bytes.push_back(std::byte(c));
}

std::string Hash(const Bytes &bytes)
{
	const auto *first = reinterpret_cast<const uint8_t *>(bytes.data());
	return picosha2::hash256_hex_string(first, first + bytes.size());
}

struct Fixture {
	Bytes bytes;
	size_t vertexOffset = 0;
	size_t indexOffset = 0;
	size_t primitiveOffset = 0;
	size_t materialOffset = 0;
};

Fixture MakeFixture()
{
	Fixture fixture;
	auto &bytes = fixture.bytes;
	for (char c : std::string("D3DMESH2"))
		bytes.push_back(std::byte(c));
	for (uint32_t value : { 2U, 3U, 3U, 1U, 1U, 0U })
		U32(bytes, value);
	for (float value : { 0.F, 0.F, 0.F, 0.F, 0.F, 0.F, 1.F, 1.F, 0.F })
		Float(bytes, value);
	U32(bytes, 0);
	for (const char *value : { "synthetic.triangle", "open", "r1", "native" })
		Text(bytes, value);
	for (int i = 0; i < 32; ++i)
		bytes.push_back(std::byte { 1 });
	fixture.vertexOffset = bytes.size();
	for (const auto position : { std::array<float, 3> { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }) {
		for (float value : position)
			Float(bytes, value);
		for (float value : { 0.F, 0.F, 1.F, -1.F, 2.F, 1.F, 0.F, 0.F, 1.F, 1.F, 1.F, 1.F, 1.F })
			Float(bytes, value);
	}
	fixture.indexOffset = bytes.size();
	for (uint32_t i : { 0U, 1U, 2U })
		U32(bytes, i);
	fixture.primitiveOffset = bytes.size();
	for (uint32_t i : { 0U, 3U, 0U })
		U32(bytes, i);
	fixture.materialOffset = bytes.size();
	for (float value : { 1.F, 1.F, 1.F, .5F, 0.F, 0.F, 0.F, .3F, .7F, .5F, 1.F, 1.F })
		Float(bytes, value);
	U32(bytes, 1);
	U32(bytes, 1);
	for (int slot = 0; slot < 5; ++slot) {
		U32(bytes, 0xffffffff);
		U32(bytes, 0);
		for (float value : { 0.F, 0.F, 1.F, 1.F, 0.F })
			Float(bytes, value);
	}
	return fixture;
}

void Synthetic()
{
	using namespace devilution;
	auto fixture = MakeFixture();
	TownModelV2 output;
	std::string error;
	Check(ParseTownModelV2(fixture.bytes, Hash(fixture.bytes), output, error), "valid indexed v2");
	Check(output.indices.size() == 3 && output.vertices.size() == 3 && output.materials.size() == 1, "indexed tables");
	Check(output.vertices[0].uv[0] == -1 && output.vertices[0].uv[1] == 2, "repeat UVs preserved");
	Check(output.materials[0].alphaMode == 1 && output.materials[0].doubleSided && output.materials[0].baseColor[3] == .5F, "material alpha and culling preserved");
	Check(output.vertices[0].normal[2] == 1 && output.vertices[0].tangent[0] == 1 && output.tangentMode == 2, "smooth normals and tangent provenance");
	const std::string baseline = output.sha256;
	auto rejected = [&](Bytes bytes, const char *name) {
		Check(!ParseTownModelV2(bytes, Hash(bytes), output, error), name);
		Check(output.sha256 == baseline && output.vertices.size() == 3, "rejection leaves output unchanged");
	};
	Check(!ParseTownModelV2(fixture.bytes, std::string(64, '0'), output, error), "exact hash mismatch");
	Check(!ParseTownModelV2(fixture.bytes, "", output, error), "hash is mandatory");
	Check(!ParseTownModelV2({}, "", output, error), "empty input");
	auto mutation = fixture.bytes;
	mutation[7] = std::byte { '1' };
	rejected(mutation, "v1 never reinterpreted");
	mutation = fixture.bytes;
	SetU32(mutation, 12, 0xffffffff);
	rejected(mutation, "oversized count before allocation");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.vertexOffset, 0x7fc00000);
	rejected(mutation, "nonfinite position");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.vertexOffset + 20, 0);
	rejected(mutation, "zero smooth normal");
	mutation = fixture.bytes;
	SetU32(mutation, 56, std::bit_cast<uint32_t>(2.F));
	rejected(mutation, "bounds mismatch");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.indexOffset, 3);
	rejected(mutation, "index out of range");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.indexOffset + 8, 1);
	rejected(mutation, "degenerate triangle");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.primitiveOffset, 3);
	rejected(mutation, "primitive gap");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.primitiveOffset + 8, 1);
	rejected(mutation, "unknown material");
	mutation = fixture.bytes;
	SetU32(mutation, fixture.materialOffset + 56, 0);
	rejected(mutation, "missing texture index");
	mutation = fixture.bytes;
	SetU32(mutation, 68, 3);
	rejected(mutation, "unknown openings compatibility");
	mutation = fixture.bytes;
	mutation.push_back(std::byte { 0 });
	rejected(mutation, "exact byte length");
	for (size_t cut : { size_t { 8 }, fixture.vertexOffset - 1, fixture.indexOffset, fixture.materialOffset, fixture.bytes.size() - 1 }) {
		mutation.assign(fixture.bytes.begin(), fixture.bytes.begin() + cut);
		rejected(mutation, "truncated table");
	}
}
} // namespace

int main(int argc, char **argv)
{
	if (argc == 1) {
		Synthetic();
		std::printf("town_model_v2: %d checks, %d failures\n", Checks, Failures);
		return Failures == 0 ? 0 : 1;
	}
	if (argc != 3) {
		std::fprintf(stderr, "Usage: town_model_v2_smoke [packet.d3d2 expected-sha256]\n");
		return 2;
	}
	std::ifstream input(argv[1], std::ios::binary);
	if (!input) {
		std::fprintf(stderr, "Cannot open private packet\n");
		return 2;
	}
	std::vector<char> raw((std::istreambuf_iterator<char>(input)), {});
	devilution::TownModelV2 model;
	std::string error;
	if (!devilution::ParseTownModelV2(std::as_bytes(std::span(raw)), argv[2], model, error)) {
		std::fprintf(stderr, "Rejected: %s\n", error.c_str());
		return 1;
	}
	std::printf("v2 parsed: asset=%s revision=%s vertices=%zu triangles=%zu primitives=%zu materials=%zu textures=%zu tangentMode=%u\n",
	    model.assetId.c_str(), model.revision.c_str(), model.vertices.size(), model.indices.size() / 3, model.primitives.size(), model.materials.size(), model.textures.size(), model.tangentMode);
	return 0;
}

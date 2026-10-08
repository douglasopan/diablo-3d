// Offscreen-only D3D11 cache checks. No game assets, profile or game window.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "engine/render/town_gpu.hpp"

namespace {
using namespace devilution;
constexpr int FrameSize = 128;
constexpr size_t MiB = 1024 * 1024;
constexpr size_t CacheBudget = 256 * MiB;
size_t Checks = 0;
size_t Failures = 0;

void Check(bool passed, const std::string &message)
{
	++Checks;
	if (!passed) {
		++Failures;
		if (Failures <= 40)
			std::cerr << "FAIL " << message << '\n';
	}
}

void CheckBytes(size_t actual, size_t expected, const std::string &message)
{
	Check(actual == expected, message + ": actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected));
}

size_t LutBytes(size_t payload)
{
	if (payload == 0)
		return 0;
	const size_t width = payload < 16384 ? payload : 16384;
	return width * ((payload + width - 1) / width);
}

bool Begin(const std::string &label)
{
	const bool started = TownGpuBeginFrame(FrameSize, FrameSize, true);
	Check(started, label + " begin: " + GetTownGpuStatus().failure);
	static bool reportedAdapter = false;
	if (started && !reportedAdapter) {
		const auto &status = GetTownGpuStatus();
		std::cout << "D3D11 adapter: " << status.adapter << "; WARP=" << (status.warp ? "true" : "false")
		          << "; featureLevel=" << status.featureLevel << '\n';
		reportedAdapter = true;
	}
	return started;
}

bool End(TownGpuFrame &frame, const std::string &label)
{
	const bool ended = TownGpuEndFrame(frame);
	Check(ended, label + " end: " + GetTownGpuStatus().failure);
	const bool complete = ended && frame.width == FrameSize && frame.height == FrameSize
	    && frame.indexed.size() == FrameSize * FrameSize && frame.pickIds.size() == FrameSize * FrameSize
	    && frame.depth.size() == FrameSize * FrameSize;
	Check(complete, label + " publishes complete color/ID/depth");
	return complete;
}

std::array<TownGpuVertex, 3> Triangle(int x, int y, int width, int height, float depth, float u = 0.5F, float v = 0.5F)
{
	return { {
		{ static_cast<float>(x), static_cast<float>(y), depth, u, v, { 0, 0, 0 } },
		{ static_cast<float>(x + width), static_cast<float>(y), depth, u, v, { 1, 0, 0 } },
		{ static_cast<float>(x), static_cast<float>(y + height), depth, u, v, { 0, 0, 1 } },
	} };
}

void Sample(const TownGpuFrame &frame, int x, int y, uint8_t color, uint32_t pick, float depth, const std::string &label)
{
	const size_t pixel = static_cast<size_t>(y) * FrameSize + x;
	if (pixel >= frame.indexed.size() || pixel >= frame.pickIds.size() || pixel >= frame.depth.size()) {
		Check(false, label + " sample unavailable");
		return;
	}
	Check(frame.indexed[pixel] == color, label + " color");
	Check(frame.pickIds[pixel] == pick, label + " ID");
	Check(std::abs(frame.depth[pixel] - depth) < 0.0001F, label + " depth");
}

TownGpuMaterial Directional(float diffuse = 0)
{
	TownGpuMaterial material;
	material.lighting = TownGpuLighting::Directional;
	material.diffuse = diffuse;
	return material;
}

TownGpuTexture Texture(uint64_t key, uint64_t revision, std::span<const uint32_t> codes,
    std::span<const uint8_t> lut, unsigned levels, uint64_t lutKey = 0, uint64_t lutRevision = 0)
{
	TownGpuTexture texture;
	texture.stableKey = key;
	texture.revision = revision;
	texture.width = static_cast<int>(codes.size());
	texture.height = 1;
	texture.texelCodes = codes;
	texture.lightLut = lut;
	texture.lightLevels = levels;
	texture.lightLutKey = lutKey;
	texture.lightLutRevision = lutRevision;
	return texture;
}

void Metrics(const std::string &label)
{
	const auto &status = GetTownGpuStatus();
	std::cout << label << ": texels=" << status.cachedTextures << ", LUTs=" << status.cachedLightLuts
	          << ", cacheBytes=" << status.cachedTextureBytes << ", uploadedTexelBytes=" << status.uploadedTexelBytes
	          << ", uploadedLutBytes=" << status.uploadedLutBytes << ", evictions=" << status.textureEvictions << '\n';
}

void SharedLargeLut()
{
	ResetTownGpuResources();
	// Nine independent imported textures formerly replicated this 32 MiB LUT
	// to 288 MiB, exceeding the unchanged 256 MiB texture cache budget.
	std::vector<uint8_t> lut(32 * MiB, 0);
	lut[0] = 67;
	const std::array<uint32_t, 1> codes { 0 };
	constexpr size_t TextureCount = 9;
	Check(TextureCount * lut.size() > CacheBudget, "large shared fixture reproduces legacy allocation above 256 MiB");
	if (!Begin("large shared LUT"))
		return;
	for (size_t i = 0; i < TextureCount; ++i) {
		const int x = 8 + static_cast<int>(i % 3) * 40, y = 8 + static_cast<int>(i / 3) * 40;
		const auto texture = Texture(0x51000000ULL + i, 1, codes, lut, 4096, 0x5100FFFFULL, 1);
		Check(TownGpuSubmitProjectedTriangle(Triangle(x, y, 32, 32, 2), texture, Directional(), static_cast<uint32_t>(101 + i)),
		    "share large LUT texture " + std::to_string(i) + ": " + GetTownGpuStatus().failure);
	}
	TownGpuFrame frame;
	if (End(frame, "large shared LUT")) {
		for (size_t i = 0; i < TextureCount; ++i)
			Sample(frame, 12 + static_cast<int>(i % 3) * 40, 12 + static_cast<int>(i / 3) * 40, 67,
			    static_cast<uint32_t>(101 + i), 2, "large shared instance " + std::to_string(i));
	}
	const auto &status = GetTownGpuStatus();
	CheckBytes(status.cachedTextures, TextureCount, "independent code resources retained");
	CheckBytes(status.cachedLightLuts, 1, "one global LUT resource");
	CheckBytes(status.uploadedTexelBytes, TextureCount * sizeof(uint32_t), "each code resource uploaded once");
	CheckBytes(status.uploadedLutBytes, LutBytes(lut.size()), "global LUT uploaded once");
	CheckBytes(status.cachedTextureBytes, TextureCount * sizeof(uint32_t) + LutBytes(lut.size()), "shared cache has exact bounded size");
	CheckBytes(status.textureEvictions, 0, "sharing requires no eviction");
	Metrics("legacy 288 MiB replication / shared 32 MiB");
	if (!Begin("large shared warm hit"))
		return;
	for (size_t i = 0; i < TextureCount; ++i) {
		auto texture = Texture(0x51000000ULL + i, 1, codes, lut, 4096, 0x5100FFFFULL, 1);
		texture.texelCodes = {};
		texture.lightLut = {};
		Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 96, 96, static_cast<float>(i + 1)), texture, Directional(),
		          static_cast<uint32_t>(101 + i)), "shared warm hit accepts omitted borrowed data");
	}
	End(frame, "large shared warm hit");
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, 0, "warm frame does not upload codes");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, 0, "warm frame does not upload global LUT");
	Metrics("large shared warm hit");
}

void ConcurrentRevisions()
{
	ResetTownGpuResources();
	const std::array<uint32_t, 1> codes { 1 };
	std::array<uint8_t, 32> oldLut {};
	oldLut[1 * 16 + 8] = 31;
	std::vector<uint8_t> newLut(66 * 256, 0);
	newLut[1 * 256 + 128] = 93;
	const auto oldTexture = Texture(0x52000001ULL, 4, codes, oldLut, 16, 0x5200FFFFULL, 1);
	const auto newTexture = Texture(0x52000001ULL, 4, codes, newLut, 256, 0x5200FFFFULL, 2);
	if (!Begin("old LUT snapshot"))
		return;
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 5), oldTexture, Directional(0.5F), 201), "upload old LUT snapshot");
	TownGpuFrame frame;
	if (End(frame, "old LUT snapshot"))
		Sample(frame, 12, 12, 31, 201, 5, "old snapshot initial");
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, sizeof(uint32_t), "initial code upload");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, oldLut.size(), "initial small LUT upload");
	if (!Begin("concurrent LUT revisions"))
		return;
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 48, 80, 3), oldTexture, Directional(0.5F), 211), "record old revision before growth");
	Check(TownGpuSubmitProjectedTriangle(Triangle(72, 8, 48, 80, 7), newTexture, Directional(0.5F), 212), "record new revision with different dimensions");
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 88, 48, 32, 9), oldTexture, Directional(0.5F), 213), "record old revision again after growth");
	if (End(frame, "concurrent LUT revisions")) {
		Sample(frame, 12, 12, 31, 211, 3, "old snapshot before growth");
		Sample(frame, 76, 12, 93, 212, 7, "new snapshot after growth");
		Sample(frame, 12, 92, 31, 213, 9, "old snapshot after growth");
	}
	const auto &status = GetTownGpuStatus();
	CheckBytes(status.cachedTextures, 1, "LUT revision growth retains one code resource");
	CheckBytes(status.cachedLightLuts, 2, "old and new LUT snapshots coexist");
	CheckBytes(status.uploadedTexelBytes, 0, "LUT growth does not reupload immutable codes");
	CheckBytes(status.uploadedLutBytes, LutBytes(newLut.size()), "new LUT upload accounts for padded final row");
	CheckBytes(status.cachedTextureBytes, sizeof(uint32_t) + LutBytes(oldLut.size()) + LutBytes(newLut.size()), "both LUT layouts accounted exactly");
	Metrics("same-frame old/new LUT dimensions");
}

void PrivateInteriorLuts()
{
	ResetTownGpuResources();
	const std::array<uint32_t, 1> codes { 1 };
	std::array<uint8_t, 8> first {}, second {}, shared {};
	first[4] = 41;
	second[4] = 82;
	shared[4] = 123;
	const auto firstTexture = Texture(0x53000001ULL, 1, codes, first, 4);
	const auto secondTexture = Texture(0x53000002ULL, 1, codes, second, 4);
	// A numeric collision between shared and private keys must remain isolated.
	const auto sharedTexture = Texture(0x53000003ULL, 1, codes, shared, 4, firstTexture.stableKey, firstTexture.revision);
	TownGpuMaterial interior;
	interior.lighting = TownGpuLighting::Interior;
	if (!Begin("private interior LUTs"))
		return;
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 32, 80, 2), firstTexture, interior, 301), "first private interior LUT");
	Check(TownGpuSubmitProjectedTriangle(Triangle(48, 8, 32, 80, 3), secondTexture, interior, 302), "second private interior LUT");
	Check(TownGpuSubmitProjectedTriangle(Triangle(88, 8, 32, 80, 4), sharedTexture, interior, 303), "shared/private key namespace isolation");
	// All borrowed payloads may change after Submit without changing the frame.
	first.fill(7);
	second.fill(7);
	shared.fill(7);
	TownGpuFrame frame;
	if (End(frame, "private interior LUTs")) {
		Sample(frame, 12, 12, 41, 301, 2, "first interior retains its LUT");
		Sample(frame, 52, 12, 82, 302, 3, "second interior retains its LUT");
		Sample(frame, 92, 12, 123, 303, 4, "shared namespace retains its LUT");
	}
	CheckBytes(GetTownGpuStatus().cachedLightLuts, 3, "private interior LUTs do not merge");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, 24, "private LUT payloads uploaded independently");
	CheckBytes(GetTownGpuStatus().cachedTextureBytes, 3 * sizeof(uint32_t) + 24, "private/shared memory accounting");
	Metrics("private interiors and namespace collision");
}

void CodesOpacityAndZero()
{
	ResetTownGpuResources();
	const std::array<uint32_t, 1> backgroundCodes { 17 };
	std::array<uint32_t, 2> codes { 0, 1 };
	std::array<uint8_t, 2> opacity { 1, 0 };
	const std::array<uint8_t, 4> lut { 0, 0, 73, 73 };
	const auto background = Texture(0x54000000ULL, 1, backgroundCodes, {}, 1);
	auto front = Texture(0x54000001ULL, 1, codes, lut, 2, 0x5400FFFFULL, 1);
	front.opacity = opacity;
	auto material = Directional(1);
	material.transparentZero = true;
	TownGpuFrame frame;
	if (!Begin("opacity and palette zero"))
		return;
	Check(TownGpuSubmitProjectedTriangle(Triangle(0, 0, 128, 128, 10), background, {}, 401), "opaque background");
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 48, 80, 2, 0.25F), front, material, 402), "masked opaque palette zero");
	Check(TownGpuSubmitProjectedTriangle(Triangle(72, 8, 48, 80, 3, 0.75F), front, material, 403), "masked transparent nonzero code");
	if (End(frame, "opacity and palette zero")) {
		Sample(frame, 12, 12, 0, 402, 2, "opacity overrides transparentZero for painted zero");
		Sample(frame, 76, 12, 17, 401, 10, "transparent mask preserves background color/ID/depth");
	}
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, 3 * sizeof(uint32_t) + 2, "code and opacity bytes included in upload accounting");
	codes = { 1, 0 };
	opacity = { 1, 1 };
	front.revision = 2;
	if (!Begin("revised codes and opacity"))
		return;
	Check(TownGpuSubmitProjectedTriangle(Triangle(0, 0, 128, 128, 10), background, {}, 411), "reuse background");
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 48, 80, 2, 0.25F), front, material, 412), "code revision changes visible color");
	Check(TownGpuSubmitProjectedTriangle(Triangle(72, 8, 48, 80, 3, 0.75F), front, material, 413), "mask revision makes zero opaque");
	if (End(frame, "revised codes and opacity")) {
		Sample(frame, 12, 12, 73, 412, 2, "new code revision");
		Sample(frame, 76, 12, 0, 413, 3, "new opacity revision");
	}
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, 2 * sizeof(uint32_t) + 2, "only revised code/mask resource uploaded");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, 0, "code revision reuses independent shared LUT");
	Metrics("code and opacity revisions / opaque zero");
}

void FailedFrame(const std::string &label)
{
	TownGpuFrame frame;
	frame.width = frame.height = 7;
	frame.indexed = { 255 };
	frame.pickIds = { 999 };
	frame.depth = { 42 };
	Check(!TownGpuEndFrame(frame), label + " refuses incomplete publication");
	Check(frame.width == 0 && frame.height == 0 && frame.indexed.empty() && frame.pickIds.empty() && frame.depth.empty(),
	    label + " clears all stale outputs together");
	Check(!GetTownGpuStatus().frameSucceeded && GetTownGpuStatus().drawCalls == 0, label + " records no successful partial frame");
}

void LayoutConflicts()
{
	const std::array<uint32_t, 4> codes { 1, 0, 1, 0 };
	const std::array<uint8_t, 4> opacity { 1, 1, 0, 1 };
	const std::array<uint8_t, 8> lut {};
	const std::array<uint8_t, 12> largerLut {};
	for (int variant = 0; variant < 4; ++variant) {
		ResetTownGpuResources();
		const std::string label = "layout conflict " + std::to_string(variant);
		if (!Begin(label))
			continue;
		auto texture = Texture(0x55000001ULL, 1, codes, lut, 4, 0x5500FFFFULL, 1);
		texture.width = texture.height = 2;
		if (variant != 1)
			texture.opacity = opacity;
		Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 4), texture, Directional(), 501), label + " records valid baseline");
		const size_t beforeBytes = GetTownGpuStatus().cachedTextureBytes;
		if (variant == 0) { texture.width = 4; texture.height = 1; }
		if (variant == 1) texture.opacity = opacity;
		if (variant == 2) texture.lightLevels = 8;
		if (variant == 3) texture.lightLut = largerLut;
		Check(!TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 2), texture, Directional(), 502), label + " rejected");
		Check(!GetTownGpuStatus().failure.empty(), label + " provides explicit failure");
		CheckBytes(GetTownGpuStatus().cachedTextureBytes, beforeBytes, label + " does not alter cache resources");
		FailedFrame(label);
	}
	ResetTownGpuResources();
	if (Begin("code outside LUT")) {
		const std::array<uint32_t, 1> invalidCodes { 2 };
		const auto invalid = Texture(0x55000002ULL, 1, invalidCodes, lut, 4, 0x5500FFFFULL, 1);
		Check(!TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 2), invalid, Directional(), 503), "reject code beyond LUT codeCount");
		CheckBytes(GetTownGpuStatus().cachedTextureBytes, 0, "invalid code does not partially insert LUT/texels");
		FailedFrame("code outside LUT");
	}
}

void EvictionAndAtomicBudget()
{
	// 4096 codes x 16384 levels = 64 MiB; valid D3D11 size 16384 x 4096.
	// Reuse the borrowed CPU buffer so the diagnostic itself needs only 64 MiB.
	std::vector<uint8_t> lut(64 * MiB, 0);
	const std::array<uint32_t, 1> codes { 0 };
	constexpr unsigned Levels = 16384;
	const auto resource = [&](size_t i) { return Texture(0x56000000ULL + i, 1, codes, lut, Levels, 0x56010000ULL + i, 1); };
	ResetTownGpuResources();
	if (!Begin("fill cache below budget"))
		return;
	for (size_t i = 0; i < 3; ++i) {
		lut[0] = static_cast<uint8_t>(51 + i);
		Check(TownGpuSubmitProjectedTriangle(Triangle(8 + static_cast<int>(i) * 40, 8, 32, 80, 2), resource(i), Directional(),
		          static_cast<uint32_t>(601 + i)), "cache initial large resource " + std::to_string(i));
	}
	TownGpuFrame frame;
	if (End(frame, "fill cache below budget"))
		for (size_t i = 0; i < 3; ++i)
			Sample(frame, 12 + static_cast<int>(i) * 40, 12, static_cast<uint8_t>(51 + i), static_cast<uint32_t>(601 + i), 2,
			    "borrowed large LUT snapshot " + std::to_string(i));
	CheckBytes(GetTownGpuStatus().cachedTextureBytes, 3 * (64 * MiB + sizeof(uint32_t)), "three large resources below fixed budget");
	if (!Begin("evict only unreferenced resources"))
		return;
	auto pinned = resource(0);
	pinned.texelCodes = {};
	pinned.lightLut = {};
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 48, 80, 2), pinned, Directional(), 611), "pin existing resource before memory pressure");
	lut[0] = 94;
	Check(TownGpuSubmitProjectedTriangle(Triangle(72, 8, 48, 80, 3), resource(3), Directional(), 612),
	    "new large resource succeeds by evicting an unused resource: " + GetTownGpuStatus().failure);
	if (End(frame, "evict only unreferenced resources")) {
		Sample(frame, 12, 12, 51, 611, 2, "pinned batch keeps its original LUT under eviction");
		Sample(frame, 76, 12, 94, 612, 3, "new resource after eviction");
	}
	Check(GetTownGpuStatus().textureEvictions > 0, "memory pressure evicted unused resources");
	Check(GetTownGpuStatus().cachedTextureBytes <= CacheBudget, "pressure eviction respects explicit 256 MiB budget");
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, sizeof(uint32_t), "pinning reuses existing codes under pressure");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, 64 * MiB, "only new LUT uploaded under pressure");
	Metrics("eviction with a pinned batch");
	if (!Begin("LUT growth pins reused texels before eviction"))
		return;
	// This code resource was used last frame, but has no earlier batch in this
	// frame. The lookup itself must pin it before making room for its new LUT.
	auto growth = resource(0);
	growth.texelCodes = {};
	growth.lightLutRevision = 2;
	lut[0] = 117;
	Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 2), growth, Directional(), 613),
	    "reused texels survive eviction for a new LUT revision: " + GetTownGpuStatus().failure);
	if (End(frame, "LUT growth pins reused texels before eviction"))
		Sample(frame, 12, 12, 117, 613, 2, "new LUT revision with reused texels under pressure");
	CheckBytes(GetTownGpuStatus().uploadedTexelBytes, 0, "new LUT under pressure does not reupload reused texels");
	CheckBytes(GetTownGpuStatus().uploadedLutBytes, 64 * MiB, "new LUT revision uploaded under pressure");
	Check(GetTownGpuStatus().textureEvictions > 0, "new LUT revision exercises pressure eviction");
	Check(GetTownGpuStatus().cachedTextureBytes <= CacheBudget, "LUT growth under pressure respects budget");
	Metrics("LUT growth reuses texels under pressure");
	ResetTownGpuResources();
	if (!Begin("all current-frame resources pinned"))
		return;
	for (size_t i = 0; i < 3; ++i) {
		lut[0] = static_cast<uint8_t>(71 + i);
		Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, static_cast<float>(i + 2)), resource(i), Directional(),
		          static_cast<uint32_t>(621 + i)), "pin large resource " + std::to_string(i));
	}
	const auto before = GetTownGpuStatus();
	lut[0] = 99;
	Check(!TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 1), resource(3), Directional(), 624), "reject fourth pinned LUT above 256 MiB");
	const auto &failed = GetTownGpuStatus();
	Check(failed.failure.find("budget") != std::string::npos, "budget failure names explicit cause");
	CheckBytes(failed.cachedTextureBytes, before.cachedTextureBytes, "budget rejection inserts no partial resource");
	CheckBytes(failed.cachedTextures, before.cachedTextures, "budget rejection inserts no partial texel identity");
	CheckBytes(failed.cachedLightLuts, before.cachedLightLuts, "budget rejection inserts no partial LUT identity");
	CheckBytes(failed.uploadedTexelBytes, before.uploadedTexelBytes, "budget rejection performs no partial code upload");
	CheckBytes(failed.uploadedLutBytes, before.uploadedLutBytes, "budget rejection performs no partial LUT upload");
	CheckBytes(failed.textureEvictions, 0, "current-frame resources never evicted");
	FailedFrame("pinned budget pressure");
	Metrics("atomic pinned-budget rejection");
	ResetTownGpuResources();
	CheckBytes(GetTownGpuStatus().cachedTextureBytes, 0, "reset clears texture memory accounting");
	CheckBytes(GetTownGpuStatus().cachedTextures, 0, "reset clears texel identities");
	CheckBytes(GetTownGpuStatus().cachedLightLuts, 0, "reset clears LUT identities");
	if (Begin("recovery after cache reset")) {
		const std::array<uint32_t, 1> recoveryCodes { 87 };
		const auto recovery = Texture(0x5600FFFFULL, 1, recoveryCodes, {}, 1);
		Check(TownGpuSubmitProjectedTriangle(Triangle(8, 8, 112, 112, 2), recovery, {}, 631), "fresh resource after reset");
		if (End(frame, "recovery after cache reset"))
			Sample(frame, 12, 12, 87, 631, 2, "reset recovery");
	}
}
} // namespace

int main()
{
	SharedLargeLut();
	ConcurrentRevisions();
	PrivateInteriorLuts();
	CodesOpacityAndZero();
	LayoutConflicts();
	EvictionAndAtomicBudget();
	ResetTownGpuResources();
	std::cout << (Failures == 0 ? "PASS " : "FAIL ") << Checks << " checks; " << Failures << " failures\n";
	return Failures == 0 ? 0 : 1;
}

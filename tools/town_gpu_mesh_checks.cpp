// Isolated offscreen D3D11 mesh checks. No game assets, profile or game window.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "engine/render/town_camera.hpp"
#include "engine/render/town_gpu_mesh.hpp"

namespace {
using namespace devilution;
constexpr int Width = 128;
constexpr int Height = 96;
size_t Checks = 0;
size_t Failures = 0;
uint64_t NextKey = 0x4D45534800010000ULL;

void Check(bool passed, const std::string &message)
{
	++Checks;
	if (!passed) {
		++Failures;
		if (Failures <= 40)
			std::cerr << "FAIL " << message << '\n';
	}
}

struct Texture {
	uint64_t key = ++NextKey;
	int width = 1;
	int height = 1;
	unsigned levels = 1;
	std::vector<uint32_t> codes { 73 };
	std::vector<uint8_t> opacity;
	std::vector<uint8_t> lut;

	TownGpuTexture View() const
	{
		TownGpuTexture result;
		result.stableKey = key;
		result.revision = 1;
		result.width = width;
		result.height = height;
		result.texelCodes = codes;
		result.opacity = opacity;
		result.lightLut = lut;
		result.lightLevels = levels;
		return result;
	}
};

struct Mesh {
	TownGpuMeshIdentity identity { ++NextKey, 1 };
	std::vector<TownGpuMeshVertex> vertices;
	std::vector<uint32_t> indices;

	bool Upload() const
	{
		return TownGpuUploadMesh({ identity, vertices, indices });
	}
};

Mesh Quad(float x, float y, float width, float height, float depth,
    float minimumU = 0, float maximumU = 1, float maximumV = 1)
{
	Mesh result;
	result.vertices = {
		{ { x, -y, depth }, { minimumU, 0 } },
		{ { x + width, -y, depth }, { maximumU, 0 } },
		{ { x + width, -(y + height), depth }, { maximumU, maximumV } },
		{ { x, -(y + height), depth }, { minimumU, maximumV } },
	};
	result.indices = { 0, 1, 2, 0, 2, 3 };
	return result;
}

TownGpuMeshInstance Instance(const Mesh &mesh, uint32_t pick, float x = 0, float y = 0, float depth = 0)
{
	TownGpuMeshInstance result;
	result.identity = mesh.identity;
	result.pickId = pick;
	result.localToWorld[0][3] = x;
	result.localToWorld[1][3] = -y;
	result.localToWorld[2][3] = depth;
	return result;
}

bool Begin(const TownGpuProjection &projection = {}, const TownGpuMeshCamera &camera = {}, int width = Width, int height = Height)
{
	const bool started = TownGpuBeginFrame(width, height, true, projection);
	Check(started, "begin diagnostic frame: " + GetTownGpuStatus().failure);
	if (!started)
		return false;
	static bool reportedAdapter = false;
	if (!reportedAdapter) {
		const auto &status = GetTownGpuStatus();
		std::cout << "D3D11 adapter: " << status.adapter << "; WARP=" << (status.warp ? "true" : "false")
		          << "; featureLevel=" << status.featureLevel << '\n';
		reportedAdapter = true;
	}
	Check(TownGpuSetShadow({}), "clear directional shadow binding");
	const bool configured = TownGpuSetMeshCamera(camera);
	Check(configured, "set mesh camera before geometry: " + GetTownGpuStatus().failure);
	return configured;
}

TownGpuFrame End(int width = Width, int height = Height)
{
	TownGpuFrame frame;
	const bool complete = TownGpuEndFrame(frame);
	Check(complete, "publish complete GPU frame: " + GetTownGpuStatus().failure);
	const size_t count = static_cast<size_t>(width) * height;
	Check(complete && GetTownGpuStatus().frameSucceeded && frame.width == width && frame.height == height
	        && frame.indexed.size() == count && frame.pickIds.size() == count && frame.depth.size() == count,
	    "color, frame-local IDs and view depth are published together");
	return frame;
}

void EmptyFailure(const std::string &name)
{
	TownGpuFrame frame;
	frame.width = frame.height = 1;
	frame.indexed = { 255 };
	frame.pickIds = { 123 };
	frame.depth = { 42 };
	Check(!TownGpuEndFrame(frame) && !GetTownGpuStatus().frameSucceeded && frame.width == 0 && frame.height == 0
	        && frame.indexed.empty() && frame.pickIds.empty() && frame.depth.empty(),
	    name + " clears every output; the adapter must redraw the entire frame on CPU");
}

void Pixel(const TownGpuFrame &frame, int x, int y, uint8_t color, uint32_t pick, float depth, const std::string &name)
{
	const size_t index = static_cast<size_t>(y) * frame.width + x;
	Check(index < frame.indexed.size() && index < frame.pickIds.size() && index < frame.depth.size()
	        && frame.indexed[index] == color && frame.pickIds[index] == pick
	        && (std::isinf(depth) ? std::isinf(frame.depth[index]) : std::abs(frame.depth[index] - depth) < 0.0001F),
	    name);
}

void SameFrame(const TownGpuFrame &a, const TownGpuFrame &b, const std::string &name, float depthTolerance = 0.0001F)
{
	Check(a.width == b.width && a.height == b.height && a.indexed == b.indexed && a.pickIds == b.pickIds,
	    name + " identical colors and IDs");
	bool same = a.depth.size() == b.depth.size();
	for (size_t i = 0; same && i < a.depth.size(); ++i)
		same = (std::isinf(a.depth[i]) && std::isinf(b.depth[i]))
		    || std::abs(a.depth[i] - b.depth[i]) <= depthTolerance * std::max(1.0F, std::abs(a.depth[i]));
	Check(same, name + " equivalent unnormalized view depth");
}

std::array<float, 3> Transform(const TownGpuMeshAffine &matrix, const std::array<float, 3> &point)
{
	std::array<float, 3> result {};
	for (size_t row = 0; row < 3; ++row)
		result[row] = matrix[row][0] * point[0] + matrix[row][1] * point[1] + matrix[row][2] * point[2] + matrix[row][3];
	return result;
}

bool Projected(const Mesh &mesh, const TownGpuMeshInstance &instance, const Texture &texture,
    const TownGpuMaterial &material, const TownCameraFrame *camera = nullptr)
{
	const size_t count = instance.indexCount == 0 ? mesh.indices.size() - instance.firstIndex : instance.indexCount;
	for (size_t first = instance.firstIndex; first < instance.firstIndex + count; first += 3) {
		std::array<TownCameraVertex, 3> triangle;
		for (size_t corner = 0; corner < 3; ++corner) {
			const auto &source = mesh.vertices[mesh.indices[first + corner]];
			const auto world = Transform(instance.localToWorld, source.position);
			triangle[corner] = { { world[0], world[1], world[2] }, source.uv[0], source.uv[1] };
		}
		if (camera != nullptr) {
			const auto clipped = ClipTownCameraTriangle(*camera, triangle);
			for (size_t part = 0; part < clipped.count; ++part) {
				std::array<TownGpuVertex, 3> vertices;
				for (size_t corner = 0; corner < 3; ++corner) {
					const auto &v = clipped.triangles[part][corner];
					vertices[corner] = { v.x, v.y, v.depth, v.source.u, v.source.v,
						{ v.source.world.x, v.source.world.height, v.source.world.z }, camera->perspective ? v.depth : 1 };
				}
				if (!TownGpuSubmitProjectedTriangle(vertices, texture.View(), material, instance.pickId))
					return false;
			}
		} else {
			std::array<TownGpuVertex, 3> vertices;
			for (size_t corner = 0; corner < 3; ++corner) {
				const auto &v = triangle[corner];
				vertices[corner] = { v.world.x, -v.world.height, v.world.z, v.u, v.v,
					{ v.world.x, v.world.height, v.world.z } };
			}
			if (!TownGpuSubmitProjectedTriangle(vertices, texture.View(), material, instance.pickId))
				return false;
		}
	}
	return true;
}

void Persistence()
{
	std::cout << "CASE immutable buffers, cold/warm reuse and two instances\n";
	ResetTownGpuResources();
	Mesh mesh = Quad(0, 0, 24, 24, 100);
	const Mesh original = mesh;
	Texture texture;
	texture.codes[0] = 41;
	Check(!TownGpuHasMesh(mesh.identity), "cache miss is a harmless query before device creation");
	if (!Begin())
		return;
	Check(mesh.Upload(), "upload one indexed quad");
	Check(TownGpuHasMesh(mesh.identity), "uploaded identity exists during recording");
	const auto a = Instance(mesh, 101, 8, 8);
	const auto b = Instance(mesh, 102, 72, 40);
	// Upload must own both arrays. No queued draw may retain these borrowed spans.
	mesh.vertices.assign(1, {});
	mesh.indices.assign(1, 999999);
	mesh.vertices.clear();
	mesh.indices.clear();
	Check(TownGpuSubmitMeshInstance(a, texture.View(), {}) && TownGpuSubmitMeshInstance(b, texture.View(), {}),
	    "draw two instances after borrowed vertex/index arrays are released");
	const auto cold = End();
	const auto coldStats = GetTownGpuMeshStats();
	Check(coldStats.uploads == 1 && coldStats.uploadedVertexBytes == original.vertices.size() * sizeof(TownGpuMeshVertex)
	        && coldStats.uploadedIndexBytes == original.indices.size() * sizeof(uint32_t) && coldStats.instances == 2
	        && coldStats.cachedMeshes == 1 && coldStats.projectedUploadBytes == 0,
	    "cold frame uploads one VB/IB pair and no projected vertex bytes");
	Pixel(cold, 12, 12, 41, 101, 100, "first instance owns its native ID");
	Pixel(cold, 76, 44, 41, 102, 100, "second instance reuses geometry with its own ID");
	Pixel(cold, 48, 32, 0, 0, std::numeric_limits<float>::infinity(), "space between instances remains untouched");
	if (!Begin())
		return;
	Check(original.Upload(), "repeated immutable identity/layout is an O(1) cache hit");
	Check(TownGpuSubmitMeshInstance(a, texture.View(), {}) && TownGpuSubmitMeshInstance(b, texture.View(), {}), "repeat both cached instances");
	const auto warm = End();
	const auto warmStats = GetTownGpuMeshStats();
	Check(warmStats.uploads == 0 && warmStats.uploadedVertexBytes == 0 && warmStats.uploadedIndexBytes == 0
	        && warmStats.projectedUploadBytes == 0 && warmStats.instances == 2 && warmStats.reusedInstances == 2,
	    "warm frame uploads zero geometry bytes and reports both reused instances");
	SameFrame(cold, warm, "cold/warm indexed frame");
	std::cout << "METRIC coldVertexBytes=" << coldStats.uploadedVertexBytes << " coldIndexBytes=" << coldStats.uploadedIndexBytes
	          << " warmVertexBytes=" << warmStats.uploadedVertexBytes << " warmIndexBytes=" << warmStats.uploadedIndexBytes
	          << " residentBytes=" << warmStats.residentBytes << " commandCpuMs=" << warmStats.commandMilliseconds
	          << " readbackMs=" << GetTownGpuStatus().readbackMilliseconds << '\n';
	for (int idle = 0; idle < 4; ++idle) {
		if (!Begin())
			return;
		End();
	}
	Check(TownGpuHasMesh(original.identity), "unused geometry survives brief camera absence without budget pressure");
	if (!Begin({}, {}, 137, 101))
		return;
	Check(TownGpuHasMesh(original.identity) && TownGpuSubmitMeshInstance(a, texture.View(), {}), "resize preserves immutable geometry");
	const auto resized = End(137, 101);
	Pixel(resized, 136, 100, 0, 0, std::numeric_limits<float>::infinity(), "resized frame clears newly exposed selection/depth");
	Check(GetTownGpuMeshStats().uploads == 0, "resize does not reupload static buffers");
	ResetTownGpuResources();
	ResetTownGpuResources();
	Check(!TownGpuHasMesh(original.identity) && GetTownGpuMeshStats().cachedMeshes == 0 && GetTownGpuMeshStats().residentBytes == 0,
	    "idempotent reset clears all mesh identities and resident bytes");
	if (!Begin())
		return;
	Check(original.Upload() && TownGpuSubmitMeshInstance(a, texture.View(), {}) && TownGpuSubmitMeshInstance(b, texture.View(), {}),
	    "reset permits reupload with the same decoded identity");
	SameFrame(cold, End(), "device recreation");
}

void IdentityAndRanges()
{
	std::cout << "CASE coexisting revisions, native pick ranges and rejected layouts\n";
	ResetTownGpuResources();
	Mesh a = Quad(0, 0, 48, 48, 100);
	Mesh b = Quad(0, 0, 24, 24, 100);
	b.identity = { a.identity.stableKey, a.identity.revision + 1 };
	Texture texture;
	texture.codes[0] = 77;
	if (!Begin())
		return;
	Check(a.Upload() && b.Upload(), "two revisions of one stable identity can coexist");
	auto first = Instance(a, 201, 8, 8);
	first.indexCount = 3;
	auto second = first;
	second.firstIndex = 3;
	second.indexCount = 0;
	second.pickId = 202;
	Check(TownGpuSubmitMeshInstance(first, texture.View(), {}) && TownGpuSubmitMeshInstance(second, texture.View(), {})
	        && TownGpuSubmitMeshInstance(Instance(b, 203, 88, 8), texture.View(), {}),
	    "split a cached index range where native pickTile changes");
	const auto frame = End();
	Pixel(frame, 48, 16, 77, 201, 100, "first triangle has its current-frame pick record");
	Pixel(frame, 16, 48, 77, 202, 100, "remaining triangle has its different native pick record");
	Pixel(frame, 96, 16, 77, 203, 100, "new revision is drawn alongside the old revision");
	Pixel(frame, 120, 16, 0, 0, std::numeric_limits<float>::infinity(), "new revision keeps its own smaller geometry");
	Check(TownGpuHasMesh(a.identity) && TownGpuHasMesh(b.identity) && GetTownGpuMeshStats().cachedMeshes == 2,
	    "revision identity never silently replaces a still-used revision");
	if (!Begin())
		return;
	first.pickId = 301;
	second.pickId = 302;
	Check(TownGpuSubmitMeshInstance(first, texture.View(), {}) && TownGpuSubmitMeshInstance(second, texture.View(), {}),
	    "reuse the same buffers with a rebuilt frame-local picking table");
	const auto repicked = End();
	Pixel(repicked, 48, 16, 77, 301, 100, "first cached range does not retain last frame's ID");
	Pixel(repicked, 16, 48, 77, 302, 100, "second cached range does not retain last frame's ID");
	if (!Begin())
		return;
	Mesh invalid = a;
	invalid.vertices.push_back(invalid.vertices.front());
	Check(!invalid.Upload(), "same identity/revision with a different vertex layout is rejected");
	EmptyFailure("conflicting immutable layout");
	for (int fault = 0; fault < 6; ++fault) {
		if (!Begin())
			return;
		auto range = Instance(a, 303);
		if (fault == 0) range.firstIndex = 1;
		if (fault == 1) range.indexCount = 4;
		if (fault == 2) range.firstIndex = 9;
		if (fault == 3) range.indexCount = 9;
		if (fault == 4) range.identity.revision = 999;
		if (fault == 5) range.localToWorld[0][3] = std::numeric_limits<float>::infinity();
		Check(!TownGpuSubmitMeshInstance(range, texture.View(), {}), "invalid/missing instance range is rejected before drawing");
		EmptyFailure("invalid instance range");
	}
	for (int fault = 0; fault < 5; ++fault) {
		if (!Begin())
			return;
		Mesh invalidMesh = Quad(8, 8, 24, 24, 100);
		if (fault == 0) invalidMesh.identity.stableKey = 0;
		if (fault == 1) invalidMesh.indices[2] = static_cast<uint32_t>(invalidMesh.vertices.size());
		if (fault == 2) invalidMesh.indices.pop_back();
		if (fault == 3) invalidMesh.vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
		if (fault == 4) invalidMesh.vertices[0].uv[0] = std::numeric_limits<float>::infinity();
		Check(!invalidMesh.Upload(), "invalid vertex/index payload is rejected");
		EmptyFailure("invalid mesh upload");
	}
}

void LargeIndicesAndState()
{
	std::cout << "CASE true 32-bit indices and invalid recording state\n";
	Mesh mesh;
	mesh.vertices.resize(65539);
	mesh.vertices[65536] = { { 8, -8, 100 }, { 0, 0 } };
	mesh.vertices[65537] = { { 56, -8, 100 }, { 1, 0 } };
	mesh.vertices[65538] = { { 8, -56, 100 }, { 0, 1 } };
	mesh.indices = { 65536, 65537, 65538 };
	Texture texture;
	texture.codes[0] = 61;
	if (!Begin())
		return;
	Check(!TownGpuHasMesh({ 0xBAD00000U, 1 }), "missing mesh lookup does not poison a recording frame");
	Check(mesh.Upload() && TownGpuSubmitMeshInstance(Instance(mesh, 350), texture.View(), {}),
	    "indices above 65535 address their actual vertices");
	Pixel(End(), 16, 16, 61, 350, 100, "32-bit index buffer never truncates to uint16");
	Check(TownGpuBeginFrame(Width, Height, true), "begin without a mesh-camera binding for rejection fixture");
	Check(!TownGpuSubmitMeshInstance(Instance(mesh, 351), texture.View(), {}), "mesh draw requires the current frame's explicit camera");
	EmptyFailure("missing per-frame mesh camera");
	if (!Begin())
		return;
	Mesh good = Quad(8, 8, 24, 24, 100);
	Check(Projected(good, Instance(good, 352), texture, {}), "record valid projected geometry before an upload failure");
	Mesh bad = good;
	bad.identity = { ++NextKey, 1 };
	bad.indices[0] = 999;
	Check(!bad.Upload(), "failed later upload invalidates earlier recorded projected geometry");
	EmptyFailure("failure after a valid queued command");
}

void CommandOrder()
{
	std::cout << "CASE projected -> mesh -> projected ordering and pick preservation\n";
	Mesh mesh = Quad(0, 0, 48, 48, 100);
	Texture base;
	base.codes[0] = 11;
	Texture middle;
	middle.codes[0] = 55;
	Texture last;
	last.codes[0] = 99;
	for (bool preserve : { false, true }) {
		if (!Begin())
			return;
		Check(mesh.Upload(), "cache ordering fixture geometry");
		Check(Projected(mesh, Instance(mesh, 401, 8, 8), base, {}), "first projected command");
		TownGpuMaterial material;
		material.preservePicking = preserve;
		Check(TownGpuSubmitMeshInstance(Instance(mesh, 402, 8, 8, preserve ? -20.0F : 0.0F), middle.View(), material), "middle indexed command");
		Check(Projected(mesh, Instance(mesh, 403, 24, 24, preserve ? -20.0F : 0.0F), last, {}), "last projected command");
		const auto frame = End();
		Pixel(frame, 12, 12, 55, preserve ? 401U : 402U, preserve ? 80.0F : 100.0F,
		    "middle mesh updates color/depth and honors pick preservation");
		Pixel(frame, 32, 32, 99, 403, preserve ? 80.0F : 100.0F, "later coplanar projected command wins after mesh");
		Check(GetTownGpuMeshStats().instances == 1 && GetTownGpuMeshStats().projectedUploadBytes > 0,
		    "one ordered command stream records both transports");
	}
	if (!Begin())
		return;
	Check(mesh.Upload() && TownGpuSubmitMeshInstance(Instance(mesh, 404), middle.View(), {}), "record mesh before state-mutation rejection");
	Check(!TownGpuSetShadow({}), "shadow changes are rejected after static as well as projected geometry");
	EmptyFailure("late shadow mutation");
	if (!Begin())
		return;
	Check(TownGpuSubmitMeshInstance(Instance(mesh, 405), middle.View(), {}), "record cached mesh before camera mutation");
	Check(!TownGpuSetMeshCamera({}), "mesh camera cannot change after geometry recording starts");
	EmptyFailure("late mesh camera mutation");
}

void ResidentDrawCountIsNotProjectedCapacity()
{
	std::cout << "CASE >1M resident triangles with a small immutable payload and ordered projected commands\n";
	ResetTownGpuResources();
	constexpr size_t TrianglesPerInstance = 4096;
	constexpr size_t Instances = 257;
	Mesh bulk;
	// Three shared vertices, wholly offscreen. Repeated indices exercise draw
	// count without a large payload or rasterizing a million overlapping faces.
	bulk.vertices = {
		{ { Width + 8.0F, -8, 100 }, { 0, 0 } },
		{ { Width + 16.0F, -8, 100 }, { 1, 0 } },
		{ { Width + 8.0F, -16, 100 }, { 0, 1 } },
	};
	bulk.indices.reserve(TrianglesPerInstance * 3);
	for (size_t i = 0; i < TrianglesPerInstance; ++i)
		bulk.indices.insert(bulk.indices.end(), { 0, 1, 2 });
	Mesh marker = Quad(0, 0, 48, 48, 100);
	Texture first;
	first.codes[0] = 11;
	Texture middle;
	middle.codes[0] = 55;
	Texture last;
	last.codes[0] = 99;
	if (!Begin())
		return;
	Check(marker.Upload(), "upload visible ordering marker");
	Check(Projected(marker, Instance(marker, 971, 8, 8), first, {})
	        && TownGpuSubmitMeshInstance(Instance(marker, 972, 8, 8), middle.View(), {})
	        && Projected(marker, Instance(marker, 973, 24, 24), last, {}),
	    "small projected/resident/projected reference");
	const auto reference = End();
	const size_t referenceProjectedBytes = GetTownGpuMeshStats().projectedUploadBytes;
	for (const bool warm : { false, true }) {
		if (!Begin())
			return;
		Check(bulk.Upload() && marker.Upload(), "cache small resident bulk and visible marker");
		Check(Projected(marker, Instance(marker, 971, 8, 8), first, {}), "projected command before resident bulk");
		bool recorded = true;
		for (size_t i = 0; recorded && i < Instances; ++i) {
			recorded = TownGpuSubmitMeshInstance(Instance(bulk, 974), first.View(), {});
			if (recorded && i == Instances / 2)
				recorded = TownGpuSubmitMeshInstance(Instance(marker, 972, 8, 8), middle.View(), {});
		}
		Check(recorded && GetTownGpuStatus().submittedTriangles > 1024 * 1024,
		    "resident aggregate exceeds the old 1M cap without consuming projected capacity");
		Check(Projected(marker, Instance(marker, 973, 24, 24), last, {}), "projected command still records after >1M resident triangles");
		const auto actual = End();
		SameFrame(reference, actual, "resident bulk preserves mixed command colors, IDs and depth");
		Pixel(actual, 12, 12, 55, 972, 100, "middle visible resident command retains its order and pick");
		Pixel(actual, 32, 32, 99, 973, 100, "last projected command wins after the resident aggregate");
		const auto &stats = GetTownGpuMeshStats();
		Check(GetTownGpuStatus().submittedTriangles == TrianglesPerInstance * Instances + 6
		        && GetTownGpuStatus().drawCalls == Instances + 3 && stats.instances == Instances + 1,
		    "total counters retain every resident triangle and ordered draw");
		Check(referenceProjectedBytes > 0 && referenceProjectedBytes < 1024
		        && stats.projectedUploadBytes == referenceProjectedBytes,
		    "actual projected stream uploads only the four visible projected triangles");
		Check(stats.residentBytes < 64 * 1024 && stats.uploadedVertexBytes + stats.uploadedIndexBytes < 64 * 1024,
		    "million resident triangles reuse less than 64 KiB of immutable geometry");
		Check(!warm || (stats.uploads == 0 && stats.uploadedVertexBytes == 0 && stats.uploadedIndexBytes == 0),
		    "warm >1M resident draw frame performs zero geometry uploads");
		Check(GetTownGpuStatus().failureKind == TownGpuFailureKind::None,
		    "large resident aggregate publishes with no failure kind");
	}
}

void FailureKindsAndFirstCause()
{
	std::cout << "CASE capacity/invalid-input classification, first cause and Begin/Reset lifecycle\n";
	ResetTownGpuResources();
	Check(GetTownGpuStatus().failureKind == TownGpuFailureKind::None && GetTownGpuStatus().failure.empty(),
	    "reset clears typed failure and message");
	Check(!TownGpuBeginFrame(0, Height, true) && GetTownGpuStatus().failureKind == TownGpuFailureKind::InvalidInput,
	    "nonpositive frame dimensions are invalid input");
	EmptyFailure("invalid dimensions");
	Check(!TownGpuBeginFrame(2049, 2048, true) && GetTownGpuStatus().failureKind == TownGpuFailureKind::Capacity,
	    "real pixel budget is recoverable capacity pressure");
	const auto capacity = GetTownGpuStatus();
	Texture texture;
	Check(!TownGpuSubmitProjectedTriangle({}, texture.View(), {}, 0)
	        && !TownGpuSubmitMeshInstance({}, texture.View(), {}) && !TownGpuSetShadow({}),
	    "later recording guards reject a failed capacity frame");
	Check(GetTownGpuStatus().failureKind == capacity.failureKind && GetTownGpuStatus().failure == capacity.failure,
	    "later invalid-frame guards preserve the first capacity cause");
	EmptyFailure("pixel capacity");
	if (!Begin())
		return;
	Check(GetTownGpuStatus().failureKind == TownGpuFailureKind::None && GetTownGpuStatus().failure.empty(),
	    "new Begin resets the first cause for a new workload");
	Mesh marker = Quad(8, 8, 24, 24, 100);
	Check(marker.Upload(), "upload valid range fixture");
	auto invalid = Instance(marker, 975);
	invalid.indexCount = 9;
	Check(!TownGpuSubmitMeshInstance(invalid, texture.View(), {})
	        && GetTownGpuStatus().failureKind == TownGpuFailureKind::InvalidInput,
	    "invalid resident range is not recoverable capacity pressure");
	const auto input = GetTownGpuStatus();
	Check(!TownGpuSetMeshCamera({}) && GetTownGpuStatus().failure == input.failure
	        && GetTownGpuStatus().failureKind == input.failureKind,
	    "later camera guard preserves the first invalid-input cause");
	EmptyFailure("invalid resident range");
	ResetTownGpuResources();
	Check(GetTownGpuStatus().failureKind == TownGpuFailureKind::None && GetTownGpuStatus().failure.empty(),
	    "explicit resource reset clears typed failure");
}

void ProjectedStreamCapacity()
{
	std::cout << "CASE actual projected-stream boundary; bounded CPU recording, no large GPU upload\n";
	// This is the real 240 MiB CPU-expanded stream limit. Never submit it to
	// EndFrame successfully: the next triangle must fail before a GPU upload.
	constexpr size_t ProjectedTriangleCapacity = 1024 * 1024;
	Texture texture;
	const std::array<TownGpuVertex, 3> triangle { {
		{ Width + 8.0F, 8, 100, 0, 0, { Width + 8.0F, -8, 100 } },
		{ Width + 16.0F, 8, 100, 1, 0, { Width + 16.0F, -8, 100 } },
		{ Width + 8.0F, 16, 100, 0, 1, { Width + 8.0F, -16, 100 } },
	} };
	if (!Begin())
		return;
	size_t recorded = 0;
	while (recorded < ProjectedTriangleCapacity && TownGpuSubmitProjectedTriangle(triangle, texture.View(), {}, 976))
		++recorded;
	Check(recorded == ProjectedTriangleCapacity && GetTownGpuStatus().submittedTriangles == recorded,
	    "real projected capacity accepts exactly 1M expanded triangles");
	Check(!TownGpuSubmitProjectedTriangle(triangle, texture.View(), {}, 976)
	        && GetTownGpuStatus().failureKind == TownGpuFailureKind::Capacity,
	    "one extra projected triangle fails at the actual CPU vertex-stream bound");
	const auto capacity = GetTownGpuStatus();
	Check(!TownGpuSetShadow({}) && !TownGpuSubmitMeshInstance({}, texture.View(), {})
	        && GetTownGpuStatus().failure == capacity.failure && GetTownGpuStatus().failureKind == capacity.failureKind,
	    "projected-stream capacity remains the first cause after later invalid guards");
	Check(GetTownGpuMeshStats().projectedUploadBytes == 0 && GetTownGpuStatus().submittedTriangles == recorded,
	    "overflow appends no triangle and sends zero expanded vertices to the GPU");
	EmptyFailure("projected stream capacity");
	if (!Begin())
		return;
	Check(GetTownGpuStatus().failureKind == TownGpuFailureKind::None && GetTownGpuStatus().failure.empty(),
	    "smaller workload Begin clears the capacity state");
	Mesh marker = Quad(8, 8, 24, 24, 100);
	Check(Projected(marker, Instance(marker, 977), texture, {}), "small projected workload records after capacity failure");
	Pixel(End(), 16, 16, 73, 977, 100, "small new frame succeeds after capacity rejection");
}

void MaterialParity(const std::string &name, Mesh &mesh, Texture &texture, const TownGpuMaterial &material,
    const TownGpuShadow &shadow = {})
{
	Texture base;
	base.codes[0] = 11;
	Mesh ground = Quad(0, 0, static_cast<float>(Width), static_cast<float>(Height), 200);
	if (!Begin())
		return;
	Check(TownGpuSetShadow(shadow), name + " projected shadow setup");
	Check(Projected(ground, Instance(ground, 501), base, {}) && Projected(mesh, Instance(mesh, 502), texture, material), name + " projected reference");
	const auto projected = End();
	if (!Begin())
		return;
	Check(TownGpuSetShadow(shadow), name + " mesh shadow setup");
	Check(mesh.Upload() && Projected(ground, Instance(ground, 501), base, {})
	        && TownGpuSubmitMeshInstance(Instance(mesh, 502), texture.View(), material), name + " indexed submission");
	SameFrame(projected, End(), name);
}

void Materials()
{
	std::cout << "CASE opacity, painted zero, repeat, fallback and exact supplied material\n";
	Mesh mesh = Quad(8, 8, 88, 64, 80, -0.25F, 1.75F, 1.25F);
	Texture masked;
	masked.width = masked.height = 3;
	masked.codes = { 0, 31, 73, 111, 0, 149, 181, 211, 0 };
	masked.opacity = { 1, 0, 1, 0, 1, 1, 1, 1, 0 };
	TownGpuMaterial material;
	material.transparentZero = true;
	MaterialParity("opacity mask retains opaque palette zero and clips outside UV", mesh, masked, material);
	Texture zero;
	zero.codes[0] = 0;
	MaterialParity("painted zero without transparentZero remains opaque", mesh, zero, {});
	MaterialParity("legacy transparentZero discards zero texels", mesh, zero, material);
	material.repeat = true;
	MaterialParity("negative and repeated UVs retain nearest palette sampling", mesh, masked, material);
	material.repeat = false;
	material.fallbackPaletteIndex = 87;
	MaterialParity("unlit cutout fallback fills discarded and out-of-range texels", mesh, masked, material);
	masked.levels = 16;
	masked.lut.resize(256 * masked.levels);
	for (size_t code = 0; code < 256; ++code)
		for (size_t level = 0; level < masked.levels; ++level)
			masked.lut[code * masked.levels + level] = static_cast<uint8_t>((code + level * 7) % 256);
	masked.key = ++NextKey;
	material.lighting = TownGpuLighting::Shadow;
	material.shade = 1;
	material.fallbackShade = 3;
	material.receivesShadow = true;
	material.shadowBias = 0.013F;
	material.shadowSlopeU = 0.2F;
	material.shadowSlopeV = -0.1F;
	std::vector<float> shadowDepth(32 * 32, 80.05F);
	TownGpuShadow shadow;
	shadow.stableKey = ++NextKey;
	shadow.revision = 1;
	shadow.resolution = 32;
	shadow.depth = shadowDepth;
	shadow.right = { 1, 0, 0 };
	shadow.up = { 0, 1, 0 };
	shadow.light = { 0, 0, 1 };
	shadow.minV = -96;
	shadow.texelU = shadow.texelV = 4;
	MaterialParity("supplied flat receiver and shaded volume fallback", mesh, masked, material, shadow);
	Texture directional;
	directional.codes[0] = 0;
	directional.levels = 64;
	directional.lut.resize(64);
	for (size_t i = 0; i < directional.lut.size(); ++i)
		directional.lut[i] = static_cast<uint8_t>(i + 1);
	TownGpuMaterial exact;
	exact.lighting = TownGpuLighting::Directional;
	exact.diffuse = 0.375F;
	exact.normal = { 0.2F, -0.7F, 0.3F };
	MaterialParity("material-normal mode preserves exact supplied diffuse", mesh, directional, exact);
}

TownCameraFrame CameraFrame(bool perspective, float nearClip = 1, float farClip = 10)
{
	TownCameraFrame result;
	result.right = { 1, 0, 0 };
	result.up = { 0, 1, 0 };
	result.forward = { 0, 0, 1 };
	result.heightScale = 1;
	result.focalPixels = 32;
	result.centerX = Width * 0.5F;
	result.centerY = Height * 0.5F;
	result.nearClip = nearClip;
	result.farClip = farClip;
	result.width = Width;
	result.height = Height;
	result.perspective = perspective;
	result.valid = true;
	return result;
}

TownGpuMeshCamera MeshCamera(const TownCameraFrame &frame)
{
	TownGpuMeshCamera result;
	const std::array<TownCameraPoint, 3> basis { frame.right, frame.up, frame.forward };
	for (size_t row = 0; row < basis.size(); ++row) {
		const auto &v = basis[row];
		result.worldToView[row] = { v.x, v.height * frame.heightScale, v.z,
			-(v.x * frame.eye.x + v.height * frame.heightScale * frame.eye.height + v.z * frame.eye.z) };
	}
	result.focalPixels = frame.focalPixels;
	result.centerX = frame.centerX;
	result.centerY = frame.centerY;
	result.eye = { frame.eye.x, frame.eye.height, frame.eye.z };
	result.towardViewer = { -frame.forward.x, -frame.forward.height / frame.heightScale, -frame.forward.z };
	return result;
}

void BorrowedLighting()
{
	std::cout << "CASE copied point lights, room apertures and opaque blockers\n";
	Mesh mesh = Quad(1, -1, 2, 2, 4);
	Texture texture;
	texture.codes[0] = 0;
	texture.levels = 2;
	texture.lut = { 17, 231 };
	auto camera = CameraFrame(false, 0.4F, 4096);
	camera.focalPixels = 16;
	std::vector<TownPointLight> lights { TownPointLight { { 2, 0, -1 }, { 1, 0.665F, 0.094F }, 16, 16 } };
	std::vector<TownLightAperture> apertures { TownLightAperture { TownLightPlane::Z, 0, 0, 4, -2, 2, 0 } };
	TownLightOccluder room { { 0, -2, 0 }, { 4, 2, 5 }, apertures };
	std::vector<TownLightOccluder> blockers { TownLightOccluder { { 1.8F, -0.5F, 1.8F }, { 2.2F, 0.5F, 2.2F }, {} } };
	TownGpuMaterial material;
	material.lighting = TownGpuLighting::Interior;
	material.normal = { 0, 0, -1 };
	material.interiorRedNormalization = 0.005F;
	material.lights = lights;
	material.room = &room;
	material.blockers = blockers;
	if (!Begin({}, MeshCamera(camera)))
		return;
	Check(Projected(mesh, Instance(mesh, 601), texture, material, &camera), "projected reference with room portal and blocker");
	const auto reference = End();
	if (!Begin({}, MeshCamera(camera)))
		return;
	Check(mesh.Upload() && TownGpuSubmitMeshInstance(Instance(mesh, 601), texture.View(), material), "mesh consumes all borrowed lighting spans");
	lights.clear();
	apertures.clear();
	blockers.clear();
	room = { { 100, 100, 100 }, { 101, 101, 101 }, {} };
	const auto actual = End();
	SameFrame(reference, actual, "borrowed material storage released before EndFrame");
	size_t dark = 0;
	size_t lit = 0;
	for (size_t i = 0; i < actual.pickIds.size(); ++i) {
		if (actual.pickIds[i] == 601) {
			dark += actual.indexed[i] == 17 ? 1 : 0;
			lit += actual.indexed[i] == 231 ? 1 : 0;
		}
	}
	Check(dark > 20 && lit > 20, "point-light fixture exercises both opaque blocking and visible aperture rays");
}

float Dot(const std::array<float, 3> &a, const std::array<float, 3> &b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> Unit(std::array<float, 3> normal)
{
	const float length = std::sqrt(Dot(normal, normal));
	for (float &value : normal)
		value /= length;
	return normal;
}

void VertexNormals()
{
	std::cout << "CASE flat authored normals, inverse transpose and two-sided orientation\n";
	for (bool perspective : { false, true }) {
		for (bool doubleSided : { false, true }) {
			auto camera = CameraFrame(perspective, 0.4F, 4096);
			camera.focalPixels = perspective ? 64.0F : 1.0F;
			if (!perspective)
				camera.centerX = camera.centerY = 0;
			Mesh mesh = perspective ? Quad(-1, -1, 2, 2, 4) : Quad(8, 8, 24, 48, 100);
			for (auto &vertex : mesh.vertices)
				vertex.normal = { 0.6F, 0, 0.8F };
			auto instance = Instance(mesh, 701);
			instance.vertexNormals = true;
			instance.doubleSidedNormals = doubleSided;
			instance.localToWorld[0][0] = 2;
			instance.localToWorld[1][1] = 0.5F;
			instance.toLight = { 0, 0, -1 };
			instance.shadowDepthBias = 0.017F;
			instance.shadowSlopeBias = 0.031F;
			Texture texture;
			texture.codes[0] = 0;
			texture.levels = 64;
			texture.lut.resize(texture.levels);
			for (size_t i = 0; i < texture.lut.size(); ++i)
				texture.lut[i] = static_cast<uint8_t>(i + 1);
			TownGpuMaterial material;
			material.lighting = TownGpuLighting::Directional;
			material.normal = { 0, 1, 0 };
			material.diffuse = 0.41F; // Must be replaced in vertex-normal mode.
			material.shadowBias = 0.75F;
			material.shadowSlopeU = 7;
			material.shadowSlopeV = 9;
			material.receivesShadow = true;
			TownGpuMaterial expected = material;
			// This diagonal nonuniform affine has inverse transpose diag(1/2,2,1).
			// The authored normal points away from both test cameras; only the
			// double-sided option may flip it. Geometry and winding stay intact.
			expected.normal = Unit({ 0.3F, 0, 0.8F });
			if (doubleSided)
				for (float &value : expected.normal)
					value = -value;
			const float towardsLight = Dot(expected.normal, instance.toLight);
			expected.diffuse = std::clamp(towardsLight, 0.0F, 1.0F);
			expected.shadowBias = instance.shadowDepthBias + instance.shadowSlopeBias * (1 - std::abs(towardsLight));
			expected.shadowSlopeU = 0;
			expected.shadowSlopeV = 0;
			std::vector<float> depths(32 * 32, perspective ? -3.9F : -99.9F);
			TownGpuShadow shadow;
			shadow.stableKey = ++NextKey;
			shadow.revision = 1;
			shadow.resolution = 32;
			shadow.depth = depths;
			shadow.right = { 1, 0, 0 };
			shadow.up = { 0, 1, 0 };
			shadow.light = instance.toLight;
			shadow.minU = perspective ? -4.0F : 0.0F;
			shadow.minV = perspective ? -4.0F : -96.0F;
			shadow.texelU = shadow.texelV = perspective ? 0.25F : 4.0F;
			shadow.pcfRadius = 0;
			if (std::abs(towardsLight) > 0.03F) {
				expected.shadowSlopeU = -Dot(expected.normal, shadow.right) / towardsLight;
				expected.shadowSlopeV = -Dot(expected.normal, shadow.up) / towardsLight;
			}
			const TownGpuProjection projection { perspective, camera.nearClip, camera.farClip };
			if (!Begin(projection, MeshCamera(camera)))
				return;
			Check(TownGpuSetShadow(shadow) && Projected(mesh, instance, texture, expected, &camera), "projected oracle for inverse-transpose flat normal");
			const auto reference = End();
			if (!Begin(projection, MeshCamera(camera)))
				return;
			Check(TownGpuSetShadow(shadow) && mesh.Upload() && TownGpuSubmitMeshInstance(instance, texture.View(), material), "derive normal/diffuse/receiver in mesh vertex shader");
			const auto actual = End();
			SameFrame(reference, actual, std::string(perspective ? "perspective" : "orthographic") + (doubleSided ? " two-sided" : " one-sided") + " flat-normal lighting");
			const size_t visible = static_cast<size_t>(std::count(actual.pickIds.begin(), actual.pickIds.end(), 701U));
			Check(visible > 100, "normal fixture retains visible geometry and picking for both sides");
		}
	}
}

void UnequalAxisNormals()
{
	std::cout << "CASE finite inverse-transpose normal with overflowing squared length\n";
	ResetTownGpuResources();
	Mesh mesh;
	mesh.vertices = {
		{ { 0, -8.0e-6F, 8.0e-6F }, { 0, 0 }, { 1, 0, 0 } },
		{ { 0, -8.0e-6F, 72.0e-6F }, { 1, 0 }, { 1, 0, 0 } },
		{ { 0, -72.0e-6F, 72.0e-6F }, { 1, 1 }, { 1, 0, 0 } },
		{ { 0, -72.0e-6F, 8.0e-6F }, { 0, 1 }, { 1, 0, 0 } },
	};
	mesh.indices = { 0, 1, 2, 0, 2, 3 };
	auto instance = Instance(mesh, 785);
	instance.localToWorld[0][0] = 1.0e-20F;
	instance.localToWorld[1][1] = instance.localToWorld[2][2] = 1.0e6F;
	instance.vertexNormals = true;
	instance.toLight = { 1, 0, 0 };
	Texture texture;
	texture.codes[0] = 0;
	texture.levels = 2;
	texture.lut = { 17, 201 };
	TownGpuMaterial material;
	material.lighting = TownGpuLighting::Directional;
	material.diffuse = 0;
	TownCameraFrame camera;
	camera.eye = { -2, 0, 0 };
	camera.right = { 0, 0, 1 };
	camera.up = { 0, 1, 0 };
	camera.forward = { 1, 0, 0 };
	camera.width = Width;
	camera.height = Height;
	camera.nearClip = 0.4F;
	camera.farClip = 4096;
	camera.valid = true;
	if (!Begin({}, MeshCamera(camera)))
		return;
	auto expected = material;
	expected.normal = { 1, 0, 0 };
	expected.diffuse = 1;
	Check(Projected(mesh, instance, texture, expected, &camera), "projected oracle uses unit X normal on visible YZ plane");
	const auto reference = End();
	if (!Begin({}, MeshCamera(camera)))
		return;
	Check(mesh.Upload() && TownGpuSubmitMeshInstance(instance, texture.View(), material), "extremely unequal valid affine keeps its authored normal");
	const auto actual = End();
	SameFrame(reference, actual, "rescaled inverse-transpose normalization");
	Pixel(actual, 16, 16, 201, 785, 2, "finite normal of magnitude 1e20 still receives full directional light");
}

void StableClipComparison(const TownGpuFrame &reference, const TownGpuFrame &actual, const std::string &name, bool visible)
{
	if (reference.width != Width || reference.height != Height || actual.width != Width || actual.height != Height)
		return;
	size_t interior = 0;
	size_t exterior = 0;
	size_t mismatches = 0;
	size_t edgeExcluded = 0;
	// D3D clipping and CPU clipping may triangulate the boundary differently.
	// Compare all stable interior/exterior samples; only a two-pixel geometric
	// silhouette band is excluded, never IDs/materials throughout the object.
	for (int y = 2; y < Height - 2; ++y) {
		for (int x = 2; x < Width - 2; ++x) {
			const size_t pixel = static_cast<size_t>(y) * Width + x;
			const bool covered = reference.pickIds[pixel] == 801;
			bool stable = true;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
					stable = stable && ((reference.pickIds[static_cast<size_t>(y + dy) * Width + x + dx] == 801) == covered);
			if (!stable) {
				++edgeExcluded;
				continue;
			}
			bool same = reference.pickIds[pixel] == actual.pickIds[pixel] && reference.indexed[pixel] == actual.indexed[pixel];
			if (covered) {
				++interior;
				same = same && std::abs(reference.depth[pixel] - actual.depth[pixel]) < 0.0002F * std::max(1.0F, reference.depth[pixel]);
			} else {
				++exterior;
				same = same && std::isinf(actual.depth[pixel]);
			}
			mismatches += same ? 0 : 1;
		}
	}
	Check(mismatches == 0, name + " raw mesh agrees with CPU six-plane clipping away from its measured silhouette");
	Check(visible ? interior > 20 : interior == 0, name + " has the expected visible/fully clipped geometry");
	Check(interior + exterior > 3000, name + " oracle compares a meaningful stable region");
	std::cout << "CLIP " << name << " interior=" << interior << " exterior=" << exterior
	          << " geometricEdgeBand=" << edgeExcluded << " mismatches=" << mismatches << '\n';
	if (!visible)
		Check(std::all_of(actual.pickIds.begin(), actual.pickIds.end(), [](uint32_t pick) { return pick == 0; })
		        && std::all_of(actual.depth.begin(), actual.depth.end(), [](float depth) { return std::isinf(depth); }),
		    name + " fully clipped mesh publishes no stale or clamped geometry");
}

void ClipCase(const std::string &name, const TownCameraFrame &camera, const std::array<TownCameraPoint, 3> &view, bool visible)
{
	Mesh mesh;
	mesh.indices = { 0, 1, 2 };
	for (size_t i = 0; i < view.size(); ++i) {
		const auto world = TownCameraToWorld(camera, view[i]);
		mesh.vertices.push_back({ { world.x, world.height, world.z }, { static_cast<float>(i) * 0.25F, 0.5F } });
	}
	Texture texture;
	texture.codes[0] = 83;
	const TownGpuProjection projection { camera.perspective, camera.nearClip, camera.farClip };
	if (!Begin(projection, MeshCamera(camera)))
		return;
	Check(Projected(mesh, Instance(mesh, 801), texture, {}, &camera), name + " CPU-clipped projected reference");
	const auto reference = End();
	if (!Begin(projection, MeshCamera(camera)))
		return;
	Check(mesh.Upload() && TownGpuSubmitMeshInstance(Instance(mesh, 801), texture.View(), {}), name + " raw unprojected mesh");
	const auto actual = End();
	Check(GetTownGpuMeshStats().projectedUploadBytes == 0, name + " performs clipping without projected CPU uploads");
	StableClipComparison(reference, actual, name, visible);
}

void Clipping()
{
	std::cout << "CASE orthographic/perspective near, far, sides and depth <= 0\n";
	for (bool perspective : { false, true }) {
		auto camera = CameraFrame(perspective);
		const std::string prefix = perspective ? "perspective " : "orthographic ";
		const float scale = perspective ? 4.0F : 1.0F;
		const std::array<TownCameraPoint, 3> inside { TownCameraPoint { -1.25F * scale, 0.75F * scale, 4 },
			TownCameraPoint { 1.25F * scale, 0.75F * scale, 4 }, TownCameraPoint { 0, -1.0F * scale, 4 } };
		ClipCase(prefix + "inside", camera, inside, true);
		for (int plane = 0; plane < 6; ++plane) {
			auto triangle = inside;
			if (plane == 0) triangle[0].z = 0.25F;
			if (plane == 1) triangle[0].z = 14;
			if (plane == 2) triangle[0].x = -4 * scale;
			if (plane == 3) triangle[1].x = 4 * scale;
			if (plane == 4) triangle[0].height = 3 * scale;
			if (plane == 5) triangle[2].height = -3 * scale;
			ClipCase(prefix + "cross plane " + std::to_string(plane), camera, triangle, true);
		}
		for (const float depth : { 0.0F, -2.0F }) {
			auto triangle = inside;
			triangle[0].z = depth;
			ClipCase(prefix + "cross depth " + std::to_string(depth), camera, triangle, true);
		}
		for (const float depth : { -2.0F, 0.25F, 14.0F }) {
			auto triangle = inside;
			for (auto &vertex : triangle)
				vertex.z = depth;
			ClipCase(prefix + "fully outside depth " + std::to_string(depth), camera, triangle, false);
		}
		for (const float depth : { 1.0F, 10.0F }) {
			auto triangle = inside;
			for (auto &vertex : triangle) {
				vertex.z = depth;
				if (perspective) {
					vertex.x *= depth / 4;
					vertex.height *= depth / 4;
				}
			}
			ClipCase(prefix + "exact depth boundary " + std::to_string(depth), camera, triangle, true);
		}
		camera.centerX = 29;
		camera.centerY = 61;
		camera.focalPixels = 27;
		camera.eye = { 2, 3, -4 };
		camera.heightScale = 0.8F;
		ClipCase(prefix + "asymmetric center, eye translation and height scale", camera, inside, true);
	}
}

void LegacyVolumes()
{
	std::cout << "CASE grouped legacy volumes: palette/front/fallback/shade/backface without per-face CPU submissions\n";
	for (const bool perspective : { false, true }) {
		const TownCameraFrame camera = CameraFrame(perspective);
		for (const bool repeat : { false, true }) {
			for (const bool paintedZero : { false, true }) {
				for (int lighting = 0; lighting <= 3; ++lighting) {
					Texture texture;
					texture.width = 2;
					texture.codes = { paintedZero ? 0U : 17U, 0 };
					texture.opacity = { 1, 0 };
					texture.levels = 16;
					texture.lut.resize(256 * 16);
					for (size_t code = 0; code < 256; ++code)
						for (size_t level = 0; level < 16; ++level)
							texture.lut[code * 16 + level] = static_cast<uint8_t>((code + level * 3) % 256);
					const auto quad = [&](int x, uint32_t face, float depth, bool back) {
						Mesh mesh = Quad(static_cast<float>(x), 8, 32, 64, depth,
						    repeat ? -0.5F : 0, repeat ? 1.5F : 1);
						for (auto &vertex : mesh.vertices) {
							const float scale = perspective ? depth / camera.focalPixels : 1 / camera.focalPixels;
							vertex.position[0] = (vertex.position[0] - camera.centerX) * scale;
							vertex.position[1] = (vertex.position[1] + camera.centerY) * scale;
							vertex.normal = { 0, 0, back ? 1.0F : -1.0F };
							vertex.volumeFace = face;
						}
						if (back) {
							std::swap(mesh.indices[1], mesh.indices[2]);
							std::swap(mesh.indices[4], mesh.indices[5]);
						}
						return mesh;
					};
					const std::array<Mesh, 4> parts { quad(8, 256 | 64, 4, false), quad(48, 100, 4, false),
						quad(88, 0, 4, false), quad(8, 255, 3, true) };
					Mesh grouped;
					for (const auto &part : parts) {
						const uint32_t offset = static_cast<uint32_t>(grouped.vertices.size());
						grouped.vertices.insert(grouped.vertices.end(), part.vertices.begin(), part.vertices.end());
						for (const uint32_t index : part.indices)
							grouped.indices.push_back(offset + index);
					}
					TownGpuMaterial material;
					material.lighting = TownGpuLighting::Shadow;
					material.transparentZero = true;
					material.repeat = repeat;
					material.normal = { 0, 0, -1 };
					material.shade = lighting;
					material.fallbackPaletteIndex = 64;
					material.fallbackShade = std::min(3, lighting + 2);
					if (!Begin({ perspective, 1, 10 }, MeshCamera(camera)))
						return;
					Check(Projected(parts[0], Instance(parts[0], 901), texture, material, &camera), "legacy front reference");
					for (size_t part = 1; part <= 2; ++part) {
						Texture solid = texture;
						solid.key = ++NextKey;
						solid.width = 1;
						solid.codes = { part == 1 ? 100U : 0U };
						solid.opacity.clear();
						TownGpuMaterial face = material;
						face.transparentZero = false;
						face.repeat = false;
						face.shade = std::min(3, lighting + 2);
						face.fallbackPaletteIndex = -1;
						Check(Projected(parts[part], Instance(parts[part], 901), solid, face, &camera), "legacy opaque palette reference");
					}
					const auto reference = End();
					if (!Begin({ perspective, 1, 10 }, MeshCamera(camera)))
						return;
					TownGpuMeshInstance instance = Instance(grouped, 901);
					instance.vertexNormals = true;
					instance.volumeMaterial = true;
					instance.volumeLighting = lighting;
					Check(grouped.Upload() && TownGpuSubmitMeshInstance(instance, texture.View(), material), "one grouped volume submission");
					const auto actual = End();
					SameFrame(reference, actual, "grouped native volume/reference");
					Check(GetTownGpuStatus().drawCalls == 1 && GetTownGpuMeshStats().instances == 1
					        && GetTownGpuMeshStats().projectedUploadBytes == 0,
					    "front and palette sides share one immutable draw; back face produces no pixels");
					if (!repeat) {
						Pixel(actual, 16, 16, static_cast<uint8_t>((paintedZero ? 0 : 17) + lighting * 12), 901, 4, "front color uses native base lighting and opacity retains painted zero");
						Pixel(actual, 32, 16, static_cast<uint8_t>(64 + std::min(3, lighting + 2) * 12), 901, 4, "masked front uses authored palette and normal-derived fallback shade");
						Pixel(actual, 96, 16, static_cast<uint8_t>(std::min(3, lighting + 2) * 12), 901, 4, "opaque side palette zero bypasses front mask and transparent zero");
					}
					if (!Begin({ perspective, 1, 10 }, MeshCamera(camera)))
						return;
					Check(TownGpuSubmitMeshInstance(instance, texture.View(), material), "warm grouped volume reuses immutable upload");
					SameFrame(actual, End(), "warm grouped volume");
					Check(GetTownGpuMeshStats().uploads == 0 && GetTownGpuMeshStats().reusedInstances == 1, "warm volume has zero geometry upload");
				}
			}
		}
	}
	Mesh bad = Quad(8, 8, 32, 32, 4);
	bad.vertices[0].volumeFace = 512;
	if (Begin()) {
		Check(bad.Upload(), "generic mesh upload can retain ignored optional face data");
		TownGpuMeshInstance instance = Instance(bad, 902);
		instance.vertexNormals = true;
		instance.volumeMaterial = true;
		TownGpuMaterial material;
		material.lighting = TownGpuLighting::Shadow;
		Texture texture;
		Check(!TownGpuSubmitMeshInstance(instance, texture.View(), material), "invalid flat volume face attributes reject before GPU drawing");
		EmptyFailure("invalid volume contract");
	}
}

} // namespace

int main()
{
	std::cout << "Scope: synthetic offscreen buffers/materials/clipping only; WARP explicitly allowed; no gameplay FPS measurement\n";
	Persistence();
	IdentityAndRanges();
	LargeIndicesAndState();
	CommandOrder();
	ResidentDrawCountIsNotProjectedCapacity();
	FailureKindsAndFirstCause();
	Materials();
	BorrowedLighting();
	VertexNormals();
	UnequalAxisNormals();
	Clipping();
	LegacyVolumes();
	ProjectedStreamCapacity();
	ResetTownGpuResources();
	std::cout << (Failures == 0 ? "PASS " : "FAIL ") << Checks << " checks; " << Failures << " failures\n";
	return Failures == 0 ? 0 : 1;
}

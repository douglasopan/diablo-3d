// Synthetic, windowless validation for the private first-person GPU timer.
// Build only against a private instrumented town_gpu.cpp snapshot.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "engine/render/town_gpu.hpp"
#include "first_person_profile.hpp"

namespace {
using namespace devilution;
namespace fp = devilution::first_person_profile;
int Checks = 0;
int Failures = 0;
void Check(bool condition, const char *description)
{
	++Checks;
	if (!condition) {
		++Failures;
		std::cerr << "FAIL " << description << '\n';
	}
}

bool SubmitSquare(int width, int height)
{
	static const std::array<uint32_t, 1> Codes { 23 };
	TownGpuTexture texture;
	texture.stableKey = 0x465054494D4552ULL;
	texture.width = texture.height = 1;
	texture.texelCodes = Codes;
	TownGpuMaterial material;
	const auto vertex = [](float x, float y) {
		return TownGpuVertex { x, y, 8, 0.5F, 0.5F, { 0, 0, 0 }, 8 };
	};
	const auto a = vertex(width * 0.125F, height * 0.125F);
	const auto b = vertex(width * 0.875F, height * 0.125F);
	const auto c = vertex(width * 0.875F, height * 0.875F);
	const auto d = vertex(width * 0.125F, height * 0.875F);
	return TownGpuSubmitProjectedTriangle({ a, b, c }, texture, material, 17)
	    && TownGpuSubmitProjectedTriangle({ a, c, d }, texture, material, 17);
}

void Series(std::ostream &rows, int series, int width, int height, int frames, bool reset)
{
	if (reset)
		ResetTownGpuResources();
	TownGpuFrame previous;
	for (int frame = 0; frame < frames; ++frame) {
		fp::Reset();
		const auto start = std::chrono::steady_clock::now();
		const bool began = TownGpuBeginFrame(width, height, false, { true, 0.4F, 100 });
		Check(began, "hardware BeginFrame");
		if (!began) {
			std::cerr << GetTownGpuStatus().failure << '\n';
			return;
		}
		Check(TownGpuSetShadow({}), "empty synthetic shadow");
		Check(SubmitSquare(width, height), "submit two perspective triangles");
		TownGpuFrame output;
		const bool ended = TownGpuEndFrame(output);
		const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const auto &status = GetTownGpuStatus();
		Check(ended && status.frameSucceeded, "complete GPU frame");
		Check(status.available && !status.warp, "hardware device, never WARP");
		Check(status.adapter.find("570") != std::string::npos, "RX 570 adapter identity");
		if (!ended)
			return;
		const size_t count = static_cast<size_t>(width) * height;
		Check(output.width == width && output.height == height, "dimensions after resize");
		Check(output.indexed.size() == count && output.pickIds.size() == count && output.depth.size() == count, "three coherent output sizes");
		Check(status.submittedTriangles == 2 && status.drawCalls == 1, "two triangles in one batch");
		const size_t center = static_cast<size_t>(height / 2) * width + width / 2;
		Check(output.indexed[center] == 23 && output.pickIds[center] == 17 && std::abs(output.depth[center] - 8) < 0.00001F, "center color, picking and perspective depth");
		Check(output.indexed[0] == 0 && output.pickIds[0] == 0 && std::isinf(output.depth[0]), "untouched background");
		if (frame > 0)
			Check(output.indexed == previous.indexed && output.pickIds == previous.pickIds && output.depth == previous.depth, "exact warm color, ID and depth");
		if (fp::Current.gpuTimestampValid) {
			Check(std::isfinite(fp::Current.gpuDrawTimestampMs) && fp::Current.gpuDrawTimestampMs >= 0, "finite nonnegative draw timestamp");
			Check(std::isfinite(fp::Current.gpuCopyTimestampMs) && fp::Current.gpuCopyTimestampMs >= 0, "finite nonnegative copy timestamp");
		}
		rows << series << ',' << frame << ',' << width << ',' << height << ',' << (reset && frame == 0) << ','
		     << fp::Current.gpuTimestampValid << ',' << fp::Current.gpuDrawTimestampMs << ','
		     << fp::Current.gpuCopyTimestampMs << ',' << wall << ',' << status.readbackMilliseconds << ','
		     << fp::Current.milliseconds[fp::ReadbackAllocation] << ','
		     << fp::Current.milliseconds[fp::ReadbackMapWait] << ','
		     << fp::Current.milliseconds[fp::ReadbackCpuCopy] << ",\"" << status.adapter << "\"\n";
		previous = std::move(output);
	}
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "Usage: first_person_gpu_timing_smoke.exe <output.csv>\n";
		return 2;
	}
	std::ofstream rows(argv[1]);
	if (!rows)
		return 2;
	rows << "series,frame,width,height,reset,gpu_timestamp_valid,gpu_draw_ms,gpu_copy_ms,wall_ms,readback_ms,allocation_ms,map_ms,cpu_copy_ms,adapter\n";
	Series(rows, 0, 64, 48, 4, false);
	Series(rows, 1, 320, 180, 4, false);
	Series(rows, 2, 961, 541, 4, false);
	Series(rows, 3, 1920, 1080, 6, false);
	Series(rows, 4, 64, 48, 4, false);
	Series(rows, 5, 320, 180, 4, true);
	ResetTownGpuResources();
	std::cout << "checks=" << Checks << " failures=" << Failures << '\n';
	std::cout << "No window, game data, assets, sleeps, polling loops or configuration changes.\n";
	std::cout << "GPU timestamp spans include queue scheduling and possible bubbles between CPU commands; they are not GPU busy-time counters or sustained game FPS.\n";
	return Failures == 0 ? 0 : 1;
}

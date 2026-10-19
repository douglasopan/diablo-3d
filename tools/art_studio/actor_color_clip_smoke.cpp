#include "engine/render/town_camera.hpp"
#include "town_camera_checks.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace devilution;
std::size_t Checks = 0, Failures = 0;
void Check(bool pass, const char *name)
{
    ++Checks;
    if (!pass) { ++Failures; std::cerr << "FAIL " << name << '\n'; }
}
bool Close(float a, float b) { return std::isfinite(a) && std::abs(a - b) < 0.000002F; }
std::array<float, 4> AffineColor(TownCameraPoint point)
{
    return { 0.5F + point.x / 40, 0.5F + point.height / 40, point.z / 20, 0.375F };
}
bool SameLegacy(const TownCameraProjectedVertex &a, const TownCameraProjectedVertex &b)
{
    return a.x == b.x && a.y == b.y && a.depth == b.depth && a.reciprocalW == b.reciprocalW
        && a.source.world.x == b.source.world.x && a.source.world.height == b.source.world.height
        && a.source.world.z == b.source.world.z && a.source.u == b.source.u && a.source.v == b.source.v;
}
} // namespace

int main()
{
    using namespace devilution;
    Check(RunTownCameraChecks(std::cout, Checks), "existing camera contracts remain valid");
    TownCameraFrame frame;
    frame.right = { 1, 0, 0 }; frame.up = { 0, 1, 0 }; frame.forward = { 0, 0, 1 };
    frame.focalPixels = 100; frame.centerX = frame.centerY = 100;
    frame.nearClip = 1; frame.farClip = 10;
    frame.width = frame.height = 200; frame.valid = true;
    const std::array<TownCameraPoint, 6> outside { TownCameraPoint { 0, 0, 0.5F },
        { 0, 0, 12 }, { -3, 0, 2 }, { 3, 0, 2 }, { 0, 3, 2 }, { 0, -3, 2 } };
    for (bool perspective : { false, true }) {
        frame.perspective = perspective;
        for (auto point : outside) {
            std::array<TownCameraVertex, 3> triangle { TownCameraVertex { point, 0, 0 },
                { { -0.2F, -0.2F, 2 }, 1, 0 }, { { 0.2F, 0.2F, 3 }, 0, 1 } };
            auto white = triangle;
            for (auto &vertex : triangle) vertex.authoredColor = AffineColor(vertex.world);
            const auto colored = ClipTownCameraTriangle(frame, triangle);
            const auto legacy = ClipTownCameraTriangle(frame, white);
            Check(colored.count > 0 && colored.count == legacy.count, "every frustum plane keeps legacy triangle coverage");
            for (std::size_t t = 0; t < colored.count; ++t) {
                for (std::size_t v = 0; v < 3; ++v) {
                    const auto &vertex = colored.triangles[t][v];
                    const auto expected = AffineColor(vertex.source.world);
                    Check(SameLegacy(vertex, legacy.triangles[t][v]), "color does not alter projection world UV or raster depth");
                    for (std::size_t c = 0; c < 4; ++c)
                        Check(Close(vertex.source.authoredColor[c], expected[c]), "clipped attribute follows independently affine world reference");
                    Check(legacy.triangles[t][v].source.authoredColor == std::array<float, 4> { 1, 1, 1, 1 }, "clipped legacy white is exact");
                }
                TownCameraSample sample;
                Check(InterpolateTownCameraSample(colored.triangles[t], { 0.2F, 0.3F, 0.5F }, sample), "clipped perspective sample accepts");
                const auto expected = AffineColor(sample.world);
                for (std::size_t c = 0; c < 4; ++c)
                    Check(Close(sample.authoredColor[c], expected[c]), "sample uses the same perspective-correct attribute as world");
                TownCameraSample legacySample;
                Check(InterpolateTownCameraSample(legacy.triangles[t], { 0.2F, 0.3F, 0.5F }, legacySample)
                    && legacySample.authoredColor == std::array<float, 4> { 1, 1, 1, 1 }, "sample legacy white remains exactly neutral");
            }
        }
    }
    std::array<TownCameraProjectedVertex, 3> primary {};
    for (std::size_t i = 0; i < 3; ++i) {
        primary[i].depth = static_cast<float>(2U << i);
        primary[i].reciprocalW = 1 / primary[i].depth;
        primary[i].source.authoredColor = { 0, 0, 0, 0.5F };
        primary[i].source.authoredColor[i] = 1;
    }
    TownCameraSample sample;
    Check(InterpolateTownCameraSample(primary, { 1.0F / 3, 1.0F / 3, 1.0F / 3 }, sample)
        && Close(sample.authoredColor[0], 4.0F / 7) && Close(sample.authoredColor[1], 2.0F / 7)
        && Close(sample.authoredColor[2], 1.0F / 7) && sample.authoredColor[3] == 0.5F,
        "independent reciprocal-depth 4:2:1 color reference, constant alpha exact");
    for (float invalid : { -0.01F, 1.01F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
        for (std::size_t component = 0; component < 4; ++component) {
            std::array<TownCameraVertex, 3> triangle { TownCameraVertex { { -0.2F, 0, 2 } },
                { { 0.2F, 0, 2 } }, { { 0, 0.2F, 2 } } };
            triangle[1].authoredColor[component] = invalid;
            Check(ClipTownCameraTriangle(frame, triangle).count == 0, "invalid COLOR_0 refuses entire clipped triangle");
            auto broken = primary; broken[1].source.authoredColor[component] = invalid;
            const auto previous = sample;
            Check(!InterpolateTownCameraSample(broken, { 0.25F, 0.25F, 0.5F }, sample)
                && sample.depth == previous.depth && sample.world.x == previous.world.x
                && sample.authoredColor == previous.authoredColor, "invalid sample leaves prior output unchanged");
        }
    }
    std::cout << "{\"checks\":" << Checks << ",\"failures\":" << Failures
        << ",\"planes\":6,\"projectionModes\":2,\"syntheticGeometry\":true,\"gameplayIntegrated\":false}\n";
    return Failures == 0 ? 0 : 1;
}

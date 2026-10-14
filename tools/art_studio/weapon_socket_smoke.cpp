// Compare the actual loader/pose/socket helper with independently sampled GLB.
#include "engine/render/actor_player_binding.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char **argv)
{
    using namespace devilution;
    if (argc != 4) return 2;
    std::ifstream binary(argv[1], std::ios::binary), reference(argv[2]), settings(argv[3]);
    if (!binary || !reference || !settings) return 3;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(binary)), {});
    ActorModel model; ActorPose pose; std::string error;
    if (!LoadActorModel(bytes, model, error) || model.clips.size() != 1) return 4;
    ActorTransform local;
    if (!(settings >> local.translation.x >> local.translation.y >> local.translation.z
          >> local.rotation.x >> local.rotation.y >> local.rotation.z >> local.rotation.w
          >> local.scale.x)) return 5;
    local.scale.y = local.scale.z = local.scale.x;
    double phase, maximum = 0, endpoint = 0;
    ActorMatrix first {}, last {};
    unsigned samples = 0;
    while (reference >> phase) {
        if (!std::isfinite(phase) || phase < 0 || phase > 1) return 6;
        ActorMatrix expected {}, actual {};
        for (float &value : expected) if (!(reference >> value) || !std::isfinite(value)) return 7;
        const auto &clip = model.clips[0];
        const float time = static_cast<float>(clip.startTime + phase * (clip.endTime - clip.startTime));
        if (!EvaluateActorPose(model, 0, time, pose, error)
            || !ActorPlayerAttachment(model, pose, "mixamorig:RightHand", local, actual, error)) {
            std::fprintf(stderr, "%s\n", error.c_str()); return 8;
        }
        for (unsigned i = 0; i < 16; ++i)
            maximum = std::max(maximum, std::abs(static_cast<double>(actual[i]) - expected[i]));
        if (samples == 0) first = actual;
        last = actual;
        ++samples;
    }
    for (unsigned i = 0; i < 16; ++i)
        endpoint = std::max(endpoint, std::abs(static_cast<double>(first[i]) - last[i]));
    const bool pass = samples == 1025 && maximum < 1e-5 && endpoint < 1e-6;
    std::printf("{\"samples\":%u,\"maximumMatrixDelta\":%.10g,\"endpointMatrixDelta\":%.10g,\"pass\":%s,\"fingerGripApproved\":false,\"worldRendererConnected\":false}\n",
        samples, maximum, endpoint, pass ? "true" : "false");
    return pass ? 0 : 9;
}

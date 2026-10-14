#include "engine/render/actor_model.hpp"
#include "engine/render/actor_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 3;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    devilution::ActorModel model;
    devilution::ActorPose pose;
    std::string error;
    if (!devilution::LoadActorModel(bytes, model, error)) {
        std::fprintf(stderr, "load: %s\n", error.c_str()); return 4;
    }
    if (model.clips.size() != 1 || model.joints.size() != 52) return 5;
    const auto &clip = model.clips.front();
    float minimumY = 1e9F;
    std::vector<devilution::ActorVec3> first;
    float loopDelta = 0;
    float maximumMotion = 0;
    for (unsigned sample = 0; sample <= 1024; ++sample) {
        const float time = clip.startTime + (clip.endTime - clip.startTime) * sample / 1024;
        if (!devilution::EvaluateActorPose(model, 0, time, pose, error)) {
            std::fprintf(stderr, "pose: %s\n", error.c_str()); return 6;
        }
        if (sample == 0) first = pose.positions;
        if (pose.positions.size() != model.vertices.size()) return 7;
        for (std::size_t i = 0; i < pose.positions.size(); ++i) {
            const auto &p = pose.positions[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return 8;
            minimumY = std::min(minimumY, p.y);
            const auto &initial = first[i];
            maximumMotion = std::max(maximumMotion, std::sqrt((initial.x-p.x)*(initial.x-p.x)+(initial.y-p.y)*(initial.y-p.y)+(initial.z-p.z)*(initial.z-p.z)));
            if (sample == 1024) {
                const auto &a = first[i];
                loopDelta = std::max(loopDelta, std::sqrt((a.x-p.x)*(a.x-p.x)+(a.y-p.y)*(a.y-p.y)+(a.z-p.z)*(a.z-p.z)));
            }
        }
    }
    std::printf("{\"loader\":\"D3DACTR1\",\"samples\":1025,\"vertices\":%zu,\"joints\":%zu,\"minimumY\":%.9f,\"endpointDifference\":%.9f,\"maximumMotion\":%.9f,\"groundPass\":%s,\"gameplayIntegrated\":false}\n",
        model.vertices.size(), model.joints.size(), minimumY, loopDelta, maximumMotion, minimumY >= -0.002F ? "true" : "false");
    return minimumY >= -0.002F ? 0 : 9;
}

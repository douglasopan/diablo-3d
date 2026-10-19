#include "engine/render/actor_attachment_resource.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
using namespace devilution;
unsigned Checks = 0, Failures = 0;
std::size_t Corners = 0;
void Check(bool pass, const char *name)
{
    ++Checks;
    if (!pass) { ++Failures; std::fprintf(stderr, "FAIL %s\n", name); }
}
std::vector<char> Read(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), {} };
}
bool SameColor(ActorVec4 a, ActorVec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
bool SameMaterial(const TownModelV2Material &a, const TownModelV2Material &b)
{
    if (a.baseColor != b.baseColor || a.emissive != b.emissive || a.metallic != b.metallic
        || a.roughness != b.roughness || a.alphaCutoff != b.alphaCutoff || a.normalScale != b.normalScale
        || a.occlusionStrength != b.occlusionStrength || a.alphaMode != b.alphaMode || a.doubleSided != b.doubleSided) return false;
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        const auto &x = a.textures[i]; const auto &y = b.textures[i];
        if (x.texture != y.texture || x.texCoord != y.texCoord || x.offset != y.offset || x.scale != y.scale || x.rotation != y.rotation) return false;
    }
    return true;
}
bool LoadAndCompare(const char *path, const char *hash, ActorRigidResource &resource)
{
    const auto raw = Read(path);
    const auto bytes = std::as_bytes(std::span(raw));
    std::string error;
    TownModelV2 original;
    Check(ParseTownModelV2(bytes, hash, original, error), "actual D3DMESH2 parser");
    Check(LoadActorRigidResource(bytes, hash, resource, error), "bounded actual rigid resource loader");
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return false; }
    Check(resource.parts.size() == original.primitives.size() && resource.packageSha256 == hash
        && resource.assetId == original.assetId && resource.revision == original.revision, "all parts and exact identities");
    bool corners = true;
    for (std::size_t p = 0; p < resource.parts.size(); ++p) {
        const auto &part = resource.parts[p]; const auto &primitive = original.primitives[p];
        corners = corners && part.material == primitive.material && part.indices.size() == primitive.indexCount
            && part.vertices.size() == part.sourceVertices.size() && part.vertices.size() == part.authoredTangents.size();
        for (std::size_t c = 0; c < part.indices.size(); ++c) {
            const auto local = part.indices[c]; const auto sourceIndex = original.indices[primitive.firstIndex + c];
            const auto &a = part.vertices[local]; const auto &b = original.vertices[sourceIndex];
            corners = corners && part.sourceVertices[local] == sourceIndex
                && a.position.x == b.position[0] && a.position.y == b.position[1] && a.position.z == b.position[2]
                && a.normal.x == b.normal[0] && a.normal.y == b.normal[1] && a.normal.z == b.normal[2]
                && a.uv.x == b.uv[0] && a.uv.y == b.uv[1]
                && SameColor(a.color, { b.color[0], b.color[1], b.color[2], b.color[3] })
                && SameColor(part.authoredTangents[local], { b.tangent[0], b.tangent[1], b.tangent[2], b.tangent[3] });
            ++Corners;
        }
    }
    Check(corners, "every authored triangle corner/order position normal UV color tangent material exact");
    bool materials = resource.materials.size() == original.materials.size();
    for (std::size_t i = 0; i < resource.materials.size(); ++i) materials = materials && SameMaterial(resource.materials[i], original.materials[i]);
    Check(materials && resource.tangentMode == original.tangentMode, "PBR factors alpha UV transform and tangent provenance retained as metadata");
    const auto views = resource.PartViews();
    bool owned = views.size() == resource.parts.size();
    for (std::size_t p = 0; p < views.size(); ++p) owned = owned && views[p].vertices.data() == resource.parts[p].vertices.data()
        && views[p].indices.data() == resource.parts[p].indices.data() && views[p].material == resource.parts[p].material;
    Check(owned, "part views borrow only owned immutable buffers");
    const auto oldHash = resource.packageSha256; const auto *oldVertices = resource.parts[0].vertices.data();
    Check(!LoadActorRigidResource(bytes, std::string(64, '0'), resource, error)
        && resource.packageSha256 == oldHash && resource.parts[0].vertices.data() == oldVertices, "wrong hash refuses atomically");
    std::vector<std::byte> corrupt(bytes.begin(), bytes.end());
    constexpr std::uint32_t TooMany = 100001;
    for (unsigned i = 0; i < 4; ++i) corrupt[12 + i] = std::byte((TooMany >> (8 * i)) & 255);
    Check(!LoadActorRigidResource(corrupt, hash, resource, error)
        && resource.packageSha256 == oldHash && resource.parts[0].vertices.data() == oldVertices, "huge packet counts refuse before general parser allocation");
    return corners && materials;
}
} // namespace

int main(int argc, char **argv)
{
    using namespace devilution;
    if (argc != 6) return 2;
    ActorRigidResource sword, shield;
    if (!LoadAndCompare(argv[1], argv[2], sword) || !LoadAndCompare(argv[3], argv[4], shield)) return 3;
    Check(sword.parts.size() == 11 && sword.materials.size() == 6 && shield.parts.size() == 27 && shield.materials.size() == 4,
        "actual starter part/material counts preserve all source partitions");
    const auto raw = Read(argv[5]);
    ActorModel body; std::string error;
    const auto bodyBytes = std::span(reinterpret_cast<const std::uint8_t *>(raw.data()), raw.size());
    Check(LoadActorModel(bodyBytes, body, error), "actual body parser for frame color transport");
    auto swordViews = sword.PartViews(), shieldViews = shield.PartViews();
    std::array<ActorPlayerAttachmentInput, 2> gear;
    gear[0] = { sword.assetId, sword.revision, sword.sourceSha256, "mixamorig:RightHand", 1, {}, swordViews };
    gear[1] = { shield.assetId, shield.revision, shield.sourceSha256, "mixamorig:LeftHand", 2, {}, shieldViews };
    const auto idle = std::find_if(body.clips.begin(), body.clips.end(), [](const auto &clip) { return clip.name == "idle"; });
    Check(idle != body.clips.end(), "actual idle role resolved without historical ordinal");
    if (idle == body.clips.end()) return 4;
    const auto idleIndex = static_cast<std::size_t>(idle - body.clips.begin());
    ActorVisualInput visual;
    visual.assetId = body.assetId; visual.variant = body.variant; visual.revision = body.revision; visual.sourceSha256 = body.sourceSha256;
    visual.action = ActorVisualAction::Idle; visual.frameCount = 5; visual.ticksPerFrame = 2;
    visual.authoritativeFootpoint = { 5, 0, 7 };
    PlayerActorCapture capture; capture.levelEpoch = 12;
    Check(MakeActorVisualSnapshot(visual, capture.snapshot, error), "explicitly synthetic capture, not native admission");
    capture.renderPhase = { 0, 0, false, true };
    ActorPlayerFrame frame;
    Check(PrepareActorPlayerFrame(body, capture, 12, 0, idleIndex, {}, {}, gear, frame, error), "actual frame prepares body and both real rigid resources");
    if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 4; }
    bool color = true;
    const std::array<const ActorRigidResource *, 2> sources { &sword, &shield };
    for (std::size_t item = 0; item < sources.size(); ++item)
        for (std::size_t p = 0; p < sources[item]->parts.size(); ++p)
            for (std::size_t v = 0; v < sources[item]->parts[p].vertices.size(); ++v)
                color = color && SameColor(frame.attachments[item].parts[p].vertices[v].authoredColor, sources[item]->parts[p].vertices[v].color);
    Check(color, "every rigid authored COLOR0 survives transformed frame exactly");
    const auto previous = frame.attachments[0].parts[0].vertices[0].authoredColor;
    const auto *previousData = frame.attachments[0].parts[0].vertices.data();
    const auto originalColor = sword.parts[0].vertices[0].color;
    sword.parts[0].vertices[0].color.x = std::numeric_limits<float>::quiet_NaN();
    Check(!PrepareActorPlayerFrame(body, capture, 12, 0, idleIndex, {}, {}, gear, frame, error)
        && frame.attachments[0].parts[0].vertices.data() == previousData
        && SameColor(frame.attachments[0].parts[0].vertices[0].authoredColor, previous), "invalid color refuses the complete frame atomically");
    sword.parts[0].vertices[0].color = originalColor;
    sword.parts[0].vertices[0].color.w = -1;
    Check(!PrepareActorPlayerFrame(body, capture, 12, 0, idleIndex, {}, {}, gear, frame, error)
        && frame.attachments[0].parts[0].vertices.data() == previousData, "out of range alpha refuses atomically");
    sword.parts[0].vertices[0].color = originalColor;
    std::printf("{\"checks\":%u,\"failures\":%u,\"actualRigidTriangleCorners\":%zu,\"syntheticCapture\":true,\"rendererConnected\":false,\"pbrRendered\":false}\n", Checks, Failures, Corners);
    return Failures == 0 ? 0 : 1;
}

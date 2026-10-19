#include "engine/render/actor_attachment_resource.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace devilution {
namespace {
constexpr std::size_t MaxVertices = 100000, MaxIndices = 300000, MaxParts = 32;
std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i);
    return value;
}
std::string HashText(const std::array<std::uint8_t, 32> &bytes)
{
    constexpr char Hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        result[2 * i] = Hex[bytes[i] >> 4]; result[2 * i + 1] = Hex[bytes[i] & 15];
    }
    return result;
}
} // namespace

std::vector<ActorRigidPartInput> ActorRigidResource::PartViews() const
{
    std::vector<ActorRigidPartInput> result;
    result.reserve(parts.size());
    for (const auto &part : parts) result.push_back({ part.material, part.vertices, part.indices });
    return result;
}

bool LoadActorRigidResource(std::span<const std::byte> bytes, std::string_view expectedSha256,
    ActorRigidResource &output, std::string &error)
{
    const auto fail = [&error](const char *message) { error = message; return false; };
    // Bound allocations before the general scenery parser's larger limits.
    // ParseTownModelV2 still authenticates magic/hash/full tables afterwards.
    if (bytes.size() < 32 || bytes.size() > 128U * 1024U * 1024U
        || ReadU32(bytes, 12) > MaxVertices || ReadU32(bytes, 16) > MaxIndices || ReadU32(bytes, 20) > MaxParts)
        return fail("Rigid actor packet exceeds preflight size/geometry/part budget");
    try {
        TownModelV2 source;
        if (!ParseTownModelV2(bytes, expectedSha256, source, error)) return false;
        if (source.vertices.size() > MaxVertices || source.indices.size() > MaxIndices
            || source.primitives.size() > MaxParts)
            return fail("Rigid actor resource exceeds bounded geometry/part budget");
        ActorRigidResource prepared;
        prepared.assetId = source.assetId; prepared.variant = source.variant; prepared.revision = source.revision;
        prepared.sourceSha256 = HashText(source.sourceSha256); prepared.packageSha256 = source.sha256;
        prepared.materials = std::move(source.materials); prepared.textures = std::move(source.textures);
        prepared.tangentMode = source.tangentMode;
        prepared.parts.reserve(source.primitives.size());
        std::vector<std::uint32_t> remap(source.vertices.size(), std::numeric_limits<std::uint32_t>::max());
        std::size_t totalVertices = 0;
        for (const auto &primitive : source.primitives) {
            ActorRigidResourcePart part;
            part.material = primitive.material;
            std::fill(remap.begin(), remap.end(), std::numeric_limits<std::uint32_t>::max());
            part.indices.reserve(primitive.indexCount);
            for (std::size_t corner = primitive.firstIndex; corner < std::size_t(primitive.firstIndex) + primitive.indexCount; ++corner) {
                const auto original = source.indices[corner];
                auto &local = remap[original];
                if (local == std::numeric_limits<std::uint32_t>::max()) {
                    if (totalVertices == MaxVertices) return fail("Rigid actor split vertex budget exceeded");
                    const auto &vertex = source.vertices[original];
                    local = static_cast<std::uint32_t>(part.vertices.size());
                    part.vertices.push_back({ { vertex.position[0], vertex.position[1], vertex.position[2] },
                        { vertex.normal[0], vertex.normal[1], vertex.normal[2] }, { vertex.uv[0], vertex.uv[1] },
                        { vertex.color[0], vertex.color[1], vertex.color[2], vertex.color[3] } });
                    part.sourceVertices.push_back(original);
                    part.authoredTangents.push_back({ vertex.tangent[0], vertex.tangent[1], vertex.tangent[2], vertex.tangent[3] });
                    ++totalVertices;
                }
                part.indices.push_back(local);
            }
            prepared.parts.push_back(std::move(part));
        }
        output = std::move(prepared); error.clear(); return true;
    } catch (const std::bad_alloc &) { return fail("Rigid actor resource allocation failed"); }
}
} // namespace devilution

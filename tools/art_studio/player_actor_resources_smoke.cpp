#include "engine/render/player_actor_resources.hpp"
#include "player-actor-resources-inputs.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <picosha2.h>

namespace {
using namespace devilution;
unsigned Checks = 0, Failures = 0, Refusals = 0;
std::size_t GeometryCases = 0;

void Check(bool pass, const char *name)
{
	++Checks;
	if (!pass) { ++Failures; std::fprintf(stderr, "FAIL %s\n", name); }
}

std::vector<std::byte> Read(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	const std::vector<char> chars((std::istreambuf_iterator<char>(file)), {});
	const auto bytes = std::as_bytes(std::span(chars));
	return { bytes.begin(), bytes.end() };
}

struct Digest {
	picosha2::hash256_one_by_one hash;
	void Raw(const void *data, std::size_t size) { if (size) { const auto *first = static_cast<const std::uint8_t *>(data); hash.process(first, first + size); } }
	void Add(std::uint64_t value) { Raw(&value, sizeof(value)); }
	void Add(float value) { Add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value))); }
	void Add(const std::string &value) { Add(static_cast<std::uint64_t>(value.size())); Raw(value.data(), value.size()); }
	void Add(ActorVec2 v) { Add(v.x); Add(v.y); }
	void Add(ActorVec3 v) { Add(v.x); Add(v.y); Add(v.z); }
	void Add(ActorVec4 v) { Add(v.x); Add(v.y); Add(v.z); Add(v.w); }
	template<class T> void Integers(const T &values) { Add(static_cast<std::uint64_t>(values.size())); for (auto value : values) Add(static_cast<std::uint64_t>(value)); }
	template<class T> void Floats(const T &values) { Add(static_cast<std::uint64_t>(values.size())); for (float value : values) Add(value); }
	void Evidence(const WarriorEvidence &e) { Add(static_cast<std::uint64_t>(e.review)); Add(e.receipt); }
	void Item(const WarriorItemIdentity &i) { Add(static_cast<std::uint64_t>(i.nativeItemId)); Add(i.nativeItemSymbol); Add(i.assetId); Add(i.variant); Add(i.revision); Add(i.sourceSha256); Add(i.packageSha256); }
	void Binding(const WarriorAttachmentCoverage &b) { Add(b.bindingId); Add(static_cast<std::uint64_t>(b.kind)); Add(b.resourceId); Add(b.resourceSha256); Add(b.boneName); Add(b.local.translation); Add(b.local.rotation); Add(b.local.scale); Evidence(b.calibration); Add(static_cast<std::uint64_t>(b.hand)); Item(b.item); }
	void MaterialBinding(const PlayerActorMaterialBinding &m) { Add(m.resourceId); Add(m.resourceSha256); Add(static_cast<std::uint64_t>(m.material)); Add(m.identity); Add(m.revision); Add(static_cast<std::uint64_t>(m.environment)); }
	void Parts(std::span<const ActorRigidPartInput> parts) { Add(static_cast<std::uint64_t>(parts.size())); for (const auto &p : parts) { Add(static_cast<std::uint64_t>(p.material)); Add(static_cast<std::uint64_t>(p.vertices.size())); Add(static_cast<std::uint64_t>(p.indices.size())); Add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p.vertices.data()))); Add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p.indices.data()))); } }
	void Action(const WarriorActionCoverage &a) { const auto &k = a.key; Add(static_cast<std::uint64_t>(k.environment)); Add(static_cast<std::uint64_t>(k.equippedWeapon)); Add(static_cast<std::uint64_t>(k.armor)); Add(static_cast<std::uint64_t>(k.nativePhaseWeapon)); Add(static_cast<std::uint64_t>(k.presentationWeapon)); Add(static_cast<std::uint64_t>(k.action)); Add(static_cast<std::uint64_t>(k.spellGraphic)); Add(a.clipName); Evidence(a.pose); Evidence(a.transitions); Evidence(a.terminalHold); Evidence(a.interruptions); Evidence(a.nativeContinuation); Evidence(a.deathRetained); Evidence(a.deathDropped); Add(static_cast<std::uint64_t>(a.deathDroppedAttachmentBindings.size())); for (const auto &id : a.deathDroppedAttachmentBindings) Add(id); Add(static_cast<std::uint64_t>(a.attachmentBindings.size())); for (const auto &id : a.attachmentBindings) Add(id); Add(static_cast<std::uint64_t>(a.loop)); Add(a.authoredImpactPhase); Add(a.authoredEmissionPhase); }
	std::string Finish() { hash.finish(); return picosha2::get_hash_hex_string(hash); }
};

std::string Hash(std::span<const std::byte> bytes)
{
	Digest d; d.Raw(bytes.data(), bytes.size()); return d.Finish();
}

void ModelDigest(Digest &d, const ActorModel &m)
{
	d.Add(m.assetId); d.Add(m.variant); d.Add(m.revision); d.Add(m.sourceSha256);
	d.Add(static_cast<std::uint64_t>(m.nodes.size()));
	for (const auto &n : m.nodes) { d.Add(static_cast<std::uint64_t>(n.parent)); d.Add(n.name); d.Add(n.rest.translation); d.Add(n.rest.rotation); d.Add(n.rest.scale); }
	d.Add(static_cast<std::uint64_t>(m.joints.size())); for (const auto &j : m.joints) { d.Add(static_cast<std::uint64_t>(j.node)); d.Floats(j.inverseBind); }
	d.Add(static_cast<std::uint64_t>(m.vertices.size())); for (const auto &v : m.vertices) { d.Add(v.position); d.Add(v.normal); d.Add(v.uv); d.Integers(v.joints); d.Floats(v.weights); }
	d.Integers(m.indices); d.Add(static_cast<std::uint64_t>(m.clips.size()));
	for (const auto &c : m.clips) { d.Add(c.name); d.Add(c.startTime); d.Add(c.endTime); d.Add(static_cast<std::uint64_t>(c.tracks.size()));
		for (const auto &t : c.tracks) { d.Add(static_cast<std::uint64_t>(t.node)); d.Add(static_cast<std::uint64_t>(t.path)); d.Add(static_cast<std::uint64_t>(t.interpolation)); d.Floats(t.times); d.Add(static_cast<std::uint64_t>(t.values.size())); for (const auto &v : t.values) d.Floats(v); } }
	d.Add(static_cast<std::uint64_t>(m.textureWidth)); d.Add(static_cast<std::uint64_t>(m.textureHeight)); d.Add(static_cast<std::uint64_t>(m.textureRgb.size())); d.Raw(m.textureRgb.data(), m.textureRgb.size());
}

void RigidDigest(Digest &d, const ActorRigidResource &r)
{
	d.Add(r.assetId); d.Add(r.variant); d.Add(r.revision); d.Add(r.sourceSha256); d.Add(r.packageSha256); d.Add(static_cast<std::uint64_t>(r.tangentMode)); d.Add(static_cast<std::uint64_t>(r.parts.size()));
	for (const auto &p : r.parts) { d.Add(static_cast<std::uint64_t>(p.material)); d.Add(static_cast<std::uint64_t>(p.vertices.size()));
		for (const auto &v : p.vertices) { d.Add(v.position); d.Add(v.normal); d.Add(v.uv); d.Add(v.color); } d.Integers(p.indices); d.Integers(p.sourceVertices);
		d.Add(static_cast<std::uint64_t>(p.authoredTangents.size())); for (auto tangent : p.authoredTangents) d.Add(tangent); }
	d.Add(static_cast<std::uint64_t>(r.materials.size())); for (const auto &m : r.materials) {
		d.Floats(m.baseColor); d.Floats(m.emissive); d.Add(m.metallic); d.Add(m.roughness); d.Add(m.alphaCutoff); d.Add(m.normalScale); d.Add(m.occlusionStrength); d.Add(static_cast<std::uint64_t>(m.alphaMode)); d.Add(static_cast<std::uint64_t>(m.doubleSided));
		for (const auto &t : m.textures) { d.Add(static_cast<std::uint64_t>(t.texture)); d.Add(static_cast<std::uint64_t>(t.texCoord)); d.Floats(t.offset); d.Floats(t.scale); d.Add(t.rotation); } }
	d.Add(static_cast<std::uint64_t>(r.textures.size())); for (const auto &t : r.textures) { d.Add(static_cast<std::uint64_t>(t.mime)); d.Add(static_cast<std::uint64_t>(t.width)); d.Add(static_cast<std::uint64_t>(t.height)); d.Add(static_cast<std::uint64_t>(t.wrapS)); d.Add(static_cast<std::uint64_t>(t.wrapT)); d.Add(static_cast<std::uint64_t>(t.minFilter)); d.Add(static_cast<std::uint64_t>(t.magFilter)); d.Integers(t.sha256); d.Add(static_cast<std::uint64_t>(t.encoded.size())); d.Raw(t.encoded.data(), t.encoded.size()); }
}

std::string OwnedDigest(const PlayerActorResources &resources)
{
	Digest d; if (resources.empty()) return d.Finish();
	ModelDigest(d, *resources.Body()); RigidDigest(d, *resources.Sword()); RigidDigest(d, *resources.Buckler());
	d.Add(resources.generation()); d.Add(static_cast<std::uint64_t>(resources.ClipLedger().size()));
	for (const auto &r : resources.ClipLedger()) { d.Add(r.role); d.Add(static_cast<std::uint64_t>(r.bundleIndex)); }
	d.Add(static_cast<std::uint64_t>(resources.AttachmentBindings().size())); for (const auto &b : resources.AttachmentBindings()) d.Binding(b);
	const auto &p = *resources.PresentationView(); d.Add(p.bodyResourceId); d.Add(p.loadedPackageSha256); d.Add(p.authenticatedPilot.domainId); d.Add(p.authenticatedPilot.manifestSha256); d.Add(p.authenticatedPilot.authorizationSha256); d.Add(p.loaderGeneration);
	d.Add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p.model))); d.Add(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p.manifest)));
	d.Add(static_cast<std::uint64_t>(p.attachments.size())); for (const auto &a : p.attachments) { d.Add(a.verified.resourceId); d.Add(a.verified.resourceSha256); d.Item(a.verified.item); d.Parts(a.parts); }
	d.Add(static_cast<std::uint64_t>(resources.DiagnosticAttachments().size())); for (const auto &a : resources.DiagnosticAttachments()) { d.Add(a.assetId); d.Add(a.revision); d.Add(a.sourceSha256); d.Add(a.bone); d.Add(static_cast<std::uint64_t>(a.nativeItemId)); d.Add(a.local.translation); d.Add(a.local.rotation); d.Add(a.local.scale); d.Parts(a.parts); }
	d.Add(static_cast<std::uint64_t>(p.materials.size())); for (const auto &m : p.materials) d.MaterialBinding(m);
	d.Add(static_cast<std::uint64_t>(p.manifest != nullptr));
	if (p.manifest) { const auto &m = *p.manifest; d.Add(m.assetId); d.Add(m.variant); d.Add(m.revision); d.Add(m.sourceSha256); d.Add(m.packageSha256); d.Evidence(m.package); d.Evidence(m.rig); d.Evidence(m.fit); d.Evidence(m.materials); d.Evidence(m.directions); d.Evidence(m.closedPilotDomain); d.Add(m.closedPilot.domainId); d.Add(m.closedPilot.manifestSha256); d.Add(m.closedPilot.authorizationSha256); d.Add(static_cast<std::uint64_t>(m.attachments.size())); for (const auto &b : m.attachments) d.Binding(b); d.Add(static_cast<std::uint64_t>(m.actions.size())); for (const auto &a : m.actions) d.Action(a); }
	return d.Finish();
}

bool ViewsOwnAllParts(const PlayerActorResources &resources)
{
	const auto *presentation = resources.PresentationView(); const auto inputs = resources.DiagnosticAttachments();
	if (!presentation || presentation->model != resources.Body() || presentation->attachments.size() != 2 || inputs.size() != 2) return false;
	const std::array<const ActorRigidResource *, 2> rigid { resources.Sword(), resources.Buckler() };
	for (std::size_t r = 0; r < 2; ++r) {
		const auto &view = presentation->attachments[r]; const auto &binding = resources.AttachmentBindings()[r];
		if (!rigid[r] || view.parts.size() != rigid[r]->parts.size() || inputs[r].parts.size() != view.parts.size() || inputs[r].parts.data() != view.parts.data()
		    || view.verified.resourceId != binding.resourceId || view.verified.resourceSha256 != binding.resourceSha256 || view.verified.item != binding.item
		    || inputs[r].assetId != binding.item.assetId || inputs[r].revision != binding.item.revision || inputs[r].sourceSha256 != binding.item.sourceSha256
		    || inputs[r].bone != binding.boneName || inputs[r].nativeItemId != binding.item.nativeItemId) return false;
		const auto &local = inputs[r].local; const auto &expected = binding.local;
		for (const auto pair : { std::array<float,2>{local.translation.x,expected.translation.x}, {local.translation.y,expected.translation.y}, {local.translation.z,expected.translation.z},
		    {local.rotation.x,expected.rotation.x}, {local.rotation.y,expected.rotation.y}, {local.rotation.z,expected.rotation.z}, {local.rotation.w,expected.rotation.w},
		    {local.scale.x,expected.scale.x}, {local.scale.y,expected.scale.y}, {local.scale.z,expected.scale.z} })
			if (std::bit_cast<std::uint32_t>(pair[0]) != std::bit_cast<std::uint32_t>(pair[1])) return false;
		for (std::size_t p = 0; p < view.parts.size(); ++p) { const auto &owned = rigid[r]->parts[p]; const auto &part = view.parts[p];
			if (part.material != owned.material || part.vertices.data() != owned.vertices.data() || part.indices.data() != owned.indices.data()
			    || part.vertices.size() != owned.vertices.size() || part.indices.size() != owned.indices.size()) return false; }
	}
	return true;
}

void Refuse(const PlayerActorResourceLoadInput &input, PlayerActorResources &output, const char *name)
{
	const auto *body = output.Body(), *oldBody = body;
	const auto *oldView = output.PresentationView(); const auto *oldSword = output.Sword(); const auto *oldBuckler = output.Buckler();
	const auto digest = OwnedDigest(output); std::string error;
	const bool result = LoadPlayerActorResourcesDiagnostic(input, output, error);
	Check(!result && !error.empty() && output.Body() == oldBody && output.PresentationView() == oldView && output.Sword() == oldSword
	    && output.Buckler() == oldBuckler && OwnedDigest(output) == digest && ViewsOwnAllParts(output), name);
	++Refusals;
}

bool Same(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
bool Same(ActorVec2 a, ActorVec2 b) { return Same(a.x, b.x) && Same(a.y, b.y); }
bool Same(ActorVec3 a, ActorVec3 b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z); }
bool Same(ActorVec4 a, ActorVec4 b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z) && Same(a.w, b.w); }
bool Same(ActorMatrix a, ActorMatrix b) { for (std::size_t i = 0; i < a.size(); ++i) if (!Same(a[i], b[i])) return false; return true; }
bool SameVertices(const std::vector<ActorPlayerGeometryVertex> &a, const std::vector<ActorPlayerGeometryVertex> &b)
{
	if (a.size() != b.size()) return false;
	for (std::size_t i = 0; i < a.size(); ++i) if (!Same(a[i].worldPosition, b[i].worldPosition) || !Same(a[i].worldNormal, b[i].worldNormal) || !Same(a[i].uv, b[i].uv) || !Same(a[i].authoredColor, b[i].authoredColor)) return false;
	return true;
}
bool SameFrame(const ActorPlayerFrame &a, const ActorPlayerFrame &b)
{
	const auto &x = a.body; const auto &y = b.body;
	if (x.assetId != y.assetId || x.variant != y.variant || x.revision != y.revision || x.sourceSha256 != y.sourceSha256
	    || x.levelEpoch != y.levelEpoch || x.nativeIndex != y.nativeIndex || x.clip != y.clip || !Same(x.sampleSeconds, y.sampleSeconds)
	    || x.indices != y.indices || !SameVertices(x.vertices, y.vertices) || x.authoredWorldNodes.size() != y.authoredWorldNodes.size()
	    || a.attachments.size() != b.attachments.size()) return false;
	for (std::size_t i = 0; i < x.authoredWorldNodes.size(); ++i) if (!Same(x.authoredWorldNodes[i], y.authoredWorldNodes[i])) return false;
	for (std::size_t r = 0; r < a.attachments.size(); ++r) { const auto &p = a.attachments[r]; const auto &q = b.attachments[r];
		if (p.assetId != q.assetId || p.revision != q.revision || p.sourceSha256 != q.sourceSha256 || p.bone != q.bone
		    || p.nativeItemId != q.nativeItemId || !Same(p.worldMatrix, q.worldMatrix) || p.parts.size() != q.parts.size()) return false;
		for (std::size_t i = 0; i < p.parts.size(); ++i) if (p.parts[i].material != q.parts[i].material || p.parts[i].indices != q.parts[i].indices || !SameVertices(p.parts[i].vertices, q.parts[i].vertices)) return false;
	}
	return true;
}

std::uint32_t U32(std::span<const std::byte> bytes, std::size_t offset)
{
	std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(std::to_integer<std::uint8_t>(bytes[offset + i])) << (8 * i); return value;
}

void WriteU32(std::vector<std::byte> &bytes, std::size_t offset, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = std::byte((value >> (8 * i)) & 255);
}

std::size_t SkipText(std::span<const std::byte> bytes, std::size_t offset)
{
	const auto size = std::to_integer<std::uint8_t>(bytes[offset]) | (std::uint16_t(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8); return offset + 2 + size;
}

// Clearly synthetic, structurally valid count mutation, with recomputed expected
// package hash. This tests aggregate limits, never qualifies source provenance.
std::vector<std::byte> BodyAtVertexBudget(std::span<const std::byte> bytes)
{
	std::size_t offset = 40; for (unsigned i = 0; i < 4; ++i) offset = SkipText(bytes, offset);
	for (std::uint32_t i = 0; i < U32(bytes, 12); ++i) offset = SkipText(bytes, offset + 4) + 40;
	offset += U32(bytes, 16) * 68;
	const auto originalVertices = U32(bytes, 20); const auto after = offset + originalVertices * 56;
	std::vector<std::byte> result(bytes.begin(), bytes.begin() + after);
	for (std::uint32_t i = originalVertices; i < 100000; ++i) result.insert(result.end(), bytes.begin() + offset, bytes.begin() + offset + 56);
	result.insert(result.end(), bytes.begin() + after, bytes.end()); WriteU32(result, 20, 100000); return result;
}

void DirectGeometryGate(const PlayerActorResources &resources)
{
	const auto &model = *resources.Body(); std::string error; bool finite = true, floor = true, colors = true, terminalFixed = true;
	ActorPlayerFrame terminal;
	for (unsigned which = 0; which < 2; ++which) for (std::uint8_t direction = 0; direction < 8; ++direction) {
		ActorVisualInput visual; visual.assetId = model.assetId; visual.variant = model.variant; visual.revision = model.revision; visual.sourceSha256 = model.sourceSha256;
		// Synthetic geometry fixture: Idle for idle, Death+terminalHold for final
		// corpse. This does not execute native Player, lifecycle or admission.
		visual.action = which ? ActorVisualAction::Death : ActorVisualAction::Idle; visual.frameCount = 5; visual.ticksPerFrame = 2;
		visual.logicalFrame = which ? 4 : 0; visual.displayedFrame = visual.logicalFrame; visual.terminalHold = which != 0; visual.nativeDirection = direction;
		PlayerActorCapture capture; capture.levelEpoch = 12;
		if (!MakeActorVisualSnapshot(visual, capture.snapshot, error)) { Check(false, "synthetic geometry capture"); return; }
		capture.renderPhase = { visual.displayedFrame, 0, false, true };
		ActorPlayerFrame frame;
		if (!PrepareActorPlayerFrame(model, capture, 12, 0, which ? 0 : 5, {}, {}, resources.DiagnosticAttachments(), frame, error)) { std::fprintf(stderr, "%s\n", error.c_str()); Check(false, "direct actual frame preparation"); return; }
		++GeometryCases;
		for (const auto &v : frame.body.vertices) { finite = finite && std::isfinite(v.worldPosition.x) && std::isfinite(v.worldPosition.y) && std::isfinite(v.worldPosition.z); floor = floor && v.worldPosition.y >= -.002F; }
		const std::array<const ActorRigidResource *, 2> rigid { resources.Sword(), resources.Buckler() };
		for (std::size_t r = 0; r < 2; ++r) for (std::size_t p = 0; p < frame.attachments[r].parts.size(); ++p) for (std::size_t v = 0; v < frame.attachments[r].parts[p].vertices.size(); ++v) {
			const auto &actual = frame.attachments[r].parts[p].vertices[v]; const auto expected = rigid[r]->parts[p].vertices[v].color;
			finite = finite && std::isfinite(actual.worldPosition.x) && std::isfinite(actual.worldPosition.y) && std::isfinite(actual.worldPosition.z); floor = floor && actual.worldPosition.y >= -.002F;
			colors = colors && actual.authoredColor.x == expected.x && actual.authoredColor.y == expected.y && actual.authoredColor.z == expected.z && actual.authoredColor.w == expected.w;
		}
		if (which && direction == 1) { terminal = frame;
			for (unsigned repeat = 0; repeat < 65; ++repeat) { ActorPlayerFrame repeated;
				if (!PrepareActorPlayerFrame(model, capture, 12, 0, 0, {}, {}, resources.DiagnosticAttachments(), repeated, error)) terminalFixed = false;
				else if (repeated.body.sampleSeconds != 2 || !SameFrame(repeated, terminal)) terminalFixed = false;
			}
		}
		if (which) { ActorPlayerFrame bodyOnly; if (!PrepareActorPlayerFrame(model, capture, 12, 0, 0, {}, {}, {}, bodyOnly, error) || !bodyOnly.attachments.empty() || bodyOnly.body.vertices.size() != frame.body.vertices.size()) finite = false; }
	}
	Check(finite, "direct actual frame outputs, eight directions, retained and body-only synthetic terminal contexts");
	Check(floor, "BODY and both real calibrated starter resources above asset-space floor tolerance");
	Check(colors, "all authored rigid COLOR0 remains separate and exact in direct frame");
	Check(terminalFixed, "fixed terminal input repeated65 has bitwise exact complete BODY and real gear frame, no varying-time claim");
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	auto bodyBytes = Read(argv[1]), swordBytes = Read(argv[2]), bucklerBytes = Read(argv[3]);
	auto bindings = player_actor_resources_fixture::Bindings(); auto ledger = player_actor_resources_fixture::Ledger();
	PlayerActorResourceLoadInput input { { bodyBytes, player_actor_resources_fixture::Body() }, { swordBytes, player_actor_resources_fixture::Sword() },
		{ bucklerBytes, player_actor_resources_fixture::Buckler() }, ledger, bindings, 1, nullptr, {} };
	PlayerActorResources resources; std::string error;
	Check(LoadPlayerActorResourcesDiagnostic(input, resources, error), "actual three packet SHA256/parser/ledger/item/socket load");
	if (resources.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
	Check(resources.Body()->joints.size() == 52 && resources.Body()->clips.size() == 11 && resources.Sword()->parts.size() == 11 && resources.Buckler()->parts.size() == 27, "actual composed BODY52/11 and all sword/buckler parts");
	const auto *view = resources.PresentationView();
	Check(view->manifest == nullptr && view->materials.empty() && view->authenticatedPilot == WarriorPilotAuthorization {} && resources.diagnosticOnly(), "no generated manifest material review or pilot authorization");
	Check(ViewsOwnAllParts(resources), "persistent nested views reference owned heap buffers");
	Check(view->loadedPackageSha256 == input.body.expected.packageSha256 && resources.Body()->sourceSha256 == input.body.expected.sourceSha256
	    && view->loadedPackageSha256 != resources.Body()->sourceSha256 && view->attachments[0].verified.item.nativeItemId == 1 && view->attachments[1].verified.item.nativeItemId == 2, "package/source and real native item identities separated");
	DirectGeometryGate(resources);
	auto bad = input; bad.loaderGeneration = 0; Refuse(bad, resources, "generation0 atomic refusal");
	bad = input; Refuse(bad, resources, "reused generation atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.body.expected.packageSha256 = std::string(64, '0'); Refuse(bad, resources, "wrong body package SHA256 atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.sword.expected.packageSha256 = std::string(64, '0'); Refuse(bad, resources, "wrong sword package SHA256 atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.buckler.expected.packageSha256 = std::string(64, '0'); Refuse(bad, resources, "wrong buckler package SHA256 atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.body.expected.sourceSha256 = bad.body.expected.packageSha256; Refuse(bad, resources, "source/package interchange atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.body.expected.sourceSha256 = std::string(64, '0'); Refuse(bad, resources, "wrong body source identity atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.sword.expected.revision += "-wrong"; Refuse(bad, resources, "wrong actual gear revision atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.buckler.expected.resourceId = bad.sword.expected.resourceId; Refuse(bad, resources, "ambiguous resource identity atomic refusal");
	auto shortBody = bodyBytes; shortBody.pop_back(); bad = input; bad.loaderGeneration = 2; bad.body.bytes = shortBody; bad.body.expected.packageSha256 = Hash(shortBody); Refuse(bad, resources, "actual BODY parser truncation with recomputed package hash atomic refusal");
	auto shortSword = swordBytes; shortSword.pop_back(); bad = input; bad.loaderGeneration = 2; bad.sword.bytes = shortSword; bad.sword.expected.packageSha256 = Hash(shortSword); Refuse(bad, resources, "actual static parser truncation with recomputed package hash atomic refusal");
	auto badLedger = ledger; badLedger[0].bundleIndex = 8; bad = input; bad.loaderGeneration = 2; bad.ledger = badLedger; Refuse(bad, resources, "historical death index8 atomic refusal");
	badLedger = ledger; badLedger[2].role = "idle"; bad = input; bad.loaderGeneration = 2; bad.ledger = badLedger; Refuse(bad, resources, "duplicate/wrong role ledger atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.ledger = std::span(ledger).first(10); Refuse(bad, resources, "missing eleventh clip ledger atomic refusal");
	auto badBindings = bindings; badBindings[0].item.nativeItemId = 3; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "wrong native sword item identity atomic refusal");
	badBindings = bindings; badBindings[1].item.nativeItemSymbol = "IDI_WARRIOR"; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "wrong shield native symbol atomic refusal");
	badBindings = bindings; badBindings[0].boneName = "RightHand"; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "non-mixamorig bone atomic refusal");
	badBindings = bindings; badBindings[0].local.translation.x = std::nextafter(badBindings[0].local.translation.x, 1.F); bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "one ULP real sword socket mismatch atomic refusal");
	badBindings = bindings; badBindings[1].local.scale.x += .001F; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "wrong/nonuniform buckler socket atomic refusal");
	badBindings = bindings; badBindings[0].hand = WarriorSocketHand::Left; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "wrong socket hand atomic refusal");
	badBindings = bindings; badBindings[1].bindingId = badBindings[0].bindingId; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "ambiguous binding ids atomic refusal");
	badBindings = bindings; badBindings[0].calibration = { WarriorReview::PilotApproved, "synthetic-unapproved-negative" }; bad = input; bad.loaderGeneration = 2; bad.bindings = badBindings; Refuse(bad, resources, "calibration approval escalation atomic refusal");
	bad = input; bad.loaderGeneration = 2; bad.bindings = std::span(bindings).first(1); Refuse(bad, resources, "missing actual shield binding atomic refusal");
	auto syntheticLarge = BodyAtVertexBudget(bodyBytes); ActorModel control; std::string controlError;
	Check(LoadActorModel({ reinterpret_cast<const std::uint8_t *>(syntheticLarge.data()), syntheticLarge.size() }, control, controlError) && control.vertices.size() == 100000, "synthetic aggregate-budget control passes actual BODY parser, no provenance claim");
	bad = input; bad.loaderGeneration = 2; bad.body.bytes = syntheticLarge; bad.body.expected.packageSha256 = Hash(syntheticLarge); Refuse(bad, resources, "synthetic BODY100000 plus real gear exceeds aggregate budget atomically");
	WarriorCoverageManifest manifest; manifest.assetId = input.body.expected.assetId; manifest.variant = input.body.expected.variant; manifest.revision = input.body.expected.revision;
	manifest.sourceSha256 = input.body.expected.sourceSha256; manifest.packageSha256 = input.body.expected.packageSha256; manifest.attachments.assign(bindings.begin(), bindings.end());
	bad = input; bad.loaderGeneration = 2; manifest.rig = { WarriorReview::GameplayApproved, "synthetic-unapproved-negative" }; bad.diagnosticManifest = &manifest; Refuse(bad, resources, "manifest art/rig approval escalation atomic refusal");
	manifest.rig = {}; manifest.closedPilot.domainId = "synthetic-unapproved-negative"; Refuse(bad, resources, "pilot credentials rejected rather than generated/copied");
	manifest.closedPilot = {}; manifest.packageSha256 = manifest.sourceSha256; Refuse(bad, resources, "manifest package/source confusion atomic refusal");
	manifest.packageSha256 = input.body.expected.packageSha256;
	PlayerActorMaterialBinding material { input.body.expected.resourceId, input.body.expected.packageSha256, 0, 17, 3, WarriorEnvironment::Town };
	std::array<PlayerActorMaterialBinding, 2> materialDup { material, material }; bad = input; bad.loaderGeneration = 2; bad.materials = materialDup; Refuse(bad, resources, "ambiguous optional material references atomic refusal");
	material.resourceSha256 = std::string(64, '0'); bad.materials = std::span(&material, 1); Refuse(bad, resources, "material references absent actual package atomic refusal");
	material.resourceSha256 = input.body.expected.packageSha256;
	PlayerActorResources originalLease = resources;
	const auto *oldView = resources.PresentationView(); const auto *oldVertices = oldView->attachments[0].parts[0].vertices.data(); const auto oldDigest = OwnedDigest(resources);
	PlayerActorResources moved = std::move(resources);
	Check(resources.empty() && moved.PresentationView() == oldView && ViewsOwnAllParts(moved) && OwnedDigest(moved) == oldDigest, "move preserves frozen heap object and all nested views");
	input.loaderGeneration = 2; input.diagnosticManifest = &manifest; input.materials = std::span(&material, 1);
	Check(LoadPlayerActorResourcesDiagnostic(input, moved, error), "caller unreviewed empty coverage/material reference copied, no authorization");
	Check(moved.generation() == 2 && moved.Body() != originalLease.Body() && originalLease.PresentationView() == oldView
	    && oldView->attachments[0].parts[0].vertices.data() == oldVertices && OwnedDigest(originalLease) == oldDigest && ViewsOwnAllParts(originalLease), "replacement retains old views while an owned lease survives");
	const auto *loadedManifest = moved.PresentationView()->manifest;
	Check(loadedManifest && loadedManifest != &manifest && loadedManifest->actions.empty() && loadedManifest->package.review == WarriorReview::Unreviewed
	    && moved.PresentationView()->authenticatedPilot == WarriorPilotAuthorization {} && moved.PresentationView()->materials.data() != &material, "optional caller declarations owned without fabricated proof");
	bad = input; bad.loaderGeneration = 3; manifest.rig = { WarriorReview::PilotApproved, "synthetic-unapproved-negative" }; Refuse(bad, moved, "refusal preserves complete owned optional manifest/material/views after publication"); manifest.rig = {};
	WarriorActionCoverage declaration; declaration.key = warrior_admission_detail::ClosedPilotKey(WarriorEnvironment::Town, ActorVisualAction::Idle); declaration.clipName = "idle"; declaration.loop = true;
	declaration.key.spellGraphic = ActorPlayerSpellGraphic::Fire; manifest.actions = { declaration }; Refuse(bad, moved, "malformed Idle spell key refused without inferring coverage");
	declaration.key.spellGraphic = ActorPlayerSpellGraphic::None; declaration.loop = false; manifest.actions = { declaration }; Refuse(bad, moved, "invalid action loop declaration refused without inferring timing");
	declaration.loop = true; declaration.authoredEmissionPhase = .5F; manifest.actions = { declaration }; Refuse(bad, moved, "Idle emission marker declaration refused without native proof"); manifest.actions.clear();
	bodyBytes.clear(); bodyBytes.shrink_to_fit(); swordBytes.clear(); swordBytes.shrink_to_fit(); bucklerBytes.clear(); bucklerBytes.shrink_to_fit();
	bindings[0].bindingId = "caller-mutated"; ledger[0].role = "caller-mutated"; manifest.assetId = "caller-mutated"; material.resourceId = "caller-mutated";
	Check(moved.AttachmentBindings()[0].bindingId != "caller-mutated" && moved.ClipLedger()[0].role == "death"
	    && moved.PresentationView()->manifest->assetId == "tristram.hero.warrior" && moved.PresentationView()->materials[0].resourceId != "caller-mutated" && ViewsOwnAllParts(moved), "all caller buffers and declarations may die/change after load");
	moved = {}; Check(originalLease.PresentationView() == oldView && oldView->attachments[0].parts[0].vertices.data() == oldVertices && OwnedDigest(originalLease) == oldDigest, "shared immutable ownership survives destruction/release of another handle");
	std::size_t vertices = originalLease.Body()->vertices.size(), indices = originalLease.Body()->indices.size();
	for (const auto *r : { originalLease.Sword(), originalLease.Buckler() }) for (const auto &p : r->parts) { vertices += p.vertices.size(); indices += p.indices.size(); }
	std::printf("{\"checks\":%u,\"failures\":%u,\"atomicRefusals\":%u,\"realPayloads\":3,\"bodyJoints\":52,\"bodyClips\":11,\"aggregateVertices\":%zu,\"aggregateIndices\":%zu,\"directGeometryCases\":%zu,\"fixedTerminalRepeatedCalls\":65,\"syntheticCaptureAndBudgetControls\":true,\"pilotAuthorized\":false,\"rendererConnected\":false,\"nativeSimulationExecuted\":false,\"artOrMaterialApproval\":false}\n", Checks, Failures, Refusals, vertices, indices, GeometryCases);
	return Failures == 0 ? 0 : 1;
}

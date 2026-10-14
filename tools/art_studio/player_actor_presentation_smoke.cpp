#define SDL_MAIN_HANDLED
#include "player.h"
#include "engine/render/player_actor_presentation.hpp"
#include "data/file.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace devilution {
std::uint8_t ProgressToNextGameTick = 0;
std::string PresentationSpellTable;
[[noreturn]] void app_fatal(std::string_view) { std::abort(); }
std::expected<DataFile, DataFile::Error> DataFile::load(std::string_view path) {
	if (path != "txtdata\\spells\\spelldat.tsv") return std::unexpected(Error::NotFound);
	std::ifstream input(PresentationSpellTable, std::ios::binary);
	if (!input) return std::unexpected(Error::OpenFailed);
	const std::string bytes((std::istreambuf_iterator<char>(input)), {});
	auto owned = std::make_unique<char[]>(bytes.size()); std::memcpy(owned.get(), bytes.data(), bytes.size());
	return DataFile(std::move(owned), bytes.size());
}
void DataFile::reportFatalError(Error, std::string_view) { std::abort(); }
void DataFile::reportFatalFieldError(DataFileField::Error, std::string_view, std::string_view, const DataFileField &, std::string_view) { std::abort(); }
} // namespace devilution
namespace {
using namespace devilution;
unsigned Checks = 0, Failures = 0;
void Check(bool value, const char *name) { ++Checks; if (!value) { ++Failures; std::fprintf(stderr, "FAIL %s\n", name); } }
WarriorCoverageManifest SyntheticPilot(const ActorModel &model, const char *package) {
	WarriorCoverageManifest manifest;
	manifest.assetId = model.assetId; manifest.variant = model.variant; manifest.revision = model.revision;
	manifest.sourceSha256 = model.sourceSha256; manifest.packageSha256 = package;
	const WarriorEvidence synthetic { WarriorReview::PilotApproved, "SYNTHETIC_ONLY: presentation owner control, never production approval" };
	manifest.package = manifest.rig = manifest.fit = manifest.materials = manifest.directions = manifest.closedPilotDomain = synthetic;
	manifest.closedPilot = { "SYNTHETIC_ONLY:light-SwordShield", std::string(64, 'a'), std::string(64, 'b') };
	WarriorAttachmentCoverage sword;
	sword.bindingId = "SYNTHETIC:sword"; sword.kind = WarriorAttachmentKind::Sword; sword.resourceId = "SYNTHETIC:sword-resource";
	sword.resourceSha256 = std::string(64, 'c'); sword.boneName = "mixamorig:RightHand"; sword.calibration = synthetic;
	sword.hand = WarriorSocketHand::Right;
	sword.item = { 1, "IDI_WARRIOR", "SYNTHETIC:sword-asset", "fixture", "fixture-r1", std::string(64, 'd'), sword.resourceSha256 };
	WarriorAttachmentCoverage shield = sword;
	shield.bindingId = "SYNTHETIC:shield"; shield.kind = WarriorAttachmentKind::Shield; shield.resourceId = "SYNTHETIC:shield-resource";
	shield.resourceSha256 = std::string(64, 'e'); shield.boneName = "mixamorig:LeftHand"; shield.hand = WarriorSocketHand::Left;
	shield.item = { 2, "IDI_WARRSHLD", "SYNTHETIC:shield-asset", "fixture", "fixture-r1", std::string(64, 'f'), shield.resourceSha256 };
	manifest.attachments = { sword, shield };
	for (auto environment : { WarriorEnvironment::Town, WarriorEnvironment::Dungeon }) {
		for (auto action : { ActorVisualAction::Idle, ActorVisualAction::Walk, ActorVisualAction::Attack, ActorVisualAction::Hit,
		    ActorVisualAction::Block, ActorVisualAction::Death, ActorVisualAction::Spell }) {
			for (unsigned spell = action == ActorVisualAction::Spell ? 1 : 0; spell <= (action == ActorVisualAction::Spell ? 3U : 0U); ++spell) {
				WarriorActionCoverage entry;
				entry.key = warrior_admission_detail::ClosedPilotKey(environment, action, static_cast<ActorPlayerSpellGraphic>(spell));
				switch (action) {
				case ActorVisualAction::Idle: entry.clipName = "idle"; break;
				case ActorVisualAction::Walk: entry.clipName = "walk"; break;
				case ActorVisualAction::Attack: entry.clipName = "attack"; break;
				case ActorVisualAction::Hit: entry.clipName = "hit"; break;
				case ActorVisualAction::Block: entry.clipName = "block-posture"; break;
				case ActorVisualAction::Death: entry.clipName = "death"; break;
				default: entry.clipName = spell == 1 ? "cast-fire" : spell == 2 ? "cast-lightning" : "cast-magic"; break;
				}
				entry.pose = entry.transitions = entry.interruptions = entry.nativeContinuation = synthetic;
				entry.attachmentBindings = { sword.bindingId, shield.bindingId };
				if (environment == WarriorEnvironment::Town && action == ActorVisualAction::Spell) entry.attachmentBindings = { shield.bindingId };
				entry.loop = action == ActorVisualAction::Idle || action == ActorVisualAction::Walk;
				if (action == ActorVisualAction::Attack) entry.authoredImpactPhase = .5F;
				if (action == ActorVisualAction::Spell) entry.authoredEmissionPhase = .68F;
				if (action == ActorVisualAction::Death) entry.terminalHold = entry.deathRetained = entry.deathDropped = synthetic;
				manifest.actions.push_back(std::move(entry));
			}
		}
	}
	return manifest;
}
void Starter(Player &player, PlayerActorLifecycle &lifecycle, PlayerActorVisualContext &visual, bool town = false) {
	player = {}; player.plractive = true; player._pClass = HeroClass::Warrior; player._pmode = PM_STAND;
	player._pHitPoints = 640; player._pStrength = player._pMagic = player._pDexterity = 50;
	player._pdir = Direction::SouthWest; player._pgfxnum = static_cast<std::uint8_t>(PlayerWeaponGraphic::SwordShield);
	player.position.tile = { 20, 30 }; player.setLevel(town ? 0 : 1);
	player._pAFNum = 9; player._pSFNum = 13; player.AnimInfo.setNewAnimation({}, 20, 2); player.AnimInfo.currentFrame = 5;
	auto &sword = player.InvBody[INVLOC_HAND_LEFT]; sword.IDidx = IDI_WARRIOR; sword._itype = ItemType::Sword;
	sword._iClass = ICLASS_WEAPON; sword._iStatFlag = true; sword._iSeed = 101;
	auto &shield = player.InvBody[INVLOC_HAND_RIGHT]; shield.IDidx = IDI_WARRSHLD; shield._itype = ItemType::Shield;
	shield._iClass = ICLASS_ARMOR; shield._iStatFlag = true; shield._iSeed = 202;
	visual = {}; visual.nativeIndex = 0; visual.world = { town ? WarriorWorldKind::Town : WarriorWorldKind::Cathedral, town ? 0U : 1U, false, false, false };
	visual.authoritativeFootpoint = { 20, 0, 30 }; visual.retainedInterpolation128 = 64; visual.localPlayer = true;
	visual.frameSerial = visual.resourceGeneration = visual.cameraRevision = visual.paletteRevision = 1; visual.selectable = true;
	lifecycle.BeginLevel(); lifecycle.Activate(player, 0); lifecycle.CompleteLevel(visual.world);
}
} // namespace

int main(int argc, char **argv) {
	using namespace devilution;
	if (argc != 4) return 2;
	PresentationSpellTable = argv[3]; LoadSpellData();
	std::ifstream file(argv[1], std::ios::binary);
	const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	ActorModel model; std::string error;
	if (!LoadActorModel(bytes, model, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
	auto manifest = SyntheticPilot(model, argv[2]);
	std::array rigidVertices { ActorRigidVertex { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 }, { .2F, .4F, .6F, .8F } },
		ActorRigidVertex { { .1F, 0, 0 }, { 0, 1, 0 }, { 1, 0 } }, ActorRigidVertex { { 0, 0, .1F }, { 0, 1, 0 }, { 0, 1 } } };
	const std::array<std::uint32_t, 3> rigidIndices { 0, 1, 2 };
	std::array parts { ActorRigidPartInput { 7, rigidVertices, rigidIndices } };
	std::array rigid { PlayerActorRigidResource { { manifest.attachments[0].resourceId, manifest.attachments[0].resourceSha256, manifest.attachments[0].item }, parts },
		PlayerActorRigidResource { { manifest.attachments[1].resourceId, manifest.attachments[1].resourceSha256, manifest.attachments[1].item }, parts } };
	std::vector<PlayerActorMaterialBinding> materials;
	for (auto environment : { WarriorEnvironment::Town, WarriorEnvironment::Dungeon }) {
		materials.push_back({ "body", argv[2], 0, static_cast<std::uint64_t>(materials.size()+1), 1, environment });
		for (const auto &resource : rigid) materials.push_back({ resource.verified.resourceId, resource.verified.resourceSha256, 7, static_cast<std::uint64_t>(materials.size()+1), 1, environment });
	}
	PlayerActorPresentationResources resources { &model, &manifest, "body", argv[2], manifest.closedPilot, 1, rigid, materials };
	Player player {}; PlayerActorLifecycle lifecycle; PlayerActorVisualContext visual; Starter(player, lifecycle, visual);
	ActorPlayerFit fit; PlayerActorPresentation owner;
	const auto scope = WarriorAdmissionScope::ClosedSwordShieldPilot;
	const auto prepare = [&] { return owner.Prepare(player, lifecycle, resources, visual, fit, scope, error) == PlayerActorPresentationResult::Prepared; };
	const auto current = [&] { return owner.Current(player, lifecycle, resources, visual, fit, scope, error); };
	Check(prepare() && current() && current()->admission.mayReplaceNativePresentation && current()->geometry.attachments.size() == 2,
	    "SYNTHETIC_PILOT: actual Player/capture/body parser produce complete owned frame");
	Check(current() && current()->geometry.attachments[0].parts[0].material == 7 && current()->materials.size() == 3
	        && current()->attachmentMaterials.size() == 2, "authored part material references remain distinct and resolved");
	Check(current() && current()->geometry.attachments[0].parts[0].vertices[0].authoredColor.x == .2F
	        && current()->geometry.attachments[0].parts[0].vertices[0].authoredColor.w == .8F,
	    "authored vertex color survives rigid frame preparation independently of material metadata");
	const auto *frozen = current(); const auto preparations = owner.preparationCount();
	bool replay = true; for (unsigned i = 0; i < 64; ++i) replay &= current() == frozen;
	Check(replay && owner.preparationCount() == preparations, "CPU replay reuses one owned frame without pose preparation or clock ticks");
	const auto savedColor = rigidVertices[0].color; const auto ownedPosition = frozen->geometry.attachments[0].parts[0].vertices[0].worldPosition;
	rigidVertices[0].color.x = .9F; rigidVertices[0].position.x = 99; ++resources.loaderGeneration;
	Check(current() == nullptr && frozen->geometry.attachments[0].parts[0].vertices[0].authoredColor.x == .2F
	        && frozen->geometry.attachments[0].parts[0].vertices[0].worldPosition.x == ownedPosition.x,
	    "owned geometry remains intact while loader replacement makes frozen payload unavailable");
	rigidVertices[0].color = savedColor; rigidVertices[0].position.x = 0; --resources.loaderGeneration;
	const auto beforeFrame = player.AnimInfo.currentFrame; const auto beforeTick = player.AnimInfo.tickCounterOfCurrentFrame;
	Check(prepare() && player.AnimInfo.currentFrame == beforeFrame && player.AnimInfo.tickCounterOfCurrentFrame == beforeTick, "presentation does not tick native animation");
	visual.paused = true; Check(prepare(), "paused actor uses explicit retained interpolation");
	ProgressToNextGameTick = 127; Check(current() != nullptr, "paused frozen frame ignores unrelated global interpolation change");
	++visual.retainedInterpolation128; Check(current() == nullptr, "changed retained phase refuses stale frozen frame"); --visual.retainedInterpolation128;
	++visual.cameraRevision; Check(current() == nullptr, "camera revision invalidates frozen frame"); --visual.cameraRevision;
	++visual.frameSerial; Check(current() == nullptr, "next renderer transaction cannot reuse previous frozen frame"); --visual.frameSerial;
	++visual.resourceGeneration; Check(current() == nullptr, "resource reset invalidates frozen material frame without native reset"); --visual.resourceGeneration;
	++visual.paletteRevision; Check(current() == nullptr, "palette revision invalidates frozen frame"); --visual.paletteRevision;
	++visual.translationIdentity; Check(current() == nullptr, "native TRN identity invalidates frozen frame"); --visual.translationIdentity;
	++visual.nativeLight; Check(current() == nullptr, "native light context invalidates frozen frame"); --visual.nativeLight;
	visual.selectable = false; Check(current() == nullptr, "native picking eligibility invalidates frozen frame"); visual.selectable = true;
	fit.uniformScale = 2; Check(current() == nullptr, "changed fit invalidates frozen frame"); fit.uniformScale = 1;
	++resources.loaderGeneration; Check(current() == nullptr, "loader replacement invalidates frozen frame"); --resources.loaderGeneration;
	++materials[0].revision; Check(current() == nullptr, "material revision invalidates frozen frame"); --materials[0].revision;
	player.AnimInfo.currentFrame++; Check(current() == nullptr, "native pose change invalidates frozen frame"); player.AnimInfo.currentFrame--;
	player._pHitPoints--; Check(current() == nullptr, "native combat state change invalidates frozen transaction"); player._pHitPoints++;
	player.position.tile.x++; Check(current() == nullptr, "native pick tile change invalidates frozen transaction"); player.position.tile.x--;
	player.InvBody[INVLOC_HAND_LEFT]._iSeed++;
	Check(!prepare() && current() == nullptr, "missed native equipment update refuses and hides old frame");
	lifecycle.BeginEquipmentUpdate(player, 0); lifecycle.EndEquipmentUpdate(player, 0); Check(prepare(), "completed native item identity update prepares new frame");
	player.InvBody[INVLOC_HAND_LEFT].IDidx = static_cast<_item_indexes>(99); lifecycle.BeginEquipmentUpdate(player, 0); lifecycle.EndEquipmentUpdate(player, 0);
	Check(!prepare() && current() == nullptr, "same SwordShield graphic with another native item rejects closed pilot");
	Starter(player, lifecycle, visual); Check(prepare(), "fresh native activation restores complete preparation");
	lifecycle.BeginEquipmentUpdate(player, 0); Check(!prepare() && current() == nullptr, "open equipment transaction never publishes an intermediate actor");
	lifecycle.EndEquipmentUpdate(player, 0); Check(prepare(), "equipment transaction completion restores a current actor");
	lifecycle.Deactivate(0); Check(!prepare() && current() == nullptr, "deactivated native slot cannot reuse prior preparation");
	lifecycle.Activate(player, 0); Check(prepare(), "new native slot activation prepares separately");
	visual.nativeIndex = 4; Check(!prepare() && current() == nullptr, "invalid native slot is refused atomically"); visual.nativeIndex = 0;
	resources.materials = std::span<const PlayerActorMaterialBinding>(materials).first(5);
	Check(!prepare() && current() == nullptr, "missing rigid material reference refuses whole frame"); resources.materials = materials;
	const auto savedMaterial = materials.back(); materials.push_back(savedMaterial); resources.materials = materials;
	Check(!prepare() && current() == nullptr, "duplicate material reference refuses whole frame"); materials.pop_back(); resources.materials = materials;
	materials.push_back({ "SYNTHETIC:absent-resource", std::string(64, 'a'), 7, 999, 1, WarriorEnvironment::Dungeon }); resources.materials = materials;
	Check(!prepare() && current() == nullptr, "material reference cannot authenticate an absent resource"); materials.pop_back(); resources.materials = materials;
	const auto savedPart = parts[0]; parts[0].material = 999;
	Check(!prepare() && current() == nullptr, "unresolved authored part material refuses before geometry preparation"); parts[0] = savedPart;
	rigidVertices[0].color.x = std::numeric_limits<float>::quiet_NaN();
	Check(!prepare() && current() == nullptr, "nonfinite authored vertex color refuses whole frame"); rigidVertices[0].color = savedColor;
	rigidVertices[0].color.w = 1.01F;
	Check(!prepare() && current() == nullptr, "out of range authored vertex alpha refuses whole frame"); rigidVertices[0].color = savedColor;
	const auto savedTexture = model.textureRgb; model.textureRgb.pop_back();
	Check(!prepare() && current() == nullptr, "incomplete body texture refuses before any frame publication"); model.textureRgb = savedTexture;
	const auto savedItem = rigid[1].verified.item; rigid[1].verified.item.revision = "wrong-loaded-item";
	Check(!prepare() && current() == nullptr, "loaded attachment item revision cannot impersonate approved binding"); rigid[1].verified.item = savedItem;
	const auto savedActions = manifest.actions; manifest.actions.pop_back();
	Check(!prepare() && current() == nullptr, "real incomplete domain cannot pass using one current approved clip"); manifest.actions = savedActions;
	const auto savedAuthorization = resources.authenticatedPilot; resources.authenticatedPilot.authorizationSha256[0] = '0';
	Check(!prepare() && current() == nullptr, "pilot approval loader identity mismatch refuses"); resources.authenticatedPilot = savedAuthorization;
	visual.hidden = true; Check(!prepare() && current() == nullptr, "localHidden does not expose geometry or suppress fallback"); visual.hidden = false;
	visual.nativeVisible = false; Check(!prepare() && current() == nullptr, "native visibility refusal does not publish geometry"); visual.nativeVisible = true;
	visual.world.levelNumber = 2; Check(!prepare() && current() == nullptr, "native world mismatch rejects before admission"); visual.world.levelNumber = 1;
	visual.world.shareware = true; Check(!prepare() && current() == nullptr, "native world flags mismatch rejects"); visual.world.shareware = false;
	Check(owner.Prepare(player, lifecycle, resources, visual, fit, WarriorAdmissionScope::DiagnosticAction, error) == PlayerActorPresentationResult::Prepared,
	    "diagnostic component preparation remains available");
	const auto *diagnostic = owner.Current(player, lifecycle, resources, visual, fit, WarriorAdmissionScope::DiagnosticAction, error);
	Check(diagnostic && !diagnostic->admission.mayReplaceNativePresentation && !current(), "DiagnosticAction never authorizes sprite suppression");
	Starter(player, lifecycle, visual, true); player._pmode = PM_SPELL; player.executedSpell = { SpellID::Firebolt, SpellType::Spell, 0, 1 };
	Check(prepare() && current() && current()->source == WarriorPresentationSource::TownCastRemap
	        && current()->geometry.attachments.size() == 1 && current()->geometry.attachments[0].nativeItemId == 2,
	    "SYNTHETIC_PILOT: real native Town spell remap attaches shield only");
	rigid[0].verified.item.revision = "unrendered-sword-not-loaded";
	Check(!prepare() && current() == nullptr, "Town remap still requires every closed pilot binding to be exactly loaded"); rigid[0].verified.item = manifest.attachments[0].item;
	Check(prepare(), "complete Town remap resources restore current shield-only actor");
	player.queuedSpell = { SpellID::Lightning, SpellType::Scroll, 2, 1 }; Check(current() != nullptr, "queued next cast never replaces executing native graphic");
	player.executedSpell.spellLevel++; Check(current() == nullptr, "executed spell metadata changes invalidate frozen current cast");
	for (bool dropped : { false, true }) {
		Starter(player, lifecycle, visual); lifecycle.BeginDeath(player, 0);
		player._pmode = PM_DEATH; player._pHitPoints = 0; player._pgfxnum = 0;
		if (dropped) for (auto &item : player.InvBody) item.clear();
		Check(lifecycle.EndDeath(player, 0), "native retained/drop fixture has authenticated lifecycle witness");
		player.AnimInfo.currentFrame = 19; player.AnimInfo.ticksPerFrame = 100; player.AnimInfo.tickCounterOfCurrentFrame = 30;
		Check(prepare() && current() && current()->capture.snapshot.terminalHold
		        && current()->geometry.attachments.size() == (dropped ? 0U : 2U)
		        && current()->geometry.body.sampleSeconds == model.clips[current()->admission.clipIndex].endTime,
		    dropped ? "SYNTHETIC_PILOT: native witnessed drop yields body-only exact terminal hold" : "SYNTHETIC_PILOT: native retained corpse keeps admitted equipment and hold");
	}
	Check(lifecycle.BeginResurrection(player, 0), "dropped corpse native resurrection begins with actual witness");
	player._pmode = PM_STAND; player._pHitPoints = 64;
	Check(lifecycle.EndResurrection(player, 0) && !prepare() && current() == nullptr,
	    "real alive baseline after drop does not impersonate closed pilot starter gear");
	Starter(player, lifecycle, visual); lifecycle.Deactivate(0);
	for (auto &item : player.InvBody) item.clear(); player._pgfxnum = 0; player._pmode = PM_DEATH; player._pHitPoints = 0; lifecycle.Activate(player, 0);
	Check(!prepare() && current() == nullptr, "loaded unknown corpse has no invented approved starter origin");
	Check(lifecycle.BeginResurrection(player, 0), "loaded corpse requires explicit native resurrection source"); player._pmode = PM_STAND; player._pHitPoints = 64;
	Check(lifecycle.EndResurrection(player, 0) && !prepare() && current() == nullptr,
	    "loaded corpse revived empty keeps native fallback eligible");
	lifecycle.BeginLevel(); Check(!prepare() && current() == nullptr, "native epoch invalidation hides prior owned corpse frame");
	std::printf("{\"checks\":%u,\"failures\":%u,\"actualPlayerCaptureParserAndPose\":true,\"pilotPositivesSynthetic\":true,\"artApprovalOrRendererHook\":false,\"nativeSimulationEventsExecuted\":false}\n", Checks, Failures);
	return Failures ? 1 : 0;
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Only data and side-effect stubs live here. The complete DoSpell body and
// current capture marker statement are supplied as exact source extractions.
// This harness does not execute the full game, native Player or real CastSpell.
namespace native_spell_marker_fixture {

enum class SpellType { Spell, Skill, Scroll, Charges };
enum class ActorVisualAction { Spell };
enum class Direction { South };
struct Point { int x = 0; int y = 0; bool operator==(const Point &) const = default; };

struct AnimationFixture {
	std::int8_t currentFrame = 0;
	std::int8_t numberOfFrames = 0;
#include "native-is-last-frame-exact.inc"
};

struct Player {
	AnimationFixture AnimInfo;
	std::int8_t _pSFNum = 0;
	Direction _pdir = Direction::South;
	struct ExecutedSpell { int spellId = 37; int spellLevel = 4; SpellType spellType = SpellType::Spell; } executedSpell;
	struct PlayerPosition { Point tile { 11, 17 }; Point temp { 19, 23 }; } position;
	unsigned casts = 0;
	int observedCastFrame = -1;
	int observedSpellId = -1;
	int observedSpellLevel = -1;
	Point observedSource;
	Point observedTarget;
	unsigned startsStand = 0;
	unsigned clearStates = 0;
	unsigned ensureReadied = 0;
};

template <class T, class... U> bool IsAnyOf(T value, U... options) { return ((value == options) || ...); }

void CastSpell(Player &player, int spellId, Point source, Point target, int spellLevel)
{
	++player.casts;
	player.observedCastFrame = player.AnimInfo.currentFrame;
	player.observedSpellId = spellId;
	player.observedSpellLevel = spellLevel;
	player.observedSource = source;
	player.observedTarget = target;
}

void StartStand(Player &player, Direction) { ++player.startsStand; }
void ClearStateVariables(Player &player) { ++player.clearStates; }
void EnsureValidReadiedSpell(Player &player) { ++player.ensureReadied; }

#include "native-dospell-exact.inc"

struct ActorVisualInput { int nativeEmissionFrame = -1; };

int CaptureEmissionMarkerFromExactStatement(const Player &player)
{
	const auto action = ActorVisualAction::Spell;
	ActorVisualInput input;
#include "native-capture-spell-marker-exact.inc"
	return input.nativeEmissionFrame;
}

} // namespace native_spell_marker_fixture

int main(int argc, char **argv)
{
	using namespace native_spell_marker_fixture;
	// Default preserves the frozen R1 baseline. R2 expects the current exact
	// capture statement to agree with the observed native call (offset zero).
	int expectedCaptureOffset = -1;
	if (argc != 1) {
		if (argc != 3 || std::strcmp(argv[1], "--expected-capture-offset") != 0) return 3;
		if (std::strcmp(argv[2], "0") == 0) expectedCaptureOffset = 0;
		else if (std::strcmp(argv[2], "-1") != 0) return 3;
	}
	unsigned cases = 0, frameCalls = 0, currentCaptureMismatches = 0;
	struct Result { int frames; int marker; int type; int observed; int captured; };
	std::vector<Result> results;
	for (const int frames : { 8, 16, 20, 24 }) {
		std::vector<int> markers { 1, frames / 2, frames - 2 };
		if (frames == 20) markers.push_back(14);
		for (const int marker : markers) {
			for (const auto type : { SpellType::Spell, SpellType::Skill, SpellType::Scroll, SpellType::Charges }) {
				Player player;
				player.AnimInfo.numberOfFrames = static_cast<std::int8_t>(frames);
				player._pSFNum = static_cast<std::int8_t>(marker);
				player.executedSpell.spellType = type;
				for (int frame = 0; frame < frames; ++frame) {
					player.AnimInfo.currentFrame = static_cast<std::int8_t>(frame);
					const bool completed = DoSpell(player);
					++frameCalls;
					// Observe actual calls made by the extracted native body. No
					// harness wrapper decides whether to call the CastSpell stub.
					if (completed != (frame == frames - 1) || player.casts != (frame >= marker ? 1U : 0U)) {
						std::fprintf(stderr, "Unexpected exact DoSpell behavior: frames=%d marker=%d frame=%d\n", frames, marker, frame);
						return 1;
					}
				}
				const int captured = CaptureEmissionMarkerFromExactStatement(player);
				if (player.casts != 1 || player.observedCastFrame != marker || captured != player.observedCastFrame + expectedCaptureOffset
				    || player.startsStand != 1 || player.clearStates != 1
				    || player.ensureReadied != (type == SpellType::Scroll || type == SpellType::Charges ? 1U : 0U)
				    || player.observedSpellId != player.executedSpell.spellId || player.observedSpellLevel != player.executedSpell.spellLevel
				    || player.observedSource != player.position.tile || player.observedTarget != player.position.temp) {
					std::fprintf(stderr, "Expected captured marker offset from observed native emission was not observed\n");
					return 2;
				}
				++cases;
				if (captured != player.observedCastFrame) ++currentCaptureMismatches;
				results.push_back({ frames, marker, static_cast<int>(type), player.observedCastFrame, captured });
			}
		}
	}
	std::printf("{\"cases\":%u,\"exactDoSpellCalls\":%u,\"expectedCaptureOffset\":%d,\"currentCaptureMarkerMismatches\":%u,\"rows\":[", cases, frameCalls, expectedCaptureOffset, currentCaptureMismatches);
	for (std::size_t i = 0; i < results.size(); ++i) {
		const auto &result = results[i];
		std::printf("%s{\"frameCount\":%d,\"nativeSFNum\":%d,\"fixtureSpellType\":%d,\"observedEmissionFrame0Based\":%d,\"currentCapturedMarker0Based\":%d}",
		    i ? "," : "", result.frames, result.marker, result.type, result.observed, result.captured);
	}
	std::printf("],\"exactNativeBodyExtracted\":true,\"captureStatementExtracted\":true,\"fixtureDataAndSideEffectStubs\":true,\"provesCurrentCaptureOneFrameEarly\":%s,\"correctedCaptureMatchesNativeEmission\":%s,\"realSpellEmitted\":false,\"realPlayerCaptureExecuted\":false,\"fullApplicationExecuted\":false,\"sourceModified\":false}\n",
	    expectedCaptureOffset == -1 && currentCaptureMismatches == cases ? "true" : "false",
	    expectedCaptureOffset == 0 && currentCaptureMismatches == 0 ? "true" : "false");
	return 0;
}

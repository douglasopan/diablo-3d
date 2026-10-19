#define SDL_MAIN_HANDLED
#include "player.h"
#include "engine/render/player_actor_capture.hpp"
#include "data/file.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace devilution {
uint8_t ProgressToNextGameTick = 0;
std::string CastSelectionTablePath;
[[noreturn]] void app_fatal(std::string_view message)
{
    std::fprintf(stderr, "fatal: %.*s\n", static_cast<int>(message.size()), message.data());
    std::abort();
}
// Fixture-only asset transport. Parsing, spell metadata and native graphic
// selector bodies come from the native sources, without starting the engine.
std::expected<DataFile, DataFile::Error> DataFile::load(std::string_view path)
{
    if (path != "txtdata\\spells\\spelldat.tsv") return std::unexpected(Error::NotFound);
    std::ifstream input(CastSelectionTablePath, std::ios::binary);
    if (!input) return std::unexpected(Error::OpenFailed);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    auto owned = std::make_unique<char[]>(bytes.size());
    std::memcpy(owned.get(), bytes.data(), bytes.size());
    return DataFile(std::move(owned), bytes.size());
}
void DataFile::reportFatalError(Error, std::string_view) { std::abort(); }
void DataFile::reportFatalFieldError(DataFileField::Error, std::string_view, std::string_view,
    const DataFileField &, std::string_view) { std::abort(); }
}

int main(int argc, char **argv)
{
    using namespace devilution;
    if (argc != 3) return 2;
    CastSelectionTablePath = argv[2];
    LoadSpellData();
    std::ifstream asset(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(asset)), {});
    ActorModel model;
    std::string error;
    if (!LoadActorModel(bytes, model, error)) return 3;
    Player player {};
    player._pClass = HeroClass::Warrior;
    player._pmode = PM_SPELL;
    player._pdir = Direction::South;
    player._pSFNum = 14;
    player._pgfxnum = static_cast<uint8_t>(PlayerWeaponGraphic::Sword);
    player.AnimInfo.setNewAnimation({}, 20, 1);
    player._pRSpell = SpellID::Lightning;
    PlayerActorCaptureContext context;
    context.levelEpoch = 24;
    context.retainedInterpolation128 = 64;
    PlayerActorCapture output;
    std::ifstream table(argv[2]);
    std::string line;
    std::getline(table, line);
    unsigned rows = 0, checks = 0;
    unsigned familyCounts[3] {};
    while (std::getline(table, line)) {
        if (line.empty()) continue;
        std::istringstream record(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(record, field, '\t')) fields.push_back(field);
        if (fields.size() < 7) return 4;
        const auto parsedId = ParseSpellId(fields[0]);
        if (!parsedId || static_cast<unsigned>(*parsedId) != rows + 1) return 5;
        // Independent expectations derive from the authored TSV family field,
        // rather than the production selector under test or spell name.
        ActorPlayerSpellGraphic expected;
        player_graphic expectedNative;
        unsigned family;
        if (fields[6].starts_with("Fire")) {
            expected = ActorPlayerSpellGraphic::Fire; expectedNative = player_graphic::Fire; family = 0;
        } else if (fields[6].starts_with("Lightning")) {
            expected = ActorPlayerSpellGraphic::Lightning; expectedNative = player_graphic::Lightning; family = 1;
        } else if (fields[6].starts_with("Magic")) {
            expected = ActorPlayerSpellGraphic::Magic; expectedNative = player_graphic::Magic; family = 2;
        } else return 6;
        player.executedSpell = { *parsedId, SpellType::Spell, 0, 3 };
        // Deliberately queue a different family and an unrelated ready spell.
        player.queuedSpell = { family == 0 ? SpellID::Lightning : SpellID::Firebolt, SpellType::Scroll, 7, 1 };
        player._pRSpell = family == 2 ? SpellID::Firebolt : SpellID::Healing;
        const auto beforeExecuted = player.executedSpell;
        const auto beforeQueued = player.queuedSpell;
        const auto beforeFrame = player.AnimInfo.currentFrame;
        const auto beforeCounter = player.AnimInfo.tickCounterOfCurrentFrame;
        if (player.getGraphic() != expectedNative
            || !CaptureWarriorActor(player, model, context, output, error)
            || output.nativeSpellGraphic != expected
            || output.snapshot.nativeEmissionFrame != 13
            || output.snapshot.action != ActorVisualAction::Spell) return 7;
        ++checks;
        if (player.executedSpell.spellId != beforeExecuted.spellId
            || player.executedSpell.spellType != beforeExecuted.spellType
            || player.executedSpell.spellLevel != beforeExecuted.spellLevel
            || player.queuedSpell.spellId != beforeQueued.spellId
            || player.queuedSpell.spellType != beforeQueued.spellType
            || player.queuedSpell.spellFrom != beforeQueued.spellFrom
            || player.AnimInfo.currentFrame != beforeFrame
            || player.AnimInfo.tickCounterOfCurrentFrame != beforeCounter) return 8;
        ++checks;
        ++familyCounts[family];
        ++rows;
    }
    if (rows != static_cast<unsigned>(SpellID::LastDiablo) || rows + 1 != SpellsData.size()
        || familyCounts[0] == 0 || familyCounts[1] == 0 || familyCounts[2] == 0) return 9;
    // Non-spell actions must clear the previous discriminator, even if metadata
    // is not loaded or the executed value is stale/invalid.
    player._pmode = PM_STAND;
    player.executedSpell.spellId = SpellID::Invalid;
    SpellsData.clear();
    if (!CaptureWarriorActor(player, model, context, output, error)
        || output.nativeSpellGraphic != ActorPlayerSpellGraphic::None) return 10;
    ++checks;
    const auto unchanged = output;
    player._pmode = PM_SPELL;
    for (const auto invalid : { SpellID::Invalid, SpellID::Null, static_cast<SpellID>(127) }) {
        player.executedSpell.spellId = invalid;
        if (CaptureWarriorActor(player, model, context, output, error)
            || output.levelEpoch != unchanged.levelEpoch
            || output.nativeSpellGraphic != unchanged.nativeSpellGraphic
            || output.snapshot.action != unchanged.snapshot.action
            || output.snapshot.assetId != unchanged.snapshot.assetId) return 11;
        ++checks;
    }
    player.executedSpell.spellId = SpellID::Firebolt;
    if (CaptureWarriorActor(player, model, context, output, error)
        || output.nativeSpellGraphic != unchanged.nativeSpellGraphic) return 12;
    ++checks;
    LoadSpellData();
    context.paused = true;
    if (!CaptureWarriorActor(player, model, context, output, error)
        || output.nativeSpellGraphic != ActorPlayerSpellGraphic::Fire
        || output.snapshot.nativeEmissionFrame != 13) return 13;
    ++checks;
    std::printf("{\"checks\":%u,\"nativeSpellRows\":%u,\"familyCounts\":[%u,%u,%u],"
                "\"nativeTableParserUsed\":true,\"sourceExactGraphicSelectorUsed\":true,"
                "\"executedOverridesQueuedAndReady\":true,\"worldRendererConnected\":false}\n",
        checks, rows, familyCounts[0], familyCounts[1], familyCounts[2]);
}

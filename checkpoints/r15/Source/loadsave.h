/**
 * @file loadsave.h
 *
 * Interface of save game functionality.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

#include "pfile.h"
#include "player.h"
#include "utils/attributes.h"

namespace devilution {

extern DVL_API_FOR_TEST bool gbIsHellfireSaveGame;
extern DVL_API_FOR_TEST uint8_t giNumberOfLevels;

void RemoveInvalidItem(Item &pItem);
_item_indexes RemapItemIdxFromDiablo(_item_indexes i);
_item_indexes RemapItemIdxToDiablo(_item_indexes i);
_item_indexes RemapItemIdxFromSpawn(_item_indexes i);
_item_indexes RemapItemIdxToSpawn(_item_indexes i);
bool IsHeaderValid(uint32_t magicNumber);
void LoadHotkeys();
void LoadHeroItems(Player &player);
/**
 * @brief Remove invalid inventory items from the inventory grid
 * @param player The player to remove invalid items from
 */
void RemoveEmptyInventory(Player &player);

/**
 * @brief Load game state
 * @param firstflag Can be set to false if we are simply reloading the current game
 */
std::expected<void, std::string> LoadGame(bool firstflag);
void SaveHotkeys(SaveWriter &saveWriter, const Player &player);
void SaveHeroItems(SaveWriter &saveWriter, Player &player);
struct SurfaceCampaignSaveContext;
// Scoped, single-use preparation. The factory runs before archive constructors
// or demo copies; it captures actual Town presentation only at this reversible
// boundary. No proof survives token destruction/another campaign or hero.
class PreparedFullSave {
public:
    ~PreparedFullSave();
    PreparedFullSave(PreparedFullSave &&) noexcept;
    PreparedFullSave &operator=(PreparedFullSave &&) noexcept;
    PreparedFullSave(const PreparedFullSave &) = delete;
    PreparedFullSave &operator=(const PreparedFullSave &) = delete;
    const SurfaceCampaignSaveContext &Context() const;
    bool IsCurrent() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit PreparedFullSave(std::unique_ptr<Impl> impl);
    friend std::optional<PreparedFullSave> PrepareFullSave(bool);
    friend bool SaveGameData(SaveWriter &, PreparedFullSave *);
};
std::optional<PreparedFullSave> PrepareFullSave(bool surfaceArchive = false);
// False is refusal/write failure + sticky writer error; legacy success is true
// too. The same opaque context reaches Commit/Fresh without any post-write copy.
bool SaveGameData(SaveWriter &saveWriter, PreparedFullSave *prepared = nullptr);
void SaveGame();
void SaveLevel(SaveWriter &saveWriter);
std::expected<void, std::string> LoadLevel();
std::expected<void, std::string> ConvertLevels(SaveWriter &saveWriter);
void LoadStash();
void SaveStash(SaveWriter &stashWriter);

} // namespace devilution

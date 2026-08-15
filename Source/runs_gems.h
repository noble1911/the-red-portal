/**
 * @file runs_gems.h
 *
 * [RUNS MOD] Diablo II style gems, adapted to a game with no sockets.
 *
 * A gem is a one-shot consumable: use it from the inventory and the cursor turns into
 * the gem, then click a weapon, shield or armour piece to fuse it in permanently. The
 * interaction is Hellfire's oil flow (see UseItem / ApplyOilToItem); the difference is
 * that a gem's effect depends on both its type and the class of item it lands on, the
 * way a D2 socket does.
 *
 * Seven types x five qualities = 35 gems, in the D2 order:
 *   Amethyst, Topaz, Sapphire, Emerald, Ruby, Diamond, Skull
 *   Chipped, Flawed, Normal, Flawless, Perfect
 *
 * Six of the 21 gem/slot cells name a stat Diablo does not have -- there is no cold or
 * poison damage, no poison resistance, no magic find, no damage-versus-undead and no
 * mana regeneration -- so those are substituted with the nearest thing D1 can express
 * (the table in runs_gems.cpp lists each one). Every other cell keeps D2's flavour,
 * rescaled to Diablo's much smaller stat economy.
 *
 * Type and quality live in the item's create-info word, exactly like a dungeon scroll's
 * level, so gems survive save/load and network sync with no new item fields.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/clx_sprite.hpp"
#include "engine/point.hpp"

namespace devilution {

// Forward declared rather than including items.h: items.cpp needs this header, so
// including items.h here would be circular.
struct Item;
struct Player;

enum class GemType : uint8_t {
	Amethyst,
	Topaz,
	Sapphire,
	Emerald,
	Ruby,
	Diamond,
	Skull,
	LAST = Skull,
};

enum class GemQuality : uint8_t {
	Chipped,
	Flawed,
	Normal,
	Flawless,
	Perfect,
	LAST = Perfect,
};

constexpr int NumGemTypes = static_cast<int>(GemType::LAST) + 1;
constexpr int NumGemQualities = static_cast<int>(GemQuality::LAST) + 1;
constexpr int NumGems = NumGemTypes * NumGemQualities;

/** @brief Sprite-sheet slot of the first gem icon.
 *
 * Gem art lives in its own list appended after objcurs/objcurs2 rather than inside them,
 * but the base index is a fixed constant instead of "however many sprites those two
 * happened to have": objcurs2 only exists when Hellfire is active, so a derived base
 * would renumber every stored gem when the same character is played without it. Items
 * keep `_iCurs` in the save file and on the wire, so that number has to be stable.
 * Chosen well above both sheets (objcurs is ~180 item frames, objcurs2 ~60).
 */
constexpr int GemCursFirst = 256;

/** @brief The `_iCurs` value that draws this gem. */
constexpr int GemCurs(GemType type, GemQuality quality)
{
	return GemCursFirst + static_cast<int>(type) * NumGemQualities + static_cast<int>(quality);
}

/** @brief True if `curs` addresses the gem sprite list rather than objcurs/objcurs2. */
constexpr bool IsGemCurs(int curs)
{
	return curs >= GemCursFirst && curs < GemCursFirst + NumGems;
}

/** @brief Gem type, from create-info bits 0-3. */
GemType GemTypeFromCreateInfo(uint16_t createInfo);
/** @brief Gem quality, from create-info bits 4-6. */
GemQuality GemQualityFromCreateInfo(uint16_t createInfo);
/** @brief Gem type of an existing gem item. */
GemType GemTypeOf(const Item &item);
/** @brief Gem quality of an existing gem item. */
GemQuality GemQualityOf(const Item &item);
/** @brief Packs type (bits 0-3) and quality (bits 4-6) into a create-info word. */
uint16_t MakeGemCreateInfo(GemType type, GemQuality quality);

/** @brief True if the item is a gem of any type. */
bool IsGem(const Item &item);

/** @brief Builds a gem of the given type and quality (used by drops and by RecreateItem). */
void CreateGem(Item &item, GemType type, GemQuality quality);

/** @brief Rolls a gem appropriate to `depth` and drops it near `position`. */
void DropRandomGem(int depth, Point position, bool sendmsg);

/** @brief Rolls how many gems, if any, a kill at `depth` should drop. */
bool ShouldDropGem(int depth);

/** @brief True while the "pick an item to fuse this gem into" cursor is up.
 *
 * Gems follow the Scroll of Preservation's flow rather than the oils': the gem is not
 * spent when it is used, only when it actually lands on something, so aiming at the
 * wrong item or clicking away costs nothing.
 */
extern bool GemTargetingActive;

/** @brief Starts targeting with `gem`, remembering the slot to spend on success. */
void BeginGemTargeting(const Item &gem, int8_t sourceSlot);

/** @brief Stops targeting without spending anything. */
void CancelGemTargeting();

/** @brief Fuses the pending gem into the item in inventory slot `cii` and spends it.
 * @return false if this gem cannot go into that item (wrong class, or already
 * saturated), leaving the cursor up so the player can aim somewhere else.
 */
bool DoGem(Player &player, int cii);

/** @brief Display name, e.g. "Flawless Ruby". */
std::string GemName(GemType type, GemQuality quality);

/** @brief Appends one line per slot describing what this gem does, for the tooltip.
 * Returned rather than printed so the info-box plumbing stays inside items.cpp. */
void GetGemEffectLines(GemType type, GemQuality quality, std::vector<std::string> &out);


/** @brief Builds the gem sprites for the palette that is currently loaded.
 *
 * Diablo's palette changes per dungeon type and cycles indices 1-31, so the gem art
 * ships as true RGB and is quantised against whatever palette is live -- call this
 * again whenever the palette changes.
 */
void RefreshGemSprites();
void FreeGemSprites();

/** @brief Sprite for a gem `_iCurs`. Only valid for IsGemCurs(curs). */
ClxSprite GetGemSprite(int curs);

} // namespace devilution

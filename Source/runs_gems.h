/**
 * @file runs_gems.h
 *
 * [RUNS MOD] Diablo II style gems and sockets.
 *
 * A gem is a one-shot consumable: use it from the inventory and the cursor turns into
 * the gem, then click a socketed weapon, shield or armour piece to fuse it in. The
 * interaction is Hellfire's oil flow (see UseItem / ApplyOilToItem); the difference is
 * that a gem's effect depends on both its type and the class of item it lands on, the
 * way a D2 socket does. Only Griswold can take a gem back out: he destroys the gem to
 * free the socket, or breaks up the item to save the gem.
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
#include "engine/surface.hpp"

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

/** @brief Gem type, from dwBuff bits 14-16. */
GemType GemTypeFromBuff(uint32_t dwBuff);
/** @brief Gem quality, from dwBuff bits 17-19. */
GemQuality GemQualityFromBuff(uint32_t dwBuff);
/** @brief Gem type of an existing gem item. */
GemType GemTypeOf(const Item &item);
/** @brief Gem quality of an existing gem item. */
GemQuality GemQualityOf(const Item &item);
/** @brief Builds the dwBuff bits marking an item as this gem. */
uint32_t MakeGemBuff(GemType type, GemQuality quality);

/** @brief True if the item is a gem of any type. */
bool IsGem(const Item &item);

/** @brief Earned depth a hero must have beaten before this quality may be used.
 *
 * Mirrors the drop window exactly (quality q first drops at depth 5q), so the rule is
 * simply "you may use what you could have earned". Chipped has no requirement.
 */
int GemRequiredDepth(GemQuality quality);

/** @brief True if `player` has beaten deep enough to use this gem.
 *
 * Diablo has no character-level requirement on items -- CanUseItem checks strength,
 * magic and dexterity only -- so without this a gem handed over in co-op, or left in the
 * stash by a dead hero, could be used at any point. Gating on earned depth matches the
 * premium shelf's cap: what you have beaten sets what you may use, and it only moves
 * when a boss dies.
 */
bool CanUseGem(const Player &player, const Item &item);

/** @brief Builds a gem of the given type and quality (used by drops and by RecreateItem). */
void CreateGem(Item &item, GemType type, GemQuality quality);

/** @brief Rolls a gem appropriate to `depth` and drops it near `position`.
 *
 * [RUNS MOD] The run boss is the ONLY source of gems: ordinary kills used to roll for one
 * too, but a floor is ~150 kills (numplacemonsters is walkable tiles / 30, half again in
 * multiplayer) so even a low per-kill rate buried the pack, and it made a permanent
 * upgrade to a piece of gear more common than a dungeon scroll -- a consumable. Coming
 * only from the vault also makes the quality roll mean something: it is taken at the run's
 * depth, so moving up the gem tiers needs the same thing moving up depth needs.
 */
void DropRandomGem(int depth, Point position, bool sendmsg);

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

// --- Sockets ------------------------------------------------------------------------
//
// A gem only goes into an item with a socket, and an item has at most one. Any weapon,
// shield or armour piece can roll one when it is created; whether it did is CF_SOCKETED
// in dwBuff, so it rides through saves and the network with the rest of the item.
// Gear gemmed before sockets existed counts as socketed.

/** Socket chance (percent) for an ordinary drop: monsters, chests, floor items. */
constexpr int SocketChanceDropPct = 8;
/** Socket chance for a boss-vault item (and the fruit machine's item prize). */
constexpr int SocketChanceVaultPct = 20;
/** Socket chance for Griswold's stock: the item may be rubbish, the socket is not. */
constexpr int SocketChanceShopPct = 10;
/** Socket chance for any unique or relic, whatever the source: a socketed unique is a chase. */
constexpr int SocketChanceUniquePct = 4;

/** @brief True if this kind of item could have a socket (weapons, shields, armour). */
bool CanHaveSocket(const Item &item);
/** @brief True if the item has a socket, empty or filled. */
bool HasSocket(const Item &item);

/** @brief Rolls a socket onto a freshly created item at `chancePct` (uniques use
 * SocketChanceUniquePct instead), plus the run's Jeweled bonus.
 *
 * The roll is a hash of the item's seed, not the game RNG: ordinary monster loot is built
 * separately on every client in the level, and all of them have to agree -- and drawing
 * from the shared RNG would shift every roll after it.
 */
void RollSocket(Item &item, int chancePct);

/** @brief Gold Griswold charges to destroy the gem in this item, freeing the socket. */
int GemRemovalCost(const Item &item);
/** @brief Destroys the item's gem, leaving the socket empty and the item otherwise as it was. */
void RemoveFusedGem(const Player &player, Item &item);
/** @brief Builds the gem fused into `item` as a loose gem (Griswold's salvage). */
void ExtractFusedGem(const Item &item, Item &gem);

/** @brief Draws the item's socket (and its gem) over the item's icon in a grid.
 * `position` and `itemSprite` are what DrawItem was given. */
void DrawItemSocket(const Surface &out, const Item &item, Point position, ClxSprite itemSprite);

/** @brief True if a gem has been fused into this item. One gem per item. */
bool HasGem(const Item &item);
/** @brief The gem fused into this item. Only meaningful when HasGem(). */
GemType FusedGemType(const Item &item);
GemQuality FusedGemQuality(const Item &item);

/** @brief Re-applies the stored gem's bonus to a freshly rebuilt item.
 *
 * RecreateItem rebuilds an item from its seed and create-info, which restores the base
 * item but not the _iPL* fields a gem wrote. dwBuff survives that trip, so the gem is
 * re-applied from it -- otherwise a gemmed item would lose its bonus the first time it
 * crossed the wire or was repacked.
 */
void ReapplyFusedGem(Item &item);

/** @brief Appends the item's socket lines: "Socket: empty", or the gem it holds and what
 * that gem gives. Nothing for an item without a socket. */
void GetFusedGemLines(const Item &item, std::vector<std::string> &out);


/** @brief Builds the gem sprites for the palette that is currently loaded.
 *
 * Diablo's palette changes per dungeon type and cycles indices 1-31, so the gem art
 * ships as true RGB and is quantised against whatever palette is live -- call this
 * again whenever the palette changes.
 */
void RefreshGemSprites();
void FreeGemSprites();

/** @brief Diagnostic: dumps the live palette and the gem colour mapping to `path`. */
std::string DumpGemPaletteMapping(const char *path);

/** @brief Sprite for a gem `_iCurs`. Only valid for IsGemCurs(curs). */
ClxSprite GetGemSprite(int curs);

} // namespace devilution

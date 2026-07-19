/**
 * @file scroll_mods.h
 *
 * [RUNS MOD] Dungeon-scroll modifiers. Each scroll rolls 1-3 modifiers from its
 * item seed (deterministic, so the same scroll always shows and applies the same
 * modifiers, and save/load + network sync preserve them). Every modifier pairs a
 * player boon (more gold / magic find / loot / experience) with an offsetting bane
 * (tougher monsters / boss), so a scroll is always a risk/reward trade, never a
 * pure gain.
 */
#pragma once

#include <cstdint>
#include <string>

namespace devilution {

/** A scroll affix: each is a boon paired with an offsetting bane. */
enum class ScrollAffix : uint8_t {
	Avaricious,  // +gold found        / monsters have more life
	Enchanted,   // +magic find        / the boss has more life & damage
	Bountiful,   // +extra item drops  / monsters deal more damage
	Erudite,     // +experience        / monsters have more life
	Savage,      // +gold found        / monsters deal more damage
	Volatile,    // +extra item drops  / the run clock ticks faster (see run_clock)
	// [RUNS MOD] Expansion wave (PoE-map direction): density/elite/speed/elemental/
	// object banes and boons, plus the twinned boss.
	Teeming,     // +experience        / the dungeon holds more monsters
	Tyrannical,  // +magic find        / some monsters are champions (tough, drop loot)
	Frenzied,    // +gold found        / monsters act faster
	Hexbound,    // +magic find        / some monsters resist fire & lightning
	Gilded,      // +chests spawn      / monsters deal more damage
	Hallowed,    // +shrines spawn     / monsters have more life
	Twinned,     // a second run boss (and its vault) / both bosses have more life
	Last = Twinned,
};

/** One rolled modifier: an affix plus the magnitudes (percent) of its boon/bane.
 * The tier (1-3, rolled rare-high) scales BOTH sides: a high-tier roll is a bigger
 * reward and a bigger risk, giving crafting rerolls texture beyond the affix pick. */
struct ScrollMod {
	ScrollAffix affix;
	int boonPct;
	int banePct;
	int tier;
};

constexpr int MaxScrollMods = 3;

/** The full set of modifiers rolled for a scroll (1..MaxScrollMods entries). */
struct ScrollModSet {
	ScrollMod mods[MaxScrollMods];
	int count;
};

/**
 * @brief Deterministically rolls the modifier set for a scroll.
 * @param seed the scroll's item seed (_iSeed)
 * @param level the scroll's dungeon level (drives magnitudes)
 *
 * @param count how many modifiers to roll (1..MaxScrollMods); picks are positional,
 *   so a higher count keeps the lower-count affixes and adds more (used by crafting).
 *
 * Uses a private RNG so it never disturbs the game's RNG state (safe to call from
 * tooltip drawing).
 */
ScrollModSet RollScrollMods(uint32_t seed, int level, int count);

/** @brief [RUNS MOD] Rolls how many modifiers a fresh scroll of `level` has (1..3). */
int RollScrollModCount(uint32_t seed, int level);

/**
 * @brief A one-line "Name: boon, bane" description for tooltips.
 * @param full when true, uses natural full wording (for the auto-sizing floating
 *   tooltip and the wide store); when false, a compact form that fits the fixed
 *   288px info box on one line.
 */
std::string DescribeScrollMod(const ScrollMod &mod, bool full = false);

/** The aggregate percentage effects a run applies while the scroll is active. */
struct RunModMultipliers {
	int monsterHpPct = 0;   // extra monster max HP
	int bossHpPct = 0;      // extra boss HP (on top of monsterHpPct)
	int monsterDmgPct = 0;  // extra monster damage
	int goldPct = 0;        // extra gold from piles
	int itemQtyPct = 0;     // extra item-drop chance
	int magicFindPct = 0;   // bias toward higher-quality items
	int xpPct = 0;          // extra experience from kills
	int collapsePct = 0;    // run clock compression: grace/waves/collapse all faster
	// [RUNS MOD] Expansion wave:
	int packPct = 0;        // extra monster placement budget (more packs on the level)
	int championPct = 0;    // chance per monster to be a champion (tough, drops loot)
	int speedPct = 0;       // monsters animate (walk/attack) this much faster
	int elemResistPct = 0;  // chance per monster to gain fire+lightning resistance
	int chestsPct = 0;      // extra random chests (relative to the vanilla baseline)
	int shrinesPct = 0;     // extra random shrines (relative to a 2-shrine baseline)
	bool twinBoss = false;  // a second run boss (and second vault) spawns
};

/** @brief Sums a modifier set into the aggregate run multipliers. */
RunModMultipliers AggregateRunMods(const ScrollModSet &set);

/**
 * The run modifiers currently in effect. Zeroed unless the player is on the open
 * dungeon-scroll portal's level; refreshed by RefreshActiveRunMods() on level load.
 */
extern RunModMultipliers ActiveRunMods;

/** @brief Recomputes ActiveRunMods for the level now being loaded. */
void RefreshActiveRunMods();

/**
 * @brief Scales every monster just placed on the run level per ActiveRunMods
 * (monster/boss HP and monster damage). Call once after InitMonsters().
 */
void ScaleRunMonsters();

/**
 * @brief [RUNS MOD] Whether the Tyrannical bane made this monster a champion.
 * Derived from the portal seed and the monster's id (no stored state), so the
 * SAME answer is available at generation time (to buff it) and at death time
 * (to drop its guaranteed loot), on every client and across save/resume.
 */
bool IsRunChampion(unsigned monsterId);

/** @brief Applies a +pct multiplier to a value, guarding against overflow. */
inline int ApplyPct(int value, int pct)
{
	if (pct == 0)
		return value;
	return static_cast<int>((static_cast<int64_t>(value) * (100 + pct)) / 100);
}

} // namespace devilution

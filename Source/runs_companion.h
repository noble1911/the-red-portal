/**
 * @file runs_companion.h
 *
 * [RUNS MOD] Companion potions: drink one in the dungeon and a beast fights beside you for
 * a few minutes, then leaves.
 *
 * The companion is the Sorcerer's Golem with different art: a monster owned by a player,
 * flagged MFLAG_GOLEM, driven by the Golem AI and synced by the Golem's spawn message. It
 * shares the Golem's reserved monster slot, so a hero has one minion at a time -- drinking
 * a potion replaces a Golem, and casting Golem dismisses a companion.
 *
 * The potion carries the companion's stats: they are rolled from the potion's seed and
 * item level (the depth it dropped at), so a potion found shallow stays weak wherever it
 * is drunk, and two potions of the same level still differ. Anyone may drink any potion;
 * a fresh hero simply has no way to come by a strong one. The best are dungeon drops;
 * Adria sells weaker ones at a premium.
 *
 * Five roles, rolled from the potion's seed, open to every class: the Tank (Hellboar) stands
 * in front, the Fighter (Gargoyle) hits hard but breaks easily, the Ranged (Psychorb) hangs
 * back and shoots, the Elemental (Frost Charger) chills what it hits, and the Healer (the
 * Myconid, art from The Hell 4) heals its owner. All are ringed in a gold outline, so a pet
 * never passes for the wild monster it borrows its art from.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/point.hpp"
#include "tables/itemdat.h"
#include "tables/monstdat.h"

namespace devilution {

struct Item;
struct Monster;
struct Player;

/** A companion's role. Rolled from the potion's seed; any class may drink any kind. */
enum class CompanionKind : uint8_t {
	Tank,
	Fighter,
	Ranged,
	Elemental,
	Healer,
};
constexpr int NumCompanionKinds = 5;

/** What a potion's companion is like. Rolled, never stored: see GetCompanionStats. */
struct CompanionStats {
	CompanionKind kind;
	int life;      // whole hit points
	int armor;
	int toHit;
	int minDamage;
	int maxDamage;
	int durationSeconds;
	int tier; // 1 ordinary, 2 strong, 3 exceptional
	int heal; // Healer: life restored to its owner per pulse; 0 for the other kinds
};

/** Adria's potion sits this many levels under the hero's deepest beaten depth... */
constexpr int WitchCompanionLevelsBelow = 3;
/** ...and costs this percent of a dropped potion of its level. */
constexpr int WitchCompanionMarkupPct = 200;

/** Drop chances (percent, or per mille for ordinary monsters): a rare magic item, on the
 * scale of a unique rather than a scroll. Ordinary monsters were 3 per mille, which across a
 * run's 150-225 monsters made a potion every other run (balance review 2026-10-28). */
constexpr int CompanionVaultChancePct = 12;
constexpr int CompanionChampionChancePct = 5;
constexpr int CompanionMonsterChancePerMille = 1;

/** @brief The companion a potion of this seed and item level summons. Pure and
 * deterministic (private RNG), so every client derives the same stats from the same item. */
CompanionStats GetCompanionStats(uint32_t seed, int level);

/** @brief The role of the companion a potion of this seed summons. */
CompanionKind CompanionKindOf(uint32_t seed);

/** @brief The monster a companion of this kind is. Hellfire art where it exists (Hellboar,
 * Firebat); plain Diablo falls back to the Horned Demon and the Familiar. */
_monster_id CompanionMonsterType(CompanionKind kind);

/** @brief True if `type` is the monster of any companion kind. */
bool IsCompanionMonsterType(_monster_id type);

/** @brief The potion's display name, which says which beast it holds. */
std::string_view CompanionPotionName(const Item &item);

/** @brief Row of the companion potion in AllItemsList. The mod's items after the last named
 * row have no enum entry (inserting one would renumber every saved relic), so it is found
 * by its misc id. IDI_NONE if this item table lacks it. */
_item_indexes CompanionPotionIndex();

/** @brief True if the item is a companion potion. */
bool IsCompanionPotion(const Item &item);

/** @brief Builds a companion potion of item level `level` (a run depth) from `seed`. */
void CreateCompanionPotion(Item &item, int level, uint32_t seed);

/** @brief Rolls a potion at `level` and drops it near `position`, spawned on every client.
 * Call only from the killer's client (see DropRandomGem). */
void DropCompanionPotion(int level, Point position);

/** @brief Drinks `potion`: summons its companion next to `player`. Returns false (and says
 * why) if it cannot be drunk here, in which case the potion must not be spent. */
bool UseCompanionPotion(Player &player, const Item &potion);

/** @brief Appends the potion's tooltip lines: the beast, its stats and how long it stays. */
void GetCompanionPotionLines(const Item &item, std::vector<std::string> &out);

/** @brief Counts down the local hero's companion and dismisses it when its time is up.
 * Once per game tick on dungeon levels. */
void UpdateCompanionTimer();

/** @brief Game ticks left and in total for the local hero's companion, for the minion
 * panel. False when no companion is counting down (none, or a Golem, which has no timer). */
bool LocalCompanionTime(int &ticksLeft, int &ticksTotal);

/** How long an Elemental companion's hit slows its target, in game ticks. */
constexpr uint8_t CompanionChillTicks = 40;

// --- Spawn message encoding -------------------------------------------------------------
//
// A companion travels in the Golem's spawn message: its seed field carries the potion's
// seed and its spell-level byte carries the potion's level with the top bit set, so peers
// and late joiners rebuild the same companion. Other spawns use that byte for a Golem
// spell level or a run wave tier, both far below the marker.

constexpr uint8_t CompanionSpawnMarker = 0x80;

constexpr bool IsCompanionSpawnLevel(int spellLevel)
{
	return (spellLevel & CompanionSpawnMarker) != 0;
}

constexpr uint8_t EncodeCompanionSpawnLevel(int level)
{
	return static_cast<uint8_t>(CompanionSpawnMarker | (level & 0x3F));
}

constexpr int DecodeCompanionSpawnLevel(int spellLevel)
{
	return spellLevel & 0x3F;
}

} // namespace devilution

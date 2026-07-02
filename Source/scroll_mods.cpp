/**
 * @file scroll_mods.cpp
 *
 * [RUNS MOD] Implementation of dungeon-scroll modifiers. See scroll_mods.h.
 */
#include "scroll_mods.h"

#include <algorithm>
#include <array>
#include <string_view>

#include <fmt/format.h>

#include "levels/gendung.h"
#include "monster.h"
#include "multi.h"
#include "portal.h"
#include "runs_mod.h"
#include "utils/language.h"

namespace devilution {

RunModMultipliers ActiveRunMods;

namespace {

/** [RUNS MOD] Extra boss HP / damage per run level (percent), on top of modifiers. */
constexpr int BossHpPctPerLevel = 25;
constexpr int BossDmgPctPerLevel = 12;

/** [RUNS MOD] Reference boss stats (the Butcher's) that every run boss is NORMALIZED to
 * at level 1, then scaled up by level -- so whichever boss is chosen is level-appropriate
 * (a Warlord on level 1 is scaled down, a Butcher on level 15 is scaled up). */
constexpr int ReferenceBossHp = 220;
constexpr int ReferenceBossMinDmg = 6;
constexpr int ReferenceBossMaxDmg = 12;

/** A private, self-contained LCG so rolling never disturbs the game RNG. */
struct ModRng {
	uint32_t state;
	explicit ModRng(uint32_t seed)
	    : state(seed * 2654435761u + 1u)
	{
	}
	uint32_t next()
	{
		state = state * 1103515245u + 12345u;
		return state >> 8;
	}
	// Inclusive range [lo, hi].
	int range(int lo, int hi)
	{
		if (hi <= lo)
			return lo;
		return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
	}
};

/** Which run multiplier a magnitude feeds into. */
enum class Effect : uint8_t {
	GoldFind,
	MagicFind,
	ItemQuantity,
	Experience,
	MonsterHp,
	BossHp,
	MonsterDmg,
};

struct AffixData {
	const char *name;
	Effect boon;
	int boonLo;
	int boonHi;
	int boonPerLevel;
	Effect bane;
	int baneLo;
	int baneHi;
	int banePerLevel;
};

// Indexed by ScrollAffix. Each affix is a boon paired with an offsetting bane.
constexpr std::array<AffixData, 5> Affixes = { {
    // Avaricious: +gold / monsters have more life
    { "Avaricious", Effect::GoldFind, 40, 70, 3, Effect::MonsterHp, 15, 30, 2 },
    // Enchanted: +magic find / boss has more life & damage
    { "Enchanted", Effect::MagicFind, 12, 25, 2, Effect::BossHp, 30, 60, 4 },
    // Bountiful: +extra item drops / monsters deal more damage
    { "Bountiful", Effect::ItemQuantity, 20, 45, 3, Effect::MonsterDmg, 15, 30, 2 },
    // Erudite: +experience / monsters have more life
    { "Erudite", Effect::Experience, 25, 50, 3, Effect::MonsterHp, 20, 40, 2 },
    // Savage: +gold / monsters deal more damage
    { "Savage", Effect::GoldFind, 30, 55, 3, Effect::MonsterDmg, 20, 40, 3 },
} };

const AffixData &DataFor(ScrollAffix affix)
{
	return Affixes[static_cast<size_t>(affix)];
}

void ApplyEffect(RunModMultipliers &out, Effect effect, int pct)
{
	switch (effect) {
	case Effect::GoldFind:
		out.goldPct += pct;
		break;
	case Effect::MagicFind:
		out.magicFindPct += pct;
		break;
	case Effect::ItemQuantity:
		out.itemQtyPct += pct;
		break;
	case Effect::Experience:
		out.xpPct += pct;
		break;
	case Effect::MonsterHp:
		out.monsterHpPct += pct;
		break;
	case Effect::BossHp:
		out.bossHpPct += pct;
		break;
	case Effect::MonsterDmg:
		out.monsterDmgPct += pct;
		break;
	}
}

// Two forms: `full` reads naturally (for the auto-sizing floating tooltip and the
// wide store), the compact form fits the 288px fixed info box on one line.
std::string DescribeEffect(Effect effect, int pct, bool full)
{
	switch (effect) {
	case Effect::GoldFind:
		return fmt::format(fmt::runtime(full ? _("+{:d}% gold found") : _("+{:d}% gold")), pct);
	case Effect::MagicFind:
		return fmt::format(fmt::runtime(full ? _("+{:d}% magic find") : _("+{:d}% magic")), pct);
	case Effect::ItemQuantity:
		return fmt::format(fmt::runtime(full ? _("+{:d}% item drops") : _("+{:d}% loot")), pct);
	case Effect::Experience:
		return fmt::format(fmt::runtime(full ? _("+{:d}% experience") : _("+{:d}% XP")), pct);
	case Effect::MonsterHp:
		return fmt::format(fmt::runtime(full ? _("monsters +{:d}% life") : _("mob HP +{:d}%")), pct);
	case Effect::BossHp:
		return fmt::format(fmt::runtime(full ? _("boss +{:d}% life") : _("boss HP +{:d}%")), pct);
	case Effect::MonsterDmg:
		return fmt::format(fmt::runtime(full ? _("monsters +{:d}% damage") : _("mob dmg +{:d}%")), pct);
	}
	return {};
}

} // namespace

int RollScrollModCount(uint32_t seed, int level)
{
	if (level < 1)
		level = 1;
	// Use a distinct RNG stream from the affix roll so the two don't interfere.
	ModRng rng(seed ^ 0x9E3779B9u ^ (static_cast<uint32_t>(level) << 8));
	// Modifier count: 1 (most common) .. 3 (rare), nudged up a little by level.
	const int roll = rng.range(0, 99) + level;
	const int count = roll < 50 ? 1 : (roll < 82 ? 2 : 3);
	return count > MaxScrollMods ? MaxScrollMods : count;
}

ScrollModSet RollScrollMods(uint32_t seed, int level, int count)
{
	ScrollModSet set;
	set.count = 0;
	if (level < 1)
		level = 1;
	count = std::clamp(count, 1, MaxScrollMods);

	ModRng rng(seed ^ (static_cast<uint32_t>(level) << 24));

	// Pick `count` distinct affixes (partial Fisher-Yates over the affix indices).
	std::array<int, Affixes.size()> pool;
	for (size_t i = 0; i < pool.size(); i++)
		pool[i] = static_cast<int>(i);
	int remaining = static_cast<int>(pool.size());
	for (int i = 0; i < count && remaining > 0; i++) {
		const int pick = rng.range(0, remaining - 1);
		const auto affix = static_cast<ScrollAffix>(pool[pick]);
		pool[pick] = pool[remaining - 1];
		remaining--;

		const AffixData &data = DataFor(affix);
		ScrollMod &mod = set.mods[set.count++];
		mod.affix = affix;
		mod.boonPct = rng.range(data.boonLo, data.boonHi) + data.boonPerLevel * (level - 1);
		mod.banePct = rng.range(data.baneLo, data.baneHi) + data.banePerLevel * (level - 1);
	}
	return set;
}

std::string DescribeScrollMod(const ScrollMod &mod, bool full)
{
	const AffixData &data = DataFor(mod.affix);
	const std::string_view fmtStr = full
	    ? _(/* TRANSLATORS: scroll modifier: "Name: +boon, but +bane" */ "{:s}: {:s}, but {:s}")
	    : _(/* TRANSLATORS: scroll modifier: "Name: boon, bane" */ "{:s}: {:s}, {:s}");
	return fmt::format(fmt::runtime(fmtStr),
	    _(data.name), DescribeEffect(data.boon, mod.boonPct, full), DescribeEffect(data.bane, mod.banePct, full));
}

RunModMultipliers AggregateRunMods(const ScrollModSet &set)
{
	RunModMultipliers out;
	for (int i = 0; i < set.count; i++) {
		const AffixData &data = DataFor(set.mods[i].affix);
		ApplyEffect(out, data.boon, set.mods[i].boonPct);
		ApplyEffect(out, data.bane, set.mods[i].banePct);
	}
	return out;
}

void RefreshActiveRunMods()
{
	ActiveRunMods = RunModMultipliers {};

	// [RUNS MOD] Modifiers and difficulty apply only while on the open portal's dungeon
	// level; the hub (and any non-run level) is always Normal.
	const bool onRunLevel = RunsModEnabled && DungeonPortalOpen
	    && leveltype != DTYPE_TOWN && currlevel == DungeonPortalLevel;

	// [RUNS MOD] The scroll's difficulty drives the engine's real Nightmare/Hell scaling
	// (monster level/HP, resistances/immunities, XP, the Hell player penalty). This runs
	// before monsters are created on each level load, so a fresh run bakes the right
	// difficulty; the hub resets to Normal. Each client mirrors its own (synced) portal
	// state, so co-op stays consistent.
	sgGameInitInfo.nDifficulty = onRunLevel
	    ? static_cast<_difficulty>(std::clamp(DungeonPortalDifficulty, 0, static_cast<int>(DIFF_HELL)))
	    : DIFF_NORMAL;

	if (!onRunLevel)
		return;
	ActiveRunMods = AggregateRunMods(RollScrollMods(DungeonPortalSeed, DungeonPortalLevel, DungeonPortalModCount));
}

void ScaleRunMonsters()
{
	const int runLevel = std::max(static_cast<int>(currlevel), 1);

	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion())
			continue;

		// [RUNS MOD] The run boss is NORMALIZED to a level-appropriate target: its own
		// base stats are replaced, so any chosen boss (weak or strong) is scaled to the
		// current depth. Scroll boss/monster modifiers stack on top; the SP HP-halving
		// matches the engine's own unique-monster init.
		if (monster.isUnique() && monster.uniqueType == RunBossType) {
			int hp = ReferenceBossHp * (100 + (runLevel - 1) * BossHpPctPerLevel) / 100;
			hp = ApplyPct(hp, ActiveRunMods.bossHpPct + ActiveRunMods.monsterHpPct);
			if (!gbIsMultiplayer)
				hp = std::max(hp / 2, 64);
			monster.maxHitPoints = hp << 6;
			monster.hitPoints = monster.maxHitPoints;

			const auto bossDmg = [runLevel](int base) {
				const int d = base * (100 + (runLevel - 1) * BossDmgPctPerLevel) / 100;
				return static_cast<uint8_t>(std::clamp(ApplyPct(d, ActiveRunMods.monsterDmgPct), 0, 255));
			};
			monster.minDamage = monster.minDamageSpecial = bossDmg(ReferenceBossMinDmg);
			monster.maxDamage = monster.maxDamageSpecial = bossDmg(ReferenceBossMaxDmg);
			continue;
		}

		// Regular monsters and any non-boss uniques: apply the scroll's flat modifiers.
		if (ActiveRunMods.monsterHpPct != 0) {
			monster.maxHitPoints = ApplyPct(monster.maxHitPoints, ActiveRunMods.monsterHpPct);
			monster.hitPoints = monster.maxHitPoints;
		}
		if (ActiveRunMods.monsterDmgPct != 0) {
			const auto scaleDmg = [](uint8_t dmg) {
				return static_cast<uint8_t>(std::min(ApplyPct(dmg, ActiveRunMods.monsterDmgPct), 255));
			};
			monster.minDamage = scaleDmg(monster.minDamage);
			monster.maxDamage = scaleDmg(monster.maxDamage);
			monster.minDamageSpecial = scaleDmg(monster.minDamageSpecial);
			monster.maxDamageSpecial = scaleDmg(monster.maxDamageSpecial);
		}
	}
}

} // namespace devilution

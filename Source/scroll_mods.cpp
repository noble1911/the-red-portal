/**
 * @file scroll_mods.cpp
 *
 * [RUNS MOD] Implementation of dungeon-scroll modifiers. See scroll_mods.h.
 */
#include "scroll_mods.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include <fmt/format.h>

#include "levels/gendung.h"
#include "missiles.h"
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
constexpr int BossDmgPctPerLevel = 16;

/** [RUNS MOD] Reference boss stats that every run boss is NORMALIZED to at level 1, then
 * scaled up by level -- so whichever boss is chosen is level-appropriate (a Warlord on
 * level 1 is scaled down, a Butcher on level 15 is scaled up). Tuned so a fresh character
 * can beat the level-1 boss: modest HP and low base damage, ramping up with level.
 *
 * This curve is now only the FLOOR. On its own it fell far behind the monsters around the
 * boss -- at depth 15 it hit for 6-19 beside monsters hitting 20-30, and it ignored
 * Nightmare/Hell entirely (the engine's difficulty multipliers are overwritten here), so a
 * Hell boss had less life than an ordinary monster. The boss now also measures the level
 * it guards (see BossLifeMonsterMultiple) and takes whichever is stronger. */
constexpr int ReferenceBossHp = 150;
constexpr int ReferenceBossMinDmg = 2;
constexpr int ReferenceBossMaxDmg = 6;

/** [RUNS MOD] The boss has this many times the life of the average monster on its level...
 * Those monsters already carry the depth, Hellfire's deeper rosters and the difficulty, so
 * the boss follows all three; the scroll's modifiers stack on top as before. */
constexpr int BossLifeMonsterMultiple = 8;
/** ...and hits this percent as hard as that average monster. */
constexpr int BossDamageMonsterPct = 130;

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
	Collapse,    // [RUNS MOD] the push-your-luck clock runs this much faster
	MonsterPack, // more monsters are placed on the level
	Champions,   // this % of monsters are champions (tough, guaranteed drop)
	MonsterSpeed, // monsters animate (walk/attack) faster
	ElemResist,  // this % of monsters resist fire & lightning
	Chests,      // extra random chests
	Shrines,     // extra random shrines
	TwinBoss,    // a second run boss spawns (its own vault + scroll)
	Sockets,     // drops are more likely to have a socket (percentage points)
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
// [RUNS MOD] Balance review (2026-10-28): the gold boons are half again larger -- gold was
// the weakest reward to weigh against a death that drops your whole kit -- and Jeweled's boon
// now grows with depth like its bane does.
constexpr std::array<AffixData, 14> Affixes = { {
    // Avaricious: +gold / monsters have more life
    { "Avaricious", Effect::GoldFind, 60, 105, 4, Effect::MonsterHp, 15, 30, 2 },
    // Enchanted: +magic find / boss has more life & damage
    { "Enchanted", Effect::MagicFind, 12, 25, 2, Effect::BossHp, 30, 60, 4 },
    // Bountiful: +extra item drops / monsters deal more damage
    { "Bountiful", Effect::ItemQuantity, 20, 45, 3, Effect::MonsterDmg, 15, 30, 2 },
    // Erudite: +experience / monsters have more life
    { "Erudite", Effect::Experience, 25, 50, 3, Effect::MonsterHp, 20, 40, 2 },
    // Savage: +gold / monsters deal more damage
    { "Savage", Effect::GoldFind, 45, 80, 4, Effect::MonsterDmg, 20, 40, 3 },
    // Volatile: big +item drops / the run clock (grace, waves, collapse) runs faster
    { "Volatile", Effect::ItemQuantity, 40, 80, 4, Effect::Collapse, 30, 60, 2 },
    // Teeming: +experience / the dungeon holds more monsters
    { "Teeming", Effect::Experience, 25, 45, 3, Effect::MonsterPack, 30, 60, 3 },
    // Tyrannical: +magic find / part of the population are champions
    { "Tyrannical", Effect::MagicFind, 15, 30, 2, Effect::Champions, 10, 20, 1 },
    // Frenzied: +gold / monsters act faster
    { "Frenzied", Effect::GoldFind, 50, 90, 4, Effect::MonsterSpeed, 15, 30, 1 },
    // Hexbound: +magic find / part of the population resists fire & lightning
    { "Hexbound", Effect::MagicFind, 18, 35, 2, Effect::ElemResist, 35, 65, 2 },
    // Gilded: extra chests / monsters deal more damage (the treasure is guarded)
    { "Gilded", Effect::Chests, 40, 80, 4, Effect::MonsterDmg, 15, 30, 2 },
    // Hallowed: extra shrines / monsters have more life
    { "Hallowed", Effect::Shrines, 50, 100, 4, Effect::MonsterHp, 20, 40, 2 },
    // Twinned: a second boss and its whole jackpot / both bosses are tougher.
    // The boon pct is unused (presence is the boon); the bane pct feeds BossHp.
    { "Twinned", Effect::TwinBoss, 0, 0, 0, Effect::BossHp, 20, 40, 3 },
    // Jeweled: more of the run's drops have a socket / monsters have more life.
    // Points, not percent: tier 1 is roughly double the 8% base drop chance.
    { "Jeweled", Effect::Sockets, 8, 15, 1, Effect::MonsterHp, 20, 40, 2 },
} };

/** Magnitude multiplier (percent) per tier: tier 2/3 rolls run hotter on BOTH sides. */
constexpr int TierScalePct[3] = { 100, 150, 220 };

/** [RUNS MOD] Tyrannical champion stat boosts (on top of the scroll's flat mods). */
constexpr int ChampionHpBonusPct = 150;

/** [RUNS MOD] Ceilings on a scroll's combined modifiers (see AggregateRunMods). The banes
 * stop at what a geared hero can still push through; the boons that are really loot --
 * champions (each drops a good item), chests, shrines, extra packs -- stop before they swamp
 * the boss vault. */
constexpr int MaxMonsterHpPct = 200;
constexpr int MaxMonsterDmgPct = 150;
constexpr int MaxBossHpPct = 200;
constexpr int MaxSpeedPct = 50;
constexpr int MaxCollapsePct = 150;
constexpr int MaxElemResistPct = 75;
constexpr int MaxChampionPct = 30;
constexpr int MaxPackPct = 100;
constexpr int MaxChestsPct = 100;
constexpr int MaxShrinesPct = 200;
constexpr int ChampionDmgBonusPct = 40;

/** Stream salts so the champion and elemental per-monster rolls are independent. */
constexpr uint32_t ChampionSalt = 0xC4A1101u;
constexpr uint32_t ElemResistSalt = 0xE1E4E57u;

/** Deterministic per-monster roll in [0,100) for population effects: derived from the
 * synced portal seed and the monster's id, so every client (and any later recompute,
 * e.g. the champion death payout) gets the same answer with no stored state. */
int PerMonsterRoll(unsigned monsterId, uint32_t salt)
{
	ModRng rng(DungeonPortalSeed ^ salt ^ (static_cast<uint32_t>(monsterId) * 0x9E3779B9u));
	return rng.range(0, 99);
}

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
	case Effect::Collapse:
		out.collapsePct += pct;
		break;
	case Effect::MonsterPack:
		out.packPct += pct;
		break;
	case Effect::Champions:
		out.championPct += pct;
		break;
	case Effect::MonsterSpeed:
		out.speedPct += pct;
		break;
	case Effect::ElemResist:
		out.elemResistPct += pct;
		break;
	case Effect::Chests:
		out.chestsPct += pct;
		break;
	case Effect::Shrines:
		out.shrinesPct += pct;
		break;
	case Effect::TwinBoss:
		out.twinBoss = true;
		break;
	case Effect::Sockets:
		out.socketPct += pct;
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
	case Effect::Collapse:
		return fmt::format(fmt::runtime(full ? _("the dungeon destabilizes {:d}% faster") : _("clock +{:d}%")), pct);
	case Effect::MonsterPack:
		return fmt::format(fmt::runtime(full ? _("the dungeon teems with {:d}% more monsters") : _("mobs +{:d}%")), pct);
	case Effect::Champions:
		return fmt::format(fmt::runtime(full ? _("{:d}% of monsters are champions") : _("champs {:d}%")), pct);
	case Effect::MonsterSpeed:
		return fmt::format(fmt::runtime(full ? _("monsters are {:d}% faster") : _("mob spd +{:d}%")), pct);
	case Effect::ElemResist:
		return fmt::format(fmt::runtime(full ? _("{:d}% of monsters resist fire and lightning") : _("warded {:d}%")), pct);
	case Effect::Chests:
		return fmt::format(fmt::runtime(full ? _("+{:d}% chests") : _("+{:d}% chests")), pct);
	case Effect::Shrines:
		return fmt::format(fmt::runtime(full ? _("+{:d}% shrines") : _("+{:d}% shrines")), pct);
	case Effect::TwinBoss:
		return std::string(full ? _("the boss is twinned (a second vault awaits)") : _("twin boss"));
	case Effect::Sockets:
		return fmt::format(fmt::runtime(full ? _("+{:d}% chance of socketed items") : _("+{:d}% sockets")), pct);
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
		// Tier scales BOTH sides (bigger reward, bigger risk). Rolled per pick, after
		// the magnitudes, so lower-count prefixes stay stable for the Add-modifier craft.
		const int tierRoll = rng.range(0, 99);
		mod.tier = tierRoll < 60 ? 1 : (tierRoll < 90 ? 2 : 3);
		const int tierPct = TierScalePct[mod.tier - 1];
		mod.boonPct = mod.boonPct * tierPct / 100;
		mod.banePct = mod.banePct * tierPct / 100;
	}
	return set;
}

std::string DescribeScrollMod(const ScrollMod &mod, bool full)
{
	const AffixData &data = DataFor(mod.affix);
	// Tier shows as a roman numeral on the name; tier 1 stays bare so common
	// scrolls read like they always did.
	std::string name(_(data.name));
	if (mod.tier == 2)
		name += " II";
	else if (mod.tier == 3)
		name += " III";
	const std::string_view fmtStr = full
	    ? _(/* TRANSLATORS: scroll modifier: "Name: +boon, but +bane" */ "{:s}: {:s}, but {:s}")
	    : _(/* TRANSLATORS: scroll modifier: "Name: boon, bane" */ "{:s}: {:s}, {:s}");
	return fmt::format(fmt::runtime(fmtStr),
	    name, DescribeEffect(data.boon, mod.boonPct, full), DescribeEffect(data.bane, mod.banePct, full));
}

RunModMultipliers AggregateRunMods(const ScrollModSet &set)
{
	RunModMultipliers out;
	for (int i = 0; i < set.count; i++) {
		const AffixData &data = DataFor(set.mods[i].affix);
		ApplyEffect(out, data.boon, set.mods[i].boonPct);
		ApplyEffect(out, data.bane, set.mods[i].banePct);
	}
	// [RUNS MOD] Ceilings on the combined effect. Magnitudes grow with depth and tier (up to
	// x2.2), and modifiers of one kind add up, so a deep tier-III scroll could turn most of
	// the population into loot-dropping champions, scatter fifty extra chests, or stack three
	// life banes past +400%. Each scroll's own lines still show what it rolled; only the
	// total is held here.
	out.monsterHpPct = std::min(out.monsterHpPct, MaxMonsterHpPct);
	out.monsterDmgPct = std::min(out.monsterDmgPct, MaxMonsterDmgPct);
	out.bossHpPct = std::min(out.bossHpPct, MaxBossHpPct);
	out.speedPct = std::min(out.speedPct, MaxSpeedPct);
	out.collapsePct = std::min(out.collapsePct, MaxCollapsePct);
	out.elemResistPct = std::min(out.elemResistPct, MaxElemResistPct);
	out.championPct = std::min(out.championPct, MaxChampionPct);
	out.packPct = std::min(out.packPct, MaxPackPct);
	out.chestsPct = std::min(out.chestsPct, MaxChestsPct);
	out.shrinesPct = std::min(out.shrinesPct, MaxShrinesPct);
	return out;
}

bool IsRunChampion(unsigned monsterId)
{
	return ActiveRunMods.championPct > 0
	    && PerMonsterRoll(monsterId, ChampionSalt) < ActiveRunMods.championPct;
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

	// [RUNS MOD] The average ordinary monster on this level, measured before any scroll
	// modifier touches it: what the boss is scaled against. Every client generates the
	// same level, so every client measures the same average.
	int64_t sumLife = 0;
	int64_t sumMinDamage = 0;
	int64_t sumMaxDamage = 0;
	int ordinaryCount = 0;
	for (size_t i = 0; i < ActiveMonsterCount; i++) {
		const Monster &monster = Monsters[ActiveMonsters[i]];
		if (monster.isPlayerMinion() || monster.isUnique() || monster.position.tile == GolemHoldingCell)
			continue;
		sumLife += monster.maxHitPoints >> 6;
		sumMinDamage += monster.minDamage;
		sumMaxDamage += monster.maxDamage;
		ordinaryCount++;
	}

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
			// The single-player halving the engine gives every monster: the floor curve is
			// a multiplayer figure, the measured average already has it.
			if (!gbIsMultiplayer)
				hp = std::max(hp / 2, 1);
			if (ordinaryCount > 0)
				hp = std::max<int>(hp, static_cast<int>(sumLife * BossLifeMonsterMultiple / ordinaryCount));
			hp = ApplyPct(hp, ActiveRunMods.bossHpPct + ActiveRunMods.monsterHpPct);
			monster.maxHitPoints = std::max(hp, 1) << 6;
			monster.hitPoints = monster.maxHitPoints;

			const auto bossDmg = [runLevel, ordinaryCount](int base, int64_t levelSum) {
				int d = base * (100 + (runLevel - 1) * BossDmgPctPerLevel) / 100;
				if (ordinaryCount > 0)
					d = std::max<int>(d, static_cast<int>(levelSum * BossDamageMonsterPct / 100 / ordinaryCount));
				return static_cast<uint8_t>(std::clamp(ApplyPct(d, ActiveRunMods.monsterDmgPct), 0, 255));
			};
			monster.minDamage = monster.minDamageSpecial = bossDmg(ReferenceBossMinDmg, sumMinDamage);
			monster.maxDamage = monster.maxDamageSpecial = std::max(bossDmg(ReferenceBossMaxDmg, sumMaxDamage), monster.minDamage);
			continue;
		}

		// Regular monsters and any non-boss uniques: apply the scroll's flat modifiers.
		// [RUNS MOD] Tyrannical champions stack a big personal boost on top; the pick is
		// re-derivable (IsRunChampion), so the death hook can pay out their loot without
		// any stored state. HP/damage/resistance are all delta-saved fields, so buffs
		// applied here survive save/resume (this only runs on fresh generation).
		const bool champion = IsRunChampion(static_cast<unsigned>(monster.getId()));
		const int hpPct = ActiveRunMods.monsterHpPct + (champion ? ChampionHpBonusPct : 0);
		const int dmgPct = ActiveRunMods.monsterDmgPct + (champion ? ChampionDmgBonusPct : 0);
		if (hpPct != 0) {
			monster.maxHitPoints = ApplyPct(monster.maxHitPoints, hpPct);
			monster.hitPoints = monster.maxHitPoints;
		}
		if (dmgPct != 0) {
			const auto scaleDmg = [dmgPct](uint8_t dmg) {
				return static_cast<uint8_t>(std::min(ApplyPct(dmg, dmgPct), 255));
			};
			monster.minDamage = scaleDmg(monster.minDamage);
			monster.maxDamage = scaleDmg(monster.maxDamage);
			monster.minDamageSpecial = scaleDmg(monster.minDamageSpecial);
			monster.maxDamageSpecial = scaleDmg(monster.maxDamageSpecial);
		}
		// [RUNS MOD] Hexbound: part of the population gains fire+lightning resistance.
		// Per-monster deterministic roll; `resistance` is a persisted field.
		if (ActiveRunMods.elemResistPct > 0
		    && PerMonsterRoll(static_cast<unsigned>(monster.getId()), ElemResistSalt) < ActiveRunMods.elemResistPct) {
			monster.resistance |= RESIST_FIRE | RESIST_LIGHTNING;
		}
	}
}

} // namespace devilution

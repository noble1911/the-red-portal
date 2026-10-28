/**
 * @file runs_companion.cpp
 *
 * [RUNS MOD] Companion potions. See runs_companion.h for the concept.
 */
#include "runs_companion.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include <fmt/format.h>

#include "engine/backbuffer_state.hpp"
#include "engine/path.h"
#include "engine/random.hpp"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "qol/floatingnumbers.h"
#include "runs_mod.h"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

// --- Stat curves --------------------------------------------------------------------------
//
// One curve per role. Every number is per item level (= the run depth the potion dropped
// at), so a companion keeps pace with the monsters of the depth it came from and no
// further. Damage is per hit for the melee kinds and per bolt for the ranged one; the
// Healer's heal is per pulse. All of these are tuning knobs.

struct KindCurve {
	int lifeBase, lifePerLevel;
	int armorBase, armorPerLevel;
	int toHitBase, toHitPerLevel;
	int minDamageBase, minDamagePer4Levels;
	int maxDamageBase, maxDamagePer4Levels;
	int healBase, healPerLevel; // Healer only
};

constexpr KindCurve KindCurves[NumCompanionKinds] = {
	// Tank: stands in front -- lots of life and armour, modest damage.
	{ 60, 30, 15, 3, 40, 4, 2, 2, 6, 4, 0, 0 },
	// Fighter: fragile and fast to kill things -- half the tank's armour, twice its damage.
	{ 40, 18, 10, 2, 50, 5, 4, 4, 10, 8, 0, 0 },
	// Ranged: hangs back and shoots -- least life, steady bolts.
	{ 30, 14, 8, 2, 50, 5, 3, 3, 8, 6, 0, 0 },
	// Elemental: a middleweight whose hits chill (slow) what they land on.
	{ 50, 24, 12, 3, 45, 4, 3, 3, 8, 6, 0, 0 },
	// Healer: weak in a fight, but heals its owner every few seconds while close. Its heal was
	// 6 + 3/level, about 12 life a second at depth 10 -- with Heal Other too, solo play got
	// very safe -- now 5 + 2/level.
	{ 35, 16, 8, 2, 35, 3, 1, 1, 4, 2, 5, 2 },
};

/** The Healer heals its owner this often, if the owner is within HealRange tiles. */
constexpr int HealPulseSeconds = 3;
constexpr int HealRange = 7;

/** Each stat lands within this percent band around its curve value. */
constexpr int StatSpreadLowPct = 85;
constexpr int StatSpreadHighPct = 115;

/** How long a companion stays, before the exceptional tier's bonus. */
constexpr int DurationLowSeconds = 120;
constexpr int DurationHighSeconds = 180;
constexpr int ExceptionalBonusSeconds = 30;

/** Tier odds (percent) and how much each tier lifts life, armour and damage. */
constexpr int StrongTierPct = 25;
constexpr int ExceptionalTierPct = 5;
constexpr int TierScalePct[] = { 100, 120, 145 };

/** Gold value per item level; Adria marks hers up on top (see SpawnWitch). */
constexpr int PotionValueBase = 800;
constexpr int PotionValuePerLevel = 300;

/** The game RNG must not move when a tooltip asks for a potion's stats, so rolls come from
 * a private generator seeded by the potion. */
class CompanionRng {
public:
	explicit CompanionRng(uint32_t seed)
	    : state_(seed ^ 0xC0A7B0A2U)
	{
	}

	/** Uniform in [lo, hi]. */
	int range(int lo, int hi)
	{
		state_ += 0x9E3779B9U;
		uint32_t x = state_;
		x ^= x >> 16;
		x *= 0x7FEB352DU;
		x ^= x >> 15;
		x *= 0x846CA68BU;
		x ^= x >> 16;
		return lo + static_cast<int>(x % static_cast<uint32_t>(hi - lo + 1));
	}

private:
	uint32_t state_;
};

int Spread(CompanionRng &rng, int value, int tierPct)
{
	return std::max(1, value * rng.range(StatSpreadLowPct, StatSpreadHighPct) / 100 * tierPct / 100);
}

// The local hero's companion, for its timer. Only the owner counts down: dismissing it is a
// kill, which is broadcast like any other, so peers need no clock of their own.
int CompanionMonsterId = -1;
int CompanionTicksLeft = 0;
int CompanionTicksTotal = 0;
/** The potion the local companion came from, for the Healer's pulse. */
uint32_t CompanionSeed = 0;
int CompanionLevel = 0;

} // namespace

CompanionKind CompanionKindOf(uint32_t seed)
{
	// Its own stream, so the kind does not shift the stat rolls.
	CompanionRng rng(seed ^ 0x6B1D5A3CU);
	return static_cast<CompanionKind>(rng.range(0, NumCompanionKinds - 1));
}

CompanionStats GetCompanionStats(uint32_t seed, int level)
{
	level = std::clamp(level, 1, 63);
	CompanionRng rng(seed ^ (static_cast<uint32_t>(level) << 24));

	CompanionStats stats;
	stats.kind = CompanionKindOf(seed);
	const KindCurve &curve = KindCurves[static_cast<size_t>(stats.kind)];

	const int tierRoll = rng.range(0, 99);
	stats.tier = tierRoll < ExceptionalTierPct ? 3 : (tierRoll < ExceptionalTierPct + StrongTierPct ? 2 : 1);
	const int tierPct = TierScalePct[stats.tier - 1];

	stats.life = Spread(rng, curve.lifeBase + curve.lifePerLevel * level, tierPct);
	stats.armor = std::min(Spread(rng, curve.armorBase + curve.armorPerLevel * level, tierPct), 255);
	stats.toHit = std::min(curve.toHitBase + curve.toHitPerLevel * level, 255);
	stats.minDamage = std::min(Spread(rng, curve.minDamageBase + curve.minDamagePer4Levels * level / 4, tierPct), 255);
	stats.maxDamage = std::clamp(Spread(rng, curve.maxDamageBase + curve.maxDamagePer4Levels * level / 4, tierPct), stats.minDamage, 255);
	stats.durationSeconds = rng.range(DurationLowSeconds, DurationHighSeconds)
	    + (stats.tier == 3 ? ExceptionalBonusSeconds : 0);
	// Drawn last so the earlier rolls (and every non-healer's stats) are unaffected.
	stats.heal = curve.healBase > 0 ? Spread(rng, curve.healBase + curve.healPerLevel * level, tierPct) : 0;
	return stats;
}

_monster_id CompanionMonsterType(CompanionKind kind)
{
	// Hellfire art where it exists; plain Diablo falls back to the nearest look-alike.
	switch (kind) {
	case CompanionKind::Fighter:
		return MT_GARGOYLE;
	case CompanionKind::Ranged:
		return gbIsHellfire ? MT_PSYCHORB : MT_FAMILIAR;
	case CompanionKind::Elemental:
		return MT_FROSTC; // Frost Charger
	case CompanionKind::Healer: {
		// The Myconid is the mod's own monster (art from The Hell 4), added by monstdat.tsv.
		// Looked up every time, never cached: the Diablo and Hellfire monster tables are
		// loaded at different times and put it at different indices, so a cached id went
		// stale and drew some other monster.
		const std::optional<_monster_id> myconid = FindMonsterIdByString("MT_MYCONID");
		if (!myconid) {
			LogWarn("Companion: MT_MYCONID is missing from monstdat.tsv; the Healer falls back to the Fiend");
			return MT_FIEND;
		}
		return *myconid;
	}
	case CompanionKind::Tank:
	default:
		return gbIsHellfire ? MT_HELLBOAR : MT_HORNED;
	}
}

bool IsCompanionMonsterType(_monster_id type)
{
	for (int kind = 0; kind < NumCompanionKinds; kind++) {
		if (CompanionMonsterType(static_cast<CompanionKind>(kind)) == type)
			return true;
	}
	return false;
}

std::string_view CompanionPotionName(const Item &item)
{
	switch (CompanionKindOf(item._iSeed)) {
	case CompanionKind::Fighter:
		return _("Gargoyle Draught");
	case CompanionKind::Ranged:
		return _("Orb Draught");
	case CompanionKind::Elemental:
		return _("Frost Draught");
	case CompanionKind::Healer:
		return _("Spore Draught");
	case CompanionKind::Tank:
	default:
		return _("Boar Draught");
	}
}

_item_indexes CompanionPotionIndex()
{
	for (size_t i = 0; i < AllItemsList.size(); i++) {
		if (AllItemsList[i].iMiscId == IMISC_COMPANION)
			return static_cast<_item_indexes>(i);
	}
	return IDI_NONE;
}

bool IsCompanionPotion(const Item &item)
{
	return item._iMiscId == IMISC_COMPANION;
}

void CreateCompanionPotion(Item &item, int level, uint32_t seed)
{
	const _item_indexes idx = CompanionPotionIndex();
	if (idx == IDI_NONE) {
		item.clear();
		return;
	}
	level = std::clamp(level, 1, static_cast<int>(CF_LEVEL));
	InitializeItem(item, idx);
	item._iSeed = seed;
	// The level lives in the create-info like a dungeon scroll's, so the potion rebuilds
	// from its seed and level alone (see RecreateItem).
	item._iCreateInfo = static_cast<uint16_t>(level);
	item._iIdentified = true;
	// Each kind has its own bottle, so they tell apart at a glance in the pack.
	switch (CompanionKindOf(seed)) {
	case CompanionKind::Fighter:
		item._iCurs = ICURS_GOLDEN_ELIXIR;
		break;
	case CompanionKind::Ranged:
		item._iCurs = ICURS_ARENA_POTION;
		break;
	case CompanionKind::Elemental:
		item._iCurs = ICURS_ELIXIR_OF_MAGIC;
		break;
	case CompanionKind::Healer:
		item._iCurs = ICURS_ELIXIR_OF_VITALITY;
		break;
	case CompanionKind::Tank:
		item._iCurs = ICURS_SPECTRAL_ELIXIR;
		break;
	}
	item._ivalue = item._iIvalue = PotionValueBase + PotionValuePerLevel * level;
	if (MyPlayer != nullptr)
		item.updateRequiredStatsCacheForPlayer(*MyPlayer);
}

void DropCompanionPotion(int level, Point position)
{
	Item potion;
	CreateCompanionPotion(potion, level, AdvanceRndSeed());
	DropPrizeItem(potion, position);
}

bool UseCompanionPotion(Player &player, const Item &potion)
{
	if (!RunsModEnabled || leveltype == DTYPE_TOWN) {
		player.Say(HeroSpeech::ThatWontWorkHere);
		return false;
	}

	const std::optional<Point> spawnPosition = FindClosestValidPosition(
	    [start = player.position.tile](Point target) {
		    return !IsTileOccupied(target) && LineClearMissile(start, target);
	    },
	    player.position.tile, 1, 5);
	if (!spawnPosition) {
		player.Say(HeroSpeech::ThatWontWorkHere);
		return false;
	}

	// One minion at a time: the companion takes the Golem's slot.
	if (Monster *existing = FindGolemForPlayer(player); existing != nullptr)
		KillGolem(*existing);

	const int level = potion._iCreateInfo & CF_LEVEL;
	const int monsterId = SpawnCompanion(player, *spawnPosition, potion._iSeed, level);
	if (monsterId < 0) {
		player.Say(HeroSpeech::ThatWontWorkHere);
		return false;
	}

	if (&player == MyPlayer) {
		CompanionMonsterId = monsterId;
		CompanionSeed = potion._iSeed;
		CompanionLevel = level;
		CompanionTicksLeft = GetCompanionStats(potion._iSeed, level).durationSeconds * std::max<int>(sgGameInitInfo.nTickRate, 1);
		CompanionTicksTotal = CompanionTicksLeft;
	}
	return true;
}

void GetCompanionPotionLines(const Item &item, std::vector<std::string> &out)
{
	const int level = item._iCreateInfo & CF_LEVEL;
	const CompanionStats stats = GetCompanionStats(item._iSeed, level);
	const std::string_view beast = pgettext("monster", MonstersData[CompanionMonsterType(stats.kind)].name);
	std::string_view role;
	switch (stats.kind) {
	case CompanionKind::Fighter:
		role = _(/* TRANSLATORS: companion role */ "Fighter");
		break;
	case CompanionKind::Ranged:
		role = _(/* TRANSLATORS: companion role */ "Ranged");
		break;
	case CompanionKind::Elemental:
		role = _(/* TRANSLATORS: companion role */ "Elemental");
		break;
	case CompanionKind::Healer:
		role = _(/* TRANSLATORS: companion role */ "Healer");
		break;
	case CompanionKind::Tank:
	default:
		role = _(/* TRANSLATORS: companion role */ "Tank");
		break;
	}
	switch (stats.tier) {
	case 3:
		out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a role, then a monster name */ "{:s}: an exceptional {:s}")), role, beast));
		break;
	case 2:
		out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a role, then a monster name */ "{:s}: a strong {:s}")), role, beast));
		break;
	default:
		out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a role, then a monster name */ "{:s}: a {:s}")), role, beast));
		break;
	}
	out.push_back(fmt::format(fmt::runtime(_("Life {:d}, armor {:d}")), stats.life, stats.armor));
	const int minutes = stats.durationSeconds / 60;
	const int seconds = stats.durationSeconds % 60;
	switch (stats.kind) {
	case CompanionKind::Ranged:
		out.push_back(fmt::format(fmt::runtime(_("Bolts {:d}-{:d}, stays {:d}:{:02d}")), stats.minDamage, stats.maxDamage, minutes, seconds));
		break;
	case CompanionKind::Elemental:
		out.push_back(fmt::format(fmt::runtime(_("Chilling hits {:d}-{:d}, stays {:d}:{:02d}")), stats.minDamage, stats.maxDamage, minutes, seconds));
		break;
	case CompanionKind::Healer:
		out.push_back(fmt::format(fmt::runtime(_("Heals you {:d} every {:d}s when hurt and near")), stats.heal, HealPulseSeconds));
		out.push_back(fmt::format(fmt::runtime(_("Damage {:d}-{:d}, stays {:d}:{:02d}")), stats.minDamage, stats.maxDamage, minutes, seconds));
		break;
	default:
		out.push_back(fmt::format(fmt::runtime(_("Damage {:d}-{:d}, stays {:d}:{:02d}")), stats.minDamage, stats.maxDamage, minutes, seconds));
		break;
	}
}

void UpdateCompanionTimer()
{
	if (CompanionMonsterId < 0)
		return;
	Monster &companion = Monsters[CompanionMonsterId];
	// Gone already (killed, dismissed by a Golem cast, or a new level): stop counting.
	if (!companion.isPlayerMinion() || companion.goalVar3 != MyPlayerId || companion.hitPoints <= 0
	    || !IsCompanionMonsterType(companion.type().type)) {
		CompanionMonsterId = -1;
		return;
	}
	// The Healer's pulse: the owner's client heals its own hero, whose life is that
	// client's to change, and the usual player sync carries it to everyone else.
	const int tickRate = std::max<int>(sgGameInitInfo.nTickRate, 1);
	if (CompanionKindOf(CompanionSeed) == CompanionKind::Healer && CompanionTicksLeft % (HealPulseSeconds * tickRate) == 0) {
		Player &owner = *MyPlayer;
		if (!owner.hasNoLife() && owner._pHitPoints < owner._pMaxHP
		    && owner.position.tile.WalkingDistance(companion.position.tile) <= HealRange) {
			const int before = owner._pHitPoints;
			const int heal = GetCompanionStats(CompanionSeed, CompanionLevel).heal << 6;
			owner._pHitPoints = std::min(owner._pHitPoints + heal, owner._pMaxHP);
			owner._pHPBase = std::min(owner._pHPBase + heal, owner._pMaxHPBase);
			RedrawComponent(PanelDrawComponent::Health);
			PlayEffect(companion, MonsterSound::Special);
			// Without a visible sign the pulse went unnoticed: show what it restored, rising
			// off the hero with the heal icon.
			const int restored = (owner._pHitPoints - before) >> 6;
			if (restored > 0)
				AddFloatingNumber(owner.position.tile, {}, StrCat("+", restored), UiFlags::ColorWhite, 0, false, FloatingNumberIcon::Heal);
		}
	}
	if (--CompanionTicksLeft > 0)
		return;
	CompanionMonsterId = -1;
	KillGolem(companion);
}


bool LocalCompanionTime(int &ticksLeft, int &ticksTotal)
{
	if (CompanionMonsterId < 0 || CompanionTicksTotal <= 0)
		return false;
	ticksLeft = std::max(CompanionTicksLeft, 0);
	ticksTotal = CompanionTicksTotal;
	return true;
}

} // namespace devilution

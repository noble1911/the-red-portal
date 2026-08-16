/**
 * @file runs_gems.cpp
 *
 * [RUNS MOD] Implementation of the D2-style gems. See runs_gems.h for the concept.
 */
#include "runs_gems.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "diablo_msg.hpp"
#include "engine/load_file.hpp"
#include "engine/palette.h"
#include "engine/random.hpp"
#include "engine/surface.hpp"
#include "inv.h"
#include "items.h"
#include "msg.h"
#include "player.h"
#include "runs_mod.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/surface_to_clx.hpp"

namespace devilution {

namespace {

// --- Effect table -------------------------------------------------------------------
//
// D2's numbers are on a completely different scale from Diablo's -- "+150 attack rating"
// is rounding error in D2 and "+150% to hit" would be absurd here -- so what is ported is
// each gem's *identity* (which stat, which slot, five ascending tiers), rescaled to
// Diablo's economy.
//
// Six of the 21 gem/slot cells name a stat Diablo simply does not have, and are
// substituted with the nearest thing that keeps the gem's character:
//
//   Sapphire, weapon  - no cold damage. Cold is D2's "drain and slow" gem, so it becomes
//                       mana burn: mana steal plus a mana bonus.
//   Emerald,  weapon  - no poison damage. Poison is D2's damage-over-time, so it becomes
//                       a flat damage bonus.
//   Emerald,  shield  - no poison resistance. Becomes flat damage reduction rather than
//                       duplicating another gem's resistance.
//   Topaz,    armour  - no magic find. Topaz is the "find more" gem, so it becomes light
//                       radius: see more of the level.
//   Diamond,  weapon  - no damage-versus-undead modifier (D1 only has one for demons).
//                       Becomes a straight percentage damage bonus.
//   Skull,    armour  - no mana regeneration stat. Becomes flat life and mana.
//
// Every number below is a tuning knob; nothing else in the file depends on the values.

struct GemBonus {
	int16_t toHit = 0;  // % chance to hit
	int16_t dam = 0;    // % damage
	int16_t damMod = 0; // flat damage added
	int16_t fireMin = 0;
	int16_t fireMax = 0;
	int16_t lightMin = 0;
	int16_t lightMax = 0;
	int16_t ac = 0;
	int16_t fr = 0;
	int16_t lr = 0;
	int16_t mr = 0;
	int16_t getHit = 0; // damage taken reduction (applied as a subtraction)
	int16_t str = 0;
	int16_t dex = 0;
	int16_t vit = 0;
	int16_t mag = 0;
	int16_t hp = 0;   // whole hit points; stored on the item in 1/64ths
	int16_t mana = 0; // whole mana points; stored on the item in 1/64ths
	int16_t light = 0;
	ItemSpecialEffect flags = ItemSpecialEffect::None;

	[[nodiscard]] bool isEmpty() const
	{
		return toHit == 0 && dam == 0 && damMod == 0 && fireMax == 0 && lightMax == 0
		    && ac == 0 && fr == 0 && lr == 0 && mr == 0 && getHit == 0
		    && str == 0 && dex == 0 && vit == 0 && mag == 0 && hp == 0 && mana == 0
		    && light == 0 && flags == ItemSpecialEffect::None;
	}
};

struct GemEffect {
	GemBonus weapon;
	GemBonus shield;
	GemBonus armor; // body armour and helms
};

using Tier = std::array<int16_t, NumGemQualities>;

// Amethyst: accuracy on a weapon, defence on a shield, raw strength on armour.
constexpr Tier AmethystToHit { 5, 8, 12, 16, 20 };
constexpr Tier AmethystAc { 4, 6, 9, 12, 16 };
constexpr Tier AmethystStr { 1, 2, 3, 4, 5 };

// Topaz: lightning damage, lightning resistance, and (standing in for magic find) light.
constexpr Tier TopazLightMax { 4, 8, 12, 18, 25 };
constexpr Tier TopazLr { 8, 12, 16, 20, 25 };
constexpr Tier TopazLight { 1, 1, 2, 2, 3 };

// Sapphire: mana burn on a weapon, magic resistance on a shield, mana on armour.
constexpr Tier SapphireWeaponMana { 5, 8, 12, 16, 20 };
constexpr Tier SapphireMr { 8, 12, 16, 20, 25 };
constexpr Tier SapphireMana { 7, 10, 14, 19, 24 };

// Emerald: flat damage on a weapon, damage reduction on a shield, dexterity on armour.
constexpr Tier EmeraldDamMod { 1, 2, 3, 5, 7 };
constexpr Tier EmeraldGetHit { 2, 3, 4, 5, 6 };
constexpr Tier EmeraldDex { 1, 2, 3, 4, 5 };

// Ruby: fire damage, fire resistance, hit points.
constexpr Tier RubyFireMax { 3, 6, 10, 14, 20 };
constexpr Tier RubyFr { 8, 12, 16, 20, 25 };
constexpr Tier RubyHp { 10, 17, 24, 31, 38 };

// Diamond: raw damage on a weapon, all resistances on a shield, accuracy on armour.
constexpr Tier DiamondDam { 10, 15, 20, 28, 35 };
constexpr Tier DiamondAllRes { 3, 5, 7, 10, 14 };
constexpr Tier DiamondToHit { 3, 5, 8, 11, 15 };

// Skull: the vampiric gem -- steals on a weapon, thorns on a shield, both pools on armour.
//
// The weapon slot carries a life bonus as well as the steal flag. Diablo's life steal is
// a pair of fixed-tier flags rather than a percentage, so a flag-only bonus had two bad
// properties: the five qualities collapsed onto two real outcomes, and the gem did
// nothing at all on a weapon that already stole life -- which is why a Perfect Skull
// would apply where a plain one silently refused. The numeric part means every tier
// always has something to give.
constexpr Tier SkullWeaponHp { 2, 3, 4, 5, 6 };
constexpr Tier SkullGetHit { 2, 3, 4, 5, 6 };
constexpr Tier SkullHp { 3, 5, 7, 9, 12 };
constexpr Tier SkullMana { 3, 5, 7, 9, 12 };

GemEffect GetGemEffect(GemType type, GemQuality quality)
{
	const auto q = static_cast<size_t>(quality);
	GemEffect e;
	switch (type) {
	case GemType::Amethyst:
		e.weapon.toHit = AmethystToHit[q];
		e.shield.ac = AmethystAc[q];
		e.armor.str = AmethystStr[q];
		break;
	case GemType::Topaz:
		e.weapon.lightMin = 1;
		e.weapon.lightMax = TopazLightMax[q];
		e.shield.lr = TopazLr[q];
		e.armor.light = TopazLight[q];
		break;
	case GemType::Sapphire:
		e.weapon.mana = SapphireWeaponMana[q];
		e.weapon.flags = q >= 3 ? ItemSpecialEffect::StealMana5 : ItemSpecialEffect::StealMana3;
		e.shield.mr = SapphireMr[q];
		e.armor.mana = SapphireMana[q];
		break;
	case GemType::Emerald:
		e.weapon.damMod = EmeraldDamMod[q];
		e.shield.getHit = EmeraldGetHit[q];
		e.armor.dex = EmeraldDex[q];
		break;
	case GemType::Ruby:
		e.weapon.fireMin = 1;
		e.weapon.fireMax = RubyFireMax[q];
		e.shield.fr = RubyFr[q];
		e.armor.hp = RubyHp[q];
		break;
	case GemType::Diamond:
		e.weapon.dam = DiamondDam[q];
		e.shield.fr = DiamondAllRes[q];
		e.shield.lr = DiamondAllRes[q];
		e.shield.mr = DiamondAllRes[q];
		e.armor.toHit = DiamondToHit[q];
		break;
	case GemType::Skull:
		e.weapon.hp = SkullWeaponHp[q];
		e.weapon.flags = q >= 3 ? ItemSpecialEffect::StealLife5 : ItemSpecialEffect::StealLife3;
		e.shield.flags = ItemSpecialEffect::Thorns;
		e.shield.getHit = SkullGetHit[q];
		e.armor.hp = SkullHp[q];
		e.armor.mana = SkullMana[q];
		break;
	}
	return e;
}

// Ceilings so that a stack of gems cannot run away with an item, in the same spirit as
// the oils refusing to push to-hit past their own limits.
constexpr int16_t MaxGemToHit = 60;
constexpr int16_t MaxGemDam = 100;
constexpr int16_t MaxGemDamMod = 30;
constexpr int16_t MaxGemAc = 100;
constexpr int16_t MaxGemResist = 75;
constexpr int16_t MaxGemStat = 20;
constexpr int16_t MaxGemHp = 100;   // whole points
constexpr int16_t MaxGemMana = 100; // whole points
constexpr int16_t MaxGemLight = 6;
constexpr int16_t MaxGemGetHit = 20;
constexpr int16_t MaxGemElemDam = 60;

/** Adds `add` to `field` without letting it past `cap`. Returns true if anything moved. */
bool AddCapped(int16_t &field, int add, int16_t cap)
{
	if (add == 0)
		return false;
	if (field >= cap)
		return false;
	field = static_cast<int16_t>(std::min<int>(field + add, cap));
	return true;
}

/** Applies one slot's bonus to the item. Returns false if nothing could be added,
 * which means the item is already saturated on everything this gem grants. */
bool ApplyBonus(Item &item, const GemBonus &bonus)
{
	bool changed = false;
	changed |= AddCapped(item._iPLToHit, bonus.toHit, MaxGemToHit);
	changed |= AddCapped(item._iPLDam, bonus.dam, MaxGemDam);
	changed |= AddCapped(item._iPLDamMod, bonus.damMod, MaxGemDamMod);
	changed |= AddCapped(item._iPLAC, bonus.ac, MaxGemAc);
	changed |= AddCapped(item._iPLFR, bonus.fr, MaxGemResist);
	changed |= AddCapped(item._iPLLR, bonus.lr, MaxGemResist);
	changed |= AddCapped(item._iPLMR, bonus.mr, MaxGemResist);
	changed |= AddCapped(item._iPLStr, bonus.str, MaxGemStat);
	changed |= AddCapped(item._iPLDex, bonus.dex, MaxGemStat);
	changed |= AddCapped(item._iPLVit, bonus.vit, MaxGemStat);
	changed |= AddCapped(item._iPLMag, bonus.mag, MaxGemStat);
	changed |= AddCapped(item._iPLLight, bonus.light, MaxGemLight);

	// Hit points and mana live on the item in 1/64ths (see IPL_LIFE).
	if (bonus.hp != 0 && item._iPLHP < (MaxGemHp << 6)) {
		item._iPLHP = static_cast<int16_t>(std::min<int>(item._iPLHP + (bonus.hp << 6), MaxGemHp << 6));
		changed = true;
	}
	if (bonus.mana != 0 && item._iPLMana < (MaxGemMana << 6)) {
		item._iPLMana = static_cast<int16_t>(std::min<int>(item._iPLMana + (bonus.mana << 6), MaxGemMana << 6));
		changed = true;
	}

	// Damage taken reduction is stored negated (see IPL_GETHIT).
	if (bonus.getHit != 0 && item._iPLGetHit > -MaxGemGetHit) {
		item._iPLGetHit = static_cast<int16_t>(std::max<int>(item._iPLGetHit - bonus.getHit, -MaxGemGetHit));
		changed = true;
	}

	// Elemental damage is a min/max pair; grow the top end and keep the floor at 1.
	if (bonus.fireMax != 0 && item._iFMaxDam < MaxGemElemDam) {
		item._iFMinDam = std::max<int16_t>(item._iFMinDam, bonus.fireMin);
		item._iFMaxDam = static_cast<int16_t>(std::min<int>(item._iFMaxDam + bonus.fireMax, MaxGemElemDam));
		changed = true;
	}
	if (bonus.lightMax != 0 && item._iLMaxDam < MaxGemElemDam) {
		item._iLMinDam = std::max<int16_t>(item._iLMinDam, bonus.lightMin);
		item._iLMaxDam = static_cast<int16_t>(std::min<int>(item._iLMaxDam + bonus.lightMax, MaxGemElemDam));
		changed = true;
	}

	if (bonus.flags != ItemSpecialEffect::None && !HasAllOf(item._iFlags, bonus.flags)) {
		item._iFlags |= bonus.flags;
		changed = true;
	}
	return changed;
}

const char *GemTypeName(GemType type)
{
	switch (type) {
	case GemType::Amethyst:
		return N_("Amethyst");
	case GemType::Topaz:
		return N_("Topaz");
	case GemType::Sapphire:
		return N_("Sapphire");
	case GemType::Emerald:
		return N_("Emerald");
	case GemType::Ruby:
		return N_("Ruby");
	case GemType::Diamond:
		return N_("Diamond");
	case GemType::Skull:
		return N_("Skull");
	}
	return "";
}

/** Base gold value per quality; the type does not change what a gem is worth. */
constexpr std::array<int, NumGemQualities> GemValues { 250, 750, 2500, 7500, 15000 };

// [RUNS MOD] Which gem is on the cursor. The gem itself is consumed the moment it is
// used (UseInvItem removes it before UseItem runs), so the type and quality have to be
// held here until the follow-up click lands on a target. Session-only: the oil cursor
// this rides on is already cleared on level load, so a pending gem never outlives it.
GemType PendingGemType = GemType::Amethyst;
GemQuality PendingGemQuality = GemQuality::Chipped;
/** Inventory/belt slot the pending gem came from, so a success spends the right one. */
int8_t GemSourceSlot = -1;

/** Renders a bonus as compact text.
 *
 * Deliberately terse: the info box is a fixed narrow strip (see the oils, which hand-split
 * their descriptions across two short lines), and anything much longer than about thirty
 * characters runs past the edge and overlaps the next line.
 */
std::string BonusText(const GemBonus &bonus)
{
	std::string text;
	const auto add = [&text](std::string_view part) {
		if (!text.empty())
			text.append(", ");
		text.append(part);
	};
	if (bonus.toHit != 0)
		add(fmt::format(fmt::runtime(_("+{:d}% to hit")), bonus.toHit));
	if (bonus.dam != 0)
		add(fmt::format(fmt::runtime(_("+{:d}% dam")), bonus.dam));
	if (bonus.damMod != 0)
		add(fmt::format(fmt::runtime(_("+{:d} dam")), bonus.damMod));
	if (bonus.fireMax != 0)
		add(fmt::format(fmt::runtime(_("{:d}-{:d} fire dam")), bonus.fireMin, bonus.fireMax));
	if (bonus.lightMax != 0)
		add(fmt::format(fmt::runtime(_("{:d}-{:d} lightning dam")), bonus.lightMin, bonus.lightMax));
	if (bonus.ac != 0)
		add(fmt::format(fmt::runtime(_("+{:d} armor")), bonus.ac));
	if (bonus.fr != 0 && bonus.fr == bonus.lr && bonus.fr == bonus.mr) {
		add(fmt::format(fmt::runtime(_("+{:d}% all resist")), bonus.fr));
	} else {
		if (bonus.fr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% fire resist")), bonus.fr));
		if (bonus.lr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% lightn resist")), bonus.lr));
		if (bonus.mr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% magic resist")), bonus.mr));
	}
	if (bonus.getHit != 0)
		add(fmt::format(fmt::runtime(_("-{:d} dam taken")), bonus.getHit));
	if (bonus.str != 0)
		add(fmt::format(fmt::runtime(_("+{:d} Str")), bonus.str));
	if (bonus.dex != 0)
		add(fmt::format(fmt::runtime(_("+{:d} Dex")), bonus.dex));
	if (bonus.vit != 0)
		add(fmt::format(fmt::runtime(_("+{:d} Vit")), bonus.vit));
	if (bonus.mag != 0)
		add(fmt::format(fmt::runtime(_("+{:d} Mag")), bonus.mag));
	if (bonus.hp != 0)
		add(fmt::format(fmt::runtime(_("+{:d} life")), bonus.hp));
	if (bonus.mana != 0)
		add(fmt::format(fmt::runtime(_("+{:d} mana")), bonus.mana));
	if (bonus.light != 0)
		add(fmt::format(fmt::runtime(_("+{:d} light")), bonus.light));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::StealLife3 | ItemSpecialEffect::StealLife5))
		add(_("steals life"));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::StealMana3 | ItemSpecialEffect::StealMana5))
		add(_("steals mana"));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::Thorns))
		add(_("thorns"));
	return text;
}

void AppendBonusLine(std::string_view slot, const GemBonus &bonus, std::vector<std::string> &out)
{
	if (bonus.isEmpty())
		return;
	out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a slot name, then its bonuses */ "{:s}: {:s}")), slot, BonusText(bonus)));
}

// --- Icons --------------------------------------------------------------------------
//
// The art ships as true RGB (assets/data/inv/gems.dat, built from the D2 gem icons)
// rather than as palette indices, because Diablo has no single palette to index into:
// every dungeon type loads its own, and indices 1-31 are cycled for the lava and water
// animations. So the colours are matched against whatever palette is live, avoiding the
// cycled range, and rebuilt whenever the palette changes.

constexpr char GemAssetPath[] = "data\\inv\\gems.dat";
/** Palette indices below this are animated (see palette_update_caves) or reserved. */
constexpr int FirstStablePaletteIndex = 32;

/** Format version this build understands (see the converter in scripts/). */
constexpr uint8_t GemAssetVersion = 2;

struct GemArt {
	uint8_t width = 0;
	uint8_t height = 0;
	// 0 = transparent, else colour table index + 1. 16-bit because the 35 gems together
	// use ~974 distinct colours -- a byte index cannot address them.
	std::unique_ptr<uint16_t[]> pixels;
};

std::array<GemArt, NumGems> GemArtwork;
std::vector<SDL_Color> GemArtColors;
bool GemArtLoaded = false;
bool GemArtMissing = false;

std::array<OptionalOwnedClxSpriteList, NumGems> GemSprites;
unsigned GemSpritesPaletteGeneration = 0;
bool GemSpritesBuilt = false;

/** Reads gems.dat into GemArtwork/GemArtColors. Safe to call repeatedly. */
void LoadGemArt()
{
	if (GemArtLoaded || GemArtMissing)
		return;

	size_t size = 0;
	std::unique_ptr<uint8_t[]> data = LoadFileInMem<uint8_t>(GemAssetPath, &size);
	if (data == nullptr || size < 8 || data[0] != 'D' || data[1] != 'X' || data[2] != 'G' || data[3] != 'M') {
		LogError("Gem art {} missing or malformed; gems will be invisible", GemAssetPath);
		GemArtMissing = true;
		return;
	}

	if (data[4] != GemAssetVersion) {
		LogError("Gem art {} is version {}, expected {}", GemAssetPath, data[4], GemAssetVersion);
		GemArtMissing = true;
		return;
	}
	const int spriteCount = data[5];
	const int colorCount = static_cast<int>(data[6]) | (static_cast<int>(data[7]) << 8);
	size_t offset = 8;
	if (spriteCount != NumGems || offset + static_cast<size_t>(colorCount) * 3 > size) {
		LogError("Gem art {} has {} sprites, expected {}", GemAssetPath, spriteCount, NumGems);
		GemArtMissing = true;
		return;
	}

	GemArtColors.resize(colorCount);
	for (int i = 0; i < colorCount; i++) {
		GemArtColors[i] = SDL_Color { data[offset], data[offset + 1], data[offset + 2], 255 };
		offset += 3;
	}

	for (int i = 0; i < NumGems; i++) {
		if (offset + 2 > size) {
			GemArtMissing = true;
			return;
		}
		const uint8_t w = data[offset++];
		const uint8_t h = data[offset++];
		const size_t pixelCount = static_cast<size_t>(w) * h;
		if (offset + pixelCount * 2 > size) {
			GemArtMissing = true;
			return;
		}
		GemArtwork[i].width = w;
		GemArtwork[i].height = h;
		GemArtwork[i].pixels = std::make_unique<uint16_t[]>(pixelCount);
		for (size_t p = 0; p < pixelCount; p++) {
			GemArtwork[i].pixels[p] = static_cast<uint16_t>(
			    static_cast<int>(data[offset + p * 2]) | (static_cast<int>(data[offset + p * 2 + 1]) << 8));
		}
		offset += pixelCount * 2;
	}
	GemArtLoaded = true;
}

/** Nearest palette entry to `color`, skipping the animated low indices.
 *
 * Uses the "redmean" approximation rather than a luminance-weighted distance. Weighting
 * green heavily (as the eye does) is right for photographs and wrong for this: it lets a
 * colour match on brightness while its hue drifts, so saturated gem colours all collapsed
 * onto whichever ramp happened to sit at the same luminance. Redmean keeps hue.
 */
uint8_t NearestPaletteIndex(SDL_Color color)
{
	int best = FirstStablePaletteIndex;
	int64_t bestDistance = std::numeric_limits<int64_t>::max();
	for (int i = FirstStablePaletteIndex; i < 256; i++) {
		const SDL_Color &candidate = logical_palette[i];
		const int rmean = (static_cast<int>(color.r) + candidate.r) / 2;
		const int dr = static_cast<int>(color.r) - candidate.r;
		const int dg = static_cast<int>(color.g) - candidate.g;
		const int db = static_cast<int>(color.b) - candidate.b;
		const int64_t distance = ((512 + rmean) * dr * dr) / 256
		    + 4LL * dg * dg
		    + ((767 - rmean) * db * db) / 256;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = i;
		}
	}
	return static_cast<uint8_t>(best);
}

void BuildGemSprites()
{
	LoadGemArt();
	if (!GemArtLoaded)
		return;

	// Map the art's colour table onto the live palette once, then paint by lookup.
	std::vector<uint8_t> colorMap(GemArtColors.size());
	for (size_t i = 0; i < GemArtColors.size(); i++)
		colorMap[i] = NearestPaletteIndex(GemArtColors[i]);

	for (int i = 0; i < NumGems; i++) {
		const GemArt &art = GemArtwork[i];
		if (art.width == 0 || art.height == 0)
			continue;
		OwnedSurface surface { art.width, art.height };
		for (int y = 0; y < art.height; y++) {
			for (int x = 0; x < art.width; x++) {
				const uint16_t value = art.pixels[static_cast<size_t>(y) * art.width + x];
				*surface.at(x, y) = value == 0 ? 0 : colorMap[value - 1];
			}
		}
		GemSprites[i].emplace(SurfaceToClx(surface, 1, 0));
	}
	GemSpritesBuilt = true;
	GemSpritesPaletteGeneration = PaletteGeneration;
}

/** Rebuilds the sprites if the palette has moved on since they were made. */
void EnsureGemSprites()
{
	if (GemSpritesBuilt && GemSpritesPaletteGeneration == PaletteGeneration)
		return;
	BuildGemSprites();
}

} // namespace

GemType GemTypeFromBuff(uint32_t dwBuff)
{
	return static_cast<GemType>(std::clamp<int>((dwBuff & CF_GEMITEMTYPE) >> 14, 0, NumGemTypes - 1));
}

GemQuality GemQualityFromBuff(uint32_t dwBuff)
{
	return static_cast<GemQuality>(std::clamp<int>((dwBuff & CF_GEMITEMQUALITY) >> 17, 0, NumGemQualities - 1));
}

GemType GemTypeOf(const Item &item)
{
	return GemTypeFromBuff(item.dwBuff);
}

GemQuality GemQualityOf(const Item &item)
{
	return GemQualityFromBuff(item.dwBuff);
}

uint32_t MakeGemBuff(GemType type, GemQuality quality)
{
	return CF_ISGEM
	    | (static_cast<uint32_t>(type) << 14)
	    | (static_cast<uint32_t>(quality) << 17);
}

bool IsGem(const Item &item)
{
	return item._iMiscId == IMISC_GEM;
}

int GemRequiredDepth(GemQuality quality)
{
	// Quality q first becomes droppable at depth 5q (see DropRandomGem), so the use
	// requirement is the same number: you may use what you could have earned.
	return 5 * static_cast<int>(quality);
}

bool CanUseGem(const Player &player, const Item &item)
{
	if (!RunsModEnabled || !IsGem(item))
		return true;
	return static_cast<int>(player.pDeepestBeatenDepth) >= GemRequiredDepth(GemQualityOf(item));
}

std::string GemName(GemType type, GemQuality quality)
{
	const std::string_view typeName = _(GemTypeName(type));
	switch (quality) {
	case GemQuality::Chipped:
		return fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a gem type, e.g. Ruby */ "Chipped {:s}")), typeName);
	case GemQuality::Flawed:
		return fmt::format(fmt::runtime(_("Flawed {:s}")), typeName);
	case GemQuality::Normal:
		return std::string(typeName);
	case GemQuality::Flawless:
		return fmt::format(fmt::runtime(_("Flawless {:s}")), typeName);
	case GemQuality::Perfect:
		return fmt::format(fmt::runtime(_("Perfect {:s}")), typeName);
	}
	return std::string(typeName);
}

void GetGemEffectLines(GemType type, GemQuality quality, std::vector<std::string> &out)
{
	const GemEffect effect = GetGemEffect(type, quality);
	AppendBonusLine(_(/* TRANSLATORS: gem tooltip slot label */ "Weapon"), effect.weapon, out);
	AppendBonusLine(_(/* TRANSLATORS: gem tooltip slot label */ "Shield"), effect.shield, out);
	AppendBonusLine(_(/* TRANSLATORS: gem tooltip slot label */ "Armor"), effect.armor, out);
}

void CreateGem(Item &item, GemType type, GemQuality quality)
{
	// [RUNS MOD] Type and quality live in the create-info word, which is already saved
	// and network-synced, so gems need no new item fields -- the same trick the dungeon
	// scrolls use for their level. RecreateItem rebuilds the icon and price from it.
	InitializeItem(item, IDI_GEM);
	GenerateNewSeed(item);
	// Identity goes in dwBuff, and the create-info is left at zero: a gem has no item
	// level, no affix roll and no vendor provenance, and every one of those is what
	// _iCreateInfo actually means. InitializeItem may have set CF_HELLFIRE, so OR in
	// rather than assign.
	item._iCreateInfo = 0;
	item.dwBuff |= MakeGemBuff(type, quality);
	item._iCurs = static_cast<uint16_t>(GemCurs(type, quality));
	item._iIdentified = true;
	item._ivalue = GemValues[static_cast<size_t>(quality)];
	item._iIvalue = item._ivalue;
	// Without this the item stays at the zero-initialised _iStatFlag = false, and
	// DrawItem renders anything "unusable" through the infravision TRN -- which maps the
	// whole sprite onto a single red ramp, so every gem came out as a flat red blob.
	// Rolled items get this from SetupItem; gems are built directly, so do it here.
	if (MyPlayer != nullptr)
		item.updateRequiredStatsCacheForPlayer(*MyPlayer);
}

void DropRandomGem(int depth, Point position, bool sendmsg)
{
	if (ActiveItemCount >= MAXITEMS)
		return;

	const auto type = static_cast<GemType>(GenerateRnd(NumGemTypes));

	// [RUNS MOD] Quality is gated on depth so Perfect gems stay a deep-run prize: the
	// window of reachable tiers opens as you descend, and the roll is weighted towards
	// the bottom of that window so the top tier stays rare even once it is reachable.
	const int maxQuality = std::clamp(depth / 5, 0, NumGemQualities - 1);
	int rolled = GenerateRnd(maxQuality + 1);
	if (rolled > 0 && GenerateRnd(100) < 50)
		rolled--; // second chance to fall a tier: the good ones should hurt to get
	const auto quality = static_cast<GemQuality>(rolled);

	const int ii = AllocateItem();
	Item &item = Items[ii];
	CreateGem(item, type, quality);
	GetSuperItemSpace(position, static_cast<int8_t>(ii));
	item.setNewAnimation(MyPlayer != nullptr && MyPlayer->pLvlLoad == 0);
	// [RUNS MOD] CMD_SPAWNITEM, not CMD_DROPITEM, and the difference is load-bearing.
	//
	// OnDropItem does nothing but DeltaPutItem: it records the item so it comes back on
	// level re-entry, and never places it in the receiving client's world. Vanilla can
	// use it for monster loot because SpawnLoot runs on EVERY client, seeded from
	// monster.rndItemSeed, so each one materialises the identical item locally and only
	// needs the delta entry. The mod's drops are not like that -- they are rolled inside
	// MonsterDeath's `sendmsg` block, which is the killer's client alone. Announced with
	// CMD_DROPITEM they existed on exactly one floor, so peers could not see them, and a
	// pickup by anyone else stalled in OnRequestGetItem (the level owner has no such
	// item, so it retries until NetSendCmdReq2's 5s expiry) after the requesting client
	// had already removed the item locally -- loot that vanished on being touched.
	//
	// OnSpawnItem is a superset: SyncDropItem on peers, plus PutItemRecord and
	// DeltaPutItem. So this replaces the drop message rather than joining it; sending
	// both would write the delta twice for one item.
	if (sendmsg)
		NetSendCmdPItem(false, CMD_SPAWNITEM, item.position, item);
}

bool HasGem(const Item &item)
{
	return RunsModEnabled && (item.dwBuff & CF_GEMMED) != 0;
}

GemType FusedGemType(const Item &item)
{
	return static_cast<GemType>(std::clamp<int>((item.dwBuff & CF_GEMTYPE) >> 7, 0, NumGemTypes - 1));
}

GemQuality FusedGemQuality(const Item &item)
{
	return static_cast<GemQuality>(std::clamp<int>((item.dwBuff & CF_GEMQUALITY) >> 10, 0, NumGemQualities - 1));
}

/** Records which gem this item holds, without applying anything. */
void StoreGemOnItem(Item &item, GemType type, GemQuality quality)
{
	item.dwBuff &= ~(CF_GEMTYPE | CF_GEMQUALITY);
	item.dwBuff |= CF_GEMMED
	    | (static_cast<uint32_t>(type) << 7)
	    | (static_cast<uint32_t>(quality) << 10);
}

/** The bonus a stored gem grants this item, chosen by the item's class. */
const GemBonus &BonusForItem(const GemEffect &effect, const Item &item)
{
	if (item._iClass == ICLASS_WEAPON)
		return effect.weapon;
	return item._itype == ItemType::Shield ? effect.shield : effect.armor;
}

void ReapplyFusedGem(Item &item)
{
	if (!HasGem(item))
		return;
	const GemEffect effect = GetGemEffect(FusedGemType(item), FusedGemQuality(item));
	ApplyBonus(item, BonusForItem(effect, item));
}

void GetFusedGemLines(const Item &item, std::vector<std::string> &out)
{
	if (!HasGem(item))
		return;
	const GemType type = FusedGemType(item);
	const GemQuality quality = FusedGemQuality(item);
	out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a gem name */ "{:s} fused")), GemName(type, quality)));
	const GemEffect effect = GetGemEffect(type, quality);
	const std::string text = BonusText(BonusForItem(effect, item));
	if (!text.empty())
		out.push_back(text);
}

bool GemTargetingActive = false;

void BeginGemTargeting(const Item &gem, int8_t sourceSlot)
{
	PendingGemType = GemTypeOf(gem);
	PendingGemQuality = GemQualityOf(gem);
	GemSourceSlot = sourceSlot;
	GemTargetingActive = true;
}

void CancelGemTargeting()
{
	GemTargetingActive = false;
	GemSourceSlot = -1;
}

bool DoGem(Player &player, int cii)
{
	// Explicit slot mapping, for the same reason DoPreserve does its own: with the
	// extended inventory, belt ids would index InvList out of range.
	Item *target;
	if (cii < INVITEM_INV_FIRST)
		target = &player.InvBody[cii];
	else if (cii <= INVITEM_INV_LAST)
		target = &player.InvList[cii - INVITEM_INV_FIRST];
	else
		target = &player.SpdList[cii - INVITEM_BELT_FIRST];

	if (target->isEmpty())
		return false;
	// Gems fuse into gear, not into consumables, gold or quest items -- the same
	// classes the oils refuse.
	if (target->_iClass != ICLASS_WEAPON && target->_iClass != ICLASS_ARMOR) {
		InitDiabloMsg(_("Only weapons and armor can hold a gem."));
		return false;
	}
	// One gem per item: a socket, not a stack. Without this the caps were the only
	// brake, so an item could quietly absorb a dozen gems and the description could
	// only ever name the last one.
	if (HasGem(*target)) {
		InitDiabloMsg(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a gem name */ "This item already holds a {:s}.")),
		    GemName(FusedGemType(*target), FusedGemQuality(*target))));
		return false;
	}

	const GemEffect effect = GetGemEffect(PendingGemType, PendingGemQuality);
	const GemBonus &bonus = BonusForItem(effect, *target);

	if (bonus.isEmpty()) {
		InitDiabloMsg(_("That gem does nothing in this kind of item."));
		return false;
	}
	if (!ApplyBonus(*target, bonus)) {
		InitDiabloMsg(_("This item is already at its limit for that."));
		return false;
	}
	StoreGemOnItem(*target, PendingGemType, PendingGemQuality);

	target->_iIdentified = true;
	target->updateRequiredStatsCacheForPlayer(player);

	// Spend the gem that opened this targeting; if its slot went stale (the inventory
	// moved under us), fall back to any matching gem, mirroring DoPreserve.
	const GemType spendType = PendingGemType;
	const GemQuality spendQuality = PendingGemQuality;
	bool spent = false;
	if (GemSourceSlot >= INVITEM_BELT_FIRST && GemSourceSlot <= INVITEM_BELT_LAST) {
		const int idx = GemSourceSlot - INVITEM_BELT_FIRST;
		if (!player.SpdList[idx].isEmpty() && IsGem(player.SpdList[idx])) {
			player.RemoveSpdBarItem(idx);
			spent = true;
		}
	} else if (GemSourceSlot >= INVITEM_INV_FIRST && GemSourceSlot <= INVITEM_INV_LAST) {
		const int idx = GemSourceSlot - INVITEM_INV_FIRST;
		if (idx < player._pNumInv && IsGem(player.InvList[idx])) {
			player.RemoveInvItem(idx);
			spent = true;
		}
	}
	if (!spent) {
		RemoveInventoryOrBeltItem(player, [spendType, spendQuality](const Item &item) {
			return IsGem(item) && GemTypeOf(item) == spendType && GemQualityOf(item) == spendQuality;
		});
	}

	CalcPlrInv(player, true);
	CancelGemTargeting();
	return true;
}

void RefreshGemSprites()
{
	BuildGemSprites();
}

std::string DumpGemPaletteMapping(const char *path)
{
	// [RUNS MOD] Diagnostic: writes the live palette and the source-colour -> palette
	// index mapping so the quantiser's actual behaviour can be inspected offline. The
	// palette Diablo uses in game is not in the repo (it lives in the MPQs and changes
	// per dungeon type), so this is the only way to see what the matcher is really given.
	LoadGemArt();
	if (!GemArtLoaded)
		return "gem art not loaded";

	std::string out;
	out.append(StrCat("palette_generation ", PaletteGeneration, "\n"));
	out.append("# index r g b\n");
	for (int i = 0; i < 256; i++) {
		out.append(StrCat("PAL ", i, " ", logical_palette[i].r, " ",
		    logical_palette[i].g, " ", logical_palette[i].b, "\n"));
	}
	out.append("# srcR srcG srcB -> index palR palG palB\n");
	for (size_t i = 0; i < GemArtColors.size(); i++) {
		const SDL_Color c = GemArtColors[i];
		const uint8_t idx = NearestPaletteIndex(c);
		out.append(StrCat("MAP ", c.r, " ", c.g, " ", c.b, " -> ", idx, " ",
		    logical_palette[idx].r, " ", logical_palette[idx].g, " ", logical_palette[idx].b, "\n"));
	}

	FILE *f = std::fopen(path, "wb");
	if (f == nullptr)
		return StrCat("could not open ", path);
	std::fwrite(out.data(), 1, out.size(), f);
	std::fclose(f);
	return StrCat("wrote ", out.size(), " bytes to ", path);
}

void FreeGemSprites()
{
	for (auto &sprite : GemSprites)
		sprite = std::nullopt;
	GemSpritesBuilt = false;
}

ClxSprite GetGemSprite(int curs)
{
	EnsureGemSprites();
	const int index = std::clamp(curs - GemCursFirst, 0, NumGems - 1);
	return (*GemSprites[index])[0];
}

} // namespace devilution

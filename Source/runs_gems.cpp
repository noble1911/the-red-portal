/**
 * @file runs_gems.cpp
 *
 * [RUNS MOD] Implementation of the D2-style gems. See runs_gems.h for the concept.
 */
#include "runs_gems.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

/** Renders one bonus as a tooltip line. Mirrors the wording of the item stat lines. */
void AppendBonusLine(std::string_view slot, const GemBonus &bonus, std::vector<std::string> &out)
{
	if (bonus.isEmpty())
		return;
	std::string text;
	const auto add = [&text](std::string_view part) {
		if (!text.empty())
			text.append(", ");
		text.append(part);
	};
	if (bonus.toHit != 0)
		add(fmt::format(fmt::runtime(_("+{:d}% to hit")), bonus.toHit));
	if (bonus.dam != 0)
		add(fmt::format(fmt::runtime(_("+{:d}% damage")), bonus.dam));
	if (bonus.damMod != 0)
		add(fmt::format(fmt::runtime(_("+{:d} damage")), bonus.damMod));
	if (bonus.fireMax != 0)
		add(fmt::format(fmt::runtime(_("{:d}-{:d} fire damage")), bonus.fireMin, bonus.fireMax));
	if (bonus.lightMax != 0)
		add(fmt::format(fmt::runtime(_("{:d}-{:d} lightning damage")), bonus.lightMin, bonus.lightMax));
	if (bonus.ac != 0)
		add(fmt::format(fmt::runtime(_("+{:d} armor class")), bonus.ac));
	if (bonus.fr != 0 && bonus.fr == bonus.lr && bonus.fr == bonus.mr) {
		add(fmt::format(fmt::runtime(_("+{:d}% all resistances")), bonus.fr));
	} else {
		if (bonus.fr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% fire resistance")), bonus.fr));
		if (bonus.lr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% lightning resistance")), bonus.lr));
		if (bonus.mr != 0)
			add(fmt::format(fmt::runtime(_("+{:d}% magic resistance")), bonus.mr));
	}
	if (bonus.getHit != 0)
		add(fmt::format(fmt::runtime(_("-{:d} damage from enemies")), bonus.getHit));
	if (bonus.str != 0)
		add(fmt::format(fmt::runtime(_("+{:d} strength")), bonus.str));
	if (bonus.dex != 0)
		add(fmt::format(fmt::runtime(_("+{:d} dexterity")), bonus.dex));
	if (bonus.vit != 0)
		add(fmt::format(fmt::runtime(_("+{:d} vitality")), bonus.vit));
	if (bonus.mag != 0)
		add(fmt::format(fmt::runtime(_("+{:d} magic")), bonus.mag));
	if (bonus.hp != 0)
		add(fmt::format(fmt::runtime(_("+{:d} life")), bonus.hp));
	if (bonus.mana != 0)
		add(fmt::format(fmt::runtime(_("+{:d} mana")), bonus.mana));
	if (bonus.light != 0)
		add(fmt::format(fmt::runtime(_("+{:d} light radius")), bonus.light));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::StealLife3 | ItemSpecialEffect::StealLife5))
		add(_("steals life"));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::StealMana3 | ItemSpecialEffect::StealMana5))
		add(_("steals mana"));
	if (HasAnyOf(bonus.flags, ItemSpecialEffect::Thorns))
		add(_("attacker takes damage"));

	out.push_back(fmt::format(fmt::runtime(_(/* TRANSLATORS: {:s} is a slot name, then its bonuses */ "{:s}: {:s}")), slot, text));
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

struct GemArt {
	uint8_t width = 0;
	uint8_t height = 0;
	std::unique_ptr<uint8_t[]> pixels; // 0 = transparent, else colour table index + 1
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

	const int spriteCount = data[5];
	const int colorCount = data[6];
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
		if (offset + pixelCount > size) {
			GemArtMissing = true;
			return;
		}
		GemArtwork[i].width = w;
		GemArtwork[i].height = h;
		GemArtwork[i].pixels = std::make_unique<uint8_t[]>(pixelCount);
		std::copy_n(&data[offset], pixelCount, GemArtwork[i].pixels.get());
		offset += pixelCount;
	}
	GemArtLoaded = true;
}

/** Nearest palette entry to `color`, skipping the animated low indices. */
uint8_t NearestPaletteIndex(SDL_Color color)
{
	int best = FirstStablePaletteIndex;
	int bestDistance = std::numeric_limits<int>::max();
	for (int i = FirstStablePaletteIndex; i < 256; i++) {
		const SDL_Color &candidate = logical_palette[i];
		const int dr = static_cast<int>(color.r) - candidate.r;
		const int dg = static_cast<int>(color.g) - candidate.g;
		const int db = static_cast<int>(color.b) - candidate.b;
		// Weighted to favour green, which the eye resolves best.
		const int distance = dr * dr * 2 + dg * dg * 4 + db * db;
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
				const uint8_t value = art.pixels[static_cast<size_t>(y) * art.width + x];
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

GemType GemTypeFromCreateInfo(uint16_t createInfo)
{
	return static_cast<GemType>(std::clamp(createInfo & 0x0F, 0, NumGemTypes - 1));
}

GemQuality GemQualityFromCreateInfo(uint16_t createInfo)
{
	return static_cast<GemQuality>(std::clamp((createInfo >> 4) & 0x07, 0, NumGemQualities - 1));
}

GemType GemTypeOf(const Item &item)
{
	return GemTypeFromCreateInfo(item._iCreateInfo);
}

GemQuality GemQualityOf(const Item &item)
{
	return GemQualityFromCreateInfo(item._iCreateInfo);
}

uint16_t MakeGemCreateInfo(GemType type, GemQuality quality)
{
	return static_cast<uint16_t>((static_cast<int>(type) & 0x0F) | ((static_cast<int>(quality) & 0x07) << 4));
}

bool IsGem(const Item &item)
{
	return item._iMiscId == IMISC_GEM;
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
	item._iCreateInfo = MakeGemCreateInfo(type, quality);
	item._iCurs = static_cast<uint16_t>(GemCurs(type, quality));
	item._iIdentified = true;
	item._ivalue = GemValues[static_cast<size_t>(quality)];
	item._iIvalue = item._ivalue;
}

bool ShouldDropGem(int depth)
{
	// [RUNS MOD] Roughly one gem every twenty kills at depth 1, climbing with depth so
	// deeper runs feel like the place to farm them. Uses the shared game RNG, so it is
	// already level-owner gated wherever monster loot is rolled.
	const int chance = std::min(2 + depth / 2, 12);
	return GenerateRnd(100) < chance;
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
	if (sendmsg)
		NetSendCmdPItem(false, CMD_DROPITEM, item.position, item);
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
		InitDiabloMsg(_("Only weapons and armor can take a gem."));
		return false;
	}

	const GemEffect effect = GetGemEffect(PendingGemType, PendingGemQuality);
	const GemBonus &bonus = target->_iClass == ICLASS_WEAPON
	    ? effect.weapon
	    : (target->_itype == ItemType::Shield ? effect.shield : effect.armor);

	if (bonus.isEmpty() || !ApplyBonus(*target, bonus)) {
		InitDiabloMsg(_("That gem has nothing to give this item."));
		return false;
	}

	target->_iIdentified = true;
	target->updateRequiredStatsCacheForPlayer(player);

	// Spend the gem that opened this targeting; if its slot went stale (the inventory
	// moved under us), fall back to any matching gem, mirroring DoPreserve.
	const uint16_t createInfo = MakeGemCreateInfo(PendingGemType, PendingGemQuality);
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
		RemoveInventoryOrBeltItem(player, [createInfo](const Item &item) {
			return IsGem(item) && item._iCreateInfo == createInfo;
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

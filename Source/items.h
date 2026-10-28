/**
 * @file items.h
 *
 * Interface of item functionality.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "DiabloUI/ui_flags.hpp"
#include "cursor.h"
#include "engine/animationinfo.h"
#include "engine/point.hpp"
#include "engine/surface.hpp"
#include "levels/dun_tile.hpp"
#include "monster.h"
#include "tables/itemdat.h"
#include "utils/is_of.hpp"
#include "utils/string_or_view.hpp"

namespace devilution {

#define MAXITEMS 127
#define ITEMTYPES 43

#define GOLD_SMALL_LIMIT 1000
#define GOLD_MEDIUM_LIMIT 2500
#define GOLD_MAX_LIMIT 5000

// Item indestructible durability
#define DUR_INDESTRUCTIBLE 255

constexpr int ItemNameLength = 64;
constexpr int MaxVendorValue = 140000;
constexpr int MaxVendorValueHf = 200000;
constexpr int MaxBoyValue = 90000;
constexpr int MaxBoyValueHf = 200000;

enum item_quality : uint8_t {
	ITEM_QUALITY_NORMAL,
	ITEM_QUALITY_MAGIC,
	ITEM_QUALITY_UNIQUE,
};

enum _unique_items : int32_t {
	UITEM_CLEAVER,
	UITEM_SKCROWN,
	UITEM_INFRARING,
	UITEM_OPTAMULET,
	UITEM_TRING,
	UITEM_HARCREST,
	UITEM_STEELVEIL,
	UITEM_ARMOFVAL,
	UITEM_GRISWOLD,
	UITEM_BOVINE,
	UITEM_RIFTBOW,
	UITEM_NEEDLER,
	UITEM_CELESTBOW,
	UITEM_DEADLYHUNT,
	UITEM_BOWOFDEAD,
	UITEM_BLKOAKBOW,
	UITEM_FLAMEDART,
	UITEM_FLESHSTING,
	UITEM_WINDFORCE,
	UITEM_EAGLEHORN,
	UITEM_GONNAGALDIRK,
	UITEM_DEFENDER,
	UITEM_GRYPHONCLAW,
	UITEM_BLACKRAZOR,
	UITEM_GIBBOUSMOON,
	UITEM_ICESHANK,
	UITEM_EXECUTIONER,
	UITEM_BONESAW,
	UITEM_SHADHAWK,
	UITEM_WIZSPIKE,
	UITEM_LGTSABRE,
	UITEM_FALCONTALON,
	UITEM_INFERNO,
	UITEM_DOOMBRINGER,
	UITEM_GRIZZLY,
	UITEM_GRANDFATHER,
	UITEM_MANGLER,
	UITEM_SHARPBEAK,
	UITEM_BLOODLSLAYER,
	UITEM_CELESTAXE,
	UITEM_WICKEDAXE,
	UITEM_STONECLEAV,
	UITEM_AGUHATCHET,
	UITEM_HELLSLAYER,
	UITEM_MESSERREAVER,
	UITEM_CRACKRUST,
	UITEM_JHOLMHAMM,
	UITEM_CIVERBS,
	UITEM_CELESTSTAR,
	UITEM_BARANSTAR,
	UITEM_GNARLROOT,
	UITEM_CRANBASH,
	UITEM_SCHAEFHAMM,
	UITEM_DREAMFLANGE,
	UITEM_STAFFOFSHAD,
	UITEM_IMMOLATOR,
	UITEM_STORMSPIRE,
	UITEM_GLEAMSONG,
	UITEM_THUNDERCALL,
	UITEM_PROTECTOR,
	UITEM_NAJPUZZLE,
	UITEM_MINDCRY,
	UITEM_RODOFONAN,
	UITEM_SPIRITSHELM,
	UITEM_THINKINGCAP,
	UITEM_OVERLORDHELM,
	UITEM_FOOLSCREST,
	UITEM_GOTTERDAM,
	UITEM_ROYCIRCLET,
	UITEM_TORNFLESH,
	UITEM_GLADBANE,
	UITEM_RAINCLOAK,
	UITEM_LEATHAUT,
	UITEM_WISDWRAP,
	UITEM_SPARKMAIL,
	UITEM_SCAVCARAP,
	UITEM_NIGHTSCAPE,
	UITEM_NAJPLATE,
	UITEM_DEMONSPIKE,
	UITEM_DEFLECTOR,
	UITEM_SKULLSHLD,
	UITEM_DRAGONBRCH,
	UITEM_BLKOAKSHLD,
	UITEM_HOLYDEF,
	UITEM_STORMSHLD,
	UITEM_BRAMBLE,
	UITEM_REGHA,
	UITEM_BLEEDER,
	UITEM_CONSTRICT,
	UITEM_ENGAGE,
	UITEM_INVALID = -1,
};

/*
CF_LEVEL: Item Level (6 bits; value ranges from 0-63)
CF_ONLYGOOD: Item is not able to have affixes with PLOK set to false
CF_UPER15: Item is from a Unique Monster and has 15% chance of being a Unique Item
CF_UPER1: Item is from the dungeon and has a 1% chance of being a Unique Item
CF_UNIQUE: Item is a Unique Item
CF_SMITH: Item is from Griswold (Basic)
CF_SMITHPREMIUM: Item is from Griswold (Premium)
CF_BOY: Item is from Wirt
CF_WITCH: Item is from Adria
CF_HEALER: Item is from Pepin
CF_PREGEN: Item is pre-generated, mostly associated with Quest items found in the dungeon or potions on the dungeon floor

Items that have both CF_UPER15 and CF_UPER1 are CF_USEFUL, which is used to generate Potions and Town Portal scrolls on the dungeon floor
Items that have any of CF_SMITH, CF_SMITHPREMIUM, CF_BOY, CF_WICTH, and CF_HEALER are CF_TOWN, indicating the item is sourced from an NPC
*/
enum icreateinfo_flag {
	// clang-format off
	CF_LEVEL        = (1 << 6) - 1,
	CF_ONLYGOOD     = 1 << 6,
	CF_UPER15       = 1 << 7,
	CF_UPER1        = 1 << 8,
	CF_UNIQUE       = 1 << 9,
	CF_SMITH        = 1 << 10,
	CF_SMITHPREMIUM = 1 << 11,
	CF_BOY          = 1 << 12,
	CF_WITCH        = 1 << 13,
	CF_HEALER       = 1 << 14,
	CF_PREGEN       = 1 << 15,

	CF_USEFUL = CF_UPER15 | CF_UPER1,
	CF_TOWN   = CF_SMITH | CF_SMITHPREMIUM | CF_BOY | CF_WITCH | CF_HEALER,
	// clang-format on
};

enum icreateinfo_flag2 {
	// clang-format off
	CF_HELLFIRE = 1 << 0,
	CF_UIDOFFSET = ((1 << 4) - 1) << 1,
	/** [RUNS MOD] Marked by a Scroll of Preservation: survives ONE extraction death,
	 * which clears the bit. dwBuff bits 5-31 are otherwise unused and round-trip
	 * fully through saves, hero packs, RecreateItem and network sync. */
	CF_PRESERVED = 1 << 5,
	/** [RUNS MOD] This item has a gem fused into it (see runs_gems.h). The gem's identity
	 * lives here rather than only in the _iPL* fields it granted, for two reasons: the
	 * raw stat fields do NOT survive ItemPack (items are rebuilt from seed + create-info,
	 * so a synced or repacked item would silently lose its gem), and an item has to be
	 * able to say which gem it holds. dwBuff does round-trip, so the bonus is re-applied
	 * from these bits after every rebuild. */
	CF_GEMMED = 1 << 6,
	CF_GEMTYPE = ((1 << 3) - 1) << 7,     // bits 7-9:  GemType
	CF_GEMQUALITY = ((1 << 3) - 1) << 10, // bits 10-12: GemQuality
	/** [RUNS MOD] This item IS a gem, and which one.
	 *
	 * Deliberately here rather than in _iCreateInfo: that word is not free storage. Its
	 * low six bits ARE the item level (CF_LEVEL) and the rest are provenance flags the
	 * validation code reads, so packing a type/quality pair into it made a plain Skull
	 * claim item level 38 while a Perfect Skull claimed 6 -- inverting which gems the
	 * game thought a hero could use, and setting CF_ONLYGOOD at random. */
	CF_ISGEM = 1 << 13,
	CF_GEMITEMTYPE = ((1 << 3) - 1) << 14,     // bits 14-16: GemType
	CF_GEMITEMQUALITY = ((1 << 3) - 1) << 17,  // bits 17-19: GemQuality
	/** [RUNS MOD] This item has a socket, so a gem can be fused into it (see runs_gems.h).
	 * Rolled once when the item is created and carried here from then on, like the gem. */
	CF_SOCKETED = 1 << 20,
	/** [RUNS MOD] A unique chosen by index (relic, fruit-machine jackpot) rather than
	 * rolled. Vanilla rebuilds a unique by re-rolling it from seed + item level, which a
	 * chosen unique cannot survive, so RecreateItem rebuilds these from the index here. */
	CF_CHOSENUNIQUE = 1 << 21,
	CF_CHOSENUNIQUEID = ((1 << 9) - 1) << 22, // bits 22-30: index into UniqueItems
	// clang-format on
};

// All item animation frames have this width.
constexpr int ItemAnimWidth = 96;

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

struct Item {
	/** Randomly generated identifier */
	uint32_t _iSeed = 0;
	uint16_t _iCreateInfo = 0;
	ItemType _itype = ItemType::None;
	bool _iAnimFlag = false;
	Point position = { 0, 0 };
	/*
	 * @brief Contains Information for current Animation
	 */
	AnimationInfo AnimInfo;
	bool _iDelFlag = false; // set when item is flagged for deletion, deprecated in 1.02
	SelectionRegion selectionRegion = SelectionRegion::None;
	bool _iPostDraw = false;
	bool _iIdentified = false;
	item_quality _iMagical = ITEM_QUALITY_NORMAL;
	char _iName[ItemNameLength] = {};
	char _iIName[ItemNameLength] = {};
	item_equip_type _iLoc = ILOC_NONE;
	item_class _iClass = ICLASS_NONE;
	// [RUNS MOD] Widened from uint8_t: the mod's gem icons live in their own sprite list
	// past objcurs/objcurs2, which together already fill 228 of the 256 slots a byte can
	// address. Safe to widen -- the save file has always stored this as a 32-bit field,
	// and ItemPack does not carry it at all (items are rebuilt by RecreateItem).
	uint16_t _iCurs = 0;
	int _ivalue = 0;
	int _iIvalue = 0;
	uint8_t _iMinDam = 0;
	uint8_t _iMaxDam = 0;
	int16_t _iAC = 0;
	ItemSpecialEffect _iFlags = ItemSpecialEffect::None;
	item_misc_id _iMiscId = IMISC_NONE;
	SpellID _iSpell = SpellID::Null;
	_item_indexes IDidx = IDI_NONE;
	int _iCharges = 0;
	int _iMaxCharges = 0;
	int _iDurability = 0;
	int _iMaxDur = 0;
	int16_t _iPLDam = 0;
	int16_t _iPLToHit = 0;
	int16_t _iPLAC = 0;
	int16_t _iPLStr = 0;
	int16_t _iPLMag = 0;
	int16_t _iPLDex = 0;
	int16_t _iPLVit = 0;
	int16_t _iPLFR = 0;
	int16_t _iPLLR = 0;
	int16_t _iPLMR = 0;
	int16_t _iPLMana = 0;
	int16_t _iPLHP = 0;
	int16_t _iPLDamMod = 0;
	int16_t _iPLGetHit = 0;
	int16_t _iPLLight = 0;
	int8_t _iSplLvlAdd = 0;
	bool _iRequest = false;
	/** Unique item ID, used as an index into UniqueItemList */
	int _iUid = 0;
	int16_t _iFMinDam = 0;
	int16_t _iFMaxDam = 0;
	int16_t _iLMinDam = 0;
	int16_t _iLMaxDam = 0;
	int16_t _iPLEnAc = 0;
	enum item_effect_type _iPrePower = IPL_INVALID;
	enum item_effect_type _iSufPower = IPL_INVALID;
	int _iVAdd1 = 0;
	int _iVMult1 = 0;
	int _iVAdd2 = 0;
	int _iVMult2 = 0;
	int8_t _iMinStr = 0;
	uint8_t _iMinMag = 0;
	int8_t _iMinDex = 0;
	bool _iStatFlag = false;
	ItemSpecialEffectHf _iDamAcFlags = ItemSpecialEffectHf::None;
	uint32_t dwBuff = 0;

	/**
	 * @brief Clears this item and returns the old value
	 */
	Item pop() &
	{
		Item temp = std::move(*this);
		clear();
		return temp;
	}

	/**
	 * @brief Resets the item so isEmpty() returns true without needing to reinitialise the whole object
	 */
	DVL_REINITIALIZES void clear()
	{
		this->_itype = ItemType::None;
	}

	/**
	 * @brief Checks whether this item is empty or not.
	 * @return 'True' in case the item is empty and 'False' otherwise.
	 */
	bool isEmpty() const
	{
		return this->_itype == ItemType::None;
	}

	/**
	 * @brief Checks whether this item is an equipment.
	 * @return 'True' in case the item is an equipment and 'False' otherwise.
	 */
	bool isEquipment() const
	{
		if (this->isEmpty()) {
			return false;
		}

		switch (this->_iLoc) {
		case ILOC_AMULET:
		case ILOC_ARMOR:
		case ILOC_HELM:
		case ILOC_ONEHAND:
		case ILOC_RING:
		case ILOC_TWOHAND:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is a weapon.
	 * @return 'True' in case the item is a weapon and 'False' otherwise.
	 */
	bool isWeapon() const
	{
		switch (this->_itype) {
		case ItemType::Axe:
		case ItemType::Bow:
		case ItemType::Mace:
		case ItemType::Staff:
		case ItemType::Sword:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is an armor.
	 * @return 'True' in case the item is an armor and 'False' otherwise.
	 */
	bool isArmor() const
	{
		switch (this->_itype) {
		case ItemType::HeavyArmor:
		case ItemType::LightArmor:
		case ItemType::MediumArmor:
			return true;

		default:
			return false;
		}
	}

	/**
	 * @brief Checks whether this item is gold.
	 * @return 'True' in case the item is gold and 'False' otherwise.
	 */
	bool isGold() const
	{
		return this->_itype == ItemType::Gold;
	}

	/**
	 * @brief Checks whether this item is a helm.
	 * @return 'True' in case the item is a helm and 'False' otherwise.
	 */
	bool isHelm() const
	{
		return this->_itype == ItemType::Helm;
	}

	/**
	 * @brief Checks whether this item is a shield.
	 * @return 'True' in case the item is a shield and 'False' otherwise.
	 */
	bool isShield() const
	{
		return this->_itype == ItemType::Shield;
	}

	/**
	 * @brief Checks whether this item is a jewelry.
	 * @return 'True' in case the item is a jewelry and 'False' otherwise.
	 */
	bool isJewelry() const
	{
		switch (this->_itype) {
		case ItemType::Amulet:
		case ItemType::Ring:
			return true;

		default:
			return false;
		}
	}

	[[nodiscard]] bool isScroll() const
	{
		return IsAnyOf(_iMiscId, IMISC_SCROLL, IMISC_SCROLLT);
	}

	/** @brief [RUNS MOD] Marked by a Scroll of Preservation to survive one death. */
	[[nodiscard]] bool isPreserved() const
	{
		return (dwBuff & CF_PRESERVED) != 0;
	}

	void setPreserved(bool preserved) // [RUNS MOD]
	{
		if (preserved)
			dwBuff |= CF_PRESERVED;
		else
			dwBuff &= ~static_cast<uint32_t>(CF_PRESERVED);
	}

	[[nodiscard]] bool isScrollOf(SpellID spellId) const
	{
		return isScroll() && _iSpell == spellId;
	}

	[[nodiscard]] bool isRune() const
	{
		return _iMiscId > IMISC_RUNEFIRST && _iMiscId < IMISC_RUNELAST;
	}

	[[nodiscard]] bool isRuneOf(SpellID spellId) const
	{
		if (!isRune())
			return false;
		switch (_iMiscId) {
		case IMISC_RUNEF:
			return spellId == SpellID::RuneOfFire;
		case IMISC_RUNEL:
			return spellId == SpellID::RuneOfLight;
		case IMISC_GR_RUNEL:
			return spellId == SpellID::RuneOfNova;
		case IMISC_GR_RUNEF:
			return spellId == SpellID::RuneOfImmolation;
		case IMISC_RUNES:
			return spellId == SpellID::RuneOfStone;
		default:
			return false;
		}
	}

	[[nodiscard]] bool isUsable() const;

	[[nodiscard]] bool keyAttributesMatch(uint32_t seed, _item_indexes itemIndex, uint16_t createInfo) const
	{
		return _iSeed == seed && IDidx == itemIndex && _iCreateInfo == createInfo;
	}

	UiFlags getTextColor() const
	{
		switch (_iMagical) {
		case ITEM_QUALITY_MAGIC:
			return UiFlags::ColorBlue;
		case ITEM_QUALITY_UNIQUE:
			return UiFlags::ColorWhitegold;
		default:
			return UiFlags::ColorWhite;
		}
	}

	UiFlags getTextColorWithStatCheck() const
	{
		if (!_iStatFlag)
			return UiFlags::ColorRed;
		return getTextColor();
	}

	/**
	 * @brief Sets the current Animation for the Item
	 * @param showAnimation Definies if the Animation (Flipping) is shown or if only the final Frame (item on the ground) is shown
	 */
	void setNewAnimation(bool showAnimation);

	/**
	 * @brief If this item is a spell book, calculates the magic requirement to learn a new level, then for all items sets _iStatFlag
	 * @param player Player to compare stats against requirements
	 */
	void updateRequiredStatsCacheForPlayer(const Player &player);

	/** @brief Returns the translated item name to display (respects identified flag) */
	StringOrView getName() const;

	[[nodiscard]] Displacement getRenderingOffset(const ClxSprite sprite) const
	{
		return { -CalculateSpriteTileCenterX(sprite.width()), 0 };
	}
};

struct ItemGetRecordStruct {
	uint32_t nSeed;
	uint16_t wCI;
	int nIndex;
	uint32_t dwTimestamp;
};

struct CornerStoneStruct {
	Point position;
	bool activated;
	Item item;
	bool isAvailable();
};

/** Contains the items on ground in the current game. */
extern Item Items[MAXITEMS + 1];
extern uint8_t ActiveItems[MAXITEMS];
extern uint8_t ActiveItemCount;
/** Contains the location of dropped items. */
extern int8_t dItem[MAXDUNX][MAXDUNY];
extern bool ShowUniqueItemInfoBox;
extern CornerStoneStruct CornerStone;
extern DVL_API_FOR_TEST bool UniqueItemFlags[128];

uint8_t GetOutlineColor(const Item &item, bool checkReq);
bool IsItemAvailable(int i);
void ClearUniqueItemFlags();
void InitItemGFX();
void InitItems();
void CalcPlrItemVals(Player &player, bool Loadgfx);
void CalcPlrInv(Player &player, bool Loadgfx);
void InitializeItem(Item &item, _item_indexes itemData);

/** @brief [RUNS MOD] Builds a dungeon scroll that opens a run at the given level. */
void CreateDungeonScroll(Item &item, int level, int difficulty = 0);

/** @brief [RUNS MOD] True if the item is a dungeon scroll. */
inline bool IsDungeonScroll(const Item &item)
{
	return item.IDidx == IDI_DUNGEONSCROLL;
}

/** @brief [RUNS MOD] Gives a dungeon scroll the mod's own inventory icon.
 *
 * The item table can only name a vanilla objcurs frame, and every scroll in the game
 * shares ICURS_SCROLL_OF, so the dedicated icon is applied here instead -- on creation
 * and again after load, so scrolls sitting in an older save pick it up too. No-op when
 * the art file is missing, leaving the vanilla frame in place.
 */
void ApplyDungeonScrollIcon(Item &item);

/** @brief [RUNS MOD] Dungeon scroll level (create-info bits 0-5). */
inline int DungeonScrollLevel(const Item &item)
{
	return item._iCreateInfo & CF_LEVEL;
}

/** @brief [RUNS MOD] Dungeon scroll modifier count (create-info bits 6-7), 1..3.
 * This doubles as the scroll's rarity tier: 1 = Common, 2 = Magic, 3 = Rare. */
inline int DungeonScrollModCount(const Item &item)
{
	const int c = (item._iCreateInfo >> 6) & 0x03;
	return c < 1 ? 1 : (c > 3 ? 3 : c);
}

/** @brief [RUNS MOD] Dungeon scroll difficulty (create-info bits 8-9):
 * 0 = Normal, 1 = Nightmare, 2 = Hell. Drives the engine difficulty for the run. */
inline int DungeonScrollDifficulty(const Item &item)
{
	const int d = (item._iCreateInfo >> 8) & 0x03;
	return d > 2 ? 2 : d;
}

/** @brief [RUNS MOD] Packs a scroll's level (bits 0-5), modifier count (bits 6-7) and
 * difficulty (bits 8-9) into the create-info word. */
inline uint16_t MakeDungeonScrollCreateInfo(int level, int modCount, int difficulty = 0)
{
	return static_cast<uint16_t>((level & CF_LEVEL) | ((modCount & 0x03) << 6) | ((difficulty & 0x03) << 8));
}

/** @brief [RUNS MOD] Drops a dungeon scroll of the given level+difficulty near a position. */
void DropDungeonScroll(int level, Point position, int difficulty, bool sendmsg);
/** @brief [RUNS MOD] Rolls the boss vault's relic chance: a retired quest unique
 * gated within a few levels of `depth`, dropped at `position`. No-op most of the time. */
void TryDropRunRelic(int depth, Point position);
/** @brief [RUNS MOD] True if the item is a relic (a unique only the boss vault's relic
 * roll hands out), so the info box can say so. */
bool IsRunRelic(const Item &item);
/** @brief [RUNS MOD] Appends the dungeon scroll tooltip's relic line: how many relics a
 * boss at `depth` can drop and the vault's relic chance. */
void GetRunRelicScrollLines(int depth, std::vector<std::string> &lines);
/** @brief [RUNS MOD] Drops a Gambler's Token (one free pull at Farnham's machine). */
void DropGambleToken(Point position, bool sendmsg);

/** @brief [RUNS MOD] True if the player already carries a dungeon scroll. */
bool PlayerHasDungeonScroll(const Player &player);

/** @brief [RUNS MOD] Grants a basic dungeon scroll if the player has none (hub safety net). */
void EnsureStarterDungeonScroll(Player &player);

void GenerateNewSeed(Item &item);
int GetGoldCursor(int value);

/**
 * @brief Update the gold cursor on the given gold item
 * @param gold The item to update
 */
void SetPlrHandGoldCurs(Item &gold);
void CreatePlrItems(Player &player);
bool ItemSpaceOk(Point position);
int AllocateItem();
/**
 * @brief Moves the item onto the floor of the current dungeon level
 * @param item The source of the item data, should not be used after calling this function
 * @param position Coordinates of the tile to place the item on
 * @return The index assigned to the item
 */
uint8_t PlaceItemInWorld(Item &&item, WorldTilePosition position);
Point GetSuperItemLoc(Point position);
void GetItemAttrs(Item &item, _item_indexes itemData, int lvl);
void SetupItem(Item &item);
Item *SpawnUnique(_unique_items uid, Point position, std::optional<int> level = std::nullopt, bool sendmsg = true, bool exactPosition = false);
void GetSuperItemSpace(Point position, int8_t inum);
_item_indexes RndItemForMonsterLevel(int8_t monsterLevel);
void SetupAllItems(const Player &player, Item &item, _item_indexes idx, uint32_t iseed, int lvl, int uper, bool onlygood, bool pregen, int uidOffset = 0, bool forceNotUnique = false);
void TryRandomUniqueItem(Item &item, _item_indexes idx, int8_t mLevel, int uper, bool onlygood, bool pregen);
void SpawnItem(Monster &monster, Point position, bool sendmsg, bool spawn = false);
void CreateRndItem(Point position, bool onlygood, bool sendmsg, bool delta, bool spawn = false);
void CreateRndUseful(Point position, bool sendmsg);
void CreateTypeItem(Point position, bool onlygood, ItemType itemType, int imisc, bool sendmsg, bool delta, bool spawn = false);
void RecreateItem(const Player &player, Item &item, _item_indexes idx, uint16_t icreateinfo, uint32_t iseed, int ivalue, uint32_t dwBuff);
void RecreateEar(Item &item, uint16_t ic, uint32_t iseed, uint8_t bCursval, std::string_view heroName);
void CornerstoneSave();
void CornerstoneLoad(Point position);
void SpawnQuestItem(_item_indexes itemid, Point position, int randarea, SelectionRegion selectionRegion, bool sendmsg);
void SpawnRewardItem(_item_indexes itemid, Point position, bool sendmsg);
void SpawnMapOfDoom(Point position, bool sendmsg);
void SpawnRuneBomb(Point position, bool sendmsg);
void SpawnTheodore(Point position, bool sendmsg);
void RespawnItem(Item &item, bool FlipFlag);
void DeleteItem(int i);
void ProcessItems();
void FreeItemGFX();
void GetItemFrm(Item &item);
void GetItemStr(Item &item);
void CheckIdentify(Player &player, int cii);
/** @brief [RUNS MOD] True while the Scroll of Preservation's mark cursor is up
 * (reuses the identify cursor art; routes the identify click to DoPreserve). */
extern bool PreserveTargetingActive;
/** @brief [RUNS MOD] Slot (INVITEM_* id) the preservation scroll was used from. */
extern int8_t PreserveScrollSource;
/** @brief [RUNS MOD] Marks the clicked item to survive one death and spends the
 * scroll. Returns false (keeping the cursor) on an invalid target. */
bool DoPreserve(Player &player, int cii);
void DoRepair(Player &player, int cii);
void DoRecharge(Player &player, int cii);
bool DoOil(Player &player, int cii);
[[nodiscard]] StringOrView PrintItemPower(char plidx, const Item &item);
void DrawUniqueInfo(const Surface &out);
void PrintItemDetails(const Item &item);
void PrintItemDur(const Item &item);
void UseItem(Player &player, item_misc_id Mid, SpellID spellID, int spellFrom);
bool UseItemOpensHive(const Item &item, Point position);
bool UseItemOpensGrave(const Item &item, Point position);
void SpawnSmith(int lvl);
void ReplacePremium(const Player &player, int idx);
void SpawnPremium(const Player &player);
void SpawnWitch(int lvl);
/** @brief [RUNS MOD] Stocks the dungeon merchant with level-appropriate scrolls. */
void SpawnDungeonVendor();
/** @brief [RUNS MOD] Rolls the displayed fruit-machine jackpot into GamblerJackpot:
 * a random droppable-base unique with UIMinLvl <= qlvlCap (empty if none qualify). */
void SpawnGamblerJackpot(int qlvlCap);
/** @brief [RUNS MOD] Drops an already-built item near `position`, network-synced
 * (fruit-machine payouts land at the player's feet). */
void DropPrizeItem(Item prize, Point position);
/** @brief [RUNS MOD] Builds (does not drop) a good-quality equipment prize around `lvl`. */
Item RollGamblePrizeItem(int lvl);

/** @brief [RUNS MOD] Rolls and drops one boss-vault item at `position` (synced like a monster drop). */
void DropBossGradeItem(Point position);
/**
 * @brief [RUNS MOD] How many depths deeper the run's own rewards pay out than the run itself:
 * 0 on Normal, more on Nightmare and Hell.
 *
 * The engine already lifts every monster's level on the harder difficulties (+15, +30), which
 * lifts their ordinary drops. The mod's rewards -- the boss's drop, its vault, relics, gems
 * and companion potions -- are keyed to depth instead, so without this a Hell run paid
 * exactly what Normal did at the same depth for far more risk. Monster level runs at about
 * twice the depth, so these match the engine's lift.
 */
int RunRewardDepthBonus();

/** @brief [RUNS MOD] Rolls Adria's companion potion for this restock (a boss kill, or the first
 * hub visit), at a premium and a few depths under the hero's deepest. SpawnWitch shelves it
 * until it is bought; it does not come back until the next restock. */
void RestockWitchCompanion();
/** @brief [RUNS MOD] Call when Adria sells `item`: selling her companion potion takes it off the
 * shelf until the next restock. */
void NoteWitchItemSold(const Item &item);
void SpawnBoy(int lvl);
void SpawnHealer(int lvl);
void MakeGoldStack(Item &goldItem, int value);
int ItemNoFlippy();
void CreateSpellBook(Point position, SpellID ispell, bool sendmsg, bool delta);
void CreateMagicArmor(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta);
void CreateAmulet(Point position, int lvl, bool sendmsg, bool delta, bool spawn = false);
void CreateMagicWeapon(Point position, ItemType itemType, int icurs, bool sendmsg, bool delta);
bool GetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void SetItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);
void PutItemRecord(uint32_t nSeed, uint16_t wCI, int nIndex);

/**
 * @brief Resets item get records.
 */
void initItemGetRecords();

void RepairItem(Item &item, int lvl);
void RechargeItem(Item &item, Player &player);
bool ApplyOilToItem(Item &item, Player &player);
/**
 * @brief Checks if the item is generated in vanilla hellfire. If yes it updates dwBuff to include CF_HELLFIRE.
 */
void UpdateHellfireFlag(Item &item, const char *identifiedItemName);

/* data */

extern int MaxGold;

extern int8_t ItemCAnimTbl[];
/** @brief Animation/sound class for an item cursor graphic.
 *
 * [RUNS MOD] Use this rather than indexing ItemCAnimTbl directly: the mod's gem cursors
 * are numbered past the end of that table and are mapped onto a stand-in here.
 */
int8_t ItemCAnim(int curs);
extern SfxID ItemInvSnds[];

} // namespace devilution

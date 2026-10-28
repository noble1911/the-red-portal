/**
 * @file stores.cpp
 *
 * Implementation of functionality for stores and towner dialogs.
 */
#include "stores.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

// [RUNS MOD] SDL ticks drive the fruit machine's reel animation.
#ifdef USE_SDL3
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "engine/backbuffer_state.hpp"
#include "engine/random.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/trn.hpp"
#include "game_mode.hpp"
#include "lua/lua_event.hpp"
#include "minitext.h"
#include "monster.h"
#include "multi.h"
#include "options.h"
#include "panels/info_box.hpp"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "runs_gems.h"
#include "runs_mod.h"
#include "scroll_mods.h"
#include "tables/townerdat.hpp"
#include "towners.h"
#include "utils/format_int.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

namespace devilution {

TalkID ActiveStore;

int CurrentItemIndex;
int8_t PlayerItemIndexes[48];
Item PlayerItems[48];

StaticVector<Item, NumSmithBasicItemsHf> SmithItems;
int PremiumItemCount;
int PremiumItemLevel;
StaticVector<Item, NumSmithItemsHf> PremiumItems;

StaticVector<Item, NumHealerItemsHf> HealerItems;

StaticVector<Item, NumWitchItemsHf> WitchItems;

StaticVector<Item, NumDungeonVendorItems> DungeonVendorItems;

int BoyItemLevel;
Item BoyItem;

/** [RUNS MOD] Tonight's fruit-machine jackpot (empty = claimed / not yet rolled). */
Item GamblerJackpot;

/** Remember currently selected text line from TextLine while displaying a dialog */
int OldTextLine;
/** Currently selected text line from TextLine */
int CurrentTextLine;
/** Remember last scroll position */
int OldScrollPos;
/** Scroll position */
int ScrollPos;
/** Remember current store while displaying a dialog */
TalkID OldActiveStore;
/** Temporary item used to hold the item being traded */
Item TempItem;

std::vector<std::pair<std::string, std::vector<TownerDialogOption>>> ExtraTownerOptions;

const char *TownerNameForTalkID(TalkID s)
{
	switch (s) {
	case TalkID::Smith: return "griswold";
	case TalkID::Witch: return "adria";
	case TalkID::Boy: return "wirt";
	case TalkID::Healer: return "pepin";
	case TalkID::Storyteller: return "cain";
	case TalkID::Tavern: return "ogden";
	case TalkID::Drunk: return "farnham";
	case TalkID::Gamble: return "farnham"; // [RUNS MOD] Farnham hosts the fruit machine

	case TalkID::Barmaid: return "gillian";
	case TalkID::DungeonVendor: return "dungeonvendor";
	default: return nullptr;
	}
}

/** Finds the entry for a towner in ExtraTownerOptions, or nullptr if none. */
static std::vector<TownerDialogOption> *FindExtraTownerOptions(std::string_view townerName)
{
	for (auto &[name, opts] : ExtraTownerOptions) {
		if (name == townerName)
			return &opts;
	}
	return nullptr;
}

void RegisterTownerDialogOption(std::string_view townerName,
    std::function<std::string()> getLabel,
    std::function<void()> onSelect)
{
	// Validate that the towner name is known.
	bool found = false;
	for (const auto &[id, shortName] : TownerShortNames) {
		if (shortName == townerName) {
			found = true;
			break;
		}
	}
	if (!found) {
		LogWarn("RegisterTownerDialogOption: unknown towner name \"{}\"", townerName);
	}

	if (auto *opts = FindExtraTownerOptions(townerName); opts != nullptr) {
		opts->push_back({ std::move(getLabel), std::move(onSelect) });
	} else {
		std::vector<TownerDialogOption> newOpts;
		newOpts.push_back({ std::move(getLabel), std::move(onSelect) });
		ExtraTownerOptions.emplace_back(std::string(townerName), std::move(newOpts));
	}
}

/**
 * Maps dialog line number to ExtraTownerOptions vector index for
 * options visible in the current dialog session.
 * Indexed by text line number (0..NumStoreLines-1).
 */
static std::optional<size_t> CurrentExtraOptionIndices[NumStoreLines];

void ClearTownerDialogOptions()
{
	ExtraTownerOptions.clear();
	std::fill(std::begin(CurrentExtraOptionIndices), std::end(CurrentExtraOptionIndices), std::nullopt);
}

namespace {

/** The current towner being interacted with */
_talker_id TownerId;

/** Is the current dialog full size */
bool IsTextFullSize;

/** Number of text lines in the current dialog */
int NumTextLines;

struct STextStruct {
	enum Type : uint8_t {
		Label,
		Divider,
		Selectable,
	};

	std::string text;
	int _sval;
	int y;
	UiFlags flags;
	Type type;
	uint8_t _sx;
	uint8_t _syoff;
	int cursId;
	bool cursIndent;

	[[nodiscard]] bool isDivider() const
	{
		return type == Divider;
	}
	[[nodiscard]] bool isSelectable() const
	{
		return type == Selectable;
	}

	[[nodiscard]] bool hasText() const
	{
		return !text.empty();
	}
};

/** Text lines */
STextStruct TextLine[NumStoreLines];

/** Whether to render the player's gold amount in the top left */
bool RenderGold;

/** Does the current panel have a scrollbar */
bool HasScrollbar;
/** Next scroll position */
int NextScrollPos;
/** Previous scroll position */
int PreviousScrollPos;
/** Countdown for the push state of the scroll up button */
int8_t CountdownScrollUp;
/** Countdown for the push state of the scroll down button */
int8_t CountdownScrollDown;

/** Maps from towner IDs to NPC names. */
const char *const TownerNames[] = {
	N_("Griswold"),
	N_("Pepin"),
	"",
	N_("Ogden"),
	N_("Cain"),
	N_("Farnham"),
	N_("Adria"),
	N_("Gillian"),
	N_("Wirt"),
};

constexpr int PaddingTop = 32;

// For most languages, line height is always 12.
// This includes blank lines and divider line.
constexpr int SmallLineHeight = 12;
constexpr int SmallTextHeight = 12;

// For larger small fonts (Chinese and Japanese), text lines are
// taller and overflow.
// We space out blank lines a bit more to give space to 3-line store items.
constexpr int LargeLineHeight = SmallLineHeight + 1;
constexpr int LargeTextHeight = 18;

/**
 * The line index with the Back / Leave button.
 * This is a special button that is always the last line.
 *
 * For lists with a scrollbar, it is not selectable (mouse-only).
 */
int BackButtonLine()
{
	if (IsSmallFontTall()) {
		return HasScrollbar ? 21 : 20;
	}
	return 22;
}

int LineHeight()
{
	return IsSmallFontTall() ? LargeLineHeight : SmallLineHeight;
}

int TextHeight()
{
	return IsSmallFontTall() ? LargeTextHeight : SmallTextHeight;
}

void CalculateLineHeights()
{
	TextLine[0].y = 0;
	if (IsSmallFontTall()) {
		for (int i = 1; i < NumStoreLines; ++i) {
			// Space out consecutive text lines, unless they are both selectable (never the case currently).
			if (TextLine[i].hasText() && TextLine[i - 1].hasText() && !(TextLine[i].isSelectable() && TextLine[i - 1].isSelectable())) {
				TextLine[i].y = TextLine[i - 1].y + LargeTextHeight;
			} else {
				TextLine[i].y = i * LargeLineHeight;
			}
		}
	} else {
		for (int i = 1; i < NumStoreLines; ++i) {
			TextLine[i].y = i * SmallLineHeight;
		}
	}
}

void DrawSTextBack(const Surface &out)
{
	const Point uiPosition = GetUIRectangle().position;
	ClxDraw(out, { uiPosition.x + 320 + 24, 327 + uiPosition.y }, (*pSTextBoxCels)[0]);
	DrawHalfTransparentRectTo(out, uiPosition.x + 347, uiPosition.y + 28, 265, 297);
}

void DrawSSlider(const Surface &out, int y1, int y2)
{
	const Point uiPosition = GetUIRectangle().position;
	int yd1 = (y1 * 12) + 44 + uiPosition.y;
	const int yd2 = (y2 * 12) + 44 + uiPosition.y;
	if (CountdownScrollUp != -1)
		ClxDraw(out, { uiPosition.x + 601, yd1 }, (*pSTextSlidCels)[11]);
	else
		ClxDraw(out, { uiPosition.x + 601, yd1 }, (*pSTextSlidCels)[9]);
	if (CountdownScrollDown != -1)
		ClxDraw(out, { uiPosition.x + 601, yd2 }, (*pSTextSlidCels)[10]);
	else
		ClxDraw(out, { uiPosition.x + 601, yd2 }, (*pSTextSlidCels)[8]);
	yd1 += 12;
	int yd3 = yd1;
	for (; yd3 < yd2; yd3 += 12) {
		ClxDraw(out, { uiPosition.x + 601, yd3 }, (*pSTextSlidCels)[13]);
	}
	if (CurrentTextLine == BackButtonLine())
		yd3 = OldTextLine;
	else
		yd3 = CurrentTextLine;
	if (CurrentItemIndex > 1)
		yd3 = 1000 * (ScrollPos + ((yd3 - PreviousScrollPos) / 4)) / (CurrentItemIndex - 1) * (y2 * 12 - y1 * 12 - 24) / 1000;
	else
		yd3 = 0;
	ClxDraw(out, { uiPosition.x + 601, ((y1 + 1) * 12) + 44 + uiPosition.y + yd3 }, (*pSTextSlidCels)[12]);
}

void AddSLine(size_t y)
{
	TextLine[y]._sx = 0;
	TextLine[y]._syoff = 0;
	TextLine[y].text.clear();
	TextLine[y].text.shrink_to_fit();
	TextLine[y].type = STextStruct::Divider;
	TextLine[y].cursId = -1;
	TextLine[y].cursIndent = false;
}

void AddSTextVal(size_t y, int val)
{
	TextLine[y]._sval = val;
}

void AddSText(uint8_t x, size_t y, std::string_view text, UiFlags flags, bool sel, int cursId = -1, bool cursIndent = false)
{
	TextLine[y]._sx = x;
	TextLine[y]._syoff = 0;
	TextLine[y].text.clear();
	TextLine[y].text.append(text);
	TextLine[y].flags = flags;
	TextLine[y].type = sel ? STextStruct::Selectable : STextStruct::Label;
	TextLine[y].cursId = cursId;
	TextLine[y].cursIndent = cursIndent;
}

void AddOptionsBackButton()
{
	const int line = BackButtonLine();
	AddSText(0, line, _("Back"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	TextLine[line]._syoff = IsSmallFontTall() ? 0 : 6;
}

void AddItemListBackButton(bool selectable = false)
{
	const int line = BackButtonLine();
	const std::string_view text = _("Back");
	if (!selectable && IsSmallFontTall()) {
		AddSText(0, line, text, UiFlags::ColorWhite | UiFlags::AlignRight, selectable);
	} else {
		AddSLine(line - 1);
		AddSText(0, line, text, UiFlags::ColorWhite | UiFlags::AlignCenter, selectable);
		TextLine[line]._syoff = 6;
	}
}

void PrintStoreItem(const Item &item, int l, UiFlags flags, bool cursIndent = false)
{
	if (item._iMiscId == IMISC_DUNGEONSCROLL) {
		// [RUNS MOD] Show the scroll's modifiers one per line (compact). Kept narrow so
		// a line never wraps into the next (two-per-line wrapped and overlapped on the
		// rare 3-modifier scrolls). A scroll fills at most its four-line row.
		const ScrollModSet mods = RollScrollMods(item._iSeed, DungeonScrollLevel(item), DungeonScrollModCount(item));
		for (int i = 0; i < mods.count; i++)
			AddSText(40, l++, DescribeScrollMod(mods.mods[i], /*full=*/false), flags, false, -1, cursIndent);
		return;
	}

	std::string productLine;

	if (item._iIdentified) {
		if (item._iMagical != ITEM_QUALITY_UNIQUE) {
			if (item._iPrePower != -1) {
				productLine.append(PrintItemPower(item._iPrePower, item));
			}
		}
		if (item._iSufPower != -1) {
			if (!productLine.empty())
				productLine.append(_(",  "));
			productLine.append(PrintItemPower(item._iSufPower, item));
		}
	}
	if (item._iMiscId == IMISC_STAFF && item._iMaxCharges != 0) {
		if (!productLine.empty())
			productLine.append(_(",  "));
		productLine.append(fmt::format(fmt::runtime(_("Charges: {:d}/{:d}")), item._iCharges, item._iMaxCharges));
	}
	// [RUNS MOD] The socket is often the whole reason to buy a plain shelf item.
	if (HasSocket(item)) {
		if (!productLine.empty())
			productLine.append(_(",  "));
		productLine.append(HasGem(item) ? GemName(FusedGemType(item), FusedGemQuality(item)) : std::string(_("Socketed")));
	}
	if (!productLine.empty()) {
		AddSText(40, l, productLine, flags, false, -1, cursIndent);
		l++;
		productLine.clear();
	}

	if (item._itype != ItemType::Misc) {
		if (item._iClass == ICLASS_WEAPON)
			productLine = fmt::format(fmt::runtime(_("Damage: {:d}-{:d}  ")), item._iMinDam, item._iMaxDam);
		else if (item._iClass == ICLASS_ARMOR)
			productLine = fmt::format(fmt::runtime(_("Armor: {:d}  ")), item._iAC);
		if (item._iMaxDur != DUR_INDESTRUCTIBLE && item._iMaxDur != 0)
			productLine += fmt::format(fmt::runtime(_("Dur: {:d}/{:d}")), item._iDurability, item._iMaxDur);
		else
			productLine.append(_("Indestructible"));
	}

	int8_t str = item._iMinStr;
	uint8_t mag = item._iMinMag;
	int8_t dex = item._iMinDex;

	if (str != 0 || mag != 0 || dex != 0) {
		if (!productLine.empty())
			productLine.append(_(",  "));
		productLine.append(_("Required:"));
		if (str != 0)
			productLine.append(fmt::format(fmt::runtime(_(" {:d} Str")), str));
		if (mag != 0)
			productLine.append(fmt::format(fmt::runtime(_(" {:d} Mag")), mag));
		if (dex != 0)
			productLine.append(fmt::format(fmt::runtime(_(" {:d} Dex")), dex));
	}
	AddSText(40, l++, productLine, flags, false, -1, cursIndent);
}

void ScrollVendorStore(std::span<Item> itemData, int storeLimit, int idx, int selling = true)
{
	ClearSText(5, 21);
	PreviousScrollPos = 5;

	for (int l = 5; l < 20 && idx < storeLimit; l += 4) {
		const Item &item = itemData[idx];
		const UiFlags itemColor = item.getTextColorWithStatCheck();
		AddSText(20, l, item.getName(), itemColor, true, item._iCurs, true);
		AddSTextVal(l, item._iIdentified ? item._iIvalue : item._ivalue);
		PrintStoreItem(item, l + 1, itemColor, true);
		NextScrollPos = l;
		idx++;
	}
	if (selling) {
		if (CurrentTextLine != -1 && !TextLine[CurrentTextLine].isSelectable() && CurrentTextLine != BackButtonLine())
			CurrentTextLine = NextScrollPos;
	} else {
		NumTextLines = std::max(storeLimit - 4, 0);
	}
}

/** Menu lines of the gem services, so Back can return to the one it came from. */
constexpr int SmithGemRemoveLine = 12;
constexpr int SmithGemSalvageLine = 14;

/** Griswold's main-menu line for gem work: after Repair in the text shop, after Trade in
 * the visual one. */
int SmithGemsLine()
{
	return *GetOptions().Gameplay.visualStoreUI ? 14 : 20;
}

void StartSmith()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 1, _("Welcome to the"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 3, _("Blacksmith's shop"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 7, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 10, _("Talk to Griswold"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	// [RUNS MOD] Gem work sits just above Leave, which moves down a line to make room.
	if (*GetOptions().Gameplay.visualStoreUI) {
		AddSText(0, 12, _("Trade / Repair"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		if (RunsModEnabled)
			AddSText(0, SmithGemsLine(), _("Gem work"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, RunsModEnabled ? 16 : 14, _("Leave the shop"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	} else {
		AddSText(0, 12, _("Buy basic items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 14, _("Buy premium items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 16, _("Sell items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 18, _("Repair items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		if (RunsModEnabled)
			AddSText(0, SmithGemsLine(), _("Gem work"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, RunsModEnabled ? 22 : 20, _("Leave the shop"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	}
	AddSLine(5);
	CurrentItemIndex = 20;
}

void ScrollSmithBuy(int idx)
{
	ScrollVendorStore(SmithItems, static_cast<int>(SmithItems.size()), idx);
}

uint32_t TotalPlayerGold()
{
	return MyPlayer->_pGold + Stash.gold;
}

void StartSmithBuy()
{
	IsTextFullSize = true;
	HasScrollbar = true;
	ScrollPos = 0;

	RenderGold = true;
	AddSText(20, 1, _("I have these items for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollSmithBuy(ScrollPos);
	AddItemListBackButton();

	CurrentItemIndex = 0;
	for (Item &item : SmithItems) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
		CurrentItemIndex++;
	}

	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

void ScrollSmithPremiumBuy(int boughtitems)
{
	int idx = 0;
	for (; boughtitems != 0; idx++) {
		if (!PremiumItems[idx].isEmpty())
			boughtitems--;
	}

	ScrollVendorStore(PremiumItems, static_cast<int>(PremiumItems.size()), idx);
}

bool StartSmithPremiumBuy()
{
	CurrentItemIndex = 0;
	for (Item &item : PremiumItems) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
		CurrentItemIndex++;
	}
	if (CurrentItemIndex == 0) {
		StartStore(TalkID::Smith);
		CurrentTextLine = 14;
		return false;
	}

	IsTextFullSize = true;
	HasScrollbar = true;
	ScrollPos = 0;

	RenderGold = true;
	AddSText(20, 1, _("I have these premium items for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	AddItemListBackButton();

	NumTextLines = std::max(CurrentItemIndex - 4, 0);

	ScrollSmithPremiumBuy(ScrollPos);

	return true;
}

bool SmithSellOk(int i)
{
	const Item *pI;

	if (i >= 0) {
		pI = &MyPlayer->InvList[i];
	} else {
		pI = &MyPlayer->SpdList[-(i + 1)];
	}

	return SmithWillBuy(*pI);
}

void ScrollSmithSell(int idx)
{
	ScrollVendorStore(PlayerItems, CurrentItemIndex, idx, false);
}

void StartSmithSell()
{
	IsTextFullSize = true;
	bool sellOk = false;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	const Player &myPlayer = *MyPlayer;

	for (int8_t i = 0; i < myPlayer._pNumInv; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (SmithSellOk(i)) {
			sellOk = true;
			PlayerItems[CurrentItemIndex] = myPlayer.InvList[i];

			if (PlayerItems[CurrentItemIndex]._iMagical != ITEM_QUALITY_NORMAL && PlayerItems[CurrentItemIndex]._iIdentified)
				PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._iIvalue;

			PlayerItems[CurrentItemIndex]._ivalue = std::max(PlayerItems[CurrentItemIndex]._ivalue / 4, 1);
			PlayerItems[CurrentItemIndex]._iIvalue = PlayerItems[CurrentItemIndex]._ivalue;
			PlayerItemIndexes[CurrentItemIndex] = i;
			CurrentItemIndex++;
		}
	}

	for (int i = 0; i < MaxBeltItems; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (SmithSellOk(-(i + 1))) {
			sellOk = true;
			PlayerItems[CurrentItemIndex] = myPlayer.SpdList[i];

			if (PlayerItems[CurrentItemIndex]._iMagical != ITEM_QUALITY_NORMAL && PlayerItems[CurrentItemIndex]._iIdentified)
				PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._iIvalue;

			PlayerItems[CurrentItemIndex]._ivalue = std::max(PlayerItems[CurrentItemIndex]._ivalue / 4, 1);
			PlayerItems[CurrentItemIndex]._iIvalue = PlayerItems[CurrentItemIndex]._ivalue;
			PlayerItemIndexes[CurrentItemIndex] = -(i + 1);
			CurrentItemIndex++;
		}
	}

	if (!sellOk) {
		HasScrollbar = false;

		RenderGold = true;
		AddSText(20, 1, _("You have nothing I want."), UiFlags::ColorWhitegold, false);
		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = CurrentItemIndex;

	RenderGold = true;
	AddSText(20, 1, _("Which item is for sale?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

bool SmithRepairOk(int i)
{
	const Player &myPlayer = *MyPlayer;
	const Item &item = myPlayer.InvList[i];

	if (item.isEmpty())
		return false;
	if (item._itype == ItemType::Misc)
		return false;
	if (item._itype == ItemType::Gold)
		return false;
	if (item._iDurability == item._iMaxDur)
		return false;
	if (item._iMaxDur == DUR_INDESTRUCTIBLE)
		return false;

	return true;
}

void StartSmithRepair()
{
	IsTextFullSize = true;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	Player &myPlayer = *MyPlayer;

	auto &helmet = myPlayer.InvBody[INVLOC_HEAD];
	if (!helmet.isEmpty() && helmet._iDurability != helmet._iMaxDur) {
		AddStoreHoldRepair(&helmet, -1);
	}

	auto &armor = myPlayer.InvBody[INVLOC_CHEST];
	if (!armor.isEmpty() && armor._iDurability != armor._iMaxDur) {
		AddStoreHoldRepair(&armor, -2);
	}

	auto &leftHand = myPlayer.InvBody[INVLOC_HAND_LEFT];
	if (!leftHand.isEmpty() && leftHand._iDurability != leftHand._iMaxDur) {
		AddStoreHoldRepair(&leftHand, -3);
	}

	auto &rightHand = myPlayer.InvBody[INVLOC_HAND_RIGHT];
	if (!rightHand.isEmpty() && rightHand._iDurability != rightHand._iMaxDur) {
		AddStoreHoldRepair(&rightHand, -4);
	}

	for (int i = 0; i < myPlayer._pNumInv; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (SmithRepairOk(i)) {
			AddStoreHoldRepair(&myPlayer.InvList[i], i);
		}
	}

	if (CurrentItemIndex == 0) {
		HasScrollbar = false;

		RenderGold = true;
		AddSText(20, 1, _("You have nothing to repair."), UiFlags::ColorWhitegold, false);
		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = myPlayer._pNumInv;

	RenderGold = true;
	AddSText(20, 1, _("Repair which item?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);

	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

// [RUNS MOD] --- Griswold's gem work ---
//
// Gems go into sockets anywhere, but only Griswold takes them out: he destroys the gem to
// free the socket (for a quarter of the gem's worth), or breaks up the item to save the
// gem (free). Body items use the repair list's negative indices.

inv_body_loc GemWorkBodySlot(int8_t i)
{
	switch (i) {
	case -1:
		return INVLOC_HEAD;
	case -2:
		return INVLOC_CHEST;
	case -3:
		return INVLOC_HAND_LEFT;
	default:
		return INVLOC_HAND_RIGHT;
	}
}

Item &GemWorkTarget(Player &player, int8_t i)
{
	return i >= 0 ? player.InvList[i] : player.InvBody[GemWorkBodySlot(i)];
}

void StartSmithGems()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Griswold's gem work"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(5);
	AddSText(0, 8, _("A gem only comes out in pieces:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("the gem's, or the item's."), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, SmithGemRemoveLine, _("Remove a gem (destroys the gem)"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, SmithGemSalvageLine, _("Salvage a gem (destroys the item)"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Back"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	CurrentItemIndex = 20;
}

void StartSmithGemWork(TalkID work)
{
	IsTextFullSize = true;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	const bool remove = work == TalkID::SmithGemRemove;
	const auto addIfGemmed = [remove](const Item &item, int8_t i) {
		if (CurrentItemIndex >= 48 || !HasGem(item))
			return;
		const int price = remove ? GemRemovalCost(item) : 0;
		PlayerItems[CurrentItemIndex] = item;
		PlayerItems[CurrentItemIndex]._iIvalue = price;
		PlayerItems[CurrentItemIndex]._ivalue = price;
		PlayerItemIndexes[CurrentItemIndex] = i;
		CurrentItemIndex++;
	};

	Player &myPlayer = *MyPlayer;
	addIfGemmed(myPlayer.InvBody[INVLOC_HEAD], -1);
	addIfGemmed(myPlayer.InvBody[INVLOC_CHEST], -2);
	addIfGemmed(myPlayer.InvBody[INVLOC_HAND_LEFT], -3);
	addIfGemmed(myPlayer.InvBody[INVLOC_HAND_RIGHT], -4);
	for (int i = 0; i < myPlayer._pNumInv; i++)
		addIfGemmed(myPlayer.InvList[i], static_cast<int8_t>(i));

	RenderGold = true;
	if (CurrentItemIndex == 0) {
		HasScrollbar = false;
		AddSText(20, 1, _("You have no gemmed items."), UiFlags::ColorWhitegold, false);
		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = myPlayer._pNumInv;
	AddSText(20, 1, remove ? _("Destroy the gem in which item?") : _("Break up which item for its gem?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

void StartWitch()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Witch's shack"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Talk to Adria"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	if (*GetOptions().Gameplay.visualStoreUI) {
		AddSText(0, 14, _("Buy / Sell"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 16, _("Recharge staves"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 18, _("Leave the shack"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSLine(4);
	} else {
		AddSText(0, 14, _("Buy items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 16, _("Sell items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 18, _("Recharge staves"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 20, _("Leave the shack"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSLine(5);
	}

	CurrentItemIndex = 20;
}

void ScrollWitchBuy(int idx)
{
	ScrollVendorStore(WitchItems, static_cast<int>(WitchItems.size()), idx);
}

void WitchBookLevel(Item &bookItem)
{
	if (bookItem._iMiscId != IMISC_BOOK)
		return;
	bookItem._iMinMag = GetSpellData(bookItem._iSpell).minInt;
	uint8_t spellLevel = MyPlayer->_pSplLvl[static_cast<int8_t>(bookItem._iSpell)];
	while (spellLevel > 0) {
		bookItem._iMinMag += 20 * bookItem._iMinMag / 100;
		spellLevel--;
		if (bookItem._iMinMag + 20 * bookItem._iMinMag / 100 > 255) {
			bookItem._iMinMag = 255;
			spellLevel = 0;
		}
	}
}

void StartWitchBuy()
{
	IsTextFullSize = true;
	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = 20;

	RenderGold = true;
	AddSText(20, 1, _("I have these items for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollWitchBuy(ScrollPos);
	AddItemListBackButton();

	CurrentItemIndex = 0;
	for (Item &item : WitchItems) {
		WitchBookLevel(item);
		item._iStatFlag = MyPlayer->CanUseItem(item);
		CurrentItemIndex++;
	}
	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

bool WitchSellOk(int i)
{
	const Item *pI;

	if (i >= 0)
		pI = &MyPlayer->InvList[i];
	else
		pI = &MyPlayer->SpdList[-(i + 1)];

	return WitchWillBuy(*pI);
}

void StartWitchSell()
{
	IsTextFullSize = true;
	bool sellok = false;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	const Player &myPlayer = *MyPlayer;

	for (int i = 0; i < myPlayer._pNumInv; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (WitchSellOk(i)) {
			sellok = true;
			PlayerItems[CurrentItemIndex] = myPlayer.InvList[i];

			if (PlayerItems[CurrentItemIndex]._iMagical != ITEM_QUALITY_NORMAL && PlayerItems[CurrentItemIndex]._iIdentified)
				PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._iIvalue;

			PlayerItems[CurrentItemIndex]._ivalue = std::max(PlayerItems[CurrentItemIndex]._ivalue / 4, 1);
			PlayerItems[CurrentItemIndex]._iIvalue = PlayerItems[CurrentItemIndex]._ivalue;
			PlayerItemIndexes[CurrentItemIndex] = i;
			CurrentItemIndex++;
		}
	}

	for (int i = 0; i < MaxBeltItems; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (!myPlayer.SpdList[i].isEmpty() && WitchSellOk(-(i + 1))) {
			sellok = true;
			PlayerItems[CurrentItemIndex] = myPlayer.SpdList[i];

			if (PlayerItems[CurrentItemIndex]._iMagical != ITEM_QUALITY_NORMAL && PlayerItems[CurrentItemIndex]._iIdentified)
				PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._iIvalue;

			PlayerItems[CurrentItemIndex]._ivalue = std::max(PlayerItems[CurrentItemIndex]._ivalue / 4, 1);
			PlayerItems[CurrentItemIndex]._iIvalue = PlayerItems[CurrentItemIndex]._ivalue;
			PlayerItemIndexes[CurrentItemIndex] = -(i + 1);
			CurrentItemIndex++;
		}
	}

	if (!sellok) {
		HasScrollbar = false;

		RenderGold = true;
		AddSText(20, 1, _("You have nothing I want."), UiFlags::ColorWhitegold, false);

		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = CurrentItemIndex;

	RenderGold = true;
	AddSText(20, 1, _("Which item is for sale?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

bool WitchRechargeOk(int i)
{
	const auto &item = MyPlayer->InvList[i];

	if (item._itype == ItemType::Staff && item._iCharges != item._iMaxCharges) {
		return true;
	}

	if ((item._iMiscId == IMISC_UNIQUE || item._iMiscId == IMISC_STAFF) && item._iCharges < item._iMaxCharges) {
		return true;
	}

	return false;
}

void AddStoreHoldRecharge(Item itm, int8_t i)
{
	PlayerItems[CurrentItemIndex] = itm;
	PlayerItems[CurrentItemIndex]._ivalue += GetSpellData(itm._iSpell).staffCost();
	PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._ivalue * (PlayerItems[CurrentItemIndex]._iMaxCharges - PlayerItems[CurrentItemIndex]._iCharges) / (PlayerItems[CurrentItemIndex]._iMaxCharges * 2);
	PlayerItems[CurrentItemIndex]._iIvalue = PlayerItems[CurrentItemIndex]._ivalue;
	PlayerItemIndexes[CurrentItemIndex] = i;
	CurrentItemIndex++;
}

void StartWitchRecharge()
{
	IsTextFullSize = true;
	bool rechargeok = false;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	const Player &myPlayer = *MyPlayer;
	const auto &leftHand = myPlayer.InvBody[INVLOC_HAND_LEFT];

	if ((leftHand._itype == ItemType::Staff || leftHand._iMiscId == IMISC_UNIQUE) && leftHand._iCharges != leftHand._iMaxCharges) {
		rechargeok = true;
		AddStoreHoldRecharge(leftHand, -1);
	}

	for (int i = 0; i < myPlayer._pNumInv; i++) {
		if (CurrentItemIndex >= 48)
			break;
		if (WitchRechargeOk(i)) {
			rechargeok = true;
			AddStoreHoldRecharge(myPlayer.InvList[i], i);
		}
	}

	if (!rechargeok) {
		HasScrollbar = false;

		RenderGold = true;
		AddSText(20, 1, _("You have nothing to recharge."), UiFlags::ColorWhitegold, false);
		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = myPlayer._pNumInv;

	RenderGold = true;
	AddSText(20, 1, _("Recharge which item?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

void StoreNoMoney()
{
	StartStore(OldActiveStore);
	HasScrollbar = false;
	IsTextFullSize = true;
	RenderGold = true;
	ClearSText(5, 23);
	AddSText(0, 14, _("You do not have enough gold"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

void StoreNoRoom()
{
	StartStore(OldActiveStore);
	HasScrollbar = false;
	ClearSText(5, 23);
	AddSText(0, 14, _("You do not have enough room in inventory"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

// [RUNS MOD] --- Dedicated dungeon-scroll merchant (buy + crafting) ---

// Crafting state (see the crafting functions below): the chosen operation and the
// inventory slots of the target/material scrolls. Upgrading fuses THREE materials
// (each of the target's level or higher); reroll/add-modifier consume one.
enum class ScrollCraftOp : uint8_t { Reroll, Upgrade, AddMod };
ScrollCraftOp CraftOp = ScrollCraftOp::Reroll;
int CraftTargetInv = -1;
constexpr int MaxCraftMaterials = 3;
int CraftMaterialInvs[MaxCraftMaterials] = { -1, -1, -1 };
int CraftMaterialCount = 0;
// Snapshot of the pick count taken when the confirm opens. CraftApply MUST use this,
// not CraftMaterialCount: rendering the confirm re-renders the material screen
// underneath it (StoreConfirm -> StartStore(OldActiveStore)), whose cancelled-confirm
// clamp decrements CraftMaterialCount -- which used to make CraftApply silently bail
// on every craft (no gold taken, nothing applied).
int CraftMaterialsLocked = 0;

int MaterialsRequired()
{
	return CraftOp == ScrollCraftOp::Upgrade ? MaxCraftMaterials : 1;
}

bool IsPickedMaterial(int invIndex)
{
	for (int i = 0; i < CraftMaterialCount; i++)
		if (CraftMaterialInvs[i] == invIndex)
			return true;
	return false;
}

void StartDungeonVendor()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Dungeon Merchant"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Buy dungeon scrolls"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 14, _("Reroll a scroll's modifiers"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 16, _("Upgrade a scroll's level"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Add a modifier to a scroll"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 20, _("Leave"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
	CurrentItemIndex = 22;
}

void ScrollDungeonVendorBuy(int idx)
{
	ScrollVendorStore(DungeonVendorItems, static_cast<int>(DungeonVendorItems.size()), idx);
}

void StartDungeonVendorBuy()
{
	IsTextFullSize = true;
	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = 20;

	RenderGold = true;
	AddSText(20, 1, _("I have these scrolls for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollDungeonVendorBuy(ScrollPos);
	AddItemListBackButton();

	CurrentItemIndex = 0;
	for (Item &item : DungeonVendorItems) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
		CurrentItemIndex++;
	}
	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

void DungeonVendorEnter()
{
	switch (CurrentTextLine) {
	case 12:
		StartStore(TalkID::DungeonVendorBuy);
		break;
	case 14:
		CraftOp = ScrollCraftOp::Reroll;
		StartStore(TalkID::DungeonVendorCraftTarget);
		break;
	case 16:
		CraftOp = ScrollCraftOp::Upgrade;
		StartStore(TalkID::DungeonVendorCraftTarget);
		break;
	case 18:
		CraftOp = ScrollCraftOp::AddMod;
		StartStore(TalkID::DungeonVendorCraftTarget);
		break;
	case 20:
		ActiveStore = TalkID::None;
		break;
	}
}

void DungeonVendorBuyItem(Item &item)
{
	const int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	if (idx < 0 || idx >= static_cast<int>(DungeonVendorItems.size()))
		return;
	TakePlrsMoney(item._iIvalue);
	StoreAutoPlace(item, true);
	// [RUNS MOD] The Scroll of Preservation is always in stock: buying it refreshes
	// the slot with a fresh one (its 50,000g price is the limiter, not scarcity).
	if (item._iMiscId == IMISC_PRESERVE) {
		Item restock = {};
		InitializeItem(restock, IDI_PRESERVESCROLL);
		GenerateNewSeed(restock);
		restock._iIdentified = true;
		restock._iStatFlag = true;
		DungeonVendorItems[idx] = restock;
		CalcPlrInv(*MyPlayer, true);
		return;
	}
	// Stock shrinks as you buy, down to a floor of one: while more than one remains,
	// remove the purchased scroll; the last scroll instead refreshes to a new one, so
	// a run can always be started. A full restock happens after a dungeon is completed.
	if (DungeonVendorItems.size() > 1) {
		DungeonVendorItems.erase(DungeonVendorItems.begin() + idx);
	} else {
		Item restock = {};
		CreateDungeonScroll(restock, item._iCreateInfo & CF_LEVEL);
		restock._iStatFlag = true;
		DungeonVendorItems[idx] = restock;
	}
	CalcPlrInv(*MyPlayer, true);
}

void DungeonVendorBuyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::DungeonVendor);
		CurrentTextLine = 12;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;
	OldActiveStore = TalkID::DungeonVendorBuy;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);
	if (idx < 0 || idx >= static_cast<int>(DungeonVendorItems.size()))
		return; // clicked an empty line (the list shrank as scrolls were bought)

	if (!PlayerCanAfford(DungeonVendorItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(DungeonVendorItems[idx], false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = DungeonVendorItems[idx];
	StartStore(TalkID::Confirm);
}

// [RUNS MOD] --- Farnham's fruit machine ---
// Three weighted reels. The outcome is rolled AND paid out the moment the handle is
// pulled, so the spin screen (and its future reel animation) is pure presentation --
// it can never desync from the payout, and quitting mid-show loses nothing. Gold
// prizes go straight to the inventory; item prizes drop at the player's feet: the
// machine spits them out, it does not open bag-room dialogs.

enum class GambleSymbol : uint8_t { Gold, Sword, Scroll, Potion, Skull, Crown };
constexpr int NumGambleSymbols = 6;
// Per-reel symbol weights (sum 27). Retuned after playtest: the prize symbols were the
// two rarest on the reels (sword 1 in 308, crown 1 in 2,460), so a session could run for
// hours and only ever see the potion and skull triples -- the machine read as "expensive
// for nothing". Sword joins the common tier and crown moves 2 -> 3, putting the jackpot
// at ~1 in 729: still a lottery, but one a real player can expect to hit. Uniques stay
// bounded by boss kills regardless, since a claimed jackpot only rerolls on a boss death.
constexpr int GambleSymbolWeights[NumGambleSymbols] = { 5, 5, 4, 5, 5, 3 };
constexpr int GambleWeightTotal = 27;
/** Full healing potions paid by the triple-potion row. They drop at the player's feet,
 * so this is bounded by what is reasonable to pick up rather than by inventory space. */
constexpr int GamblePotionPrizeCount = 8;

GambleSymbol GambleReels[3];
/** The result headline shown on the spin screen (set by ResolveGamble). */
std::string GambleResultLine;
/** Item prizes won at pull time but not yet dropped: the payout is decided instantly,
 * the visible drop waits for the last reel (playtest: a prize thudding onto the floor
 * mid-spin spoiled the show). Flushed at reveal and on any early exit/re-pull. */
std::vector<Item> GamblePendingDrops;
/** True when the pull paid out anything (drives the reveal sound). */
bool GambleWon;
/** Reel animation: spin start time, per-stop click bookkeeping, reveal latch. The
 * reels stop left to right at these times; the result shows after the last stop. */
uint32_t GambleSpinStartTicks;
int GambleReelStopsPlayed;
bool GambleSpinDone;
constexpr uint32_t GambleReelStopMs[3] = { 750, 1250, 1750 };

int GambleStake()
{
	// Scales with earned depth so the machine stays a real gold sink for rich
	// late-game ghosts without walling off fresh characters.
	return 500 + 250 * MyPlayer->pDeepestBeatenDepth;
}

// [RUNS MOD] Gambler's Tokens: dungeon-dropped chips, each worth one free pull at
// full payouts (a token's value therefore scales with the stake, like everything
// else on the machine).
int CountGambleTokens(const Player &player)
{
	int count = 0;
	for (int i = 0; i < player._pNumInv; i++) {
		if (player.InvList[i].IDidx == IDI_GAMBLETOKEN)
			count++;
	}
	return count;
}

void ConsumeGambleToken(Player &player)
{
	// Highest index first: RemoveInvItem compacts InvList downward.
	for (int i = player._pNumInv - 1; i >= 0; i--) {
		if (player.InvList[i].IDidx == IDI_GAMBLETOKEN) {
			player.RemoveInvItem(i);
			return;
		}
	}
}

GambleSymbol RollGambleSymbol()
{
	int roll = GenerateRnd(GambleWeightTotal);
	for (int s = 0; s < NumGambleSymbols - 1; s++) {
		roll -= GambleSymbolWeights[s];
		if (roll < 0)
			return static_cast<GambleSymbol>(s);
	}
	return static_cast<GambleSymbol>(NumGambleSymbols - 1);
}

void AwardGambleGold(int amount)
{
	Player &myPlayer = *MyPlayer;
	AddGoldToInventory(myPlayer, amount);
	myPlayer._pGold += amount;
	// No sound here: payouts apply at pull time, but the reveal (and its jingle)
	// waits for the last reel to land.
	GambleWon = true;
}

void FlushGamblePrizes()
{
	for (Item &prize : GamblePendingDrops)
		DropPrizeItem(prize, MyPlayer->position.tile);
	GamblePendingDrops.clear();
}

void ResolveGamble(bool tokenSpin = false)
{
	Player &myPlayer = *MyPlayer;
	// A mashed re-pull mid-show delivers the previous spin's prizes immediately.
	FlushGamblePrizes();
	// [RUNS MOD] A token buys the pull outright; payouts still key off the full
	// stake value, so a token is worth exactly one gold pull wherever you stand.
	const int stake = GambleStake();
	if (tokenSpin)
		ConsumeGambleToken(myPlayer);
	else
		TakePlrsMoney(stake);
	GambleWon = false;
	for (GambleSymbol &reel : GambleReels)
		reel = RollGambleSymbol();

	int counts[NumGambleSymbols] = {};
	for (const GambleSymbol reel : GambleReels)
		counts[static_cast<size_t>(reel)]++;
	int best = 0;
	for (int s = 1; s < NumGambleSymbols; s++) {
		if (counts[s] > counts[best])
			best = s;
	}
	const auto symbol = static_cast<GambleSymbol>(best);
	const int matched = counts[best];

	// Prize items are capped at EARNED depth, same rule as the vendor and the craft:
	// town can never hand out more than the hero has proven they can reach. `accessDepth`
	// is the deepest run the hero may enter -- the vendor's scroll cap.
	const int accessDepth = ClampRunDepth(myPlayer.pDeepestBeatenDepth + 1);
	// [RUNS MOD] The item prize is levelled at 2 x accessDepth -- the tier that depth's
	// boss vault pays -- rather than the raw depth. Triple sword is ~1 in 308 pulls, so
	// at this stake a prize costs several hundred thousand gold on average; at the old
	// raw-depth level its base pool (iMinMLvl <= 4 for a depth-4 hero) and affixes were
	// beaten by a routine vault drop and by Griswold, which made the machine a worse
	// item source than simply playing. With uper 15 on top (see RollGamblePrizeItem)
	// this lands at the boss-unique tier for the deepest run the hero can enter: the
	// rarest payout on the machine is worth the pulls it took.
	//
	// Still bounded by earned depth, so this cannot be farmed from a shallow scroll --
	// it moves only when a boss dies.
	const int prizeLevel = 2 * accessDepth;
	// Scroll prizes stay on the depth scale (they ARE depths), capped at accessDepth so
	// the machine can never hand out a run deeper than the boss ladder has unlocked --
	// that would make it a progression bypass. Raised from pDeepestBeatenDepth, which
	// paid a scroll strictly worse than one the vendor already sells for depth x 50.
	const int scrollPrizeLevel = accessDepth;

	// Payouts, third pass. The first paid ~35% and felt like a mugging; the second claimed
	// 65-70% but actually returned 61% in gold, with the two item prizes so rare that a
	// long session saw nothing but potion and skull triples. This pass targets ~75%.
	//
	// Return to player, in stake units, over the 19683 equally likely reel combinations
	// (weights 5/5/4/5/5/3, jackpot already claimed so crowns pay gold):
	//
	//   pair, non-crown   7704 x 1   pairs are 42.4% of pulls and keep the wallet alive
	//   pair, crown        648 x 6
	//   triple gold        125 x 15
	//   triple crown        27 x 15
	//   triple potion      125 x 3    + 8 full heals
	//   triple scroll       64 x 2    + a dungeon scroll
	//   triple skull       125 x 1    stake back -- see below
	//   triple sword       125 x 0    the item prize IS the payout
	//                    ---------
	//                    14500 / 19683 = 73.7% in gold, ~74% counting the potions and
	//                                    scroll at shop value; the sword prize (1 in 157)
	//                                    and the jackpot ride on top of that.
	//
	// Triple skull returning the stake is deliberate: three matching symbols paying
	// literally nothing is the worst outcome a slot can show, and at the old weights it
	// was as common as the potion triple. Farnham keeps the joke, not the money.
	if (matched == 3) {
		switch (symbol) {
		case GambleSymbol::Crown:
			if (!GamblerJackpot.isEmpty()) {
				GamblePendingDrops.push_back(GamblerJackpot);
				GambleResultLine = fmt::format(fmt::runtime(_("JACKPOT! {:s} is yours!")), GamblerJackpot.getName().str());
				GamblerJackpot.clear(); // claimed at pull time so a mashed re-pull can't double-claim
				GambleWon = true;
			} else {
				// The displayed prize was already claimed; crowns still pay big gold.
				AwardGambleGold(stake * 15);
				GambleResultLine = fmt::format(fmt::runtime(_("Three crowns! {:s} gold!")), FormatInteger(stake * 15));
			}
			break;
		case GambleSymbol::Gold:
			AwardGambleGold(stake * 15);
			GambleResultLine = fmt::format(fmt::runtime(_("Three gold piles! {:s} gold!")), FormatInteger(stake * 15));
			break;
		case GambleSymbol::Sword: {
			GamblePendingDrops.push_back(RollGamblePrizeItem(prizeLevel));
			GambleWon = true;
			GambleResultLine = _("Three swords! The machine spits out a prize!");
			break;
		}
		case GambleSymbol::Scroll: {
			Item scroll = {};
			CreateDungeonScroll(scroll, scrollPrizeLevel);
			GamblePendingDrops.push_back(scroll);
			AwardGambleGold(stake * 2);
			GambleResultLine = fmt::format(fmt::runtime(_("Three scrolls! A dungeon scroll, and {:s} gold!")), FormatInteger(stake * 2));
			break;
		}
		case GambleSymbol::Potion:
			// A round for the house: two potions read as a shrug for a 1-in-157 hit.
			for (int i = 0; i < GamblePotionPrizeCount; i++) {
				Item potion = {};
				InitializeItem(potion, IDI_FULLHEAL);
				GenerateNewSeed(potion);
				GamblePendingDrops.push_back(potion);
			}
			AwardGambleGold(stake * 3);
			GambleResultLine = fmt::format(fmt::runtime(_("Three potions! A round for the house, and {:s} gold!")), FormatInteger(stake * 3));
			break;
		case GambleSymbol::Skull:
			AwardGambleGold(stake);
			GambleResultLine = _("Three skulls. Farnham laughs, then slides your stake back.");
			break;
		}
	} else if (matched == 2) {
		if (symbol == GambleSymbol::Crown) {
			AwardGambleGold(stake * 6);
			GambleResultLine = fmt::format(fmt::runtime(_("Two crowns -- so close! {:s} gold.")), FormatInteger(stake * 6));
		} else {
			AwardGambleGold(stake);
			GambleResultLine = _("A pair. Your stake back.");
		}
	} else {
		GambleResultLine = _("Nothing. The house drinks well tonight.");
	}
}

void StartGamble()
{
	// Entering (or returning to) the menu delivers any prizes still queued behind an
	// interrupted spin -- nothing won can ever be lost to an early exit.
	FlushGamblePrizes();
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 1, _("Farnham's One-Armed Bandit"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(3);
	if (!GamblerJackpot.isEmpty()) {
		AddSText(0, 7, _("Tonight's jackpot:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSText(0, 9, GamblerJackpot.getName(), GamblerJackpot.getTextColorWithStatCheck() | UiFlags::AlignCenter, false);
	} else {
		AddSText(0, 7, _("The jackpot is claimed!"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
		AddSText(0, 9, _("Beat a boss for a new prize."), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
	}
	AddSText(0, 12, fmt::format(fmt::runtime(_("Pull the handle ({:s} gold)")), FormatInteger(GambleStake())), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	// [RUNS MOD] Tokens buy free pulls; the line only shows while you hold some.
	// Line numbers are fixed (12 gold / 14 token / 16 jackpot / 18 goodbye) so the
	// enter handler never has to guess.
	if (const int tokens = CountGambleTokens(*MyPlayer); tokens > 0)
		AddSText(0, 14, fmt::format(fmt::runtime(_("Pull with a token ({:d} left)")), tokens), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	if (!GamblerJackpot.isEmpty())
		AddSText(0, 16, _("Inspect the jackpot"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Say goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

void StartGambleSpin()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	AddSText(0, 1, _("Farnham's One-Armed Bandit"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(3);
	// Lines 8 (the reels) and 12 (the result) are deliberately left empty: they are
	// drawn per-frame by DrawGambleReels so the reels spin. "Pull again" stays live
	// during the show -- gamblers mash, and a re-pull just restarts the spin.
	// [RUNS MOD] Token re-pulls sit above the gold pull, shown only while held.
	if (const int tokens = CountGambleTokens(*MyPlayer); tokens > 0)
		AddSText(0, 14, fmt::format(fmt::runtime(_("Pull again with a token ({:d} left)")), tokens), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 16, fmt::format(fmt::runtime(_("Pull again ({:s} gold)")), FormatInteger(GambleStake())), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, BackButtonLine(), _("Back"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	GambleSpinStartTicks = static_cast<uint32_t>(SDL_GetTicks());
	GambleReelStopsPlayed = 0;
	GambleSpinDone = false;
}

// [RUNS MOD] Each reel face is a real item icon from the cursor sheet -- text reels
// were unreadable in playtests. Icons are deliberately iconic silhouettes: gold pile,
// short sword, scroll, red potion, skull cap and crown.
int GambleSymbolCursor(GambleSymbol symbol)
{
	switch (symbol) {
	case GambleSymbol::Gold: return ICURS_GOLD_LARGE;
	case GambleSymbol::Sword: return ICURS_SHORT_SWORD;
	case GambleSymbol::Scroll: return ICURS_SCROLL_OF;
	case GambleSymbol::Potion: return ICURS_POTION_OF_FULL_HEALING;
	case GambleSymbol::Skull: return ICURS_SKULL_CAP;
	case GambleSymbol::Crown: return ICURS_CROWN;
	}
	return ICURS_GOLD_SMALL;
}

void DrawGambleReels(const Surface &out)
{
	// [RUNS MOD] The spinning reels: pure presentation over the static panel. The
	// outcome (GambleReels + payout) was locked in at pull time, so nothing here can
	// desync -- this only decides WHEN each reel shows its final face.
	//
	// Each reel draws CENTERED in its own fixed column rect via DrawString directly.
	// PrintSString cannot place columns: its `margin` narrows the line from BOTH
	// sides (stores.cpp PrintSString: width -= margin * 2), which truncated the
	// middle reel to ~35px and gave the right reel negative width (playtest
	// screenshot: three chars then a wrap, third reel invisible). The geometry
	// below mirrors PrintSString's: text area starts at uiPosition.x + 32 and is
	// 575 wide for the full-size panel.
	const Point uiPosition = GetUIRectangle().position;
	const int baseX = uiPosition.x + 32;
	// Icons stand on a common shelf (the bottom of line 11): mixed icon heights
	// (28px potions to 84px swords) grow upward into the panel's empty lines, and
	// the result line at 12 stays clear.
	const int shelfY = uiPosition.y + PaddingTop + TextLine[11].y + TextLine[11]._syoff;
	constexpr int PanelTextWidth = 575;
	constexpr int ColumnSpacing = 170;
	const int mid = PanelTextWidth / 2;

	const uint32_t elapsed = static_cast<uint32_t>(SDL_GetTicks()) - GambleSpinStartTicks;
	int stopped = 0;
	for (int i = 0; i < 3; i++) {
		GambleSymbol shown;
		if (elapsed >= GambleReelStopMs[i]) {
			stopped++;
			shown = GambleReels[i];
		} else {
			// Cycle a new face every 50 ms, each reel offset so they never look locked.
			shown = static_cast<GambleSymbol>((elapsed / 50 + static_cast<uint32_t>(i) * 2) % NumGambleSymbols);
		}
		const int center = mid + (i - 1) * ColumnSpacing;
		const ClxSprite sprite = GetInvItemSprite(static_cast<int>(CURSOR_FIRSTITEM) + GambleSymbolCursor(shown));
		ClxDraw(out, { baseX + center - sprite.width() / 2, shelfY }, sprite);
	}
	// One click per reel landing; the payout jingle only after the last one.
	while (GambleReelStopsPlayed < stopped) {
		PlaySFX(SfxID::MenuMove);
		GambleReelStopsPlayed++;
	}
	if (stopped == 3) {
		if (!GambleSpinDone) {
			GambleSpinDone = true;
			// The show is over: prizes hit the floor now, with the jingle.
			FlushGamblePrizes();
			if (GambleWon)
				PlaySFX(SfxID::ItemGold);
		}
		PrintSString(out, 0, 12, GambleResultLine, (GambleWon ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) | UiFlags::AlignCenter);
	}
}

void StartGambleJackpotShow()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	AddSText(0, 1, _("Farnham's One-Armed Bandit"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(3);
	AddSText(0, 5, _("Tonight's jackpot:"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
	const UiFlags itemColor = GamblerJackpot.getTextColorWithStatCheck();
	AddSText(0, 7, GamblerJackpot.getName(), itemColor | UiFlags::AlignCenter, false);
	// A unique's powers live in its UniqueItems row, not in the prefix/suffix slots
	// PrintStoreItem reads (vanilla stores never sell uniques), so list them the way the
	// inventory's unique info panel does: one per line, up to six (lines 9-14).
	int line = 9;
	for (const ItemPower &power : UniqueItems[GamblerJackpot._iUid].powers) {
		if (power.type == IPL_INVALID)
			break;
		AddSText(0, line++, PrintItemPower(power.type, GamblerJackpot), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
	}
	// Damage/armor, durability and requirements.
	PrintStoreItem(GamblerJackpot, 16, itemColor);
	AddSText(0, BackButtonLine(), _("Done"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

void GambleEnter()
{
	switch (CurrentTextLine) {
	case 12:
		if (!PlayerCanAfford(GambleStake())) {
			OldActiveStore = TalkID::Gamble;
			OldTextLine = 12;
			OldScrollPos = 0;
			StartStore(TalkID::NoMoney);
			return;
		}
		ResolveGamble();
		StartStore(TalkID::GambleSpin);
		break;
	// [RUNS MOD] A token pull: free spin, one chip consumed.
	case 14:
		if (CountGambleTokens(*MyPlayer) > 0) {
			ResolveGamble(/*tokenSpin=*/true);
			StartStore(TalkID::GambleSpin);
		}
		break;
	case 16:
		if (!GamblerJackpot.isEmpty())
			StartStore(TalkID::GambleJackpotShow);
		break;
	case 18:
		ActiveStore = TalkID::None;
		break;
	}
}

void GambleSpinEnter()
{
	// [RUNS MOD] Token re-pull: free spin while chips last.
	if (CurrentTextLine == 14) {
		if (CountGambleTokens(*MyPlayer) > 0) {
			ResolveGamble(/*tokenSpin=*/true);
			StartStore(TalkID::GambleSpin);
		}
		return;
	}
	if (CurrentTextLine == 16) { // pull again
		if (!PlayerCanAfford(GambleStake())) {
			OldActiveStore = TalkID::Gamble;
			OldTextLine = 12;
			OldScrollPos = 0;
			StartStore(TalkID::NoMoney);
			return;
		}
		ResolveGamble();
		StartStore(TalkID::GambleSpin);
		return;
	}
	StartStore(TalkID::Gamble);
	CurrentTextLine = 12;
}

// [RUNS MOD] Scroll crafting: reroll a scroll's modifiers, upgrade its level, or add a
// modifier -- each for gold plus another scroll consumed as material (so scrolls are a
// currency). Flow: pick an operation from the menu -> pick the target scroll -> pick the
// material scroll -> confirm. Requires at least two scrolls.

int CraftGoldCost(const Item &scroll)
{
	const int level = std::max(1, DungeonScrollLevel(scroll));
	switch (CraftOp) {
	case ScrollCraftOp::Upgrade: {
		const int target = level + 1;
		// [RUNS MOD] Within EARNED depth (the vendor already sells this level) the
		// craft is just "keep your affixes": it costs the shelf price of the new
		// level, so catching a stash of low scrolls up to your depth is routine.
		if (target <= ClampRunDepth(MyPlayer->pDeepestBeatenDepth + 1))
			return target * 50;
		// Beyond the earned range, upgrading is the ghost-run path to depth (farm
		// wealth, buy your way deeper without ever facing a boss), so it must be a
		// REAL gold sink: quadratic in the target level. L4->5 2,500, L9->10 10,000,
		// L14->15 22,500 -- a serious investment at depth, and always pricier than
		// just beating the boss for the free next-depth scroll.
		return target * target * 100;
	}
	case ScrollCraftOp::AddMod:
		return level * 200;
	case ScrollCraftOp::Reroll:
	default:
		return level * 75;
	}
}

// Whether `scroll` can undergo the current operation (upgrade needs headroom in level,
// add-modifier needs fewer than the max modifiers; reroll is always valid).
bool CraftValid(const Item &scroll)
{
	switch (CraftOp) {
	case ScrollCraftOp::Upgrade:
		return DungeonScrollLevel(scroll) < GetMaxRunDepth();
	case ScrollCraftOp::AddMod:
		return DungeonScrollModCount(scroll) < MaxScrollMods;
	case ScrollCraftOp::Reroll:
	default:
		return true;
	}
}

std::string_view CraftTargetPrompt()
{
	switch (CraftOp) {
	case ScrollCraftOp::Upgrade:
		return _("Upgrade which scroll's level?");
	case ScrollCraftOp::AddMod:
		return _("Add a modifier to which scroll?");
	case ScrollCraftOp::Reroll:
	default:
		return _("Reroll which scroll's modifiers?");
	}
}

int CountDungeonScrolls()
{
	const Player &player = *MyPlayer;
	int n = 0;
	for (int i = 0; i < player._pNumInv; i++)
		if (player.InvList[i]._iMiscId == IMISC_DUNGEONSCROLL)
			n++;
	return n;
}

// [RUNS MOD] Whether `material` may be consumed for the current op on `target`.
// Upgrading fuses scrolls: the material must match or exceed the target's level, so
// pushing past a depth requires a second scroll OF that depth -- otherwise cheap
// level-1 vendor scrolls would make gold the only gate and boss-skipping trivial.
// Reroll/Add-modifier don't change depth, so any scroll burns.
bool CraftMaterialValid(const Item &target, const Item &material)
{
	if (CraftOp != ScrollCraftOp::Upgrade)
		return true;
	return DungeonScrollLevel(material) >= DungeonScrollLevel(target);
}

// Fills PlayerItems with the player's dungeon scrolls; if `forCraft` only those valid
// for the current op, skipping the inventory slot `exceptInv` (the chosen target).
// When `exceptInv` names a chosen target (material step), only valid materials list.
void BuildScrollList(bool forCraft, int exceptInv)
{
	CurrentItemIndex = 0;
	for (auto &item : PlayerItems)
		item.clear();
	const Player &player = *MyPlayer;
	for (int i = 0; i < player._pNumInv && CurrentItemIndex < 48; i++) {
		if (i == exceptInv)
			continue;
		const Item &inv = player.InvList[i];
		if (inv._iMiscId != IMISC_DUNGEONSCROLL)
			continue;
		if (forCraft && !CraftValid(inv))
			continue;
		if (!forCraft && IsPickedMaterial(i))
			continue;
		if (!forCraft && exceptInv >= 0 && exceptInv < player._pNumInv
		    && !CraftMaterialValid(player.InvList[exceptInv], inv))
			continue;
		PlayerItems[CurrentItemIndex] = inv;
		if (forCraft)
			PlayerItems[CurrentItemIndex]._ivalue = PlayerItems[CurrentItemIndex]._iIvalue = CraftGoldCost(inv);
		PlayerItemIndexes[CurrentItemIndex] = i;
		CurrentItemIndex++;
	}
}

void ShowScrollListNotice(std::string_view text)
{
	HasScrollbar = false;
	RenderGold = true;
	AddSText(20, 1, text, UiFlags::ColorWhitegold, false);
	AddSLine(3);
	AddItemListBackButton(/*selectable=*/true);
}

void StartDungeonVendorCraftTarget()
{
	IsTextFullSize = true;
	CraftMaterialCount = 0; // fresh target choice restarts the material picks
	CraftMaterialsLocked = 0;
	if (CountDungeonScrolls() < 1 + MaterialsRequired()) {
		CurrentItemIndex = 0;
		ShowScrollListNotice(CraftOp == ScrollCraftOp::Upgrade
		        ? _("Upgrading fuses scrolls: you need four (three are consumed).")
		        : _("You need two scrolls to craft (one is consumed)."));
		return;
	}
	BuildScrollList(/*forCraft=*/true, /*exceptInv=*/-1);
	if (CurrentItemIndex == 0) {
		ShowScrollListNotice(_("No scroll can be crafted this way."));
		return;
	}
	HasScrollbar = true;
	ScrollPos = 0;
	RenderGold = true;
	AddSText(20, 1, CraftTargetPrompt(), UiFlags::ColorWhitegold, false);
	AddSLine(3);
	ScrollVendorStore(PlayerItems, CurrentItemIndex, ScrollPos, false);
	AddItemListBackButton();
	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

void StartDungeonVendorCraftMaterial()
{
	IsTextFullSize = true;
	// Returning here from a cancelled confirm re-opens the last pick.
	if (CraftMaterialCount >= MaterialsRequired())
		CraftMaterialCount = MaterialsRequired() - 1;
	BuildScrollList(/*forCraft=*/false, /*exceptInv=*/CraftTargetInv);
	if (CurrentItemIndex == 0) {
		ShowScrollListNotice(CraftOp == ScrollCraftOp::Upgrade
		        ? _("Upgrading fuses three scrolls of the target's level or higher.")
		        : _("You have no other scroll to use as material."));
		return;
	}
	HasScrollbar = true;
	ScrollPos = 0;
	RenderGold = true;
	if (MaterialsRequired() > 1) {
		AddSText(20, 1,
		    fmt::format(fmt::runtime(_("Consume which scroll? ({:d} of {:d})")), CraftMaterialCount + 1, MaterialsRequired()),
		    UiFlags::ColorWhitegold, false);
	} else {
		AddSText(20, 1, _("Consume which scroll as material?"), UiFlags::ColorWhitegold, false);
	}
	AddSLine(3);
	ScrollVendorStore(PlayerItems, CurrentItemIndex, ScrollPos, false);
	AddItemListBackButton();
	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

void CraftApply()
{
	Player &player = *MyPlayer;
	if (CraftTargetInv < 0 || CraftTargetInv >= player._pNumInv)
		return;
	Item &target = player.InvList[CraftTargetInv];
	if (target._iMiscId != IMISC_DUNGEONSCROLL)
		return;
	// Re-validate every material at apply time (indices can go stale between screens):
	// all distinct, none the target, each a scroll valid for the operation. The count
	// comes from the confirm-time snapshot; see CraftMaterialsLocked.
	if (CraftMaterialsLocked != MaterialsRequired())
		return;
	for (int i = 0; i < CraftMaterialsLocked; i++) {
		const int inv = CraftMaterialInvs[i];
		if (inv < 0 || inv >= player._pNumInv || inv == CraftTargetInv)
			return;
		for (int j = 0; j < i; j++)
			if (CraftMaterialInvs[j] == inv)
				return;
		if (player.InvList[inv]._iMiscId != IMISC_DUNGEONSCROLL
		    || !CraftMaterialValid(target, player.InvList[inv]))
			return;
	}

	TakePlrsMoney(CraftGoldCost(target));
	switch (CraftOp) {
	case ScrollCraftOp::Reroll:
		GenerateNewSeed(target); // new seed => new affixes (same level and count)
		break;
	case ScrollCraftOp::Upgrade: {
		const int newLevel = NextRunDepth(DungeonScrollLevel(target));
		target._iCreateInfo = MakeDungeonScrollCreateInfo(newLevel, DungeonScrollModCount(target), DungeonScrollDifficulty(target));
		target._ivalue = target._iIvalue = newLevel * 50;
		break;
	}
	case ScrollCraftOp::AddMod: {
		const int newCount = std::min(DungeonScrollModCount(target) + 1, MaxScrollMods);
		target._iCreateInfo = MakeDungeonScrollCreateInfo(DungeonScrollLevel(target), newCount, DungeonScrollDifficulty(target));
		break;
	}
	}
	// Remove materials from the highest inventory index down: RemoveInvItem compacts
	// InvList, so descending order keeps the remaining picked indices valid.
	int sorted[MaxCraftMaterials];
	std::copy(CraftMaterialInvs, CraftMaterialInvs + CraftMaterialsLocked, sorted);
	std::sort(sorted, sorted + CraftMaterialsLocked, std::greater<>());
	for (int i = 0; i < CraftMaterialsLocked; i++)
		player.RemoveInvItem(sorted[i]);
	CraftMaterialCount = 0;
	CraftMaterialsLocked = 0;
	CalcPlrInv(player, true);
}

void DungeonVendorCraftTargetEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::DungeonVendor);
		CurrentTextLine = 14;
		return;
	}
	if (CurrentItemIndex == 0)
		return; // a notice screen; only the back button is active
	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);
	if (idx < 0 || idx >= CurrentItemIndex)
		return;
	CraftTargetInv = PlayerItemIndexes[idx];
	StartStore(TalkID::DungeonVendorCraftMaterial);
}

void DungeonVendorCraftMaterialEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		// Back steps through the picks before leaving the material screen.
		if (CraftMaterialCount > 0) {
			CraftMaterialCount--;
			StartStore(TalkID::DungeonVendorCraftMaterial);
		} else {
			StartStore(TalkID::DungeonVendorCraftTarget);
		}
		return;
	}
	if (CurrentItemIndex == 0)
		return;
	if (CraftTargetInv < 0 || CraftTargetInv >= MyPlayer->_pNumInv) {
		StartStore(TalkID::DungeonVendor);
		return;
	}
	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);
	if (idx < 0 || idx >= CurrentItemIndex)
		return;
	if (!PlayerCanAfford(CraftGoldCost(MyPlayer->InvList[CraftTargetInv]))) {
		StartStore(TalkID::NoMoney);
		return;
	}
	CraftMaterialInvs[CraftMaterialCount++] = PlayerItemIndexes[idx];
	if (CraftMaterialCount < MaterialsRequired()) {
		StartStore(TalkID::DungeonVendorCraftMaterial); // pick the next material
		return;
	}
	// Snapshot the full pick count NOW: the confirm's own render clamps
	// CraftMaterialCount back down (see CraftMaterialsLocked).
	CraftMaterialsLocked = CraftMaterialCount;
	OldActiveStore = TalkID::DungeonVendorCraftMaterial;
	TempItem = MyPlayer->InvList[CraftTargetInv]; // the confirm shows the target scroll
	StartStore(TalkID::Confirm);
}

void StoreConfirm(Item &item)
{
	StartStore(OldActiveStore);
	HasScrollbar = false;
	ClearSText(5, 23);

	const UiFlags itemColor = item.getTextColorWithStatCheck();
	AddSText(20, 8, item.getName(), itemColor, false);
	AddSTextVal(8, item._iIvalue);
	PrintStoreItem(item, 9, itemColor);

	std::string_view prompt;

	switch (OldActiveStore) {
	case TalkID::BoyBuy:
		prompt = _("Do we have a deal?");
		break;
	case TalkID::StorytellerIdentify:
		prompt = _("Are you sure you want to identify this item?");
		break;
	case TalkID::HealerBuy:
	case TalkID::SmithPremiumBuy:
	case TalkID::WitchBuy:
	case TalkID::SmithBuy:
	case TalkID::DungeonVendorBuy:
		prompt = _("Are you sure you want to buy this item?");
		break;
	case TalkID::WitchRecharge:
		prompt = _("Are you sure you want to recharge this item?");
		break;
	case TalkID::SmithSell:
	case TalkID::WitchSell:
		prompt = _("Are you sure you want to sell this item?");
		break;
	case TalkID::SmithRepair:
		prompt = _("Are you sure you want to repair this item?");
		break;
	case TalkID::SmithGemRemove:
		prompt = _("Destroy the gem to free the socket?");
		break;
	case TalkID::SmithGemSalvage:
		prompt = _("Destroy this item to keep its gem?");
		break;
	case TalkID::DungeonVendorCraftMaterial:
		switch (CraftOp) {
		case ScrollCraftOp::Upgrade:
			prompt = _("Upgrade this scroll's level? Three scrolls are consumed.");
			break;
		case ScrollCraftOp::AddMod:
			prompt = _("Add a modifier to this scroll? A scroll is consumed.");
			break;
		case ScrollCraftOp::Reroll:
		default:
			prompt = _("Reroll this scroll's modifiers? A scroll is consumed.");
			break;
		}
		break;
	default:
		app_fatal(StrCat("Unknown store dialog ", static_cast<int>(OldActiveStore)));
	}
	AddSText(0, 15, prompt, UiFlags::ColorWhite | UiFlags::AlignCenter, false);
	AddSText(0, 18, _("Yes"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 20, _("No"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

void StartBoy()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Wirt the Peg-legged boy"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(5);
	if (!BoyItem.isEmpty()) {
		AddSText(0, 8, _("Talk to Wirt"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
		AddSText(0, 12, _("I have something for sale,"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSText(0, 14, _("but it will cost 50 gold"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSText(0, 16, _("just to take a look. "), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSText(0, 18, _("What have you got?"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 20, _("Say goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	} else {
		AddSText(0, 12, _("Talk to Wirt"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
		AddSText(0, 18, _("Say goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	}
}

void SStartBoyBuy()
{
	IsTextFullSize = true;
	HasScrollbar = false;

	RenderGold = true;
	AddSText(20, 1, _("I have this item for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);

	BoyItem._iStatFlag = MyPlayer->CanUseItem(BoyItem);
	const UiFlags itemColor = BoyItem.getTextColorWithStatCheck();
	AddSText(20, 10, BoyItem.getName(), itemColor, true, BoyItem._iCurs, true);
	if (gbIsHellfire)
		AddSTextVal(10, BoyItem._iIvalue - (BoyItem._iIvalue / 4));
	else
		AddSTextVal(10, BoyItem._iIvalue + (BoyItem._iIvalue / 2));
	PrintStoreItem(BoyItem, 11, itemColor, true);

	{
		// Add a Leave button. Unlike the other item list back buttons,
		// this one has different text and different layout in LargerSmallFont locales.
		const int line = BackButtonLine();
		AddSLine(line - 1);
		AddSText(0, line, _("Leave"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		TextLine[line]._syoff = 6;
	}
}

void HealPlayer()
{
	Player &myPlayer = *MyPlayer;

	if (myPlayer._pHitPoints != myPlayer._pMaxHP) {
		PlaySFX(SfxID::CastHealing);
	}
	myPlayer._pHitPoints = myPlayer._pMaxHP;
	myPlayer._pHPBase = myPlayer._pMaxHPBase;
	RedrawComponent(PanelDrawComponent::Health);
}

void StartHealer()
{
	HealPlayer();
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 1, _("Welcome to the"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 3, _("Healer's home"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Talk to Pepin"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddSText(0, 14, _("Buy items"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Leave Healer's home"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
	CurrentItemIndex = 20;
}

void ScrollHealerBuy(int idx)
{
	ScrollVendorStore(HealerItems, static_cast<int>(HealerItems.size()), idx);
}

void StartHealerBuy()
{
	IsTextFullSize = true;
	HasScrollbar = true;
	ScrollPos = 0;

	RenderGold = true;
	AddSText(20, 1, _("I have these items for sale:"), UiFlags::ColorWhitegold, false);
	AddSLine(3);

	ScrollHealerBuy(ScrollPos);
	AddItemListBackButton();

	CurrentItemIndex = 0;
	for (Item &item : HealerItems) {
		item._iStatFlag = MyPlayer->CanUseItem(item);
		CurrentItemIndex++;
	}

	NumTextLines = std::max(CurrentItemIndex - 4, 0);
}

void StartStoryteller()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("The Town Elder"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 8, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 10, _("Talk to Cain"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddSText(0, 12, _("Identify an item"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	// [RUNS MOD] Cain doubles as the in-game manual and the hero's chronicler: a
	// short primer on the run rules, and a report of earned progression.
	if (RunsModEnabled) {
		AddSText(0, 14, _("Ask about the rules"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 16, _("Ask about your deeds"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 18, _("Ask about the heroes"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	}
	AddSText(0, RunsModEnabled ? 20 : 18, _("Say goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
}

// [RUNS MOD] The class whose guide page Cain is showing.
HeroClass GuideClass = HeroClass::Warrior;

// [RUNS MOD] Cain's hero guides: pick a class, get a short build primer (what stats
// matter, how the class plays, and one run-specific tip). The Hellfire trio shows
// only when Hellfire is active -- the same gate as hero creation.
void StartStorytellerGuides()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	AddSText(0, 1, _("The Heroes"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(3);
	AddSText(0, 5, _("Whose tale would you hear?"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 7, _("The Warrior"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 9, _("The Rogue"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 11, _("The Sorcerer"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	if (gbIsHellfire) {
		AddSText(0, 13, _("The Monk"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 15, _("The Bard"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
		AddSText(0, 17, _("The Barbarian"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	}
	AddSText(0, BackButtonLine(), _("Back"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

// [RUNS MOD] One class's guide page. Which class is in GuideClass.
void StartStorytellerGuideShow()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	AddSLine(3);
	int l = 5;
	const auto say = [&l](const char *text) {
		AddSText(20, l++, _(text), UiFlags::ColorWhite, false);
	};
	const auto gap = [&l] { l++; };
	switch (GuideClass) {
	case HeroClass::Warrior:
		AddSText(0, 1, _("The Warrior"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Strength drives his blows and carries the heaviest plate;");
		say("Dexterity lands them; Vitality lets him trade hit for hit.");
		gap();
		say("A frontline brawler: simple, sturdy, gear-hungry. His craft");
		say("mends gear in the field, at a small cost to its durability.");
		gap();
		say("In a run: hold a doorway when the dungeon wakes, and spend");
		say("your gold at the smithy -- plate is his second health bar.");
		break;
	case HeroClass::Rogue:
		AddSText(0, 1, _("The Rogue"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Dexterity is everything: her damage, her aim, her armor.");
		say("A little Magic for utility; Vitality enough to survive a");
		say("mistake.");
		gap();
		say("She wins by never being touched, and her craft disarms the");
		say("traps that chests and doors love to hide.");
		gap();
		say("In a run: the waves die before they arrive if you keep");
		say("moving. A cornered rogue is a dead rogue -- kite wide.");
		break;
	case HeroClass::Sorcerer:
		AddSText(0, 1, _("The Sorcerer"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Magic rules all: his spell damage, his mana, the books he");
		say("can read. Vitality is precious; little else stands between");
		say("him and the teeth.");
		gap();
		say("Frail at dawn, a god by dusk. Books raise his spells, his");
		say("craft recharges staves, and Mana Shield turns mana to life.");
		gap();
		say("In a run: buy every book Adria sells, open with the staff,");
		say("and never melee anything you could have burned.");
		break;
	case HeroClass::Monk:
		AddSText(0, 1, _("The Monk"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Balance Dexterity and Strength. He shuns heavy armor --");
		say("cloth suits him best -- and his staff strikes several foes");
		say("in a single sweep.");
		gap();
		say("Fast and mobile, at his best in the open where he can");
		say("weave. His craft is Search, laying bare what the floor hides.");
		gap();
		say("In a run: he thrives against the waves -- wade in, sweep");
		say("the pack, and be gone before the dungeon closes its fist.");
		break;
	case HeroClass::Bard:
		AddSText(0, 1, _("The Bard"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Split her points between Strength and Dexterity, with");
		say("Magic enough to read what she finds. She fights with a");
		say("blade in each hand.");
		gap();
		say("The flexible one: a rogue's speed up close, a passable");
		say("caster in a pinch. Her craft identifies loot on the spot.");
		gap();
		say("In a run: two fast blades shred a boss; keep one spell");
		say("ready for the pack that gets too friendly.");
		break;
	case HeroClass::Barbarian:
		AddSText(0, 1, _("The Barbarian"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		say("Strength beyond reason and the Vitality to match; Magic");
		say("was never his language. He swings two-handed steel in a");
		say("single fist.");
		gap();
		say("Pure violence, with a Rage that trades safety for");
		say("slaughter when the crowd presses close.");
		gap();
		say("In a run: the waves only feed him. Mind the collapse");
		say("clock -- slaughter has made many a hero late.");
		break;
	default:
		AddSText(0, 1, _("The Heroes"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		break;
	}
	AddSText(0, BackButtonLine(), _("Done"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

// [RUNS MOD] Cain's progression report: everything the hero has EARNED, in one place
// -- the numbers that gate the vendor, the pack, and the difficulty ladder.
void StartStorytellerProgress()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	AddSText(0, 1, _("Your Deeds"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(3);
	const Player &myPlayer = *MyPlayer;
	int l = 5;

	if (myPlayer.pDeepestBeatenDepth > 0)
		AddSText(20, l++, fmt::format(fmt::runtime(_("Deepest boss slain: depth {:d} of {:d}.")), myPlayer.pDeepestBeatenDepth, GetMaxRunDepth()), UiFlags::ColorWhite, false);
	else
		AddSText(20, l++, _("No run boss has fallen to you yet."), UiFlags::ColorWhite, false);
	AddSText(20, l++, fmt::format(fmt::runtime(_("The Dungeon Merchant stocks scrolls to depth {:d}.")), ClampRunDepth(myPlayer.pDeepestBeatenDepth + 1)), UiFlags::ColorWhite, false);
	l++;

	const int extraRows = myPlayer.pExtraInvRows - InitialExtraInventoryRows;
	const int maxExtraRows = MaxExtraInventoryRows - InitialExtraInventoryRows;
	if (myPlayer.pExtraInvRows >= MaxExtraInventoryRows) {
		AddSText(20, l++, fmt::format(fmt::runtime(_("Pack rows earned: {:d} of {:d} -- fully grown.")), extraRows, maxExtraRows), UiFlags::ColorWhite, false);
	} else {
		const int nextRowDepth = (myPlayer.pExtraInvRows - InitialExtraInventoryRows + 1) * 2;
		AddSText(20, l++, fmt::format(fmt::runtime(_("Pack rows earned: {:d} of {:d}; the next needs a boss at depth {:d}.")), extraRows, maxExtraRows, nextRowDepth), UiFlags::ColorWhite, false);
	}
	l++;

	switch (std::min<int>(myPlayer.pDiabloKillLevel, 3)) {
	case 0:
		AddSText(20, l++, fmt::format(fmt::runtime(_("Only Normal lies open. Fell the boss at depth {:d} to unlock Nightmare.")), DifficultyUnlockDepth), UiFlags::ColorWhite, false);
		break;
	case 1:
		AddSText(20, l++, fmt::format(fmt::runtime(_("Nightmare is unlocked. Fell its depth-{:d} boss to open Hell.")), DifficultyUnlockDepth), UiFlags::ColorWhite, false);
		break;
	case 2:
		AddSText(20, l++, _("Hell is unlocked. Nothing deeper remains."), UiFlags::ColorWhite, false);
		break;
	default:
		AddSText(20, l++, _("Hell itself has been conquered."), UiFlags::ColorWhitegold, false);
		break;
	}
	l++;

	int totalKills = 0;
	for (int i = 0; i < NUM_MAX_MTYPES; i++)
		totalKills += MonsterKillCounts[i];
	AddSText(20, l++, fmt::format(fmt::runtime(_("Monsters slain: {:s}.")), FormatInteger(totalKills)), UiFlags::ColorWhite, false);

	int preservedCount = 0;
	for (const Item &item : myPlayer.InvBody) {
		if (!item.isEmpty() && item.isPreserved())
			preservedCount++;
	}
	for (int i = 0; i < myPlayer._pNumInv; i++) {
		if (myPlayer.InvList[i].isPreserved())
			preservedCount++;
	}
	for (const Item &item : myPlayer.SpdList) {
		if (!item.isEmpty() && item.isPreserved())
			preservedCount++;
	}
	if (preservedCount > 0)
		AddSText(20, l++, fmt::format(fmt::runtime(_("Items preserved against death: {:d}.")), preservedCount), UiFlags::ColorWhite, false);

	AddSText(0, 18, _("Done"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

// [RUNS MOD] Which page of Cain's rules primer is showing.
int RulesPage = 0;
constexpr int NumRulesPages = 2;

// [RUNS MOD] Cain's rules primer: the whole loop in two static pages. Kept to short
// hand-broken lines -- the store panel does not word-wrap. Body lines start at 5 and
// must end by 18 so the nav line at BackButtonLine() (20 with tall fonts) stays clear.
void StartStorytellerRules()
{
	IsTextFullSize = true;
	HasScrollbar = false;
	int l = 5;
	if (RulesPage == 0) {
		AddSText(0, 1, _("The Rules of the Run (1 of 2)"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSLine(3);
		AddSText(20, l++, _("A dungeon scroll opens the red portal to a run of its depth."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("Deeper runs hold deadlier foes -- and richer spoils."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("A portal's clock starts as it opens: five quiet minutes,"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("then ever-larger packs hunt you, minute by minute. The"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("dungeon festers on even while you linger back in town."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("Every run has a boss. Slay it and its vault spills open:"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("treasure, gold, and always a scroll one depth deeper."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("Its death starts the collapse: three minutes to loot and go."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("Then the portal seals, and what still lies below is lost."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("Too fearsome? Ghost the run: take what you find, slip out,"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("and keep it all. But depth must then be bought, not won."), UiFlags::ColorWhite, false);
		AddSText(0, BackButtonLine(), _("Next"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	} else {
		AddSText(0, 1, _("The Rules of the Run (2 of 2)"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
		AddSLine(3);
		AddSText(20, l++, _("Death is costly: everything you carried falls where you die"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("-- gear, pack, and half your gold. Your corpse waits in the"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("run while its portal stands. A Scroll of Preservation"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("shields one carried item from death."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("The Dungeon Merchant sells scrolls for conquered depths;"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("beating a boss restocks his shelf, one depth deeper."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("For gold he will also reroll, upgrade or empower a scroll."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("Boss kills at depths 2, 4, 6 and 8 grow your pack a row."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("Fell the boss at depth 15 to unlock the next difficulty."), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("The chest in the square is your stash; its goods are safe."), UiFlags::ColorWhite, false);
		l++;
		AddSText(20, l++, _("A run lives only while you play: leave the game and its"), UiFlags::ColorWhite, false);
		AddSText(20, l++, _("portal seals. A boss left wounded greets you healed."), UiFlags::ColorWhite, false);
		AddSText(0, BackButtonLine(), _("Done"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	}
}

bool IdItemOk(Item *i)
{
	if (i->isEmpty()) {
		return false;
	}
	if (i->_iMagical == ITEM_QUALITY_NORMAL) {
		return false;
	}
	return !i->_iIdentified;
}

void AddStoreHoldId(Item itm, int8_t i)
{
	PlayerItems[CurrentItemIndex] = itm;
	PlayerItems[CurrentItemIndex]._ivalue = 100;
	PlayerItems[CurrentItemIndex]._iIvalue = 100;
	PlayerItemIndexes[CurrentItemIndex] = i;
	CurrentItemIndex++;
}

void StartStorytellerIdentify()
{
	bool idok = false;
	IsTextFullSize = true;
	CurrentItemIndex = 0;

	for (auto &item : PlayerItems) {
		item.clear();
	}

	Player &myPlayer = *MyPlayer;

	auto &helmet = myPlayer.InvBody[INVLOC_HEAD];
	if (IdItemOk(&helmet)) {
		idok = true;
		AddStoreHoldId(helmet, -1);
	}

	auto &armor = myPlayer.InvBody[INVLOC_CHEST];
	if (IdItemOk(&armor)) {
		idok = true;
		AddStoreHoldId(armor, -2);
	}

	auto &leftHand = myPlayer.InvBody[INVLOC_HAND_LEFT];
	if (IdItemOk(&leftHand)) {
		idok = true;
		AddStoreHoldId(leftHand, -3);
	}

	auto &rightHand = myPlayer.InvBody[INVLOC_HAND_RIGHT];
	if (IdItemOk(&rightHand)) {
		idok = true;
		AddStoreHoldId(rightHand, -4);
	}

	auto &leftRing = myPlayer.InvBody[INVLOC_RING_LEFT];
	if (IdItemOk(&leftRing)) {
		idok = true;
		AddStoreHoldId(leftRing, -5);
	}

	auto &rightRing = myPlayer.InvBody[INVLOC_RING_RIGHT];
	if (IdItemOk(&rightRing)) {
		idok = true;
		AddStoreHoldId(rightRing, -6);
	}

	auto &amulet = myPlayer.InvBody[INVLOC_AMULET];
	if (IdItemOk(&amulet)) {
		idok = true;
		AddStoreHoldId(amulet, -7);
	}

	for (int i = 0; i < myPlayer._pNumInv; i++) {
		if (CurrentItemIndex >= 48)
			break;
		auto &item = myPlayer.InvList[i];
		if (IdItemOk(&item)) {
			idok = true;
			AddStoreHoldId(item, i);
		}
	}

	if (!idok) {
		HasScrollbar = false;

		RenderGold = true;
		AddSText(20, 1, _("You have nothing to identify."), UiFlags::ColorWhitegold, false);
		AddSLine(3);
		AddItemListBackButton(/*selectable=*/true);
		return;
	}

	HasScrollbar = true;
	ScrollPos = 0;
	NumTextLines = myPlayer._pNumInv;

	RenderGold = true;
	AddSText(20, 1, _("Identify which item?"), UiFlags::ColorWhitegold, false);
	AddSLine(3);

	ScrollSmithSell(ScrollPos);
	AddItemListBackButton();
}

void StartStorytellerIdentifyShow(Item &item)
{
	StartStore(OldActiveStore);
	HasScrollbar = false;
	ClearSText(5, 23);

	const UiFlags itemColor = item.getTextColorWithStatCheck();

	AddSText(0, 7, _("This item is:"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
	AddSText(20, 11, item.getName(), itemColor, false);
	PrintStoreItem(item, 12, itemColor);
	AddSText(0, 18, _("Done"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
}

void StartTalk()
{
	int la;

	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, fmt::format(fmt::runtime(_("Talk to {:s}")), _(TownerNames[TownerId])), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSLine(5);
	if (gbIsSpawn) {
		AddSText(0, 10, fmt::format(fmt::runtime(_("Talking to {:s}")), _(TownerNames[TownerId])), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
		AddSText(0, 12, _("is not available"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
		AddSText(0, 14, _("in the shareware"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
		AddSText(0, 16, _("version"), UiFlags::ColorWhite | UiFlags::AlignCenter, false);
		AddOptionsBackButton();
		return;
	}

	int sn = 0;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && GetTownerQuestDialog(TownerId, quest._qidx) != TEXT_NONE && quest._qlog)
			sn++;
	}

	if (sn > 6) {
		sn = 14 - (sn / 2);
		la = 1;
	} else {
		sn = 15 - sn;
		la = 2;
	}

	const int sn2 = sn - 2;

	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && GetTownerQuestDialog(TownerId, quest._qidx) != TEXT_NONE && quest._qlog) {
			AddSText(0, sn, _(QuestsData[quest._qidx]._qlstr), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
			sn += la;
		}
	}
	AddSText(0, sn2, _("Gossip"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddOptionsBackButton();
}

void StartTavern()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 1, _("Welcome to the"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 3, _("Rising Sun"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Talk to Ogden"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Leave the tavern"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
	CurrentItemIndex = 20;
}

void StartBarmaid()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Gillian"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Talk to Gillian"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddSText(0, 14, _("Access Storage"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Say goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
	CurrentItemIndex = 20;
}

void StartDrunk()
{
	IsTextFullSize = false;
	HasScrollbar = false;
	AddSText(0, 2, _("Farnham the Drunk"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 9, _("Would you like to:"), UiFlags::ColorWhitegold | UiFlags::AlignCenter, false);
	AddSText(0, 12, _("Talk to Farnham"), UiFlags::ColorBlue | UiFlags::AlignCenter, true);
	AddSText(0, 18, _("Say Goodbye"), UiFlags::ColorWhite | UiFlags::AlignCenter, true);
	AddSLine(5);
	CurrentItemIndex = 20;
}

void SmithEnter()
{
	if (*GetOptions().Gameplay.visualStoreUI) {
		switch (CurrentTextLine) {
		case 10:
			TownerId = TOWN_SMITH;
			OldTextLine = 10;
			OldActiveStore = TalkID::Smith;
			StartStore(TalkID::Gossip);
			break;
		case 12:
			ActiveStore = TalkID::None;
			OpenVisualStore(VisualStoreVendor::Smith);
			break;
		case 14:
			if (RunsModEnabled)
				StartStore(TalkID::SmithGems);
			else
				ActiveStore = TalkID::None;
			break;
		case 16:
			ActiveStore = TalkID::None;
			break;
		}
		return;
	}

	switch (CurrentTextLine) {
	case 10:
		TownerId = TOWN_SMITH;
		OldTextLine = 10;
		OldActiveStore = TalkID::Smith;
		StartStore(TalkID::Gossip);
		break;
	case 12:
		StartStore(TalkID::SmithBuy);
		break;
	case 14:
		StartStore(TalkID::SmithPremiumBuy);
		break;
	case 16:
		StartStore(TalkID::SmithSell);
		break;
	case 18:
		StartStore(TalkID::SmithRepair);
		break;
	case 20:
		if (RunsModEnabled)
			StartStore(TalkID::SmithGems);
		else
			ActiveStore = TalkID::None;
		break;
	case 22:
		ActiveStore = TalkID::None;
		break;
	}
}

/**
 * @brief Purchases an item from the smith.
 */
void SmithBuyItem(Item &item)
{
	TakePlrsMoney(item._iIvalue);
	if (item._iMagical == ITEM_QUALITY_NORMAL)
		item._iIdentified = false;
	StoreAutoPlace(item, true);
	int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	SmithItems.erase(SmithItems.begin() + idx);
	CalcPlrInv(*MyPlayer, true);
}

void SmithBuyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Smith);
		CurrentTextLine = 12;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;
	OldActiveStore = TalkID::SmithBuy;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);
	if (!PlayerCanAfford(SmithItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(SmithItems[idx], false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = SmithItems[idx];
	StartStore(TalkID::Confirm);
}

/**
 * @brief Purchases a premium item from the smith.
 */
void SmithBuyPItem(Item &item)
{
	TakePlrsMoney(item._iIvalue);
	if (item._iMagical == ITEM_QUALITY_NORMAL)
		item._iIdentified = false;
	StoreAutoPlace(item, true);

	int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	ReplacePremium(*MyPlayer, idx);
}

void SmithPremiumBuyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Smith);
		CurrentTextLine = 14;
		return;
	}

	OldActiveStore = TalkID::SmithPremiumBuy;
	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;

	int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(PremiumItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(PremiumItems[idx], false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = PremiumItems[idx];
	StartStore(TalkID::Confirm);
}

bool StoreGoldFit(Item &item)
{
	const int cost = item._iIvalue;

	const Size itemSize = GetInventorySize(item);
	const int itemRoomForGold = itemSize.width * itemSize.height * MaxGold;

	if (cost <= itemRoomForGold) {
		return true;
	}

	return cost <= itemRoomForGold + RoomForGold();
}

/**
 * @brief Sells an item from the player's inventory or belt.
 */
void StoreSellItem()
{
	Player &myPlayer = *MyPlayer;

	int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	const int srcIdx = PlayerItemIndexes[idx];
	if (srcIdx >= 0)
		myPlayer.RemoveInvItem(srcIdx);
	else
		myPlayer.RemoveSpdBarItem(-(srcIdx + 1));

	const int cost = PlayerItems[idx]._iIvalue;
	CurrentItemIndex--;
	if (idx != CurrentItemIndex) {
		while (idx < CurrentItemIndex) {
			PlayerItems[idx] = PlayerItems[idx + 1];
			PlayerItemIndexes[idx] = PlayerItemIndexes[idx + 1];
			idx++;
		}
	}

	AddGoldToInventory(myPlayer, cost);

	myPlayer._pGold += cost;
}

void SmithSellEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Smith);
		CurrentTextLine = 16;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldActiveStore = TalkID::SmithSell;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!StoreGoldFit(PlayerItems[idx])) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

/**
 * @brief Repairs an item in the player's inventory or body in the smith.
 */
void SmithRepairItem(int price)
{
	const int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	PlayerItems[idx]._iDurability = PlayerItems[idx]._iMaxDur;

	const int8_t i = PlayerItemIndexes[idx];

	Player &myPlayer = *MyPlayer;

	if (i < 0) {
		if (i == -1)
			myPlayer.InvBody[INVLOC_HEAD]._iDurability = myPlayer.InvBody[INVLOC_HEAD]._iMaxDur;
		if (i == -2)
			myPlayer.InvBody[INVLOC_CHEST]._iDurability = myPlayer.InvBody[INVLOC_CHEST]._iMaxDur;
		if (i == -3)
			myPlayer.InvBody[INVLOC_HAND_LEFT]._iDurability = myPlayer.InvBody[INVLOC_HAND_LEFT]._iMaxDur;
		if (i == -4)
			myPlayer.InvBody[INVLOC_HAND_RIGHT]._iDurability = myPlayer.InvBody[INVLOC_HAND_RIGHT]._iMaxDur;
		TakePlrsMoney(price);
		return;
	}

	myPlayer.InvList[i]._iDurability = myPlayer.InvList[i]._iMaxDur;
	TakePlrsMoney(price);
}

void SmithGemsEnter()
{
	switch (CurrentTextLine) {
	case SmithGemRemoveLine:
		StartStore(TalkID::SmithGemRemove);
		break;
	case SmithGemSalvageLine:
		StartStore(TalkID::SmithGemSalvage);
		break;
	case 18:
		StartStore(TalkID::Smith);
		CurrentTextLine = SmithGemsLine();
		break;
	}
}

void SmithGemWorkEnter(TalkID work)
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::SmithGems);
		CurrentTextLine = work == TalkID::SmithGemRemove ? SmithGemRemoveLine : SmithGemSalvageLine;
		return;
	}

	OldActiveStore = work;
	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (work == TalkID::SmithGemRemove && !PlayerCanAfford(PlayerItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}
	// Breaking up a worn item frees no pack space, so the gem needs somewhere to go.
	if (work == TalkID::SmithGemSalvage && PlayerItemIndexes[idx] < 0) {
		Item gem;
		ExtractFusedGem(PlayerItems[idx], gem);
		if (!StoreAutoPlace(gem, false)) {
			StartStore(TalkID::NoRoom);
			return;
		}
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

/**
 * @brief [RUNS MOD] Destroys the gem in the chosen item, leaving its socket empty.
 */
void SmithRemoveGem(int price)
{
	const int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	const int8_t i = PlayerItemIndexes[idx];
	Player &myPlayer = *MyPlayer;

	RemoveFusedGem(myPlayer, GemWorkTarget(myPlayer, i));
	if (i < 0)
		NetSendCmdChItem(true, GemWorkBodySlot(i));
	else
		NetSyncInvItem(myPlayer, i);

	TakePlrsMoney(price);
	CalcPlrInv(myPlayer, true);
}

/**
 * @brief [RUNS MOD] Breaks up the chosen item and hands its gem back.
 */
void SmithSalvageGem()
{
	const int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	const int8_t i = PlayerItemIndexes[idx];
	Player &myPlayer = *MyPlayer;

	Item gem;
	ExtractFusedGem(GemWorkTarget(myPlayer, i), gem);
	if (i < 0)
		RemoveEquipment(myPlayer, GemWorkBodySlot(i), true);
	else
		myPlayer.RemoveInvItem(i);
	StoreAutoPlace(gem, true);
	CalcPlrInv(myPlayer, true);
}

void SmithRepairEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Smith);
		CurrentTextLine = 18;
		return;
	}

	OldActiveStore = TalkID::SmithRepair;
	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(PlayerItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

void WitchEnter()
{
	if (*GetOptions().Gameplay.visualStoreUI) {
		switch (CurrentTextLine) {
		case 12:
			OldTextLine = 12;
			TownerId = TOWN_WITCH;
			OldActiveStore = TalkID::Witch;
			StartStore(TalkID::Gossip);
			break;
		case 14:
			ActiveStore = TalkID::None;
			OpenVisualStore(VisualStoreVendor::Witch);
			break;
		case 16:
			StartStore(TalkID::WitchRecharge);
			break;
		case 18:
			ActiveStore = TalkID::None;
			break;
		}
		return;
	}

	switch (CurrentTextLine) {
	case 12:
		OldTextLine = 12;
		TownerId = TOWN_WITCH;
		OldActiveStore = TalkID::Witch;
		StartStore(TalkID::Gossip);
		break;
	case 14:
		StartStore(TalkID::WitchBuy);
		break;
	case 16:
		StartStore(TalkID::WitchSell);
		break;
	case 18:
		StartStore(TalkID::WitchRecharge);
		break;
	case 20:
		ActiveStore = TalkID::None;
		break;
	}
}

/**
 * @brief Purchases an item from the witch.
 */
void WitchBuyItem(Item &item)
{
	int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);

	if (idx < 3)
		item._iSeed = AdvanceRndSeed();

	TakePlrsMoney(item._iIvalue);
	StoreAutoPlace(item, true);

	if (idx >= 3) {
		WitchItems.erase(WitchItems.begin() + idx);
	}

	CalcPlrInv(*MyPlayer, true);
}

void WitchBuyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Witch);
		CurrentTextLine = 14;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;
	OldActiveStore = TalkID::WitchBuy;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(WitchItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(WitchItems[idx], false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = WitchItems[idx];
	StartStore(TalkID::Confirm);
}

void WitchSellEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Witch);
		CurrentTextLine = 16;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldActiveStore = TalkID::WitchSell;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!StoreGoldFit(PlayerItems[idx])) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

/**
 * @brief Recharges an item in the player's inventory or body in the witch.
 */
void WitchRechargeItem(int price)
{
	const int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	PlayerItems[idx]._iCharges = PlayerItems[idx]._iMaxCharges;

	Player &myPlayer = *MyPlayer;

	const int8_t i = PlayerItemIndexes[idx];
	if (i < 0) {
		myPlayer.InvBody[INVLOC_HAND_LEFT]._iCharges = myPlayer.InvBody[INVLOC_HAND_LEFT]._iMaxCharges;
		NetSendCmdChItem(true, INVLOC_HAND_LEFT);
	} else {
		myPlayer.InvList[i]._iCharges = myPlayer.InvList[i]._iMaxCharges;
		NetSyncInvItem(myPlayer, i);
	}

	TakePlrsMoney(price);
	CalcPlrInv(myPlayer, true);
}

void WitchRechargeEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Witch);
		CurrentTextLine = *GetOptions().Gameplay.visualStoreUI ? 16 : 18;
		return;
	}

	OldActiveStore = TalkID::WitchRecharge;
	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(PlayerItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

void BoyEnter()
{
	if (!BoyItem.isEmpty() && CurrentTextLine == 18) {
		if (!PlayerCanAfford(50)) {
			OldActiveStore = TalkID::Boy;
			OldTextLine = 18;
			OldScrollPos = ScrollPos;
			StartStore(TalkID::NoMoney);
		} else {
			TakePlrsMoney(50);
			if (*GetOptions().Gameplay.visualStoreUI) {
				ActiveStore = TalkID::None;
				OpenVisualStore(VisualStoreVendor::Boy);
			} else {
				StartStore(TalkID::BoyBuy);
			}
		}
		return;
	}

	if ((CurrentTextLine != 8 && !BoyItem.isEmpty()) || (CurrentTextLine != 12 && BoyItem.isEmpty())) {
		ActiveStore = TalkID::None;
		return;
	}

	TownerId = TOWN_PEGBOY;
	OldActiveStore = TalkID::Boy;
	OldTextLine = CurrentTextLine;
	StartStore(TalkID::Gossip);
}

void BoyBuyItem(Item &item, int itemPrice)
{
	TakePlrsMoney(itemPrice);
	StoreAutoPlace(item, true);
	item.clear();
	OldActiveStore = TalkID::Boy;
	CalcPlrInv(*MyPlayer, true);
	OldTextLine = 12;
}

/**
 * @brief Purchases an item from the healer.
 */
void HealerBuyItem(Item &item)
{
	int idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	if (!gbIsMultiplayer) {
		if (idx < 2)
			item._iSeed = AdvanceRndSeed();
	} else {
		if (idx < 3)
			item._iSeed = AdvanceRndSeed();
	}

	TakePlrsMoney(item._iIvalue);
	if (item._iMagical == ITEM_QUALITY_NORMAL)
		item._iIdentified = false;
	StoreAutoPlace(item, true);

	if (!gbIsMultiplayer) {
		if (idx < 2)
			return;
	} else {
		if (idx < 3)
			return;
	}
	idx = OldScrollPos + ((OldTextLine - PreviousScrollPos) / 4);
	HealerItems.erase(HealerItems.begin() + idx);
	CalcPlrInv(*MyPlayer, true);
}

void BoyBuyEnter()
{
	if (CurrentTextLine != 10) {
		ActiveStore = TalkID::None;
		return;
	}

	OldActiveStore = TalkID::BoyBuy;
	OldScrollPos = ScrollPos;
	OldTextLine = 10;
	int price = BoyItem._iIvalue;
	if (gbIsHellfire)
		price -= BoyItem._iIvalue / 4;
	else
		price += BoyItem._iIvalue / 2;

	if (!PlayerCanAfford(price)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(BoyItem, false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = BoyItem;
	TempItem._iIvalue = price;
	StartStore(TalkID::Confirm);
}

void StorytellerIdentifyItem(Item &item)
{
	Player &myPlayer = *MyPlayer;

	const int8_t idx = PlayerItemIndexes[((OldTextLine - PreviousScrollPos) / 4) + OldScrollPos];
	if (idx < 0) {
		if (idx == -1)
			myPlayer.InvBody[INVLOC_HEAD]._iIdentified = true;
		if (idx == -2)
			myPlayer.InvBody[INVLOC_CHEST]._iIdentified = true;
		if (idx == -3)
			myPlayer.InvBody[INVLOC_HAND_LEFT]._iIdentified = true;
		if (idx == -4)
			myPlayer.InvBody[INVLOC_HAND_RIGHT]._iIdentified = true;
		if (idx == -5)
			myPlayer.InvBody[INVLOC_RING_LEFT]._iIdentified = true;
		if (idx == -6)
			myPlayer.InvBody[INVLOC_RING_RIGHT]._iIdentified = true;
		if (idx == -7)
			myPlayer.InvBody[INVLOC_AMULET]._iIdentified = true;
	} else {
		myPlayer.InvList[idx]._iIdentified = true;
	}
	item._iIdentified = true;
	TakePlrsMoney(item._iIvalue);
	CalcPlrInv(myPlayer, true);
}

void ConfirmEnter(Item &item)
{
	if (CurrentTextLine == 18) {
		switch (OldActiveStore) {
		case TalkID::SmithBuy:
			SmithBuyItem(item);
			break;
		case TalkID::SmithSell:
		case TalkID::WitchSell:
			StoreSellItem();
			break;
		case TalkID::SmithRepair:
			SmithRepairItem(item._iIvalue);
			break;
		case TalkID::SmithGemRemove:
			SmithRemoveGem(item._iIvalue);
			break;
		case TalkID::SmithGemSalvage:
			SmithSalvageGem();
			break;
		case TalkID::WitchBuy:
			WitchBuyItem(item);
			break;
		case TalkID::DungeonVendorBuy:
			DungeonVendorBuyItem(item);
			break;
		case TalkID::DungeonVendorCraftMaterial:
			CraftApply();
			StartStore(TalkID::DungeonVendor);
			return;
		case TalkID::WitchRecharge:
			WitchRechargeItem(item._iIvalue);
			break;
		case TalkID::BoyBuy:
			BoyBuyItem(BoyItem, item._iIvalue);
			break;
		case TalkID::HealerBuy:
			HealerBuyItem(item);
			break;
		case TalkID::StorytellerIdentify:
			StorytellerIdentifyItem(item);
			StartStore(TalkID::StorytellerIdentifyShow);
			return;
		case TalkID::SmithPremiumBuy:
			SmithBuyPItem(item);
			break;
		default:
			break;
		}
	}

	StartStore(OldActiveStore);

	if (CurrentTextLine == BackButtonLine())
		return;

	CurrentTextLine = OldTextLine;
	ScrollPos = std::min(OldScrollPos, NumTextLines);

	while (CurrentTextLine != -1 && !TextLine[CurrentTextLine].isSelectable()) {
		CurrentTextLine--;
	}
}

void HealerEnter()
{
	switch (CurrentTextLine) {
	case 12:
		OldTextLine = 12;
		TownerId = TOWN_HEALER;
		OldActiveStore = TalkID::Healer;
		StartStore(TalkID::Gossip);
		break;
	case 14:
		if (*GetOptions().Gameplay.visualStoreUI) {
			ActiveStore = TalkID::None;
			OpenVisualStore(VisualStoreVendor::Healer);
		} else {
			StartStore(TalkID::HealerBuy);
		}
		break;
	case 18:
		ActiveStore = TalkID::None;
		break;
	}
}

void HealerBuyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Healer);
		CurrentTextLine = 14;
		return;
	}

	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;
	OldActiveStore = TalkID::HealerBuy;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(HealerItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	if (!StoreAutoPlace(HealerItems[idx], false)) {
		StartStore(TalkID::NoRoom);
		return;
	}

	TempItem = HealerItems[idx];
	StartStore(TalkID::Confirm);
}

void StorytellerEnter()
{
	switch (CurrentTextLine) {
	case 10:
		OldTextLine = 10;
		TownerId = TOWN_STORY;
		OldActiveStore = TalkID::Storyteller;
		StartStore(TalkID::Gossip);
		break;
	case 12:
		StartStore(TalkID::StorytellerIdentify);
		break;
	// [RUNS MOD] The rules primer, from page one.
	case 14:
		RulesPage = 0;
		StartStore(TalkID::StorytellerRules);
		break;
	// [RUNS MOD] The progression report.
	case 16:
		StartStore(TalkID::StorytellerProgress);
		break;
	// [RUNS MOD] The hero guides (in run mode; otherwise 18 is goodbye).
	case 18:
		if (RunsModEnabled) {
			StartStore(TalkID::StorytellerGuides);
			break;
		}
		ActiveStore = TalkID::None;
		break;
	case 20:
		ActiveStore = TalkID::None;
		break;
	}
}

// [RUNS MOD] Enter on the hero-guides list: pick a class or go back to Cain.
void StorytellerGuidesEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 18;
		return;
	}
	switch (CurrentTextLine) {
	case 7: GuideClass = HeroClass::Warrior; break;
	case 9: GuideClass = HeroClass::Rogue; break;
	case 11: GuideClass = HeroClass::Sorcerer; break;
	case 13: GuideClass = HeroClass::Monk; break;
	case 15: GuideClass = HeroClass::Bard; break;
	case 17: GuideClass = HeroClass::Barbarian; break;
	default: return;
	}
	StartStore(TalkID::StorytellerGuideShow);
}

// [RUNS MOD] Enter on the primer's nav line: page forward, then back to Cain's menu.
void StorytellerRulesEnter()
{
	if (RulesPage + 1 < NumRulesPages) {
		RulesPage++;
		StartStore(TalkID::StorytellerRules);
		return;
	}
	StartStore(TalkID::Storyteller);
	CurrentTextLine = 14;
}

void StorytellerIdentifyEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 14;
		return;
	}

	OldActiveStore = TalkID::StorytellerIdentify;
	OldTextLine = CurrentTextLine;
	OldScrollPos = ScrollPos;

	const int idx = ScrollPos + ((CurrentTextLine - PreviousScrollPos) / 4);

	if (!PlayerCanAfford(PlayerItems[idx]._iIvalue)) {
		StartStore(TalkID::NoMoney);
		return;
	}

	TempItem = PlayerItems[idx];
	StartStore(TalkID::Confirm);
}

void TalkEnter()
{
	if (CurrentTextLine == BackButtonLine()) {
		StartStore(OldActiveStore);
		CurrentTextLine = OldTextLine;
		return;
	}

	int sn = 0;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && GetTownerQuestDialog(TownerId, quest._qidx) != TEXT_NONE && quest._qlog)
			sn++;
	}
	int la = 2;
	if (sn > 6) {
		sn = 14 - (sn / 2);
		la = 1;
	} else {
		sn = 15 - sn;
	}

	if (CurrentTextLine == sn - 2) {
		Towner *target = GetTowner(TownerId);
		assert(target != nullptr);
		InitQTextMsg(target->gossip);
		return;
	}

	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && GetTownerQuestDialog(TownerId, quest._qidx) != TEXT_NONE && quest._qlog) {
			if (sn == CurrentTextLine) {
				InitQTextMsg(GetTownerQuestDialog(TownerId, quest._qidx));
			}
			sn += la;
		}
	}
}

void TavernEnter()
{
	switch (CurrentTextLine) {
	case 12:
		OldTextLine = 12;
		TownerId = TOWN_TAVERN;
		OldActiveStore = TalkID::Tavern;
		StartStore(TalkID::Gossip);
		break;
	case 18:
		ActiveStore = TalkID::None;
		break;
	}
}

void BarmaidEnter()
{
	switch (CurrentTextLine) {
	case 12:
		OldTextLine = 12;
		TownerId = TOWN_BMAID;
		OldActiveStore = TalkID::Barmaid;
		StartStore(TalkID::Gossip);
		break;
	case 14:
		ActiveStore = TalkID::None;
		IsStashOpen = true;
		Stash.RefreshItemStatFlags();
		invflag = true;
		if (ControlMode != ControlTypes::KeyboardAndMouse) {
			if (pcurs == CURSOR_DISARM)
				NewCursor(CURSOR_HAND);
			FocusOnInventory();
		}
		break;
	case 18:
		ActiveStore = TalkID::None;
		break;
	}
}

void DrunkEnter()
{
	switch (CurrentTextLine) {
	case 12:
		OldTextLine = 12;
		TownerId = TOWN_DRUNK;
		OldActiveStore = TalkID::Drunk;
		StartStore(TalkID::Gossip);
		break;
	case 18:
		ActiveStore = TalkID::None;
		break;
	}
}

int TakeGold(Player &player, int cost, bool skipMaxPiles)
{
	for (int i = 0; i < player._pNumInv; i++) {
		auto &item = player.InvList[i];
		if (item._itype != ItemType::Gold || (skipMaxPiles && item._ivalue == MaxGold))
			continue;

		if (cost < item._ivalue) {
			item._ivalue -= cost;
			SetPlrHandGoldCurs(player.InvList[i]);
			return 0;
		}

		cost -= item._ivalue;
		player.RemoveInvItem(i);
		i = -1;
	}

	return cost;
}

void DrawSelector(const Surface &out, const Rectangle &rect, std::string_view text, UiFlags flags)
{
	const int lineWidth = GetLineWidth(text);

	int x1 = rect.position.x - 20;
	if (HasAnyOf(flags, UiFlags::AlignCenter))
		x1 += (rect.size.width - lineWidth) / 2;

	ClxDraw(out, { x1, rect.position.y + 13 }, (*pSPentSpn2Cels)[PentSpn2Spin()]);

	int x2 = rect.position.x + rect.size.width + 5;
	if (HasAnyOf(flags, UiFlags::AlignCenter))
		x2 = rect.position.x + (rect.size.width - lineWidth) / 2 + lineWidth + 5;

	ClxDraw(out, { x2, rect.position.y + 13 }, (*pSPentSpn2Cels)[PentSpn2Spin()]);
}

} // namespace

bool StoreAutoPlace(Item &item, bool persistItem)
{
	Player &player = *MyPlayer;

	if (AutoEquipEnabled(player, item) && AutoEquip(player, item, persistItem, true)) {
		return true;
	}

	if (AutoPlaceItemInBelt(player, item, persistItem, true)) {
		return true;
	}

	if (persistItem) {
		return AutoPlaceItemInInventory(player, item, true);
	}

	return CanFitItemInInventory(player, item);
}

void AddStoreHoldRepair(Item *itm, int8_t i)
{
	Item *item;
	int v;

	item = &PlayerItems[CurrentItemIndex];
	PlayerItems[CurrentItemIndex] = *itm;

	const int due = item->_iMaxDur - item->_iDurability;
	if (item->_iMagical != ITEM_QUALITY_NORMAL && item->_iIdentified) {
		v = 30 * item->_iIvalue * due / (item->_iMaxDur * 100 * 2);
		if (v == 0)
			return;
	} else {
		v = item->_ivalue * due / (item->_iMaxDur * 2);
		v = std::max(v, 1);
	}
	item->_iIvalue = v;
	item->_ivalue = v;
	PlayerItemIndexes[CurrentItemIndex] = i;
	CurrentItemIndex++;
}

void InitStores()
{
	ClearSText(0, NumStoreLines);
	ActiveStore = TalkID::None;
	IsTextFullSize = false;
	HasScrollbar = false;
	PremiumItemCount = 0;
	PremiumItemLevel = 1;

	SmithItems.clear();
	WitchItems.clear();
	HealerItems.clear();
	PremiumItems.clear();

	BoyItem.clear();
	BoyItemLevel = 0;
}

void SetupTownStores()
{
	const Player &myPlayer = *MyPlayer;

	int l = myPlayer.getCharacterLevel() / 2;
	if (!gbIsMultiplayer) {
		l = 0;
		for (int i = 0; i < NUMLEVELS; i++) {
			if (myPlayer._pLvlVisited[i])
				l = i;
		}
	}

	// [RUNS MOD] Drive the shop level off EARNED DEPTH -- the deepest run the hero may
	// enter, the same gate the Dungeon Merchant's scroll shelf uses -- instead of scanning
	// _pLvlVisited. Two reasons: the flags are no longer a record of how deep you have
	// been (ApplyRunSeed clears the entry on every reseed, so re-rolling a portal from the
	// hub silently drops the shelf a tier), and depth belongs on the progression clock
	// rather than on wherever a scroll happened to take you. Also drives Adria's magic
	// tier, which is derived from this level.
	if (RunsModEnabled)
		l = ClampRunDepth(myPlayer.pDeepestBeatenDepth + 1);

	l = std::clamp(l + 2, 6, 16);
	SpawnSmith(l);
	SpawnWitch(l);
	// [RUNS MOD] The Dungeon Merchant's stock persists between hub visits (it only
	// shrinks as you buy, down to a floor of 1); it fully restocks after a dungeon is
	// completed, or on the first visit when it has no stock yet.
	if (DungeonVendorItems.empty() || DungeonVendorRestockPending) {
		SpawnDungeonVendor();
		// [RUNS MOD] The fruit-machine jackpot rerolls with the vendor: session state,
		// refreshed by a boss kill. The floor keeps the pool interesting for fresh
		// characters; the cap keeps it at the edge of earned progression.
		//
		// The cap is 2 x the deepest run the hero may enter -- the same vault scale as
		// the item prize and every other drop in the mod. It was the raw access depth,
		// which capped the rarest payout on the machine (triple crown, ~1 in 2,460 pulls)
		// at the tier a routine early vault already beats.
		SpawnGamblerJackpot(std::max<int>(4, 2 * ClampRunDepth(myPlayer.pDeepestBeatenDepth + 1)));
		DungeonVendorRestockPending = false;
	}
	SpawnHealer(l);
	// [RUNS MOD] Wirt's shop has no NPC in run mode (his sprite hosts the Dungeon
	// Merchant), so don't stock it; Farnham's fruit machine is the gamble sink instead.
	if (!RunsModEnabled)
		SpawnBoy(myPlayer.getCharacterLevel());
	SpawnPremium(myPlayer);
}

void FreeStoreMem()
{
	if (*GetOptions().Gameplay.showItemGraphicsInStores) {
		FreeHalfSizeItemSprites();
	}
	ActiveStore = TalkID::None;
	for (STextStruct &entry : TextLine) {
		entry.text.clear();
		entry.text.shrink_to_fit();
	}
}

void PrintSString(const Surface &out, int margin, int line, std::string_view text, UiFlags flags, int price, int cursId, bool cursIndent)
{
	const Point uiPosition = GetUIRectangle().position;
	int sx = uiPosition.x + 32 + margin;
	if (!IsTextFullSize) {
		sx += 320;
	}

	const int sy = uiPosition.y + PaddingTop + TextLine[line].y + TextLine[line]._syoff;

	int width = IsTextFullSize ? 575 : 255;
	if (HasScrollbar && line >= 4 && line <= 20) {
		width -= 9; // Space for the selector
	}
	width -= margin * 2;

	const Rectangle rect { { sx, sy }, { width, 0 } };

	// Space reserved for item graphic is based on the size of 2x3 cursor sprites
	constexpr int CursWidth = INV_SLOT_SIZE_PX * 2;
	constexpr int HalfCursWidth = CursWidth / 2;

	if (*GetOptions().Gameplay.showItemGraphicsInStores && cursId >= 0) {
		const Size size = GetInvItemSize(static_cast<int>(CURSOR_FIRSTITEM) + cursId);
		const bool useHalfSize = size.width > INV_SLOT_SIZE_PX || size.height > INV_SLOT_SIZE_PX;
		const bool useRed = HasAnyOf(flags, UiFlags::ColorRed);
		const ClxSprite sprite = useHalfSize
		    ? (useRed ? GetHalfSizeItemSpriteRed(cursId) : GetHalfSizeItemSprite(cursId))
		    : GetInvItemSprite(static_cast<int>(CURSOR_FIRSTITEM) + cursId);
		const Point position {
			rect.position.x + ((HalfCursWidth - sprite.width()) / 2),
			rect.position.y + ((TextHeight() * 3 + sprite.height()) / 2)
		};
		if (useHalfSize || !useRed) {
			ClxDraw(out, position, sprite);
		} else {
			ClxDrawTRN(out, position, sprite, GetInfravisionTRN());
		}
	}

	if (*GetOptions().Gameplay.showItemGraphicsInStores && cursIndent) {
		const Rectangle textRect { { rect.position.x + HalfCursWidth + 8, rect.position.y }, { rect.size.width - HalfCursWidth + 8, rect.size.height } };
		DrawString(out, text, textRect, { .flags = flags });
	} else {
		DrawString(out, text, rect, { .flags = flags });
	}

	if (price > 0)
		DrawString(out, FormatInteger(price), rect, { .flags = flags | UiFlags::AlignRight });

	if (CurrentTextLine == line) {
		DrawSelector(out, rect, text, flags);
	}
}

void DrawSLine(const Surface &out, int sy)
{
	const Point uiPosition = GetUIRectangle().position;
	int sx = 26;
	int width = 587;

	if (!IsTextFullSize) {
		sx += SidePanelSize.width;
		width -= SidePanelSize.width;
	}

	uint8_t *src = out.at(uiPosition.x + sx, uiPosition.y + 25);
	uint8_t *dst = out.at(uiPosition.x + sx, sy);

	for (int i = 0; i < 3; i++, src += out.pitch(), dst += out.pitch())
		memcpy(dst, src, width);
}

void DrawSTextHelp()
{
	CurrentTextLine = -1;
	IsTextFullSize = true;
}

void ClearSText(int s, int e)
{
	for (int i = s; i < e; i++) {
		TextLine[i]._sx = 0;
		TextLine[i]._syoff = 0;
		TextLine[i].text.clear();
		TextLine[i].text.shrink_to_fit();
		TextLine[i].flags = UiFlags::None;
		TextLine[i].type = STextStruct::Label;
		TextLine[i]._sval = 0;
	}
}

void StartStore(TalkID s)
{
	if (*GetOptions().Gameplay.showItemGraphicsInStores) {
		CreateHalfSizeItemSprites();
	}
	SpellbookFlag = false;
	CloseInventory();
	CloseCharPanel();
	RenderGold = false;
	QuestLogIsOpen = false;
	CloseGoldDrop();
	ClearSText(0, NumStoreLines);
	ReleaseStoreBtn();

	// Fire StoreOpened Lua event for main store entries
	if (const char *name = TownerNameForTalkID(s); name != nullptr)
		lua::StoreOpened(name);

	switch (s) {
	case TalkID::Smith:
		StartSmith();
		break;
	case TalkID::SmithBuy: {
		if (!SmithItems.empty())
			StartSmithBuy();
		else {
			ActiveStore = TalkID::SmithBuy;
			OldTextLine = 12;
			StoreESC();
			return;
		}
		break;
	}
	case TalkID::SmithSell:
		StartSmithSell();
		break;
	case TalkID::SmithRepair:
		StartSmithRepair();
		break;
	case TalkID::SmithGems:
		StartSmithGems();
		break;
	case TalkID::SmithGemRemove:
	case TalkID::SmithGemSalvage:
		StartSmithGemWork(s);
		break;
	case TalkID::Witch:
		StartWitch();
		break;
	case TalkID::WitchBuy:
		if (CurrentItemIndex > 0)
			StartWitchBuy();
		break;
	case TalkID::WitchSell:
		StartWitchSell();
		break;
	case TalkID::WitchRecharge:
		StartWitchRecharge();
		break;
	case TalkID::DungeonVendor:
		StartDungeonVendor();
		break;
	case TalkID::DungeonVendorBuy:
		if (CurrentItemIndex > 0)
			StartDungeonVendorBuy();
		break;
	case TalkID::DungeonVendorCraftTarget:
		StartDungeonVendorCraftTarget();
		break;
	case TalkID::DungeonVendorCraftMaterial:
		StartDungeonVendorCraftMaterial();
		break;
	case TalkID::Gamble: // [RUNS MOD]
		StartGamble();
		break;
	case TalkID::GambleSpin: // [RUNS MOD]
		StartGambleSpin();
		break;
	case TalkID::GambleJackpotShow: // [RUNS MOD]
		StartGambleJackpotShow();
		break;
	case TalkID::NoMoney:
		StoreNoMoney();
		break;
	case TalkID::NoRoom:
		StoreNoRoom();
		break;
	case TalkID::Confirm:
		StoreConfirm(TempItem);
		break;
	case TalkID::Boy:
		StartBoy();
		break;
	case TalkID::BoyBuy:
		SStartBoyBuy();
		break;
	case TalkID::Healer:
		StartHealer();
		break;
	case TalkID::Storyteller:
		StartStoryteller();
		break;
	case TalkID::HealerBuy:
		if (CurrentItemIndex > 0)
			StartHealerBuy();
		break;
	case TalkID::StorytellerIdentify:
		StartStorytellerIdentify();
		break;
	case TalkID::StorytellerRules: // [RUNS MOD]
		StartStorytellerRules();
		break;
	case TalkID::StorytellerProgress: // [RUNS MOD]
		StartStorytellerProgress();
		break;
	case TalkID::StorytellerGuides: // [RUNS MOD]
		StartStorytellerGuides();
		break;
	case TalkID::StorytellerGuideShow: // [RUNS MOD]
		StartStorytellerGuideShow();
		break;
	case TalkID::SmithPremiumBuy:
		if (!StartSmithPremiumBuy())
			return;
		break;
	case TalkID::Gossip:
		StartTalk();
		break;
	case TalkID::StorytellerIdentifyShow:
		StartStorytellerIdentifyShow(TempItem);
		break;
	case TalkID::Tavern:
		StartTavern();
		break;
	case TalkID::Drunk:
		StartDrunk();
		break;
	case TalkID::Barmaid:
		StartBarmaid();
		break;
	case TalkID::None:
		break;
	}

	std::fill(std::begin(CurrentExtraOptionIndices), std::end(CurrentExtraOptionIndices), std::nullopt);
	if (const char *extraTownerName = TownerNameForTalkID(s); extraTownerName != nullptr) {
		if (auto *extraOpts = FindExtraTownerOptions(extraTownerName); extraOpts != nullptr) {
			// Find the last selectable line (the "leave"/"say goodbye" option).
			int lastSelectableLine = -1;
			for (int i = NumStoreLines - 1; i >= 0; --i) {
				if (TextLine[i].isSelectable()) {
					lastSelectableLine = i;
					break;
				}
			}

			// Insert extra options into empty even-numbered lines before the leave option.
			size_t optIdx = 0;
			for (int line = 10; line < lastSelectableLine && optIdx < extraOpts->size(); line += 2) {
				if (TextLine[line].hasText()) continue;
				std::string label = (*extraOpts)[optIdx].getLabel();
				if (!label.empty()) {
					AddSText(0, line, label, UiFlags::ColorWhite | UiFlags::AlignCenter, true);
					CurrentExtraOptionIndices[line] = optIdx;
				}
				++optIdx;
			}
			if (optIdx < extraOpts->size()) {
				LogWarn("Towner \"{}\" dialog: {} extra option(s) could not be placed (no empty lines)",
				    extraTownerName, extraOpts->size() - optIdx);
			}
		}
	}

	CurrentTextLine = -1;
	for (int i = 0; i < NumStoreLines; i++) {
		if (TextLine[i].isSelectable()) {
			CurrentTextLine = i;
			break;
		}
	}

	ActiveStore = s;
}

void DrawSText(const Surface &out)
{
	if (!IsTextFullSize)
		DrawSTextBack(out);
	else
		DrawQTextBack(out);

	if (HasScrollbar) {
		switch (ActiveStore) {
		case TalkID::SmithBuy:
			ScrollSmithBuy(ScrollPos);
			break;
		case TalkID::SmithSell:
		case TalkID::SmithRepair:
		case TalkID::SmithGemRemove:
		case TalkID::SmithGemSalvage:
		case TalkID::WitchSell:
		case TalkID::WitchRecharge:
		case TalkID::StorytellerIdentify:
			ScrollSmithSell(ScrollPos);
			break;
		case TalkID::WitchBuy:
			ScrollWitchBuy(ScrollPos);
			break;
		case TalkID::HealerBuy:
			ScrollHealerBuy(ScrollPos);
			break;
		case TalkID::SmithPremiumBuy:
			ScrollSmithPremiumBuy(ScrollPos);
			break;
		default:
			break;
		}
	}

	CalculateLineHeights();
	const Point uiPosition = GetUIRectangle().position;
	for (int i = 0; i < NumStoreLines; i++) {
		if (TextLine[i].isDivider())
			DrawSLine(out, uiPosition.y + PaddingTop + TextLine[i].y + (TextHeight() / 2));
		else if (TextLine[i].hasText())
			PrintSString(out, TextLine[i]._sx, i, TextLine[i].text, TextLine[i].flags, TextLine[i]._sval, TextLine[i].cursId, TextLine[i].cursIndent);
	}

	// [RUNS MOD] The fruit machine's reels animate per-frame over the static panel.
	if (ActiveStore == TalkID::GambleSpin)
		DrawGambleReels(out);

	if (RenderGold) {
		PrintSString(out, 28, 1, fmt::format(fmt::runtime(_("Your gold: {:s}")), FormatInteger(TotalPlayerGold())).c_str(), UiFlags::ColorWhitegold | UiFlags::AlignRight);
	}

	if (HasScrollbar)
		DrawSSlider(out, 4, 20);
}

void StoreESC()
{
	if (qtextflag) {
		qtextflag = false;
		if (leveltype == DTYPE_TOWN)
			stream_stop();
		return;
	}

	switch (ActiveStore) {
	case TalkID::Smith:
	case TalkID::Witch:
	case TalkID::Boy:
	case TalkID::BoyBuy:
	case TalkID::Healer:
	case TalkID::Storyteller:
	case TalkID::Tavern:
	case TalkID::Drunk:
	case TalkID::Barmaid:
	case TalkID::DungeonVendor:
	case TalkID::Gamble: // [RUNS MOD]
		ActiveStore = TalkID::None;
		break;
	case TalkID::Gossip:
		StartStore(OldActiveStore);
		CurrentTextLine = OldTextLine;
		break;
	case TalkID::SmithBuy:
		StartStore(TalkID::Smith);
		CurrentTextLine = 12;
		break;
	case TalkID::SmithPremiumBuy:
		StartStore(TalkID::Smith);
		CurrentTextLine = 14;
		break;
	case TalkID::SmithSell:
		StartStore(TalkID::Smith);
		CurrentTextLine = 16;
		break;
	case TalkID::SmithRepair:
		StartStore(TalkID::Smith);
		CurrentTextLine = 18;
		break;
	case TalkID::SmithGems:
		StartStore(TalkID::Smith);
		CurrentTextLine = SmithGemsLine();
		break;
	case TalkID::SmithGemRemove:
		StartStore(TalkID::SmithGems);
		CurrentTextLine = SmithGemRemoveLine;
		break;
	case TalkID::SmithGemSalvage:
		StartStore(TalkID::SmithGems);
		CurrentTextLine = SmithGemSalvageLine;
		break;
	case TalkID::WitchBuy:
		StartStore(TalkID::Witch);
		CurrentTextLine = 14;
		break;
	case TalkID::WitchSell:
		StartStore(TalkID::Witch);
		CurrentTextLine = 16;
		break;
	case TalkID::WitchRecharge:
		StartStore(TalkID::Witch);
		CurrentTextLine = *GetOptions().Gameplay.visualStoreUI ? 16 : 18;
		break;
	case TalkID::DungeonVendorBuy:
		StartStore(TalkID::DungeonVendor);
		CurrentTextLine = 12;
		break;
	case TalkID::DungeonVendorCraftTarget:
		StartStore(TalkID::DungeonVendor);
		CurrentTextLine = 14;
		break;
	case TalkID::DungeonVendorCraftMaterial:
		StartStore(TalkID::DungeonVendorCraftTarget);
		break;
	case TalkID::GambleSpin: // [RUNS MOD]
	case TalkID::GambleJackpotShow:
		StartStore(TalkID::Gamble);
		CurrentTextLine = 12;
		break;
	case TalkID::HealerBuy:
		StartStore(TalkID::Healer);
		CurrentTextLine = 14;
		break;
	case TalkID::StorytellerIdentify:
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 12;
		break;
	case TalkID::StorytellerRules: // [RUNS MOD]
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 14;
		break;
	case TalkID::StorytellerProgress: // [RUNS MOD]
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 16;
		break;
	case TalkID::StorytellerGuides: // [RUNS MOD]
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 18;
		break;
	case TalkID::StorytellerGuideShow: // [RUNS MOD]
		StartStore(TalkID::StorytellerGuides);
		break;
	case TalkID::StorytellerIdentifyShow:
		StartStore(TalkID::StorytellerIdentify);
		break;
	case TalkID::NoMoney:
	case TalkID::NoRoom:
	case TalkID::Confirm:
		StartStore(OldActiveStore);
		CurrentTextLine = OldTextLine;
		ScrollPos = OldScrollPos;
		break;
	case TalkID::None:
		break;
	}
}

void StoreUp()
{
	PlaySFX(SfxID::MenuMove);
	if (CurrentTextLine == -1) {
		return;
	}

	if (HasScrollbar) {
		if (CurrentTextLine == PreviousScrollPos) {
			if (ScrollPos != 0)
				ScrollPos--;
			return;
		}

		CurrentTextLine--;
		while (!TextLine[CurrentTextLine].isSelectable()) {
			if (CurrentTextLine == 0)
				CurrentTextLine = NumStoreLines - 1;
			else
				CurrentTextLine--;
		}
		return;
	}

	if (CurrentTextLine == 0)
		CurrentTextLine = NumStoreLines - 1;
	else
		CurrentTextLine--;

	while (!TextLine[CurrentTextLine].isSelectable()) {
		if (CurrentTextLine == 0)
			CurrentTextLine = NumStoreLines - 1;
		else
			CurrentTextLine--;
	}
}

void StoreDown()
{
	PlaySFX(SfxID::MenuMove);
	if (CurrentTextLine == -1) {
		return;
	}

	if (HasScrollbar) {
		if (CurrentTextLine == NextScrollPos) {
			if (ScrollPos < NumTextLines)
				ScrollPos++;
			return;
		}

		CurrentTextLine++;
		while (!TextLine[CurrentTextLine].isSelectable()) {
			if (CurrentTextLine == NumStoreLines - 1)
				CurrentTextLine = 0;
			else
				CurrentTextLine++;
		}
		return;
	}

	if (CurrentTextLine == NumStoreLines - 1)
		CurrentTextLine = 0;
	else
		CurrentTextLine++;

	while (!TextLine[CurrentTextLine].isSelectable()) {
		if (CurrentTextLine == NumStoreLines - 1)
			CurrentTextLine = 0;
		else
			CurrentTextLine++;
	}
}

void StorePrior()
{
	PlaySFX(SfxID::MenuMove);
	if (CurrentTextLine != -1 && HasScrollbar) {
		if (CurrentTextLine == PreviousScrollPos) {
			ScrollPos = std::max(ScrollPos - 4, 0);
		} else {
			CurrentTextLine = PreviousScrollPos;
		}
	}
}

void StoreNext()
{
	PlaySFX(SfxID::MenuMove);
	if (CurrentTextLine != -1 && HasScrollbar) {
		if (CurrentTextLine == NextScrollPos) {
			if (ScrollPos < NumTextLines)
				ScrollPos += 4;
			if (ScrollPos > NumTextLines)
				ScrollPos = NumTextLines;
		} else {
			CurrentTextLine = NextScrollPos;
		}
	}
}

void TakePlrsMoney(int cost)
{
	Player &myPlayer = *MyPlayer;

	myPlayer._pGold -= std::min(cost, myPlayer._pGold);

	cost = TakeGold(myPlayer, cost, true);
	if (cost != 0) {
		cost = TakeGold(myPlayer, cost, false);
	}

	Stash.gold -= cost;
	Stash.dirty = true;
}

void StoreEnter()
{
	if (qtextflag) {
		qtextflag = false;
		if (leveltype == DTYPE_TOWN)
			stream_stop();

		return;
	}

	PlaySFX(SfxID::MenuSelect);

	if (CurrentTextLine >= 0 && CurrentTextLine < NumStoreLines && CurrentExtraOptionIndices[CurrentTextLine].has_value()) {
		size_t optIdx = *CurrentExtraOptionIndices[CurrentTextLine];
		if (const char *townerName = TownerNameForTalkID(ActiveStore); townerName != nullptr) {
			if (auto *extraOpts = FindExtraTownerOptions(townerName); extraOpts != nullptr && optIdx < extraOpts->size()) {
				ActiveStore = TalkID::None;
				(*extraOpts)[optIdx].onSelect();
				// If onSelect() set ActiveStore (e.g. to open a sub-dialog), preserve it.
				// Otherwise it stays TalkID::None (dialog closed).
				return;
			}
		}
		ActiveStore = TalkID::None;
		return;
	}

	switch (ActiveStore) {
	case TalkID::Smith:
		SmithEnter();
		break;
	case TalkID::SmithPremiumBuy:
		SmithPremiumBuyEnter();
		break;
	case TalkID::SmithBuy:
		SmithBuyEnter();
		break;
	case TalkID::SmithSell:
		SmithSellEnter();
		break;
	case TalkID::SmithRepair:
		SmithRepairEnter();
		break;
	case TalkID::SmithGems:
		SmithGemsEnter();
		break;
	case TalkID::SmithGemRemove:
	case TalkID::SmithGemSalvage:
		SmithGemWorkEnter(ActiveStore);
		break;
	case TalkID::Witch:
		WitchEnter();
		break;
	case TalkID::WitchBuy:
		WitchBuyEnter();
		break;
	case TalkID::WitchSell:
		WitchSellEnter();
		break;
	case TalkID::WitchRecharge:
		WitchRechargeEnter();
		break;
	case TalkID::DungeonVendor:
		DungeonVendorEnter();
		break;
	case TalkID::DungeonVendorBuy:
		DungeonVendorBuyEnter();
		break;
	case TalkID::DungeonVendorCraftTarget:
		DungeonVendorCraftTargetEnter();
		break;
	case TalkID::DungeonVendorCraftMaterial:
		DungeonVendorCraftMaterialEnter();
		break;
	case TalkID::Gamble: // [RUNS MOD]
		GambleEnter();
		break;
	case TalkID::GambleSpin: // [RUNS MOD]
		GambleSpinEnter();
		break;
	case TalkID::GambleJackpotShow: // [RUNS MOD]
		StartStore(TalkID::Gamble);
		CurrentTextLine = 16; // back on "Inspect the jackpot" (14 is absent without tokens)
		break;
	case TalkID::NoMoney:
	case TalkID::NoRoom:
		StartStore(OldActiveStore);
		CurrentTextLine = OldTextLine;
		ScrollPos = OldScrollPos;
		break;
	case TalkID::Confirm:
		ConfirmEnter(TempItem);
		break;
	case TalkID::Boy:
		BoyEnter();
		break;
	case TalkID::BoyBuy:
		BoyBuyEnter();
		break;
	case TalkID::Healer:
		HealerEnter();
		break;
	case TalkID::Storyteller:
		StorytellerEnter();
		break;
	case TalkID::HealerBuy:
		HealerBuyEnter();
		break;
	case TalkID::StorytellerIdentify:
		StorytellerIdentifyEnter();
		break;
	case TalkID::StorytellerRules: // [RUNS MOD]
		StorytellerRulesEnter();
		break;
	case TalkID::StorytellerProgress: // [RUNS MOD] Done -> back to Cain's menu
		StartStore(TalkID::Storyteller);
		CurrentTextLine = 16;
		break;
	case TalkID::StorytellerGuides: // [RUNS MOD]
		StorytellerGuidesEnter();
		break;
	case TalkID::StorytellerGuideShow: // [RUNS MOD] Done -> back to the class list
		StartStore(TalkID::StorytellerGuides);
		break;
	case TalkID::Gossip:
		TalkEnter();
		break;
	case TalkID::StorytellerIdentifyShow:
		StartStore(TalkID::StorytellerIdentify);
		break;
	case TalkID::Drunk:
		DrunkEnter();
		break;
	case TalkID::Tavern:
		TavernEnter();
		break;
	case TalkID::Barmaid:
		BarmaidEnter();
		break;
	case TalkID::None:
		break;
	}
}

void CheckStoreBtn()
{
	const Point uiPosition = GetUIRectangle().position;
	const Rectangle windowRect { { uiPosition.x + 344, uiPosition.y + PaddingTop - 7 }, { 271, 303 } };
	const Rectangle windowRectFull { { uiPosition.x + 24, uiPosition.y + PaddingTop - 7 }, { 591, 303 } };

	if (!IsTextFullSize) {
		if (!windowRect.contains(MousePosition)) {
			while (IsPlayerInStore())
				StoreESC();
		}
	} else {
		if (!windowRectFull.contains(MousePosition)) {
			while (IsPlayerInStore())
				StoreESC();
		}
	}

	if (qtextflag) {
		qtextflag = false;
		if (leveltype == DTYPE_TOWN)
			stream_stop();
	} else if (CurrentTextLine != -1) {
		const int relativeY = MousePosition.y - (uiPosition.y + PaddingTop);

		if (HasScrollbar && MousePosition.x > 600 + uiPosition.x) {
			// Scroll bar is always measured in terms of the small line height.
			const int y = relativeY / SmallLineHeight;
			if (y == 4) {
				if (CountdownScrollUp <= 0) {
					StoreUp();
					CountdownScrollUp = 10;
				} else {
					CountdownScrollUp--;
				}
			}
			if (y == 20) {
				if (CountdownScrollDown <= 0) {
					StoreDown();
					CountdownScrollDown = 10;
				} else {
					CountdownScrollDown--;
				}
			}
			return;
		}

		int y = relativeY / LineHeight();

		// Large small fonts draw beyond LineHeight. Check if the click was on the overflow text.
		if (IsSmallFontTall() && y > 0 && y < NumStoreLines
		    && TextLine[y - 1].hasText() && !TextLine[y].hasText()
		    && relativeY < TextLine[y - 1].y + LargeTextHeight) {
			--y;
		}

		if (y >= 5) {
			if (y >= BackButtonLine() + 1)
				y = BackButtonLine();
			if (HasScrollbar && y <= 20 && !TextLine[y].isSelectable()) {
				if (TextLine[y - 2].isSelectable()) {
					y -= 2;
				} else if (TextLine[y - 1].isSelectable()) {
					y--;
				}
			}
			if (TextLine[y].isSelectable() || (HasScrollbar && y == BackButtonLine())) {
				CurrentTextLine = y;
				StoreEnter();
			}
		}
	}
}

void ReleaseStoreBtn()
{
	CountdownScrollUp = -1;
	CountdownScrollDown = -1;
}

bool IsPlayerInStore()
{
	return ActiveStore != TalkID::None;
}

// TODO: Change `_iIvalue` to be unsigned instead of passing `int` here.
bool PlayerCanAfford(int price)
{
	return TotalPlayerGold() >= static_cast<uint32_t>(price);
}

bool SmithWillBuy(const Item &item)
{
	if (item.isEmpty())
		return false;

	if (item._iMiscId > IMISC_OILFIRST && item._iMiscId < IMISC_OILLAST)
		return true;

	if (item._itype == ItemType::Misc)
		return false;
	if (item._itype == ItemType::Gold)
		return false;
	if (item._itype == ItemType::Staff && (!gbIsHellfire || IsValidSpell(item._iSpell)))
		return false;
	if (item._iClass == ICLASS_QUEST)
		return false;
	if (item.IDidx == IDI_LAZSTAFF)
		return false;

	return true;
}

bool WitchWillBuy(const Item &item)
{
	if (item.isEmpty())
		return false;

	bool rv = false;

	if (item._itype == ItemType::Misc)
		rv = true;
	if (item._iMiscId > 29 && item._iMiscId < 41)
		rv = false;
	if (item._iClass == ICLASS_QUEST)
		rv = false;
	if (item._itype == ItemType::Staff && (!gbIsHellfire || IsValidSpell(item._iSpell)))
		rv = true;
	if (item.IDidx >= IDI_FIRSTQUEST && item.IDidx <= IDI_LASTQUEST)
		rv = false;
	if (item.IDidx == IDI_LAZSTAFF)
		rv = false;

	return rv;
}

} // namespace devilution

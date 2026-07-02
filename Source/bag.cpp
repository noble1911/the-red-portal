/**
 * @file bag.cpp
 *
 * [RUNS MOD] Implementation of the per-character carried bag. See bag.h. Mirrors the
 * stash's grid logic (items moved via the held-item cursor) but single-page 10x8.
 */
#include "bag.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "control/control.hpp"
#include "cursor.h"
#include "engine/clx_sprite.hpp"
#include "engine/displacement.hpp"
#include "engine/load_clx.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "headless_mode.hpp"
#include "inv.h"
#include "minitext.h"
#include "panels/ui_panels.hpp"
#include "quests.h"
#include "qol/stash.h"
#include "utils/language.h"

namespace devilution {

bool IsBagOpen;
BagStruct Bag;
uint16_t pcursbagitem;

namespace {

OptionalOwnedClxSpriteList BagPanelArt;

void AddItemToBagGrid(Point position, uint16_t bagListIndex, Size itemSize)
{
	for (const Point point : PointsInRectangle(Rectangle { position, itemSize })) {
		Bag.grid[point.x][point.y] = bagListIndex + 1;
	}
}

std::optional<Point> FindTargetSlotUnderItemCursor(Point cursorPosition, Size itemSize)
{
	for (auto point : BagGridRange) {
		const Rectangle cell { GetBagSlotCoord(point), InventorySlotSizeInPixels + 1 };
		if (cell.contains(cursorPosition)) {
			if (itemSize.height <= 1 && itemSize.width <= 1)
				return point;
			Displacement hotPixelCellOffset = { (itemSize.width - 1) / 2, (itemSize.height - 1) / 2 };
			if (itemSize.width % 2 == 0 && cell.contains(cursorPosition + Displacement { INV_SLOT_HALF_SIZE_PX, 0 }))
				hotPixelCellOffset.deltaX++;
			if (itemSize.height % 2 == 0 && cell.contains(cursorPosition + Displacement { 0, INV_SLOT_HALF_SIZE_PX }))
				hotPixelCellOffset.deltaY++;
			point.y = std::clamp(point.y - hotPixelCellOffset.deltaY, 0, BagGridSize.height - itemSize.height);
			point.x = std::clamp(point.x - hotPixelCellOffset.deltaX, 0, BagGridSize.width - itemSize.width);
			return point;
		}
	}
	return {};
}

bool IsItemAllowedInBag(const Item &item)
{
	// Gold is not held in the bag; other consumables/gear are fine.
	return item._itype != ItemType::Gold && item._iMiscId != IMISC_ARENAPOT;
}

void CheckBagPaste(Point cursorPosition)
{
	Player &player = *MyPlayer;
	if (!IsItemAllowedInBag(player.HoldItem))
		return;

	const Size itemSize = GetInventorySize(player.HoldItem);
	std::optional<Point> targetSlot = FindTargetSlotUnderItemCursor(cursorPosition, itemSize);
	if (!targetSlot)
		return;
	const Point firstSlot = *targetSlot;

	// At most one item may be displaced by the paste.
	BagStruct::BagCell bagIndex = BagStruct::EmptyCell;
	for (const Point point : PointsInRectangle(Rectangle { firstSlot, itemSize })) {
		const BagStruct::BagCell iv = Bag.GetItemIdAtPosition(point);
		if (iv == BagStruct::EmptyCell || bagIndex == iv)
			continue;
		if (bagIndex == BagStruct::EmptyCell) {
			bagIndex = iv;
			continue;
		}
		return; // a second item is in the way
	}

	PlaySFX(ItemInvSnds[ItemCAnimTbl[player.HoldItem._iCurs]]);
	// Anchor at the bottom-left, matching the draw code.
	player.HoldItem.position = firstSlot + Displacement { 0, itemSize.height - 1 };

	if (bagIndex == BagStruct::EmptyCell) {
		Bag.items.emplace_back(player.HoldItem.pop());
		bagIndex = static_cast<uint16_t>(Bag.items.size() - 1);
	} else {
		std::swap(Bag.items[bagIndex], player.HoldItem);
		for (auto &col : Bag.grid)
			for (auto &itemId : col)
				if (itemId - 1 == bagIndex)
					itemId = 0;
	}
	AddItemToBagGrid(firstSlot, bagIndex, itemSize);
	Bag.dirty = true;
	NewCursor(player.HoldItem);
}

void CheckBagCut(Point cursorPosition, bool automaticMove)
{
	Player &player = *MyPlayer;

	Point slot = InvalidBagPoint;
	for (auto point : BagGridRange) {
		const Rectangle cell { GetBagSlotCoord(point), InventorySlotSizeInPixels + 1 };
		if (cell.contains(cursorPosition)) {
			slot = point;
			break;
		}
	}
	if (slot == InvalidBagPoint)
		return;

	Item &holdItem = player.HoldItem;
	holdItem.clear();
	bool automaticallyMoved = false;

	const BagStruct::BagCell iv = Bag.GetItemIdAtPosition(slot);
	if (iv != BagStruct::EmptyCell) {
		holdItem = Bag.items[iv];
		if (automaticMove) {
			if (CanBePlacedOnBelt(player, holdItem))
				automaticallyMoved = AutoPlaceItemInBelt(player, holdItem, true, true);
			else
				automaticallyMoved = AutoEquip(player, holdItem, true, true);
		}
		if (!automaticMove || automaticallyMoved)
			Bag.RemoveBagItem(iv);
	}

	if (!holdItem.isEmpty()) {
		CalcPlrInv(player, true);
		holdItem._iStatFlag = player.CanUseItem(holdItem);
		if (!automaticMove || automaticallyMoved)
			PlaySFX(SfxID::GrabItem);
		if (automaticMove) {
			if (!automaticallyMoved) {
				if (CanBePlacedOnBelt(player, holdItem))
					player.SaySpecific(HeroSpeech::IHaveNoRoom);
				else
					player.SaySpecific(HeroSpeech::ICantDoThat);
			}
			holdItem.clear();
		} else {
			NewCursor(holdItem);
		}
	}
}

} // namespace

bool AutoPlaceItemInBag(const Item &item, bool persistItem)
{
	if (!IsItemAllowedInBag(item))
		return false;

	const Size itemSize = GetInventorySize(item);
	for (const Point firstSlot : PointsInRectangle(Rectangle { { 0, 0 },
	         Size { BagGridSize.width - (itemSize.width - 1), BagGridSize.height - (itemSize.height - 1) } })) {
		bool isSpaceFree = true;
		for (const Point p : PointsInRectangle(Rectangle { firstSlot, itemSize })) {
			if (Bag.GetItemIdAtPosition(p) != BagStruct::EmptyCell) {
				isSpaceFree = false;
				break;
			}
		}
		if (!isSpaceFree)
			continue;
		if (persistItem) {
			Bag.items.push_back(item);
			const auto bagIndex = static_cast<uint16_t>(Bag.items.size() - 1);
			Bag.items[bagIndex].position = firstSlot + Displacement { 0, itemSize.height - 1 };
			AddItemToBagGrid(firstSlot, bagIndex, itemSize);
			Bag.dirty = true;
		}
		return true;
	}
	return false;
}

void TransferBagItemToInventory(Player &player, uint16_t itemId)
{
	if (itemId == BagStruct::EmptyCell)
		return;
	const Item &item = Bag.items[itemId];
	if (item.isEmpty())
		return;
	if (!AutoPlaceItemInInventory(player, item)) {
		player.SaySpecific(HeroSpeech::IHaveNoRoom);
		return;
	}
	PlaySFX(ItemInvSnds[ItemCAnimTbl[item._iCurs]]);
	Bag.RemoveBagItem(itemId);
}

bool BagOnLeftPanel()
{
	// The bag stacks directly below the inventory when there is vertical room between the
	// inventory panel and the bottom (main) panel. On short displays (e.g. 720p / Switch
	// handheld) it would overlap, so fall back to the left-hand (stash) panel position.
	const int stackedBottom = GetRightPanel().position.y + 2 * SidePanelSize.height;
	return stackedBottom > GetMainPanel().position.y;
}

Point GetBagPanelPosition()
{
	if (BagOnLeftPanel())
		return GetPanelPosition(UiPanels::Stash);
	// Directly below the inventory panel, same width, so the grids line up and the two
	// read as one continuous inventory.
	return GetRightPanel().position + Displacement { 0, SidePanelSize.height };
}

Rectangle GetBagRect()
{
	return Rectangle { GetBagPanelPosition(), SidePanelSize };
}

Point GetBagSlotCoord(Point slot)
{
	constexpr int BagNextCell = INV_SLOT_SIZE_PX + 1;
	// The bag grid shares the stash's panel-relative layout (x=17, 29px spacing), which is
	// also the inventory backpack's column layout -- so stacked below the inventory the
	// columns align exactly.
	return GetBagPanelPosition() + Displacement { slot.x * BagNextCell + 17, slot.y * BagNextCell + 48 };
}

void BagStruct::RemoveBagItem(BagStruct::BagCell iv)
{
	for (auto &col : Bag.grid)
		for (BagStruct::BagCell &itemId : col)
			if (itemId - 1 == iv)
				itemId = 0;

	const auto lastItemIndex = static_cast<BagStruct::BagCell>(items.size() - 1);
	if (lastItemIndex != iv) {
		items[iv] = items[lastItemIndex];
		for (auto &col : Bag.grid)
			for (BagStruct::BagCell &itemId : col)
				if (itemId == lastItemIndex + 1)
					itemId = iv + 1;
	}
	items.pop_back();
	Bag.dirty = true;
}

void BagStruct::RefreshItemStatFlags()
{
	for (Item &item : items)
		item._iStatFlag = MyPlayer->CanUseItem(item);
}

void InitBag()
{
	if (!HeadlessMode)
		BagPanelArt = LoadClx("data\\stash.clx");
}

void FreeBagGFX()
{
	BagPanelArt = std::nullopt;
}

void OpenBag()
{
	// Bag is a left-side panel shown alongside the inventory; close the other left
	// panels so only one is visible at a time.
	invflag = true;
	IsBagOpen = true;
	CharFlag = false;
	QuestLogIsOpen = false;
	IsStashOpen = false;
	Bag.RefreshItemStatFlags();
}

void CloseBag()
{
	IsBagOpen = false;
}

void DrawBag(const Surface &out)
{
	// [RUNS MOD] Unlike the town-only stash, the bag is carried and can be opened in
	// dungeons -- but its art is freed on every level transition (FreeGameMem) and only
	// reloaded on town entry (InitBag). Reload it on demand so drawing it outside town
	// does not dereference an empty optional (assert value_data_ != nullptr).
	if (!BagPanelArt)
		InitBag();
	if (!BagPanelArt)
		return; // headless / load failed

	const Point panel = GetBagPanelPosition();
	RenderClxSprite(out, (*BagPanelArt)[0], panel);

	// Mask off the stash-specific chrome (gold/page/nav header) and the two grid rows
	// the bag does not use, so the reused 10x10 panel reads as a clean 10x8 bag.
	// Use an opaque fill (not a half-transparent overlay) so the hidden grid rows and
	// header widgets do not show through.
	constexpr uint8_t MaskColor = 0; // black
	const int panelWidth = (*BagPanelArt)[0].width();
	const int panelBottom = panel.y + (*BagPanelArt)[0].height();
	// Header: everything above the first grid row (the grid starts 48px down -- see
	// the Displacement in GetBagSlotCoord).
	const int gridTop = GetBagSlotCoord({ 0, 0 }).y;
	FillRect(out, panel.x, panel.y, panelWidth, gridTop - panel.y, MaskColor);
	// Bottom: from the top of the first unused row (row index BagGridSize.height) down.
	const int unusedTop = GetBagSlotCoord({ 0, BagGridSize.height }).y - 1;
	if (panelBottom > unusedTop)
		FillRect(out, panel.x, unusedTop, panelWidth, panelBottom - unusedTop, MaskColor);
	DrawString(out, _("Bag"), { panel + Displacement { 0, 6 }, { panelWidth, 24 } },
	    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });

	constexpr Displacement offset { 0, INV_SLOT_SIZE_PX - 1 };
	for (auto slot : BagGridRange) {
		const BagStruct::BagCell itemId = Bag.GetItemIdAtPosition(slot);
		if (itemId == BagStruct::EmptyCell)
			continue;
		const Item &item = Bag.items[itemId];
		InvDrawSlotBack(out, GetBagSlotCoord(slot) + offset, InventorySlotSizeInPixels, item._iMagical);
	}
	for (auto slot : BagGridRange) {
		const BagStruct::BagCell itemId = Bag.GetItemIdAtPosition(slot);
		if (itemId == BagStruct::EmptyCell)
			continue;
		const Item &item = Bag.items[itemId];
		if (item.position != slot)
			continue;
		const int frame = item._iCurs + CURSOR_FIRSTITEM;
		const Point position = GetBagSlotCoord(item.position) + offset;
		const ClxSprite sprite = GetInvItemSprite(frame);
		if (pcursbagitem == itemId) {
			const uint8_t color = GetOutlineColor(item, true);
			ClxDrawOutline(out, color, position, sprite);
		}
		DrawItem(item, out, position, sprite);
	}
}

void CheckBagItem(Point mousePosition, bool isShiftHeld, bool isCtrlHeld)
{
	if (!MyPlayer->HoldItem.isEmpty())
		CheckBagPaste(mousePosition);
	else if (isCtrlHeld)
		TransferBagItemToInventory(*MyPlayer, pcursbagitem);
	else
		CheckBagCut(mousePosition, isShiftHeld);
}

uint16_t CheckBagHLight(Point mousePosition)
{
	Point slot = InvalidBagPoint;
	for (auto point : BagGridRange) {
		const Rectangle cell { GetBagSlotCoord(point), InventorySlotSizeInPixels + 1 };
		if (cell.contains(mousePosition)) {
			slot = point;
			break;
		}
	}
	if (slot == InvalidBagPoint)
		return BagStruct::EmptyCell;

	InfoColor = UiFlags::ColorWhite;

	const BagStruct::BagCell itemId = Bag.GetItemIdAtPosition(slot);
	if (itemId == BagStruct::EmptyCell)
		return BagStruct::EmptyCell;

	const Item &item = Bag.items[itemId];
	if (item.isEmpty())
		return BagStruct::EmptyCell;

	InfoColor = item.getTextColor();
	InfoString = item.getName();
	FloatingInfoString = item.getName();
	if (item._iIdentified)
		PrintItemDetails(item);
	else
		PrintItemDur(item);

	return itemId;
}

} // namespace devilution

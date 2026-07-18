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
#include "plrmsg.h"
#include "quests.h"
#include "qol/stash.h"
#include "qol/visual_store.h"
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
	// [RUNS MOD] Placement is confined to the unlocked rows; InitialBagRows >= 3
	// guarantees even the tallest (2x3) items always have a valid clamp range.
	const int unlockedRows = Bag.unlockedRows;
	if (itemSize.height > unlockedRows)
		return {};
	for (auto point : PointsInRectangle(Rectangle { { 0, 0 }, Size { BagGridSize.width, unlockedRows } })) {
		const Rectangle cell { GetBagSlotCoord(point), InventorySlotSizeInPixels + 1 };
		if (cell.contains(cursorPosition)) {
			if (itemSize.height <= 1 && itemSize.width <= 1)
				return point;
			Displacement hotPixelCellOffset = { (itemSize.width - 1) / 2, (itemSize.height - 1) / 2 };
			if (itemSize.width % 2 == 0 && cell.contains(cursorPosition + Displacement { INV_SLOT_HALF_SIZE_PX, 0 }))
				hotPixelCellOffset.deltaX++;
			if (itemSize.height % 2 == 0 && cell.contains(cursorPosition + Displacement { 0, INV_SLOT_HALF_SIZE_PX }))
				hotPixelCellOffset.deltaY++;
			point.y = std::clamp(point.y - hotPixelCellOffset.deltaY, 0, unlockedRows - itemSize.height);
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
	// [RUNS MOD] Gold never lives in the bag: the gold total and every spending path
	// scan the inventory only, so bag gold would be money vendors cannot see. Since the
	// bag renders as an extension of the inventory, honor the drop gesture anyway by
	// merging the gold into the inventory's stacks; any remainder that does not fit
	// stays on the cursor.
	if (player.HoldItem._itype == ItemType::Gold) {
		if (GoldAutoPlace(player, player.HoldItem)) {
			PlaySFX(SfxID::ItemGold);
			player.HoldItem.clear();
			NewCursor(player.HoldItem);
		}
		return;
	}
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
	for (auto point : PointsInRectangle(Rectangle { { 0, 0 }, Size { BagGridSize.width, Bag.unlockedRows } })) {
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
	// [RUNS MOD] Only scan the unlocked rows for a free spot.
	if (itemSize.height > Bag.unlockedRows)
		return false;
	for (const Point firstSlot : PointsInRectangle(Rectangle { { 0, 0 },
	         Size { BagGridSize.width - (itemSize.width - 1), Bag.unlockedRows - (itemSize.height - 1) } })) {
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

// [RUNS MOD] Inventory panel art geometry (from InvRect in inv.cpp): the backpack grid's
// first row band starts at y=222 with a 29px pitch, the last row ends at y=338, and the
// panel's bottom border occupies y=338..352. The stacked bag repeats the row band and
// re-stamps the border, so inventory + bag read as ONE taller panel.
namespace {
constexpr int InvGridLeft = 17;
constexpr int InvRowBandTop = 222;
constexpr int InvRowPitch = INV_SLOT_SIZE_PX + 1; // 29
constexpr int InvLastRowBottom = 338;
constexpr int InvBottomBorderHeight = 14; // 352 - 338
} // namespace

bool IsBagShown()
{
	return IsBagOpen && !(IsVisualStoreOpen && BagOnLeftPanel());
}

bool BagOnLeftPanel()
{
	// The bag extends the inventory panel downward when there is vertical room above the
	// bottom (main) panel; on short displays fall back to the left-hand (stash) position.
	// Only the UNLOCKED rows need space, so a young bag fits where a full one may not.
	const int stackedBottom = GetRightPanel().position.y + InvLastRowBottom
	    + Bag.unlockedRows * InvRowPitch + InvBottomBorderHeight;
	return stackedBottom > GetMainPanel().position.y;
}

Point GetBagPanelPosition()
{
	if (BagOnLeftPanel())
		return GetPanelPosition(UiPanels::Stash);
	// The stacked bag is a continuation of the inventory panel itself.
	return GetRightPanel().position;
}

Rectangle GetBagRect()
{
	if (BagOnLeftPanel())
		return Rectangle { GetBagPanelPosition(), SidePanelSize };
	return Rectangle { GetRightPanel().position + Displacement { 0, InvLastRowBottom },
		Size { SidePanelSize.width, Bag.unlockedRows * InvRowPitch + InvBottomBorderHeight } };
}

Point GetBagSlotCoord(Point slot)
{
	if (BagOnLeftPanel()) {
		// Stash-panel layout: header on top, grid starting 48px down.
		return GetBagPanelPosition() + Displacement { slot.x * InvRowPitch + InvGridLeft, slot.y * InvRowPitch + 48 };
	}
	// Stacked: continue the backpack grid directly (row 0 sits right under inv row 4).
	return GetRightPanel().position + Displacement { slot.x * InvRowPitch + InvGridLeft, InvLastRowBottom + slot.y * InvRowPitch };
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
	invflag = true;
	IsBagOpen = true;
	// [RUNS MOD] Left panels conflict only when the bag itself needs the left-panel
	// fallback (short displays). Stacked below the inventory it coexists with the
	// stash, character and quest panels -- notably, the stash + bag together is how
	// items get ferried between long-term storage and the carried overflow.
	if (BagOnLeftPanel()) {
		CharFlag = false;
		QuestLogIsOpen = false;
		IsStashOpen = false;
	}
	Bag.RefreshItemStatFlags();
}

void CloseBag()
{
	IsBagOpen = false;
}

void DrawBag(const Surface &out)
{
	// [RUNS MOD] On short displays the bag's fallback position is the left panel -- the
	// same spot the visual store's grid occupies. The store wins; the bag hides until
	// the store closes (its items are still sellable via lift-and-drop from inventory,
	// and always via the classic text store).
	if (IsVisualStoreOpen && BagOnLeftPanel())
		return;
	if (!BagOnLeftPanel()) {
		// [RUNS MOD] Seamless mode: extend the inventory panel itself. Repeat the
		// backpack grid's row band (cropped from the inventory art) for each unlocked
		// row, then re-stamp the panel's bottom border below the last one -- the
		// inventory and bag read as one taller panel, and locked rows simply don't
		// exist yet. DrawInv has already drawn the panel above us in this frame.
		std::optional<ClxSprite> invSprite = GetInventoryPanelSprite();
		if (!invSprite)
			return; // headless / gfx not loaded
		const Point invPos = GetRightPanel().position;
		for (int row = 0; row < Bag.unlockedRows; row++) {
			const Surface band = out.subregion(invPos.x, invPos.y + InvLastRowBottom + row * InvRowPitch,
			    SidePanelSize.width, InvRowPitch);
			RenderClxSprite(band, *invSprite, { 0, -InvRowBandTop });
		}
		const Surface borderBand = out.subregion(invPos.x,
		    invPos.y + InvLastRowBottom + Bag.unlockedRows * InvRowPitch,
		    SidePanelSize.width, InvBottomBorderHeight);
		RenderClxSprite(borderBand, *invSprite, { 0, -InvLastRowBottom });
	} else {
		// [RUNS MOD] Left-panel fallback (short displays): standalone panel reusing the
		// stash art. The art is freed on level transitions (FreeGameMem) and only
		// reloaded on town entry (InitBag); reload on demand so drawing in a dungeon
		// does not dereference an empty optional.
		if (!BagPanelArt)
			InitBag();
		if (!BagPanelArt)
			return; // headless / load failed

		const Point panel = GetBagPanelPosition();
		RenderClxSprite(out, (*BagPanelArt)[0], panel);

		// Mask off the stash-specific chrome (gold/page/nav header) and every row past
		// the unlocked ones, opaque so the hidden widgets don't show through.
		constexpr uint8_t MaskColor = 0; // black
		const int panelWidth = (*BagPanelArt)[0].width();
		const int panelBottom = panel.y + (*BagPanelArt)[0].height();
		const int gridTop = GetBagSlotCoord({ 0, 0 }).y;
		FillRect(out, panel.x, panel.y, panelWidth, gridTop - panel.y, MaskColor);
		const int unusedTop = GetBagSlotCoord({ 0, Bag.unlockedRows }).y - 1;
		if (panelBottom > unusedTop)
			FillRect(out, panel.x, unusedTop, panelWidth, panelBottom - unusedTop, MaskColor);
		DrawString(out, _("Bag"), { panel + Displacement { 0, 6 }, { panelWidth, 24 } },
		    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhitegold });
		// Tell the player how the bag grows while rows are still locked.
		if (Bag.unlockedRows < BagGridSize.height) {
			const int lockedHeight = GetBagSlotCoord({ 0, BagGridSize.height }).y - 1 - unusedTop;
			DrawString(out, _("Defeat deeper bosses to grow your bag"),
			    { Point { panel.x, unusedTop }, { panelWidth, lockedHeight } },
			    { .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::ColorWhite | UiFlags::FontSize12 });
		}
	}

	constexpr Displacement offset { 0, INV_SLOT_SIZE_PX - 1 };
	for (auto slot : BagGridRange) {
		const BagStruct::BagCell itemId = Bag.GetItemIdAtPosition(slot);
		if (itemId == BagStruct::EmptyCell || Bag.items[itemId].isEmpty())
			continue;
		const Item &item = Bag.items[itemId];
		InvDrawSlotBack(out, GetBagSlotCoord(slot) + offset, InventorySlotSizeInPixels, item._iMagical);
	}
	for (auto slot : BagGridRange) {
		const BagStruct::BagCell itemId = Bag.GetItemIdAtPosition(slot);
		// [RUNS MOD] Skip cleared items too: CheckBagHLight already refuses to pick them up,
		// so drawing them would put an un-grabbable garbage sprite on the grid.
		if (itemId == BagStruct::EmptyCell || Bag.items[itemId].isEmpty())
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

void TryUnlockBagRowForDepth(int clearedDepth)
{
	// [RUNS MOD] Rows 4..8 unlock at depths 2, 4, 6, 8, 10: each unlocked row raises the
	// depth needed for the next, so growing the bag requires actually going deeper rather
	// than farming the first run. Runs on every client (each grows its own local bag),
	// mirroring how the difficulty-tier unlock updates each client's own hero.
	if (Bag.unlockedRows >= BagGridSize.height)
		return;
	const int requiredDepth = (Bag.unlockedRows - InitialBagRows + 1) * 2;
	if (clearedDepth < requiredDepth)
		return;
	Bag.unlockedRows++;
	Bag.dirty = true;
	EventPlrMsg(_("Your bag has grown by a row."));
}

void CheckBagItem(Point mousePosition, bool isShiftHeld, bool isCtrlHeld)
{
	// [RUNS MOD] Hidden while the visual store occupies the bag's fallback panel.
	if (IsVisualStoreOpen && BagOnLeftPanel())
		return;
	if (!MyPlayer->HoldItem.isEmpty()) {
		CheckBagPaste(mousePosition);
	} else if (isCtrlHeld) {
		// [RUNS MOD] With a vendor open, ctrl-click sells straight from the bag,
		// matching the inventory's ctrl-click sale; otherwise it quick-transfers
		// to the inventory as before.
		if (IsVisualStoreOpen)
			SellBagItemToVisualStore(pcursbagitem);
		else
			TransferBagItemToInventory(*MyPlayer, pcursbagitem);
	} else {
		CheckBagCut(mousePosition, isShiftHeld);
	}
}

uint16_t CheckBagHLight(Point mousePosition)
{
	// [RUNS MOD] Hidden while the visual store occupies the bag's fallback panel.
	if (IsVisualStoreOpen && BagOnLeftPanel())
		return BagStruct::EmptyCell;
	Point slot = InvalidBagPoint;
	for (auto point : PointsInRectangle(Rectangle { { 0, 0 }, Size { BagGridSize.width, Bag.unlockedRows } })) {
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

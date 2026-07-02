/**
 * @file bag.h
 *
 * [RUNS MOD] A per-character "bag": an extra 10x8 (80-slot) inventory grid the player
 * carries everywhere. Modeled on the stash (a programmatic grid moved via the held-item
 * cursor) but single-page, per-character, and saved in the hero save. Local-only, like
 * the stash -- moving items between the bag and the inventory needs no network sync.
 */
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/point.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "engine/rectangle.hpp"
#include "engine/size.hpp"
#include "engine/surface.hpp"
#include "items.h"
#include "utils/attributes.h"

namespace devilution {

inline constexpr Size BagGridSize { 10, 8 };
inline constexpr PointsInRectangle<int> BagGridRange { { { 0, 0 }, BagGridSize } };

class BagStruct {
public:
	using BagCell = uint16_t;
	// grid[x][y]: 10 columns of 8 rows; a cell stores itemIndex+1 (0 = empty).
	using BagGrid = std::array<std::array<BagCell, 8>, 10>;
	static constexpr BagCell EmptyCell = -1;

	BagGrid grid {};
	std::vector<Item> items;
	bool dirty = false;

	/** @brief 0-based index of the item at a grid position, or EmptyCell. */
	BagCell GetItemIdAtPosition(Point p)
	{
		// BagCell is unsigned, so an empty (0) cell underflows to EmptyCell.
		return grid[p.x][p.y] - 1;
	}
	bool IsItemAtPosition(Point p)
	{
		return GetItemIdAtPosition(p) != EmptyCell;
	}
	void RemoveBagItem(BagCell iv);
	/** @brief Updates _iStatFlag for all bag items. */
	void RefreshItemStatFlags();
	void clear()
	{
		grid = {};
		items.clear();
		dirty = false;
	}
};

constexpr Point InvalidBagPoint { -1, -1 };

extern bool IsBagOpen;
extern BagStruct Bag;
/** @brief 0-based index of the bag item under the cursor, or EmptyCell. */
extern uint16_t pcursbagitem;

Point GetBagSlotCoord(Point slot);
/** @brief Top-left screen position of the bag panel (stacked below the inventory, or on
 * the left panel when it does not fit below). */
Point GetBagPanelPosition();
/** @brief The bag panel rectangle, for cursor hit-testing. */
Rectangle GetBagRect();
/** @brief True when the bag is drawn at the left-panel position instead of below the inventory. */
bool BagOnLeftPanel();
void InitBag();
void FreeBagGFX();
void DrawBag(const Surface &out);
void CheckBagItem(Point mousePosition, bool isShiftHeld = false, bool isCtrlHeld = false);
uint16_t CheckBagHLight(Point mousePosition);
/** @brief [RUNS MOD] Opens the bag alongside the inventory (closing other left panels). */
void OpenBag();
/** @brief [RUNS MOD] Closes the bag. */
void CloseBag();

} // namespace devilution

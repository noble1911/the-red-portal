/**
 * @file runs_icons.h
 *
 * [RUNS MOD] Mod-supplied inventory icons.
 *
 * The mod adds items the vanilla art has no picture for. Repainting an objcurs frame is
 * not an option -- every scroll in the game shares ICURS_SCROLL_OF, so the dungeon scroll
 * cannot be given its own look without also changing Town Portal, Identify and the rest.
 *
 * So these icons live in their own sprite list addressed past both objcurs sheets, exactly
 * like the gems (see runs_gems.h). GetInvItemSprite routes the ids here.
 *
 * Art ships as true RGB in assets/data/inv/runicons.dat and is quantised against the live
 * palette, because Diablo loads a different palette per dungeon type. The container is the
 * same "DXGM" format the gems use -- see LoadIconArt in runs_icons.cpp for the layout, and
 * scripts/make-inv-icon.ps1 to rebuild the file from ordinary images.
 *
 * Icons must be an exact multiple of 28px on both axes: an item's inventory footprint is
 * derived from its bitmap, not declared (GetInventorySize in inv.cpp divides by 28).
 */
#pragma once

#include <cstdint>

#include "engine/clx_sprite.hpp"

namespace devilution {

/** @brief One entry per mod-supplied icon, in the order they appear in runicons.dat. */
enum class RunIcon : uint8_t {
	DungeonScroll,
	LAST = DungeonScroll,
};

constexpr int NumRunIcons = static_cast<int>(RunIcon::LAST) + 1;

/** @brief Sprite-sheet slot of the first mod icon.
 *
 * A fixed constant, not a derived one, for the same reason as GemCursFirst: items keep
 * `_iCurs` in the save file and on the wire, and objcurs2 only exists when Hellfire is
 * active, so anything derived from the sheet sizes would renumber stored items. Chosen
 * above the gem block (256..290) with room for that block to grow.
 */
constexpr int RunIconCursFirst = 320;

/** @brief The `_iCurs` value that draws this icon. */
constexpr int RunIconCurs(RunIcon icon)
{
	return RunIconCursFirst + static_cast<int>(icon);
}

/** @brief True if `curs` addresses the mod icon list rather than objcurs/objcurs2/gems. */
constexpr bool IsRunIconCurs(int curs)
{
	return curs >= RunIconCursFirst && curs < RunIconCursFirst + NumRunIcons;
}

/** @brief Sprite for a mod icon `_iCurs`. Only valid for IsRunIconCurs(curs). */
ClxSprite GetRunIconSprite(int curs);

/** @brief Rebuilds the sprites for the palette that is currently loaded. */
void RefreshRunIconSprites();
void FreeRunIconSprites();

/** @brief True once the art file has been read and found usable.
 *
 * Callers that would otherwise hand out a RunIcon `_iCurs` should fall back to a vanilla
 * frame when this is false, so a missing or corrupt art file degrades to the old icon
 * instead of an invisible item.
 */
bool RunIconsAvailable();

} // namespace devilution

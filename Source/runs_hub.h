/**
 * @file runs_hub.h
 *
 * [RUNS MOD] Where things stand in the compact hub: the vendors, the stash chest, the
 * dungeon and town portals, and each player's arrival spot.
 *
 * Positions live in txtdata\runs\hub_layout.tsv (id, x, y in world tiles) so the hub can
 * be rearranged without touching code. Every spot has a built-in default, used when the
 * file or a row is missing. In Debug builds the dev.hub console commands move a spot to
 * where the hero is standing and write the file to the pref path, which overrides the
 * shipped copy -- see lua/modules/dev/hub.cpp.
 *
 * The layout is shipped data, so co-op peers on the same build agree on it; that matters
 * for the dungeon portal, whose tile both clients check on their own.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "engine/point.hpp"

namespace devilution {

enum class HubSpot : uint8_t {
	Griswold,
	Pepin,
	Adria,
	Cain,
	StashChest,
	Farnham,
	DungeonMerchant,
	DungeonPortal,
	/** Player 1's town portal; player N's sits 2N tiles east (see GetPortalTownPos). */
	TownPortal,
	Spawn1,
	Spawn2,
	Spawn3,
	Spawn4,
	LAST = Spawn4,
};

constexpr int NumHubSpots = static_cast<int>(HubSpot::LAST) + 1;
constexpr int NumHubSpawns = 4;

/** @brief Where `spot` is, loading the layout file on first use. */
Point HubPosition(HubSpot spot);

/** @brief Arrival tile for the player with this id; players past the fourth share spawns. */
Point HubSpawnPosition(size_t playerId);

/** @brief The id used for `spot` in hub_layout.tsv and the dev.hub commands. */
std::string_view HubSpotId(HubSpot spot);
std::optional<HubSpot> HubSpotFromId(std::string_view id);

/** @brief Where `spot` stands when the layout file does not say. */
Point HubDefaultPosition(HubSpot spot);

/** @brief Moves `spot` for this session (dev.hub.place); call SaveHubLayout to keep it. */
void SetHubPosition(HubSpot spot, Point position);

/**
 * @brief Why the current layout is unsafe, or empty if it is fine.
 *
 * A town portal, or the tile a player lands on leaving one (portal + (1,1), see
 * GetPortalLvlPos), must not be the dungeon portal: leaving a run would drop that player
 * straight back in. Nor may a spawn be the dungeon portal.
 */
std::string HubLayoutProblem();

/** @brief Writes the layout to <pref path>/txtdata/runs/hub_layout.tsv; returns that path. */
std::string SaveHubLayout();

/** @brief Drops the loaded layout so the next HubPosition re-reads the file. */
void ReloadHubLayout();

} // namespace devilution

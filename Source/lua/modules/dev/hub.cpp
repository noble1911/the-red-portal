#ifdef _DEBUG
#include "lua/modules/dev/hub.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <sol/sol.hpp>

#include "interfac.h"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lua/metadoc.hpp"
#include "player.h"
#include "runs_hub.h"
#include "runs_mod.h"
#include "utils/str_cat.hpp"

namespace devilution {
namespace {

// [RUNS MOD] Rearranging the hub from inside the game: walk somewhere, name what goes
// there. Each change is written straight to the pref-path copy of hub_layout.tsv, which
// overrides the shipped one; dev.hub.reload() re-enters town to show the result.

std::string SpotNames()
{
	std::string names;
	for (int i = 0; i < NumHubSpots; i++)
		names += StrCat(i == 0 ? "" : ", ", HubSpotId(static_cast<HubSpot>(i)));
	return names;
}

std::string DebugCmdHubList()
{
	std::string ret = "Hub layout (x, y):";
	for (int i = 0; i < NumHubSpots; i++) {
		const auto spot = static_cast<HubSpot>(i);
		const Point position = HubPosition(spot);
		ret += StrCat("\n  ", HubSpotId(spot), ": ", position.x, ", ", position.y);
	}
	const Point here = MyPlayer->position.tile;
	ret += StrCat("\nYou are standing on ", here.x, ", ", here.y, ".");
	return ret;
}

std::string DebugCmdHubPlace(std::string_view name)
{
	const std::optional<HubSpot> spot = HubSpotFromId(name);
	if (!spot)
		return StrCat("Unknown spot '", name, "'. One of: ", SpotNames());
	if (setlevel || !MyPlayer->isOnLevel(0))
		return "Stand in the hub to place things.";

	const Point here = MyPlayer->position.tile;
	if (!IsInsideHub(here.x, here.y) || IsTileSolid(here))
		return StrCat(here.x, ", ", here.y, " is not open floor inside the hub.");

	const Point previous = HubPosition(*spot);
	SetHubPosition(*spot, here);
	if (const std::string problem = HubLayoutProblem(); !problem.empty()) {
		SetHubPosition(*spot, previous);
		return StrCat("Not moved: ", problem, ".");
	}
	const std::string path = SaveHubLayout();
	if (path.empty())
		return "Moved for this session, but the layout file could not be written.";
	return StrCat(name, " -> ", here.x, ", ", here.y, ". Saved to ", path, ". dev.hub.reload() to see it.");
}

std::string DebugCmdHubReset(std::string_view name)
{
	const std::optional<HubSpot> spot = HubSpotFromId(name);
	if (!spot)
		return StrCat("Unknown spot '", name, "'. One of: ", SpotNames());
	const Point previous = HubPosition(*spot);
	const Point fallback = HubDefaultPosition(*spot);
	SetHubPosition(*spot, fallback);
	if (const std::string problem = HubLayoutProblem(); !problem.empty()) {
		SetHubPosition(*spot, previous);
		return StrCat("Not reset: ", problem, ".");
	}
	SaveHubLayout();
	return StrCat(name, " back to ", fallback.x, ", ", fallback.y, ". dev.hub.reload() to see it.");
}

std::string DebugCmdHubReload()
{
	if (setlevel || !MyPlayer->isOnLevel(0))
		return "Only from the hub.";
	ReloadHubLayout();
	StartNewLvl(*MyPlayer, interface_mode::WM_DIABNEXTLVL, 0);
	return "Re-entering the hub.";
}

} // namespace

sol::table LuaDevHubModule(sol::state_view &lua)
{
	sol::table table = lua.create_table();
	LuaSetDocFn(table, "list", "()", "[RUNS MOD] Show where every hub spot is.", &DebugCmdHubList);
	LuaSetDocFn(table, "place", "(name: string)", "[RUNS MOD] Move a hub spot (griswold, spawn2, dungeon_portal, ...) to where you stand, and save.", &DebugCmdHubPlace);
	LuaSetDocFn(table, "reset", "(name: string)", "[RUNS MOD] Put a hub spot back where it started, and save.", &DebugCmdHubReset);
	LuaSetDocFn(table, "reload", "()", "[RUNS MOD] Re-enter the hub to see layout changes.", &DebugCmdHubReload);
	return table;
}

} // namespace devilution
#endif // _DEBUG

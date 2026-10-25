/**
 * @file runs_hub.cpp
 *
 * [RUNS MOD] Implementation of the hub layout. See runs_hub.h.
 */
#include "runs_hub.h"

#include <array>
#include <cstdio>
#include <string>

#include "data/file.hpp"
#include "data/record_reader.hpp"
#include "portal.h"
#include "utils/file_util.h"
#include "utils/log.hpp"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

constexpr char LayoutAssetPath[] = "txtdata\\runs\\hub_layout.tsv";

struct SpotInfo {
	std::string_view id;
	Point fallback;
};

// Ids are the tsv's and the dev.hub commands' names; defaults are the layout the hub
// shipped with before it moved to data.
constexpr std::array<SpotInfo, NumHubSpots> Spots { {
    { "griswold", { 22, 34 } },
    { "pepin", { 28, 34 } },
    { "adria", { 20, 37 } },
    { "cain", { 30, 37 } },
    { "stash", { 25, 37 } },
    { "farnham", { 20, 32 } },
    { "merchant", { 30, 33 } },
    { "dungeon_portal", { 27, 32 } },
    { "town_portal", { 30, 31 } },
    { "spawn1", { 25, 32 } },
    { "spawn2", { 23, 32 } },
    { "spawn3", { 25, 34 } },
    { "spawn4", { 23, 34 } },
} };

std::array<Point, NumHubSpots> Layout;
bool LayoutLoaded = false;

void LoadLayout()
{
	for (int i = 0; i < NumHubSpots; i++)
		Layout[i] = Spots[i].fallback;
	LayoutLoaded = true;

	tl::expected<DataFile, DataFile::Error> dataFile = DataFile::load(LayoutAssetPath);
	if (!dataFile.has_value()) {
		LogWarn("{} not found; the hub uses its built-in layout", LayoutAssetPath);
		return;
	}
	if (!dataFile->skipHeader().has_value())
		return;
	for (DataFileRecord record : *dataFile) {
		RecordReader reader { record, LayoutAssetPath };
		std::string id;
		int x = 0;
		int y = 0;
		reader.readString("id", id);
		reader.readInt("x", x);
		reader.readInt("y", y);
		const std::optional<HubSpot> spot = HubSpotFromId(id);
		if (!spot) {
			LogWarn("{}: unknown hub spot '{}' ignored", LayoutAssetPath, id);
			continue;
		}
		Layout[static_cast<size_t>(*spot)] = { x, y };
	}

	if (const std::string problem = HubLayoutProblem(); !problem.empty()) {
		LogError("{}: {}; falling back to the built-in layout", LayoutAssetPath, problem);
		for (int i = 0; i < NumHubSpots; i++)
			Layout[i] = Spots[i].fallback;
	}
}

} // namespace

Point HubPosition(HubSpot spot)
{
	if (!LayoutLoaded)
		LoadLayout();
	return Layout[static_cast<size_t>(spot)];
}

Point HubSpawnPosition(size_t playerId)
{
	return HubPosition(static_cast<HubSpot>(static_cast<size_t>(HubSpot::Spawn1) + playerId % NumHubSpawns));
}

std::string_view HubSpotId(HubSpot spot)
{
	return Spots[static_cast<size_t>(spot)].id;
}

std::optional<HubSpot> HubSpotFromId(std::string_view id)
{
	for (int i = 0; i < NumHubSpots; i++) {
		if (Spots[i].id == id)
			return static_cast<HubSpot>(i);
	}
	return std::nullopt;
}

Point HubDefaultPosition(HubSpot spot)
{
	return Spots[static_cast<size_t>(spot)].fallback;
}

void SetHubPosition(HubSpot spot, Point position)
{
	if (!LayoutLoaded)
		LoadLayout();
	Layout[static_cast<size_t>(spot)] = position;
}

std::string HubLayoutProblem()
{
	if (!LayoutLoaded)
		LoadLayout(); // Marks itself loaded before validating, so this cannot recurse.
	const Point dungeonPortal = Layout[static_cast<size_t>(HubSpot::DungeonPortal)];
	const Point townPortal = Layout[static_cast<size_t>(HubSpot::TownPortal)];
	for (int i = 0; i < MAXPORTAL; i++) {
		const Point portal = townPortal + Displacement { i * 2, 0 };
		if (portal == dungeonPortal)
			return StrCat("player ", i + 1, "'s town portal sits on the dungeon portal");
		if (portal + Displacement { 1, 1 } == dungeonPortal)
			return StrCat("leaving player ", i + 1, "'s town portal lands on the dungeon portal");
	}
	for (int i = 0; i < NumHubSpawns; i++) {
		if (Layout[static_cast<size_t>(HubSpot::Spawn1) + i] == dungeonPortal)
			return StrCat("spawn", i + 1, " sits on the dungeon portal");
	}
	return {};
}

std::string SaveHubLayout()
{
	if (!LayoutLoaded)
		LoadLayout();
	const std::string directory = StrCat(paths::PrefPath(), "txtdata", DIRECTORY_SEPARATOR_STR, "runs");
	RecursivelyCreateDir(directory.c_str());
	const std::string path = StrCat(directory, DIRECTORY_SEPARATOR_STR, "hub_layout.tsv");
	FILE *file = OpenFile(path.c_str(), "wb");
	if (file == nullptr) {
		LogError("Could not write {}", path);
		return {};
	}
	// CRLF, matching the shipped txtdata files.
	std::string text = "id\tx\ty\r\n";
	for (int i = 0; i < NumHubSpots; i++)
		text += StrCat(Spots[i].id, "\t", Layout[i].x, "\t", Layout[i].y, "\r\n");
	std::fwrite(text.data(), 1, text.size(), file);
	std::fclose(file);
	return path;
}

void ReloadHubLayout()
{
	LayoutLoaded = false;
}

} // namespace devilution

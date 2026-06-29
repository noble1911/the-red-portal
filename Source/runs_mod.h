#pragma once

namespace devilution {

// Roguelike "dungeon runs" total-conversion mod.
//
// Master switch for run-based mode: Tristram becomes a streamlined hub and the
// linear quest campaign (quests + the fixed cathedral stairs descent) is turned
// off. The random dungeon-run loop and its entry portal are layered on in later
// milestones (M2+).
//
// Compile-time constant for now (zero cost -- the disabled campaign branches fold
// away). May be promoted to a runtime option later.
constexpr bool RunsModEnabled = true;

// Compact hub (M1): Tristram is cropped to a small plaza around the cathedral run
// portal. Bounds below are in world-tile coordinates; everything outside is voided
// out so the playable hub is just this corner. These are deliberately easy to tune.
constexpr int HubMinX = 10;
constexpr int HubMaxX = 47;
constexpr int HubMinY = 10;
constexpr int HubMaxY = 47;

// Where the player spawns when starting a new game in the compact hub.
constexpr int HubSpawnX = 25;
constexpr int HubSpawnY = 32;

constexpr bool IsInsideHub(int x, int y)
{
	return x >= HubMinX && x <= HubMaxX && y >= HubMinY && y <= HubMaxY;
}

// The dungeon level used for a run (M2/M3). A run is a single contained level;
// kept at 1 (Cathedral) for now. M4 may vary it for depth.
constexpr int RunLevel = 1;

// Begin a fresh run from the hub. In multiplayer, if another player is already in
// the run it joins their layout instead of reseeding; otherwise it generates a new
// seed, applies it, and broadcasts CMD_RUNSEED to the other clients. Defined in
// diablo.cpp.
void StartNewRun();

// Apply a run seed received from another client (CMD_RUNSEED): set the run level's
// seed and clear its cached generation state so it regenerates that exact layout.
void ApplyRunSeed(uint32_t seed);

} // namespace devilution

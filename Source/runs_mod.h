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

// A run is a single contained dungeon level. Successive runs go progressively
// deeper (RunDepth grows), which scales monster difficulty and loot for free via
// the engine. Capped below the Diablo arena (level 16) to avoid its special-case
// generation/monster handling.
constexpr int MaxRunDepth = 15;

// The dungeon level of the current run (1..MaxRunDepth). Defined in diablo.cpp.
extern int RunDepth;

// True once the current run's boss has been killed. Progression to a deeper run
// is gated on this: you only advance if you actually beat the run. Reset when a
// new run starts. Defined in diablo.cpp.
extern bool RunBossDefeated;

// Advance to the next (deeper) run. Called on return to the hub only when the
// run's boss was defeated.
void AdvanceRunDepth();

// Begin a fresh run from the hub. In multiplayer, if another player is already in
// the run it joins their layout instead of reseeding; otherwise it generates a new
// seed for the current RunDepth, applies it, and broadcasts CMD_RUNSEED (seed +
// depth) to the other clients. Defined in diablo.cpp.
void StartNewRun();

// Apply a run seed received from another client (CMD_RUNSEED): adopt the run depth
// and set that level's seed, clearing its cached generation state so it regenerates
// the exact same layout.
void ApplyRunSeed(int depth, uint32_t seed);

} // namespace devilution

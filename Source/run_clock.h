/**
 * @file run_clock.h
 *
 * [RUNS MOD] The push-your-luck clock for dungeon runs. Two pressures:
 *  - Escalation: after a grace period, monster waves spawn near a player every
 *    interval while anyone is on the run level, growing in size. The clock also
 *    festers while the portal stands open with nobody inside -- waves keep growing
 *    (without spawning), so town trips return to a hotter dungeon.
 *  - Collapse: killing the run boss arms a countdown; at zero the hub portal seals
 *    for good (no re-entry, corpse loot lost) and waves shift into overtime pace
 *    while anyone lingers, toughening the longer they stay, with every other wave
 *    rising from the exit stairs. The stairs always work -- sealed never means
 *    trapped, but leaving late can mean fighting through to them.
 * The Volatile scroll affix accelerates the whole clock.
 *
 * All state is session-only (like the dungeon-portal state itself) and all timing is
 * driven by the deterministic GameLogic tick, never the wall clock. Spawns go through
 * SpawnMonster, which is level-owner gated and broadcast, so multiplayer stays in sync.
 */
#pragma once

#include "engine/surface.hpp"

namespace devilution {

/** @brief True once the run boss has died and the collapse countdown is running. */
extern bool RunCollapseArmed;
/** @brief Ticks until the portal seals (0 while in post-seal overtime). */
extern int RunCollapseTicksLeft;

/** @brief Advances the clock one game tick: escalation waves, collapse countdown, seal. */
void ProcessRunClock();

/** @brief Draws the collapse countdown / overtime banner. Call from the HUD overlay. */
void DrawRunClock(const Surface &out);

/** @brief Arms the collapse countdown (call when the run boss dies, on every client). */
void ArmRunCollapse();

/** @brief Clears all clock state (new run primed, or portal manually closed). */
void ResetRunClock();

} // namespace devilution

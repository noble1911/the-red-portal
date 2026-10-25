/**
 * @file trigs.h
 *
 * Interface of functionality for triggering events when the player enters an area.
 */
#pragma once

#include <optional>

#include "engine/point.hpp"
#include "interfac.h"
#include "levels/gendung.h"

namespace devilution {

#define MAXTRIGGERS 7

struct TriggerStruct {
	WorldTilePosition position;
	interface_mode _tmsg;
	int _tlvl;
};

extern bool trigflag;
extern int numtrigs;
extern TriggerStruct trigs[MAXTRIGGERS];
extern int TWarpFrom;

void InitNoTriggers();
bool IsWarpOpen(dungeon_type type);
void InitTownTriggers();
void InitL1Triggers();
void InitL2Triggers();
void InitL3Triggers();
void InitL4Triggers();
void InitHiveTriggers();
void InitCryptTriggers();
void InitSKingTriggers();
void InitSChambTriggers();
void InitPWaterTriggers();
void InitVPTriggers();
void Freeupstairs();

/** @brief [RUNS MOD] Where players arrive on the current run level; empty off run rules. */
extern std::optional<Point> RunArrivalPosition;

/**
 * @brief [RUNS MOD] Picks the run level's arrival point, far from its exit staircase.
 *
 * Under RunDungeonRules the only staircase is the exit, so arriving at it (vanilla's
 * ENTRY_MAIN spot) would let a run be abandoned in one step. Players instead arrive on
 * open floor in the farthest stretch of the level by walking distance, picked from the
 * level's synced seed so co-op peers arrive together. Call after the level's triggers
 * exist; sets ViewPosition for ENTRY_MAIN.
 */
void PlaceRunArrival(lvl_entry entry);
void CheckTrigForce();
void CheckTriggers();

/**
 * @brief Check if the provided position is in the entrance boundary of the entrance.
 * @param entrance The entrance to check.
 * @param position The position to check against the entrance boundary.
 */
bool EntranceBoundaryContains(Point entrance, Point position);

} // namespace devilution

/**
 * @file portal.h
 *
 * Interface of functionality for handling town portals.
 */
#pragma once

#include <cstdint>

#include "engine/point.hpp"
#include "levels/gendung.h"

namespace devilution {

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

#define MAXPORTAL 4

struct Portal {
	bool open;
	Point position;
	int level;
	dungeon_type ltype;
	bool setlvl;
};

extern Portal Portals[MAXPORTAL];

// [RUNS MOD] The dungeon-scroll portal: a single persistent hub portal with its own
// visual (a RedPortal missile) and entry trigger, fully independent of the per-player
// town portals, so casting a town portal never disturbs it.
extern bool DungeonPortalOpen;
extern int DungeonPortalLevel;
extern Point DungeonPortalPos;
// [RUNS MOD] The seed + modifier count of the scroll that opened this portal, so the
// run can recompute the scroll's exact modifiers (kept alongside the layout seed).
extern uint32_t DungeonPortalSeed;
extern int DungeonPortalModCount;
// [RUNS MOD] The difficulty (0 Normal / 1 Nightmare / 2 Hell) of the scroll that opened
// this portal; drives the engine difficulty while on the run level.
extern int DungeonPortalDifficulty;

void InitPortals();
void SetPortalStats(int i, bool o, Point position, int lvl, dungeon_type lvltype, bool isSetLevel);
void AddPortalMissile(const Player &player, Point position, bool sync);
void SyncPortals();
void AddPortalInTown(const Player &player);
void ActivatePortal(const Player &player, Point position, int lvl, dungeon_type lvltype, bool sp);
void DeactivatePortal(const Player &player);
/** @brief [RUNS MOD] Open the hub dungeon-scroll portal to a run at `level` (reseeds it).
 * @param scrollSeed the opening scroll's item seed, used to derive the run's modifiers.
 * @param modCount the opening scroll's modifier count.
 * @param difficulty the opening scroll's difficulty (0 Normal / 1 Nightmare / 2 Hell). */
void OpenDungeonPortal(int level, uint32_t scrollSeed, int modCount, int difficulty);
/** @brief [RUNS MOD] Close the hub dungeon-scroll portal and remove its visual. */
void CloseDungeonPortal();
/** @brief [RUNS MOD] Adopt a co-op peer's portal state (no reseed/rebroadcast). */
void SetDungeonPortalRemote(int level, uint32_t scrollSeed, int modCount, int difficulty);
/** @brief [RUNS MOD] Re-spawn the dungeon-portal visual on hub load if it is open. */
void SyncDungeonPortal();
bool PortalOnLevel(const Player &player);
void RemovePortalMissile(const Player &player);
void SetCurrentPortal(size_t p);
void GetPortalLevel();
void GetPortalLvlPos();
bool PosOkPortal(int lvl, Point position);

} // namespace devilution

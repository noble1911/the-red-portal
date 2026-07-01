/**
 * @file portal.cpp
 *
 * Implementation of functionality for handling town portals.
 */
#include "portal.h"

#include "lighting.h"
#include "missiles.h"
#include "msg.h"
#include "multi.h"
#include "player.h"
#include "runs_mod.h"
#include "tables/misdat.h"

namespace devilution {

/** In-game state of portals. */
Portal Portals[MAXPORTAL];

namespace {

/** Current portal number (a portal array index). */
size_t portalindex;

/** Coordinate of each player's portal in town. */
Point PortalTownPosition[MAXPORTAL] = {
	{ 57, 40 },
	{ 59, 40 },
	{ 61, 40 },
	{ 63, 40 },
};

/** [RUNS MOD] Town portals land in the compact hub near the vendors (the vanilla
 * spot is in the cropped-out void). */
Point GetPortalTownPos(size_t index)
{
	if (RunsModEnabled)
		return Point { HubPortalX + static_cast<int>(index) * 2, HubPortalY };
	return PortalTownPosition[index];
}

} // namespace

void InitPortals()
{
	for (auto &portal : Portals) {
		portal.open = false;
	}
}

void SetPortalStats(int i, bool o, Point position, int lvl, dungeon_type lvltype, bool isSetLevel)
{
	Portals[i].open = o;
	Portals[i].position = position;
	Portals[i].level = lvl;
	Portals[i].ltype = lvltype;
	Portals[i].setlvl = isSetLevel;
}

void AddPortalMissile(const Player &player, Point position, bool sync)
{
	auto *missile = AddMissile({ 0, 0 }, position, Direction::South, MissileID::TownPortal, TARGET_MONSTERS, player, 0, 0, /*parent=*/nullptr, SfxID::None);
	if (missile != nullptr) {
		// Don't show portal opening animation if we sync existing portals
		if (sync)
			missile->setFrameGroup<PortalFrame>(PortalFrame::Idle);

		if (leveltype != DTYPE_TOWN)
			missile->_mlid = AddLight(missile->position.tile, 15);
	}
}

void SyncPortals()
{
	for (int i = 0; i < MAXPORTAL; i++) {
		if (!Portals[i].open)
			continue;
		const Player &player = Players[i];
		if (leveltype == DTYPE_TOWN)
			AddPortalMissile(player, GetPortalTownPos(i), true);
		else {
			int lvl = currlevel;
			if (setlevel)
				lvl = setlvlnum;
			if (Portals[i].level == lvl && Portals[i].setlvl == setlevel)
				AddPortalMissile(player, Portals[i].position, true);
		}
	}
}

void AddPortalInTown(const Player &player)
{
	AddPortalMissile(player, GetPortalTownPos(player.getId()), false);
}

void ActivatePortal(const Player &player, Point position, int lvl, dungeon_type dungeonType, bool isSetLevel)
{
	Portal &portal = Portals[player.getId()];
	portal.open = true;

	if (lvl != 0) {
		portal.position = position;
		portal.level = lvl;
		portal.ltype = dungeonType;
		portal.setlvl = isSetLevel;
	}
}

// [RUNS MOD] --- Dungeon-scroll portal (independent of town portals) ---

bool DungeonPortalOpen = false;
int DungeonPortalLevel = 1;
Point DungeonPortalPos = { 0, 0 };
uint32_t DungeonPortalSeed = 0;
int DungeonPortalModCount = 1;

void CloseDungeonPortal()
{
	DungeonPortalOpen = false;
	Missiles.remove_if([](Missile &missile) {
		if (missile._mitype == MissileID::RedPortal && missile.position.tile == DungeonPortalPos) {
			dFlags[missile.position.tile.x][missile.position.tile.y] &= ~DungeonFlag::Missile;
			AddUnLight(missile._mlid);
			return true;
		}
		return false;
	});
}

void SetDungeonPortalRemote(int level, uint32_t scrollSeed, int modCount)
{
	// Set/replace the hub dungeon portal's state locally, including the RedPortal visual.
	// Used both by the initiator and by co-op peers receiving CMD_OPENDUNGEONPORTAL; it
	// does NOT reseed the layout (that rides on CMD_RUNSEED) or rebroadcast.
	CloseDungeonPortal();
	DungeonPortalOpen = true;
	DungeonPortalLevel = level;
	DungeonPortalSeed = scrollSeed;
	DungeonPortalModCount = modCount;
	DungeonPortalPos = { HubDungeonPortalX, HubDungeonPortalY };
	if (leveltype == DTYPE_TOWN)
		AddMissile(DungeonPortalPos, DungeonPortalPos, Direction::South, MissileID::RedPortal, TARGET_MONSTERS, *MyPlayer, 0, 0);
}

void OpenDungeonPortal(int level, uint32_t scrollSeed, int modCount)
{
	// Open the hub dungeon portal to a run at `level`, reseeding the run once (new
	// layout). The dungeon then persists/resumes until the portal is replaced or
	// closed. `scrollSeed`/`modCount` are remembered so the run derives this scroll's
	// exact modifiers, and are broadcast so co-op peers build the same modified run.
	SetDungeonPortalRemote(level, scrollSeed, modCount);
	PrimeScrollRun(level);
	if (gbIsMultiplayer)
		NetSendCmdParam4(true, CMD_OPENDUNGEONPORTAL, static_cast<uint16_t>(level),
		    static_cast<uint16_t>(scrollSeed >> 16), static_cast<uint16_t>(scrollSeed & 0xFFFF),
		    static_cast<uint16_t>(modCount));
}

void SyncDungeonPortal()
{
	if (DungeonPortalOpen && leveltype == DTYPE_TOWN)
		AddMissile(DungeonPortalPos, DungeonPortalPos, Direction::South, MissileID::RedPortal, TARGET_MONSTERS, *MyPlayer, 0, 0);
}

void DeactivatePortal(const Player &player)
{
	Portals[player.getId()].open = false;
}

bool PortalOnLevel(const Player &player)
{
	const Portal &portal = Portals[player.getId()];
	if (portal.setlvl == setlevel && portal.level == (setlevel ? static_cast<int>(setlvlnum) : currlevel))
		return true;

	return leveltype == DTYPE_TOWN;
}

void RemovePortalMissile(const Player &player)
{
	const size_t id = player.getId();
	Missiles.remove_if([id](Missile &missile) {
		if (missile._mitype == MissileID::TownPortal && missile._misource == static_cast<int>(id)) {
			dFlags[missile.position.tile.x][missile.position.tile.y] &= ~DungeonFlag::Missile;

			if (Portals[id].level != 0)
				AddUnLight(missile._mlid);

			return true;
		}
		return false;
	});
}

void SetCurrentPortal(size_t p)
{
	portalindex = p;
}

void GetPortalLevel()
{
	if (leveltype != DTYPE_TOWN) {
		setlevel = false;
		currlevel = 0;
		MyPlayer->setLevel(0);
		leveltype = DTYPE_TOWN;
		return;
	}

	if (Portals[portalindex].setlvl) {
		setlevel = true;
		setlvlnum = (_setlevels)Portals[portalindex].level;
		currlevel = Portals[portalindex].level;
		MyPlayer->setLevel(setlvlnum);
		setlvltype = leveltype = Portals[portalindex].ltype;
	} else {
		setlevel = false;
		currlevel = Portals[portalindex].level;
		MyPlayer->setLevel(currlevel);
		leveltype = Portals[portalindex].ltype;
	}

	if (portalindex == MyPlayerId) {
		NetSendCmd(true, CMD_DEACTIVATEPORTAL);
		DeactivatePortal(*MyPlayer);
	}
}

void GetPortalLvlPos()
{
	if (leveltype == DTYPE_TOWN) {
		ViewPosition = GetPortalTownPos(portalindex) + Displacement { 1, 1 };
	} else {
		ViewPosition = Portals[portalindex].position;

		if (portalindex != MyPlayerId) {
			ViewPosition.x++;
			ViewPosition.y++;
		}
	}
}

bool PosOkPortal(int lvl, Point position)
{
	for (auto &portal : Portals) {
		if (portal.open
		    && portal.level == lvl
		    && ((portal.position == position)
		        || (portal.position == position - Displacement { 1, 1 })))
			return true;
	}
	return false;
}

} // namespace devilution

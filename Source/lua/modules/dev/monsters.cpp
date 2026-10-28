#ifdef _DEBUG
#include "lua/modules/dev/monsters.hpp"

#include <optional>
#include <string>

#include <sol/sol.hpp>

#include "crawl.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "lua/metadoc.hpp"
#include "monster.h"
#include "player.h"
#include "tables/monstdat.h"
#include "utils/str_case.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace {

std::string DebugCmdSpawnUniqueMonster(std::string name, std::optional<unsigned> countOpt)
{
	if (leveltype == DTYPE_TOWN) return "Can't spawn monsters in town";
	if (name.empty()) return "name is required";
	const unsigned count = countOpt.value_or(1);
	if (count < 1) return "count must be positive";

	AsciiStrToLower(name);

	int mtype = -1;
	UniqueMonsterType uniqueIndex = UniqueMonsterType::None;
	for (size_t i = 0; i < UniqueMonstersData.size(); ++i) {
		const auto &mondata = UniqueMonstersData[i];
		const std::string monsterName = AsciiStrToLower(std::string_view(mondata.mName));
		if (monsterName.find(name) == std::string::npos)
			continue;
		mtype = mondata.mtype;
		uniqueIndex = static_cast<UniqueMonsterType>(i);
		if (monsterName == name) // to support partial name matching but always choose the correct monster if full name is given
			break;
	}

	if (mtype == -1) return "Monster not found";

	size_t id = MaxLvlMTypes - 1;
	bool found = false;

	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (LevelMonsterTypes[i].type == mtype) {
			id = i;
			found = true;
			break;
		}
	}

	if (!found) {
		if (LevelMonsterTypeCount == MaxLvlMTypes)
			LevelMonsterTypeCount--; // we are running out of monster types, so override last used monster type
		tl::expected<size_t, std::string> idResult = AddMonsterType(uniqueIndex, PLACE_SCATTER);
		if (!idResult.has_value()) return std::move(idResult).error();
		id = idResult.value();
		CMonster &monsterType = LevelMonsterTypes[id];
		InitMonsterGFX(monsterType);
		monsterType.corpseId = 1;
	}

	Player &myPlayer = *MyPlayer;

	unsigned spawnedMonster = 0;

	auto ret = Crawl(0, MaxCrawlRadius, [&](Displacement displacement) -> std::optional<std::string> {
		Point pos = myPlayer.position.tile + displacement;
		if (dPlayer[pos.x][pos.y] != 0 || dMonster[pos.x][pos.y] != 0)
			return {};
		if (!IsTileWalkable(pos))
			return {};

		Monster *monster = AddMonster(pos, myPlayer._pdir, id, true);
		if (monster == nullptr)
			return StrCat("Spawned ", spawnedMonster, " monsters. (Unable to spawn more)");
		PrepareUniqueMonst(*monster, uniqueIndex, 0, 0, UniqueMonstersData[static_cast<size_t>(uniqueIndex)]);
		monster->corpseId = 1;
		spawnedMonster += 1;

		if (spawnedMonster >= count)
			return StrCat("Spawned ", spawnedMonster, " monsters.");

		return {};
	});

	if (!ret.has_value())
		ret = StrCat("Spawned ", spawnedMonster, " monsters. (Unable to spawn more)");
	return *ret;
}

std::string DebugCmdSpawnMonster(std::string name, std::optional<unsigned> countOpt)
{
	if (leveltype == DTYPE_TOWN) return "Can't spawn monsters in town";
	if (name.empty()) return "name is required";
	const unsigned count = countOpt.value_or(1);
	if (count < 1) return "count must be positive";

	AsciiStrToLower(name);

	int mtype = -1;

	for (size_t i = 0; i < MonstersData.size(); i++) {
		const auto &mondata = MonstersData[i];
		const std::string monsterName = AsciiStrToLower(std::string_view(mondata.name));
		if (monsterName.find(name) == std::string::npos)
			continue;
		mtype = static_cast<int>(i);
		if (monsterName == name) // to support partial name matching but always choose the correct monster if full name is given
			break;
	}

	if (mtype == -1) return "Monster not found";
	if (!MyPlayer->isLevelOwnedByLocalClient()) return "You are not the level owner.";

	size_t id = MaxLvlMTypes - 1;
	bool found = false;

	for (size_t i = 0; i < LevelMonsterTypeCount; i++) {
		if (LevelMonsterTypes[i].type == mtype) {
			id = i;
			found = true;
			break;
		}
	}

	if (!found) {
		if (LevelMonsterTypeCount == MaxLvlMTypes)
			LevelMonsterTypeCount--; // we are running out of monster types, so override last used monster type
		tl::expected<size_t, std::string> idResult = AddMonsterType(static_cast<_monster_id>(mtype), PLACE_SCATTER);
		if (!idResult.has_value()) return std::move(idResult).error();
		id = idResult.value();
		CMonster &monsterType = LevelMonsterTypes[id];
		InitMonsterGFX(monsterType);
		monsterType.corpseId = 1;
	}

	Player &myPlayer = *MyPlayer;

	size_t monstersToSpawn = std::min<size_t>(MaxMonsters - ActiveMonsterCount, count);
	if (monstersToSpawn == 0)
		return "Can't spawn any monsters";

	size_t spawnedMonster = 0;
	Crawl(0, MaxCrawlRadius, [&](Displacement displacement) {
		Point pos = myPlayer.position.tile + displacement;
		if (dPlayer[pos.x][pos.y] != 0 || dMonster[pos.x][pos.y] != 0)
			return false;
		if (!IsTileWalkable(pos))
			return false;

		SpawnMonster(pos, myPlayer._pdir, id);
		spawnedMonster += 1;

		return spawnedMonster == monstersToSpawn;
	});

	if (monstersToSpawn != count)
		return StrCat("Spawned ", spawnedMonster, " monsters. (Unable to spawn more)");
	return StrCat("Spawned ", spawnedMonster, " monsters.");
}

/**
 * [RUNS MOD] Diagnostic for the companion: which monsters own map cells, and how many.
 * A monster should own one cell (two while walking); more means stale entries that draw
 * copies of it. Also reports the reserved minion slots.
 */
std::string DebugCmdMonsterCells()
{
	int cellsOf[MaxMonsters] = {};
	int bad = 0;
	for (int x = 0; x < MAXDUNX; x++) {
		for (int y = 0; y < MAXDUNY; y++) {
			const int mi = std::abs(dMonster[x][y]) - 1;
			if (mi < 0)
				continue;
			if (mi >= static_cast<int>(MaxMonsters)) {
				bad++;
				continue;
			}
			cellsOf[mi]++;
		}
	}
	std::string out = StrCat("active ", ActiveMonsterCount, ", bad ids ", bad, ". ");
	for (int i = 0; i < 4; i++) {
		const Monster &m = Monsters[i];
		out += StrCat("slot", i, ": ", m.name(), " mode ", static_cast<int>(m.mode), " hp ", m.hitPoints >> 6,
		    m.isPlayerMinion() ? " minion" : "", " at ", m.position.tile.x, ",", m.position.tile.y, " cells ", cellsOf[i], ". ");
	}
	for (size_t i = 4; i < MaxMonsters; i++) {
		if (cellsOf[i] > 2)
			out += StrCat("#", i, " ", Monsters[i].name(), " owns ", cellsOf[i], " cells. ");
	}
	return out;
}

} // namespace

sol::table LuaDevMonstersModule(sol::state_view &lua)
{
	sol::table table = lua.create_table();
	LuaSetDocFn(table, "spawn", "(name: string, count: number = 1)", "Spawn monster(s)", &DebugCmdSpawnMonster);
	LuaSetDocFn(table, "spawnUnique", "(name: string, count: number = 1)", "Spawn unique monster(s)", &DebugCmdSpawnUniqueMonster);
	LuaSetDocFn(table, "cells", "()", "[RUNS MOD] Report which monsters own map cells (stale-copy diagnostic).", &DebugCmdMonsterCells);
	return table;
}

} // namespace devilution
#endif // _DEBUG

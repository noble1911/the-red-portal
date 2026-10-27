/**
 * @file run_clock.cpp
 *
 * [RUNS MOD] Implementation of the push-your-luck run clock. See run_clock.h.
 */
#include "run_clock.h"

#include <algorithm>
#include <cstdint>

#include <fmt/format.h>

#include "DiabloUI/ui_flags.hpp"
#include "diablo.h"
#include "diablo_msg.hpp"
#include "engine/render/text_render.hpp"
#include "interfac.h"
#include "levels/gendung.h"
#include "levels/trigs.h"
#include "monster.h"
#include "multi.h"
#include "player.h"
#include "portal.h"
#include "runs_mod.h"
#include "scroll_mods.h"
#include "utils/display.h"
#include "utils/language.h"

namespace devilution {

bool RunCollapseArmed = false;
int RunCollapseTicksLeft = 0;

namespace {

// All values in game ticks (sgGameInitInfo.nTickRate per second, default 20).
constexpr int TicksPerSecondDefault = 20;
/** Quiet time on the run level before the dungeon starts pushing back. */
constexpr int GraceSeconds = 300;
/** Interval between escalation waves after the grace period. */
constexpr int WaveIntervalSeconds = 60;
/** Waves grow one monster per wave up to this size. */
constexpr int WaveSizeCap = 4;
/** Countdown from boss death to the portal sealing. */
constexpr int CollapseSeconds = 180;
/** Overtime wave interval once the run has sealed with players still inside. */
constexpr int OvertimeWaveSeconds = 10;
/** Overtime waves toughen one tier per this many seconds lingered (see ApplyRunWaveTier):
 * staying pays at first, then the odds turn and the player has to judge when to leave. */
constexpr int OvertimeTierSeconds = 20;

/** Ticks accrued by the run (drives grace + wave pacing). Runs while anyone is on
 * the run level OR while the portal stands open (fester), so leaving to restock
 * never pauses the dungeon's escalation. */
int RunClockTicks = 0;
int WavesSpawned = 0;
/** Overtime waves spawned so far; odd ones come up from the exit stairs. */
int OvertimeWaves = 0;
/** Ticks spent in overtime on the run level (drives the overtime tier). */
int OvertimeTicks = 0;
bool Warned30 = false;
bool Warned10 = false;

int TicksPerSecond()
{
	return sgGameInitInfo.nTickRate > 0 ? sgGameInitInfo.nTickRate : TicksPerSecondDefault;
}

/** [RUNS MOD] The Volatile affix compresses every clock duration by its bane percent. */
int ScaledTicks(int seconds)
{
	const int base = seconds * TicksPerSecond();
	const int pct = ActiveRunMods.collapsePct;
	if (pct <= 0)
		return base;
	return std::max(base * 100 / (100 + pct), TicksPerSecond());
}

/** True while the local simulation is on the run's dungeon level. Uses RunDepth (not
 * DungeonPortalOpen) so overtime pressure keeps running after the portal seals. */
bool OnRunLevel()
{
	return RunsModEnabled && leveltype != DTYPE_TOWN && currlevel == RunDepth;
}

/** A deterministic per-tick stream, identical on every client by construction
 * (seeded from synced portal state + the shared tick counter); never touches the
 * game RNG, so clients on other levels are unaffected. */
uint32_t WaveRngNext(uint32_t &state)
{
	state = state * 1103515245u + 12345u;
	return state >> 8;
}

/** Spawns `size` wave monsters around a player on the run level -- or, with
 * `atExit`, around the exit staircase, so camping the stairs is no free escape. */
void SpawnWave(int size, uint8_t tier = 0, bool atExit = false)
{
	// Deterministic selection: same picks on every client that is on the level; the
	// actual spawn is level-owner gated inside SpawnMonster and broadcast from there.
	uint32_t rng = DungeonPortalSeed ^ (static_cast<uint32_t>(RunClockTicks) * 2654435761u);

	// Pick a target player on the run level (Players order is identical everywhere).
	const Player *target = nullptr;
	int candidates = 0;
	for (const Player &player : Players) {
		if (!player.plractive || !player.isOnActiveLevel())
			continue;
		candidates++;
		if (WaveRngNext(rng) % candidates == 0)
			target = &player;
	}
	if (target == nullptr)
		return;

	// Under RunDungeonRules the level's only staircase is trigs[0] (see PlaceRunArrival).
	const Point around = atExit && RunDungeonRules && numtrigs > 0 ? trigs[0].position : target->position.tile;
	for (int i = 0; i < size; i++)
		SpawnRunWaveMonster(around, WaveRngNext(rng), tier);
}

} // namespace

void ProcessRunClock()
{
	if (!RunsModEnabled)
		return;

	// [RUNS MOD] A save made inside a run loads back into a sealed session (see
	// LoadGame): walk the player out to the hub on the first standing tick. The flag
	// stays set through the level transition and is cleared, with the notice, once
	// the hub loads.
	if (RunForfeitPending) {
		if (leveltype == DTYPE_TOWN) {
			RunForfeitPending = false;
			InitDiabloMsg(_("The dungeon sealed itself while you were away!"));
		} else if (MyPlayer->_pmode == PM_STAND) {
			TWarpFrom = currlevel;
			StartNewLvl(*MyPlayer, WM_DIABTWARPUP, 0);
		}
		return;
	}

	// Collapse countdown runs in real ticks once armed, everywhere (also while players
	// shop in the hub -- the countdown is the window for looting and corpse runs).
	// Each client seals locally when its own countdown expires, and a few ticks of
	// cross-client drift in the arm time is harmless. The ARM itself does have to be
	// broadcast (CMD_ARMRUNCOLLAPSE): MonsterDeath only reaches a client that was on the
	// run level when the boss died, so a partner in the hub would otherwise never start
	// counting and never seal.
	if (RunCollapseArmed && RunCollapseTicksLeft > 0) {
		RunCollapseTicksLeft--;
		const int tps = TicksPerSecond();
		if (!Warned30 && RunCollapseTicksLeft <= 30 * tps) {
			Warned30 = true;
			InitDiabloMsg(_("The dungeon collapses in 30 seconds!"));
		}
		if (!Warned10 && RunCollapseTicksLeft <= 10 * tps) {
			Warned10 = true;
			InitDiabloMsg(_("10 seconds! Get out!"));
		}
		if (RunCollapseTicksLeft == 0 && DungeonPortalOpen) {
			CloseDungeonPortal();
			InitDiabloMsg(_("The dungeon has sealed itself!"));
		}
	}

	// [RUNS MOD] Fester: while the portal stands open the run's clock keeps accruing
	// even with nobody inside -- grace burns down and wave sizes grow, though actual
	// spawns only happen on the run level. Every potion trip to town means a hotter
	// dungeon on return. Portal state is mirrored on every client, so accrual stays
	// identical everywhere.
	const bool onRunLevel = OnRunLevel();
	if (!onRunLevel && !DungeonPortalOpen)
		return;

	RunClockTicks++;

	const bool overtime = RunCollapseArmed && RunCollapseTicksLeft == 0;
	if (overtime) {
		// Post-seal: relentless max-size waves until everyone leaves, each tougher than
		// the last, and every other one rising from the exit stairs. RunClockTicks
		// advances exactly once per tick, so the modulo fires once per interval.
		if (!onRunLevel)
			return;
		OvertimeTicks++;
		if (RunClockTicks % ScaledTicks(OvertimeWaveSeconds) == 0) {
			const int tier = std::min<int>(OvertimeTicks / ScaledTicks(OvertimeTierSeconds), MaxRunWaveTier);
			SpawnWave(WaveSizeCap, static_cast<uint8_t>(tier), OvertimeWaves % 2 == 1);
			OvertimeWaves++;
		}
		return;
	}

	// Baseline escalation: quiet through the grace period, then a growing wave per
	// interval. Waves that come due off-level still grow the count (fester) but only
	// spawn while someone is there to hunt; the first-notice message shows anywhere,
	// warning a lingering player that the dungeon is now awake.
	const int nextWaveTick = ScaledTicks(GraceSeconds) + WavesSpawned * ScaledTicks(WaveIntervalSeconds);
	if (RunClockTicks >= nextWaveTick) {
		WavesSpawned++;
		if (WavesSpawned == 1)
			InitDiabloMsg(_("The dungeon has noticed you..."));
		if (onRunLevel)
			SpawnWave(std::min(WavesSpawned, WaveSizeCap));
	}
}

void DrawRunClock(const Surface &out)
{
	if (!RunsModEnabled || !RunCollapseArmed)
		return;

	if (RunCollapseTicksLeft > 0) {
		const int tps = TicksPerSecond();
		const int totalSeconds = (RunCollapseTicksLeft + tps - 1) / tps;
		const std::string text = fmt::format(fmt::runtime(_("Collapse in {:d}:{:02d}")), totalSeconds / 60, totalSeconds % 60);
		const UiFlags color = totalSeconds <= 30 ? UiFlags::ColorRed : UiFlags::ColorWhitegold;
		DrawString(out, text, { { 0, 40 }, { gnScreenWidth, 24 } },
		    { .flags = color | UiFlags::AlignCenter | UiFlags::Outlined });
	} else if (OnRunLevel()) {
		DrawString(out, _("THE DUNGEON IS COLLAPSING"), { { 0, 40 }, { gnScreenWidth, 24 } },
		    { .flags = UiFlags::ColorRed | UiFlags::AlignCenter | UiFlags::Outlined });
	}
}

void ArmRunCollapse()
{
	if (!RunsModEnabled || RunCollapseArmed)
		return;
	RunCollapseArmed = true;
	RunCollapseTicksLeft = ScaledTicks(CollapseSeconds);
	Warned30 = false;
	Warned10 = false;
	InitDiabloMsg(_("The dungeon begins to collapse!"));
}

void ResetRunClock()
{
	RunClockTicks = 0;
	WavesSpawned = 0;
	OvertimeWaves = 0;
	OvertimeTicks = 0;
	RunCollapseArmed = false;
	RunCollapseTicksLeft = 0;
	Warned30 = false;
	Warned10 = false;
}

} // namespace devilution

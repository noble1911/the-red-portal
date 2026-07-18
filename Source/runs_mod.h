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

// The mod's display name: window title, main menu footer, Switch home menu. Branding is
// presentation-layer only -- PROJECT_NAME/binary names stay "devilutionx" so upstream
// merges stay clean. NOTE: "Diablo" is Blizzard's trademark; while the repo and releases
// are private this is fine, but any PUBLIC distribution should drop it and ship under
// "The Red Portal" alone.
constexpr char RunsModGameName[] = "Diablo: The Red Portal";

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

// Where town portals land in the hub (near the vendors) in run mode -- the vanilla
// Tristram portal spot is outside the cropped plaza. Players are offset along x.
constexpr int HubPortalX = 30;
constexpr int HubPortalY = 31;

// Where the dungeon-scroll portal appears in the hub (run mode). Its own spot so it
// never collides with a town portal. Tunable.
constexpr int HubDungeonPortalX = 33;
constexpr int HubDungeonPortalY = 32;

constexpr bool IsInsideHub(int x, int y)
{
	return x >= HubMinX && x <= HubMaxX && y >= HubMinY && y <= HubMaxY;
}

// A run is a single contained dungeon level. Successive runs go progressively
// deeper (RunDepth grows), which scales monster difficulty and loot for free via
// the engine.
//
// Level 16 is the Diablo arena: its special-case generation and monster handling
// don't suit a run, so it is skipped over entirely rather than capped below.
// Hellfire adds the Nest (17-20) and the Crypt (21-24); GetLevelType() already maps
// those, but their tilesets live in the Hellfire MPQs, so the cap is only raised
// when the Hellfire mod is actually active.
constexpr int DiabloMaxRunDepth = 15;
constexpr int HellfireMaxRunDepth = 24;
constexpr int ArenaRunDepth = 16;

// Depth whose boss unlocks the next difficulty tier. Held at the Diablo cap so the
// tier ladder keeps the pacing it was tuned for even when Hellfire raises the depth
// cap to 24; the Nest and Crypt are then extra depth within a tier rather than a
// longer road to Nightmare.
constexpr int DifficultyUnlockDepth = DiabloMaxRunDepth;

/** @brief Deepest reachable run level: 24 when Hellfire is active, otherwise 15.
 *
 * Safe to branch on in multiplayer. GAME_ID folds gbIsHellfire into
 * GameData::programid, and IsGameCompatible() refuses a join whose programid differs,
 * so every client in a game necessarily agrees on this value and therefore on the
 * whole depth progression. Defined in diablo.cpp.
 */
int GetMaxRunDepth();

/** @brief Clamps to a valid run level: 1..GetMaxRunDepth(), never the arena. */
int ClampRunDepth(int depth);

/** @brief The run level after `depth`: skips the arena, stops at the cap. */
int NextRunDepth(int depth);

// The dungeon level of the current run. Defined in diablo.cpp.
extern int RunDepth;

// True once the current run's boss has been killed. Progression to a deeper run
// is gated on this: you only advance if you actually beat the run. Reset when a
// new run starts. Defined in diablo.cpp.
extern bool RunBossDefeated;

// [RUNS MOD] Set when a dungeon is completed (run boss killed); consumed on the next
// hub load to restock the Dungeon Merchant. Starts true so the vendor stocks on the
// first hub visit. Defined in diablo.cpp.
extern bool DungeonVendorRestockPending;

// Advance to the next (deeper) run. Called on return to the hub only when the
// run's boss was defeated.
void AdvanceRunDepth();

// Prepare a fresh run at `level` for a dungeon-scroll portal: reseed that level
// (new layout) and prime it, WITHOUT teleporting. Called when the scroll is used.
// Defined in diablo.cpp.
void PrimeScrollRun(int level);

// Enter the already-primed scroll-portal run at `level` WITHOUT reseeding, so the
// dungeon persists (resume) while the portal stays open. Defined in diablo.cpp.
void EnterScrollRun(int level);

// While true, entering a run level does not reseed it (used for scroll-portal
// re-entry so the dungeon resumes). Defined in diablo.cpp.
extern bool SuppressRunReseed;

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

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

} // namespace devilution

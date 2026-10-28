#pragma once

#include <string>

#include <expected.hpp>

#include "engine/clx_sprite.hpp"
#include "engine/surface.hpp"

namespace devilution {

extern bool PartySidePanelOpen;
extern bool InspectingFromPartyPanel;
extern int PortraitIdUnderCursor;

tl::expected<void, std::string> LoadPartyPanel();
void FreePartyPanel();
void DrawPartyMemberInfoPanel(const Surface &out);
/** @brief [RUNS MOD] The local hero's minion (companion or Golem): name, life and, for a
 * companion, the time it has left. Top-left, under any party portraits. */
void DrawMinionPanel(const Surface &out);
bool DidRightClickPartyPortrait();

} // namespace devilution

# Diablo: The Red Portal — Mod Plan & Roadmap

The mod: a **roguelike-run total conversion** of Diablo 1 on top of DevilutionX. A compact
hub town replaces Tristram; **dungeon scrolls** open red portals to seeded, self-contained
dungeon runs with scaled bosses and loot. Builds for **Windows and Nintendo Switch** from
one tree; co-op requires all clients on the same build (layouts are deterministic from a
synced seed).

Branding is presentation-layer only ("Diablo: The Red Portal" in window title / menu /
Switch NACP); binary and project stay `devilutionx` so upstream merges stay clean.
Public distribution must drop the "Diablo" mark and ship as **The Red Portal**.

Last updated: **2026-07-18** (v0.3.0).

## Milestones

| Milestone | Status |
|---|---|
| M0 — toolchain, both targets build | ✅ done |
| M1 — compact hub (cropped Tristram, essential vendors) | ✅ done |
| M2 — one contained run with a boss | ✅ done |
| M3 — reseed per run + co-op seed sync (CMD_RUNSEED) | ✅ done (2-console determinism test still pending) |
| M4 — depth/difficulty scaling, loot tuning, polish | 🔶 in progress (scroll system largely done) |

## Systems shipped

- **Hub**: cropped plaza, relocated vendors, stash chest (replaces Gillian), Dungeon
  Merchant (Balrog sprite) selling scrolls, red dungeon portal at the plaza.
- **Dungeon scrolls** define runs on three axes packed in `_iCreateInfo`
  (recreation-safe, synced): level (bits 0-5), rarity = modifier count (bits 6-7:
  Common/Magic/Rare), difficulty (bits 8-9: Normal/Nightmare/Hell = real engine
  difficulty). Scrolls are bought (infinite restock, 4 distinct levels), dropped (boss
  always drops next-level scroll; trash ~3%), and crafted (Reroll / Upgrade / Add-mod at
  the merchant). A starter scroll is granted only when the player has none anywhere and
  no portal is open.
- **Modifiers**: 1-3 seeded affixes per scroll, each a boon paired with an offsetting
  bane — Avaricious (gold), Enchanted (magic find), Bountiful (drops), Erudite (XP),
  Savage (damage); magnitudes scale with scroll level.
- **Bosses**: per-tileset pools (incl. six Nest/Crypt bosses), stats normalized to the
  run level so any boss is level-appropriate; boss kill gates progression, restocks the
  vendor, and (at the deepest level) unlocks the next difficulty tier.
- **Depth**: runs extend through the Hellfire Nest and Crypt (levels 17-24); Hellfire
  ships as always-on mod data.
- **Extended inventory** (2026-07-18, replaced the separate "bag"): the inventory grid
  itself grows from 10x4 to up to 10x11 — 3 extra rows on a fresh character, one more per
  boss kill at depths 2/4/6/8 (max 7 extra). Items can span all rows, gold overflows into
  the extension, stash/vendors/tooltips/controller nav all treat it as one grid. Saves,
  hero files and network packs stay vanilla-shaped (40 cells); extension items live in a
  per-hero side file with migration from old bag saves. Remote players see only the base
  40 cells (extension is local, like the stash).
- **Presentation**: torch flicker, dynamic wall shadows, red-portal home-menu icon,
  no pregame difficulty screen (difficulty comes from scrolls), multiplayer unblocked.

## Releases

- v0.1.0 (2026-07-01), v0.2.0 (2026-07-02), **v0.3.0 (2026-07-18)** — CI builds the
  Windows zip + Switch `.nro` on release publish.

## Open items

**Verify / playtest**
- [ ] Playtest the unified extended inventory (seam placement, gold, stash, vendor sell,
      controller nav on gamepad, old-character bag migration).
- [ ] Two-client co-op determinism test (the M3 make-or-break; needs 2 instances or
      2 consoles).
- [ ] Crafting flow edge cases (slice 10 was never fully gameplay-tested).
- [ ] Real-Switch hardware test (releases are only CI-verified).

**Known gaps**
- [ ] Co-op join-sync: a mid-game joiner doesn't receive dungeon-portal state.
- [ ] Scroll seed/modifier sync for the starter-scroll grant (granted locally without a
      network message).
- [ ] A new scroll can replace an open portal even with players inside (manual close
      only partly covers this).
- [ ] Dungeon portal state is session-only (not saved).

**Feature ideas (next slices)**
- [ ] **Scroll modifier expansion** (PoE-map direction): more affix variety — pack
      density, extra elites, monster speed, elemental-heavy rolls, shrine/chest density,
      boss mods (e.g. twinned) — plus a wider risk/reward magnitude spread and affix
      tiers so crafting rerolls have texture.
- [ ] Loot tuning pass via TSV (M4 remainder).

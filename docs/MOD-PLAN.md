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
- **Presentation**: red-portal home-menu icon, no pregame difficulty screen (difficulty
  comes from scrolls), multiplayer unblocked. (Torch flicker + dynamic wall shadows were
  tried 2026-07-18 and reverted the same day: the flicker read as jarring, and the
  shadows were visually redundant with fog-of-war darkness.)

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

## Gameplay direction: PvE extraction (decided 2026-07-18)

The loop is not a roguelike (no permadeath, persistent character/stash/economy) — it is a
**PvE extraction loop**: gear up in the hub, commit to a sortie, decide how greedy to get,
leave with what you can carry. Design principle: **a run is a heist to survive, not a
level to clear** — the tension lives at the exit. Planned slices, in order:

**E1 — Extraction death** ✅ shipped 2026-07-18 (design revised at sign-off: full
Tarkov, no tagging/no safe items — "anything you bring in gets dropped")
- Dying in a run drops EVERYTHING at the corpse: equipped gear (vanilla), plus the
  whole backpack and belt (new, run-mode only), plus half your gold (vanilla rule;
  the retained half stays retained — gold stacks are excluded from the backpack drop).
- Death is survivable: SP now respawns in the hub exactly like MP (Esc), instead of
  game-over/reload; the outcome is force-saved on town arrival so a reload cannot
  undo the loss (SP never autosaved on level change).
- Corpse runs: the drops lie in the run, which persists behind the portal — go back
  in to recover them; when the portal closes or reseeds they are gone for good.
- Impl: `StartPlayerKill` dropItems branch (player.cpp), SP Esc handler (diablo.cpp),
  WM_DIABRETOWN SaveGame (interfac.cpp). Level item cap (MAXITEMS) overflow silently
  discards excess drops — accepted.

**E2 — Push-your-luck clock** ✅ shipped 2026-07-18
- Baseline on every run: 5 min grace on-level (raised from 2 after playtest), then a
  monster wave spawns near a player every 60 s, growing 1→4; boss death arms a
  **3 min collapse countdown**
  (HUD timer; runs in real ticks even from town — it is the looting/corpse-run
  window). At zero the portal seals itself (no re-entry, corpse loot gone) and waves
  go overtime (every 10 s, max size) while anyone lingers — the stairs still work.
- **Volatile** affix (new): +40-80% item drops, but the whole clock runs 30-60%
  faster.
- Impl: `Source/run_clock.h/.cpp` (deterministic GameLogic-tick driven; session-only
  state like the portal), `SpawnRunWaveMonster` in monster.cpp (level-owner gated via
  SpawnMonster + broadcast; only level-native types spawn), seal = local
  CloseDungeonPortal on countdown expiry per client. Full co-op verification folds
  into the existing deferred 2-client test.

**E3 — Boss-optional runs** ✅ shipped 2026-07-18
- The incentive split is now intentional: **ghost = wealth, boss = progression +
  jackpot**. Boss death drops the VAULT on top of its normal unique item + next-depth
  scroll: 2 + depth/5 good-quality items and 2 + depth/10 gold piles (2 items + 2
  gold at depth 1 → 5 items + 3 gold at 15), delta-persisted so the pile survives
  exit/re-entry during the collapse window.
- Ghost-run economy deliberately unchanged: trash scrolls stay 3%/same-depth; the
  vendor's **Upgrade craft is the ghost path to depth** and was repriced as a real
  gold sink — quadratic in target level (L1→2: 400g … L14→15: 22,500g), so buying
  depth is always pricier than beating the boss for the free scroll.
- **Upgrade is fusion** (follow-up, same day): consumes **three** scrolls, each of
  the target's level or higher (picked one at a time, "1 of 3" prompts) — otherwise
  cheap L1 vendor scrolls made gold the only gate and boss-skipping trivial. Ghosting
  deeper now takes four same-depth scrolls total (target + 3 materials, from 3% trash
  drops or the vendor when your character level keeps pace), while the boss stays the
  fast lane. Reroll/Add-modifier still burn one scroll of any level.
- Vestigial `AdvanceRunDepth` retired (depth is scroll-defined; boss rewards all fire
  at kill time). A physical vault room was rejected: theme rooms don't generate on
  Nest/Crypt/many layouts.
- Playtest context kept: fresh level-1 characters need several ghost sorties before
  the first boss — that loop is now the intended one, and E4 must keep it cheap to
  lose.

**E4 — Death economy tuning**
- E1 shipped the harsh end (full drop). If playtesting shows it is too brutal for
  fresh level-1 characters (who already need several sorties per boss), the levers
  are: a small "secure container" (a few slots whose contents survive death,
  Tarkov-style — could reuse the per-item-tag design that was considered and
  dropped for E1), belt-as-safe-pocket, or an insurance fee at a vendor. Optional
  "hardcore" scroll affix stays an idea for the knife-edge crowd.

**Feature ideas (next slices)**
- [ ] **Scroll modifier expansion** (PoE-map direction): more affix variety — pack
      density, extra elites, monster speed, elemental-heavy rolls, shrine/chest density,
      boss mods (e.g. twinned) — plus a wider risk/reward magnitude spread and affix
      tiers so crafting rerolls have texture. Pairs with E2's risk affixes.
- [ ] Loot tuning pass via TSV (M4 remainder).

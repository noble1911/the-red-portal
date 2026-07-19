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
  difficulty). Scrolls are bought (restocks on boss kill; up to 4 distinct levels, capped
  at the hero's deepest beaten depth + 1), dropped (boss always drops next-level scroll;
  trash ~3%), and crafted (Reroll / Upgrade / Add-mod at the merchant). A starter scroll is granted only when the player has none anywhere and
  no portal is open.
- **Modifiers**: 1-3 seeded affixes per scroll, each a boon paired with an offsetting
  bane, from a 13-affix pool — gold/magic-find/drops/XP boons vs monster stat banes
  (Avaricious, Enchanted, Bountiful, Erudite, Savage, Volatile) plus the 2026-07-19
  expansion wave: Teeming (density), Tyrannical (champions), Frenzied (speed), Hexbound
  (elemental resists), Gilded (chests), Hallowed (shrines), Twinned (double boss). Each
  roll has a tier (I-III, rare-high) scaling boon AND bane; magnitudes also scale with
  scroll level.
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
- **In-game manual** (2026-07-19): Cain's menu gained "Ask about the rules" — a two-page
  static primer (TalkID::StorytellerRules) covering the run loop, clocks, ghost/boss
  split, death rules, crafting, and progression unlocks. Update it when rules change.
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
- [ ] Expansion affixes (2026-07-19): each of the 7 new affixes fires correctly
      (density, champions, speed, resists, chests, shrines, twin boss), tier II/III
      names + longer lines still fit the 288px compact info box, and champion drops
      arrive in co-op.
- [ ] Fruit machine (2026-07-19): stake scaling, every payout row, jackpot
      display/claim/boss-restock reroll, reel timing + sounds, prize floor-drops,
      and gamepad navigation of the gamble screens.
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
- **Vendor stock is progression-gated** (follow-up, 2026-07-19): the merchant now sells
  scrolls only up to the hero's **deepest beaten depth + 1** (`pDeepestBeatenDepth`,
  persisted in the herobag side file v3; pre-v3 saves infer it from unlocked inventory
  rows). Stock previously tracked character level and sold a level-4 scroll for 200g to
  a fresh character, undercutting both the boss lane and the quadratic Upgrade craft.
  Scrolls stay cheap (level x 50) -- they are entry tickets; depth access is the earned
  thing. A fresh character sees one level-1 scroll; the shelf grows to four levels.
- Vestigial `AdvanceRunDepth` retired (depth is scroll-defined; boss rewards all fire
  at kill time). A physical vault room was rejected: theme rooms don't generate on
  Nest/Crypt/many layouts.
- Playtest context kept: fresh level-1 characters need several ghost sorties before
  the first boss — that loop is now the intended one, and E4 must keep it cheap to
  lose.

**E4 — Death economy tuning** 🔶 E4a shipped 2026-07-19
- **E4a — Scroll of Preservation** (shipped): a 50,000-gold consumable, always in
  stock at the Dungeon Merchant. Use it, then click any equipped / backpack / belt
  item: the item is marked (persistent gold outline + "Preserved" tooltip line) and
  survives exactly ONE death — the mark is spent by it. The mark persists across
  saves and dungeons until spent (dwBuff bit, save/pack/net-safe). The scroll is
  consumed only on a successful mark; cancelling the targeting cursor costs nothing.
  Multiple marks may be active (one scroll each). Gold and preservation scrolls
  themselves cannot be marked.
- Remaining E4 levers if full-drop still needs softening after playtest: secure
  container slots, belt-as-safe-pocket, "hardcore" scroll affix for the knife-edge
  crowd.

**Scroll modifier expansion** ✅ shipped 2026-07-19 (PoE-map direction)
- Affix pool doubled 6 → 13: **Teeming** (+XP / more monsters — scales the
  `InitMonsters` placement budget before the MaxMonsters cap), **Tyrannical** (+magic
  find / 10-20% of monsters are champions: +150% HP, +40% dmg, guaranteed good drop on
  death), **Frenzied** (+gold / monsters act faster — anim rate scaled in
  NewMonsterAnim, the funnel all action animations pass through), **Hexbound** (+magic
  find / part of the population gains fire+lightning resistance — persisted per-monster
  field), **Gilded** (+chests / monsters +dmg), **Hallowed** (+shrines / monsters
  +life — both appended at the TAIL of InitObjects' seed stream so vanilla placement
  is untouched), **Twinned** (a second run boss with its own vault + next-depth scroll /
  both bosses tougher; per-run progression fires on first kill only).
- **Affix tiers**: each rolled mod gets tier I/II/III (60/30/10), scaling BOTH boon and
  bane (x1 / x1.5 / x2.2); II/III show as roman numerals on the name. Tier rolls after
  the magnitudes per pick, so Add-modifier's prefix stability holds.
- Champion/resist picks are derived from portal seed + monster id (`IsRunChampion`) —
  no stored state, same answer at generation and death, on every client.
- **Save note**: the roll stream changed (tier roll + larger pool), so scrolls saved
  before this build silently re-roll their modifiers once. Accepted pre-1.0.

**Farnham's fruit machine** ✅ shipped 2026-07-19 (the gamble gold sink)
- Farnham returns to the hub as **Farnham the Gambler** (drunk sprite, TOWN_GAMBLER
  behavior, Wirt's old plaza spot {33,36}); the dead SpawnBoy stock is retired.
- Three weighted reels (Gold/Sword/Scroll/Potion/Skull/Crown, weights 5/4/4/6/6/2).
  The outcome is rolled AND paid at pull time; the spin screen is pure presentation —
  reels cycle per-frame (SDL-tick driven in DrawSText), stop left→right at
  750/1250/1750 ms with a click each, payout jingle after the last.
- Stake = 500 + 250 x deepest-beaten depth. Payouts: triple gold 8x stake; triple
  sword a good item; triple scroll a dungeon scroll; triple potion two full heals;
  crown pair 3x, other pairs half back; triple skull nothing. Item/scroll prizes are
  capped at EARNED depth (same rule as the vendor gate) and drop at the player's
  feet — no bag-room dialogs.
- **The displayed jackpot**: a pre-rolled unique (droppable base, UIMinLvl <=
  max(4, deepest+1)) shown by name and inspectable; triple crowns (~1 in 2,460)
  claim it. Session state, rerolled with the vendor restock on boss kill; once
  claimed it stays gone until the next boss.

**Feature ideas (next slices)**
- [ ] Loot tuning pass via TSV (M4 remainder).
- [ ] Fruit machine polish: reel symbols as item icons instead of text, Farnham
      win/lose voice barks, odds/EV tuning after playtest.

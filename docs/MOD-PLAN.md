# The Red Portal — Mod Plan & Roadmap

The mod: a **roguelike-run total conversion** of Diablo 1 on top of DevilutionX. A compact
hub town replaces Tristram; **dungeon scrolls** open red portals to seeded, self-contained
dungeon runs with scaled bosses and loot. Builds for **Windows and Nintendo Switch** from
one tree; co-op requires all clients on the same build (layouts are deterministic from a
synced seed).

Branding is presentation-layer only (**The Red Portal** in window title / menu / Switch
NACP — renamed from the private-era working title on 2026-07-19, ahead of going public);
binary and project stay `devilutionx` so upstream merges stay clean. Factual references
to the game the mod requires (e.g. "needs your own Diablo game data") are fine; the mark
never appears in our product name or branding.

Last updated: **2026-07-21** (post-v0.4.2: portal-exploit fixes + restored cut content).

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
- **In-game manual + chronicle** (2026-07-19): Cain's menu gained "Ask about the rules"
  — a two-page static primer (TalkID::StorytellerRules) covering the run loop, clocks,
  ghost/boss split, death rules, crafting, and progression unlocks (update it when
  rules change) — and "Ask about your deeds" (TalkID::StorytellerProgress): deepest
  boss slain, vendor stock depth, pack rows + next-row depth, difficulty ladder
  status, total monster kills, and active Preservation marks.
- **Presentation**: red-portal home-menu icon, no pregame difficulty screen (difficulty
  comes from scrolls), multiplayer unblocked.
- **Own identity** (2026-07-19, v0.4.1): release artifacts ship as `the-red-portal.exe` /
  `.nro` (renamed at packaging time only — build-tree targets stay `devilutionx` for
  clean upstream merges), and the save/config namespace is `diasurgical/the-red-portal`
  (paths.cpp PrefPath/ConfigPath), fully separate from vanilla DevilutionX installs —
  mod heroes carry item types vanilla cannot load, so shared saves were a footgun.
  Migration = copy saves from the old `devilution` folder; documented in both READMEs. (Torch flicker + dynamic wall shadows were
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
- **Anti-exploit trio** (follow-up, 2026-07-21), closing the two playtest exploits
  (ghost-portal reload loop; boss poke-and-retreat):
  - **One scroll, one session**: run state never survives a game start/load boundary.
    `ResetRunSession()` (diablo.cpp) seals the portal + zeroes depth/boss/clock; called
    from StartGame for every game and from LoadGame after reading the save. Root cause:
    portal globals are session-only but the process outlives quit-to-menu, so a portal
    "ghosted" into the reloaded game while LoadGame wiped LevelSeeds — one scroll
    re-entered a freshly regenerated dungeon forever.
  - **Mid-run saves are forfeited**: loading a save made inside a run sets
    `RunForfeitPending`; the first standing tick (ProcessRunClock) warps the player to
    the hub via TWARPUP with "The dungeon sealed itself while you were away!". Also
    kills the save-scum variants (clock/modifier stripping, boss whittling via reload).
  - **Fester clock**: RunClockTicks accrues while the portal is open even with nobody
    inside (spawns still on-level only; wave COUNT grows silently, first-notice message
    shows anywhere). Town supply trips now cost escalation time; grace starts at portal
    open, not first entry.
  - **Boss heals on abandon**: re-entering the run level restores any wounded living
    run boss to full (`HealAbandonedRunBoss`, monster.cpp) unless another player held
    the level throughout — the boss must be beaten in one committed stay (or a co-op
    partner holds the door).
  - Cain's rules primer updated (fester line on page 1; session/boss-heal lines on
    page 2).

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
- Stake = 500 + 250 x deepest-beaten depth. Payouts (retuned same day after playtest —
  v1 paid ~35% EV and felt like a mugging; now ~65-70% + the jackpot): any pair
  returns the FULL stake (~44% of pulls — keeps the wallet alive), crown pair 5x,
  triple gold 12x, triple crown 15x when the jackpot is claimed; triple sword a good
  item with boss-grade unique odds (uper 15); triple scroll a dungeon scroll + stake
  back; triple potion two full heals + 2x stake; triple skull nothing. Item/scroll
  prizes are capped at EARNED depth (same rule as the vendor gate) and drop at the
  player's feet — no bag-room dialogs.
- Playtest fixes (same day): item prizes queue in GamblePendingDrops and hit the
  floor only when the last reel lands (they dropped mid-spin and spoiled the show),
  flushed on any early exit so nothing is lost; reels draw in FIXED columns with
  per-symbol colors (one centered concatenated row re-flowed every frame and was
  unreadable).
- **The displayed jackpot**: a pre-rolled unique (droppable base, UIMinLvl <=
  max(4, deepest+1)) shown by name and inspectable; triple crowns (~1 in 2,460)
  claim it. Session state, rerolled with the vendor restock on boss kill; once
  claimed it stays gone until the next boss.

**Restored cut content** ✅ shipped 2026-07-21 (data-only; zero shipped assets)
- Diablo 1 shipped monster rows that never spawn (`availability=Never`); we verified
  against the actual MPQ hash tables (scratch tool, StormLib-compatible hashing) which
  ones have complete sprites+sounds and re-enabled those. The worm family (Wyrm/Cave
  Slug/Devil Wyrm/Devourer) is truly gone from retail MPQs and stays cut.
- **New pack monsters** (base + Hellfire TSVs, native Blizzard stat rows):
  - *Red Death* (dun 8-10), *Litch Demon* (9-11), *Undead Balrog* (11-13) — Storm-AI
    recolors of the Storm Rider (`thin` + `thinv3.trn`), fully self-contained in D1.
  - *Bone Fiend* (renamed from placeholder "oldboned", demon-skeleton sprite,
    re-banded 24 → 7-10 to match its lvl-12 stats; skeleton sounds).
  - *Incinerator* (13-14) / *Flame Lord* (14-15) / *Doom Fire* (21-23) / *Hell
    Burner* (22-24) — the cut "fireman"; its FireMan AI was never implemented, so
    they run Magma AI (magma-ball lobber) with magma-demon sounds.
  - *The Shredded / Hollow One / Pain Master / Reality Weaver* (17-20, Hellfire only:
    `unravw.cl2` exists only in hellfire.mpq; the rest live in DIABDAT).
- **Blizzard's half-finished rows completed**: all Storm/Magma-AI enables had
  toHitSpecial/animFrameNumSpecial/special damage zeroed (lightning that never fires);
  filled from Storm Rider / Magma Demon reference values.
- **Six new run bosses** (unique_monstdat rows appended, level 0 = never organic;
  stats normalized by ScaleRunMonsters as usual), one per tileset pool: *The Red
  Death* (cathedral), *Marrowgrave* (catacombs, Bone Fiend), *Satyr Lord* (caves,
  goatlord sprite — D1 sessions use goat sounds, Hellfire its intended `newsfx\satyr`),
  *The Invisible Lord* (hell, dark Hidden; `tsneak` sounds don't exist so it borrows
  `sneak`), *Gorefang the Brute* (nest, giant fallen `bigfall`), *The First Flame*
  (crypt, fireman).
- Not restored: worm family (no sprites), Golem/quest types (special-cased).

**Relics: the quest uniques return** ✅ shipped 2026-07-21
- With quests retired, the classic quest uniques (Harlequin Crest, Ring of Truth,
  Empyrean Band, Optic Amulet, The Undead Crown, Griswold's Edge, Arkaine's Valor,
  Veil of Steel, + Bovine Plate on Hellfire) were dead data: their base items have
  dropRate 0 and nothing referenced them. They now drop as **relics** — a 25% roll (was 8%; see the loot pass)
  per boss vault (`TryDropRunRelic`, items.cpp), uniform over the relics whose
  depth gate (2/3/4/5/6/6/8/10/12, ordered by power) the run has reached, announced
  with "A relic of the old world!". Duplicates allowed: relics are loot, not
  achievements. Twinned runs roll per twin vault.
- Origin: a survey of permissively-licensed DevilutionX mods (wkdmod SUL, Middle
  Earth Mod Unlicense) for portable content. Verdict recorded for the future:
  wkdmod's original items all depend on their custom engine powers (IPL_EMPOWER,
  IPL_ACIDBOW) and its remaining catalogue is the Hellfire uniques we already have;
  Middle Earth is a Tolkien re-theme (rebalance reference only). Neither yields
  direct data ports — the quest-unique gap found during the survey was the prize.

**Boss drop-rate audit** ✅ fixes shipped 2026-07-21
- A unique's effective level is `mlevel*2` (row level set) or `type level + 5` (row
  level 0), which drives its base-item pick (RndUItem), pre-fix affix floor, XP and
  hit math. Pool bosses span effLvl 4 (Bonehead) .. 56 (Doomlock) at the same depth,
  and the new cut-content bosses widened it (The Red Death effLvl 21 in cathedral).
- **Fixed — loot**: the boss's base-item pick now uses `RndUItem(nullptr)` (capped at
  2x depth like the affix level) instead of monster level, and the affix level is a
  flat `2x depth` (was `max(2x depth, type level)`, which leaked type-level affixes
  at shallow depths). Every same-depth boss now pays identically; vault (2x depth,
  uper 1), champion payout, trash, scrolls, relics and gamble were already
  depth-consistent.
- **Fixed — XP**: run-boss XP normalized to `300 x depth` (x2 unique, vanilla
  difficulty transform), replacing type XP that swung 68..4480 (~30x) between
  same-depth bosses.
- The Undead Crown left the relic pool: MT_SKING carries `Uniq(SKCROWN)` treasure,
  so the Skeleton King boss already guarantees it (as the Butcher does his Cleaver).
- Known, accepted variance (report if playtests feel it): boss toHit/AC and the
  level term in hit math still come from the base type/row -- combat feel differs
  per boss (e.g. The Red Death is evasive for depth 1-4 characters); HP/damage are
  normalized in ScaleRunMonsters. Nest/Crypt outliers Grimspike (38) and Doomlock
  (56) predate this audit and only affected these same paths.

**Full roster + Cain's hero guides** ✅ shipped 2026-07-21
- **Bard and Barbarian are roster members** in run mode (Hellfire sessions, same gate
  as the Monk): selhero no longer hides them behind the sprite packs / test options.
  Their classdat/sprites TSVs already ship with the mod; the engine renders them with
  Rogue/Warrior art when dedicated packs are absent.
- **Cain: "Ask about the heroes"** (menu line 18; goodbye moved to 20): a class list
  (TalkID::StorytellerGuides; Hellfire trio gated on gbIsHellfire) opening a one-page
  build primer per class (TalkID::StorytellerGuideShow, GuideClass selector) -- what
  stats matter, how the class plays, one run-specific tip each.

**Feature ideas (next slices)**
- [x] Loot tuning pass (2026-10-25) — see below.
- [ ] Fruit machine polish (remaining): Farnham win/lose voice barks, odds/EV tuning
      after playtest.

**Loot tuning pass** ✅ 2026-10-25 (built, NOT yet playtested)
- Audit: item level is already on the vanilla curve at every depth — trash drops at the
  monster's base level (vanilla), chests/barrels/floor items at 2x depth (vanilla), and
  the boss + vault at 2x depth (= a vanilla chest at that depth). Monster pools/levels are
  vanilla per depth. So item LEVEL was not the problem; VOLUME was: vanilla's guaranteed
  quest uniques (depths 5-13) are gone, extraction death wipes gear, and the vault was
  shared by the whole party.
- Vault: (3 + depth/4) items PER PLAYER in the game (was 2 + depth/5 shared), each
  boss-grade via RollGamblePrizeItem (uper 15: 16% unique chance, affix tier 2x depth + 4,
  same as the boss's own drop). Gold piles/tokens/gem unchanged.
- Relic chance 8% -> 25% per vault (relics are the quest uniques' only source).
- Trash drops and item levels deliberately unchanged (vanilla parity).
**Dungeon rules: one exit, any tileset** (2026-10-25, built + drlg tests green, NOT yet playtested)
- One staircase per run level, and it is the exit: generators skip the down stairs, the
  town-shortcut stairs (5/9/13) and the hell gate under `RunDungeonRules`. Players arrive on
  open floor in the farthest quarter of the level by walking distance from that exit
  (`PlaceRunArrival`, trigs.cpp; seed-picked so co-op peers arrive together; kept clear of
  objects and of monster spawns like stairs are). The portal remains the quick way back.
- Tileset variety without losing progression (user: fully random tilesets 'lose the sense
  of progression'): each run level uses its depth's own tileset 3 times in 4, otherwise the
  band just above or below (Nest/Crypt only on Hellfire), rolled from the synced dungeon
  seed (`RunTileset`, gendung.cpp). Palette is seed-picked too (`RunLevelPalettePath`): the
  four vanilla palettes or four TH4 recolours shipped as `assets/levels/l<n>data/l<n>_rp<k>.pal`
  (Cathedral/Catacombs/Caves/Hell; Caves get the strongest: Glacial, Lava Pits, Mossy
  Grotto, Milk Rivers). Monster pools, item levels and trap strength follow DEPTH
  (ItemsGetCurrlevel/AddTrap rebase the Hellfire bands by depth); the boss pool follows the
  tileset. Tileset-bound object art loads via `TilesetContentLevel`; Crypt books / Na-Krul
  gate only on real Crypt tiles; the automap names the run depth.
- `RunDungeonRules` is a runtime flag set in StartGame, so the golden-layout dungeon tests
  keep exercising vanilla generation.

**Gambler's Tokens** ✅ shipped 2026-07-21
- New base item `IDI_GAMBLETOKEN` (itemdat.tsv last row + enum; BLOOD_STONE icon,
  dropRate 0, value 150): each token is ONE free pull at Farnham's at full payouts,
  so a token's worth scales with the stake like everything else on the machine.
- Drops from run-level trash at 2% (scrolls are 3%; tokens don't stack, so rarer
  keeps the pack breathing) via DropGambleToken, mirroring DropDungeonScroll.
- Farnham's menus grow fixed-position token lines while chips are held (menu:
  12 gold / 14 token / 16 jackpot / 18 goodbye; spin screen: 14 token re-pull /
  16 gold re-pull). ResolveGamble(tokenSpin) consumes a chip instead of gold.
- Dev QoL: `dev.items.spawn` gained a direct path for dropRate-0 items (exact name
  match) -- they can never come out of the random roller; the path also sets the
  ground-drop animation InitializeItem skips (null-sprite crash otherwise). (Icon reels shipped 2026-07-21: each face is a cursor-sheet
      item sprite — gold pile / short sword / scroll / red potion / skull cap /
      crown — bottom-anchored on a shelf at line 11 via GambleSymbolCursor +
      ClxDraw, so mixed icon heights grow upward and the result line stays clear.
      Text reels and their per-symbol colors retired.)

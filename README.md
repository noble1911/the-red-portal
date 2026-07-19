# The Red Portal

**A PvE-extraction total conversion of the classic Diablo engine**, built on
[DevilutionX](https://github.com/diasurgical/devilutionX) for Windows and Nintendo
Switch (homebrew), with cross-device co-op.

The linear sixteen-level descent is gone. Tristram is a compact hub; **dungeon
scrolls** open a red portal to a single, self-contained run. Go in, get greedy,
and get out — *a run is a heist to survive, not a level to clear.*

> Requires your own copies of the original game data (see
> [Game data](#game-data)). This project contains no Blizzard assets.

## How it plays

- **Dungeon scrolls are the only way down.** Each scroll defines its run: depth
  (1–15, or 1–24 with Hellfire data), difficulty tier (Normal / Nightmare /
  Hell), rarity, and 1–3 **affixes** — paired boons and banes rolled from a
  13-affix pool (more gold / magic find / drops / XP against tougher, faster,
  denser, elementally-warded monsters, extra champions, twinned bosses, richer
  chests and shrines...). Each affix rolls a tier (I–III) scaling both sides.
- **The clock is always running.** Five quiet minutes, then the dungeon notices
  you: hunt waves spawn every minute and keep growing.
- **Every run has a boss.** Slay it and its vault spills open — a loot shower,
  gold, and always a scroll one depth deeper — but its death starts a
  three-minute **collapse**: loot fast and leave, because when the countdown
  ends the portal seals for good.
- **Or ghost the run.** Skip the boss, keep everything you found, and slip out.
  Depth must then be bought: the merchant's scroll-fusion crafting is a real
  gold sink, and his shelf only stocks depths you have *earned* by beating
  bosses.
- **Death is an extraction failure.** Everything you carried drops at your
  corpse — gear, backpack, belt, half your gold. The run persists behind the
  portal, so a corpse run can win it back... until the portal closes. A
  **Scroll of Preservation** insures a single treasured item through one death.
- **Progression is earned, not bought.** Boss kills extend the merchant's
  stock, grow your backpack (up to 10x11), and — at the deepest level — unlock
  the next difficulty tier.
- **Town has teeth.** Cain explains the rules and chronicles your deeds;
  Farnham runs a three-reel fruit machine with a displayed unique jackpot
  (~1 in 2,460 — the house usually drinks well).

The full design history, shipped systems, and roadmap live in
[docs/MOD-PLAN.md](docs/MOD-PLAN.md).

## Installation

Grab the latest [release](../../releases/latest):

### Windows

1. Unzip `the-red-portal-windows.zip` anywhere.
2. Copy your game MPQs (see [Game data](#game-data)) next to `the-red-portal.exe`.
3. Run `the-red-portal.exe`.

### Nintendo Switch (homebrew)

1. Unzip `the-red-portal-switch.zip` and copy `the-red-portal.nro` **and the
   `mods` folder** to `/switch/the-red-portal/` on your SD card. The `mods/Hellfire`
   folder is required for the Hellfire content — the MPQs alone are not enough.
2. Copy your game MPQs to `/switch/the-red-portal/`.
3. Launch via title takeover (hold **R** while opening any installed game — the
   album applet has too little memory). See
   [Packaging/switch/README.txt](Packaging/switch/README.txt) for controls.

### Saves

The mod keeps its saves in its **own folder**, fully separate from any vanilla
DevilutionX install on the same device — your DevilutionX characters are never
touched (and mod characters carry items vanilla can't load, so don't move them
by hand into a vanilla install):

- Windows: `%AppData%\diasurgical\the-red-portal\`
- Switch: `/switch/the-red-portal/`

Upgrading from a pre-v0.4.1 build of this mod: copy your saves (`*.sv`,
`*.hsv`, `diablo.ini`) from the old location (`%AppData%\diasurgical\devilution\`
on Windows, `/switch/devilutionx/` on Switch) into the new folder.

### Game data

You need data files from a copy of the game you own:

- `DIABDAT.MPQ` — from the original CD or the
  [GOG release](https://www.gog.com/game/diablo).
- Optional, for the Hellfire depths (runs 17–24): `hellfire.mpq`, `hfmonk.mpq`,
  `hfmusic.mpq`, `hfvoice.mpq`.

## Multiplayer

Co-op works on desktop and Switch, including Switch-to-Switch. All players must
run the **same build** — dungeon layouts are generated deterministically from a
seed synced between clients, so mixed versions will desync. The scroll owner's
portal defines the run (depth, difficulty, and affixes) for everyone.

## Building from source

The tree builds both targets:

- **Windows (MSVC + vcpkg)**: `scripts/build-desktop.ps1` — see
  [BUILD-WINDOWS.md](BUILD-WINDOWS.md) for prerequisites and the devkitPro
  PATH gotchas.
- **Switch (devkitPro)**: `scripts/build-switch.ps1`, or the
  `CMake/platforms/switch.cmake` toolchain on Linux/macOS as upstream does.

For everything engine-level (other platforms, options, translations), the
[upstream DevilutionX docs](https://github.com/diasurgical/devilutionX#readme)
apply unchanged.

## Credits

- Built on **[DevilutionX](https://github.com/diasurgical/devilutionX)** by
  [Diasurgical and contributors](https://github.com/diasurgical/devilutionX/graphs/contributors)
  — the engine reconstruction that makes projects like this possible.
- The Red Portal mod design and implementation: this repository's commits.

## Legal

The source code in this repository is licensed under the
[Sustainable Use License](LICENSE.md) — non-commercial use and free
redistribution only.

This project may only be used with game assets you own. Diablo® is a
registered trademark of Blizzard Entertainment, Inc. This project is a fan
modification of a community engine reconstruction; it is in no way associated
with or endorsed by Blizzard Entertainment.

# Phase 4 research: Magic Carpet 2 - The Netherworlds as an asset / feature source

Written 2026-10-07. Sources: the remc2 / Magic Carpet 2 HD repositories, the user's own GOG copy of
MC2 (`C:\Program Files\GOG Galaxy\Games\Magic Carpet 2`, read only; data track extracted to
`C:\Magic Carpet\extracted\mc2_cd`), the GOG manual and reference card PDFs (scanned, read page by
page), the CD README, the English language file `LANGUAGE/L2.TXT`, the exe string dump kept in the
remc2 repo (`remc2/mined-texts.txt`), the SDA knowledge base, and TCRF (via search snippets only:
tcrf.net serves an anti-bot page to fetchers, see "TCRF findings").

Everything the Phase 4 planner needs is in the first five sections; the file inventory and the
format verification are in "Data files on the GOG CD".

---

## 1. Decomp repo

| | |
|---|---|
| Original | `https://github.com/turican0/remc2` - "Recode binary code of game Magic Carpet 2 to C/C++", by Tomas Vesely (turican0). ~150 stars. Wiki: `https://github.com/turican0/remc2/wiki` (method: IDA/Hex-Rays + Snowman, scripted renaming, manual rewrite of graphics/IO/sound, validated against a modified DOSBox running the original binary; 64-bit cleanup of byte arrays into typed structs). |
| Active fork | `https://github.com/thobbsinteractive/magic-carpet-2-hd` ("Magic Carpet 2 HD", remc2 fork). Default branch `development`, last push 2026-10-03, 110 stars, C++17. Releases up to `0.9-alpha-stable`. |
| Other forks | `https://zff.dev/rodan/magic_carpet_2` (Linux/FreeBSD/OpenBSD port: controller + haptics, OpenAL positional audio rewrite, narrator playback, CMake, `config.ini`). |
| Licence | GitHub reports **GPL-3.0** (`docs/LICENSE`, 35 KB). The README says "Original Source Code is Copyright 1995 Bullfrog Productions" with the project's additions under MIT - i.e. the translated Bullfrog code has no clean licence at all, and the repo-level licence file is GPL-3. Treat it as GPL / reference-only (same verdict as ROADMAP.md). |
| Build | Windows: Visual Studio 2022 + vcpkg (`remc2.sln`, x64 or x86). Linux: CMake + GCC with SDL2, SDL2_mixer, SDL2_image, SDL2_ttf, spdlog, nlohmann-json, wxGTK 3.2; flatpak manifest. CI builds both. MSI installer (`remc2-installer`). |
| Data | Needs the GOG game. Windows: copy `NETHERW` to `remc2\Debug`, run `EXTRACT\extract-GOG-CD.bat` (DOSBox + XXCOPY16 copies the CD files to `CD_Files`). Linux: `check_install.sh -s <GOG dir> -d <dest>` or `extract-GOG-CD.sh` (innoextract). |
| Completeness | All MC2 levels play on Windows and Linux; HD / 2K / 4K software rendering, W/A/S/D + controller, configurator, level editor, re-enabled debug/cheat functions, custom levels beyond the 127-level table via a `-level`-style argument; the original NetBIOS/serial netcode is replaced by their own TCP client/server (see below). Roadmap: OpenGL renderer, MC1 compatibility, VR, online play. |

### Directory layout (development branch)

```
remc2/                     the translated game
  sub_main.cpp             entry, main loop
  engine/
    Basic*.cpp, engine_support*.cpp   memory, file I/O, RNC ("ReadAndDecompress.cpp", "lzcompress.h")
    GameRenderOriginal.cpp (466 KB)   the 1995 software renderer, translated
    GameRenderHD.cpp (501 KB)         same renderer widened for HD
    GameRenderNG.cpp (465 KB), GameRenderGL.cpp   next-gen / OpenGL experiments
    Terrain.cpp (63 KB)               heightmap generation + deformation
    EventsFunctions.cpp (1.9 MB!)     the per-class / per-state Thing update handlers (creature AI, spells, buildings)
    Events.cpp (108 KB)               the dispatch (like our Table A/B)
    Spells.cpp/.h                     SPELLS.DAT table (26 x 80 bytes) and defaults
    Level.cpp, LevelInit.cpp, LevelStructs.h   level load, decompressed level layout (Type_CompressedLevel_2FECE)
    Player.cpp, PlayerInput.cpp, GameUI.cpp, MenusAndIntros.cpp (246 KB)
    Network.cpp (62 KB) / Network.h   replacement netcode (TCP, server/client)
    Sound.cpp (188 KB), AIL_stub.cpp (228 KB)  Miles AIL 3 API stub over SDL_mixer; XMI via libADLMIDI
    Animation.cpp, GameBitmap.cpp, DatTabIndexes.cpp, GameBitmapIndexes.h (named sprite ids)
    Type_*.h / global_types.h / structures.x / info.x   typed data-segment structures
  mined-texts.txt          every string of NETHERW.EXE with its data address (very useful)
remc2-editor/              SDL "kiss" GUI level editor (editor.cpp 154 KB)
remc2-configurator/, remc2-installer/, remc2-unit-test/, remc2-regression-test/
docs/                      "magic carpet file format.txt" (= michaelhoward's MC1 notes), structures_info.txt, events.md
EXTRACT/                   GOG CD extraction scripts
libADLMIDI-master/         OPL emulation for the AdLib music path
enhancedassets/, graphics/ upscaled textures (ESRGAN, 32x32 -> 128x128 per the wiki)
```

### Engine facts recorded in the repo (MC2 vs our MC1 knowledge)

- **Thing record** `type_shadow_str_0x6E8E`, comment "lenght a8": the fields run to `dword_0xA4_164x`
  + 4 = **0xA8 (168) bytes** in MC2 vs 0xA4 in MC1; the first 0xA4 bytes line up with ours
  (+4 life, +8 max life, +0x18 next in cell, +0x1a owner, +0x1c..0x22 rotations, +0x3f model,
  +0x40 class, +0x4c xyz, +0x82/84/86 speed actual/min/max, +0x8c/+0x90 mana actual/max,
  +0x94 player thing index). The pool is still **1000 Things** (`struct_0x6E8E[1000]`, two
  1000-entry pointer tables at +0x246 / +0x11EA).
- **Maps**: `mapTerrainType[65536]`, `mapHeightmap[65536]`, `mapShading[65536]`, `mapAngle[65536]`,
  `mapEntityIndex[65536]` (int16) - the same five 256x256 maps as MC1 (texture / height / light /
  flags / cell -> thing head). `building_F2CD0x[2800][2]` = the 7-level castle footprint table
  (2*(7^4+7^3+7^2+7) = 2800).
- **Textures**: block16/block32 atlases are the same 155,648-byte 32x32 layout (608 rows x 256) -
  MC1's block32 is also 155,648. Sprites are 8-bit palette, 6-bit VGA palettes (max component 63).
- **Sound**: MC2 moved from HMI SOS (MC1) to **Miles AIL 3** (`.DIG`/`.MDI` drivers, `DIG.INI`,
  `MDI.INI`, `AIL3DIG`/`AIL3MDI` headers, XMIDI music, RIFF WAV samples, AWE32 `.SBK` bank). The
  remc2 `AIL_stub.cpp` is a reimplementation of that API.
- **Netcode**: original = NetBIOS (2-8 players, session number 0-9, `-network` argument,
  `netbios.exe` shipped by GOG) plus VFX1/i-glasses serial. remc2 HD replaced it with its own
  TCP lockstep: `--network server <port>` / `--network client <ip> <port> <client-port>`,
  `--network_debug` logs (`Network Guide.txt`). Their `Network.h` only exposes connect / roster /
  send / receive / leave-session functions, so the game logic still runs the original
  "one command packet per player per tick" model like our port.
- **Resolution**: 320x200 and 640x480 (VESA) in the original (hi-res sprite banks `HSPR*`,
  `HWEB*`, `HFONT3`, `HSCREEN0`); remc2 HD renders the same software pipeline at any size.
- **Debug / cheats re-enabled** by the fork: see TCRF section for the keys.

---

## 2. Spells (full list + behaviour + reuse ideas)

Authoritative list = `LANGUAGE/L2.TXT` (26 names, then 26 groups of sub-spell names) +
`DATA/SPELLS.DAT` (26 records x 80 bytes: `u8 flags, u8 enabled, 3 x {i32 subSpellIndex, i32 manaCost,
i32 castleManaNeeded, i32 xp1, i32 xp2, i16 hintText, i16, i8 life, u8 font}`, per remc2 `Spells.h`)
+ the manual pages 14-17. The manual says "25 basic spells, three levels each = 75"; the data has 26
(Cave In is the 26th, probably cavern-only) and Morph / Summon Army have four named variants
(day / night creature swap). Mana costs below are level 1 / 2 / 3 from SPELLS.DAT; "castle" is the
castled-mana threshold that unlocks the level.

| # | Spell (levels) | Cost 1/2/3 | Behaviour (manual) | Reuse for our RTS mode |
|---|---|---|---|---|
| 0 | **Fireball** / Rapid Fire / Fire Storm | 100 / 250 / 2500 (lvl3 needs 160k castle) | basic shot; stream; "wraps a fiery storm around its target" | already in MC1 (fireball, rapid fire = MC1 lvl 2) |
| 1 | **Possession** / Mana Magnet / Mana Lock | 100 / 250 / 1000 | take free mana; **Mana Magnet** possesses and attracts all mana in a radius (SDA: casting too often freezes the magnet; can overfill the balloon past its 10,000 soft cap); **Mana Lock** prevents others possessing your mana unless they also have Mana Lock | Mana Magnet = great "late-game economy" pickup; Mana Lock = anti-griefing in BR |
| 2 | **Castle** / Fire Tower / Lightning Tower | 1000 / 1250 / 5000 | build/upgrade (7 stages, SDA HP: 10k/40k/40k/60k/60k/80k/80k); **towers add wall turrets** that fire rapid fireballs / lightning at attackers | the "buildable castle" of the BR brief: turrets are the obvious defensive upgrade |
| 3 | **Speed Up** / Super Speed / Super Speed Plus | 1000 / 2500 / 5000 | 2x / 3x / 4x speed (forward/back only) | mobility tier |
| 4 | **MetaMorph** (Bee or Firefly / Cymmerian / Wyvern) | 10000 / 20000 / 40000 (needs 40k/120k/360k castle) | turn into a creature; SDA says "never found it has any effect" in practice | fun but unreliable - skip or reimplement |
| 5 | **Heal** / Aid / Constitution | 500 / 10000 / 50000 | small heal; longer + bonus HP; full HP for a short period (SDA: level 1 far more mana efficient) | keep |
| 6 | **Shield** / Shield II / Invulnerable | 1000 / 5000 / 10000 | -25% damage; same, twice as long; blocks one hit completely | keep |
| 7 | **Lightning** / Thunderbolt / Thunderstorm | 1000 / 10000 / 20000 | bolt; storm; double storm. SDA: lvl 2-3 are the castle killers (Lightning 3 ~100,000 damage vs castle), lvl 1 misses often | anti-castle siege spell |
| 8 | **Rebound** / Rebound II / Amplify | 5000 / 15000 / 25000 | returns incoming shots; longer; doubles rebounded damage. Wyverns are immune | keep |
| 9 | **Meteor** 1/2/3 | 6000 / 10000 / 20000 | big rock; 2x; more (SDA: Meteor 2 is the Hydra answer) | keep |
| 10 | **Teleport** / Teleport II / Castle Port | 5000 / 20000 / 40000 | to castle; to castle and back; cycle between you, your castle and every rival castle | Castle Port = BR "rotation" / raid mechanic |
| 11 | **Invisible** / Possess Invisible / Attack Invisible | 9000 / 18000 / 36000 | cloak until you cast; stays through Possession; stays through attacks | stealth tier |
| 12 | **Beyond Sight** / See Invisible / True Sight | 10000 / 20000 / 30000 | shows players + balloons on map; reveals invisible; both | map-intel tier (BR circle/recon) |
| 13 | **Steal Mana** / Double Steal / Ransack | 10000 / 20000 / 30000 | steal a portion of a rival's possessed mana; 2x; **Ransack robs a tenth of the target's castle mana and drops it as free balls around your own castle** | "raid" spell; Ransack is a strong BR objective |
| 14 | **Duel** / Mana Drain / Health Drain | 10000 / 20000 / 40000 | lock your position onto a target player; + drain mana; + drain health | PvP lock-on; interesting for 1v1 finals |
| 15 | **Tremor** 1/2/3 | 4000 / 6000 / 10000 | ground shake damaging players/creatures in range; larger / longer | new terrain-effect spell |
| 16 | **Crater** 1/2/3 | 6000 / 9000 / 12000 | hole in the landscape swallowing land foes; big; huge | MC1 has it |
| 17 | **Earthquake** 1/2/3 | 10000 / 12000 / 15000 | crevice under land creatures; SDA: kills land creatures that fall into water/lava, best wall breaker | MC1 has it |
| 18 | **Volcano** 1/2/3 | 12000 / 15000 / 18000 | small / larger / towering lava volcano | MC1 has it |
| 19 | **Summon Army** (8 Bees or Fireflies / 4 Cymmerians / 2 Wyverns) | 10000 / 20000 / 40000 (needs 150k/200k/300k castle) | summoned creatures hunt rival players and castles; they vanish if no other wizard is nearby | = MC1 Skeleton army generalised; ideal RTS "unit production" |
| 20 | **Gravity Well** 1/2/3 | 15000 / 20000 / 30000 | sucks creatures and rivals "into oblivion"; wider/longer. SDA: **instantly destroys non-wizard buildings and impenetrable walls** | siege / terrain-clearing |
| 21 | **Whirlwind** 1/2/3 | 2000 / 2500 / 5000 | tornado that sucks earthbound beasts into a vortex; more damaging/longer | the "tornado" of the brief; cheap crowd control |
| 22 | **Fool's Mana** / Rapid Fire Fool's Mana / Lightning Fool's Mana | 15000 / 20000 / 25000 | fake mana balls that fire a fireball / a stream / lightning at whoever possesses them | BR trap item |
| 23 | **Magic Mine** 1/2/3 | 10000 / 12000 / 24000 | deposit a Mine Node, arm it by hitting it with any offensive spell; invisible to others; fires 2 / 4 / 8 shots at passing wizards, then expires (remc2 gives slot 23 special castle limits 50k/70k/90k) | BR trap item |
| 24 | **Alliance** 1/2/3 | 10000 / 18000 / 30000 | all creatures of one species in a radius fight for you for a while; wider/longer | neutral-creature recruitment (an RTS staple) |
| 25 | **Cave In** 1/2/3 | 11000 / 13000 / 26000 | not in the manual; name only ("CAVE IN"); presumably collapses cavern ceiling/walls | cavern-only; skip unless we do caves |

Spell experience: each level has a fixed XP requirement; attack spells gain XP only on hits, Castle
gains per stage built (max 7 per mission), Speed Up / Morph / Beyond Sight / Invisible / Teleport
gain per use; scrolls give 4 XP (manual p13, SDA). Spells are collected from urns; spells that
need more castled mana stay greyed in the menu.

MC2 dropped from MC1: Skeleton Army (replaced by Summon Army), Thunderbolt as its own slot
(folded into Lightning), Mana Magnet as its own slot (folded into Possession), Fire Wall / Reverse
Speed / Smart Bomb / Mini Fireball (the unused MC1 slots). MC1's `Rubber band` became Duel.

---

## 3. Creatures (list + behaviour + RTS unit suitability)

Model names in the exe string table at 0xDAC71.. (order = model id of class 5, matching MC1's
table for the first 17): Dragon, Vulture, Bee, Worm, Archer, Crab, Kraken, Troll, Griffon,
Skeleton, Vissuluth, Genie, Builder, Townie, Trader, Wyvern, Manticore, Sentinel, Firefly, Spider,
Devil, Mana Worm, Mana Eater, Cymmerian, Dark Demon, Hydra, Deep One, Tail, Goat, Zombie. TCRF notes
the early names: Mana Eater = Moon Dweller, Dark Demon = Troglodyte, Deep One = Leviathan.
MC1's Crab / Kraken / Troll / Griffon / Genie keep their ids but are not placed in any of the 200
MC2 levels (class-5 census below), and the manual does not list them.

| Creature | Manual behaviour | Env. | RTS unit verdict |
|---|---|---|---|
| Archers | wander in groups, attack on sight with arrow volleys | day/night | basic ranged infantry (MC1 has them) |
| Castle Archers | tied to a fortress, fire when you come close | all | garrison / tower unit |
| Bees | swarm, attack you and your castle, sting at close range | day | cheap melee swarm (Summon Army 1) |
| Fireflies | swarm, only retaliate, poison sting, fragile | night | night counterpart of Bees |
| Cymmerians | winged, hard to kill, fireballs; **on death spawn a swarm of tiny fast bugs that cross land and water to nibble castle walls** | all | flying raider; the bug swarm is a ready-made "siege insect" unit |
| Devils | small, leap across land and water, fireballs, erratic | all | light harasser |
| Dragons | flying, segmented, dodge simple spells, hunt alone, medium fireballs | all | MC1 unit |
| Goats | herds, no attack, easy mana | day | "neutral farm" resource |
| Hydras | huge multi-headed ground beast; every head must die before the body, heads regrow; fireballs + lightning from each head (SDA: head order 5-3-1-2-4 or similar; fight next to your castle) | all | boss / objective creature |
| Leviathans (Deep One) | sea creature, submerges then rears up; hard to kill in water | sea | naval boss / zone denial |
| Mana Worms | pure mana, drift with the wind, indestructible, no attack; possess one and it walks slowly to your castle and converts to castle mana; value grows with age | all | moving resource node - perfect BR "supply drop" |
| Manticores | fast land packs; stun shots with cumulative effect, then lunge; hard to kill | day | cavalry |
| Moon Dwellers (Mana Eater) | high in the atmosphere, descend to eat mana balls, powerful lightning, land when mana is scarce; high mana value | night | mana competitor / "vulture" over loot |
| Sentinels | stone faces, land-bound, slow, see far, fireball volleys (SDA: 2 or 3 homing volleys, 8k/12k damage), virtually indestructible | cavern | static turret / map hazard |
| Skeletons | arrow volleys at you and castles, kill human archers to swell their ranks | all | MC1 army unit (converts villagers in MC1 too) |
| Spiders | fast, hard to kill, web shots immobilise you then they lunge | cavern | crowd-control melee |
| Troglodytes (Dark Demon) | huge cavern dwellers, throw boulders, nearly immobile | cavern | artillery |
| Worms | segmented earthbound, fireballs that can down you | all | MC1 unit |
| Wyvern | "makes a Dragon resemble a spring lamb": rapid fireballs, immune to Rebound, attacks castles, regenerates when half dead (SDA: can be kited at max visible range) | all | elite air unit (Summon Army 3) |
| Zombies | undead army that drains player mana, easy to kill, transparent until close, occasionally steal spells/objects | night/cavern | anti-caster swarm |
| Builder / Townie / Trader, Goat | civilians (MC1 behaviour), Vissuluth | - | - |
| Vulture (model 1, 1779 placements), Tail (27, Hydra necks) | present in levels, not in the manual | - | - |

Damage stacking (SDA): some monsters deal more damage per consecutive hit (bee 200, 400, 600, 800
...), reset by full heal or castle contact; three damage groups (webs/manticores/leviathans/
troglodytes; bees; fireballs/arrows). Creature death explosions damage nearby things and can take
a castle stage.

---

## 4. Terrain / level features

- **Three environments**, chosen by level header byte +6 (0 day, 1 night, 2 cave): every
  environment file has a `D`/`N`/`C` variant - palette `PAL?-0`, colour table `CLR?-0` (4096 =
  16x256 shade rows), `TABLES?` (83,456, same size as MC1 `tables.dat`), `SKY?0-0` (65,536 = 256x256,
  as MC1), `BL16?0-0` / `BL32?0-0` tile atlases, `GTDEF?` / `FTDEF?` (ground / feature texture
  definitions, 65,536 / 16,384-51,200), HUD sprites `HSPR?`/`MSPR?`, `HWEB?`/`MWEB?` (25 / 9 large
  120x126 panel pieces), `TMAPS0/1/2` (billboards, loaded with block16/skyd, bl16n/skyn, bl16c/bl32c
  respectively in the exe string order). There is also an `F` set of block atlases and palette
  (`BL16F0-0`, `BL32F0-0`, `PALF-0`) of unknown use (not referenced next to a sky; possibly the
  front-end or a fourth tint).
- **Night levels**: light sources toggle (`Shift-F4`), fireflies instead of bees, zombies,
  moon dwellers; creatures shown as white dots on the map instead of black.
- **Cavern levels** are **separate levels**, not sub-areas: the campaign enters them through
  portals/tunnel entrances ("the entrance only reveals itself to the most powerful wizards",
  "you can now return to the surface"); there are no rival wizards underground (Wikipedia) and
  objective text says "these tunnels all return on each other, find a way to blast out". The
  same 256x256 heightmap engine is used with a cavern sky texture (`SKYC0-0`, the ceiling),
  stalactite/stalagmite scenery, lava rivers, mushrooms instead of trees on the map, and lots of
  class-10 wall / path / canyon things (model 28 "Wall" 1,557 placements, 29 "Path" 2,554, 31
  "Canyon" 1,752 across all levels). Walls are impassable ("Walls cannot be passed", "Cannot build
  castles too close to walls") and some are indestructible (SDA: only Gravity Well / Earthquake /
  building a castle next to them breaks through).
- **Landscape deformation** (all run-time, stored only in the heightmap like MC1): Crater,
  Earthquake, Volcano (as MC1) plus Tremor, Whirlwind (tornado vortex), Gravity Well, Cave In.
  Terrain functions in remc2 `Terrain.h`: `sub_44DB0_truncTerrainHeight`, `sub_44EE0_smooth_tiles`,
  `sub_45DC0` (deformation with a type parameter), `terrain_tile_is_water`, `getTerrainAlt`.
- **Structures**: player castles with 7 stages (footprint table `BLDGPRM.DAT` / BUILD0-0 sprites
  with 77 tab entries vs MC1's 68), **Fire / Lightning Tower** turrets, enemy wizard castles with
  castle archers, mission buildings (barracks, war-council tower, apothecary, weapons vaults,
  temple, smelting houses, spires, Jark's Loretower that "is revealed" = rises). The SDA castle
  tricks (upgrade next to a building to raze it; build a castle to tunnel through walls) show that
  castle placement deforms terrain and destroys things in the footprint exactly as in MC1.
- **Mission system** (new vs MC1): objectives (collect mana %, destroy building(s), kill
  creature(s)/player(s), fly to point, exit point), checkpoints ("stages"), marker stones,
  "fly-to" signs, hidden realms (5 secret levels: Karakir, Ymbul, Pav Durivium, Beleem,
  Ommosyth), experience scrolls, spell urns, restore-life stones, mid-level save (SLEV/SMAP/SVER
  = level state + full map dump + version), screenshots (`shots`), demo movies (`movie/gam%05d`,
  `map%05d`, `mvi%05d` - same naming as MC1).
- **Wizard names** (network colours): You white, Nyphur red, Rahn green, Jark purple, Belix
  blue, Elyssia pink, Yragore black, Prish orange.

---

## 5. Data files on the GOG CD (inventory + compatibility with mctools)

### Install layout

`game.gog` (401 MB) is a raw **MODE1/2352 BIN** with 28 tracks (cue sheet `game.ins`: track 1 data,
tracks 2-28 CD audio = speech/music; the exe has a "rbyb" red-book/yellow-book player). 7-Zip
cannot open it directly; `scratchpad/mc2/track1.iso` was made by stripping the 2352-byte sectors
to 2048 (85,188 sectors -> 174 MB ISO, volume "_BULLFROG", 1995-09-06, CeQuadrat formatter).
DOSBox config mounts it as `D:` and runs `NETHERW.EXE` from the CD; `GAME\NETHERW` on disk holds
only the local-drive copies (`CDATA\TMAPS*`, `CLEVELS\LEVELS.*`, `SOUND\*.DIG/.MDI`, `SAVE`,
`CONFIG.DAT` 32 bytes, `VERSION.DAT` = 0x3c), identical to the CD files.

Extracted (everything except the 144 MB of `INTRO/*.DAT` FLI cut-scenes; `INTRO/INTEL.DAT` kept)
to `C:\Magic Carpet\extracted\mc2_cd` (148 files, 29.6 MB).

### CD root
`NETHERW.EXE` (1,039,105; MZ + DOS/4GW, strings say "Beta Sep 06 1995 03:42:13, Bullfrog, Alan
Wright, Supplied to PUBLIC"), `DOS4GW.EXE` (265,396, same as MC1), `SETSOUND.EXE` (Miles setup),
`NWSETUP.BAT`, `README.TXT` (controls, command line, network, All-Seeing Eye key).

### DATA (compared with MC1)

| File(s) | Size | Format | Same as MC1? |
|---|---|---|---|
| `TMAPS0-0 / 1-0 / 2-0 .DAT+.TAB` | 1.39 MB / 1.35 MB / 0.89 MB; tab 5,050 | `BULLFROG` tag + RNC chunks; tab = 505 x {u32 unpacked, u32 offset, u16 group} incl. sentinel; chunk header {u8 kind 2/3, u8 draw type, u16 w, u16 h}; kind 3 = frame 0 + FLC SS2 frames (`0xF1FA` follows) | **identical** - `mctools.tmaps.read_all` decodes all three banks (504/467/429 chunks, 143 groups; only guard against the 1/38/76 placeholder entries of size < 6) |
| `HSPR?0-0`, `MSPR?0-0` (.DAT+.TAB) | 158 KB / 49 KB; tab 1,572 | 262 x 6-byte {u32 off, u8 w, u8 h}; span rows + 1 trailing byte; **not** RNC any more | **identical encoding** - `mctools.sprites` renders the HUD/spell/castle icons correctly (verified visually, `scratchpad/mc2prev/HSPRD0-0.png`) |
| `BUILD0-0`, `POINTERS`, `BUTTON`, `HWEB?`, `MWEB?`, `FONT0/1/2`, `DFONT0/1`, `HFONT3`, `SFONT1` | | same 6-byte tab + span sprites (FONT0/2 still RNC) | identical |
| `PAL?-0.DAT`, `PALETTE.DAT`, `PALLOGO`, `PALTIT3`, `*.PAL` | 768 | 6-bit VGA | identical |
| `BLOCK16/32`, `BL16?0-0`, `BL32?0-0` | 155,648 | 32x32 tile atlas 256 wide (608 rows); the 16 variants are the same size here (MC1 block16 was 45,056) | same layout |
| `SKY?0-0` | 65,536 | 256x256 | identical |
| `TABLES.DAT` (RNC) / `TABLES?.DAT` | 83,456 | shade/blend tables, same size as MC1 | same |
| `SEARCH.DAT` | RNC -> 1,024 | spiral search rings | identical |
| `SPELLS.DAT` | 2,080 | 26 x 80-byte spell table (new; MC1 keeps it in the exe) | new |
| `BLDGPRM.DAT` | 304 | 152 u16 building parameters (new) | new |
| `GTDEF?`, `GTD2`, `FTDEF?`, `CLR?-0` | 65,536 / 16-51 KB / 4,096 | ground / feature texture definitions, colour tables (new names) | new |
| `LEVELS.DAT` (DATA dir) | RNC -> 38,812 | **an MC1-format level** left over on the CD | MC1 |
| `SCREENS/HSCREEN0.DAT` | 1.56 MB | hi-res screen container (31 RNC chunks behind a table) | new container |
| `SMALTIT`, `SMATITLE`, `SMATITL2` (.DAT+.PAL), `TITLE3`, `TITBASF` | 64,000 | 320x200 screens (RNC or raw) | identical |
| `?TEXT.DAT`, `LANGUAGE/L1-6.TXT`, `D2.TXT` | ~20 KB | plain text, `02 00` header then 0x58-prefixed records; L2 = D2 = English, L1 French, L3 German, L4 Spanish, L5 Italian, L6 Swedish | plain |
| `INTRO/*.DAT` | 3-36 MB; `INTEL.DAT` 296,632 | 12-byte-header FLI like MC1 (`0c 00 00 00 12 af`) | identical |
| `SOUND/SOUND.DAT` | 14.9 MB | 393 concatenated RIFF WAV (8-bit mono 22,050 Hz) | **different** (MC1 = HMI raw banks) |
| `SOUND/MUSIC.DAT` | 644 KB | XMIDI `FORM XDIR` catalogue + 96 `FORM XMID` songs/tracks | **different** (MC1 = HMI `HMIMIDIP`) |
| `SOUND/*.DIG`, `*.MDI`, `AILDRVR.LST`, `BULLFROG.SBK`, `SAMPLE.AD/.OPL` | | Miles AIL 3.x drivers, AWE32 SoundFont bank, OPL timbres | different |

### LEVELS

`LEVELS.TAB` = 1000 x u32 like MC1, `LEVELS.DAT` = `BULLFROG` tag + chunks. **200 levels**:
entries 0-99 are RNC chunks, entries 100-199 point at **raw 26,116-byte levels** (not
compressed), 200+ = file size. 180 distinct levels (some campaign/secret levels are repeated).
TCRF confirms the 200 and that "every level in the development directory was lumped in";
`-level 0..127` (0-39 single player incl. secret, 50-60 multiplayer).

Decompressed level = **26,116 bytes** (`Type_CompressedLevel_2FECE`, remc2 `LevelStructs.h`):

| Offset | Content |
|---|---|
| 0x00 | u16 `02 00` version (TCRF: "2 = second version of the level format") |
| 0x02 | u16 level id |
| 0x04 | u8 graphics-type byte, 0x06 **level type 0 day / 1 night / 2 cave**, 0x07 graphics type |
| 0x0B | player castle level, 0x0C..0x12 castle levels of Nyphur, Rahn, Belix, Jark, Elyssia, Yragore, Prish |
| 0x17.. | map generation parameters (u16 + pad: seed position, seed height, random seed, ... 12 values like MC1's GEN_MAP, "names mostly from GAM00088.DAT") |
| 0x443 | **1200 x 20-byte entities** (MC1: 1999 x 18 at 0x442): u16 class, u16 model, u16 x, u16 y, then 6 x u16 (dis id / switch size / switch id / parent / child / extra) |
| 0x6203 | u8, then 8 x 110-byte per-wizard blocks (MC1: 8 x 0xd8) - spells owned / allowed |
| 0x6574 | 8 x 7-byte stages / checkpoints |
| 0x65AC | 11 x 8-byte records to the end |

Entity class census over all 200 levels (class: total, models): 2 scenery 8,480 (models 0-3, 6-8);
3 player start 533 (4-11 like MC1); 5 creature 28,549 (models 0-4, 9-10, 12-14, 16-28); 10 effect /
terrain 33,981 (same id meanings as MC1 where they overlap: 28 wall, 29 path, 31 canyon, 39 mana
ball, 45 wizard, 50 ridge node, plus new 57-86); 11 switch 3,051 (0-44); **14** (new, models 1-5,
5 = 2,775 - objective/sign/portal markers?) 3,466; **15 = spell urns 1,010 (models 0-25 = the 26
SPELLS.DAT slots)**; class 0 with non-zero models 14,381 (unidentified; possibly a different
record kind). MC1's class 12 "spell" is gone (urns are class 15), class 7 weather is gone.

`mctools.level` therefore needs an MC2 variant (header at 0x17, 20-byte records at 0x443, new
class 14/15 tables, raw entries) - a small change. `rnc`, `dattab`, `sprites`, `tmaps`,
`palette` work unchanged.

### Saves / config

`SAVE/SLEV2.DAT` 224,791 bytes = the whole game-state block (`type_shadow_D41A0_BYTESTR_0`: 1000
pointers + 1000 Things + settings + the level), `SMAP2.DAT` 463,554 = map dump (5 x 65,536 +
2 x 65,536 for the int16 entity map + extras), `SVER2.DAT` = 8-byte version. `CONFIG.DAT` 32 bytes.

---

## 6. TCRF findings

tcrf.net and new.tcrf.net return a Cloudflare-style anti-bot page (with a prompt-injection payload
aimed at LLM fetchers) to every fetch path tried (direct, MediaWiki API, r.jina.ai, archive
proxies). The following is assembled from search-engine snippets of the TCRF pages plus the exe
string dump, which contains the same material:

- **Magic Carpet 2: The Netherworlds** (`https://tcrf.net/Magic_Carpet_2:_The_Netherworlds`):
  30 single-player + 10 multiplayer levels shipped, but LEVELS.DAT holds **200** (every level of the
  dev directory, as in Syndicate Wars). Unused areas, unused graphics, and "a significant amount of
  cut content from the original Magic Carpet finally seeing daylight". **Level select**: `NETHERW
  -level x` (0-127; 0-39 campaign incl. secret, 50-60 multiplayer). `-showversion` prints the build
  stamp ("TESTERS: WRITE DOWN THE ABOVE VERSION DATE AND TIME"), `-showversion2` prints the title
  and exits, `-debug` does nothing in the final. **Build comment** at 0xE6E08: "Magic Carpet 2
  (Netherworlds), Beta, Sep 06 1995 03:42:13, Bullfrog, Alan Wright, Supplied to PUBLIC". **Early
  enemy names** at 0xE6530: Mana Eater (= Moon Dweller), Dark Demon (= Troglodyte), Deep One
  (= Leviathan). Full command-line list from the exe: `network`, `level`, `harddrive`, `skipscreens`,
  `nocd`, `showversion`, `showversion2`, `detectoff`, `langcheck`, `vio`, `extern`, `VFX1`, `cc`,
  `spellsedit`, `music2` (README documents `-harddrive`, `-music2` = MC1 music, `-vio`, `-vfx1`,
  `-detectoff`). **Cheat strings** in the exe ("CHEAT: access all spells / more mana / destroy all
  players / castles / balloons / heal / Kill all creatures / More Spell Experience Points / Free
  Spell Usage ON-OFF / Invincability ON-OFF"), activated (cheatbook.de) by pressing `I`, typing
  `WINDY`, Enter, then Alt+F1..F10 (all spells, mana, kill players, castles, balloons, heal, kill
  creatures, XP, free spells, god mode), Shift+D complete objective, Shift+C complete level.
  Debug overlay strings: "Thing %d, Active %d / Carpet / Tape / Heap / Memory (Used/Free)",
  "CLASS / MODEL / STATE", "LIFE / MAX LIFE", "SPEED ACTUAL / MINIMUM / MAXIMUM", "ACTUAL X / Y / Z",
  "ID / WHO OWNS ME", "MANA ACTUAL / MAXIMUM", "Game turn", "Transfer rate", "FPS" - the same
  Thing inspector our MC1 `debug overlay` has.
- **Notes:Magic Carpet 2** (`https://tcrf.net/Notes:Magic_Carpet_2:_The_Netherworlds`): level
  format "very similar to MC1 at the start, deviating until the later parts are completely
  different"; header `02 00`; +6 level type (Day/Night/Cave), +7 graphics type, +0xB..0x12 castle
  levels; generation parameters from 0x17; "most of this was copied from the MC1 notes and needs
  MC2-specific updating; the REMC2 level editor has most of it figured out". Matches section 5.
- **Proto:Magic Carpet 2** (`https://tcrf.net/Proto:Magic_Carpet_2:_The_Netherworlds`) and
  Hidden Palace (July 13 1995 prototype): the "BASF demo" exe is `CARPET2.EXE`; silver menu
  highlights, no name entry / save / load, no pause menu (Esc quits), different fonts, levels that
  are earlier versions of levels cut from the final. The GOG CD's `LANGUAGE/*.TXT` and `?TEXT.DAT`
  still start with the **demo blurb** ("This demo of Magic Carpet 2 features a playable level and two
  rolling demo levels ... 25 action packed levels ... released at the end of September 1995").
- **Magic Carpet (DOS)** (`https://tcrf.net/Magic_Carpet_(DOS)`, `Notes:`, `Development:`,
  `Prerelease:`): 65 maps shipped, 70 loose; `carpet -debug` has no visible effect; debug text and
  function names exist only in the CD release (removed in floppy / Magic Carpet Plus); unused raven
  familiar sprite (`DRAW_SCALED_RAVEN`); cut weather (wind, thermals, thunder clouds, rain clouds,
  tornadoes - only Wind is placed and does nothing); `GAM00088.DAT` is the raw text form of level
  88 (= level 65) that named our fields; `MAPHACK.EXE` replaces level 1's surface with a 256x256
  image; the standalone **Hidden Worlds** release accidentally shipped `CARPET.MAP` (the Watcom
  link map with every function name - worth obtaining for our naming work) and lists functions of
  two cut spells, **Alliance** (`init_shot_alliance`, `init_effect_alliance`, `init_spell_alliance`,
  `fn_shot_alliance`, `fn_spell_alliance_one`, `fn_spell_alliance_to_all`, `fn_spell_alliance`;
  Alliance shipped in MC2) and **Blindness** (`fn_shot_blindness`, `fn_effect_blindness`). Hidden
  Worlds has 25 levels followed by all the original levels; its exe lacks the level hardcoding and
  plays them all.

Other useful references: SDA mechanics page
`https://kb.speeddemosarchive.com/Magic_Carpet_2/Game_Mechanics_and_Glitches` (mana categories:
monster-bound, free, balloon, castle, building + the 1000 wizard global mana; castle meter is a
density meter of all map mana; balloon soft cap 10,000; F3 fast mode; diagonal movement is faster;
high-res is less stable), `https://github.com/Moburma/MCDatExtractor` (extracts LEVELS.DAT/TAB of
MC1, Hidden Worlds and MC2 - and plays custom levels in REMC2 beyond the 127 table),
`https://github.com/Moburma/MCLevelReader` (MC1 CD only, CSV + PNG of entities).

---

## 7. Licensing / compatibility verdict

- **Data**: the user owns MC2 on GOG; the data track is a plain ISO-9660 inside a BIN, so our
  tooling can read every asset from the user's own copy at run time (no redistribution needed).
  The **container, RNC, sprite, tmaps, palette, sky, block atlas and FLI formats are the same as
  MC1** (verified with mctools on the real files), so MC2 billboards (wyvern, manticore, hydra,
  spider, sentinel, zombie, troglodyte, cymmerian + bugs, devil, moon dweller, leviathan, mana
  worm, the 7 wizards), the night / cavern tile sets, skies and palettes, the 26-spell icon set,
  the hi-res HUD and fonts can be loaded by our engine with: a 3-bank `tmaps` loader, per-level
  palette/colour-table switching, and an MC2 level reader (20-byte entities at 0x443). What is
  **not** reusable without new code: sound (RIFF/XMIDI instead of HMI; the WAVs are trivial, the
  XMIDI songs need an XMI player or conversion), the screen container `HSCREEN0.DAT`, and of
  course the behaviour tables that live in `NETHERW.EXE`.
- **Code**: remc2 / MC2-HD is GPL-3 (repo licence) on top of unlicensed translated Bullfrog
  code. Use it as a **Rosetta stone only** (function naming, struct layouts, Events dispatch,
  Terrain deformation, Spells table semantics); do not copy. Our port already reproduces the
  shared MC1 engine, so MC2 features (towers, Mana Magnet/Lock, Summon Army, Gravity Well,
  Whirlwind, Fool's Mana, Magic Mine, Alliance, Duel, missions/objectives, day/night/cave sets)
  are best re-implemented from the manual + SDA descriptions + our own reading of the MC2 exe if
  needed (it is the same Watcom/DOS4GW toolchain; Ghidra + our cspec would work on it).
- **Design value for Phase 4**: MC2 is essentially "MC1 + RTS verbs": creature production
  (Summon Army), neutral recruitment (Alliance), defensive structures (towers), economy raids
  (Steal Mana / Ransack, Mana Lock, Mana Worms, Moon Dwellers), traps (Magic Mine, Fool's Mana),
  mobility (Castle Port, Duel), map intel (Beyond Sight tiers, invisibility counters) and a
  mission/objective system with checkpoints - a near-complete feature list for a battle-royale /
  RTS hybrid on the MC1 engine.

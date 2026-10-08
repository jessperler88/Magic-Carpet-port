# carpet.exe engine notes

Addresses are for the retail `carpet.exe` (LE objects: code 0x10000, data
0x90000). Names follow `ghidra/names/carpet_names.csv`; the suffix is the
original address so every name is traceable. Decompiled C lives in
`ghidra/export/carpet_all.c`.

The later sections correct the earlier ones: the newest merge ("Port round 5 corrections (merged in
round 6)" at the end) wins over "Port round 4 corrections (merged in round 5)", which wins over everything
above it. Superseded statements are kept and marked *Corrected (round N merge)* in place.

## Binary layout

| Range | Content |
|---|---|
| 0x10000 - ~0x5E000 | Game code (Bullfrog). 1,714 functions in the whole code object after the gap-finder, handler-table, fragment-merge and 2026-10-06 cleanup passes (Ghidra's own pass found 930). |
| ~0x5E000 - 0x7B9EE | Watcom C runtime, DOS/4GW glue, HMI sound driver interface (`hmidrv.386`/`hmidet.386` strings at 0x936D4), VESA/VGA code. CRT entry at 0x624C8. |
| 0x90000 - 0x9xxxx | Strings and initialised data. Credits text at 0x92508+. |
| 0x9E000+ | Globals (file name tables at 0x9E73C, sound state 0x9E30C..0x9E321). |
| 0xA0000+ | BSS: big tables (0xB99B0 tables.dat image, 0xBD9B0 blend table, 0xCDCB0 256-entry table). |

## Data embedded in the code object

Function coverage after the gap-finder, handler-table and fragment-merge
passes: 2,083 functions, 298 KB of the 320 KB game region (0x10000-0x5E000);
the remainder is data placed in the code segment (below). (The span fillers and the asm
ISRs are functions since the 2026-10-06 cleanup pass, see "Rasteriser" below.)
What remains unclassified is almost entirely data that Watcom placed in the
code segment:

| Where | Content |
|---|---|
| scattered (e.g. 0x2ACE8, 0x2C4CC, 0x32109, 0x3A80E, 0x49661, 0x4CF6E) | Switch jump tables: runs of dword code addresses right after a `lea`-nop pad. |
| 0x510CA - 0x51EA8 | Front-end strings (`data\screens\*.dat`, `intro\*.dat`), 3.5 KB. |
| 0x17C87, 0x42953 | Small zero-filled / pointer blocks. |
| 0x70000 - 0x7B9EE | Watcom CRT / DOS/4GW glue, except 0x71E00-0x78DD5 which is game code Watcom placed late: terrain fractal, texture averages and the 25 KB triangle rasteriser `poly_fill_triangle_722e3` (0x722E3-0x78DD5; the formerly "unreached" 0x74000-0x78000 block is its span fillers). |

A handful of 100-600 byte code runs inside the game region (0x112CB, 0x196C9,
0x25C2F, 0x342DF, 0x455A2) are switch-case bodies whose jump table the
analyzer did not resolve; `FindGapFunctions.java` now follows jump-table runs.

Watcom pads between functions with `lea` no-ops (`8D 80 00 00 00 00`,
`8D 92 00 00 00 00`, `8D 40 00`, ...) and occasionally `90`; the gap finder
recognises these.

## Startup and main loop (from decompilation)

```
CRT start (0x624C8)
  -> main_3d0a0                      (FUN_0003d0a0)
       init_early_3c800 x3
       game_main_32a00
            config_parse_33750       returns -1 to abort
            data_load_all_334c0      "Load all data files", builds 16x16x16 RGB->palette cube
            [if !(cfg->flags & 8)]   cfg = *DAT_000adf74 (config/flags struct)
              FUN_0003cf20, FUN_0004bd10, FUN_00059500, FUN_0003ed60   (init: tmaps, video, memory, mouse?)
              FUN_00051ed0 or (network/demo path FUN_0005ae80, FUN_0003ed30)
              loop while !game_over_flag:
                  timer_sync_61510
                  [front end]  FUN_00030350, FUN_00059760/59860 (video mode), frontend_menu_loop_52070 until DAT_0009e504
                  level_load_and_init_3d3b0
                      level_load_file_3d160          "Load Level"
                      terrain_generate_map_303f0     "Generate map"
                      terrain_generate_features_34db0 "Generate features"
                      models_initialise_354c0        "Initialise Models" (mem_clear_5afd0 is memset)
                  in-level loop while !level_over:
                      [multiplayer: random event every N ticks FUN_0005c0a0]
                      FUN_0004a000
                      game_tick_32f90                 (simulation + render; reaches 368 functions)
                      FUN_0005c040, FUN_0001f960
                      timer_sync_61510
                      state machine on player->status (offset 0x340D in the per-player block)
                  level_finish_3d4e0
              FUN_00034220
            FUN_000335e0, FUN_0004efc0 (shutdown)
```
*Corrected (round 6 merge): the in-level part of this sketch has the wrong shape: the per-tick loop is inside `game_tick_32f90`, the status test runs once after it, `timer_sync_61510` is the palette fade, and `level_finish_3d4e0` is the level *restart* (now `level_restart_3d4e0`); the music track is picked per (re)start from a 16-bit LCG of Config.level. see "Port round 5 corrections" at the end.*

Key globals:

- `DAT_000adf74` - pointer to the config/flags struct. Bit 8 = skip game
  (`FUN_00023766` path, probably "play intro only"), bit 0x100 = network/demo,
  bit 4, bit 0x10, bit 0x48 = sound disabled, byte +0x17 = "data loaded",
  word +0x11 = current level number, byte +0x97 = sound card type.
- `DAT_000adf6c` - pointer to the game-state block. Per-player records of
  size 0x801 start at +0x340F (indexed by `*(short*)(state+8)` = local player).
  Player status word at +0x340D (bits 2/4/8 = level-end states). *Corrected (round 6 merge): 2 = won (Space leaves), 8 = leave the tick loop, 4 (with 8, without 2) = lost -> restart in place; see "Port round 5 corrections" at the end.*
- `DAT_0009e504` - "leave front end" flag; `DAT_0012edae` - video-mode flags (bit 0 = 320x200, UI coordinates are halved; bit 3 = 640x480).
- `dbg_progress_4b540` is called with a message string before each phase; it
  is the best hook for tracing initialisation order.

## Data loading

- `file_load_rnc_3cbe0(path, dest)` - loads and RNC-decompresses a file,
  prints "ERROR decompressing %s" on failure.
- `tables_load_or_generate_3eaa0` - `data/tables.dat` (83,456 bytes) or
  computes: `0x100`-stride blend tables (`FUN_0004cb88`, 0x20 + 0x20 + 0x100
  rows), a 256-entry table from `FUN_0004cd7a(i*i, 0x10000 - i*i)` (looks like
  sqrt(1 - x^2) in 8.8 fixed point, i.e. a sphere/circle profile).
- `tmaps_load_4b580` - loads `data/tmaps.dat` via the "BULLFROG" + RNC chunk
  container; errors "ERROR decompressing tmap%03d".
- Sound: `data/snds%d-%d.dat/.tab` (13 referencing sites) and
  `data/music%d-%d.dat/.tab`; HMI driver error strings at 0x934EC..0x9367A.

## Open questions from the first pass (resolved 2026-10-06)

1. The landscape renderer is `render_landscape_29050` (polygon grid, not voxel);
   `FUN_0002ad60` is `render_sprite_scaled_2ad60`, `FUN_00028580` builds the screen-roll
   table. See "Renderer" below.
2. `FUN_0001fab0` is `render_frame_1fab0` (frame composition + HUD), not entity update;
   entity update is `thing_update_all_3dce0` dispatching through the class/model tables.
3. The zero-caller functions were handler-table targets and switch-case fragments; the
   handler tables are documented in the next section, the fragments have been merged.

Remaining open items are collected per subsystem in the "Open questions" subsections of
the agent-pass sections below and summarised under Phase 1 in `docs/ROADMAP.md`.

## Thing (entity) system: class/model dispatch tables (established 2026-10-06)

Things live in a pool inside the game-state block (`DAT_000adf6c`): records of
0xa4 (164) bytes starting at state+0x7463; slot 0 is unused, so the per-tick
loop walks state+0x7507 .. state+0x2f503 (999 usable slots, "1000 things").
Known Thing fields so far: +0x08 (set to 300 at alloc), +0x10 flags (bit 0x80
= not audible/positional), +0x18 u16 own slot index, +0x2c (100 at alloc),
+0x3f u8 tick counter (incremented after each handler call), +0x40 u8 class,
+0x41 u8, +0x42/+0x43 u8 = 0xff at alloc, +0x44 u8 = 10, +0x41 u8 *type* (= level-file Model, indexes Table B), +0x46 u8 *state*
(indexes Table A, see below), +0x48 position block used by the
sound code, +0x4e..+0x54 four u16 half-extents, +0x56 u16 sprite id, +0x58,
+0x59 u8 from table 0x94344, +0x7e u16 = 0x10, +0x9c ptr = &DAT_00096a10,
+0xa0 ptr = &DAT_000b6e90.

- `thing_alloc` = `FUN_00035560`: pops a free slot from the int array at
  state+0x251 (count at state+0x28); when that list is empty it decrements the
  counter at state+0x11f1 and reuses a slot after `FUN_0003e330`; zero-fills
  0xa4 bytes and writes the defaults above. Returns the Thing in EBX.
- `FUN_00035240(thing, sprite)`: sets +0x56 = sprite, +0x58 = 0, +0x59 from
  `(&DAT_00094344)[(&DAT_00097684)[sprite*14]]`, and the four half-extents from
  the 14-byte sprite-dimension records at 0x9767e/0x97680.

### The dispatch tables (data object, 0x943c8 ..)

A tree of records that all point back to a root node at 0x943cc. The class
table is indexed with base `0x943da` and stride 0x12 (18):

| offset | field |
|---|---|
| +0 | pointer to **Table A** (per-tick update handlers, indexed by Thing state, +0x46) |
| +4 | pointer to **Table B** (constructors, indexed by Thing type = level-file *Model*, +0x41) |
| +8 | parent pointer (0x943cc) |
| +12 | u16 ordinal (= class + 1) |
| +14 | u32 0 |

Both tables hold 14-byte records `{u32 parent = 0x943cc; u16 index; u32 handler;
u32 enabled}`; a record is only used when `index == requested` and `enabled`.
Table B record counts match the model lists recovered from the level files
(`tools/mctools/level.py`) exactly, which pins the class numbering:

| class | name | Table A (update) | Table B (create) |
|---|---|---|---|
| 1 | (unused) | 2 | 11 |
| 2 | Scenery | 18 | 6 |
| 3 | Player / flyers | 11 | 12 |
| 4 | - | 0 | 0 |
| 5 | Creature | 121 | 17 |
| 6 | ? | 2 | 2 |
| 7 | Weather | 5 (all `ret`) | 5 |
| 8 | ? | 6 | 6 |
| 9 | ? | 21 | 20 |
| 10 | Effect | 62 | 57 |
| 11 | Switch | 32 | 32 |
| 12 | Spell | 72 = 24 spells x 3 states | 24 |
| 13 | ? | 4 | 4 |

Table A is a concatenation of per-type state machines. Spells: state =
`spell*3 + phase`, phases 1 and 2 share 0x472f0 / 0x47300 for every spell.
Scenery: state = type*3. Effects: state == type up to 0x13, then type + 2.
Projectiles (class 9) and switches: state == type. Creatures: Dragon starts at
state 1, Vulture 7, Bee 13, Worm 19, Archer 25, Crab 31, Kraken 37, Troll 43,
Griffon 49, Skeleton 54, Emu 61, Genie 66, Builder 73, Townie 79, Trader 85,
type 15 at 91, Wyvern 97, and state 120 is the body-segment handler (0x18050)
used by dragon/worm/kraken segments. The constructors (Table B) set the
initial state, health and sprite (e.g. `creature_create_dragon_362d0`: state 1,
health 9000, sprite 0x13 plus 16 linked segments). All constructors live together at 0x359c0-0x3a7e0 (one source
file), all creature update handlers at 0x19b70-0x1f6b0, effects at
0x238b0-0x284c0, spells at 0x47130-0x492e0, switches at 0x4a2a0-0x4a8a0. *Corrected (round 5 merge): states are `type * 6 + {0 idle, 1 main, 2 attack, 3 follow, 4 dying, 5 dead}` (Dragon 0, Vulture 6, ... Wyvern 96); projectile types 14..19 run states 15..20 (14 = lightning segment); class 10 is state == type only up to 0x1a, then see the state -> type map; switches are state == type; see "Port round 4 corrections" at the end.*

Names for all 435 table handlers are generated from these tables
(`ghidra/names/carpet_names_tables.csv`, regenerated by the gennames step):
`<class>_create_<model>_<addr>` for Table B and `<class>_<label>_s<state>_update_<addr>`
for Table A; hand-curated names in `carpet_names.csv` override them. Since round 5 `tools/analysis/gennames.py` maps states to types correctly (creatures `state / 6`, class 9 / 10 / 3 by explicit maps) and `carpet_names.csv` gives the descriptive names (`docs/analysis/names_round5.md`).

### Who uses the tables

- `FUN_0003dce0` (called 1, 4 or 16 times per tick depending on cfg+0x96):
  walks the pool and, for every Thing with class != 0, calls
  `A[class][state].handler` with the Thing in EBX, then increments +0x3f.
- `FUN_00035690(class, model)` (76 callers): calls `B[class][model].handler`
  = "create a Thing of this class/model".
- `00035800`: creates Things from level THING_INIT records: checks
  `B[Class][Model].enabled`, converts cell coordinates with
  `x*0x100 + 0x80` (so a map cell is 256 world units and Things are placed
  at the cell centre), then calls `FUN_00010bc0` and `FUN_00035690`.
- `level_run_terrain_effects_34fa0`: at level start runs the update handlers of class-10
  things of types 9..0xb, 0x1b..0x20, 0x2d, 0x32, 0x33 (terrain-shaping effects) until none remain.


## Renderer (agent pass, 2026-10-06)
### 2.1 Frame flow

```
game_tick_32f90 -> FUN_00032e80
   ... simulation ...
   render_frame_1fab0                      (view mode = player byte +0x3855)
       render_set_view_window_2f3c0(size)  (size = state+0x2198, 0x28 = full; F5/F6/F7 shrink it)
       render_view_2f6e0(cam...)           (stereo/SIRDS/mono selection, post filters)
           render_landscape_29050(cam_x, cam_y, yaw, cam_z, pitch, roll, zoom)   1x or 2x
               render_build_roll_table_28580(roll)
               render_sky_2f080(roll)      or clear
               [per cell] poly_fill_triangle_722e3 x2, then render_cell_things_2c600
                                                     -> render_sprite_scaled_2ad60
       HUD: ui_draw_radar_43610, ui_draw_radar_blips_42a20, ui_draw_status_bars_219f0,
            ui_draw_thing_label_22870, ui_draw_player_list_21370, ui_draw_text_4a9a0 ...
   FUN_0004ad80, FUN_0003ca00 (screenshot/dump when DAT_00094338)
   vga_present_frame_2f480                 (copy DAT_0012ed74 -> 0xA0000)
```

Camera arguments are pushed on the stack by render_frame_1fab0 (Watcom stack args;
Ghidra dropped the pushes, they appear as `in_stack_000000xx` in 2f6e0/29050).
Order seen in render_landscape_29050: +4 cam_x, +8 cam_y, +0xc yaw, +0x10 cam_z,
+0x14 pitch (horizon offset), +0x18 roll, +0x1c zoom/focal scale.

### 2.2 Video / frame buffer globals

| Global | Meaning |
|---|---|
| DAT_0012ed74 | 8-bit back buffer (whole screen). Cleared with mem_set_5afd0(w*h). |
| DAT_0012ed70 / DAT_0012ed78 | screen width / height (0x140 or 0x280; render_view checks `== 0x280`) |
| DAT_0012edae | video-mode flags: bit0 = 320x200 (UI coordinates are in 640x400 space and get halved: 6070d, 6abbc, 2f3c0), bit3 = 640x480. (ENGINE.md calls this "game mode"; the renderer code treats it as resolution.) |
| DAT_0012ed80/ed88/ed90/ed98/eda4/eda8 | 2D clip rectangle used by the span blitter / rect fill |
| DAT_000adf70 | second full-screen buffer: right-eye image for anaglyph/SIRDS, previous frame for the +0x219c blend. Allocated with FUN_00059870(64000) in FUN_00033600 (0 when not enough memory, which disables the 'D' key modes). |
| DAT_000adf68 | 64 KB+ work buffer: terrain vertex grid (840 x 44 = 0x9060 bytes), then sprite column table (+0x9060), per-row clip triples (+0xb360), roll index list (+0xe7e0). Also reused by the front end as a swap buffer and by 3bfc0 as a file buffer. Set by render_set_buffers_57331. |
| DAT_0009b5f4 / f8 / fc / 600 / 604 | current render target: dest, texture, pitch, width, height (render_set_viewport_78dd5). DAT_0009b5f0 = dest - pitch. |
| DAT_000b5810 | byte offset of the reduced view window inside the frame |
| DAT_0009e5d4 | "blit in progress" (mouse cursor lock) |
| DAT_0009e5dc / de | mouse x / y (640-space) |
| DAT_00093f74 | 16-bit colour anaglyph mode active |

VRAM output: 320x200 -> rep movsd 64000 bytes (vga_copy_320x200_610f0); VESA -> 64 KB banks via
int 10h 4F05 (vesa_set_bank_6126c, granularity at ram 0x12ef02); interlaced copy for shutter glasses
(7946d); hi-colour anaglyph through two word tables (793b0).

### 2.3 Map representation (5 parallel 256x256 maps, 0x10000 stride)

| Address | Element | Content (evidence) |
|---|---|---|
| 0xCDFB0 | byte | terrain texture index per cell -> vertex+0x29; DAT_0009afec[idx] = texture pointer (set in tables_load_or_generate_3eaa0); property tables DAT_00093a78 (water/translucent -> fill mode 0x1a), DAT_000939d4 (flat-shaded -> mode 7), DAT_00093930 (-> vertex+0x2b; non-zero suppresses shadows) |
| 0xDDFB0 | byte | height; world height = byte * 0x20 (29050: `h*0x20 - cam_z`; 71e00) |
| 0xEDFB0 | byte | light/shade per vertex: `light*0x100 + 0x80` then scaled by fog -> vertex+0x20 |
| 0xFDFB0 | byte | flags: bit 3 (0x08) = animated (water) - vertex height gets `-(sin(tick*0x40+x*0x80)*sin(..+y*0x80))>>10` and the shade gets the wave term; bits 2..4 (>>2 & 0x1c) = texture rotation/flip selector combined with the view quadrant -> vertex+0x2a -> UV set DAT_00093b48[sel*8] |
| 0x10DFB0 | u16 | index of the first Thing in the cell (0 = none) -> vertex+0x24; list continues through thing+0x14. The same 128 KB is used as the 16-bit scratch for the diamond-square generator (terrain_fractal_fill_71f08). |

Cell index = `(y_cell << 8) | x_cell` (CONCAT11 everywhere); world coordinates are 16-bit with the
cell in the high byte and the fraction in the low byte (1 cell = 256 units; map wraps at 256 cells).
Camera cell = cam >> 8 (with -1 rounding when fraction < 0x80 in render_view).

### 2.4 Camera, angles, trig

* Angles are 11-bit: 0..0x7ff = full circle (`& 0x7ff` everywhere). Sine table of 32-bit 16.16
  values in the data segment at **0x987EC** (sin(a) = `*(int*)(0x987ec + a*4)`), cos(a) = entry +0x800
  (0x98FEC). 0x983EC (= -256) and 0x98BEC (= +256) are used as sin(a-45deg)/sin(a+45deg)
  (render_landscape uses `yaw + 0x100` with those two, i.e. plain cos/sin of yaw for DAT_000b5864/58a0).
  The table therefore spans at least 0x983EC..0x9AFEC (2816 entries). No code writes it.
* Camera globals written by render_landscape_29050: DAT_000b58ac cam_x, DAT_000b58aa cam_y,
  DAT_000b58a8 yaw, DAT_000b5884 cam_z, DAT_000b5864 cos(yaw), DAT_000b58a0 sin(yaw),
  DAT_000b585c sin(roll), DAT_000b5870 cos(roll), DAT_000b5894 = width/2 + eye offset
  (DAT_00093b1c), DAT_000b586c = height/2, DAT_000b588c = horizon offset = pitch*width>>8,
  DAT_000b5854 = focal = sqrt(w^2+h^2) * zoom >> 8 (FUN_0004cd7a = isqrt), DAT_000b58af = shadows on.
* Projection: `sx = x_rot * focal / z`, `sy = (h - cam_z) * focal / z + horizon`, z clamped >= 0x80;
  then screen roll: `x' = cx + (x*cos - y*sin)>>16`, `y' = cy - (x*sin + y*cos)>>16`.
* Player tick counter player+0x341d drives water animation (`*0x40` phase).

### 2.5 Landscape algorithm (render_landscape_29050) and the draw-distance limits

1. Quadrant `q = ((yaw+0x100) >> 9) & 3`; angle inside quadrant `a' = ((yaw+0x100)&0x1ff)-0x100`;
   `local_64/68 = sin/cos(a')`. Per-quadrant 10-byte table at **DAT_00093b20** (+0/+1 start cell
   offset, +2/+3 texture-corner offset, +4/+5 thing-cell offset, +6/+7 row step, +8/+9 column step).
2. Vertex grid: **40 columns x 21 rows** (0x28 x 0x15 = 0x348 = 840 records of **44 bytes**) at
   DAT_000adf68. Column coordinate runs -0x1300..+0x1400 (cells -19..+20 sideways), row coordinate
   -0x100..+0x1400 (cells -1..+20 forward), both in 1/256 cell. Rotated by a' into camera space:
   rec+0 = x_cam, rec+0xc = z_cam.
3. Per vertex: `d2 = x^2 + z^2`; culled (flag 0x02) when `z < -0xff` or `d2 >= DAT_000b584c
   (0x1900000 => 5120 units = 20 cells)`. Fog: full light below DAT_000b5848 (0xE10000 => 15 cells),
   linear fade to 0 at DAT_000b5850 (0x1690000 => 19 cells), divisor DAT_000b5844 (0x880000).
   The same four constants are used for Things in render_cell_things (plus near cull z < 0x41).
4. Flags byte rec+0x26: bit0 = (cx+cy)&1 (which diagonal splits the quad), bit1 = culled, bit2 =
   column is on the far side of the view axis (set when the lateral coordinate >= 0; the row loop
   draws columns left-to-centre then right-to-centre for painter's order), bits 3..6 = off-screen
   left/right/top/bottom, bit7 = translucent texture; rec+0x27 bits 0..3 = second surface
   off-screen, bit4 = flat-shaded texture.
5. Draw loop: rows from far (row 20) to near, 39 quads per row; each quad = 2 triangles via
   poly_fill_triangle_722e3 with (x,y,shade) triples, `DAT_0009e309` = 5/7/0x1a, texture pointer
   `DAT_0009b5f8 = DAT_0009afec[tex]`, UVs from `DAT_00093b48[sel*8..+7]`; then the Thing list of
   the cell (rec+0x24 != 0).
6. Second surface (state+0x2195, key '?'; also when cam_z < 0x1000): rec+0x8 = mirrored height
   `-cam_z - ((wave>>4 + 0x8000) * h >> 10)`, projected to rec+0x18/0x1c and drawn with
   render_cell_things_mirrored_2e5a0 - a ceiling / water-reflection pass.

**To raise the draw distance in a port**: enlarge the grid (0x28/0x15 counts, the 0x1b8-int row
stride = 40 records, 0x2260-int start = row 20, the -0x1300/-0x100 start offsets, the 0x348 loop
count), the DAT_000adf68 layout (the sprite tables start at +0x9060 right after 840 records), and the
four squared-distance constants (0x1900000 / 0x1690000 / 0xE10000 / 0x880000, also the SIRDS depth
scale `0x1400 - z` and the thing cull in 2c600/2dac0/2e5a0). Everything else is data driven.

Vertex record (44 bytes): +0 x_cam, +4 height-cam_z, +8 mirrored height, +0xc z_cam, +0x10 screen x,
+0x14 screen y, +0x18/+0x1c screen x/y of mirrored vertex, +0x20 shade (light<<16 scaled by fog,
plus wave term), +0x24 u16 first thing index, +0x26 flags, +0x27 flags2, +0x29 texture, +0x2a UV
selector, +0x2b texture property.

### 2.6 Things as seen by the renderer

Thing pool: record i at `state + 0x7463 + i*0xa4` (so the first real record, i=1, is at +0x7507).
Fields used: +0x10 flags (0x21 = invisible/hidden, 0x80 = silent), +0x14 next thing in cell, +0x1e
facing angle, +0x48/+0x4a/+0x4c x/y/z (shorts), +0x56 sprite type (index into DAT_00097678), +0x58
animation frame, +0xa0 pointer to owner/player data.
Sprite type table DAT_00097678: 14-byte records: +0 u16 base sprite index, +8 u16 world size,
+0xa byte shade-group (indexes DAT_00093f48 lit / DAT_00093f4e fogged -> pixel mode; non-zero in
DAT_00097682 also means "no shadow"), +0xc byte draw type (see 2c600 in the CSV).
Sprite cache: DAT_000b8d3c[n] = pointer to pointer to sprite (header +0 flags (bit3 = used this
frame), +2 w, +4 h, +6 pixels, raw w*h bytes, 0 = transparent) for 0x211 sprites; DAT_000b84f4 =
tmaps directory (10 bytes/entry, +4 file offset, +8 group id); DAT_000b7cb0[group] = last-use frame
counter for LRU eviction; DAT_000b9580[group] = locked.
Note: 3D sprites are **not** span encoded (render_sprite_scaled walks raw w*h pixels with 0 =
transparent). The span encoding (n>0 copy, n<0 skip, 0 = end of row) is the 2D HUD/font format
handled by vga_draw_sprite_spans_6070d.

### 2.7 Tables

* 0xBD9B0 blend table: `blend[a<<8 | b]` 256x256 (render_view smoothing/motion blur, sprite modes
  2..7, HUD translucency, radar).
* 0xB99B0 shade table: `shade[level<<8 | colour]`, level 0..0x20 (sprite mode 1/6/7, ui_shade_rect,
  radar lighting). This is the start of the tables.dat image.
* 0xCDCB0 256-entry circle profile (sqrt(1-x^2) 8.8): row half-widths of the circular radar
  (ui_draw_radar_43610 `DAT_000cdcb0[i] * radius >> 8`).
* 0xCD9B0 (just below): per-texture colour used by the overview map / radar (`DAT_000cd9b0[tex]`),
  probably produced by texture_average_colours_72147.
* DAT_00093b48: 32 x 8 dwords texture UV corner table (16.16), rescaled by render_set_texture_uv_scale_284f0.
* DAT_000b2e10..DAT_000b3510: 8 x 256 anaglyph colour tables built by stereo_mode_enter_2ff50.
* DAT_000b3a10: roll line table (3 dwords per screen column/row) built per frame by 28580.

### 2.8 Render option bytes in the game-state block

+0x2195 second surface ('?'), +0x2196 shadows ('A'), +0x2197 textured sky (set with 2195 from
cfg byte +8 in config_parse_33750), +0x2198 view window size (0x28 = full), +0x2199/+0x219a HUD
parts, +0x219b 3D mode (0 off, 1 anaglyph, 2 SIRDS; key 'D'), +0x219c motion-blur/ghost blend (set
automatically when the player's thing speed |+0x7e| > 0x50 in single-player), +0x219d 2x2 smoothing
('>'), +0x219e interlaced stereo (set by config_parse when a stereo driver is detected, and by
FUN_00059330), +0x21ad..0x21b7 "option available" bytes (all 1 at startup).

### 2.9 FUN_0003a8b0 verdict

Not part of the renderer and not camera setup: it is the per-player command/message processor
(network and demo playback) - join/leave/name/chat/spell-slot commands from the 10-byte records at
state+0x7413, plus the frame counters. Its callees are sound (49720), network sync (4f360/4f530),
demo recording (3c540), chat (3bb50/3bbd0), position log (3e080) and state reload (3c200).

### 2.10 Open questions

1. (resolved) The span fillers are the tail of poly_fill_triangle_722e3, see "Rasteriser"; mode 0x1a is
   shaded texture with depth-limited blend (water), mode 1 is a colour-index ramp (SIRDS depth).
2. Which segment the sky texture (render_sky_2f080) and the SIRDS pattern (0x24080 + ...) are
   addressed through (FS/GS base); the C export shows them as near pointers.
3. Exact meaning of the view-mode-4 screen and of player bytes +0x2199/+0x219a.
4. What writes DAT_00093b20 (per-quadrant step table) and DAT_00093930/0x939d4/0x93a78 texture
   property tables - probably static data; check in the data segment.
5. mem_set_5afd0 contradicts the existing name models_initialise_5afd0 ("Initialise Models" is
   printed by its caller, not by it) - rename.


## Things: update, spatial index, spawning (agent pass, 2026-10-06)
### 2.1 Thing pool and indices

- Pool: 1000 records of 0xa4 bytes. `thing_base = state+0x7463` is the index-0 sentinel; thing index i is at
  `state+0x7463 + i*0xa4`, so the first real thing (index 1) is at state+0x7507 and the pool ends at state+0x2f503.
  All inter-thing links are u16 indices relative to this base; index 0 = none. Loops of the form
  `p = base + idx*0xa4; while (p != base)` are index-list walks.
- Free stack: `state+0x28` = top (int, -1 when empty), `state+0x251` = int[1000] of thing pointers.
  `thing_pool_reset_35460` pushes indices 1000..1 so index 1 is allocated first.
  Second "recyclable" stack `state+0x11f1`/`state+0x11f5` holds live things with flags 0x20400; `thing_alloc_35560`
  frees one of those when the free stack is empty.
- `thing_mark_delete_3e3e0` only sets flag 0x400; `thing_update_all_3dce0` frees flagged things at the start of the
  next tick (handlers can still reference them within the tick).
- Per-tick lists (singly linked via +0 `next`, terminated by `state+0x7463`), rebuilt each tick in
  `thing_update_all_3dce0`, heads in the config block `DAT_000adf74`:
  - `cfg+0x8e1e + type*4` (20 heads): creatures (class 5) by creature type (+0x41), excluding segments (+0x46==120)
    and dead ones (+0xc<0).
  - `cfg+0x8e6e`: players (class 3, alive, flag 0x10 clear).
  - `cfg+0x8e72`: effects type 0x27/0x28 (mana balls). `cfg+0x8e76`: effects type 0x2d (wizards).
  - `cfg+0x8e7a`: class 9 (projectiles).

### 2.2 Thing struct (0xa4 bytes) - recovered offsets

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | ptr | next in per-tick class list | 3dce0 list building |
| +0x04 | u32 | per-thing RNG seed / unique id (`x*0x24a1+0x24df` LCG) | alloc: index + state RNG; handlers advance it |
| +0x08 | i32 | max health (300 default; dragon 9000, castle 40000, player 10000) | creates, 35080 |
| +0x0c | i32 | health / remaining life (effects use it as lifetime; <0 = dead) | 18870, 238b0, 405f0 |
| +0x10 | u32 | flags: 1 ?, 2 initialised/impact done, 4 linked in cell map, 8 collidable (default), 0x10 skip player list, 0x20 dead, 0x40 mana ball being collected, 0x80 sound/shield, 0x100/0x200 projectile hand side, 0x400 marked for delete, 0x2000 processed (class 9), 0x10000 no area damage, 0x20000 recyclable, 0x40000 spell dropped | 3e3e0, 3e250, 105f0, 42530, 354c0, 405f0 *Corrected (round 5 merge): bit 0 = hidden / not drawn; 0x80 on a spell = speed sound; 0x4000 shield, 0x8000 rebound; 0x10000 is tested but has no effect; see "Port round 4 corrections" at the end.* |
| +0x14 | u16 | next thing index in same cell | 3e250/3e330 |
| +0x16 | u16 | prev thing index in same cell | 3e330 |
| +0x18 | u16 | owner thing index (self for independent things; projectiles/effects copy the owner's) | alloc, 43ba0, 23dc0, 44fc0 |
| +0x1a | i16 | per-type: z velocity (creatures 19c30), timer (effects/switch), castle level (+0x1a>0 = castle), spell aim byte | 19c30, 238b0, 4a350, 42530 *Corrected (round 5 merge): the projectile aim byte is only clamped, never read; see "Port round 4 corrections" at the end.* |
| +0x1c | u16 | property flags: bit0 damageable/solid, bit1 castle/wizard; castle 0x21, dragon 1 | 35bbc, 35090, 11160 |
| +0x1e | u16 | yaw (0..0x7ff, 2048 units per turn) | 3e420, 181e0 |
| +0x20 | u16 | pitch | 3e420, 47130 |
| +0x22 | u16 | target yaw | 1a0e0, 43f30 |
| +0x24 | u16 | target pitch | 448b0 |
| +0x26 | u16 | killer thing index (set when health<0) | 18870, 42770, 3da30 |
| +0x28 | u16 | last attacker thing index | 18870 |
| +0x2a | u16 | caster thing index (spell things) | 47130, 22870 |
| +0x2c | u16 | damage value (100 default, 2000 wizard, 0x7d fireball) | alloc, 3a210, 44fc0 |
| +0x2e | i16 | z velocity (player fall) / random jitter | 405f0, 238b0 |
| +0x30 | i16 | spell: remaining cast ticks; creature: distance for sound | 47130, 46960 *Corrected (round 5 merge): creature: `isqrt(dy^2)` written by 46960 (caller bug), read only by the debug overlay; see "Port round 4 corrections" at the end.* |
| +0x32 | i16 | spell: total duration/levels; castle: hit timer | 3a210, 11450 |
| +0x34 | u16 | parent / leader thing index (segments -> head; flocking) | 362d0, 18050, 1a0e0 |
| +0x36 | u16 | child / next segment thing index | 362d0, 46960, 18870 |
| +0x38 | u16 | speed (0x60 creatures) | 362d0 |
| +0x3a | u8 | anim/sound countdown timer | 362d0, 46960, 18870 *Corrected (round 5 merge): awake gate, reloaded near the local player by 46960; see "Port round 4 corrections" at the end.* |
| +0x3b | u8 | secondary timer | 46960 |
| +0x3c/+0x3d/+0x3e | u8 | spell flags / burst counter | 3a210, 47130 |
| +0x3f | u8 | tick counter (++ after each handler call; set from per-type counter at create) | 3dce0, 1a0e0 (anim phase) |
| +0x40 | u8 | class (0 free, 2 scenery, 3 player, 5 creature, 7 weather, 9 projectile, 10 effect, 11 switch, 12 spell) | dispatch |
| +0x41 | u8 | type = level-file Model (creature type, spell id, effect type, player type) | creates |
| +0x42 | u8 | collision filter class (0xff any) | 105f0 |
| +0x43 | u8 | collision filter type (0xff any) | 105f0 |
| +0x44 | u8 | 10 default | alloc, 47130 |
| +0x45 | u8 | 0 | 47130 |
| +0x46 | u8 | state = dispatch index into class table A (handler per state) | 3dce0, 3ea50 |
| +0x47 | u8 | castle size index (wizard/castle effects) | 35090, 26680 |
| +0x48 | u16 | pos x (world units; cell = x>>8, 256x256 cells) | 3e250 |
| +0x4a | u16 | pos y | 3e250 |
| +0x4c | i16 | pos z (height) | 3e250, 19c30 |
| +0x4e | i16 | bbox half extent z / ground offset | 35240, 43ea0, 10530 |
| +0x50 | i16 | bbox half extent x (also area-damage radius) | 35240, 10d20 |
| +0x52 | i16 | bbox half extent y | 35240 |
| +0x54 | i16 | bbox half height | 35240, 47130 |
| +0x56 | u16 | sprite id (incremented for animation by some effects) | 35240, 24100 |
| +0x58 | u8 | current frame | 3ea70 |
| +0x59 | u8 | frame count / palette index from sprite table | 35240 |
| +0x5a | i32 | pending damage | 10d20, 18870 |
| +0x5e | u16 | pending damage source (thing index) | 10d20, 18870 |
| +0x60/+0x64 | i32/u16 | mana ball: pending owner change | 25980 |
| +0x76 | u16 | mana ball: pusher thing index | 25980 |
| +0x7e | i16 | speed (0x10 default; dragon 0x1e; smoke rise speed; player speed) | alloc, 24100, 47420 |
| +0x80 | i16 | base speed / castle area | 47420, 35090 |
| +0x82 | u16 | 0x10 (turn rate?) | 362d0 |
| +0x84 | i32 | spell: mana cost (negative while charging) | 3a210, 46e70, 46f20 |
| +0x88 | i32 | total mana (castle/owner accumulator; spell: total mana) | 427d0, 3a210 |
| +0x8c | i32 | mana carried / mana per level | 35230, 428e0, 26120 |
| +0x90 | u16 | mana owner thing index (0 = unowned) | 428e0, 26120, 10730 |
| +0x92 | u16 | target thing index | 1a830, 448b0, 42530 |
| +0x96..+0x9b | u16 x3 | home/origin position (castle: cell-aligned; projectile: launch pos) | 35bbc, 47130, 42010 |
| +0x9c | ptr | sprite/creature descriptor (default DAT_00096a10; dragon DAT_00096b90, worm 96bf0 ...; fields +0xa/+0xc height clearance, +0x10, +0x14 terrain mask, +0x1a anim period, +0x1c sight radius, +0x1e fov) | 362d0, 181e0, 1a0e0 *Corrected (round 5 merge): +0xa = maximum / cruise height, +0xc = minimum clearance above ground; +0x10 / +0x12 pitch limits; see "Port round 4 corrections" at the end.* |
| +0xa0 | ptr | owner player record (default dummy DAT_000b6e90; record fields +0xc speed, +0xe, +0x30 player number, +0x32 castle thing idx, +0x134/+0x142 mana, +0x146 aim, +0x157/+0x15b stats, +0x187/+0x189 HUD flash, +0x1cc[] threat table, +0x214[24] spell thing idx, +0x2a4[] spell icon table, +0x394[] spell flags) | 427d0, 47130, 405f0 |

Position struct passed around (`DAT_000adfc4` scratch): `{u16 x, u16 y, i16 z}`; cell coordinates are the high
bytes (`+1`, `+3`). World = 256x256 cells x 256 units.

### 2.3 Class / state dispatch tables (0x943da)

Class table record (18 bytes): +0 table A (update handlers, indexed by **state** +0x46), +4 table B (create
handlers, indexed by **type** +0x41 = level Model), +8 parent ptr, +12 u16 = class id + 1.
Both tables use 14-byte records {parent 0x943cc, u16 model, handler, enabled}.
Create handlers are called via `thing_create_35690(class, type)`; they call `thing_alloc_35560`, fill the
fields, `thing_set_sprite_35240`, `thing_link_cell_3e250`, `thing_restore_health_35080`.

State ranges (table A) from the initial state written by each create handler:

| class | states | type -> initial state |
|---|---|---|
| 2 Scenery | 18 | type*3: Tree 0, Standing stone 3, Dolmen 6, Bad stone 9, dome 12, dome 15 (3 states each: normal/burning/burnt) |
| 3 Player | 11 | type 0 flyer -> 0, type 1 -> 1, type 2 castle -> 5, type 3 -> 7; state 2 = dying fall (405f0), 4/6 respawn (416d0), 9 = mana balloon (42530) *Corrected (round 5 merge): type 2 castle: 4 active, 5 build, 6 destroyed (not a respawn); type 3 = balloon (7 waiting, 9 flying); types 4..11 are start-position markers; see "Port round 4 corrections" at the end.* |
| 5 Creature | 121 | Dragon 1, Vulture 7, Bee 13, Worm 19, Archer 25, Crab 31, Kraken 37, Troll 43, Griffon 49, Skeleton 54, Emu 61, Genie 66, Builder 73, Townie 79, Trader 85, type15 91, Wyvern 97; 120 = body segment (0x18050); 102..119 disabled *Corrected (round 5 merge): wrong by one for every type: base = type * 6 (Dragon 0 .. Wyvern 96), the constructors start in base + 1; see "Port round 4 corrections" at the end.* |
| 7 Weather | 5 | Wind type 4 -> state 4; all handlers = ret (0x4b570) |
| 9 Projectile | 21 | type == state (0..20); 1 homing (448b0), 9 lightning (44fc0), 14 lightning segment *Corrected (round 5 merge): types 14..19 run states 15..20; state 14 belongs to type 9; see "Port round 4 corrections" at the end.* |
| 10 Effect | 62 (gaps 39,47,49,50) | types <=0x13: state==type; 0x17, 0x19..0x1b same; 0x1c Wall->0x1e, 0x1d Path->0x1f, 0x1e->0x20, 0x1f Canyon->0x21, 0x20->0x22, 0x21->0x23, 0x22 Teleport->0x24, 0x24->0x26, 0x25->0x27, 0x26->0x28, 0x27 Mana ball->0x29 (0x25980), 0x28->0x2a; castle building states 0x2a..0x3d (0x25f10..0x27e90; 0x26680 for 48/51) *Corrected (round 5 merge): 0x1c / 0x1d / 0x1f / 0x32 are markers; 0x1b is the wall piece (states 0x1b..0x1d); 0x2d wizard castle 0x30 / 0x33..0x35; 0x34 crab egg 0x38; 0x35..0x38 -> 0x3a..0x3d; see "Port round 4 corrections" at the end.* |
| 11 Switch | 32 | create handlers are 32-byte stubs (not functions yet); state 3 repeat trigger, 5 one-shot *Corrected (round 5 merge): state == type for every switch kind; states 13..29 are creature-type triggers, 30 all creatures; see "Port round 4 corrections" at the end.* |
| 12 Spell | 72 | state = spell*3 + phase; phase 0 = spell-specific cast handler, phase 1 = 0x472f0, phase 2 = 0x47300 (common; dropped spell / pickup) |

Level-file THING_INIT (18 bytes: Class, Model, Xpos, Ypos, DisId, SwiSz, SwiId, Parent, Child) is copied into the
game state at `state+0x442` (level buffer) and `state+0x2f945..0x385d3`; records with DisId == -1 are spawned at
level start (`level_spawn_effect_record_34e00` for class 10), the rest by `switch_activate_356e0` when a switch
with that DisId fires. Position = (Xpos<<8, Ypos<<8, terrain height). Parent/Child are 1-based record indices
used by `level_build_linked_feature_34c40` for wall/path/canyon/ridge chains.

### 2.4 Algorithms

- `thing_update_all_3dce0`: advance state RNG; if !(cfg->flags2 & 1) { free things flagged 0x400; clear list
  heads; one pass building class lists; creature_sound_tick (unless cfg flag 0x10); hit_record_scores_150f0;
  mana_totals_update_427d0; second pass: for each live thing `rec = tableA[class] + state*0xe; if rec.model==state
  { if rec.enabled rec.handler(thing); thing->counter++ } else { FUN_000603bc(); thing_mark_delete }` }.
  It is called 1/4/16 times per tick depending on cfg+0x96, i.e. the simulation sub-step count.
- Cell map: `DAT_0010dfb0` = u16[256*256] head thing index per cell (index = celly*256+cellx). Doubly linked via
  +0x14/+0x16. Spatial queries (`thing_find_collision_105f0`, `thing_find_mana_near_10730`, area damage) walk
  cells in a spiral (`spiral_search_*`) and then the cell list, testing `math_bbox_overlap_10530` with the
  +0x4e..+0x54 extents.
- Terrain maps (all 256x256 bytes): `DAT_000ddfb0` height (0..0xc4), `DAT_000cdfb0` cell type (water=1 ...),
  `DAT_000fdfb0` cell flags (bit 0x80 = castle footprint, low nibble = type bits). `DAT_0010dfb0` is reused as
  u16 scratch during generation (`terrain_finalise_heightmap_313a0` zeroes it before use as the cell map).
- Damage: dealers write `+0x5a` (amount, accumulated) and `+0x5e` (attacker index). The victim applies it on its
  next update (creatures in `creature_ai_step_18870`, players in `player_take_damage_42770`, trees in
  `scenery_tree_update_43ba0`), records `+0x28` and, on death, `+0x26`.
- Mana economy: creatures carry +0x8c (half their max health); mana balls (effect 0x27) hold +0x8c with owner
  +0x90; balloons (class 3 state 9) ferry mana to the castle (class 3 type 2, +0x1a = level); the castle's
  +0x8c is what `spell_can_cast_46e70` compares with the spell cost (+0x84). `mana_totals_update_427d0`
  recomputes per-owner totals (+0x88) every tick and `game_check_level_won_3db20` compares castle mana against
  the level's requirement (`state+0x38c93` percent of `cfg+0x5e`). *Corrected (round 5 merge): the divisor is Config+0xbc (total mana), not +0x5e; see "Port round 4 corrections" at the end.*
- `ui_draw_frame_1fab0` (6196 bytes) is **not** simulation: it is the per-frame composition. It selects the
  viewport (`render_set_viewport_2f3c0`), calls the renderer `FUN_0002f6e0`, then draws the HUD (minimap
  43610/42a20, status bars 219f0, spell slots 22870, per-player message queue at playerrec+0x3427 with timers at
  +0x3467, text via ui_draw_text_4a9a0), handles the spell-selection screen (view mode 2: 24 icons at x
  0x180..0x27f step 0x40, mouse `DAT_0009e5dc/0x9e5de`, hovered slot in cfg+0x16) and the map/scoreboard screen
  (mode 4), and finally the credits ticker (`PTR_s_Designed_by_0009861c`) when cfg flag 4 (demo/attract). *Corrected (round 6 merge): the credits scroller runs from Config.credits_state (+0xa1: 3 waits +0xa2 frames, 2 resets, 1 scrolls), which the attract demo level and the movie recorder start.*

### 2.5 Globals

- `DAT_000adf6c` game state: +4 RNG, +8 local player index, +10 player count, +0x28/+0x251 free stack,
  +0x11f1/+0x11f5 recyclable stack, +0x2198 view/border mode, +0x2199/+0x219a HUD enable flags, +0x340b.. player
  records (0x801), +0x442 level record buffer, +0x7463 thing base, +0x2f945 level THING_INIT copy, +0x38c93 win %,
  +0x38c9f creature count.
- `DAT_000adf74` config: +0x16 hovered spell slot, +0x5e level total mana, +0x5f/+0x63 options, +0x96 sub-steps,
  +0xa1..+0xa7 credits ticker state, +0xbc total mana, +0x8e1e.. list heads. *Corrected (round 5 merge): +0x5e..+0x60 are tick bits; the total mana is +0xbc; see "Port round 4 corrections" at the end.*
- `DAT_0010dfb0` cell->thing map; `DAT_000ddfb0` heightmap; `DAT_000cdfb0` cell type; `DAT_000fdfb0` cell flags;
  `DAT_000ade28` spiral ring tables; `0xac160` spiral iterators; `DAT_000987ec/0x98fec` sin/cos (16.16, 2048
  entries); `0x9b3ec` atan table; `DAT_000adfc4` position scratch; `DAT_000adfbc` current font; `DAT_0012ed74`
  screen buffer, `DAT_0012ed70` pitch, `DAT_0012ed78` height; `DAT_000b5810` viewport offset; `DAT_0012edae` game
  mode (bit 0 = low-res/half-size layout); `DAT_00097630` player colours; `DAT_00097660` spell screen order.
- Sprite table at 0x97676 (14-byte records: +8/+10 half extents, +0xe palette index) via thing_set_sprite_35240.

### 2.6 Open questions

- Player types: 0 is the flying wizard (used by switches/AI targeting), 2 is the castle (40000 HP, 0x21 flags);
  the roles of types 1 and 3 (10000 HP) and of the level "Flyer1..8" models 4..11 (table B entries 0x359c0..0x35aa0,
  32 bytes each, not read) are unconfirmed. *Corrected (round 5 merge): type 1 = computer wizard, 3 = balloon, 4..11 = start-position markers; see "Port round 4 corrections" at the end.*
- Spell 2: level.py calls it "Alliance" but its cast handler (0x47420) multiplies the caster's speed by 2/3 ->
  likely "Accelerate"; check the save-game spell name table. *Corrected (round 5 merge): spell 2 is speed-up; see "Port round 4 corrections" at the end.*
- Phase-1/2 spell handlers 0x472f0/0x47300 and the switch create stubs 0x39e10.. are not functions in the export.
- Class 9 type numbering vs spells (type 0 fireball?, 1 homing, 9 lightning, 10, 12, 13, 14, 18, 19) needs the
  cast handlers to be read; `FUN_00045f00` auto-aim groups types {0,3,4,0x10,0x12,0x13}.
- Descriptor struct at +0x9c (DAT_00096a10.. per creature) deserves a full layout pass; +0x1a is the anim
  period used as the "think every N ticks" divisor.
- `hit_record_scores_150f0` writes playerrec+0x1cc[8-byte entries]: probably AI threat/aggression weights.


## Tick-side subsystems: palette, input, sound, network, demo (agent pass, 2026-10-06)
### Corrections to existing notes
- `timer_sync_61510` is NOT a timer: it is `vga_palette_fade_61510` (reads the DAC, interpolates to a
  target palette over N steps with a vsync wait per step). The game loop calls it to fade to black.
- `DAT_0012edae` is the screen mode, not single/multiplayer: 1 = 320x200 (mode 13h, "*WScreen" 64000
  bytes), 8 = 640x480 VESA 0x101 (0x4B000 bytes). Bit 0 is tested everywhere as "low-res".
  The R key toggles it at run time (`video_toggle_resolution_33600`).
- cfg bit 0x100 is "movie" (scripted demo playback), not network. Network = cfg bit 0x80.
- The "random event every N ticks FUN_0005c0a0" in game_main is `music_play_track_5c0a0`.
- `FUN_0003a8b0` (4760 bytes) is not renderer: it calls `demo_record_playback_step_3c540` and
  `net_exchange_frame_4f530`, i.e. it is the per-tick command/packet processing for all players.

### Tick order (FUN_00032e80 = game_tick_update_32e80)
1. `palette_effect_update_33010` (only when state+0x219b == 0, i.e. not in map/hicolor mode)
2. `texture_anim_update_4be50` (not when paused, cfg+2 bit 0)
3. `player_local_input_16660` (skipped in demo playback, cfg bit 4)
4. FUN_0003a8b0 (command processing / net exchange / demo IO)
5. `player_check_mana_win_3db20` (not paused)
6. FUN_0003dce0 things, 1 / 4 / 16 passes by cfg+0x96 (F3 cycles 0..2)
7. `sound_update_494b0`
8. FUN_0001fab0
9. cfg+0x99 = frame time (DAT_0012eab4 tick counter delta); `debug_overlay_draw_4ad80`
10. `debug_screenshot_3ca00` if DAT_00094338; FUN_0002f480 present.

### Input
- Keyboard: ISR at LAB_0004fa28 (asm, directly after `vga_blit_banked_4f974`, not a Ghidra function),
  installed by `input_keyboard_install_4fb46` on int 9. Key-down table: byte[128] at 0x12EE20
  indexed by scancode (0x12EE21 = Esc, 0x12EE3C = Enter, 0x12EE68/70/6B/6D = Up/Down/Left/Right).
  Last-pressed scancode: DAT_0012eea0 (consumer clears it to 0). Scancode->ASCII tables at
  0x9E5E8 (in-game text entry) and 0x51E55 (front end); shift state DAT_0012ed35 bit 0x20.
- Mouse: int 33h via `dos_int86_61f30`; event callback LAB_0005b86c (asm, mask 0x7F). Position
  DAT_0009e5dc/de (640x400/480 space, 320x200 code halves it), click position DAT_0009e5d8/da.
  Button events: DAT_0012ee0e left-click, DAT_0012ee0c right-click (cleared by consumer);
  held: DAT_0012ee14 left, DAT_0012ee12 right. DAT_0009e5e4 mouse present. Cursor is a 64x64
  sprite buffer (0xFE transparent) with two save-under buffers.
- Joystick: game port 0x201, polled by `input_joystick_poll_5a4e0` each tick from player input;
  "digijoy"/"anojoy"/"anojoy4" select digital/analog/4-button (DAT_0009e583, DAT_0012ed40).
  Axes map to arrow-key flags, buttons to mouse-button events / Enter.
- Alternate device (DAT_0009e43c/43d, ports DAT_0012e61c/624/62e/630, int AX=0x6008): a
  head-tracking / stereo display peripheral; it receives the palette and provides x/y/z. Flagged `?`.

### Player command packet (10 bytes at state+0x7413 + player*10)
`+0 cmd, +1 arg/bits, +3 steer x, +4 steer y (-127..127 from mouse), +5 bits`. Written by
`player_queue_command_17270` / `player_mouse_steer_15590`, sent/received by the NetBIOS layer and
recorded/played back by the movie system. cmd 0x15/0x16/0x17 carry spell-slot selections (mode 2),
0x1e is the "chronicle" cheat command (player name check unless cfg+1&0x80). *Corrected (round 5 merge): +1 arg, +2 second argument (cmd 0x17: book slot), +5 key bits only under cmd 5 / 6; see "Port round 4 corrections" at the end.*

### Player record (0x801 bytes at state+0x340F + p*0x801) fields seen here
`+0x340b win-timer, +0x340d status (bit2 won, bit4, 8 = level aborted), +0x3410 flags (bit8 debug
overlay), +0x3411 active, +0x3415 thing index, +0x341d, +0x3423 (config value), +0x3427 message
text (0x44 bytes, also +0x340b+0x1c variant), +0x3467 message ticks (0x32), +0x3469, +0x3815 name
(9+ chars), +0x3855 input mode (0 flight, 2 spell select, 3 text entry)`. A per-player
0x845 stride is also used for the message fields. *Corrected (round 5 merge): status 2 won, 4 lost, 0xc restart; input mode 0 flight, 1 help, 2 book, 3 chat, 4 map; see "Port round 4 corrections" at the end.*

### Game-state block (0x38D03 bytes, DAT_000adf6c)
`+8 local player, +0xa player count, +0x28 free-thing count, +0x2c texture-needed table (0x211),
+0x245/+0x249/+0x24d title flags, +0x251 free list, +0x11f1 active count, +0x11f5 active list,
+0x2195..0x219e option toggles (F4..F10: 0x2195, 0x2196, 0x2199/0x219a, 0x219b map mode, 0x219c,
0x219d, 0x219e), +0x2198 view range (0x11..0x28), +0x21ad..0x21b7 "option allowed" gates,
+0x38c93 required mana percent`. Thing pool indexed from +0x7463 (0xa4 stride, index starts at 1;
+0x7503 = record 1 base used by the win check).

### Config struct (DAT_000adf74)
`+0 flags: 2 record demo, 4 play demo, 8 skip-game, 0x10 local player chosen (custom/network),
0x40, 0x80 network, 0x100 movie, 0x200 (set with movie); +1: bit0 options given, bits 2..0x20 cheat
level 1..5, 0x80 cheat gate; +2 bit0 paused; +0xd, +0xf movie number, +0x11 level, +0x12 demo file
handle, +0x16 selected spell slot, +0x17 palette-fade stage, +0x18 debug bits, +0x19 session magic
(0xF851B9 check), +0x5e level total mana, +0x96 update sub-steps (0..2), +0x98 palette effect,
+0x99 frame time, +0xce PIT reload (0x2726), +0xd2 BIOS-tick accumulator`. *Corrected (round 5 merge): +0x16 = book cell under the pointer; +0x5e..+0x60 tick bits (total mana is +0xbc); see "Port round 4 corrections" at the end.*

### Timer
- int 8 handler LAB_0004ac03 (asm) installed by `timer_isr_install_4ac79` from
  `sound_initialise_34140`; it increments DAT_0012eab4 through `timer_tick_isr_3411a`.
  Reload 0x2726 = 10022 -> 1193182/10022 = 119 Hz (flag: inferred from cfg+0x19c, verify in asm).
- The HMI sound library has its own 16-slot timer event system (`hmi_timer_add_event_5d093`,
  constant 0x1234DC, PIT programming in 66680/667a9); music fades use it.

### Sound (HMI SOS, 32 digital channels)
Tables: DAT_0012e070[32] {owner thing, sample id}, DAT_0012e0f0[32] volume, DAT_0012e330 fade-out
flags, DAT_0012e270 fade-in flags (target DAT_0012e2b0), DAT_0012e246 sample count, 0x97078 target
volumes. Sample bank .tab records are 0x20 bytes (+0x12 data ptr, +0x1a length+0x10). Request table
0xB7AC0: 47 x 10 bytes indexed by sound id. Globals: DAT_0009e320 sfx available, DAT_0009e321 sfx
on, DAT_0009e30c music available, DAT_0009e30d music on, DAT_0009e312 current track,
DAT_0009e316 track count, DAT_0009e328 sound card type. *Corrected (round 5 merge): record 0 is a header, +0x1a length includes 16 bytes of padding; DAT_0009e328 is the memory class / bank quality, not the card type; see "Port round 4 corrections" at the end.*

### Network (NetBIOS, int 5Ch through DPMI 0300h)
NCB layout used: +0 command, +6 LSA/post, +0xa callname(16), +0x1a name(16), +0x31 cmd_cplt.
Commands: 0xB0 add name, 0xB1 delete name, 0x90 call, 0x91 listen, 0x92 hang up, 0x94 send,
0x95 receive, 0x35 cancel, 0x7F presence test. 8 player slots DAT_0009e400, local index
DAT_0009e3ca, count DAT_0009e3cc, abort flag DAT_0009e3f8. No IPX/serial/modem code found in the
decompiled range (no int 7Ah/14h and no COM/modem strings).

### Demo / movie system
Files `movie/mvi%05d.dat` (10-byte packet stream) and `movie/gam%05d.dat` (RNC game-state
snapshot, 0x38D03 bytes) plus a terrain dump (64K heightmap at 0xCDFB0 + 3x64K + 128K + 0x12C2).
Recording: cfg bit 2; playback: cfg bit 4 (config "movie N" sets cfg+0xf and bits 0x120, game_main
then disables sound, loads data/ftext.dat and skips the front end). *Corrected (round 5 merge): `-movie n` / `-roll n` do not start playback; the attract mode is the only movie path; see "Port round 4 corrections" at the end.*

### Other data formats
- Textures animate through embedded Autodesk FLIC (0xAF12 / 0xF1FA, chunk types 4/7/15).
- Screenshots are Bullfrog "mhwanh" images (big-endian header, 256-entry 8-bit palette, raw pixels).

### Open questions
- (resolved, see "Interrupt handlers") PIT reload 0x2726 = 119.06 Hz; the three asm handlers are functions.
- Identity of the alternate pointing/display device (DAT_0009e43c). Possibly a VR headset
  (Forte VFX1 / i-glasses style) - it gets a copy of the palette and provides 3 axes.
- Meaning of the F4..F9 option bytes (state+0x2195..0x219e); their on-screen message strings were
  passed in registers and are not recoverable from the decompiler output.
- `FUN_0004d2b0`/`FUN_0004d8f0`/`FUN_0004da10` parse `sndsetup.inf` and set up the HMI drivers
  (card names SBPRO/ADLIBG/SB16FM/PASFM/SBAWE32/GRAVIS...) - not covered in depth here.


## Front end, config, save games, C runtime and DOS glue (agent pass, 2026-10-06)
### Command line (config_parse_33750)

Options are matched with `crt_stricmp_61745` against the strings at 0x90664..0x90707 (each option
`-x`/`/x`, optional value in the next argv). Effects (cfg = `*DAT_000adf74`, state = `*DAT_000adf6c`):

| option | effect |
|---|---|
| `digijoy` | `joystick_digital_init_5a010`; if present DAT_0009e583 \|= 2 |
| `anojoy`, `anojoy4` | `joystick_analog_calibrate_5a060`; DAT_0009e583 \|= 1 (0x20 for 4-button variant) |
| `vfx` | `vfx1_init_4fdc0`; state+0x219e = 1, DAT_0009e583 \|= 8, prints "VFX\n" |
| `help` | abort (returns -1) |
| `debug` | DAT_000adfca -> cfg.flags \|= 0x80 |
| `network` | `net_init_4ee70`; if players>1 `net_join_session_4f030` -> state+8 (local player), cfg.flags \|= 0x10; cfg \|= 0x100 |
| `custom` | cfg \|= 0x100 |
| `setsound` | sets a local that is not used afterwards (dead) |
| `demo n` | cfg byte+1 \|= 1 and bit 2<<(n-1) for n=1..5 (0x200..0x2000) |
| `detail n` | n==0 -> state+0x2195/96/97 = 0, +0x2198 = 0x28 |
| `cheat n` | player[local]+0x14 (state + idx*0x801 + 0x3423) = n |
| `name s` | copied to a local (player name, not stored here) |
| `level n` | cfg+0x11 = n |
| `movie n` | cfg+0xd = n *Corrected (round 5 merge): only when n != 0; see "Port round 4 corrections" at the end.* |
| `roll n` | cfg+0xd = 0, cfg+0xf = n, cfg.flags \|= 0x120 *Corrected (round 5 merge): nothing sets the playback bit 4; see "Port round 4 corrections" at the end.* |
| `time` | cfg.flags \|= 0x40 |
| `password n` | cfg+0x19 = n (int) |
| `players n` | state+0xa = n (default 2) |
| `session s` | cfg+0x75 = s (default "CARPET") |

After parsing: `DAT_0012ef00 = mem_alloc_low(0x100)` (VESA info buffer), `state = mem_alloc(0x38d03)`,
`cfg = mem_alloc(0x8e7e)`, eleven option bytes state+0x21ad..0x21b7 = 1, `cpu_detect_5ac80` -> cfg+8,
state+0x2195/0x2197 = cfg+8 (Pentium), +0x2196/+0x2199/+0x219a = 1, +0x2198 = 0x28, cfg+0x1d = 0,
`DAT_0012edae = 1` (video mode flags: bit0 = 320x200).

### Front-end state machine

`frontend_menu_loop_52070` runs once per frame. `DAT_0012ed2e` selects the screen through the jump
table at 0x52038:

| state | function | screen |
|---|---|---|
| 0 | fe_screen_intro_movie_54900 | intro\intro.dat |
| 1 | fe_screen_config_521c0 | gconfig.dat: sound setup wizard + input device |
| 2 | fe_screen_main_menu_532b0 | mainmenu.dat/.pal, mmspr.dat, mmmask.dat |
| 3 | (none) | |
| 4 | fe_screen_multiplayer_54bd0 | pmulti.dat, pmultspr.dat |
| 5 | fe_screen_level_result_55b00 | pperf.dat; levelw1/levelw2/levelose.dat FLIs |
| 6 | fe_screen_language_56940 | language.dat, langspr.dat |
| 7 | fe_screen_intel_logo_56510 | intro\intel.dat |
| 8 | fe_screen_title_56730 | intro\title-01/02.dat |
| 9 | fe_screen_bullfrog_logo_563c0 | intro\logo.dat |
| 10 | fe_screen_outro_movie_54ab0 | intro\outro.dat |

Flow: `fe_init_state_51ed0` sets state 6 (language) -> 1 (config; the sound wizard is skipped when
sndsetup.inf exists, DAT_0012ed29 = 7) -> 7/9/8/0 (logos, title, intro) -> 2 (main menu). In the main
menu, after 0x12c0 ticks without input (`fe_input_idle_check_53ad0`) the attract counter
`DAT_0012ed34` cycles: 0 -> state 0 (intro movie), 1 -> state 8 (title), 2 -> demo level (sets
`DAT_0009e504`, cfg.flags |= 0x24, cfg+0xa1 = 3, cfg+0xa2 = 200). A level start sets state 5 so the
result screen is shown on return; result screen returns to state 2, or 10 (outro) after level 50.

Main menu items (hit mask byte `DAT_0012ed26`; table 0x5167c of 6-byte `{handler,kind}`):
1 new game / resume (57480), 2 handler 0x57580 (redraw, unanalysed) [*Corrected (round 6 merge): item 2 is `fe_menu_text_dialog_57580`: "Enter your name:" (Config+0x1d) and "Enter your call-name:" (Config+0x3d, 8 chars).*], 3 multiplayer (573fc), 4 quit
(57270), 5..10 kind 1..6: in top-level mode kind 1 = load, 2 = save, 3 = start game; in slot mode
(`DAT_0012ed2d` 1/2) they are the six save slots -> `fe_savegame_load_59030` / `fe_save_slot_dialog_541f0`
+ `fe_savegame_save_591d0`, 11 start level (5744c, only when no game in progress).

Save game `save\carpet%02X.gam` (slot 0..5): u32 version 4; char name[0x14]; cfg+0x1d[0x20];
cfg+0x3d[0x20]; state+0x2195[0xc] (detail options); u32 checksum = (level + DAT_0012ed30 +
DAT_0012ed31)*4; state+0x3bd6[0x18] (campaign progress); u8 DAT_0012ed30; u8 DAT_0012ed31;
state+0x2195[0xc] again. Slot names default "Game One".."Game Six" (0x9e469.., pointers 0x9e4e8).

Level names: pointer table 0x97490, index = level number (entry 0 empty, 1..50 "1. Al Jahan" ..
"50. Volcania", 51.. multiplayer maps "Bussorah" "Bisnagar" "Tartary" "Akkania" "Ryahn" "Zhullor"
"Dombren"). Referenced only from fe_screen_level_result_55b00 (0x55fb3). The multiplayer lobby offers
levels 50 + index. 0x943cc (the class/model table "parent" pointer) points at the string "Text Omitted". *Corrected (round 6 merge): the table has 101 entries (0x97490..0x97624): 51..70 are twenty multiplayer names ("Bussorah".. "Comari"), 71..100 all point at "0"; typed as `g_level_names` in carpet_types.txt.*

Text rendering: font struct (DAT_0009e508 sfont0, 0x9e510 sfont1, 0x9e518 sfont2) = {u32 glyph table
(6-byte {u32 offset,u8 w,u8 h} entries, ASCII-0x20), u16 flags (bit0/1 enable colour remap, 0x10 use
sprite blitter, 0xc000 draw mode), u8 colour1, u8 colour2}. `ui_draw_text_58ab0(x, y, font, string)`
handles control bytes 1/2 (set colours from next byte), 3..6 flag toggles, 10 newline (advance by
glyph 5 height). Glyph pixels are span encoded: n>0 copy n bytes (bytes 1/2 remapped to the font
colours), n<0 skip, 0 end of row; identical to the sprite format in FORMATS.md.

Screen helpers: back buffer `DAT_0012ed74` (pitch `DAT_0012ed70`, height `DAT_0012ed78`),
background copy `DAT_000adf68` (swapped with the back buffer around overlay drawing). Every screen
has two code paths on `DAT_0012edae & 1`: 320x200 (`vga_blit_backbuffer_320_610f0`, 320-wide
fill/copy) vs 640x480 VESA (`vesa_blit_backbuffer_640_4f974`, sprite tables relocated with doubled
sizes). Mouse coordinates `DAT_0009e5dc/de` are in 640x400 space and halved for hit tests.
Palette fades go through `vga_palette_fade_61510` (the function previously named timer_sync_61510:
it reads the DAC, steps towards target or black with vsync waits; 0x10 steps on fade-out, 0x20 on
fade-in). Mouse cursor is a 64x64 buffer `DAT_0012edf8` rendered by `ui_set_mouse_cursor_5ba5c` from
sptrs.dat entry `DAT_0012ed2e*6` (one pointer sprite per screen state).

FLI player: `fli_play_508f0` -> `fli_next_frame_50dfd` (chunk magic 0xAF12 file header, 0xF1FA frame)
-> `fli_decode_frame_50e88` (sub-chunk types 4 COLOR256 skipped, 7 SS2, 15 BRUN) ->
`fli_present_frame_50600` (callback `DAT_0012ea9c`, pacing by tick counter `DAT_0012eab4`).
`fli_event_script_17d80` syncs sound/music events (7-byte records) to frames. Strings "COLOUR256 ",
"SS2 ", "BRUN ", "COPY ", "PSTAMP " (0x92e9c..) are debug names of chunk types.
*Corrected (round 6 merge): the call chain is wrong. `fli_play_508f0(abort_on_input, apply_palette, cue)` plays the file (path in DAT_0009e708) through `fli_read_frame_50520` (was fli_error_unknown_frame) and `fli_decode_present_frame_50600` (was movie_frame_present), which decodes types 4, 7, 11, 12, 13, 15, 16, 18 (COLOR256 is applied). `flic_play_chunk_50dfd` / `flic_decode_frame_50e88` are the separate memory-stream player (types 7 and 15, 4 skipped) of the texture and front-end animations. see "Port round 5 corrections" at the end.*

### Memory manager (game side, not CRT)

`mem_pool_init_61f80` grabs DOS/DPMI memory into a region table 0x12f520 (12-byte: {addr, size,
real-mode flag}) and a block list at 0x130120 (256 nodes of 18 bytes: +0 ptr, +4 size, +8 next,
+0xc prev, +0x10 owner tag (0 = free), +0x11 region type). `mem_alloc_59870(size)` is best-fit and
zero-fills; `mem_alloc_low_598f0` only takes blocks with a real-mode address (network buffers, VESA
info). `mem_update_stats_622a8` keeps DAT_0013132c = largest free block, which `mem_init_pools_59500`
uses to pick the memory class DAT_0009e328 (0..3), texture block size state+0x21a0 (0x10 ->
data/block16.dat, 0x20 -> block32.dat) and to disable music/digital sound below 1 MB.
`file_load_resource_list_5ae80` loads NUL-terminated lists of 0x2c-byte records (these are the
`data\screens\*.dat` lists at 0x510d0.. in the code segment: name[0x1c], ptr to dest pointer, ptr to
end pointer, size, flags) via `file_load_rnc_3cbe0`; `file_free_resource_list_610c0` frees them.

### Watcom CRT / DOS glue (0x5e000-0x7b9ee)

Identified by behaviour (see CSV): printf family (`crt_prtf_core_6a0e6` with '%' loop, spec parser
6a395, converter 6a6ef; `crt_sprintf_603bc` = __prtf + NUL; `crt_printf_5b02a` = va wrapper onto
stdout FILE 0xa2856 via `crt_vfprintf_core_66193`), scanf family (`crt_scnf_core_6c23a`), heap
(`crt_nmalloc_6b186` over segment list DAT_000a4670, growth via DPMI 0x501 / int 21h 48h), low-level
I/O (int 21h 3Dh/3Ch open in 66296, 3Fh read 664ec with text-mode CR stripping, 40h write 6af03 with
LF->CRLF, 42h lseek 6aeb8, 3Eh close 6bfe0, 41h unlink 712c4, 39h mkdir 61ea6, 4300h access 61e68,
4400h ioctl 6de66, 1Ah/4Eh findfirst 71498, 4Bh exec 71713, 4Ch exit 627b9), stdio (FILE table
0xa283c, 0x1a bytes each; stdin = 0xa283c), errno at 0x131a58 / _doserrno 0x131a54, startup
`crt_startup_62542` -> `crt_cmain_6b95e` -> `main_3d0a0`. Extender type byte `DAT_000a46ae`
(1 = DPMI/DOS4GW, 2..8 = Phar Lap style, 9 = other) selects between int 31h and int 21h 25xxh
variants in the vector/memory helpers. The PIT/PIC helpers (`pit_set_rate_66680`,
`timer_install_irq0_666c6` saving int 8 to DAT_000a2a54) and the DPMI lock lists (`snd_hmi_lock_code_5f04d`)
belong to the HMI Sound Operating System, as do the sos* functions and the AWE32 EMU8000 driver (named `snd_gus_*` until round 4, now `snd_awe32_*`)
(GUS register access through port base DAT_000a4ec2 + 0x802). The 0x79000+ block is Watcom's text
console / adapter detection (int 10h, "TRIDENT", "OAK", "VGA=").

Game code that Watcom placed late in the image: `terrain_fractal_*_71f08/71f92/72027`
(diamond-square over a 256x256 16-bit map at 0x10dfb0 with LCG 0x24a1*x+0x24df),
`terrain_sample_height_71e00` (bilinear over the byte map at 0xddfb0), `render_sort_triangle_722e3`. *Corrected (round 5 merge): triangle interpolation, not bilinear; see "Port round 4 corrections" at the end.*

### Globals recovered

- DAT_0012ed2e FE state; DAT_0012ed2d load(1)/save(2) slot mode; DAT_0012ed26 hovered menu item;
  DAT_0012ed29 sound-wizard step (7 = done); DAT_0012ed2a/2b selected/hovered option; DAT_0012ed33
  input-device page; DAT_0012ed35 FE flags (bit0 screen initialised, bit1 sndsetup missing, bit3
  start game, bit4 lobby wait, bit5/6 text entry); DAT_0012ed34 attract phase; DAT_0012ed36 attract flags;
  DAT_0012ed14 idle start tick; DAT_0012eab4 FE tick counter; DAT_0012ed10 frame counter;
  DAT_0012ed30 "CARPET%d" counter, DAT_0012ed31 lobby player count; DAT_0012ed1e lobby level scroll.
- Input: DAT_0012ed2f mode; DAT_0012ec3c/3e cursor (2x), DAT_0012ec40/42 click pos; DAT_0012ec44..48
  edge flags; DAT_0009e5dc/de mouse pos; DAT_0012ee0a/0c/0e, ee12/ee14 buttons; DAT_0012eea0 last
  scancode; DAT_0012ee21 Enter, DAT_0012ee3c Escape [*Corrected (round 6 merge): swapped: 0x12ee21 = g_key_down[1] = Esc, 0x12ee3c = g_key_down[0x1c] = Enter.*], DAT_0012ee68/6b/6d/70 cursor keys;
  DAT_0012ee20[128] key table; DAT_0009e583 device flags (1 analog joy, 2 digital, 8 VFX1, 0x20 4-button);
  DAT_0012ed40 joystick present; DAT_0012ed42/44 joystick axes; DAT_0009e5e4 mouse present.
- Video: DAT_0012edae mode flags (bit0 320x200; 2/8 other modes), DAT_0012edac saved BIOS mode,
  DAT_0012ef00 VESA info buffer, DAT_0012ef04 granularity, DAT_0009e860 fade active,
  DAT_0012ead0..e4 text clip rect, DAT_0012ed88/98/80/a8/a4/90 sprite clip window, DAT_0009e5d4 cursor lock.
- Game: DAT_0009e504 leave front end / start level; DAT_0009e500 game in progress (resume available) [*Corrected (round 6 merge): DAT_0009e500 is 1 in the data and never set again; "resume" is the very first start.*];
  DAT_0009e3c8 network active, DAT_0009e3ca local player, DAT_0009e3cc player count, DAT_0009e3fc/3d0/3d4/3d8/400 net buffers;
  DAT_0009e520 lobby slots (8 x {joined,ready,anim}); DAT_0009e320/321 digital sound ok,
  DAT_0009e30c/30d music ok, DAT_0009e312 current track, DAT_0012e244 rate code, DAT_0009e328 memory class.
- cfg (DAT_000adf74) extra fields: +8 Pentium flag, +0xd movie, +0xf roll, +0x11 level, +0x13 saved
  level, +0x19 password, +0x1d/+0x3d two 0x20-byte strings saved in .gam, +0x75 session name,
  +0x97 language, +0xa1..0xa5 demo parameters, +0xa8 pool pointer, +0xac pool size.
- state (DAT_000adf6c) extra fields: +0xa player count, +0x2195..0x219a detail options, +0x219b
  requested video mode, +0x219e VFX enabled, +0x21a0 texture block size, +0x21ad..0x21b7 option bytes,
  +0x2409 player record backup, +0x3bd6 campaign progress (0x18 bytes, saved), +0x7414 per-player
  10-byte records (byte 0 = level).

### Corrections to existing names

- `timer_sync_61510` is a palette fade (`vga_palette_fade_61510`); the real frame pacing in the FE is
  `fli_wait_frame_50430` / tick counter DAT_0012eab4.
- `FUN_000603bc` is `sprintf` (not a stack probe); the stack probe is `crt_stack_check_70966`.
- cfg byte +0x97 is the language, not the sound card type (sound card names live in sndsetup.inf
  strings DAT_0012eb00..).

### Open questions

- 0x57580 (menu item 2 handler) and 0x579c0 (FLI frame callback used by the scroll dialogs) are not
  functions in Ghidra; they need to be created manually. *Answered (round 6): both are functions now (`fe_menu_text_dialog_57580`, `fe_fli_composite_bg_579c0`).*
- `video_mode_extra_5be68` int 10h AX=7/8 calls for DAT_0012edae == 8/2 and the stereo blits
  (`gfx_blit_stereo_*`) suggest a 3D-glasses/VFX1 page-flipped mode; not verified at runtime.
- `cpu_detect_5ac80` -> what 5ace8 reads (CPUID/flags test) was not checked.
- The dead `setsound` option and the never-set `local_60` (mode 8) path in config_parse indicate
  stripped debug features.


## Rasteriser: poly_fill_triangle_722e3 and the span fillers (2026-10-06, round 2)

One 25,367-byte function (0x722E3-0x78DD5, Watcom-placed after the CRT) with several levels of
computed jumps that the analyzer had given up on ("too many branches", flow treated as a call).
`FixJumpTables.java` with `ghidra/names/jump_tables.txt` wires the 28 sites; labels for the parts
are in `ghidra/names/carpet_labels.csv` (applied with `ApplyNames.java <csv> labels`).

Call: `poly_fill_triangle(v0, v1, v2)` with three `int[3]` vertices `{x, y, shade}` (the landscape
passes screen x, y and the vertex shade; textured modes take u/v from the UV table `DAT_00093b48`).
Globals: `DAT_0009e309` fill mode (0..26), `DAT_0009e308` constant colour or shade level,
`DAT_0009b5f8` texture (256x256, texel = tex[v<<8|u]), `DAT_0009b5f4/5fc/600/604` destination /
pitch / clip width / clip height, `0x9B608` per-scanline edge table. *Corrected (round 5 merge): vertices are int[5] {x, y, u, v, shade}; see "Port round 4 corrections" at the end.*

1. Sort the three vertices by y (degenerate ones exit through `poly_fill_exit_73f74`); reject when
   the top vertex is below the clip height. Four "cases" remain (which vertex is the middle one and
   on which side it bends).
2. Per case, a 27-entry table (`poly_edge_table_case1..4`, index = fill mode) selects one of four
   edge-setup routines: `flat` (x only), `shade` (x + shade gradient), `tex` (x + u + v), `texshade`
   (x + u + v + shade). They walk the long edge against the two short edges and write one 20-byte
   record per scanline into the table at 0x9B608: `+2 x_left (i16), +6 pixel count (u16), +8 u 16.16,
   +0xc v 16.16, +0x10 shade 16.16`; the per-pixel gradients (du, dv, dshade across the span) stay on
   the stack (`[esp+0x24]`, `[esp+0x4c]`, `[esp+0x3c]`...). Increments are 16.16 with the integer part
   kept in a byte register (`add dx, step; adc bl, step_hi` = u += du). *Corrected (round 5 merge): +6 is the integer part of x_right (exclusive end); +0 / +4 hold the 16.16 x_left / x_right; see "Port round 4 corrections" at the end.*
3. The edge setup jumps through `poly_span_table_73eab` (27 entries, 25 distinct routines) to the
   span filler for the mode, which loops over the scanline records (dest row pointer at `[esp]`,
   advanced by the pitch) with a 16x unrolled inner loop. Tables: `SHADE = 0xB99B0 [level<<8 |
   colour]` (33 levels), `BLEND = 0xBD9B0 [a<<8 | b]` (asymmetric; the two operand orders give two
   weights, which is why most modes come in pairs). *Corrected (round 5 merge): 64 generated shade rows; see "Port round 4 corrections" at the end.*

| mode | routine | pixel |
|---|---|---|
| 0 | span_m00_flat | `rep stosb` constant colour |
| 1 | span_m01_colour_ramp | colour index = running shade gradient (palette ramp; used for the SIRDS depth map) |
| 2 / 3 | span_m02_textured / _keyed | texel; mode 3 skips texel 0 |
| 4 | span_m04_flat_shaded | SHADE[gradient][colour] |
| 5 / 6 | span_m05_textured_gouraud / _keyed | SHADE[gradient][texel] |
| 7, 11 / 8 | span_m07_textured_flatlit / _keyed | SHADE[colour as level][texel] |
| 9, 10 | span_m09_shadow | dest = SHADE[texel][dest] where texel != 0 (the texture is a light map applied to what is already drawn: shadows) |
| 12 / 13 | span_m12_textured_tint (/_swapped) | BLEND[texel][colour] / BLEND[colour][texel] |
| 14 / 15 | span_m14_flat_translucent (/_swapped) | BLEND[colour][dest] / BLEND[dest][colour] |
| 16 / 17 | span_m16_shaded_translucent (/_swapped) | BLEND[SHADE[g][colour]][dest] and swapped |
| 18 / 19 | span_m18_textured_translucent (/_swapped) | BLEND[texel][dest] and swapped |
| 20 / 21 | span_m20_gouraud_translucent (/_swapped) | BLEND[SHADE[g][texel]][dest] and swapped |
| 22 / 23 | span_m22_..._keyed | as 18/19 with texel 0 transparent |
| 24 / 25 | span_m24_..._keyed | as 20/21 with texel 0 transparent |
| 26 (0x1a) | span_m26_water | SHADE[g][texel], blended with dest only while a per-pixel counter stays below a threshold: water with depth-limited translucency *Corrected (round 5 merge): the test is on the texel value: texel < 12 -> blended; see "Port round 4 corrections" at the end.* |

The landscape uses modes 5 (textured, lit per vertex), 7 (flat-lit texture, `DAT_000939d4`
textures) and 0x1a (water, `DAT_00093a78` textures). For the port the edge table + mode table
structure maps directly onto a scanline rasteriser with a `switch(mode)` pixel shader.

## Interrupt handlers (2026-10-06, round 2)

All three were asm blocks the analyzer had not turned into functions; they are functions now
(`repair_functions_2.txt`).

- `timer_isr_4ac03` (int 8, installed by `timer_isr_install_4ac79`): `pushal`, segment pushes,
  `call 0x62794` (DOS/4GW DS load), `DAT_0012eab4++` (the game tick counter), then
  `cfg+0xd2 += cfg+0xce`; when the accumulator passes 0x10000 it subtracts 0x10000 and chains to
  the saved BIOS handler (`DAT_000b7ca0/ca4`) through `0x62c2c`. Finally EOI (`out 0x20, 0x20` via
  `dos_outb_62c4d`) and `iretd`. With cfg+0xce = 0x2726 (10022) the PIT runs at 1193182 / 10022 =
  **119.06 Hz** and the BIOS still sees its 18.2 Hz (10022/65536 of the ticks are forwarded). One
  game tick = 1/119 s; the simulation sub-steps (cfg+0x96) divide that further. *Corrected (round 6 merge): the PIT tick only drives the counter DAT_0012eab4; the game itself is unpaced (one simulation tick per rendered frame, no vsync outside palette fades) and the sub-steps multiply Thing updates per frame (F3 = fast-forward). With sound or music the counter is incremented by the HMI timer event at 120 Hz instead (`timer_tick_isr_34120`); see "Port round 5 corrections" at the end.*
- `input_keyboard_isr_4fa28` (int 9): reads port 0x60 into `DAT_0012eea0` (last scancode). Handles
  the E0 prefix (`DAT_0012e396` = previous byte; E0 2A / E0 AA fake shifts are dropped and replaced
  by 0x80). Sets `DAT_0012ee20[scancode & 0x7f]` = 1 on make, 0 on break; stores the first make code
  since the last read in `DAT_0012e397`. Acknowledges through port 0x61 (pulse bit 7). Scancode 0x0F
  (Tab) with cfg flag 0x80 (debug) calls `0x4cdf0(0, 0x3f, 0, 0)` and sets `DAT_000adfca` = 1; while
  `DAT_000adfca` is set the old int 9 handler is chained (`DAT_0012e390`). EOI, `iretd`.
- `mouse_event_callback_5b86c` (int 33h event handler, far `retf`): EAX = event mask, ECX/EDX = x, y.
  Stores the mask in `DAT_0012ede0`; in 640x480 mode (`DAT_0012edae & 8`) the coordinates are >> 3,
  then clamped to 0x27E x 0x1DE and written to `DAT_0009e5dc/de`. Bits 2/8/0x20 = left/right/middle
  press -> edge flags `DAT_0012ee0e/0c/0a` (set once per press) and click position `DAT_0009e5d8/da`;
  bits 4/0x10/0x40 = releases -> held flags `DAT_0012ee14/12/10` cleared. Unless the blit lock
  `DAT_0009e5d4` is set it redraws the cursor (`0x5b560` restore, `0x5b050` draw) and in VESA mode
  re-selects the bank (`vesa_set_bank_6126c`). `DAT_0009e5e4` = mouse present gate.

## Function inventory cleanup (2026-10-06, round 2)

`DeleteStubs.java` removed 442 bogus functions (304 padding runs, about 100 tail fragments and
garbage starts, 46 functions that began on padding bytes and were recreated at the real entry, see
`names/moved_entries.txt`); `ReflowBodies.java` cleared the stale mis-aligned instruction units in
those ranges and re-disassembled from the owners' flow so truncated bodies grew back. 2,083 -> 1,693
functions after round 2; 1,714 after round 3, all named.


# Round 2 agent findings (2026-10-06, functions named in docs/analysis/agent2_*.md)
Corrections to earlier notes established in this round: `net_receive_from_player_4f470` / `net_send_to_player_4f4d0` were swapped (4f470 issues NCB 0x94 SEND, 4f4d0 NCB 0x95 RECEIVE) and are now `net_send_to_player_4f470` / `net_receive_from_player_4f4d0`; `video_mode_extra_5be68` is `mouse_set_range_5be68` (int 33h fn 7/8); the PIT reload lives at cfg+0xce (accumulator +0xd2), not +0x19c; `hit_record_scores_150f0` is the AI threat table writer; the Thing fields +0x5a..+0x7d are six 6-byte damage slots `{i32 amount, u16 attacker}` indexed by damage type (replacing the separate +0x60/+0x64/+0x76 notes); `castle_mana_tick_42010` is `castle_collapse_level_42010`; `fe_dialog_scroll_text_57617` started mid-instruction, the real function is `fe_menu_text_dialog_57580`; effect types 50/51 are village spawners, not ridge nodes. *Corrected (round 5 merge): the class-10 states 52 / 53 are the living wizard castle and its collapse; types 0x32 / 0x33 are a marker and the ridge raiser; see "Port round 4 corrections" at the end.*

## Region A (docs/analysis/agent2_A.md)

### 2. Notes for docs/ENGINE.md

#### 2.1 Computer-controlled wizard AI (class 3 type 1; 0x11820-0x155xx)

Player type 1 is the AI opponent wizard. Its Table A state-1 handler `player_type1_s1_update_11de0` calls
`player_ai_wizard_tick_11f20` (housekeeping + movement) then dispatches on the **AI mode byte playerrec+0x19f**
and finally runs `ai_choose_goal_12330`, which may switch mode through 23-byte setters (0x138a0..0x13a00). *Corrected (round 5 merge): renamed `ai_wizard_update_11de0`; see "Port round 4 corrections" at the end.*

| mode | handler | meaning |
|---|---|---|
| 0 | - | choose a goal |
| 1 | ai_mode1_upgrade_castle_12470 | fly to own castle and cast the castle spell (upgrade) |
| 2, 5 | FUN_00012550 (ret 0) | unused *Corrected (round 5 merge): now `ai_goal_none_12550`; see "Port round 4 corrections" at the end.* |
| 3 | ai_mode3_fly_to_castle_site_12560 | fly to home pos +0x96 and cast the castle spell |
| 4 | ai_mode4_approach_target_12600 | approach target |
| 6 | ai_mode6_collect_mana_12830 | collect a mana ball (claim ball +0x90 when facing it) |
| 7 | ai_mode7_attack_castle_12950 | attack an enemy castle (spell from 14f00) |
| 8, 9, 0xd | ai_mode8_attack_wizard_12a90 | attack wizard / type-3 player / creature (spell from 14c70) |
| 0xb | ai_mode11_return_home_126f0 | return to own castle |
| 0xc | ai_mode12_idle_12680 | idle cruise |

Goal priority (12330): castle site (12bd0) > retreat when health < half (12f70) > [every think tick] upgrade
castle (12df0) > attack castle (13000) > attack wizard (13210) > attack type-3 (13440) > collect mana (12e90) >
hunt creature (13770) > default (13a20). Think tick = `counter % (0x40 - rec+0x20e/4) == 0`.

Spell ids used by the AI (confirming the create-table order): 0 fireball, 7 meteor, 8 volcano, 0xf lightning,
**0x10 = Castle** (missing from the round-1 list; `ai_cast_spell_14240` case 0x10 creates class 3 type 2),
0x11 skeleton. `DAT_000938c4` = 24 x u16 spell cooldown reload values. `DAT_000962b6 + spell*0xe` is the
handler column of the class-12 Table B (so Table B for spells starts at 0x962ae).

AI-specific player-record fields (0x801-byte records; pointer at thing+0xa0):

| off | meaning | evidence |
|---|---|---|
| +0xc | desired speed (0 or thing+0x80 base) | 13b10, 140d0 |
| +0xe | "moving" flag | 12680 |
| +0x10 | steering value, decays by 4/tick; 0x50 when dodging a projectile | 13b10, 15420 |
| +0x14b | inside-own-castle timer (2): damage slots cleared, fast regen | 11f20 |
| +0x146 | u8 counter to 200 | 11f20 |
| +0x155 | health regen per tick (max/200 in castle, max/500 outside) | 11f20 |
| +0x15f | countdown | 11f20 |
| +0x167 | kills of independent creatures by this wizard | 191d0 |
| +0x194 | fireball burst counter; negative = recovering | 14240, 11f20 |
| +0x196 | conserve-mana flag | 14c70 |
| +0x19f | AI mode | 11de0 |
| +0x1cc[8] | 8-byte entries {u16 threat toward player p (drifts to 0x601f), u16 grudge flag} | 11f20, 150f0, 13000 |
| +0x20a | aggression 0..255 (threat drift rate and hostility threshold `50000 - aggr*health/10/255`) | 11f20, 13000 |
| +0x20c | accuracy 0..255 (aim tolerance ((255-acc)/4+20) degrees; random skip of big spells) | 14640, 14c70 |
| +0x20e | reaction 0..255 (think period 0x40-r/4; turn rate) | 12330, 13b10 |
| +0x210 | set to 200 when one of this player's creatures kills a builder/townie/trader (fragments 1dae5..1e6ec) | 1e0b0 |
| +0x214[24] | spell thing indices (slots) | 14b00, 40240 |
| +0x274[24] | spell acquisition countdowns | 14b00 |
| +0x2a4[24] | spell thing index by spell id (rebuilt each tick by FUN_00040240) | 13ac0 |
| +0x2d4[24] | spell cooldowns (0x2f4 = castle spell) | 14640, 14980 |

`ai_record_threat_from_projectiles_150f0` (was hit_record_scores) is the only writer of +0x1cc from combat; it runs
from thing_update_all for all players, so human players also accumulate threat tables (unused for them).

#### 2.2 Damage slots

thing+0x5a..+0x7d is **six 6-byte damage slots indexed by damage type** `{i32 amount, u16 attacker idx}`
(`thing_add_pending_damage_117c0`, `thing_try_damage_116dc`, memset of 0x24 bytes in 11f20). The creature code
only consumes slot 0 (+0x5a/+0x5e); thing+0x1c is the bitmask of damage types a thing accepts (`1 << type`).
This supersedes the round-1 reading of +0x60/+0x64/+0x76 as separate fields for mana balls (they are slots 1 and 4).

#### 2.3 Creature state machine layout (confirmed for all 17 types)

Each creature type occupies 6 consecutive Table A states: +0 idle / seek flock leader
(`creature_idle_seek_leader_18610`), +1 attack (`creature_attack_target_18c20` with a per-type attack callback
pushed on the stack: dragon/worm 193f0, vulture/bee melee 1961e, archer/emu arrow 1949e, crab volley 19680,
troll 19940, griffon 199ee), +2 follow leader (`creature_follow_leader_18e90`), +3 dying (`creature_die_191d0`,
kill credit playerrec+0x167), +4 dead (`creature_dead_drop_mana_19310`: mana ball + death effect + delete),
+5 type-specific (e.g. dragon s6 = seek + move; crab s33 = collect mana; skeleton s56 = attack with 19550;
genie s72 = place a castle for its Wizard target). Flocking: +0x34 leader, speed copied from the leader plus
leader+0x82; separation radius 0x100. *Corrected (round 5 merge): +1 is main, +2 attack, +3 follow, +4 dying, +5 dead; the type-specific examples named here belong to the *next* type's +0 state; see "Port round 4 corrections" at the end.*

Descriptor (+0x9c) fields used here: +2/+4 min/max turn rate, +0xa/+0xc ground clearance, +0xe z step,
+0x14 allowed-terrain mask, +0x1a think period, +0x1c sight radius, +0x1e fov. Attack projectiles use
descriptors DAT_00096a10 (+i*0x20), 96a50, 96a70, 96ad0.

#### 2.4 Castle placement helpers

`castle_footprint_clear_11980` / `castle_site_clear_at_pos_11be0` test DAT_000fdfb0 bit 0x80 (built cell) over
the castle rectangle and castle-vs-castle overlap; `castle_crush_wizards_118c0` kills Wizard effects under a castle
footprint; `castle_size_half_extents_1d4b0` reads the 6-byte castle size table (+4 w, +5 h). The Genie creates
castles for Wizard effects (type 0x2d) via `effect_wizard_init_35090` + state 0x33.

#### 2.5 Misc

- `spiral_search_init_101b0` builds the ring tables from a 32x32 image loaded with file_load_rnc (the ring map
  file; name not recoverable here) - DAT_000ade28 = 32 x {ptr, count} 12-byte records. *Corrected (round 6 merge): the file is data/search.dat (string 0x90004) and the records are **6 bytes** {SpiralCell *cells, u16 count} (`imul esi, esi, 6` at 0x10203); typed as `g_spiral_rings` / `g_spiral_iters` in carpet_types.txt.*
- `dos_is_cdrom_drive_10010`: CD-ROM detection by int 21h/36h signature (0xFFFF clusters, 2048-byte sectors).
- Thing slot 0 (state+0x7463) is used as a scratch Thing by the AI castle-site search (12bd0 writes +0x18/+0x48).
- `FUN_0004cdf0` writes a VGA DAC entry (out 3c8h/3c9h); `creature_follow_leader_18e90` flashes colour 0 red when a
  follower has no leader - a leftover debug aid.

#### 2.6 Open questions

- Which file the ring image in 101b0 comes from (file_load_rnc arguments are in registers).
- Player type 3 (goal 13440) and creature type 15 (wander along cell lines, 1ef80): roles still unnamed.
- `skeleton_convert_villager_1c1e0` creates a thing at the victim position with class/type in registers; verify it
  is a skeleton (would explain why skeletons hunt archers/builders/townies).
- `creature_attack_fire_offset_19a90` is unreferenced in the call graph; the kraken (1b000) and wyvern handlers may
  pass it in a register.
- The mode-2/5 stub FUN_00012550 and goals 12d6e / 135fe have no callers: cut features of the AI.

## Region B (docs/analysis/agent2_B.md)

### 2. ENGINE.md section: terrain generation, terrain painting, constructors (region B, round 2)

#### 2.1 Terrain generation pipeline (terrain_build_303f0)

Order of the passes called by `terrain_build_303f0` after the heightmap exists (loaded 64 KB file or
`terrain_fractal_fill_71f08` + `terrain_generate_313a0`), with the flag-byte (0xFDFB0) low nibble used
as a **terrain class** during generation:

| step | function | effect |
|---|---|---|
| 0 | mem_set(0x10DFB0, 0, 0x20000) | clear the u16 scratch / cell->thing map |
| 1 | terrain_carve_rivers_31430(n, min_h) | class = 5 (land) or 0 (sea) by height != 0; n rivers traced downhill by terrain_trace_river_314e0 (visited map in 0xCDFB0, river cells -> class 0, heights lowered monotonically); texture map := 0xff |
| 2 | terrain_flatten_water_quads_31e50 | class-0 quads levelled to their min height until stable |
| 3 | terrain_classify_flat_309f0(thr) | class 5 with cross range < thr -> 3, == thr -> 4 |
| 4 | terrain_mark_lowland_31650(max_h, max_range) | low smooth non-water -> 5 |
| 5 | terrain_insert_transitions_30c50 | class 4 inserted between 0/3/5 in every 2x2 quad |
| 6 | terrain_mark_interior_31800(max_h, max_range) | class 5 surrounded by 5/2 -> 2 |
| 7 | terrain_mark_steep_31ad0(min_range) | steep -> 6; steep at mixed class boundaries -> 1 |
| 8 | terrain_flags_fill_holes_308f0 | majority fill of single-cell holes |
| 9 | mem_set(0xCDFB0, 0, 0x10000) | clear texture map |
| 10 | terrain_smooth_spikes_30500 | remove 1-cell spikes/pits on land |
| 11 | terrain_fix_shore_quads_30810 | water/transition quads touching height 0 flattened |
| 12 | terrain_assign_textures_30eb0 | textures from the 7^4 corner-class table (below) |
| 13 | terrain_mark_water_anim_30690 | flag bit 8 (animated water) on all-zero water areas with texture 0 |
| 14 | terrain_build_lightmap_31310 | 0xEDFB0 = 0x20 - NW/SE slope, with noise for flat cells |

Resulting class meanings (flag & 7): 0 water, 1 cliff edge at mixed boundaries, 2 interior lowland,
3 flat (higher) land, 4 transition, 5 default land, 6 steep. The generator's RNG is the 16-bit
`DAT_0012dfb0` (LCG *0x24a1 + 0x24df), seeded from the level header (+4) in terrain_build_303f0.

The numeric arguments of steps 1,3,4,6,7 are pushed by terrain_build_303f0 (not visible in the C);
they are the level's terrain parameters - worth reading from the disassembly of 303f0 when porting.

#### 2.2 Texture assignment and the corner-class table

`terrain_assign_textures_30eb0` reads a 0x94-entry table of 4-byte class tuples (one tuple of corner
classes per texture, index = texture number; the table is the `""`-addressed data right after the
code, exact address in the disassembly at 0x30eb0) and expands each tuple into the 8 symmetries
(rotation codes 0x10,0x20,...,0x70 stored in the flag high nibble). The lookup is keyed by
`c0*0x157 + c1*0x31 + c2*7 + c3` (7^4 = 2401 buckets of 0x19 bytes: count, 12 texture ids, 12
rotation codes) and built in the frame buffer as scratch. A compact copy (first match: texture,
rotation; `{1,0}` when none) is kept at **DAT_000b58b0** (2401 x 2 bytes) and is what the run-time
retexture functions use:

* `terrain_retexture_rect_324e0(xy0, xy1)` / `terrain_retexture_rect_force_32760`: re-derive the
  texture of every cell in a rectangle from the 4 corner classes (random rotation for textures < 8),
  then rebuild the light map and clear water bits. 324e0 skips cells flagged 0x80 (built-on).
* `terrain_paint_cell_32150(cell, kind)`: the "paint kinds" used by explosions and castle building:
  0..7 class paint (then retexture), 8/9 textures 8/9, 0xf texture 0xb, 0xa..0xe slope textures from
  the 2-byte tables at 0x94298 (+0x10 per kind, +8 when nearly flat), 0x10..0x16 from the tables at
  0x94218/0x94258/0x94268/0x94278/0x94228/0x94238/0x94248 indexed by `terrain_slope_orientation_31f90`
  (highest corner 0..3 or ridge edge 4..7; `DAT_00093fc4` = nearly flat). Sets flag 0x80 on the cell.
* Flag byte 0xFDFB0 layout (consolidated): bits 0-2 class (generation) / low nibble set by paint
  kinds; bit 3 animated water; bits 4-6 texture rotation/flip (renderer: `>>2 & 0x1c` selector);
  bit 7 built-on / castle footprint (also set by the raise effects on their perimeter).
* Texture ids 6..0x22 are castle textures (`terrain_smooth_cell_34a40` refuses to smooth them);
  texture 8 is tested by the terrain-raise effects (`terrain_cell_raise_needed_24e20`).

#### 2.3 Castle footprint maps

The 6-byte castle-size table indexed by thing +0x47 is `{u32 map_ptr, u8 w, u8 h}`. The map is
span-encoded exactly like the 2D sprites (0 = end of row, n<0 skip, n>0 copy n bytes). Each byte is
a terrain instruction: 7..0xe flat at the castle base height; high nibble 1/2 -> paint kind 8/9
(castle textures); high nibble 3 -> terrace (+0xc / +0x10 height) with slope kind 10 + (low%16)/3 and
orientation low%3; high nibble >= 4 -> height base + (nib-1)*4 and paint kind nib+0xb.
`castle_stamp_footprint_26320` applies it instantly (level start, caller FUN_3f360),
`effect_castle_raise_terrain_s44_26f0e` animates it over 19 ticks while the player builds, and
`terrain_smooth_castle_border_348b0` smooths the surrounding ring. Oddity: both halve w/h when
`DAT_0012edae == 1` (320x200) - the footprint table is probably shared with the HUD castle icon. *Corrected (round 5 merge): the height uses the low nibble: base + (low - 1) * 4; also: 18 height steps plus a 1- or 25-tick wait; the 320x200 halving is settled by the per-tick reference (the tab relocation doubles building.tab w / h at 320x200, the halving restores them); see "Port round 4 corrections" at the end.*

#### 2.4 Constructors (Table B) - entry layout and common fields

Every Table-B constructor is `push ebx; call thing_alloc_35560; mov ebx,eax` (8 bytes) followed by
the body (`test eax,eax; je fail; ...`). The gap finder produced a second function at entry+8 for 29
of them; they are listed as FRAGMENT above with the constants they set. Patterns recovered:

* Projectiles (class 9, 0x37cb0..0x386b0): speed +0x7e = +0x80 = 0x180, +0x8c = 0x32, lifetime
  +8 = range/speed with range 0x2000 (most), 0x1000 (type 1 homing, 0x11), 0xe00 (type 9 lightning),
  0x800 (type 0xc), 0x1400/0x1e00 (type 0xd, table vs standalone 383cc); descriptor (+0x9c)
  DAT_00096a30 (types 2-7, 0xa-0xc), DAT_00096a50 (1, 0x11), DAT_00096a90 (8, 9), DAT_00096ab0
  (0, 0x10, 0x12, 0x13); flag 8 (collidable) cleared; type 1 sets collision filter class +0x42 = 10.
  Types 0x10..0x13 map to states 0x11..0x14 (state = type + 1 above 0xf).
* Switches: all 32 table stubs call `switch_create_common_39dc0(pos, type, state)`.
* Scenery (dolmen, bad stone, domes): +0x1a = thing index % 11 as animation phase, half-size bbox
  via `thing_set_sprite_small_352d0`.
* Effects set +0x2c (damage) and +8 (life) and typically `flags &= ~0x20008; flags |= 0x200`. *Corrected (round 5 merge): it is `|= 0x20000` (recyclable); see "Port round 4 corrections" at the end.*
* Bbox helpers: `FUN_000353d0(thing, xy, z)` sets +0x50/+0x52 = xy and +0x54 = z;
  `thing_set_sprite_double_35340` / `thing_set_sprite_halved_35380` scale the sprite-table extents.
* `effect_create_type37_397c0` (class 10 type 0x25 -> state 0x27, damage 64000) and
  `projectile_create_type13_long_383cc` are standalone constructors with their own thing_alloc
  and no callers in the export - probably reached through pointers or dead.

#### 2.5 Level data

* `level_spawn_thing_record_35800` (THING_INIT -> thing): field 6 (SwiId) is reused per class:
  switch id (class 11, -> +0x18), spell phase offset (class 12; > 2 means a dropped spell: state-3,
  sprite 0x118, flag 0x40000), owner for effect type 4; fields 7/8 (Parent/Child) are the
  destination cell of a Teleport (effect 0x22 -> +0x98/+0x96).
* `level_load_levels_dat_3bfc0`: levels are stored in one RNC container: a 4000-byte index of u32
  offsets (room for 1000 levels; the caller checks level < 1000) followed by RNC-packed level records
  that unpack to 0x979c bytes ("ERROR decompressing levels.dat").
* `level_skip_number_329c0`: level numbers 8, 0x11, 0x1c, 0x21, 0x27 are advanced past (speculation:
  these are the campaign positions where a cut-scene/intermission level sits). *Corrected (round 5 merge): the skipped levels are normal playable levels (Round 4 region B); see "Port round 4 corrections" at the end.*

#### 2.6 Smaller systems

* Mana balls: size thresholds `DAT_00093910[7]` (int); sprites 0x69 + player*8 + size (player from
  owner playerrec+0x30), 0x34 + size when unowned (`mana_ball_update_sprite_25e20`).
  `thing_drop_mana_ball_25fe0` is the shared "drop carried mana" used by creature death and players.
* Village effects: types 50/51 (states 52/53, currently named ridge_node/type51) spawn random
  villagers through `creature_spawn_random_villager_27660` (Archer 2/12, Trader 2/12, Townie 5/12,
  Builder 3/12) and use `thing_apply_pending_damage_27f90`; state 53 also smooths terrain
  (`terrain_smooth_rect_34a00`). The level.py name "Ridge node" for type 50 looks wrong. *Corrected (round 5 merge): states 0x34 / 0x35 are the living wizard castle (type 0x2d) and its collapse, now `effect_wizard_castle_s52_update_27710` / `effect_wizard_castle_collapse_s53_update_27930`; see "Port round 4 corrections" at the end.*
* Movie subtitles (ftext.dat): `movie_subtitle_init_23600` / `_show_236a0` / `_set_colour_23740`,
  strip at frame+0xe100 (DAT_000b2e04, size 0x4b00), state DAT_0009390a/0c, colour DAT_000adfd6,
  driven by cue_script_step_17d80.
* Music: `music_update_1f800` state bytes DAT_000938f8..fb (f9 = current mood 1/2, fb = fade step
  +-2, fa = HMI timer event pending), DAT_0009e310 = volume 100.
* Memory names: '*SearchD' (spiral-search buffer, 0x96df0) and '*WScreen' allocated by
  `video_alloc_buffers_33480`.
* Player record byte +0x340f (first byte of the 0x801 record) is set to 2 by `player_cheat_check_3bbd0`.

#### 2.7 Open questions

1. `terrain_ring_find_height_ne8_24d70`: the ring-start radius comes from an uninitialised stack byte
   in the decompile; verify in the disassembly whether a register argument initialises [esp+8].
2. What texture 8 is (tested by the raise effects 24fc0/24eb0/250b0 and by 24d70): probably the
   plateau/cliff-top texture produced by paint kind 8.
3. The numeric arguments pushed to the generation passes in terrain_build_303f0 (river count, class
   thresholds) - read the disassembly of 0x303f0 for the exact constants.
4. The 0x94-entry corner-class tuple table and the slope-texture tables 0x94218..0x94298 should be
   dumped and documented as data (they define the whole texture vocabulary).
5. Why castle footprint dimensions are halved in 320x200 mode (26320, 26f10) - if the footprint is
   also the HUD castle icon, the terrain result differs by resolution, which would be a bug. *Corrected (round 5 merge): resolved by the per-tick reference; see "Port round 4 corrections" at the end.*
6. `FUN_00010010` (called by player_cheat_check_3bbd0) and `FUN_0003f240` (fire) are unnamed; the
   player record byte +0x340f meaning (value 2) is unknown.
7. 0x24d10 (effect_rain_of_fire_s26) is only ~0x44 bytes before its jump table at 0x24d54; check
   the function split there. *Corrected (round 5 merge): 0x24d10 is the type-0x1a handler, now `effect_type26_s26_update_24d10`; see "Port round 4 corrections" at the end.*

## Region C (docs/analysis/agent2_C.md)

Fragment list (19): 0x3c1d1, 0x3e630, 0x404b9, 0x4099c, 0x40a81, 0x41404, 0x414b6, 0x425d1, 0x4265c, 0x439ec, 0x43a70,
0x43bc8, 0x451fb, 0x46857, 0x46d8b, 0x46e17, 0x47584 (tail of spell_speedup_update_47420: restores P+0xc speed and
+0x7e and clears flag 0x80), 0x49dc2, 0x49ff0.

Padded starts named at the todo address (real entry in the comment): 0x3d7ce (+2), 0x3daeb (+5), 0x3e72e (+2),
0x466af (+1), 0x49ccc (+4), 0x49e96 (+10), 0x49ede (+2), 0x49f0c (+4), 0x49f3c (+4), 0x4a16e (+2), 0x4aa81 (+15).

### 2. ENGINE.md section: players, castle, projectiles, spells (region C, agent pass 2)

#### Player record layout (corrections and new fields)

The player record is `state + 0x340b + p*0x801` (3f360 computes `p = (rec - (state+0x340b)) / 0x801`; the +0x340F
quoted earlier is rec+4). `thing+0xa0` of every thing owned by player p points at **rec+0x44f** (3f360, 3dc10), so
the "owner player record" fields documented as thing+0xa0+X are at rec+0x44f+X. Fields of that sub-block `P` seen
in this pass:

| P+ | meaning | evidence |
|---|---|---|
| +0 | u32 input bits from the command packet (1/2 accel/decel, 4/8 turn) | 40e70, 3a8b0 |
| +4/+6 | i16 steer deltas (mouse) -> added to +0x147/+0x149 | 3fc00, 3a8b0 |
| +0xc | i16 target speed (-0x50..0x50) ; +0xe accelerating flag; +0x10 turn rate (-0x50..0x50) | 40e70 *Corrected (round 5 merge): +0x10 is the strafe speed; see "Port round 4 corrections" at the end.* |
| +0x16 | knockback strength (dmg/10, max 0x50, decays 4/tick); +0x18/+0x1a knock yaw/pitch | 40b70, 3fc00 |
| +0x1c | camera pitch (terrain-following pitch & 0x7ff) | 3fc00 |
| +0x30 | player number; +0x32 castle thing idx; +0x34[3] balloon idx; +0x54[34] castle guard idx | 3f360, 419a0 |
| +0x122/+0x126 | balloon mana totals (recomputed each castle tick) | 419a0 *Corrected (round 5 merge): +0x126 is cleared per loop iteration, +0x122 never; see "Port round 4 corrections" at the end.* |
| +0x134 | mana in transit (added to castle mana in capacity checks) | 41720, 3ef10 |
| +0x13a/+0x13c/+0x13e | rubber-band target idx / timer (200, released at 1000) / length (0x400..0xc00) | 40b70, 3fc00 |
| +0x142 | mana; +0x146 aim byte copied into projectile +0x1a | 3f360, 47840 *Corrected (round 5 merge): the projectile aim byte is never read; see "Port round 4 corrections" at the end.* |
| +0x147/+0x149 | yaw/pitch accumulators (yaw advances by +0x147/8 per tick) | 3fc00 |
| +0x14b | 100; +0x155 health/2000 or /250; +0x15f 2000 countdown; +0x210 timer | 3f360, 404b9 |
| +0x157/+0x15b | shots / hits; +0x167 kills; +0x16b spells %; +0x16f accuracy %; +0x173 mana %; +0x177 overall %; +0x17b start tick / elapsed | 3ef10 |
| +0x17f/+0x188 | hit flash (0x10 / 4); +0x187 castle-hit flash | 40b70, 42460 |
| +0x18c/+0x190 | nearest fire effect / nearest ridge-node distance (reset to 0x800, sound when < 0x600) | 3f240, 3f2c0, 3fc00 |
| +0x198/+0x19a/+0x19c | pending displacement added to the position next tick (teleport / rubber band) | 3fc00 |
| +0x1a0 | castle level; +0x1cc[8] threat table (0x9fdf default vs flyers, 0x601f for AI) | 41f00, 3f360 |
| +0x20a/+0x20c/+0x20e | castle position (copied from the level block `state+0x385d7 + p*0xd8`) | 3f360 *Corrected (round 5 merge): AI aggression / accuracy / reaction from level block +4 / +0xc / +8; see "Port round 4 corrections" at the end.* |
| +0x214[24] | spell slot -> spell thing idx (templates: negative = empty); +0x274[24] AI "wants spell" timer (200); +0x2a4[24] spell id -> thing idx (40240); +0x304[10] queue of newly picked slots; +0x31c[24] AI allowed; +0x37c[24] found flags; +0x394[24] sealed (-> sprite 0x118, flag 0x40000); +0x3ac selected slot | 3f360, 46ae0, 40240 *Corrected (round 5 merge): P+0x304[24] is the quick-select key -> book slot table; see "Port round 4 corrections" at the end.* |

Other per-level state: `state+0x23d9 + p*6` start position, `state+0x38c97[p]` castle level per player,
`state+0x38ca3[24]` spells present in the level, `state+0x38c9f` creature count (3f360, 3ef10).

#### Thing flags found here (u32 at +0x10)

0x1000 = standing on a dolmen (43de0 sets it on the player each tick); **0x4000 = shield active** (47760; 40b70
quarters damage and charges it to mana); **0x8000 = rebound active** (48490; 44150 reflects projectiles whose impact
class/type is 10/1 or 10/0x11); 0x40 on a castle = upgrade requested (42460 -> state 5). The earlier note "0x40 mana
ball being collected" applies to balloons only.

Projectile fields: **+0x44 / +0x45 = class/type of the effect spawned on impact** (10/0 explosion for fireballs and
mini fireballs, 10/0xb crater, 10/0xf earthquake, 10/0x11 meteor, 10/0x17 lightning, 10/0x22 teleport whose life
= +0x2c), +0x96 = launch/target position, +0x1a = aim byte from P+0x146, +0x7c on a castle = owner idx of an upgrade
request. Castle +0x1a = level 0..7.

#### Castle state machine (class 3 type 2)

* Created in state 5 with +0x30 = 0. **State 5 (41500) = build/upgrade sequencer** on +0x30: 0 -> site check
  (118c0/11980) then `castle_begin_build_stage_41f00` (effect type 42, level++, +0x30 = 4); 4 -> wait; 3 ->
  `castle_spawn_build_effect_2a_41610` (+0x30 = 4); 5 -> `castle_spawn_build_effect_29_41670` (+0x30 = 6); 2 ->
  state 4. The build effects carry the level in +0x47 (castle size index).
* **State 4 (413a0) = active castle**: `castle_take_damage_42460` (2 -> state 6; flag 0x40 -> state 5 upgrade),
  `castle_spill_mana_41720`, extents, `castle_manage_balloons_and_guards_419a0`, pick up own mana balls; +0x32 = 1
  -> state 5 step 3 (rebuild).
* **State 6 (416d0) = destroyed**: `castle_collapse_level_42010` (10% mana spilled, level-1, collapse effect; level 0
  -> castle removed) then back to state 4 with +0x32 = 5.
* Level table (42200/42370): capacity +0x88 = 5000 << level (level 7 = 30,000,000); max health 0, 20000, 40000,
  40000, 60000, 60000, 80000, 80000; balloons 0,1,1,1,2,2,3,3; guards (creature type 15) 0,0,0,4,6,14,18,34.
  The Castle spell (spell 16, P+0x2c4) has its total mana set to the capacity of the next level.
* Creature type 15 ("type15" in level.py) is the castle guard archer spawned by 419a0 at castle pos + (0x80,0x280).

#### Spell life cycle

Phase 0 (state = spell*3) is the owned spell's cast handler (+0x30 remaining ticks, +0x32 total). Phase 1
(472f0 -> 46e50 -> `spell_dropped_update_46ae0`) is a spell lying on the ground (dropped on death, 405f0); phase 2
(47300 -> `spell_phase2_pickup_46dd0`) is the level pickup: on contact it creates a fresh spell thing from table B and
sets its state += 2 (**open question**: why +2 and not phase 0; possibly phase 2 = "owned, in book" and phase 0 only
while a template is active - check 402c0/16660 which read P+0x214 things). Casting is started by
`player_cast_spell_410f0` (+0x30 = +0x32; spells 2 and 0x15 are mutually exclusive; spell 16 refuses while busy). *Corrected (round 5 merge): the picked Thing becomes the owner's phase-0 spell and the new Thing stays in phase 2; see "Port round 4 corrections" at the end.*

#### Misc

* `demo_relink_state_pointers_3dc10` confirms descriptors live in a table at DAT_96af0.. and that saved states
  store raw pointers.
* `sprite_set_group_priority_4bf60` / `sprite_groups_reload_by_priority_4bfb0`: sprite record byte +0xb (0x97683) is
  a load priority, DAT_b9791[group] holds it, and switch_activate reloads sprite groups by priority after spawning.
  Table DAT_9649e (34-byte records keyed by two u16) lists the sprites a (class, model?) needs - verify the key.
* 4bd10 shows sprite record +6/+8 are world extents (one derived from the tmap aspect ratio) and +0xc the tmap
  header draw-type byte.
* Open: identity of the record with +0x18 == &DAT_ae89e in 3f360 (gets 1,000,000 health/mana - attract-mode or
  network dummy player?); exact meaning of the 14-byte log written by 3e080 (rec+0x24a, count rec+0x10); what
  `terrain_cell_flag_check_3d5f0`'s cell-type values 2/3/5 are (low nibble of DAT_fdfb0). *Corrected (round 5 merge): 0xae89e is the 1,000,000-health dummy; the 14-byte log is the camera log read by render_frame_1fab0; see "Port round 4 corrections" at the end.*

## Region D (docs/analysis/agent2_D.md)

### ENGINE.md section: region D findings (agent 2, 2026-10-06)

#### Corrections to existing names

- `net_send_to_player_4f4d0` and `net_receive_from_player_4f470` are **swapped**. 4f470 calls 4ecf0, which
  issues NCB 0x94 SEND (`net_send_4ec80`) in 0x800-byte blocks; 4f4d0 calls 4ebb0, which issues NCB 0x95
  RECEIVE (`net_receive_4eb30`). So 4f470 = send to player, 4f4d0 = receive from player. Read with that in
  mind, `net_exchange_frame_4f530` becomes: host (local index == DAT_0009e3fa) receives from every other
  player then sends to all; a client sends first then receives. Both re-listen (`net_listen_4ea10`) when the
  player's NCB cmd_cplt (+0x31) is nonzero.
- `fe_dialog_scroll_text_57617` is not a function start: 0x57617 is inside the instruction `push 0x516cc`
  at 0x57615. The real function is `fe_menu_text_dialog_57580` (0x57580-0x57888, 775 bytes), the main-menu
  item 2 handler from table 0x5167c. Delete the 57617 symbol when fixing function bounds.
- `video_mode_extra_5be68` is `mouse_set_range_5be68`: int 33h functions 7 and 8 set the mouse x/y range
  (640x400 for modes 1 and 4, 640x480 for mode 2, 5120x3840 for mode 8).
- `ui_clip_rect_width_588f0` returns the whole clip rect (6 dwords), not just the width.
- `sprite_cache_*` (4c880/4cb10/4ca50/4c610) and the new `texture_anim_table_*` names (4c7f0/4c9be/4ca66/
  4cab0/4cb50) operate on the same table: {u16 count; ptr to count x 0x1c records} created by 4c7f0 and
  stepped by `texture_anim_next_frame_4c9f0`. The 0x1c record is the texture-animation record
  (+0 active, +4 ptr to sprite ptr, +8 offset, +0xc 6, +0xe first, +0x10 count, +0x12/+0x14 w/h,
  +0x16 frame, +0x18 slot, +0x1a sprite id). Consider renaming the sprite_cache_ names to texture_anim_.
- Three todo entries are data, not code: 0x4cdb0 (sqrt seed table), 0x51eac (tail of the scancode->ASCII
  table at 0x51E55), and 0x4f6c1 is a fragment inside 4f61e.

#### The "alternate device" is the Forte VFX1, driven through an int 33h driver extension (confirmed)

`vfx1_init_4fdc0` allocates a DOS transfer buffer (`dpmi_alloc_dos_mem_502f0`, int 31h 0100h ->
DAT_0012e628), reads the VIP board port from the `VFX` environment variable (default 0x300 ->
DAT_0012e624 index, DAT_0012e61c = port+1 data), then probes the VFX1 driver: `vfx1_find_driver_fn_base_4fba0`
issues real-mode int 33h (DPMI 0300h, register block DAT_0012e634, 0x32 bytes, AX at DAT_0012e650) with
AX = 0x60xx|7F .. 0x70xx|7F until the driver answers AX = 0x7F00|xx; the function base goes to DAT_0012e5bc.
Driver calls then use AX = base|3 (query: `vfx1_driver_query_4fc20`, copies DAT_0012e64c bytes from the DOS
buffer -> info block DAT_0012e5f4), base|4 (`vfx1_driver_send_block_4fca0`, 256 bytes to the driver) and
base|5 (`vfx1_driver_read_tracker_4fd20`, head-tracker report). `altdev_read_50080` (now
`vfx1_read_tracker_50080`) is polled from `input_joystick_poll_5a4e0`: DAT_0009e43c = head tracker present ->
DAT_0012e62c/2e/30 (three axis words, yaw/pitch/roll), DAT_0009e43d = CyberPuck present ->
DAT_0012e66a/6c/6e + buttons DAT_0012e670. `vfx1_driver_cmd_6008_50120` is an extra int 33h AX=6008h call
with BX taken from the info block. `vfx1_vip_set_palette_50180` mirrors the game palette into the VIP
board's own DAC (index regs 7, 6, 9, 10 + 768 bytes), called from `vga_set_palette_302f0`.
Speculative: the names "CyberPuck" and the axis meaning are from the VFX1 product, not from strings.

Ports 302h/303h (`stereo_page_blank_50370`: 303h = 10h, `stereo_page_flip_503d0`: 303h = 1, both preceded
by 302h = 2) are constants and not the VIP port variable; the device behind them is still unidentified
(3D shutter glasses or the VFX1's stereo page register). Flagged `?`.

#### sndsetup.inf and HMI music initialisation

`sound_initialise_34140` calls `sound_init_from_sndsetup_4d8f0` (SOUNDFX line) and
`music_init_from_sndsetup_4d2b0` (MUSIC line). Both build the path with sprintf("c:%s/sndsetup.inf") from
the `\carpet.cd` environment value, create the file with defaults `SOUNDFX = none 0 0 0` /
`MUSIC = none 388 0 0` when missing, parse `%s = %s %x %d %d` (name, card, port, irq, dma), and accept an
environment override (`BF_SOUND` / `BF_MUSIC`, format `%s %x`). Card name `none` disables the subsystem.
Music card names: ADLIB SBLAST SBPRO ADLIBG SB16FM GRAVIS PASFM COMPATIBLE ROLAND SBAWE32 GENERAL WBLAST
(strings 0x92c2b..0x92c90). `music_init_hmi_4d550` receives the HMI device id in EBX and switches on it:
0xA001 -> DAT_0012e06e = 2, 0xA002 -> loads `data\inst.bnk` and `data\drum.bnk` (through
`file_load_rnc_alloc_far_4d880`, far pointers DAT_0012e068/6c and DAT_0012e062/66) and sets DAT_0012e06f = 1,
0xA004 -> 1, 0xA008 -> 2. From HMI SOS headers (recollection, flag): 0xA001 = MPU-401, 0xA002 = OPL2 FM,
0xA004 = internal/OPL3?, 0xA008 = AWE32. [*Corrected (round 6 merge): the music .tab file names settle it: 0xA001 MPU-401 (General MIDI, *.GEN, bank -2), 0xA002 OPL2 FM (*.HMP, bank -0), 0xA004 Roland MT-32 (*.ROL, bank -1), 0xA008 AWE32 (*.GEN, bank -2).*] Then `music_load_bank_5c870` loads music%d-%d.dat/.tab and
`music_bank_relocate_5c924` relocates the 0x20-byte tab records (+0x12 += data base DAT_0012dfe8) and
counts them into DAT_0009e316 (track count). `sound_bank_relocate_5ca58` does the same for the sample bank
(DAT_0012e1d4 .. DAT_0012e240, base DAT_0012e1b0, count DAT_0012e246). Both were decompiled as empty bodies.

#### Sound fades

`sound_request_fade_in_4e0f0` stores the target volume in DAT_0009e3c0 and, for request mode 3, calls
`sound_play_fade_in_4e120`: an already-playing (owner, sample) channel is flagged in DAT_0012e270 with the
target in DAT_0012e2b0[ch]; otherwise a new channel is started at volume 0 with flags 0x4300 and flagged.
`sound_start_fade_out_4e400` flags DAT_0012e330[ch], saves the current volume in DAT_0012e350[ch] and the
argument in DAT_0012e2f0[ch]. The per-tick steppers are the known `sound_update_fadein_4dfc0` /
`sound_update_fadeout_4e2e0`. `sound_play_sample_loud_5cd60` (cue script) starts at 0x7FFF with flags 0x100.

#### Two FLI players

1. Memory-stream player (`fli_play_508f0`, in use): `fli_mem_read_504f0` reads from the pointer
   DAT_0012eab0; chunk decoders `fli_mem_decode_color256_509f0` (type 4), `fli_mem_decode_ss2_50a90`
   (type 7), `fli_mem_decode_lc_50be0` (type 12) and `fli_mem_decode_brun_50d00` (type 15) write into
   DAT_0009e444 with width DAT_0012eaa8 and height DAT_0012eaaa. *Corrected (round 6 merge): `fli_play_508f0` plays the intro\*.dat files: `fli_read_frame_50520` reads each frame chunk from the file (position DAT_0009e450) and the decoders above read it from memory; the separate in-memory chunk-stream player is `flic_play_chunk_50dfd` (port_fli.md).*
2. File-stream player (`fli_file_play_5c40b`, no callers): `fli_stream_read_5cac0` reads from a preloaded
   buffer (DAT_0009e844 buffer, DAT_0009e848 cursor, DAT_0009e84c end, DAT_0009e850 position; filled by
   `fli_stream_preload_5cb50`) or from the file; header fields DAT_0009e6de/e0/e2 (w, h, depth);
   decoders 5c609 (SS2), 5c6e0 (LC), 5c787 (BRUN) write straight into DAT_0012ed74; pacing by int 21h 2Ch
   hundredths with delay DAT_0009e704 (`fli_file_wait_frame_5c813`). `cue_script_step_17d80` still calls
   the preloader, so the buffer globals are live even though the player itself is dead code. *Corrected (round 6 merge): the preload call of op L is guarded by DAT_0009e844, which only the dead player fills, so it never runs; `fli_file_play_5c264` has no reference anywhere in the image.*

#### Front end helpers

- `fe_wait_ticks_or_input_529d0(callback, n)` waits n*0x78 ticks (DAT_0012eab4) or any key/mouse event, and
  `fe_check_shift_q_quit_52980` makes Shift+Q (scancodes 0x2a/0x36 + 0x10) quit to DOS from the logo/title
  screens (DAT_0009e504 = 1, player record +0x340f = 1).
- `fe_lobby_slot_connecting_55920` / `fe_lobby_slot_clear_55a60` are the progress callbacks the NetBIOS
  layer (`net_add_name_4e530`, `net_call_4e600`) invokes to animate lobby slot p (DAT_0009e522[p*3] = 1 / 0)
  and redraw the session name cfg+0x75 in the rect (0xad, 0x2a, 0x5a x 0xf).
- `fe_dialog_draw_title_a_578a0` / `_b_57930` are confirm-dialog draw callbacks that centre the strings at
  DAT_000add70 / DAT_000add74 (string pointer globals; their contents were not resolved).
- `fe_fli_composite_bg_579c0` is the FLI frame callback used by the text dialog: colour 0 pixels of the
  back buffer are replaced by the background copy (64000 pixels, i.e. 320x200 only).
- Text editing: `ui_str_prepend_58820`, `ui_str_delete_at_58880`, cursor via `gfx_fill_rect_clipped_5940c`
  (clipped to the sprite window DAT_0012ed80/88/90/98/a4/a8; only fill mode 0 implemented).
- `ui_font_relocate_glyphs_58f30` adds the font base to every glyph offset smaller than the base (6-byte
  entries), making font files position-independent after loading.

#### Texture cache (tmaps) container

`tmap_cache_create_4c280` builds a 0x1a-byte header: +0 total size, +4 free bytes, +8 directory
(0x211 x 0xe entries {+0 data ptr, +4 size, +8 order, +0xa slot, +0xc id}), +0xc index (0x211 x u32 entry
ptrs in insertion order), +0x10 data area, +0x14 count, +0x16 capacity, +0x18 mode (1 = four mem_alloc
blocks, 2 = carved in place from the pool cfg+0xa8 / size cfg+0xac). `tmap_cache_add_4c460` appends at
data + size - free; `tmap_dir_find_by_id_4c750`, `tmap_dir_find_free_4c3d0`, `tmap_cache_clear_4c6ee`,
`tmap_cache_destroy_4c790` complete the API (DAT_000adf58 holds the cache, DAT_000adf50 the anim table;
both created in 4bbf0 and torn down in 4bc80). DAT_000b9580[0x211] marks always-resident textures
(`texture_mark_resident_4c0a0` from a 14-byte record list when DAT_000987e8, loaded by
`texture_load_resident_4c0f0`).

#### CPU detection

`cpu_detect_5ac80` -> 5ace8 -> `cpu_identify_5ad03` (Intel's CPUID sample: AC-bit toggle for 386/486,
ID-bit for CPUID; DAT_0009e59c family, DAT_0009e59d GenuineIntel, DAT_0009e59e model, DAT_0009e59f
stepping, DAT_0009e5a6 raw CPUID(1), DAT_0009e5aa vendor string, DAT_0009e5b6 CPUID available) and
`cpu_detect_fpu_5adfe` (DAT_0009e5a0 FPU present, DAT_0009e5a1 2 = 287 / 3 = 387, DAT_0009e5a4 control
word). cfg+8 = (family == 5), i.e. Pentium.

#### HMI timer / MIDI internals touched

`hmi_timer_set_event_rate_5d235` (sosTIMERAlterEventRate) and `hmi_timer_get_event_rate_5d558` use the
tables already noted for 5d093 (DAT_000a3c59 callbacks, DAT_000a3cb9 rates, DAT_000a3cf9 increments,
DAT_000a3d39 accumulators, DAT_000a3c55 PIT divisor, clock 0x1234DC). `hmi_midi_send_event_5d69b` is the
sosMIDI event sender with dynamic channel allocation/stealing (logical->physical map DAT_000a1847, owner
DAT_000a1b17, priority DAT_000a1ac7, enabled mask DAT_000a2515, per-channel program/bend/volume/pan state
DAT_000a1bb7..bb, master volume DAT_000a2565, driver function table DAT_0009f91a + driver*0x24).

#### Open questions

- Which device sits behind ports 302h/303h in the stereo page functions (50370/503d0)?
- Meaning of the VFX1 driver function 6008h (`vfx1_driver_cmd_6008_50120`) and the 256-byte block sent by
  `vfx1_driver_send_block_4fca0` during init (palette? configuration?).
- What main-menu item 2 (`fe_menu_text_dialog_57580`) edits with its two text fields (buffers were passed
  in registers), and the contents of DAT_000add70/74 (dialog title strings). *Answered (round 6): "Enter your name:" into Config+0x1d (30 chars) and "Enter your call-name:" into Config+0x3d (8 chars, filtered keys) (port_frontend.md).*
- The exact HMI device-id constants (0xA001/A002/A004/A008) should be checked against the HMI SOS headers. *Answered (round 6): the music bank .tab records name the files per device: 0xA002 *.HMP (OPL2), 0xA004 *.ROL (MT-32), 0xA001 / 0xA008 *.GEN (General MIDI / AWE32) (port_audio.md).*
- `fli_file_chunk_dispatch_5c594` body was dropped by the decompiler; confirm it is the type switch.

## Region E (docs/analysis/agent2_E.md)

### ENGINE.md section: HMI SOS internals, OPL2/GUS MIDI drivers, Watcom graph.lib console, CRT corrections

#### HMI Sound Operating System: driver function tables

- A MIDI driver handle h (0..5) owns a 0x24-byte table at `DAT_0009f91a + h*0x24` of six 6-byte far pointers:
  fn0 = send MIDI event (`hmi_midi_send_event_5e4b8`), fn1 = init (`UNK_0009f920`), fn2 = uninit,
  fn3 = reset, fn4 = set instrument data (`snd_midi_driver_call_6330e` indexes 0x9f932 = +0x18, i.e. this is
  sosMIDISetInsData), fn5 unused/callback. `DAT_0009ea6e[h]` = device id, `DAT_0009ea46[h]` = external driver loaded.
- Four MIDI drivers are linked into carpet.exe and are selected by device id in `snd_midi_init_driver_5fa77`
  (tables are filled by `snd_midi_init_system_5f8b1`, all `retf` thunks, so Ghidra has no functions for them):
  | id | table | thunks | implementation |
  |---|---|---|---|
  | 0xa002 | 0x131366 | 0x69849/0x69878/0x698b3/0x698d1/0x698ef | internal OPL2 (Adlib, port 0x388/0x380): `snd_opl_*` 0x68619-0x6a0e6 + 0x704cd/0x70697/0x7075e |
  | 0xa003 | 0x1313a2 | 0x6730a/0x6733a/0x67353/0x6736c/0x67385 | pass-through to user callback `DAT_0009fae6` (fn4 sets it) |
  | 0xa005 | 0x131384 | 0x673f2/0x676f9/0x67878/0x678f6/0x6790f | digital-sample MIDI (plays notes through sosDIGI; note queue `hmi_wavemidi_queue_*` 0x67adc-0x67ded, calls snd_digi_stop_sample_65485) |
  | 0xa008 | 0x131348 | 0x68370/0x68536/0x685a7/0x685c5/0x685de | internal AWE32 EMU8000 MIDI (`snd_awe32_*`, ex `snd_gus_*`, 0x6de66-0x70480; see Round 4) |
  Any other 0xa000..0xa200 id loads a driver image from hmidrv.386 (`hmi_midi_load_driver_67f7e`: 0x2c-byte file
  header + 0x2c-byte device records, malloc + selector + lock) and binds its 5 entry points with
  `hmi_midi_bind_driver_table_6a093`. The game's sound setup switches on 0xa001/0xa002/0xa004/0xa008 (carpet_all.c
  ~line 69484), so 0xa002 (OPL2) and 0xa008 (AWE32 EMU8000, see Round 4) are the built-in paths and 0xa001/0xa004 need hmidrv.386.
- Digital drivers (hmidrv.386 / hmidet.386) are called through a C-callable entry with a function number:
  detect driver fn0 = find hardware, fn1 = get settings (AX port, CL IRQ, CH DMA -> DAT_000a3f94/98/9c), fn2 = get
  0x6a-byte capabilities; digital driver fn0 = init(port, irq/dma), fn1 = uninit, fn2/fn3 = buffer/format setup,
  fn4 = start, fn5 = stop, fn8 = get info far ptr, fn10 = get 3 entry points (CS:off, DS:off, DS:off).
  Per-handle digital state: `DAT_000a3a84 + h*6` driver far ptr, `DAT_000a3bf0[h]` device id (< 0xe106 = needs own
  DMA buffer -> `hmi_digi_alloc_dma_buffer_64c3f`, which retries DOS allocations until the block does not cross a 64K
  page), 32 channel records of 0x6c bytes inside the driver (`hmi_digi_init_channel_ptrs_66e83`).
- `hmi_digi_lock_code_63f76` / `hmi_digi_unlock_code_6413b` are the sosDIGI lock lists (16 regions including the
  IRQ0 ISR 0x66608+0x238 and DAT_000a2a50+0x1030, the timer state block). The IRQ0 ISR at 0x66609 saves SS:ESP to
  DAT_000a2a72/76, switches to the stack at DAT_000a2a6a, calls the C tick handler `[DAT_000a2a60]`, EOIs port 0x20;
  `hmi_timer_isr_chain_old_667fe` is its alternate exit that retf's into the saved old int 8 vector
  (DAT_000a2a54/58) when DAT_000a3a7c is set. 0x62c2c is the same idiom with the vector passed on the stack.

#### Internal OPL2 driver state (device 0xa002)

- Voice tables (9 voices): `DAT_000a401c[v]` note playing (0 = free), `DAT_000a40c0[v]` MIDI channel,
  `DAT_000a41e8[v]` velocity, `DAT_000a4011[v]` timbre loaded, register shadows `DAT_000a3fec[v]` (0xA0+v),
  `DAT_000a3ff5[v]` (0xB0+v, bit 0x20 = key on), `DAT_000a3fdc[op]` levels, `DAT_000a3fbc[op]`, operator slot table
  `DAT_000a402d/402e[v*2]`, rhythm register shadow `DAT_000a4010` (0xBD).
- Channel tables (16): `DAT_000a4080` program, `DAT_000a40e4` bend MSB (0x40 centre), `DAT_000a4124` bend pending,
  `DAT_000a4164` bend range (default 2), `DAT_000a41a8` volume, `DAT_000a420c`, `DAT_000a424c` sustain; sustained
  note-off queue `DAT_000a42ac` (3-byte MIDI events, count `DAT_000a42a8`, max 16).
- Frequency table `DAT_000a4320` (F-numbers, 12 per octave) with `DAT_000a44b8/44bc` octave helpers
  (`snd_opl_calc_bent_freq_686db`).
- Instrument data is the AdLib .BNK format: header +8 count, +0xc name-table offset, +0x10 data offset, 30-byte
  records; `snd_opl_pack_timbre_bank_6955a` converts them in place to OPL register bytes and
  `snd_opl_set_timbre_bank_69401` stores the melodic bank first (`DAT_000a404c`, data `DAT_000a4058`) and the
  percussion bank on the second call (`DAT_000a4062`, flag `DAT_000a4074`; channel 9 uses it). Port in
  `DAT_000a3fa8` (0x388 or 0x380 only), chip-ready flag `DAT_000a402b`, driver-open flag `DAT_000a4040`.

#### Internal AWE32 (EMU8000) MIDI driver state (device 0xa008; functions were `snd_gus_*`, renamed `snd_awe32_*` in round 4)

- 30 voice entries of 10 bytes at `DAT_000a516c`: word 0 = (channel << 8) | note, 0xffff = free, low byte 0xff =
  released while sustained.
- 16 channel records of 0x1e bytes from `DAT_000a53ee`: +0 sustain, +1/+2 scaled (val*45/100*2) controllers,
  +3 pan (-2*val-2, 0x80 centre), +4 expression (default 0x7f), +5 volume (default 100), +6 bend/30, +8 second bend
  value, +0xa bend range (0x200 = 2 semitones), +0x10/+0x12 bank select, +0x14 RPN marker 0x100, +0x16/+0x18 RPN number.
- Patch layer records stride 0x7c at `DAT_000a55ec` (`snd_gus_patch_adjust_param_6e2e6`, key range test
  `snd_gus_note_in_range_6e2b8`).

#### Watcom graph.lib text/graphics console (0x79000-0x7b9ee)

This is Watcom's `graph.lib` (`_outtext`, `_settextposition`, `_scrolltextwindow`, `_setcolor`, `_setplotaction`,
`_setfillmask`, `_displaycursor`), linked in because the game prints its DOS messages through it.
- Globals: `DAT_000a45b9` graphics-mode flag (0 = text; the decompiler treats it as constant 0 so every graphics
  path shows up as "unreachable block"), `DAT_000a45ba` `_GrStatus` (0 ok, 2 `_GRCLIPPED`, -3 `_GRNOTINPROPERMODE`,
  -5 `_GRINSUFFICIENTMEMORY`), `DAT_000a45c5` text attribute, `DAT_000a45c8` cursor enabled, `DAT_000a45ca` cursor
  currently drawn, `DAT_000a45cc` active page, `DAT_000a45d4` current colour, `DAT_000a45d8[8]` fill mask
  (`DAT_000a45e0` default, `DAT_000a45e8` mask-set flag), `DAT_000a45f3` plot action (internal 0..3), `DAT_000a4655`
  BIOS data area base (0x400), `DAT_000a4659/465d/4661/4665` B000/B800/A000/C000 bases with selector words at
  `DAT_000a464b..4653`, `DAT_001319a0/a2` cursor row/col, `DAT_001319a4..aa` pixel/char width/height and cols/rows,
  `DAT_001319ac` colours, `DAT_001319ae` planes/bpp, `DAT_001319b0` pages, `DAT_001319b2` BIOS mode (7 = mono),
  `DAT_001319ba` device-driver function table (+0/+4/+8/+0xc thunks at 0x79dd6..0x79e01; +0x10 set address,
  +0x38 solid span, +0x3c masked span, 0x40/0x44 get/put image), `DAT_001319e2..ee` text window,
  `DAT_001319e4` cursor shape word, `DAT_001319dc` planar flag, `DAT_000a4690` stack low bound.
- Built-in fonts for graphics-mode text: 8x8 at 0xaa660 (modes with char height < 14) and 8x14 at 0xab300.
- `con_gen_glyph_blitter_7b408` is self-modifying: it assembles a per-device blit loop on the stack from three code
  fragments stored with a length byte in front, then calls it. Harmless but worth knowing when tracing.
- Existing name `con_set_video_mode_79cb2` is wrong: it is int 10h AH=01 set cursor shape (CX = DAT_001319e4, CH |=
  0x20 hides when arg == 0). 0x7a699 (not in the list) = colour CRTC probe at 3D4h returning 2.

#### CRT corrections and additions

- scanf: `crt_scnf_number_6c8c1` is the float collector (digits, '.', exponent, stores via `crt_fdfs_7136e` for %f),
  `crt_scnf_float_6cc04` is the integer converter; names swapped above.
- spawn family: 6d999 = spawnve core, 719fe = spawnl, 71c25 = spawnvpe (PATH search), 71a1f = spawnvp, 6dcf2 = spawnlp,
  658d5 = system. `_environ` = DAT_000a46c0 (built by `crt_setenvp_713ba` from the DOS env block selector
  DAT_000a46b5 : offset DAT_000a46b1).
- `crt_fatal_runtime_error_71a4f` -> `crt_debugger_trap_71d84` (int 3 + "WVIDEO" when DAT_000ac138 says a Watcom
  debugger is attached) -> `crt_fatal_error_6275c`.
- Streams: `crt_init_std_streams_71161` sets stderr unbuffered and links stdin/stdout/stderr into the
  `__OpenStreams` list `DAT_001319f4` (8-byte nodes). FILE layout confirmed: +0 _ptr, +4 _cnt, +8 _base, +0xc _flag,
  +0x10 _handle, +0x14 _bufsize, +0x18 _ungotten, +0x19 _tmpfchar (0x1a bytes).
- `_splitpath` pieces are limited to _MAX_PATH2 = 0x92 (`crt_splitpath_pcopy_717ff`).
- `crt_prtf_fixed_point_6a5f1` confirms Bullfrog built with a no-FPU printf: %f prints a 16.16 fixed-point value.

#### Renderer/UI names in this range that were wrong

- `vga_fill_rect_60688` and `vga_fill_rect_606c0` are byte-identical sprite draws (`ui_draw_sprite_*`): record
  {+0 data ptr, +4 w, +5 h} drawn by `vga_draw_sprite_spans_6070d`.
- Rect outline variants: 604c0 (via 6acd4, pitch 0x280) is 640x480; 603f0 (via 6abbc, coords halved onto pitch
  0x140) is 320x200. Both existing comments say the opposite.

#### Open questions

- Which ISR jumps to `hmi_isr_chain_to_vector_62c2c`? No C xref; probably the HMI keyboard or secondary timer
  handler. Needs a byte scan for `jmp 0x62c2c`.
- The exact HMI SOS device-id names (sos.h `_MIDI_*` constants) for 0xa002/0xa003/0xa005/0xa008 were not verified
  against a header; the hardware identification above is from code behaviour (OPL port 0x388, snd_gus_* calls,
  snd_digi_stop_sample calls).
- `hmi_digi_drv_call2_6720b` / `_call3_6723a` (driver fns 2 and 3) parameters are not understood.
- `snd_gus_cc_scaled_a/b` (6ee78/6eea1) and `snd_gus_setup_regs_a/b` (6f801/6f895) are named by shape only.
- The internal driver bodies 0x6730a-0x673d1, 0x673f2-0x67adc, 0x68370-0x68619 and 0x69849-0x69940 are not
  functions in Ghidra (reached only via far thunks + retf); they should be created so the OPL/GUS/wave drivers get
  callers.
- `DAT_00131460` written by `crt_init_std_streams_71161` (value = terminator FILE's _flag) is unexplained; may be a
  decompiler artefact of `__NFiles`.


# Round 3 agent findings (2026-10-06, functions created by the cleanup; docs/analysis/agent3_*.md)

## Region G (docs/analysis/agent3_G.md)

### ENGINE.md section: region G findings (agent 3, 2026-10-06)

#### Function inventory corrections

- `creature_trader_s90_update_1ea50` is truncated to 2 bytes (`push 0x5a`); its body is the listed
  0x1ea52 (`creature_idle_seek_leader_18610(thing, 0x5a)`). Merge 0x1ea52 into it.
- `fe_reset_clip_320x200_573a0` starts mid-instruction (inside the `call 0x60688` at 0x5739f). Its code
  is the 640x480 branch of `fe_draw_centred_icon_57350`; delete it.
- 0x4d0dd (`ui_draw_icon_4d0dd`) contains a second asm routine with its own `push ebp; mov ebp,esp`
  prologue at **0x4d140** (same draw with w,h divided by arg4+1 and a scale argument). It needs a function
  entry; the current 235-byte body spans both.
- 0x284cf starts one byte early; the real entry of the tiny asm "DAC entry 0 = blue" routine is 0x284d0.
- 0x1b233 is a second mis-start of the kraken s38 continuation already recorded as FRAGMENT at 0x1b231.
- 0x6622c is a BIOS keyboard poll routine (int 16h AH=12h/11h/10h) that is not a function in the export;
  it writes the shift state to `DAT_0012eea1` (bit 1 shift, 2 ctrl, 4 alt) and the scancode to
  `DAT_0012eea0`. Called by `input_bios_read_key_ascii_5bff8`.
- Data in the code object: 0x4cdb0 is the `u16[32]` isqrt seed table (`sqrt(2^n)`, indexed by the highest
  set bit of the argument in `math_isqrt_4cd7a`); the front-end string block runs to 0x51ec8 and ends with
  the four UK keyboard-row strings (`!"\$%^&*()_+`, `QWERTYUIOP{}`, `ASDFGHJKL:@~`, `|ZXCVBNM<>?`) used for
  shifted text entry.
- Fall-through handler continuations (no prologue, pop registers pushed by the predecessor):
  0x1ddb2 (builder s74), 0x1e2f1/0x1e41e/0x1e4ad (townie s79), 0x1e75b (trader s85), 0x385e0
  (projectile_create_type17_38590), 0x414a3 (flyer1 s4), 0x425d3 (balloon_update), 0x47586
  (spell_speedup_update), 0x280ad (effect s57), 0x282ca (effect s59), 0x1884f (creature_idle_seek_leader),
  0x10b34 (thing_exists_near_pos). 0x25c2c is the switch-case body of `effect_mana_ball_update_25980`.

#### Thing / player fields

- `thing_set_extents_353d0(thing, xy, h)` is the generic bbox setter (ext_x = ext_y = xy, ext_h = h), used
  by 43 constructors; `thing_set_castle_extents_353f0` is the castle variant.
- `thing_signature_14080(thing)` = `(cls << 7) + type + owner_idx` (u16). AI goals store it in the AI player
  thing's `+0x94`; `ai_target_valid_140a0` compares it against the current target to detect recycled slots.
- Player sub-block `P` (thing+0xa0): **P+0x2e** is set to 100 when the player is hit (`player_apply_hits_40b70`)
  or when a projectile picks a human player as target (`projectile_pick_target_45f00`), and zeroed at player
  init - an "under attack" timer (reader not found; speculation). P+0x19f AI modes written by the setters:
  0, 1, 3, 5, 6, 7, 8, 9, 0xb, 0xd (this region) plus 2, 4, 10 already named.
- Mana ball rolling physics (fragment 0x25c2c of `effect_mana_ball_update_25980`, active when +0x3a != 0):
  **+0x96/+0x98 = i16 vx/vy** clamped to +/-0x40 per tick, z_vel (+0x2e) -= 0x10 per tick (min -0x80),
  ground contact bounces with z_vel = -z_vel/4 (killed below 0x11), on the ground the slope vector from
  `terrain_slope_vector_3e4b0` is added to v and v *= 250/256 (friction); balls touching another owner's ball
  merge (`thing_find_collision_other_owner_10980` -> `mana_ball_merge_26120`).
- Effect s59 (`effect_type57_s59_update_28270`, fragment 0x282ca) damages every type-0x27 thing within
  dist^2 0xc40000 through damage slot 4 (`+0x72` amount 100, `+0x76` attacker).

#### Level features: walls

`level_build_linked_feature_34c40` dispatches Effect type 0x1c (Wall) to `level_build_wall_342e0`
(0x34570 path / 0x346b0 canyon / 0x34760 ridge are still `LAB_` and need function entries). The wall
builder takes two cell positions, computes the wrapped deltas with `math_wrap_diff_34250(.., 0x100)`,
swaps the endpoints so dx >= 0 and walks `n = max(|dx|,|dy|)/10 + 1` steps. Each step creates two
class 10 **type 0x1b** things at the scratch position from `terrain_set_pos_scratch_34280`: one with
state 0x1d and `+0x1a = dx/n` (run along +x) and one with state 0x1b (dy < 0) or 0x1c (dy >= 0) and
`+0x1a = |dy|/n` (run along y); the division remainder goes on the first step. So the Table A handlers
`effect_steal_mana_s27_update_24fc0` (0x1b), `effect_type26_s28_update_24eb0` (0x1c) and
`effect_type27_s29_update_250b0` (0x1d) are the wall-segment things (-y, +y, +x runs; `+0x1a` = length in
cells) - the "steal mana" name on s27 is inherited from the type-25 Steal mana Table B column and is
probably wrong for this state. *Corrected (round 5 merge): renamed `effect_wall_north_s27_update_24fc0` / `effect_wall_south_s28_update_24eb0` / `effect_wall_east_s29_update_250b0`; see "Port round 4 corrections" at the end.*

#### Sound / music / HMI

- `sound_request_49720` keeps a 10-byte record per sound id at 0xb7ac0 (+0 active, +2 ?, +4 word
  compared by `sound_priority_ok_49c20`: a new request replaces the playing one only if `new - cur >= -8`).
- `sound_sample_done_49d80(owner, sample)` maps the pair through `DAT_0012e070[32]` to a channel and asks
  `hmi_sample_done_6291e(DAT_0012e1c0, channel)`; returns 1 (done) when sound is off or the pair is gone.
- `music_fade_timer_cb_49ca0` is a far routine (ends in `retf`): the 60 Hz HMI timer event used by the
  music fade (decrements `DAT_000943c8`, sets MIDI volume, ends the fade at 0).
- `hmi_timer_dispatch_events_5d5a9` (far) is the PIT-tick service of the HMI timer: 16 slots, callback
  `DAT_000a3c59[i*6]`, increment `DAT_000a3cf9[i]`, 16.16 accumulator `DAT_000a3d39[i]` (fires when bit 16
  sets, high word cleared), per-slot event id `DAT_000a3d79[i]` copied to the current id `DAT_000a3d89`.
  Adds the two id tables to the 5d093 note.

#### Front end, FLI, DOS

- `fe_flic_loop_frame_56670`: looping 4-frame flic animation driven from `DAT_0012eccc` (current chunk
  pointer), `DAT_0012ecc8` (frame-1 pointer captured on the first pass), `DAT_0012ecd0` (frame counter
  1..4); 16-tick frame pacing unless input is pending.
- `fe_draw_save_slot_names_54850`: six slot labels "Game One".."Game Six" (pointer table 0x9e4e8) in the
  16-byte rect table at 0x51d3c (code-segment data), font g_font1, colour `DAT_0009e516`.
- `level_save_file_3c150` writes `levels/lev%05d.dat` with exactly **0x979c = 38812 bytes**, confirming the
  level file size (footer at 0x9790 + 12). Dead code (level editor leftover).
- `g_cfg->flags & 8` (skip game) swaps the keyboard ISR install/restore for the empty stubs 0x5bfd0 /
  0x5bfe4.
- `mouse_shutdown_5bdd4` resets the int 33h driver and frees the four cursor buffers
  `DAT_0012ede8/edf8/edc8/edb8`; `dos_get_current_drive_5af2c` / `dos_set_current_drive_5af44` are the
  int 21h AH=19h / 0Eh wrappers (1-based drive numbers); `crt_srand_5b01a` seeds `DAT_0009e5c8`.
- File-stream FLI player: `DAT_0009e700` = file handle, `DAT_0009e6e4` = current chunk size (the three
  read thunks read `size - 6` bytes after the chunk header).

#### Open questions

- Who reads P+0x2e (the 100-tick "under attack" value)? No reader found in the export. *Answered (round 6): player_flyer_move_3fc00 (combat-music timer; round 4 region C).*
- Why do walls use effect *type* 0x1b (not the level-file type 0x1c)? Possibly type 0x1b = "wall segment"
  and 0x1c = "wall descriptor" (the level record that `level_build_linked_feature_34c40` walks).
- `gfx_copy_b400_4ced4` copies 46080 bytes (320x144): the 3D view height with a HUD in 320x200? No callers.
- `flag_set_false/toggle/set_true_4cae0..4cb40` have no direct callers; they are probably menu-option
  callbacks referenced from a pointer table in the data object that the export does not resolve.

## Region H (docs/analysis/agent3_H.md)

### 2. Notes for docs/ENGINE.md (region H, round 3)

#### HMI lock-region markers (the 17-byte empty functions)

Every HMI source module was compiled with empty delimiter functions (`push ebx/esi/edi/ebp; mov ebp,esp;
sub esp,0; ...; ret`, 17 bytes). The two lock lists use them as region bounds: a region in
`snd_hmi_lock_code_5f04d` starts at marker+0x11 (the first byte after the empty function) and its length
runs exactly to the next marker (e.g. 0x5e529+0xb02 = 0x5f02b, 0x67f6d+0x26c = 0x681d9, 0x69f59+0x30 =
0x69f89); the `hmi_digi_lock_code_63f76` list instead starts at the marker itself (0x632b8+0x34 = 0x632ec,
0x6cee2+0xa6 = 0x6cf88). 35 of them were in this list and are named `hmi_lock_marker_<addr>`; more exist that
are not functions yet (0x66887, 0x66dc0, 0x67aba, 0x67acb, 0x68251, 0x68262, 0x6832d, 0x6833e, 0x685f7,
0x68608, 0x6991e, 0x6992f, 0x6d40b, 0x6d4b3, 0x6d52f, 0x672c7, 0x673af, 0x673c0). One of them,
`hmi_wavemidi_queue_uninit_67b52`, is also a real (compiled-out) function called by the wave-MIDI uninit entry.

#### Internal MIDI driver far entries -> implementation (device table `DAT_0009f91a + h*0x24`)

The "thunks" listed in Region E are the driver bodies themselves (far functions ending in `retf`,
all args on the stack: [ebp+0x14] first arg, handle last):

| device | fn0 send event | fn1 init | fn2 uninit | fn3 reset | fn4 SetInsData |
|---|---|---|---|---|---|
| 0xa002 OPL2 | 0x69849 -> `snd_opl_midi_event_6911c` | 0x69878 -> `snd_opl_driver_init_697e1` (DAT_0013196c = handle) | 0x698b3 -> `snd_opl_driver_uninit_6977c` | 0x698d1 -> `snd_opl_reset_state_69319` | 0x698ef -> `snd_opl_set_timbre_bank_69401` |
| 0xa003 callback | `hmi_midi_cbdrv_send_event_6730a` -> `[DAT_0009fae6]` | 0x6733a return 0 | 0x67353 return 0 | 0x6736c return 0 | 0x67385: DAT_0009fae6/ea = callback far ptr |
| 0xa005 wave MIDI | `hmi_wavemidi_send_event_673f2` | 0x676f9 (see below) | 0x67878 | 0x678f6 return 0 | 0x6790f (bank loader, see below) |
| 0xa008 AWE32 (ex GUS) | `snd_gus_midi_send_event_68370` | 0x68536: `snd_gus_detect_6f986`, `snd_gus_init_703e2`, 7 table ptrs DAT_000a57dc..f4 = 0xa5800/0xa5a08/0xa5b74/0xa60e7/0xa624d/0xa6817/0xa9295, `snd_gus_channels_init_6def4` | 0x685a7 -> `snd_gus_shutdown_70480` | 0x685c5 return 0 | 0x685de return 0 |

The fn1/fn2/fn3/fn4 bodies, 0x67a9b and 0x6d5ea are not functions in Ghidra yet.

Wave-MIDI driver (0xa005) internals, from 0x676f9/0x67878/0x6790f:
- init(far init struct, handle h): searches the 5 digital driver slots `0x131478 + i*0x40` for one whose
  +0 equals struct+0 (device id) and stores its index in `DAT_000a11da[h]`; if none, `DAT_000a1216[h] = 1`,
  initialises its own digital driver with `snd_digi_init_driver_64386(struct+0, struct+0x1c, +0x20, +0x14,
  +0x18, ...)`, registers a timer event (`hmi_timer_add_event_5d093`, handle kept in `DAT_000a1202[h]`) and
  sets `DAT_000a11ee[h] = 1`; `DAT_000a122a[h] = struct+0x10` (velocity scaling on/off, master volume byte
  `DAT_000a2565`); `hmi_wavemidi_queue_init_67adc(h, struct+0xc)`.
- uninit: if it owns the digital driver: `snd_digi_uninit_driver_64ab8(DAT_000a11da[h], 1, 1)`,
  `hmi_timer_remove_event_5d3a9(DAT_000a1202[h])`; then `hmi_wavemidi_queue_uninit_67b52(h)`.
- SetInsData(bank far ptr, selector, h): `DAT_000a11bc[h*6]` = bank; clears the 128-note table
  `DAT_000a02bc + h*0x300` (6 bytes per note: far ptr to sample record); the bank starts with the
  signature string at `DAT_000a123e` (mismatch -> error 0xe), +0x24 = total size, records from +0x28:
  a 0x54-byte sample header (+8 data length, +0x12 MIDI note, +0x14/+0x18 = done-callback far ptr which
  the loader sets to CS:0x67a9b, +0x1a/+0x1c flags used by `hmi_start_sample_64e7c`) followed by the PCM
  data; the per-note table gets the record pointer and record+0x54 becomes the sample data pointer.
  0x67a9b (far, retf) is the sample-done callback: `hmi_wavemidi_queue_remove_at_67cc1(h, slot-1)`.
- send event: see the CSV line; the queue holds the playing notes (slot+1 is stored in record+0x12 and
  used as the sosDIGI sample id).

#### sosDIGI detection and channel records
- `snd_digi_detect_get_caps_63947` / `snd_digi_detect_verify_settings_63e70` complete the sosDIGIDetect*
  set (init 635dd, uninit 63746, find 6379a, settings 63d88). hmidet.386 layout: 0x2c-byte header (record
  count at +0x20 -> `DAT_00131428`), 0x30-byte device records (+0x24 image offset, +0x28 device id, +0x2d
  flags bit 0x80), file handle `DAT_001313f4`, current image `DAT_001313f8`, entry `DAT_00131404`,
  data selector/offset `DAT_00131434`/`DAT_00131438`. hmidet.386 function 3 = verify settings
  (`hmi_det_drv_verify_settings_670dc`: EBX port, CH dma, CL irq).
- HMI error codes seen: 2 _ERR_INVALID_POINTER, 6 _ERR_INVALID_DRIVER_ID, 7 _ERR_NO_DRIVER_FOUND,
  10 _ERR_INVALID_HANDLE (hmi_stop_sample_65485), 0xe invalid bank signature.
- Digital channel record (0x6c bytes, far ptr table `DAT_001315a4 + h*0xc0 + ch*6`): +0 current ptr,
  +8 start ptr, +0x10 end ptr, +0x18/+0x1c loop start, +0x20 loop length, +0x2c remaining, +0x30 flags
  (0x8000 playing, 0x800 set at start, 0x1000 tested by `hmi_sample_is_playing_629ba`), +0x34 sample id,
  +0x38 volume?, +0x3c/+0x40 done callback, +0x4a pan/word from _SOS_START_SAMPLE+0x1a, +0x50 copy of
  struct+8. `hmi_digi_continue_sample_65511` (probably sosDIGIContinueSample) refills a record in place.
- `snd_midi_send_ins_data_paced_63344` sends instrument/SysEx data one byte per 1500 Hz tick through
  driver fn4 (callback 0x6341d sets `DAT_000a25b6`).

#### AWE32 (ex GUS) MIDI SysEx
`snd_gus_match_sysex_6f911` recognises Roland GS DT1 messages `F0 41 dev 42 12 40 01 30 vv` (reverb macro)
and `40 01 38 vv` (chorus macro) from templates at `DAT_000a4c84` (0x16 bytes each, 0xfe wildcard, 0xff end,
+0x10 value index, +0x12 handler). This resolves the Region E open question: `snd_gus_setup_regs_b_6f895`
is the reverb-macro handler and `snd_gus_setup_regs_a_6f801` the chorus-macro handler.

#### Watcom CRT details
- Watcom errno numbering starts at ENOENT = 1: `crt_set_edom_6608b` sets 13 (EDOM), 0x66096 sets 14
  (ERANGE), `crt_errno_fail_6609a` sets 9 (EINVAL) and `crt_freopen_find_stream_62ebf` sets 4 (EBADF).
- `DAT_00131460` (Region E open question) is the `__ClosedStreams` list head: freopen moves a FILE from it
  back to `__OpenStreams` `DAT_001319f4`.
- Heap: `crt_heapenable_71a7d` is `_heapenable()`, `DAT_000ac130` = `__heap_enabled` (heap growth refused
  when 0 or when `DAT_000a4680 == -2`); `crt_heap_realloc_dpmi_block_70bf6` is `__ReAllocDPMIBlock`
  (DPMI 0503h grow-in-place of a whole heap segment).
- Several public entries are 3-arg stack wrappers over internal workers (`crt_*_wrap_*`): stricmp,
  access, lseek, tolower, itoa, ltoa; strtol/strtoul share one body (true entries 0x65a95 / 0x65ab1, see
  FRAGMENT 0x65a98).
- `crt_record_ss_7095e` stores SS in `DAT_000a9c24` for `crt_alloca_check_70979`; `crt_creat_6add5` uses
  O_RDWR|O_CREAT|O_TRUNC (0x62); `crt_getche_71328` with ungetch buffer `DAT_000a46a0` feeds
  `crt_fill_buffer_6c0dd` for unbuffered tty stdin; `dos_findnext_714c5` + `dos_dta_copy_in_7150b`
  (caller buffer -> DTA) complete the findfirst/findnext pair with `dos_dta_copy_714e8` (DTA -> caller).
- `dos_int_call_allregs_6d59d` is a second int386-style caller using a 0x26-byte block with segment
  registers and flags (layout in the CSV), distinct from `dos_int386x_core_6d540`.

#### graph.lib corrections
- `con_draw_glyph_gfx_7a922` and `con_gen_glyph_blitter_7b408` are the styled **line** primitive: 7a864
  (`con_lineto_clipped_7a864`) passes (x1, y1, x2, y2, colour `DAT_000a45d4`, 16-bit line style
  `DAT_000a45ef`) and the generated loop tests the style with `shl bx,1 / jnc`. `con_draw_glyph_clipped_7a819`
  is probably the other _lineto path. Suggested renames: con_line_gfx_7a922, con_gen_line_loop_7b408.
- 0x7acf7 (not a function) is the `_imagesize` arithmetic used by `con_imagesize_7acac`.

#### Late game code
- `castle_mark_footprint_circle_72217` (0x72217, right before the triangle rasteriser) sets bit 0x80 of the
  cell flag map 0xFDFB0 in a circle using `g_circle_profile` (0xCDCB0); no callers (dead or pointer-called).
- `render_draw_line_clipped_78e23` is the software line drawer over the viewport globals set by
  `render_set_viewport_78dd5` (`DAT_0009b600/9b604` size, `DAT_0009b5f4` dest, `DAT_0009b5fc` pitch,
  `g_fill_colour`).

#### Open questions
- 0x66898-0x66dbb (1.2 KB, HMI frame, song index `DAT_000a3d89`, tables 0x9f8fa/0x9f9ce/0x9f9ee) is a
  large sosMIDI function with no Ghidra function (probably the timer-tick sequencer) [*Answered (round 6): it is the song timer callback, created as `snd_midi_song_timer_cb_66898` (with the markers 0x66887 / 0x66dc0 / 0x66dd1); the other driver entries listed here are still not functions.*]; likewise the driver
  entries 0x6733a..0x67385, 0x676f9/0x67878/0x678f6/0x6790f/0x67a9b, 0x68536..0x685de, 0x69849..0x698ef
  and the int-table loader 0x6d5ea should be created so their callees get callers.
- `DAT_000a46b9` (called by 0x660bd with EAX) is never written in code; its static initial value was not
  checked.
- Bit 0x1000 of the digital channel flags (tested only by `hmi_sample_is_playing_629ba`) has no setter in
  the decompiled code; its meaning (paused?) is unverified.
- `hmi_digi_continue_sample_65511` = sosDIGIContinueSample is inferred from behaviour, not from a header.


## Round 4 findings (2026-10-06, resolving the last UNCERTAIN names)

Evidence trail: `docs/analysis/agent4_A..D.md`. Headline corrections: the `snd_gus_*` driver (HMI device 0xa008) is the Creative AWE32 **EMU8000** driver, not a Gravis UltraSound, and was renamed `snd_awe32_*`; the 302h/303h "stereo page" ports belong to the VFX1 VIP card; the file-stream FLI player is hand-written asm at 0x5c264 (`fli_file_play_5c264`, created in round 4); Thing+0xa0 points at PlayerRec+0x44f, so every `P+off` field in this document is PlayerRec+0x44f+off; Config+4 is a u32 tick counter; PlayerRec+4 is the level-loop quit flag (1 quit, 2 CD check failed); creature +0x3a is an "awake / near the local player" gate reloaded by `creature_proximity_wake_timer_46960`, not a sound timer; P+0x2e is a 100-tick combat-music timer; skeletons (state 55) convert Archers/Builders/Townies within 0x600 units into new skeletons; the dolmen sets player flag 0x1000 (treated like standing in the own castle); campaign level numbers 8, 17, 28, 33 and 39 are skipped by `level_skip_number_329c0` although the files are playable.

### Region A (0x10000-0x1f6b0)

### Notes for ENGINE.md

- **Skeleton conversion confirmed.** The tail of `skeleton_convert_villager_1c1e0` is `push 9; push 5; push 0xadfc4;
  call thing_create_35690` (0x1c542..0x1c54b), i.e. class 5 type 9 = Skeleton (type 9 * 6 = state 54 base), created
  at the deleted victim's position with `+0x18` = the skeleton's owner. The inline copy in
  `creature_skeleton_s55_update_1bb70` (0x1c164) does the same but copies the owner only when the owner thing is
  class 3 (a player). This closes the round-2 open question.
- **Skeleton state 55 layout.** `thing_set_state` constants: hit -> 0x38 (56, `creature_skeleton_s56_attack_1c570`),
  dead -> 0x3a (58); 1bb70 builds them as `[esp+0x34] + 2 / + 4` from the base 54. So for the skeleton, state 55 is
  the type-specific "wander, seek enemy castle/wizard, convert villagers" state and 56 is the attack state, not the
  generic +1 attack / +5 special layout used by the other creatures. *Corrected (round 5 merge): the generic layout is +1 main, +2 attack for every type; see "Port round 4 corrections" at the end.*
- **Thing field reuse by the skeleton:** `+0x47` (castle size index elsewhere) is the pose flag (1 = summoning pose,
  sprite 0xf5; 0 = normal, sprite 0xc9); `+0x1a` is the countdown to the next pose (400 ticks, reloaded while
  `+0x3a != 0`; set to -50 when the pose is interrupted). `+0x3a != 0` gates the damage-slot processing in both
  1bb70 and 1c1e0, so for creatures it behaves as a "was hit / in combat" flag rather than a plain animation timer.
- **Creature list indices** 4 / 0xc / 0xd in `g_cfg->creature_lists` are Archer / Builder / Townie, consistent with
  the creature type numbering (Dragon 0 ... Archer 4 ... Skeleton 9 ... Builder 12, Townie 13).
- **Correction for `ai_mode12_idle_12680`:** its stack argument is the player *thing*, not a spell id; the spell is
  hard-coded: `push 2; push ebx; call ai_spell_ready_14640` / `ai_cast_spell_14240` / `ai_get_spell_thing_13ac0`.
  Spell id 2 is **Alliance** (class-12 Table B index 2 = `spell_create_alliance_3a360`). So the idle mode casts
  Alliance when it is ready. The existing name comment ("stack arg = spell id") should be fixed. *Corrected (round 5 merge): spell 2 is speed-up (port_ai_wizard.md), Table B `spell_create_speedup_3a360`; see "Port round 4 corrections" at the end.*
- `math_rotate_offset_3e420(pos, yaw, pitch, dist)` returns immediately when `dist == 0`;
  `creature_attack_fire_homing_desc_19a90` passes 0, so its "offset" is disabled - probably why the function was
  left unreferenced.
- `creature_attack_fire_homing_desc_19a90` has **no reference anywhere in the image**: a byte search for the dword
  0x00019a90 over the whole image hits only 0x3fef5, which is inside `mov dx,[eax+0x19a]` in player_flyer_move_3fc00.
  It is dead code, not a register-passed callback.
- Dead AI goals `ai_goal_repair_castle_12d70` and `ai_goal_creature_near_rival_13600` are not in the
  `ai_choose_goal_12330` chain (confirmed by reading 12330). 13600 compares the 0/1 return of the idle handler with
  another wizard's mode byte - it would only ever match wizards in mode 0 or 1; speculation: a stale call target left
  in an abandoned "join another wizard's hunt" goal.
- `terrain_max_corner_level_10c30` returns `max corner height byte >> 5` (0..6); nothing else in the binary uses
  that scale (world height = byte * 0x20), so its intended consumer is unknown (speculation: castle/site level test).

### Open questions

- What sets the `+0x3a` creature flag (the write site is outside this region; `thing_add_pending_damage_117c0`
  does not touch it). *Corrected (round 5 merge): creature_proximity_wake_timer_46960, the constructors and creature_wake_tick_468e0; see "Port round 4 corrections" at the end.*
- Whether the dead `castle_near_thing_11820` (3-cell margin) was an early castle-site rule superseded by
  `castle_footprint_clear_11980`.

### Region B (0x1f6b0-0x40000)

### Notes for ENGINE.md

- **thing+0xa0 vs PlayerRec**: `player_spawn_3f360` (0x3f360 line `*(int*)(thing+0xa0) = rec + 0x44f`) sets the owner-record pointer to **PlayerRec + 0x44f**, not to the record start. So every "P+off" field in the AI/control notes (P+0xc speed, P+0x30 player number, P+0x147/P+0x149 steering, P+0x16, P+0x214 spell slots ...) lives at PlayerRec+0x44f+off; e.g. P+0x30 = rec+0x47f, P+0x147 = rec+0x596. This is why `player_commands_process_3a8b0` can write a steering short at P+4 (= rec+0x453) while rec+4 (state+0x340f) stays the quit flag.
- **PlayerRec+4 (state+0x340f+p*0x801) is the quit flag**: 1 = quit to DOS/menu (fe_check_shift_q_quit_52980, confirm dialog, command 2 and 0x1a), 2 = CD-ROM check failed (`player_cd_check_quit_3bbd0`). `game_main_32a00` breaks its level loop on any non-zero value and skips title/sound reload when set. Round-2 open question "meaning of value 2" is closed.
- **Config+4** is a u32 tick/frame counter (used as the sprite LRU stamp `DAT_b7cb0[] = cfg+4` in 2c600/2dac0/2e5a0 and as `& 7` cadence in 3bbd0); not yet in carpet_types.txt.
- **Config debug_bits (+0x18)**: bit0 is set unconditionally at the top of `ui_draw_debug_overlay_4ad80` (so it is 1 once a frame has been drawn), bit1 is set there only when `cfg->password == 0xF851B9`. `(debug_bits & 3) == 1` therefore means "running normally without the debug password".
- **PlayerRec position history**: rec+0x10 (u16, init 0x20), rec+0xe (init 0x1f), rec+0x248 (u16, init 0x80; command 0x?? at carpet_all.c:35276 adds a signed command arg to it), rec+0x23c..0x249 (template entry), rec+0x24a: 32 x 14-byte entries {x, y, z, yaw, derived value, P+0x147, rec+0x248}. Writer `player_log_position_3e080` (entry [rec+0x10 - 1]), initialiser `players_init_records_3bc10`. No reader found in the export; probably a network/replay trail that is never consumed (speculation). *Corrected (round 5 merge): it is the camera log, read by render_frame_1fab0; see "Port round 4 corrections" at the end.*
- **0x24d70 tests the height map**: `cmp byte [ebx+0xDDFB0], 8` is g_map_height, so the "texture 8" wording for 24d70 in ENGINE.md open question 2 is wrong (24e20/24fc0/24eb0/250b0 do test g_map_type 0xCDFB0 == 8). Height byte 8 = world z 0x100; nothing else in the export compares the height map with 8, and the function has no callers, so its purpose stays unknown.
- **Five dead constructor stubs** at 0x36200/20/40/60/80 (alloc + restore health, return 0) sit between class-2 and class-5 Table B; only 0x36200 is in the function list. Candidates for the empty class-4 Table B (0 records) that was never filled (speculation).
- **Level skip**: levels 8, 17, 28, 33, 39 exist in levels.dat as normal 0x979c-byte records with players (extracted/raw/levels/lev000NN.inf), so `level_skip_number_329c0` is a campaign decision. The round-2 guess "cut-scene positions" is not supported by the data; the real reason (removed levels? difficulty pacing?) is still open.
- **Fire effect spawns White smoke**: `effect_fire_update_23c20` creates class 10 type 0xd (White smoke, Table B m13 = effect_create_white_smoke_38d40) with a 1/7 chance per tick while shrinking; the smoke gets +0x1a = 100 and life 15.
- **g_map_flags low 3 bits as terrain class**: `terrain_cell_is_nonland_3d5f0` confirms the generator classes survive into gameplay: classes 2/3/5 are treated as land, everything else (0 water, 1 cliff, 4 transition, 6 steep) as non-land when deciding whether a cell lowered to height 0 becomes water (low nibble cleared) or a cliff edge (class 1).

Open questions: who (if anyone) reads the rec+0x24a history; why the five campaign levels are skipped; what height 8 means for the dead ring scan. *Answered (round 6): rec+0x24a is the camera log read by render_frame_1fab0 (typed `PosLogEntry log[32]`). The skipped levels are still unexplained; one data point: the retail exe started with `-level 17` crashes at tick 1-2 (divide by zero in ui_draw_status_bars_219f0, a Thing with max_health 0), so at least level 17 was not finished (port_reference2.md). Height 8 in the dead ring scan: no caller, still open.*

### Region C (0x40000-0x4c000)

### Notes for ENGINE.md

#### Player record P+0x2e = combat-music timer (established)

`player_set_combat_music_timer_40b50` writes 100 into P+0x2e; the only reader is `player_flyer_move_3fc00`
(disassembly 0x401b8-0x401e9): for the local player (`P+0x30 == g_state->local_player`) it decrements P+0x2e while
positive and calls `music_update_1f800(mood)` with mood **2 while P+0x2e > 0, else 1**. So P+0x2e is "ticks of
combat music left"; a hit (40b70) or being locked onto by a projectile (45f00) keeps the combat track playing for
100 ticks. ENGINE.md's player record table (~line 1200) should list `+0x2e i16 combat music timer (100)`.

#### +0x3a is an "awake / near the local player" gate, not a sound timer (established)

`creature_sound_timer_46960` and its caller `creature_sound_tick_468e0` were named after the +0x30 "distance for
sound" field. The consumers show the real role of +0x3a:

- 20 creature handlers (`creature_ai_step_18870`, `creature_archer_s25_1a0e0`, `creature_crab_s31_1a830`,
  `creature_kraken_s38_update_1b000`, `creature_skeleton_s55_update_1bb70`, builders, townies, trader, wyvern ...)
  start with `if (timer_a != 0) { apply damage slots; think ... }` - a dormant creature neither takes pending
  damage nor runs its AI.
- `projectile_pick_target_45f00` skips creatures, mana balls and wizards whose `timer_a == 0` (5 tests), so
  auto-aim only locks onto things near the local player.
- Defaults: `thing_alloc_35560` sets +0x3a = 0xfa (250 ticks awake after creation), `effect_create_mana_ball_39840`
  0x80, dead creatures are reset to 0xfa/0 by 468e0 so death animations run.
- 46960 reloads +0x3a = 0x10 only when the thing is within 24 cells (dist^2 < 0x2400000) of
  `players[g_state->local_player].thing`; segments of a chain (+0x36) get 0x12 and are kept in step while the head
  counts down. +0x3b is a snooze: while nonzero the distance test is skipped and +0x3b-- (only writers: 46960
  itself and the mana-ball constructor, which zeroes it).

Suggested renames outside my list: `creature_sound_tick_468e0` -> `creature_wake_tick_468e0`. The +0x30 = isqrt(dist)
written by 46960 has no reader in the export other than the debug overlay `dbg_draw_thing_timers_23050`; the
"distance for sound" meaning in the Thing table (+0x30) is therefore unconfirmed (speculation: leftover from a
distance-based creature-call sound that was moved into `sound_request_49720`, which computes its own distance).

#### Scenery handlers

- Standing stone (state 3) and Bad stone (state 9) handlers are byte-identical: set flag 0x20000 every tick and
  snap z to the terrain. 0x20000 is the bit effect constructors set (`flags = (flags & ~0x20008) | 0x20000`), i.e.
  "may be recycled when the pool is exhausted" (thing_alloc fallback stack). Trees set it only once they burn
  (43ba0 line 51-53). Dolmens never set it, so they are never recycled. *Corrected (round 5 merge): trees set it on every update of state 0; see "Port round 4 corrections" at the end.*
- Dolmen (state 6) is a regeneration site: it sets flag **0x1000** on every player thing whose bbox overlaps it;
  `player_type0_s0_update_402c0` and `player_ai_wizard_tick_11f20` test `(inside own castle) || flag 0x1000` for the
  fast regen branch (mana +0x84 = +0x88/200 min 1000, health regen max/200 vs max/500 outside) and clear the flag.
  ENGINE.md line 1225 already records the flag; this adds its effect.

#### Spell cast handlers (class 12 phase 0) - projectile/effect pairing

All five projectile spells in my list follow the fireball template (first tick when `+0x30 == +0x32` creates the
projectile, every tick `spell_charge_mana_46f20`, `+0x30--`, cannot-cast sets `+0x30 = 1`). The class 9 projectile
type and the impact effect written to +0x44/+0x45 are:

| spell | state | projectile class 9 type | impact effect 10/x | terrain-snap target z |
|---|---|---|---|---|
| 4 Shield | 12 | none - sets caster flag 0x4000 | - | - |
| 6 Earthquake | 18 | 2 | 0xf Earthquake | yes |
| 7 Meteor | 21 | 3 | 0x11 Meteor | no |
| 9 Crater | 27 | 5 | 0xb Crater | yes |
| 14 Rebound | 42 | none - sets/clears caster flag 0x8000 | - | - |
| 15 Lightning | 45 | 9 (lightning projectile 44fc0) | 0x17 Lightning | no |
| 23 Mini fireball | 69 | 0 (burst of +0x3d+1) | 0 Explosion | no |

Shield (0x4000) is re-asserted every cast tick but no code clears it (grep for `& 0xbf` on +0x11 / `0xffffbfff`
finds only the mana-ball handler, a different thing). Open question: where the shield flag is dropped when the
spell ends (possibly never, i.e. shield lasts until the player dies and the thing is re-allocated - speculation). *Corrected (round 5 merge): the first absorbed hit clears it (player_apply_hits 0x40d2a); see "Port round 4 corrections" at the end.*

#### Castle

- `castle_build_seq_set_done_42000` (+0x30 = 2) is the "build finished" step of the state-5 sequencer but has no
  caller; the sequencer reaches step 2 some other way or this is dead code from an earlier build flow.
- `castle_collapse_level_42010` spills exactly 10% of the capacity +0x88 by temporarily lowering the capacity
  before calling `castle_spill_mana_41720` (which spills `+0x8c + P+0x134 - +0x88`) and restoring it afterwards; the
  collapse visual is produced by calling the class-10 state-53 handler directly on the scratch Thing `things[0]`
  (state+0x7463) filled with the castle's position, level (+0x47) and index (+0x2a), not by creating a Thing.

### Region D (0x4c000-0x80000)

### Notes for ENGINE.md

#### HMI device 0xa008 is the Creative AWE32 (EMU8000), not a Gravis UltraSound

Everything named `snd_gus_*` (0x6de66-0x70480) talks to an EMU8000:

- `snd_gus_poke_b_6df9d` / `snd_gus_poke_w_6e069` / `snd_gus_peek_b_6e001` write the register code to
  `DAT_000a4ec2 + 0x802` and the data to `base | ((code>>8)&2) + ((code>>8)&0xc)<<8`. With the default base
  0x620 (initial value at 0xa4ec2) that is the EMU8000 layout: Pointer register 0xE22 (base+0x802), Data0 0x620
  (base+0, 32-bit), Data1 0xA20 (base+0x400), Data2 0xA22 (base+0x402), Data3 0xE20 (base+0x800). A GUS uses
  base+0x102..0x107 and base+0x506 and has no +0x802 register.
- Register code encoding (16-bit): bits 0-4 channel, bits 12-14 register number (shifted into pointer bits 5-7),
  bits 10-11 data-port group (0x4 = +0x400 Data1, 0x8 = +0x800 Data3), bit 9 = +2 (Data2). So 0x60|v = PSST,
  0x70|v = CSL (Data0 dwords), 0x04|v = CCCA, 0x54|v = DCYSUSV (Data1), 0x24xx/0x26xx/0x34xx/0x36xx =
  INIT1/INIT2/INIT3/INIT4, 0x1409..0x140e = HWCF4..HWCF7, 0x807f = Data3 reg 0 channel 31.
- `snd_awe32_set_chorus_mode_6f801` and `snd_awe32_set_reverb_mode_6f895` issue exactly Creative's published
  chorus/reverb sequences, and the preset tables are Creative's values byte for byte: chorus table
  `DAT_000a4a38` (8 x {feedback, delay_offset, lfo_depth, delay, lfo_freq}), entry 0 = E600 03F6 BC2C 00000000
  0000006D (Chorus 1), entry 1 = E608 031A BC6E 00000000 0000017C (Chorus 2); reverb command bytes
  `DAT_000a4aa8` = 03 05 7f 07 34 36 0f 17 1f 27 2f 37 3d 3f 41 43 09 0b 11 13 19 1b 21 23 29 2b 31 33 and
  reverb values `DAT_000a4ac4` (8 x 28 words), preset 0 = B488 A450 9550 84B5 383A 3EB5 72F4 72A4 7254 7204
  7204 7204 4416 4516 A490 A590 842A 852A ... (Room 1).
- The memory probe in `snd_gus_init_703e2` writes/reads the test word 0x1234 and stores
  `DAT_000a4ec4 = end - 0x200000`: EMU8000 sample DRAM starts at sample address 0x200000.
- 30 voices (`DAT_000a516c`, +8 of the hardware block) = EMU8000's 32 channels minus the two reserved for effects.
- The SysEx matcher `snd_gus_match_sysex_6f911` maps Roland GS DT1 reverb/chorus macros onto the EMU8000
  presets; the GS reverb/chorus type numbers 0..7 index the tables directly.

This also settles the open question about the HMI SOS device ids: 0xa008 = AWE32, as the earlier recollection
in ENGINE.md said. Recommendation: rename the whole `snd_gus_*` family to `snd_awe32_*` (about 60 functions),
fix the region-E table row for 0xa008, and change "GUS register access through port base DAT_000a4ec2 + 0x802"
to "EMU8000 pointer register at base+0x802". `snd_gus_detect_6f986` ("probe at base and base+0x400") is an
EMU8000 presence probe.

#### EMU8000 driver channel record (stride 0x1e from DAT_000a53ee), corrections

+1 = CC91 reverb send, +2 = CC93 chorus send, both scaled `(val*45/100)*2`. The chorus byte is applied at note
on (CSL bits 31-24, added to the patch chorus word at patch+0x26 / `DAT_000a5612`); the reverb byte is written
but never read anywhere in the image (operand scan for 0xa53ef: only 0x6ee98 and the reset at 0x6f2b9). Pan
(+3) goes to PSST bits 31-24 (`or ah,0x60`) in the same code.

#### The 302h/303h "stereo page" ports are the VFX1 VIP card

`g_state->opt_interlaced` is set to 1 in exactly two places (config_parse_33750 line ~27123 and
fe_config_apply_input_device_59330), both immediately after `vfx1_init_4fdc0` returns non-zero. So the
interlaced-stereo 640x480 path exists only for the Forte VFX1, and the fixed ports 302h/303h are the VIP card's
default base 300h + 2/+3 (vfx1_init reads the base from the `VFX` variable, default 0x300). 303h = 10h is written
when the 3D view is entered (`stereo_page_blank_50370`, from stereo_mode_enter_2ff50) and 303h = 1 when it is
left (`vfx1_vip_stereo_leave_503d0`, from mapmode_palette_save_30350), each preceded by 302h = 2 and followed by a
clear-and-blit of the back buffer. The meaning of the values 1 and 0x10 (line-interleaved stereo on/off is the
obvious reading) is not confirmed by any string; speculation. `stereo_page_blank_50370` should be renamed
`vfx1_vip_stereo_enter_50370` for symmetry. *Done in round 6.*

#### File-stream FLI player: the real player is un-functioned asm at 0x5c264

- 0x5c264-0x5c40a is a hand-written player that Ghidra has no function for (prologue `push ebp; mov ebp,esp;
  push eax..esi`; args [ebp+8] = loop flag source, [ebp+0xc] = apply-palette flag -> `DAT_0009e6fc`, [ebp+0x10] =
  cue script for `cue_script_step_17d80`). It opens `DAT_0009e708` (path buffer) with crt open 66296 -> handle
  `DAT_0009e700`, zeroes `DAT_0009e6ec` (frame counter) and `DAT_0009e6f6`, seeds the DOS clock pacing word
  `DAT_0009e6f4` (int 21h AH=2Ch, seconds*100+hundredths), then per chunk reads 4-byte size `DAT_0009e6e4` and
  2-byte magic `DAT_0009e6dc`: 0xAF12 -> `fli_file_read_header_5c4ce`; 0xF1FA -> frame: `DAT_0009e6de`--
  (frames left), `DAT_0009e6ec`++, cue step, `fli_file_play_5c40b` (which is really the per-frame sub-chunk
  loop: count `DAT_0009e6e8`, 8 header bytes to [DAT_000adf68], type 7 -> 5c609, 12 -> 5c6e0, 4 -> 5c56d,
  15 -> 5c787, 16 (FLI_COPY) -> 5c531 straight into the frame buffer DAT_0012ed74, other -> 5c54f discard into
  [DAT_000adf68]), then if `DAT_0009e704` (delay) != 0: palette apply (5c594), `rep movsd` 0x3e80 dwords from
  DAT_0012ed74 to A0000h (320x200 only), `fli_file_wait_frame_5c813`; when frames reach 0: int 10h AX=1200h
  BL=36h (VGA refresh on), loop control through `DAT_0009e6fa` (looping flag, cleared when [ebp+8] and the
  key/mouse words DAT_0012ee0e / DAT_0012eea0 are set), close (664de) and a 500-iteration re-open loop while
  `DAT_0009e6fa` counts down. The function needs to be created in Ghidra (`fli_file_play_5c264`), and 5c40b should
  become `fli_file_decode_frame_chunks_5c40b`.
- `fli_file_skip_chunk_5c56d` is misnamed: it reads the COLOR_256 chunk body (size-6 bytes) into the buffer at
  [DAT_000adf8c] and raises `DAT_0009e73a` [*Renamed `fli_file_read_color256_5c56d` in round 6.*]; the palette itself lives at [DAT_000adf7c] (both pointers are also
  listed in the data table at 0x96f6c / 0x96f98, probably the allocation table that fills them).
- `DAT_0009e6fc` is only ever written by the asm player, which is why the decompiler pruned the palette path of
  5c594 as unreachable; the path is real.

#### Watcom CRT / HMI glue

- `crt_getenv_631ca` is Watcom's case-insensitive DOS getenv (strnicmp + '=' test over `_environ`).
- `crt_prtf_fixed_point_6a5f1` confirms the 16.16 fixed-point %f: integer part = high word, four fraction digits
  by default, round-half-up on bit 15 of the residue.
- `crt_call_hook_a46b9_660bd`, `hmi_drv_call_ax_stub_66f71` and `snd_awe32_get_hw_block_704b6` have no call,
  jump or data reference anywhere in the flat image (scanned for rel32 E8/E9 targets and little-endian dword
  values); all three are dead code. Which Watcom default-routine pointer `DAT_000a46b9` is (it sits between the
  environment selector/offset DAT_000a46b5/b1 and `_environ` DAT_000a46c0 in the startup data) could not be
  established without the CRT source; speculation: a `__null_*_rtn`-style hook such as `__FPE_handler`.
- `cpu_detect_5ac80` copies the five detection bytes into a stack buffer it never uses (compiler kept the
  dead stores); only the family==5 test matters.

#### Open questions

- Exact meaning of the VIP register values written to 303h (1 vs 0x10) and of 302h = 2.
- Whether the data table at 0x96f6c/0x96f98 (holding the addresses 0xadf8c / 0xadf7c) is the game's
  startup buffer-allocation list; not read. *Answered (round 6): yes. They are the dest fields of the `*PalData` / `*PalMem` (0x400 bytes each) records of the 0x2c-byte resource list at 0x96df0 (*SearchD, building, fonts, tmaps.tab, *PalData, *PalMem, pointers, palette.dat), loaded by `file_load_resource_list_5ae80` (was mem_alloc_named) from data_load_all_334c0 / video_alloc_buffers_33480; typed as `ResourceRec` lists in carpet_types.txt.*
- The 0x5678 marker at the start of the EMU8000 hardware block (0xa4eb8) has no reader in the image. *Answered (round 6): confirmed: the only reference to 0xa4eb8 is `mov eax, 0xa4eb8` in `snd_awe32_get_hw_block_704b6`, which itself is unreferenced, so the marker is for code outside carpet.exe (if any).*

# Corrections from the Phase 2 port (2026-10-06, night)

Translating the terrain generator, rasteriser, landscape renderer and table generator into
`src/mcengine` (reports: `docs/analysis/port_terrain.md`, `port_raster.md`, `port_render.md`,
`port_tables.md`) established or corrected the following; the detailed evidence is in those reports.

- **Rasteriser vertices are int[5] {x, y, u, v, shade}**, not int[3]; SpanRec +6 is the integer part of
  x_right (exclusive end), +0/+4 hold 16.16 x_left/x_right. Only clockwise triangles are drawn (the
  back-face cull); fill rule rows [top, bottom), pixels [floor(x_left), floor(x_right)). Mode 26 tests
  the texel value (< 12 -> translucent) rather than a counter; mode 1 writes the shade byte.
- **render_view_2f6e0 takes 8 args** (frame buffer, then the 7 camera values from the player's
  position-history entry: x, y, yaw, z+0x80, pitch, roll, zoom; default zoom 0x80). Its slope smoothing
  nudges cam_x/cam_y. render_landscape calls render_build_roll_table((-roll) & 0x7ff) and
  render_sky(roll); the sky pointer is DAT_000adf48 (no segment trick). yaw 0 looks toward -y, 0x200
  toward +x; pitch is a horizon offset (pitch*width>>8 pixels, positive looks down); the second surface
  is a reflection in z = 0 drawn before the terrain. The 2x2 smoothing filter is
  BLEND[BLEND[p11][p01]][BLEND[p10][p00]].
- **Terrain generation**: all pass parameters come from the level header (fractal: seed, Off, Raise,
  Gnarl; rivers: River, Sourc; classify: SnFlt; lowland/interior: BhLin, BhFlt; steep: RkSte; SnLin
  unused). The fractal uses a private LCG copy; carve_rivers / assign_textures / build_lightmap use
  DAT_0012dfb0, which build_lightmap zeroes first. Corner-class tuple table at 0x93fc8 (0x94 x 4),
  corner order (x,y), (x+1,y), (x+1,y+1), (x,y+1), compact table DAT_000b58b0. The port output is
  bit-exact against movie/map00000.dat on every cell not touched by level-start features.
- **Tables**: the shade table has 64 generated rows (0..0x1f toward palette entry 255 with factor
  0x100-8*row, 0x20 identity, 0x21..0x3f toward black); blend[a<<8|b] = pal[b] + (pal[a]-pal[b])*0x55/256
  (one third toward a). texture_average_colours_72147 fills 0xCD9B0 (+ copy at +0x80) from a 32x32
  window; the shipped tables.dat averages were generated from an older texture set. block16/32.dat are
  256-wide atlases (176 / 608 rows); texture pointers = DAT_000adf5c + row*B*256 + col*B.
- **GameState**: the level file lives verbatim at +0x2f503 (header, THING_INIT at +0x2f945, footer at
  +0x38c93: win percent, player count, castle levels); the "level buffer at +0x442" note above is wrong
  (that range is the free list).

# Corrections from port round 2 (2026-10-06/07: Things, level features, sprites, players)

Translating the Thing core, the level-start features, the Table B constructors, the sprite renderer
and the player / demo code (reports: `docs/analysis/port_thing.md`, `port_features.md`,
`port_constructors.md`, `port_sprites.md`, `port_player.md`) established or corrected the following.
The reports hold the evidence and many smaller items; where an older section of this file disagrees,
this section wins.

- **Calling convention**: game functions take all arguments on the stack (caller cleans up). The
  "Thing in EBX" wording in older sections describes a callee-saved local, not an argument.
- **Level file**: 1999 THING_INIT records (0x442..0x90d0), then 8 per-player blocks of 0xd8 bytes
  (GameState+0x385d3: AI aggression / reaction / accuracy at +4 / +8 / +0xc, starting spells at
  +0x10[24], allowed spells at +0x74[24]), then the footer. GameState+0x38c9f is a u32 creature count,
  +0x38ca3 a u32[24] spell-record count. Class-10 records with DisId 0xffff are placed at the cell
  *corner*, every other record at the cell centre.
- **Level start order** (verified end to end against `movie/gam00000.dat`: scenery, switches, wizard
  castles and the four player things land in the snapshot's pool slots): terrain_build, pool reset,
  terrain_generate_features (walls / paths / canyons / ridges are built by
  level_build_linked_feature from marker records, wizard castles by effect 0x2d in state 0x33),
  models_initialise, switch_activate(0), players_init_records - which only *queues command 1* for all
  8 players; the first tick's player_commands_process spawns them (player_spawn_3f360).
- **Snapshot / movie**: gam00000 / map00000 were written 413 ticks after level start, in the middle of
  player_commands_process (tick counters already incremented, player 0's packet not yet executed), by
  a build that differs slightly from retail (MoveDesc table 4 bytes later, wizard castle capacity
  `w*h >> 2` instead of `>> 4`). The tail of map00000.dat is `g_corner_tex_table` (0xb58b0), not the
  RNG block. mvi00000.dat is a headerless stream of 10-byte command packets, one per player per tick
  (4 players, 8550 ticks) plus the final quit packet. *Corrected (round 5 merge): the per-tick reference shows the retail exe reproduces the movie; footprints and capacity come from the 320x200 mode; see "Port round 4 corrections" at the end.*
- **Video mode is game state**: castle footprints are halved when DAT_0012edae == 1; the recording was
  made with another value, and the port fixes the game-logic value at 8 (full size). *Corrected (round 5 merge): the recording was made in 320x200 (`DAT_0012edae == 1`), where the doubled building.tab makes the halving neutral; see "Port round 4 corrections" at the end.*
- **Castle size table** (pointer at 0xadfb0) comes from `data/building.tab` + `building.dat`: 1..7
  player castle levels, 8..16 1x1, 17..68 wizard buildings (size = THING_INIT.Parent + 0x10).
  Footprint byte: height = base + (low nibble - 1) * 4 (not the high nibble), paint kind = high
  nibble + 0xb. `data/search.dat` is the 32x32 ring table of the spiral walks, whose last cell is
  never processed (callers test `== 1`, the iterator returns 2 with it).
- **Class 10 names**: type 0x2d is a "wizard castle" building (townies walk to it and raise its
  inhabitant count, so it reads like a town building - interpretation, not established): state 0x33 builds it at level
  start, 0x34 (27710, formerly "ridge node") is the living castle (aux = inhabitants, speed_base =
  capacity), 0x35 (27930) its collapse. Type 0x1b is a wall piece (states 0x1b / 0x1c / 0x1d by
  direction - the "steal_mana_s27 / type26 / type27" names are wrong), 0x1e a path piece, 0x20 the
  canyon digger, 0x33 the ridge raiser, 0xa an impact dent; 0x1c / 0x1d / 0x1f / 0x32 are markers.
  Castles without a level record are placed by creature_genie_s72_place_castle_1d540.
- **Constructors**: class 3 types 4..11 are start-position markers (`start_pos[type-4]`), not things;
  classes 1, 6, 8 and weather 0..3 allocate a slot and drop it. Effects are made recyclable with
  `flags |= 0x20000` (not 0x200). Creature +0x3f starts as a per-type serial from GameState+0xc+type,
  +0x3a from the descriptor's think period. Projectile and effect type -> state maps are in
  port_constructors.md.
- **Sprites**: the descriptor table is completed at load time (half_xy from the tmap aspect ratio,
  draw_type = tmap header byte 1; Thing.draw_type = last animation frame). tmap header byte 0 bit0 =
  animated (FLIC SS2 frames follow the pixels; 213 of 529 sprites), bit3 = drawn since the last
  texture_anim_update. Group id = index of the group's first sprite. Pixel mode 8 darkens the
  destination (shadow), 9 is a solid colour. sprite_groups_reload_by_priority_4bfb0 compares against
  the *address* of tmap_free_space (never true); sprite_mark_needed_for_model's record (5,4) has no
  terminator. render_sprite_scaled drops one edge row / column in the odd roll octants.
- **Player**: the 32-entry "position history" is the camera log read by render_frame_1fab0, and only
  its last entry is ever written. P+0x10 is the strafe speed; turning is P+0x147
  (`yaw += P+0x147 / 8`), pitch P+0x149; flight height is governed by the descriptor's cruise height
  (0x400) and clearance (0x80). P+0x20a..0x20e are AI aggression / accuracy / reaction (not a castle
  position). P+0x14b is an invulnerability timer, P+0x304[24] the quick-select key table. The shield
  flag 0x4000 is cleared by the first hit it absorbs. Config+9 is the movie file handle, Config+0x5e..
  are tick-cadence bits `(tick / k) & 1`, game_check_level_won divides by Config+0xbc.
  `chat_message_show_3bb50` only sets the input mode.

## Port round 3 corrections (2026-10-07)

Facts established while translating the simulation; where they contradict text above, these win.
Details and evidence are in `docs/analysis/port_{core3,projectiles,spells,effects,castle,creatures,input}.md`.

- **Damage slots** (Thing+0x5a, six of `{i32 amount, u16 attacker}`): 0 = ordinary damage, 1 = mana
  ball claim, 3 = steal mana, 4 = magnet pull, 5 = castle upgrade request. Area functions accumulate
  while an attacker is recorded; `thing_add_pending_damage_117c0` does the reverse. Castles are only
  reached through the player list. Flag 0x10000 is tested but ignored in `thing_area_damage_10d20`.
  Creature damage intake clears only the attacker word of slot 0.
- **MoveDesc**: +2 = max yaw step, +6 = max pitch step, +0xa = *maximum* and +0xc = *minimum* height
  above ground (`clear_lo` / `clear_hi` in mc_types.h are swapped in meaning), +0x10 = max walkable
  slope / max pitch down, +0x12 = max pitch up.
- **Creature states**: base = type * 6; +0 type-specific, +1 main, +2 attack, +3 follow, +4 dying,
  +5 dead. `creature_attack_target_18c20(thing, base, callback)` has three arguments. The awake gate
  (+0x3a) is set by `creature_proximity_wake_timer_46960` relative to the local player. No creature
  code calls the projectile target selection: a projectile aims itself on its first tick.
- **Projectiles**: type is not state (types 14..19 run states 15..20; state 14 is the lightning
  segment). `projectile_line_of_fire_clear_466af` is dead code. A miss stores an address-dependent
  garbage index in the impact effect's `target` (the port stores 0).
- **Spells**: `spell_phase2_pickup_46dd0` uses flag 0x40000; the picked Thing becomes the owner's spell
  and a copy stays as the pickup. Earthquake charges mana on the launch tick only. Holding fire gives
  one refused cast (sound 0x1d) after each shot - original behaviour. Spell 22's level pickup can
  never be picked up (state 68 is bound to the cast handler).
- **Effects**: `terrain_cell_flag_bit & 1` = class-0 water without the animated-sea bit. Castle raise
  is 18 steps plus a 1- or 25-tick wait. `thing_drop_mana_ball_25fe0` drops one ball and leaves the
  dropper's mana untouched; `mana_ball_merge_26120` frees the second ball at once. GameState+0x24 /
  +0x26 = erupting crater thing / its smoke column.
- **Castles**: class 3 state 4 = active castle (413a0), 5 = building / upgrade (41500), 6 = destroyed
  castle (416d0, not a respawn), 9 = balloon. Levels 2 and 3 share a footprint size. A parked balloon
  is repaired every update before damage is applied. The victory switch reads player record 0.
- **Input**: all UI coordinates are in a 640-wide virtual screen (640x400 in the 320x200 mode, 640x480
  otherwise). Command 0x1c can never be queued (the function-key layer eats Space first); nothing in
  the input code queues commands 5, 7, 8, 0xb, 0xe, 0x12. `ui_button_table_dispatch_17a80` is dead.
  The shipped movie's packets were recorded in 320x200; the snapshot is taken after 412 complete
  thing updates, and the recording player used the "all spells" cheat on tick 390.

# Port round 4 corrections and the per-tick reference (merged in round 5, 2026-10-07)

This section merges the "Corrections" sections of the round-4 port reports
(`docs/analysis/port_{ai_wizard,creatures2,creatures3,hud,sound,reference}.md`) and the items of the
round 2-3 reports (`port_{thing,features,constructors,sprites,player,core3,projectiles,spells,effects,castle,creatures,input,terrain,render,raster,tables}.md`)
that the two summaries above left out. **Where an older section of this file disagrees, this section
wins**; the older statements are kept and marked "*Corrected (round 5 merge)*" in place so a reader who
relied on them sees what changed. The reports hold the evidence (addresses, disassembly).

Round 5 also renamed the dispatch-table handlers whose names were wrong (creature states, class-10 and
class-9 state -> type, castle handlers, switch states, start-position markers, ...);
`docs/analysis/names_round5.md` has the old -> new list. Older sections of this file still use the
old names; binding in the port is by address, so nothing depends on the names.

## The per-tick reference settled (port_reference.md, ROADMAP Phase 2 round 4)

- A patched copy of the **retail** carpet.exe in the bundled DOSBox dumps the GameState every tick of
  movie 0, and the port reproduces every Thing, every PlayerRec and the global RNG byte for byte over
  the whole movie (`reference_test`, a ctest gate). So the retail exe *is* the reference; no "recording
  build" is needed to explain the shipped movie.
- **The movie and its snapshot were recorded and are played in 320x200** (`DAT_0012edae == 1`). In that
  mode the tab relocation (0x49de0 with 0x62a80) doubles every width / height byte of the relocated
  sprite tables, which includes `building.tab` - so the castle code's "halve w / h when
  `DAT_0012edae == 1`" gives the full size back. The wizard castle capacity `w*h >> 4`
  (`effect_wizard_init_35090`) is taken *before* the halving, i.e. from the doubled size (4x the
  640x480 value) - that is the `w*h >> 2` the snapshot shows. All round-2/3 "build difference"
  explanations (castle footprints, capacity) are replaced by this. Game logic therefore depends on the
  video mode; the port models it in `castle_footprint()`.
- Movie playback loads the French notices (`data/ftext.dat`).
- A projectile that hits nothing stores `(NULL - &things[0]) / 0xa4` (an address-dependent garbage
  index) in its impact effect's `target`; the port takes the value from the reference run's pool base
  (`g_projectile_null_hit_index`).
- `render_frame_1fab0` writes game state once per rendered frame (spell flash, message timers, book
  selection: `hud_tick_state` in the port), and the original renders once per tick.
- Dump point: right after the thing-update passes of `game_tick_update_32e80`, before
  `sound_update_494b0` (= the end of the port's `game_tick_sim()`).
- `demo_save_state_3c2c0` writes the raw GameState (not RNC-compressed); only the loader accepts both.
- **Command line** (`config_parse_33750`): options need a `-` or `/` prefix (`carpet movie 0` prints
  "ERROR : Incorrect command : 1"); `-movie n` stores n in cfg+0xd only when n != 0; `-roll n` sets
  cfg+0xf and flags 0x120 but nothing enables playback (flag 4). The shipped attract mode of the main
  menu (3 idle periods of 0x12c0 timer ticks; Config.movie = 0, flags |= 0x24) is the only movie path.
- `player_cd_check_quit_3bbd0` (called from `player_set_input_mode_3bb50`, i.e. on the spell-book
  packets, every 8th tick) sets PlayerRec.quit = 2 when the game does not run from a CD; it has no other
  GameState side effect.
- The MoveDesc table offset "4 bytes later" in the snapshot (round 2) is an observation about absolute
  pointers in the saved block; it was not re-examined, and nothing in the simulation depends on it
  (descriptor pointers are rebased on player 0's thing = descriptor 7).

## Thing core, collision and helpers (round 2-3 details)

- `angle_turn_step_3e800` takes 4 arguments (cur, target, unused MoveDesc+4, max step = MoveDesc+2).
  `math_atan2_4cc33(dx, dy)`: octant lookup in the 0x9b3ec table, 0 = -y, 0x200 = +x, result 0..0x800.
- `pos_dist_manhattan` sign-extends before subtracting (no wrap); the other distance helpers subtract
  in 16 bits first (they wrap around the 256-cell torus).
- `terrain_type_mask_at_10480` maps the cell's *texture id* (0..0x22, jump table 0x103f4; > 0x22 ->
  0x800000, 13 / 14 -> 0) to the MoveDesc terrain-mask bits.
- `thing_alloc_35560`'s recycle path drops all per-tick lists (the victim may be in one).
  `thing_set_sprite_small_352d0` is functionally identical to `thing_set_sprite_35240`.
- The four spiral searches walk rings `0 .. (ext_x + 0xff) / 256` around the cell of
  `((x + 0x80) >> 8, (y + 0x80) >> 8)`. The slot-0 area walks use `(x - 0x80) / 256` (idiv, toward
  zero) and a square of `+-(ext_x + 0xff) / 256` cells; their test order is owner, overlap,
  `prop_flags & 1`, flag 8, not a castle, filter (other slots: owner, class != 0, flag 8,
  `prop_flags & (1 << slot)`, filter, overlap). 11450 sets the hit timer +0x32 = 0x1e on every castle
  the box touches, the dealer's own included.
- `creature_check_terrain_102b0(t, pos, flags)`: flag 2 tests `pos.z` between ground + MoveDesc+0xc
  (minimum) and ground + MoveDesc+0xa (maximum); flag 1 returns the offending type-mask bits; flag 4
  limits the pitch to `pos` by MoveDesc+0x12 (up) / +0x10 (down).
- `castle_footprint_clear_11980` tests four border strips of the grown footprint for built-on cells;
  strips 3 / 4 use `dy` rows and strip 4 restarts each row at the left edge (kept as in the original).
  `cell_kill_things_3da30` compares the sign-extended owner with the zero-extended argument.
- `data/search.dat` = 32x32 ring numbers; ring 0 is a 2x2 quad whose first cell is the centre; the
  last cell of the last ring is never processed (the iterator returns 2 with it, callers test `== 1`).
- Thing flags: bit 0 = hidden / not drawn (the genie sets it while hidden; also set on the local
  player's flyer); 0x80 on a spell Thing = speed sound requested; 0x4000 shield (set every cast tick,
  cleared by the first hit it absorbs, `player_apply_hits` 0x40d2a); 0x8000 rebound (cleared by the
  first idle tick of the spell; an attacking griffon sets it every tick); 0x10000 is tested at 0x10d46
  but nothing branches on it (no effect); 0x40000 = sealed / dropped spell (46dd0).

## Computer wizard AI (port_ai_wizard.md)

- `ai_approach_target_140d0(thing, target, near, far)` has 4 arguments; a null target measures the 2D
  distance to Thing.home. Beyond `far` with spell 2 ready the AI casts it instead of cruising.
- **Spell 2 is speed-up** (`spell_speedup_update_47420`, Table B `spell_create_speedup_3a360`), not
  Alliance; `ai_mode12_idle_12680`, mode 0xb without a castle and the far branch of 140d0 cast it.
- The hostility threshold is `50000 - (other.mana_total / 10 * aggression) / 255` (mana_total, not
  health).
- `ai_cast_spell_14240` refuses spell ids above 0x11 *after* `ai_spell_ready` passed, so
  `ai_mode11_return_home`'s "farther than 0x2800 -> cast" (spell 0x13) always fails. Case 0x10 without
  a castle is just `thing_create(home, 3, 2)` + owner + P.castle (no cooldown, no cast_ticks). Every
  cast writes `cooldown[spell] = DAT_000938c4[spell]`.
- `ai_goal_attack_castle_13000`: "farther than 0x1e00" is the castle **owner's** distance from his own
  castle; the richness test is reversed: the *own* castle's mana must exceed the target castle's mana +
  (255 - aggr) * 0x280. `ai_goal_attack_wizard_13210`: `(255 - aggr) * 32 + their mana < own mana`,
  for wizards *without* a castle that own the castle spell. `ai_find_mana_ball_target_13ce0`: hostile
  owners' balls are measured from the own castle; `ai_goal_hunt_creature_13770`: from the own castle
  when there is one.
- `ai_choose_attack_spell_14c70`: order 0x11, 8, [target casting rebound: `rand() % 255 < accuracy`
  -> 0xf (lightning) else none], 7, 0, 0xf - the accuracy roll is made only while the target casts
  rebound. This and 14f00 are the only users of the C runtime `rand()` (crt_rand_5aff8) in the game.
- `ai_spawn_spells_14b00`: the countdown loop runs once per **human** flyer in the player list.
  `player_ai_wizard_tick_11f20`: the damage slots are cleared whenever `invuln_timer != 0`; the dolmen
  flag 0x1000 also gives the fast regeneration.
- `ai_record_threat_from_projectiles_150f0`: the grudge threshold is computed with the *castle's*
  player block (the dummy, aggression 0), so it is a constant 50000; projectile type 0xa (castle seed)
  adds no threat but still runs the grudge test.
- `PlayerBlock.accelerating` (P+0xe) is set by every AI speed decision and cleared at the start of
  `ai_approach_target`: for the AI it means "speed decided this tick".
- `ai_goal_none_12550` (was `ret_zero_12550`) is the goal test of modes 2 and 5.

## Creatures (port_creatures*.md)

- **State layout**: six states per type from `type * 6`: +0 idle / type-specific, +1 main
  (`creature_ai_step` or own code; the state constructors set), +2 attack, +3 follow, +4 dying, +5
  dead (`creature_die` sets base + 5). Exceptions: crab +3 = collect mana; builder +1 wander, +2 walk
  to a wizard castle, +3 pick one, +0 found a castle; townie / trader +0, +2, +3 are `ret`, +4 = arrived
  (vanish into the castle) or killed. `creature_attack_target_18c20(thing, base, callback)` has three
  arguments; `creature_follow_leader_18e90` switches on `leader.state - base` (0, 1 follow; 2 take the
  leader's target; 3 adopt the leader's leader; else drop it). States 102..119 are disabled `ret`s.
- **Awake gate** +0x3a: set by `creature_proximity_wake_timer_46960` (0x10 within 0x1800 xy of the
  local player's thing, segments 0x12), by the constructors (stagger
  `think_period - serial % think_period + 4` for dragon, bee, worm, archer, crab, skeleton, type 15;
  `think_period + 1` vulture; 0x40 the rest) and `creature_wake_tick_468e0` (0xfa for dead creatures;
  skipped while `Config.flags & 0x10`). 46960 also writes `+0x30 = isqrt(dy^2)` (the caller pushes the
  EDX left by `pos_dist_sq_xy_3e970`). Asleep creatures move and think but neither take damage nor look
  for enemies (the crab is the exception, see below). Mana balls use the same gate (`timer_a` 0x80 at
  creation, then 16 ticks at a time near the local player).
- Damage intake clears only the attacker word of slot 0 (the amount stays).
- `creature_die`: kill credit only when the killer is a class-3 type-0 thing, the creature is its own
  owner, not type 9 / 0xc..0xf, and (type 9) has no mana owner. `creature_attack_volley_19680`:
  n = mana * 7 / mana_total; kind = (rng % (n * 100)) / 100 with the unclamped n, then n clamped to
  1..5; kind 0 -> n type-0 shots (damage 400), 1 / 2 -> n - 1 type-9 shots (impact 0x17, damage 800),
  3..6 -> one type-3 shot (impact 0x11, damage 8000). `creature_attack_fire_homing_desc_19a90` is dead
  and ends with `thing_move_to(projectile, creature position)`, undoing its z raise.
- **Skeleton** (9): 54 = rising animation (constructor state); 55 walks toward the nearest castle of
  another owner anywhere on the map and attacks within sight + castle half width; any player-list
  thing of another owner in sight / fov is attacked while awake; conversions use the Archer / Builder
  / Townie lists in turn; the summoning pose starts after 400 asleep ticks (50 after an attack),
  `aux = -50` when broken off.
- **Builder** (12): 72 tries up to four sites (east / west / south / north of the target wizard castle);
  after founding one it continues in state 0x4f (townie) with type 0xc. 73 / 75 / 74: wander for `aux`
  think ticks, pick the nearest wizard castle (3D distance > 0), walk to within 0xa00.
- **Townie / trader** (13 / 14): walk to the nearest wizard castle (trader: nearest farther than
  0x3c00) to within 0x800 and enter when `castle.speed_base > castle.aux` (base + 4 with aux = 1 deletes
  the villager, castle.aux++); a full castle is forgotten. Villagers blame a killer of type 0 / 1
  (P+0x210 = 200) without testing its class.
- **Archer** (4): attacks only wizards with P+0x210 != 0 (tested every 4th think tick) and skeletons;
  re-arms P+0x210 = 200 while shooting; walks into a wizard-castle target (within 0x1000).
- **Crab** (5): wanders and eats type-0x27 mana balls (grows: sprite 0xb9..0xc0, +5000 max health per
  size step); lays a crab egg (class 10 type 0x34) for 500 mana when carrying more than mana_total +
  500; regenerates max_health / 128 per tick in states 31 / 32; attacks the nearest player-list thing
  in sight / fov **awake or asleep**; the volley is its only attack. `crab_target_nearest_mana_ball_1aef0`
  has no caller.
- **Kraken** (6): grip cycle of 41 ticks of pull (the target wizard's P+0x16 / 0x18 / 0x1a knock-back =
  0x50 / toward the kraken / 0x100, sound 0x2a) and 90 of rest; bursts of 5 type-9 shots (impact 0x17,
  800 damage, desc 0x96ad0) counted in Thing+0x47.
- **Troll** (7): firing pose sprite 0xc6 for 30 ticks at turn-rate speed (0x55 variant only); state 43
  "regeneration" is a full heal on think ticks.
- **Griffon** (8): passive by default; attacks a class-3 thing that damages it (state 49 -> 50) and, on
  its own, only wizards that are already "wanted" (P+0x210 != 0). It makes wizards wanted when it shoots
  at them or is killed by them. No flag-0x20 exclusion in its enemy search. State 50 sets the rebound flag
  0x8000 every tick, so an attacking griffon throws projectiles back at their owner.
- **Emu** (10): archer arrows with the plain `creature_ai_step` (no "wanted" test).
- **Genie** (11): hidden (flag bit 0) while wandering and regenerating (state 67); teleports next to
  whoever hits it, whoever is in sight, or (above 3/4 health) the first player thing that holds mana;
  shoots class-9 type-8 bolts (68); vanishes again (teleport by 0x3200..0x6f00 in x and y) below half
  health or when the target leaves sight; eats type-0x27 mana balls up to `mana_total` (= 2 * mana).
- **Type 15** = castle-wall guard archer: terrain mask 0x20000 (castle textures 0x15 / 0x16 / 0x18), so
  it dies when the castle cell under it goes away; grid-based movement (weights {7000, 7000, 10, 7000}
  at 0x1ea40); attacks only things of another owner.
- **Wyvern** (16): hunts villages (class 10 type 0x2d) every 41 ticks besides `creature_ai_step`'s
  targets; bursts of 15 homing-descriptor fireballs (desc 0x96a50) once facing the target within 0xe3.
- Movie 0 contains no crab, kraken, troll or griffon (none in the snapshot or in any of its ticks).

## Castles, balloons, scenery, switches (port_castle.md)

- Class 3: state 4 active castle (413a0), 5 build / upgrade sequencer (41500), 6 destroyed castle
  (416d0, not a respawn), 7 balloon waiting (42520 `ret`; a balloon is created in state 7 and only flies
  once a castle sets state 9), 8 / 10 unused `ret`s, 9 balloon (42530). Types: 0 human wizard, 1 computer
  wizard, 2 castle, 3 balloon, 4..11 start-position markers.
- State 5 steps (`cast_ticks`): 0 crush wizards / site test / begin stage, 2 -> state 4, 3 effect 0x2a,
  5 effect 0x29 (no writer of 5 exists), 1 / 4 / 6 wait. State 4: `duration` (+0x32) is a busy timer
  (0x1e from the quake, 5 after a collapse); balloons, guards, spill and ball pickup run on even
  `Thing.tick` only. Thing+0x2e on a castle = guard cooldown, +0x7c = `damage_slots[5].attacker`.
- `castle_manage_balloons_and_guards_419a0`: P+0x126 is cleared every loop iteration (ends as the last
  balloon's cargo) and P+0x122 is never cleared; the free-ball search is measured from the balloon
  (`castle_find_free_mana_ball_41290`'s first argument is the balloon). `castle_begin_build_stage_41f00`:
  the effect gets flag 0x10000 and `spell_flags = 0`; P+0x1a0 gets the level as a word.
- A balloon parked at its castle is repaired every update before damage is applied. Castle levels 2 and
  3 have equal footprints, so the 2 -> 3 upgrade only tests for other castles.
- Possible mana duplication in the original: neither castle nor balloon tests the delete flag of a
  ball, so a ball both touch in one update is credited twice.
- Trees set the recyclable flag 0x20000 on **every** update of state 0 (not only when burning);
  burning clears flag 8; burnt trees never regrow.
- Switches test on every 8th update; the victory switch reads player record 0; "all creatures" =
  lists 0..0xb and 0x10; creature triggers fire 17 updates after the list empties; repeat switches re-arm
  after 10 updates without a player Thing in the triggering position; SwiId >= 0x8000 never matches.

## Effects, level features, terrain painting (port_effects.md, port_features.md)

- Class 10 state -> type: state == type up to 0x1a; 0x1b..0x1d = the three direction states of the wall
  piece (type 0x1b); 0x1e..0x2e = type + 2 (0x1e / 0x1f / 0x21 are markers that delete themselves);
  0x30 / 0x33 build the wizard castle (type 0x2d), 0x34 = living wizard castle, 0x35 its collapse;
  0x36 marker (type 0x32), 0x37 ridge raiser (0x33), 0x38 crab egg (0x34), 0x39 hatching egg, 0x3a..0x3d
  = types 0x35..0x38. Constructors 0x14..0x16 and 0x18 return null; type 0x23 returns the raw allocation.
- Types: 0x10 lava blob, 0x12 erupting crater (registers in GameState+0x24), 0x13 its smoke column
  (+0x26), 0x17 lightning strike, 0x19 steal mana (damage slot 3 once), 0x1a writes damage slot 4 every
  tick, 0x1b wall piece, 0x1e path piece, 0x20 canyon digger, 0x23 orbiter (unreachable), 0x24 skeleton
  army, 0x26 storm cloud, 0x28 mana hoard marker (owns balls through +0x90), 0x29 castle ground
  levelling, 0x2a castle raise, 0x2b castle seed landing, 0x2c boulder (disabled), 0x2d wizard castle,
  0x33 ridge raiser, 0x34 crab egg, 0x35 fire pillar, 0x36 mana magnet, 0x37 delayed blast, 0x38
  nothing; 0x1c / 0x1d / 0x1f / 0x32 are markers (`level_build_linked_feature_34c40` does the work).
- `terrain_cell_flag_bit(pos) & 1` = flag low nibble 0: class-0 water *without* the animated-sea bit 3
  (open sea has nibble 8 and does not match). Bit 3 doubles as a parking bit: 26b50 / 26f10 move bit 7
  (built-on) into bit 3 after their last height step and back when their wait ends.
- Castle raise: 18 height steps (aux 0x12..1), paint on `aux % 7 == 0` and on the last step, then a
  1-tick wait, or 25 when +0x3c is set (the type-0x2a constructor sets it).
- Footprint bytes (26320, 26680, 26f10): `>= 0xf` with high nibble != 3: height = base + (low nibble -
  1) * 4, skipped when the low nibble is 0, paint kind = high nibble + 0xb; high nibble 3: low % 3 = 1 ->
  base + 0xc, 2 -> base + 0x10, 0 -> no height, kind 10 + low / 3. 26320 writes the class with `& 0xf8`,
  26680 with `& 0xf0`.
- Castle size table = `data/building.tab` + `building.dat`: 1..7 player castle levels (8x8, 21x21,
  21x21, 35x35, 35x35, 48x48, 48x48 at 640x480 scale), 8..16 1x1, 17..68 wizard buildings (size =
  THING_INIT.Parent + 0x10). Wizard castle fields: aux = inhabitants, speed_base = capacity, mana =
  aux << 8 every 40 ticks, `damage_slots[1].attacker` = claiming player, mana_owner = current claimer,
  +0x32 = hit timer. Castles without a level record come from a builder (state 72).
- `terrain_modify_cell_3d620 / 3d7d0`: new height != 0 -> class 1; height 0 -> class nibble cleared only
  when all 8 neighbours are non-land, otherwise the flags stay. `terrain_set_quad_texture_32430`
  clamps light below 0x20 to 0x20; the retexture functions use (v & 3) + 0x1c below 0x1c.
- `level_run_terrain_effects_34fa0` does not increment Thing.tick (a crater's `tick % 3` uses its
  thing index for the whole generation). `level_spawn_effect_record` places things at the cell corner.
- `thing_drop_mana_ball_25fe0` drops one ball with all the mana and leaves the dropper's mana (only its
  mana owner is cleared); `mana_ball_merge_26120` frees the second ball at once (`thing_free`); a
  resting ball's z_vel alternates 0 / -0x10. `terrain_ring_find_height_ne8_24d70`'s start radius byte
  is never written (open question 1 of region B confirmed).
- Level 38's two volcanoes are live things at level start and end at tick 18.

## Projectiles and spells (port_projectiles.md, port_spells.md)

- Class 9: type == state for 0..13; state 14 = the bolt segments of the type-9 lightning; types
  14..19 run states 15..20. Type 1 = possession / homing shot, 9 lightning, 0xa castle seed, 0xb skeleton
  army seed, 0xc thunderbolt carrier (leaves the storm cloud 0x26), 0xd arrow, 0x10 fire wall shot,
  0x11 mana magnet shot (state 18: ground-hugging, stops at a mana ball), 0x12 smart bomb shot (state 19:
  turns into its impact effect at once).
- `projectile_lightning_update_44fc0` steps until the Thing is marked for deletion; the "quartered"
  case is a rebound-flagged class-3 target with mana >= projectile mana / 4 (signed). The same rebound
  condition holds in `projectile_fly_and_impact_44150`; 44510 has its own rules.
  `projectile_target_score_46470`: the sine terms are (4 d sin)^2 (weight 16), the distance limit is
  xy. The aim byte +0x1a is only clamped by 45f00 and never read by class-9 code. The
  `player_set_under_attack_timer` call of 45f00 applies to the type groups {0, 3, 4, 0x10, 0x12, 0x13}
  and {7, 8, 0xb, 0xc} only. 45360 is the castle-seed state handler; 45530 runs without a target Thing
  and creates the impact on ground contact / lifetime regardless of a free site (stepping back once
  when the site is taken).
- Spells: `spell_phase2_pickup_46dd0` uses flag 0x40000; the picked Thing itself becomes the owner's
  phase-0 spell and the *new* Thing stays as the pickup. Table A state 68 is bound to the cast handler
  0x49140 (`spell_smart_bomb_update_49140`), so a level pickup of spell 22 can never be picked up.
  Level records SwiId 3..5 = phases 0..2 of a sealed spell. Earthquake charges mana on the launch tick
  only. Castle spell: `cast_ticks == duration - 1` is the busy state; a failed `spell_can_cast` resets it.
  Heal charges 1000 per tick for 5 % health and stops below 1000 mana. Invisible (48250) zeroes P+0x14b
  through the *spell's* player pointer (the dummy block): a no-op; caster flag 0x20 is cleared by
  `player_cast_spell_410f0`, so casting anything ends invisibility. Teleport is the only cast handler that
  draws from an RNG (the spell Thing's own). Holding fire gives one refused cast (sound 0x1d) after each
  shot: one fireball per 3 ticks at 100 mana per tick.

## Player, input, demo (port_player.md, port_input.md)

- Command packet: +0 cmd, +1 arg, **+2 second argument** (cmd 0x17: book slot), +3 / +4 steer, +5 key
  bits (only under cmd 5 / 6). `player_queue_command_17270`: cmd 0x1b needs status & 2, 0x1c status & 4,
  0xf health < 0 and state 3; the name test is 9 bytes at PlayerRec+0x40a, lower case. Command 0x1c can
  never be queued; nothing queues 5, 7, 8, 0xb, 0xe, 0x12; command 6 has no handler; command 0xf =
  respawn request (no castle: status |= 0xc). Chat line "RATTY" sets Config.flags |= 0x8000.
- Input modes (PlayerRec+0x44a, read by render_frame as the view mode): 0 flight, 1 help screen
  (Enter / both buttons leave it), 2 spell book (+ map), 3 chat line, 4 map screen (input handled like
  0). `player_set_input_mode_3bb50` (was `chat_message_show_3bb50`) sets it (pointer sprite +6 for the
  book, input_mouse_center otherwise).
- PlayerRec.status: 2 won (Shift+C sets it), 4 lost (Shift+F), 0xc restart (Shift+R, cmd 0xf without
  a castle). PlayerRec.flags: 8 debug overlay; 0x10 / 0x20 toggled by Alt+M / Alt+F, read nowhere.
  PlayerRec: +7 u16 own index, +9 is_computer, +0x12 u32 tick, +0x18 u32 (kept by
  players_clear_records; 0xae89e = the 1,000,000-health dummy), +0x1c 8 x {char[0x40], u16 ticks, u16
  arg} messages by sending player, +0x44b cleared per tick. `players_init_records` queues cmd 1 for all
  8 players.
- **Camera log** (PlayerRec+0x24a, 32 x 14 bytes, index PlayerRec+0xe): read by render_frame_1fab0;
  {x, y, z, yaw, pitch, roll, zoom}; pitch = `tickbit1 * (knock / 16) + pitch_acc / 2 - knock / 8`,
  roll = P+0x147, zoom = PlayerRec+0x248 (command 8 adds to it). Only entry [PlayerRec+0x10 - 1] is
  written; command 7 moves the view onto entries never written. Not a network trail.
- P+0x10 = strafe speed (yaw + 0x200, +-0x50, step 0x10, decay 4); turning is P+0x147
  (`steer_dx = (2 * steer_x - P+0x147) / 4`, `yaw += P+0x147 / 8`), pitch P+0x149 (positive = down).
  Flight height: descriptor +0xa cruise height (0x400), +0xc minimum clearance (0x80); the climb fades
  out at the cruise height (`ratio = clamp((z - ground - 0x400) * 1024 / 0x400, +-0x100)`); at speed 0
  the wizard sinks 8 per tick above cruise height. Flight constants are data (0x94384.. step / min /
  max / knock decay, 0x943a4.. strafe).
- P+0x1e[8] kills of player n's wizard; P+0x13e i32; P+0x14b invulnerability timer (100 at spawn, 2 in
  the own castle); P+0x189 damage flash; P+0x304[24] quick-select key -> book slot (0xff free);
  P+0x34c[24] (by spell id) 0x20 when chosen by such a key; P+0x3ac / +0x3b0 0xff until a spell is
  chosen; P+0x20a / +0x20c / +0x20e = AI aggression / accuracy / reaction from the level player block
  +4 / +0xc / +8 (not a castle position). Level player block: +0x10[24] starting spells, +0x74[24]
  allowed spells.
- `player_dying_update_405f0`: spell slots go back to spell ids; dropped Things get state + 1 and life
  200..289; AI respawn delay `((255 - reaction) / 8) * 32 + 32`. `game_check_level_won_3db20` divides by
  Config+0xbc and is skipped when Config.flags & 0x110.
- Option bytes: F4 +0x219d soften, F5 +0x2195 reflections, F6 +0x2197 sky, F7 +0x2196 shadows, F8
  +0x2199 and +0x219a, F9 +0x219c speed blur (0 / 1 / 2), F10 +0x219b 3D mode, Alt+H +0x219e;
  `opt_allowed[]` (+0x21ad): [0] reflections, [1] shadows, [2] sky, [6] 3D mode, [7] speed blur, [8]
  soften, [10] resolution switch. View size +0x2198: `[` grows to 0x28, `]` shrinks to 0x11.
- Config: +0x16 = book cell under the pointer (index into DAT_00097660, 0xff none), written by the HUD
  every frame; flags 0x200 = no spell book by double click (movie); 0x8000 = cheat gate; +0x5e / 5f / 60
  = tick bits (blink phases 1 / 2 / 3 ticks); +0xa1..+0xa7 credits state {u8 1 roll / 2 restart / 3
  pause, i32 countdown, u16 line} over the 112 credit pointers at 0x9861c.
- GameState+0x240 (i32) = the level's music track `(lcg(level) % 3) + 1`.
- Language strings: 80 pointers at 0xadce8 into data/{e,f,g,i}text.dat; `DAT_000addxx` = string
  (addr - 0xadce8) / 4.
- Keyboard: DAT_0012eea0 raw last byte, DAT_0012e396 the byte before, DAT_0012e397 first make code;
  in-game text table DAT_0009e5e8[128]. Mouse: driver range 0..0x1400 x 0..0xf00 (>> 3) at 640x480,
  0..0x280 x 0..0x190 at 320x200 (`mouse_set_range_5be68`); clamp 0x27e / 0x1de; start (320, 200). All
  UI coordinates are in a 640-wide virtual screen; the blitter halves position and size at 320x200.
- `ui_button_table_dispatch_17a80(table)`: 0x18-byte records (rect, mouse condition, key, key-table
  tests, command, arg); no caller.
- `demo_relink_state_pointers_3dc10` only fixes the players' own Things and the descriptor pointers.

## HUD and frame (port_hud.md)

- DAT_000acc18 = 16x16x16 colour cube `[r << 8 | g << 4 | b]` of nearest palette indices (built by
  data_load_all_334c0) [*Corrected (round 6 merge): nearest to `(r*4+3, g*4+3, b*4+3)`, so black is palette entry 12.*]; the HUD colour constants are entries of it (0xacc18 black, 0xadc17 white, 0xadb18
  red, 0xacd08 green, 0xacc27 blue, 0xadb27 magenta); blinking text uses `cube[(Config+0x5f) * 0xfff]`.
- DAT_00097630: 8 player colour pairs; `[p * 2 + tick bit]` blinks. DAT_000adfbc = current font = a span
  sprite table (glyph c = entry c + 1; +0xca / +0xcb space glyph w / h); the FontDesc struct at 0x9e508
  belongs to the front end only. DAT_0009e5c4 = blit flag word of `vga_draw_sprite_spans_6070d` (0x40
  solid colour); DAT_0009e858 is null and never written.
- `ui_draw_sprite_blend_224e0` / `ui_draw_sprite_tint_22610` / `ui_draw_sprite_shaded_22760` (the first
  two were `ui_fill_rect_blend*`) are span-sprite blitters, not rectangle fills.
- GameState+0x2199 gates radar + blips, +0x219a the hand labels and status panels. The radar centre
  cross uses shade levels 0x2c..; spell 5 being cast shows the other wizards' names and every balloon.
- HUD sprite tables: 0x97294 (320x200: data/mspr0-0) and 0x97370 (640x480: hspr0-0), chosen at 0x33480;
  0x49de0 relocates the tab list 0x9744c (at 320x200 it also doubles every width / height byte).
- data/bookbkg.dat and book.pal are not referenced by carpet.exe.

## Sound and music (port_sound.md)

- Request record at 0xb7ac0 (per sound id): {+0 mode (1 restart, 2 simple, 3 play if idle, 4 loop /
  stop), +2 pan, +4 volume (priority reference, cleared every tick), +6 owner, +8 played flag}.
- Sample banks: see FORMATS.md. `DAT_0009e328` is the memory class / bank quality (1 = 22 kHz, 0 / 3 =
  11 kHz, 3 = low-memory bank), not the card type. Set 0 is the in-game bank (game_main always loads
  it); sets 1..13 are cue-script speech.
- Music: `music<set>-<device>`, device = DAT_0012e06e from the HMI device id: 0 OPL2 FM (*.HMP), 1 MT-32
  (*.ROL), 2 General MIDI / AWE32 (*.GEN) (track names in the .tab records). Set 0 = CGAME1..3 (level
  tracks) + CSETUP (front end), set 1 = CINTRO4..6.
- **Combat music** does not change tracks: `music_update_1f800` ramps controller 7 on MIDI channels
  3..5 of the same song from the far timer callback `music_mood_fade_cb_1f6d0` (function created in
  round 5): 0 -> 0x7e at 60 Hz in steps of 2 for combat (P+0x2e > 0), back at 20 Hz for calm. Globals
  DAT_000938f8 layer level, 938f9 mood, 938fa timer active, 938fb step.

## Renderer, rasteriser, sprites, terrain (remaining round-1/2 details)

- `render_build_roll_table_28580((-roll) & 0x7ff)`, `render_sky_2f080(roll)`: yaw scroll =
  yaw*0x8000>>16 texels, 256 texels span the view width, v = 0 at the horizon; per-pixel u/v deltas are
  applied after sampling. DAT_000b3a10 entry = {delta to previous offset, byte offset, minor steps}.
  Motion blur mode 1 = `BLEND[dest<<8|prev]`, other values `BLEND[prev<<8|dest]`. With
  opt_second_surface == 0 everything draws with mode 5.
- Rasteriser: half-open fill rule; a negative top row is pre-stepped, not rejected; inner jump tables
  0x74A05 / 0x74DA0 are Duff's-device entries (`count & 15`) with the offset table at 0x748D5.
- Sprites: tmap header byte 0 = flags (bit0 animated, bit3 drawn since the last texture_anim_update),
  byte 1 = draw type; pixel mode 8 darkens the destination (shadow), 9 solid colour; DAT_000b58ae =
  "upright" flag. Animation record (0x1c bytes): +0 active, +4 handle, +8 next-frame offset, +0xe first
  frame offset (w*h + 6), +0x10 frame count, +0x12 w, +0x14 h, +0x16 next frame (1-based), +0x1a sprite.
  `fli_decode_ss2_50f71`: a fill count of 0 means 256 words. GameState+0x2c (texture_needed): 0 not
  loaded, 1 loaded, 2 loaded and resident; residents = SpriteDesc load_priority 0xff (DAT_000987e8 set by
  mem_init_pools_59500). `sprite_mark_needed_for_model_4bee0`: 17-word records {class, model, 15 indices},
  last match wins. `render_sprite_scaled_2ad60` quirks: odd octants drop one edge row / column; the
  clip code reads below the step list for hidden lines; the column table gets one write too many;
  upright column record 0 gets delta 0x16.
- Terrain: the river candidate counter (1000) is shared by all rivers [*Corrected (round 6 merge): it is reset for every river (`mov ecx, 0x3e8` inside the river loop at 0x31461); running out ends the whole pass (port_reference2.md, level 44).*], cell = rng16 % 0xffff, source
  needs height > sourc (strict) and class != 0. `terrain_generate_313a0` normalises with
  `scale = 0xc40000 / max` (the min is unused). `terrain_fractal_fill_71f08(seed, off, raise, gnarl)`:
  8 levels 128..1, square then diamond pass per level, one LCG step per midpoint, only where the scratch
  is 0; private LCG copy. `terrain_fix_shore_quads_30810` classifies only (x+1,y), (x+1,y+1), (x,y+1).
  `terrain_mark_steep_31ad0` second pass: with a class-3 neighbour any 2/5/4 -> class 1, else a 2 or
  both 5 and 4. Symmetry codes of `terrain_assign_textures_30eb0`: (a,b,c,d) 0x00, (b,a,d,c) 0x10,
  (c,d,a,b) 0x30, (d,c,b,a) 0x20, (b,c,d,a) 0x60, (c,b,a,d) 0x70, (d,a,b,c) 0x50, (a,d,c,b) 0x40;
  pick = rng16 % (n+1). mark_lowland / interior / steep copy the flags into g_map_type as scratch.
  `terrain_sample_height_71e00` interpolates over the triangle of the quad (diagonal by cell parity),
  not bilinearly.

# Port round 5 corrections (merged in round 6, 2026-10-07)

This section merges the "Corrections" sections of the round-5 port reports
(`docs/analysis/port_{audio,frontend,fli,game,reference2,render_reference}.md`) and the naming / typing work of
round 6 (`docs/analysis/names_round6.md`). **Where an older section of this file disagrees, this section wins**
(it also supersedes the round-5 merge above where they disagree: the river counter). The older statements are
kept and marked "*Corrected (round 6 merge)*" or "*Answered (round 6)*" in place. The reports hold the evidence.

Round 6 renamed `level_finish_3d4e0` -> `level_restart_3d4e0`, `fli_error_unknown_frame_50520` ->
`fli_read_frame_50520`, `movie_frame_present_50600` -> `fli_decode_present_frame_50600`, `movie_wait_frame_50430` ->
`fli_wait_frame_50430`, `mem_alloc_named_5ae80` -> `file_load_resource_list_5ae80`, `movie_buffer_clear_236d0` /
`movie_free_23700` -> `movie_subtitle_clear_236d0` / `movie_subtitle_free_23700`, `stereo_page_blank_50370` ->
`vfx1_vip_stereo_enter_50370`, `fli_file_skip_chunk_5c56d` -> `fli_file_read_color256_5c56d`, and created
`snd_midi_song_timer_cb_66898` plus three HMI lock markers (0x66887, 0x66dc0, 0x66dd1). Older sections still use the
old names.

## Game main loop, status bits, campaign (port_game.md)

`game_main_32a00` (0x32a00..0x32dac; `rec` = `players[local]`, `quit` = PlayerRec+4, `status` = PlayerRec+2):

```
config_parse; data_load_all; if (cfg.flags & 8) { ret_stub_23766; goto shutdown }
cfg.fade_stage = 0; cd_check; sprite sizes; mem_init_pools; video_input_init
if (!(cfg+1 & 1)) fe_init_state_51ed0  else { sound/music off; load ftext.dat }     (cfg+1 bit0: front end skipped)
rec.quit = 0
while (rec.quit == 0) {                                   // one front-end visit + one level
    fade(black, 0x10)
    if (!(cfg+1 & 1)) {
        mapmode_palette_save; was_hires = (video == 8); if (was_hires) video_toggle_resolution   // the FE is always 320x200
        mem_check_lowmem; mem_set_owner_tag(2)
        sound_on = sound_available; DAT_0012ebdc = 1; music_on = music_available; DAT_0009e504 = 0
        do frontend_menu_loop_52070 while (!DAT_0009e504)
        level_skip_number_329c0
        fade(black); mouse_cursor_set_sprite(pointers[0])
        if (rec.quit == 0) title_screen_show_32db0        // smatitle.dat loading screen, GameState+0x245 = 1
        video_alloc_buffers; free resource lists x2; mem_grow_check
        if (rec.quit == 0) { sound_load_bank(0); music_load_bank(0) }
        if (!(cfg.flags & 4)) { mapmode_palette_save; if (was_hires) video_toggle_resolution; mapmode_palette_restore }
        mem_set_owner_tag(3)
    }
    if (rec.quit == 0) {
        si = cfg.level                                    // 16-bit music seed
        level_load_and_init_3d3b0
        if (video == 1 && !g_frame2) g_frame2 = alloc(64000)
    }
    while (rec.quit == 0) {                               // one pass per (re)start of the level
        if (music_available && music_on && track_count) {
            si = si * 0x24a1 + 0x24df                     // 16-bit
            state+0x240 = (uint32)(int32)(int16)si % 3 + 1; music_play_track(state+0x240)
        }
        input_mouse_center_4a000
        game_tick_32f90:  cfg.fade_stage = 0; rec.status = 0
                          while (rec.quit == 0 && !(rec.status & 8)) game_tick_update_32e80()
        sound_stop_all; music_stop; fade(black)
        if ((rec.status & 6) != 4) {
            if (rec.status & 2) { rec.status = 2; player_compute_level_stats_3ef10; cfg.level++ }
            else rec.status = 8
            break
        }
        level_restart_3d4e0                               // the same level is regenerated in place
        rec.status = 4
    }
    fade(black)
}
sound_timer_shutdown; fade(black); game_shutdown_all; net_shutdown
```

- **Status values** (PlayerRec+2): 2 = won (`game_check_level_won_3db20`: the 17th consecutive tick above
  `level.win_percent`; Shift+C) - shows "World restored. Press the space bar to continue.", the level does not end
  by itself; 8 = leave the tick loop (command 0x1b Space / Esc when won -> 10, 0x1c -> 0xc, 0x1d leave game -> 8,
  respawn command 0xf without a castle |= 0xc, Shift+R |= 0xc, movie end = 8); 4 = lost (with 8, the same writers as
  above; Shift+F). After the loop: `status & 6 == 4` -> restart in place; bit 2 -> won (status 2, stats, Config.level +
  1); else status 8 (lost / left; the result screen plays levelose).
- **`level_restart_3d4e0`** (was `level_finish_3d4e0`) regenerates Config.level in place like
  level_load_and_init_3d3b0 (no progress messages, player_count unconditional). Its old comment
  ("level-complete state") was wrong.
- **PlayerRec.quit** (+4): any nonzero value ends the whole program loop (1 = quit: Shift+Q, the front end's quit,
  command 2; 2 = CD check failed). It is not "quit to menu".
- **Campaign**: Config.level is the levels.dat index; "new game" sets 0 and clears `players[local].blk.spell_found`
  (PlayerRec+0x7cb = P+0x37c = GameState+0x3bd6 for player 0, the save game's "campaign progress"); +1 per win
  (after the loop, before the front end); the result screen shows `level_names[Config.level]` after that increment;
  Config.level == 50 -> outro. Levels 8, 17, 28, 33, 39 are skipped only on the front-end path (level_skip_number
  runs after frontend_menu_loop; the path without front end plays Config.level as it is). DAT_0012ed30 /
  DAT_0012ed31 belong to the multiplayer lobby (ed31 = player count, ed30 cycles 0..9) and only appear in the save
  game's checksum.
- Before every front-end visit sound_on / music_on are reset to "available" (F1 / F2 do not survive a level); the
  front end always runs in 320x200 (a 640x480 game is switched down and back up unless a movie plays); sound bank 0 and
  music bank 0 are reloaded after every visit.
- `title_flag_a` (GameState+0x245) = loading screen shown: set by title_screen_show_32db0 (data/smatitle.dat /
  smatitle.pal, the latter into the game palette DAT_000adf90), cleared by palette_fade_out_and_load_32e40 at fade
  stage 0.
- **`level_load_file_3d160` resets** GameState+0x244 and Config +0x17 (fade stage), +0x5d[0x10], +0x96 (substeps),
  +0x98 (palette effect), +0xb8[0xe], +0x8e1a[4], the per-tick list heads +0x8e1e..+0x8e7d, flags 0x4000 / 0x8000
  (`&= 0xfffe3fff` on the dword) and the pause bit.
- **`video_toggle_resolution_33600`**: DAT_0012edae 1 <-> 8; g_frame2 (DAT_000adf70) reallocated (64000 / 0x4b000);
  DAT_00093f74 = 0 through stereo_mode_leave_2ff10 (= the port's `video_restore_mode_2ff10`); mouse reset + re-init
  (pointer recentred, range by mode); Config.fade_stage = 0.
- **Config+0x99 frame time is a u32** (timer ticks per frame, shown by the debug overlay); +0x9d is the network time
  (u32). Both are typed in carpet_types.txt now.
- **Pacing: there is none.** One simulation tick per rendered frame, as fast as the machine renders.
  `game_tick_update_32e80` = palette_effect_update -> texture_anim_update -> player_local_input ->
  player_commands_process -> win check -> thing_update_all x 1 / 4 / 16 -> sound_update -> render_frame_1fab0 ->
  frame time -> debug overlay -> screenshot -> `vga_present_frame_2f480` (copy to VRAM, **no vsync**).
  `vga_wait_vsync_65b10` is called only by the FLI presenter, the joystick calibration and `vga_palette_fade_61510`,
  so the only in-level waits are palette fades (17 vsyncs for a 0x10-step fade; the 4-tick fade-in of
  palette_effect_update effect 1 after every flash). The tick counter DAT_0012eab4 is read in-level only by the
  frame-time display, the network time, PlayerBlock.start_tick and the level statistics. **F3 "game speed"**
  (Config.substeps 0 / 1 / 2) runs thing_update_all 1 / 4 / 16 times per frame (a fast-forward, not a limiter;
  player_mouse_steer_15590 returns at once while substeps != 0).

## Front end (port_frontend.md)

- The marker file `fe_init_state_51ed0` checks is `c:\carpet.cd\intro.pld` (string 0x93148): present -> DAT_0012ed35
  bit 1 ("intro already seen", the intro becomes skippable); missing -> an empty marker is written. `language.inf`
  belongs to fe_screen_language_56940 (the language screen is skipped when it exists).
- DAT_0009e500 is 1 in the data and never set again (only three writes of 0: start level, resume, load): "resume" is
  the very first start (menu item 1 starts Config.level at once); after that item 1 asks "New Game? Yes/No".
- 0x12ee21 is `g_key_down[1]` (**Esc**), 0x12ee3c is `g_key_down[0x1c]` (**Enter**). Esc on the config screen
  confirms and skips the logos; in the dialogs Esc is No and Enter is Yes.
- Main-menu item 2 is `fe_menu_text_dialog_57580`: the scroll animation, then "Enter your name:" (Config+0x1d, 30
  chars) and "Enter your call-name:" (Config+0x3d, 8 chars, filtered keys).
- The level-result stats are P-block fields of the local player's Thing: +0x167 creatures killed %, +0x16f accuracy,
  +0x16b spells found, +0x173 mana, +0x177 overall (all "% 3d %%"), +0x17b time ("%dh% 02dm %02ds"). The result
  screen reads `players[local].status`: 8 skips it, 2 plays levelw1 / levelw2 (by the parity of DAT_0012eab4), 4
  plays levelose.
- The attract "demo level" (phase 2) sets Config.flags |= 0x24 (play movie, recording blocked), Config.movie = 0,
  Config.credits_state = {3, 200} (wait 200 frames, then scroll the credits), and saves the level and the 0x18 spell
  bytes in DAT_0012ed18 / DAT_0012ebc0 (restored by the main-menu init).
- `fli_play_508f0`'s arguments are (abort_on_key -> DAT_0012eabe, apply_palette -> DAT_0009e44c, cue). The
  memory-stream decoders skip value-0 pixels only when DAT_0009e468 == 1 (set by the main-menu init).
- `vga_draw_sprite_spans_6070d` positions are relative to the sprite window (DAT_0012ed88 / ed98); the dialogs set the
  window to (0x41, 0x4b, 0xbd, 0x2c) for their title sprites. `ui_font_init_589d0` takes the palette as its third
  argument.
- The original SS2 decoder (0x50f71) lets a "last byte" word fall through and use the same word as the packet count -
  a bug the shipped files never trigger.
- The level-name table 0x97490 has 101 entries: 0 empty, 1..50 the campaign ("1. Al Jahan".."50. Volcania"), 51..70
  twenty multiplayer maps ("Bussorah" .. "Comari"), 71..100 all "0" (round 6, from the image).

## FLI player, cue scripts, subtitles, palette (port_fli.md)

- **Call chain.** `fli_play_508f0(abort_on_input, apply_palette, cue_script)` plays the file whose path is in
  DAT_0009e708 (sprintf at the call site): per frame `cue_script_step_17d80`, `fli_read_frame_50520` (reads the 16-byte
  frame header into DAT_0012e680 and the body; the "ERROR UNKNOWN FRAME TYPE" is only its bad-magic path) and
  `fli_decode_present_frame_50600` (decodes types 4, 7, 11, 12, 13, 15, 16, 18 - COLOR256 is applied -, calls the
  callback DAT_0012ea9c, waits in `fli_wait_frame_50430`, applies the palette when DAT_0009e44c, blits with +0x1a40
  when the subtitle strip is on). `flic_play_chunk_50dfd` / `flic_decode_frame_50e88` are the separate memory-stream
  player (types 7 and 15 only, 4 skipped): texture animations, main-menu globe / timer, title-02, scroll.dat.
- **Which file, which arguments**: intel.dat (Pentium only, cue 0x51734, abort 1), logo.dat (0x51b70, 1),
  title-01.dat (0x51ab8, 1; DAT_0009e44e = 1 when DAT_0012ed36 & 2), intro.dat (0x5174c, abort = DAT_0012ed35 & 2),
  levelw1 / levelw2 / levelose.dat (0x51b28 / 0x51710, 1), outro.dat (0x51b88, 0), scroll.dat for the dialogs (0x516cc,
  0, palette 0, callback `fe_fli_composite_bg_579c0`). All with apply_palette 1 except scroll.
- **Bullfrog FLIC variant**: a 12-byte header {u32 12, u16 0xAF12, u16 frames, u16 w, u16 h}; `fli_play` loops
  `while (frame < frames - 1)`, so the last frame and the ring frame are never shown. COLOR256 carries 6-bit values.
- **Pacing**: `fli_wait_frame_50430` waits until DAT_0012eab4 >= DAT_0009e704 (unsigned) and then resets the counter
  to 0, between decode and blit. DAT_0009e704 (initial 5) is set only by cue op `A` and is never reset per movie. The
  counter is incremented by the HMI timer event (`hmi_timer_add_event_5d093(0x78, timer_tick_isr_34120)` = **120 Hz**)
  whenever sound or music is available, otherwise by the int 8 handler at 119.06 Hz. The order is cue(n), read, decode,
  callback, wait, palette, blit. Abort: key / click with arg 1; any input change with DAT_0009e44e; the pending frame is
  still shown.
- **Cue scripts**: 7-byte records {u16 frame, char op, i16 arg, u16 unused}, index DAT_000938f4, no terminator (the
  look-ahead reads the next code bytes). Ops (jump table 0x17c90, lower case = alias for ABELMRSTXZ only): A frame
  delay; B music_stop + music_load_bank (music1-* = front-end songs); E sound_stop_all + sound_load_bank (speech banks
  1..13 replace the game bank, which game_main reloads); K clear subtitle strip; L (dead preload) then as M; M
  music_play_track, loop off; O subtitles on (if DAT_000938fc); P subtitles off; Q subtitle = text entry arg; R loop a
  sample; S play sample loud (0 = stop all); T stop sample (0 = stop all); X music_stop, loop off; Z music_play_track
  with loop (DAT_0012eaba = track + 1; restarted when music_song_done). The export comment "K/L play music" was wrong.
- **Subtitles**: DAT_000938fc is set by `sound_initialise_34140` when the language is not English, or English without
  digital sound. Op O loads data\screens\sfont1 (resource list 0x1f9f0), picks the nearest white, places the strip at
  back buffer + 0xe100 (row 180, 0x4b00 bytes) and shifts the blit by 0x1a40 (21 rows; DAT_000938fd).
- **This package's movies are placeholders**: intel / intro / levelw1 / levelw2 / levelose / outro.dat are
  byte-identical (the 41-frame Intel animation). `fli_file_play_5c264` has **no reference anywhere in the image**
  (rel32 and dword scan), and the preload branch of cue op L depends on DAT_0009e844, which only that dead player fills.
- **Palette**: `vga_palette_fade_61510(target, steps, incremental)` sets `start + (int16)(target - start) * k / steps`
  for k = 0..steps (steps + 1 frames, k = 0 repeats the start); the incremental mode (third argument 1, only
  palette_effect_update_33010) advances one k per call and clears DAT_0009e860 at the end. `palette_effect_update_33010`
  by Config.fade_stage: 0 / 1 fade to black and load data/palette.dat, 2 effect 1, 3 the effect; effects 2 red, 3
  magenta, 4 blue, 5 dark, 6 bright, 7 grey; **effects 5 and 6 compute red from the blue component** (`mov dl,
  [edx+eax+2]`). Measured in the original (port_render_reference.md): the level fade-in is black, then `palette * k / 4`
  for k = 1, 2, 3, then full; a red flash holds red = 63, then `63 - ((63 - r) * j) / 4` for j = 1..3.
- **Globals** (labels in carpet_types.txt since round 6, address suffix kept): DAT_0009e44c apply palette, 0012eabe
  abort on key, 0009e44e abort on input change, 0012eabc aborted, 0012eac0 frame counter, 0012eaa0.. header (w
  0012eaa8, h 0012eaaa), 0012e680 frame chunk header, 0012e798 FLI palette, 0012e698 debug chunk name, 0009e450 file
  position, 000938f4 cue index, 0009e704 frame delay, 0012eaba Z loop track + 1, 000938fc subtitles enabled, 000938fd
  strip active, 0009390a subtitle initialised, 0009390c current subtitle text, 000adfd0 subtitle font (colour1 at
  000adfd6), 000b2e04 / 000b2e0c strip pointer / length.

## Music sequencer (port_audio.md)

- Bank suffix (already merged in round 5, now settled by the device ids): `music<set>-0` = OPL2 (*.HMP, device
  0xa002, also loads inst.bnk / drum.bnk), `-1` = MT-32 (*.ROL, 0xa004), `-2` = General MIDI (*.GEN, 0xa001 MPU-401
  and 0xa008 AWE32). `music1-*` holds CINTRO4..6.
- The HMP file layout is in FORMATS.md.
- The song timer callback at 0x66898 is now a function, `snd_midi_song_timer_cb_66898` (far, `retf`; pushed by
  `snd_midi_start_song_5eab0` at 0x5ead3 with the rate from header +0x38, 120 Hz in every file). Per track a counter;
  an event fires when delta <= counter, then the counter is cleared and delta-0 events follow in the same tick. FF 51
  (tempo) is ignored. Songs do not loop: at the end every playing track's channel is reset (7B, 79, E0 40 40, CC7 =
  0), the timer removed and the song rewound; `music_update_1f800` restarts it.
- `music_play_track_5c0a0` runs with channel mapping off (`snd_midi_set_flag_5e4ee(0)`); `snd_midi_set_volume_5ef56`
  then writes CC7 = master * 0x7f >> 7 on all 16 channels; `hmi_midi_send_event_5d69b` scales every CC7 by the master
  `v * 0xa2565 >> 7`; the combat layer (`music_mood_fade_cb_1f6d0`) sends raw CC7 on channels 3..5 through
  `hmi_midi_send_event_5e4b8`, without master scaling.
- Tables: 0x9fa0e / 0x9fa1e = the HMI event-length tables (`g_hmi_event_len`, `g_hmi_event_len_fx`); 0x9e65c = the
  track map (16 x 0xff, then 0; `g_music_track_map`).

## Per-tick references of campaign levels (port_reference2.md)

- **`terrain_carve_rivers_31430`**: at most 999 candidate cells **per river** (the 1000-try counter is reset inside
  the river loop at 0x31461); running out ends the whole pass. This supersedes the round-5 merge's "shared by all
  rivers" (it mattered only on level 44).
- **Level-start randomness: there is none.** `terrain_build_303f0` seeds g_rng16 and GameState.rng from the level
  header; the port reproduces the original's tick-1 state of all 69 levels byte for byte.
- `game_check_level_won_3db20` returns at once when `Config.flags & 0x110` (network 0x10, front end skipped / custom /
  `-roll` 0x100) - not "movies": the attract movies run with flags 0x24 and do check the win.
- `Config.credits_state` (+0xa1..+0xa7) is the credits scroller of `render_frame_1fab0` (state 3 waits +0xa2 frames,
  2 resets, 1 scrolls); the attract demo level is movie playback with the credits, and the recorder starts the same
  scroller (`cfg.flags |= 0x24` = play with recording blocked).
- `demo_relink_state_pointers_3dc10` rebases every Thing.desc relative to player 0's Thing: a snapshot taken before
  player 0's Thing exists cannot be played back (divide by zero in the townie update, 0x1e2fc).
- `-level 17` without the front end crashes the retail exe at tick 1-2 in `ui_draw_status_bars_219f0` (0x22328,
  `health * 64 / max_health` with max_health 0).
- `PlayerBlock.hit_flash` (P+0x188) differs between recording and playback of the same game (render side); the only
  such byte seen on 8 levels.

## Renderer pixel reference (port_render_reference.md)

- **Colour cube** DAT_000acc18 `[r << 8 | g << 4 | b]` = nearest palette index to the 6-bit colour `(r*4+3, g*4+3,
  b*4+3)` (`shl al, 2; add al, 3` at 0x334f3 / 0x3350e / 0x3352c). Black is palette entry 12.
- **Radar blips** (`ui_draw_radar_blips_42a20`): owned creatures in the owner's **B** colour `0x97631 + player * 2`;
  mana balls `0x97630 + player * 2 + Config+0x60`; projectiles / other effects the A colour; wild creatures cube black,
  types 12..14 cube blue.
- `render_view_2f6e0`'s SIRDS path uses g_rng16 (DAT_0012dfb0), the terrain generator / retexturing RNG.
- **g_rng16 at a movie start**: it is not saved in the GameState or the map dump; it holds the value left by
  generating the movie's level (build_lightmap resets it to 0 and then draws) - 0x2fea for level 38 (movie 0).
- GameState+0x2195..+0x21b8 during movie playback are the player's settings, not the snapshot's (demo_load_state_3c200
  keeps them); with Config.pentium == 0 (DOSBox) textured sky and second surface are off.
- The sprite animation table at [DAT_000adf50] is {u16 count = 0x211, u32 records}, records 0x1c bytes (+4 handle != 0
  = in use, +0x16 next frame, +0x1a sprite id).
- Confirmed pixel-exactly: `render_frame_1fab0` runs once per game_tick_update after the simulation, and the frame of
  tick N is drawn from the state dumped before it.

## Round 6: data tables typed, open questions answered

New in `ghidra/names/carpet_types.txt` (applied, visible in the export):

- **Spiral ring tables**: `g_spiral_rings` 0xade28 = `RingRec[32]` {SpiralCell *cells, u16 count} - **6-byte**
  records, not 12 (`imul esi, esi, 6` at 0x10203); `SpiralCell` = {i8 dx, i8 dy, u8 ring, pad}, pool in the
  *SearchD buffer DAT_000adf60, built from data/search.dat (string 0x90004) by `spiral_search_init_101b0`.
  `g_spiral_iters` 0xac160 = `SpiralIter[100]` {start, end, ring (-1 = free), idx, rec, cell}, slots 1..99 handed out
  by spiral_search_begin_10080 and freed by _end_10100.
- **Level names**: `g_level_names` 0x97490 = `char*[101]` (see Front end above).
- **Texture directory and sprite cache**: `g_tmaps_dir` 0xb84f4 = `TmapDirEntry*` (the loaded tmaps.tab: {u32
  unpacked size, u32 offset, u16 group}); `g_sprite_group_stamp` 0xb7cb0 u32[529] (LRU), `g_sprite_ptr` 0xb8d3c,
  `g_sprite_locked` 0xb9580, `g_sprite_group_priority` 0xb9791 (u8[529] each; the four arrays are back to back).
- **PlayerRec** synced with `src/mcengine/mc_types.h`: quit (+4), index (+7), is_computer (+9), view_entry (+0xe),
  log_count (+0x10), tick u32 (+0x12), cheat u32 (+0x18), `PlayerMsg messages[8]` (+0x1c), `PosLogEntry log_template`
  (+0x23c) and the 32-entry camera log `PosLogEntry log[32]` (+0x24a) {x, y, z, yaw, pitch, roll, zoom}, the P block
  as `u8 p[0x3b2]` (+0x44f). (game_main_32a00 still decompiles through the ExportAll fallback.)
- **GameState**: volcano_thing / volcano_smoke (+0x24 / +0x26), level_music_track (+0x240), start_pos StartPos[8]
  (+0x23d9). **Config**: demo_file (+9), tick_bits[15] (+0x5e, was "level_mana"), session char[0x20], disk_activity
  (+0x95), frame_time u32 (+0x99), net_time u32 (+0x9d); the flags comment now reads 0x10 network, 0x20 recording
  blocked, 0x100 front end skipped.
- **Resource lists**: `ResourceRec` {char name[0x1c], dest*, end*, size, flags} (0x2c bytes), loaded by
  `file_load_resource_list_5ae80` (was `mem_alloc_named_5ae80`): 0x96df0 data (*SearchD, building.dat/.tab,
  font0/1.dat/.tab, tmaps.tab, *PalData, *PalMem, pointers.dat/.tab, palette.dat), 0x97058 / 0x970b0 / 0x97108 /
  0x97160 the four language texts, 0x971b8 block16 / 0x97210 block32 + sky.dat, 0x97294 (320x200: *WScreen 0x11580,
  *BScreen, mspr0-0) and 0x97370 (640x480: *WScreen 0x4b000, *BScreen, hspr0-0). The "HUD sprite tables
  0x97294 / 0x97370" of the round-5 merge are these lists. 0x9744c is the tab relocation list (`TabReloc[5]` {tab,
  tab_end, dat}: pointers, font0, font1, mspr/hspr, building). The destination globals got labels
  (`g_search_dat_adf60`, `g_palette_adf90`, `g_texture_atlas_adf5c`, `g_sky_adf48`, `g_back_buffer_12ed74`, ...).
- **Labels** for the FLI, front-end and sound globals of the round-5 reports (names = the port's, with the address
  suffix: `g_fli_frame_delay_9e704`, `g_fe_leave_9e504`, `g_music_device_12e06e`, ...); full list in
  names_round6.md.

Open questions of the earlier sections, status after round 6 (marked in place):

| question (section) | status |
|---|---|
| who reads PlayerRec+0x24a (round 4 B) | answered: the camera log read by render_frame_1fab0 (round 5), typed now |
| why levels 8, 17, 28, 33, 39 are skipped (round 4 B) | partly: level 17 crashes the retail exe when started directly (a Thing with max_health 0), so at least that one was unfinished; the rest is a design decision not visible in the code |
| height 8 in the dead ring scan 24d70 (round 4 B) | open (no caller) |
| data table 0x96f6c / 0x96f98 (round 4 D) | answered: the `*PalData` / `*PalMem` records of the resource list 0x96df0 |
| the 0x5678 marker of the EMU8000 block 0xa4eb8 (round 4 D) | answered: no reader; its only reference is in the unreferenced `snd_awe32_get_hw_block_704b6` |
| VIP register values 303h = 1 / 0x10, 302h = 2 (round 4 D) | open (no strings; hardware documentation needed) |
| castle_near_thing_11820 an early castle-site rule? (round 4 A) | open (unreferenced) |
| 0x66898 not a function (round 3 H) | answered: `snd_midi_song_timer_cb_66898` |
| who reads P+0x2e (round 3 G) | answered in round 4: player_flyer_move_3fc00 (combat music) |
| what main-menu item 2 edits (round 2 D) | answered: player name and call-name |
| the HMI device-id constants (round 2 D) | answered: 0xa001 MPU-401, 0xa002 OPL2, 0xa004 MT-32, 0xa008 AWE32 (bank file names) |
| 0x57580 / 0x579c0 not functions (front end) | answered: both exist |

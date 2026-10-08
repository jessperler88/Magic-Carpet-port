# Render reference: pixel comparison against the original (task F, port round 5, 2026-10-07)

The original carpet.exe now also dumps what it **drew** (back buffer, DAC palette, Config, sprite
animation table, renderer globals) every tick of movie 0, and `render_reference_test` compares the
port's frames with it pixel by pixel. Result after two HUD fixes: **every dumped frame is
pixel-identical** - 1396 / 1396 frames of the plain movie (every tick 413..1013, every 10th tick to 8960)
and 2734 / 2734 frames of a second run with the flight HUD forced on (every tick 413..2500, every 10th
to 8960): sky, terrain, things, reflections / shadows, the spell book, the map screen and the flight HUD
(radar, blips, hand labels, status panels, notices). Both are ctest gates now.

One divergence that was not a renderer bug: the terrain RNG `g_rng16` (DAT_0012dfb0) is in neither the
GameState nor the map dump, and the reference run generated level 38 before the snapshot replaced it.
Without that the port's map flags (texture rotation bits) drift from tick 800 on and 300 frames differ
in the terrain; the fix belongs in the movie start path (requested below).

## Files

| file | what |
|---|---|
| `tools/reference/fb/make_refgame.py` | copy of `tools/reference/make_refgame.py`: `MagicCarpet/magic` + DOSBox -> `extracted/refgame_fb/`, then patches |
| `tools/reference/fb/patch_carpet.py` | copy of `patch_carpet.py`: the four round-4 patches unchanged (state cave A, `-roll`, input, CD) + **cave B** (frame dump) + optional `--hud` |
| `tools/reference/fb/run_reference.py` | copy of `run_reference.py`: runs `extracted/refgame_fb/`, collects `gam` / `map` / `fb` dumps into `extracted/reference/movie0_fb/` (`tick%05d.fb`) |
| `tools/reference/fb/fb_info.py` | new: `--ppm` (frame through its DAC palette), `--same-run DIR` (state dumps byte-identical to another set?), `--summary` |
| `src/tests/render_reference_test.cpp` + `.cmake` | `mc_unit_test` with `${MC_SIM_ALL}` + engine / tables / render_landscape / render_things / sprite_cache / raster / hud / ui_draw; two ctest entries: `render_reference_test` (movie0_fb) and `render_reference_hud` (movie0_fbhud, `MC_RFB_HUD=1`) |
| `extracted/reference/movie0_fb/` | 1396 `tick%05d.fb` (80142 bytes each) + the run's own 1396 `.gam` / 85 `.map`, 454 MB |
| `extracted/reference/movie0_fbhud/` | HUD run: 2734 `.fb` + 2734 `.gam` + 85 `.map`, 857 MB |
| renderer fixes | `ui_draw.cpp` (colour cube), `hud.cpp` (creature blip colour), `render_landscape.cpp` (two test accessors) |

The originals in `tools/reference/` were not touched (task E edits them).

## The frame dump (cave B)

`game_tick_update_32e80` ends with `render_frame_1fab0`, `ui_draw_debug_overlay_4ad80`, the screenshot
hook and `call vga_present_frame_2f480` at **0x32f84**. That call is redirected to cave B, which, while a
movie plays (Config.flags & 4 and the demo handle Config+9 open) and on the dump policy (default as the
state dumps: every tick <= 1013, then tick % 10 == 0), writes `movie/fb%05d.dat` (tick = local
PlayerRec.tick, the same number as that tick's state dump) with the game's own `sprintf_603bc`,
`file_open_619a0(name, 0x222)`, `file_write_61e20`, `file_close_61a10`, then jumps to vga_present_frame.
Layout:

| offset | size | content |
|---|---|---|
| 0x00000 | 64000 | back buffer `DAT_0012ed74` (320 x 200, pitch 320 - movie 0 runs in mode 1) |
| 0x0fa00 | 768 | DAC palette read through ports 0x3c7 / 0x3c9 (`rep insb`, 6-bit) |
| 0x0fd00 | 256 | Config bytes 0..0xff |
| 0x0fe00 | 6 + n * 0x1c | sprite animation table `[DAT_000adf50]` = {u16 count (0x211), u32 records} + the records |
| ... | 0x60 | data 0x93f40..0x93f9f (sprite mode tables, slope low-pass 0x93f7c / 0x93f80) |
| ... | 0xc0 | data 0xb5800..0xb58bf (renderer camera / fog globals DAT_000b58xx) |
| ... | 12 | data 0x12ed70..0x12ed7b (width 320, buffer pointer, height 200) |

The renderer globals are the values **after** render N, i.e. the state render N + 1 starts from.

Cave B is ~380 bytes and no single dead function has that much fixup-free room, so the patch script's
small assembler places whole instructions into a list of holes and links them with `jmp rel32`: dead
code of `terrain_max_corner_level_10c30` / `terrain_minmax_along_path_10cb0` (no callers),
`creature_attack_fire_homing_desc_19a90` (no reference at all) and `terrain_ring_find_height_ne8_24d70`
(no callers; its jump table 0x24d54 is only used by itself). Holes are cut around the fixup sources
(0x10c8b, 0x19ad0, 0x19b1e, 0x19b3c, 0x19b49, 0x24db9, 0x24df8); the script refuses any fixup inside a
written range and checks that no fixup target or `call` / `jmp` points into the holes. Position
independent like cave A: code delta from `call $+5 / pop`, data delta from the relocated g_cfg operand
at 0x32e95 (run-time &g_cfg - 0xadf74). Chunks: 0x10c8f+127, 0x19a90+63, 0x19ad4+73, 0x19b22+22,
0x19b4d+26, 0x24d70+69 (`patch_carpet.py --verify` lists them).

`--hud` additionally nops the `jne 0x21073` at 0x1fc85 (render_frame_1fab0's `test byte [cfg], 4`
after the health check), so the original draws the flight HUD during the movie. Its HUD state writes
(PlayerMsg ticks, flashes) then happen as in a normal game: that run's state dumps differ from movie0
from tick 1274 on, and the port test applies the same override (hides Config bit 2 from render_frame
only) and compares the state with that run's own dumps.

**Same run as the state reference**: the frame run's 1396 state dumps are byte-identical to
`extracted/reference/movie0` (1396 / 1396) and to the 1367 ticks they share with `movie0_full`
(`fb_info.py --same-run`): DOSBox playback is deterministic and the frames belong to the reference the
port already matches.

Run time: ~25 s of dumping + the 30 s idle kill (56 s / 67 s with the HUD set).

Regenerate:

```
cd "C:/Magic Carpet/tools/reference/fb"
python -I make_refgame.py                       # extracted/refgame_fb + patch (MagicCarpet/ only read)
python -I run_reference.py                      # -> extracted/reference/movie0_fb
python -I patch_carpet.py --hud --every-until 2500 --fb-every-until 2500
python -I run_reference.py --out "C:/Magic Carpet/extracted/reference/movie0_fbhud"
python -I patch_carpet.py                       # back to the plain patch
python -I fb_info.py --same-run "C:/Magic Carpet/extracted/reference/movie0"
cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_F
cmake --build ../build_F --config Debug --target render_reference_test
cd ../build_F && ctest -C Debug -R render_reference
```

## The port side (render_reference_test)

Plays movie 0 as `reference_test` does (sim_init via engine_init, sim_prepare_movie, demo_open,
demo_step), with what game_tick_update_32e80 does around the simulation: `texture_anim_update()` before
it, render_frame after it - `render_frame_draw(fb, local)` for the pixels, then `hud_tick_state(local)`
for its game-state writes (engine_init's `g_hook_frame_state` is cleared so the writes come after the
comparison, as in the original: dump of tick N is before render N). Every tick is rendered (the
original renders every tick; the animation table and the slope low-pass depend on it), frames with a
dump are compared. Setup that mirrors the reference run:

- `ui_draw_set_video_mode()` after `sim_prepare_movie()` (mode 1 -> the mspr0-0 HUD set, as mcport does);
- `sim_load_level(38)` before the movie (the reference run's `-level 38`; leaves g_rng16 = 0x2fea);
- the render options GameState+0x2195..+0x21b8 copied from the run's first state dump: they are the
  running game's settings, which `demo_load_state_3c200` keeps (reference run: `00 01 00 28 01 01 00 00
  00 00` = no second surface, shadows on, **no textured sky**, full view, both HUD parts on, no 3D / blur
  / smoothing; the snapshot file itself carries `01 01 01 28 00 00`). Config.pentium is 0 in DOSBox,
  which is where textured sky / second surface default off and the automatic motion blur stays off.

Per compared tick: differing pixels, first / last differing row, the split by region (port
classification from extra renders of the same tick with the slope state put back: "thing" = pixels that
change when the cell-things hooks are null, "sky" = pixels that change when the textured sky is toggled
or stay 0xff, "hud" = outside the 3D view window or - HUD set - changed by the flight HUD, "terrain" =
the rest), plus: the state dump (Things without links, players, RNG), the terrain dump every 100th tick
(4 maps), the slope low-pass after the render vs the dump, every active animation record (sprite id,
next frame) vs the port's `sprite_anim_info`, the DAC vs `g_palette6`, Config.credits_state.
`MC_RFB_TICKS=t,..` writes `rfb_<tick>.ppm` (reference | port | diff in red) and `.raw` (both index
frames) into argv[4]. Run time 5.4 s (plain) / 8.7 s (HUD set), Debug.

### Carried-over renderer state (what there is, how it is handled)

| state | original | handling | result |
|---|---|---|---|
| slope low-pass of render_view (DAT_00093f7c / 80) | data-segment defaults at the first movie frame (nothing is rendered before) | port starts from the same defaults (`g_slope_smooth_default`); compared after every render (`MC_RFB_SEED=1` seeds from dump 1 instead) | equal after all 1396 / 2734 renders |
| sprite animation records (FLIC frames, advance only for sprites drawn in the previous frame) | table at [DAT_000adf50] | port's `s_anim` via `sprite_anim_info` | 61..65 active records, all equal every tick |
| previous frame in the back buffer (motion-blur blend) | only with Config.pentium != 0 | not used in the reference; the port's frame buffer is kept across ticks anyway | - |
| render_frame's screen-clear memory (`s_last_clear`) | per call | the tick's frame is drawn first, the classification passes after it | - |
| HUD credits roll (Config+0xa1..+0xa7) | 0 during movie 0 | compared | equal (never active: the roll is not covered) |
| renderer RNG | `render_view_2f6e0`'s SIRDS path draws from **g_rng16** (the terrain RNG, line `DAT_0012dfb0 % 7` for the dot-pattern row); no other renderer function uses an RNG | SIRDS is not ported | a SIRDS port must advance g_rng16 - it feeds the terrain retexturing, i.e. game state |
| texture / water animation | water phase = local player tick * 0x40 | `g_anim_tick = tick` after the step (as engine_tick) | - |

## Results per region, before / after each change

Region numbers are "matching pixels / pixels in the region" summed over all compared frames.

### Window 413..1013 (601 frames, every tick), test setup steps

| step | identical frames | sky | terrain | things | HUD | all |
|---|---|---|---|---|---|---|
| port as found, test without the option copy | 0 / 601 | 0.02 % | 84.94 % | 75.75 % | - | 42.52 % |
| + render options from the reference run (textured sky / second surface off) | 538 / 601 | 100 % | 100 % (3D) | 100 % (3D) | book / map frames ~13600 px each | 97.76 % |
| + mspr0-0 HUD set (`ui_draw_set_video_mode`) and frame-first pass order | 538 / 601 | 100 % | 100 % | 100 % | 99.582 % | 99.963 % |
| + the two HUD fixes below | **601 / 601** | 100 % | 100 % | 100 % | 100 % | 100 % |

The first two rows are test-setup findings (the port renders what it is told), not renderer bugs; the
63 differing frames of rows 2..3 are the spell-book / map-screen episodes (PlayerRec.input_mode 2,
ticks 441..452, 480..490, 576..587, 667..683, 849..859).

### Whole movie (1396 frames) and HUD set (2734 frames), final test setup

| renderer | set | identical | sky | terrain | things | HUD | all |
|---|---|---|---|---|---|---|---|
| as found (round 4) | movie0_fb | 1237 / 1396 | 100 % | 100 % | 100 % | 99.670 % | 99.968 % |
| as found (round 4) | movie0_fbhud | 5 / 2734 | 100 % | 100 % | 100 % | 99.606 % | 99.912 % |
| + colour cube fix + creature blip colour fix | movie0_fb | **1396 / 1396** | 100 % | 100 % | 100 % | 100 % | 100 % |
| + colour cube fix + creature blip colour fix | movie0_fbhud | **2734 / 2734** | 100 % | 100 % | 100 % | 100 % | 100 % |
| final renderer, but without `sim_load_level(38)` (g_rng16 not as in the reference run) | movie0_fb | 1096 / 1396 (first 1210) | 100 % | 95.515 % | 99.708 % | 100 % | 98.202 % |

In every row the simulation matched the state dumps on every tick (0 differing ticks); in the last
row the **map flags** differ from tick 800 (82 of 85 terrain dumps; 39 cells at 800, bits 4..6 = the
texture rotation of `retexture_rect`, drawn as `g_rng16 % 7`), which is invisible until the camera
looks at those cells (tick 1210) and then shows as off-by-one shade / rotated texels in the terrain.

## Fixes (renderer files, evidence)

1. **Colour cube** (`ui_draw.cpp`, `ui_colour_cube_build`): data_load_all_334c0 builds
   `DAT_000acc18[r << 8 | g << 4 | b]` with components **`c * 4 + 3`** (`shl al, 2; add al, 3` at
   0x334f3, 0x3350e, 0x3352c; arguments pushed as (pal, r, g, b) at 0x33546..0x3355b), not `c * 4`. So
   "black" (cube[0]) is palette entry 12 (4, 3, 3), not entry 0, and every UI colour taken from the cube
   shifts (text, the book's quick-key digits, wild-creature blips, radar text). Evidence in the frames:
   ref 12 / port 0 on the map blips, ref 13 / port 254 and ref 128 / port 13 on the book digits; all gone
   after the fix.
2. **Creature blip colour** (`hud.cpp`, ui_draw_radar_blips): a creature owned by a player is drawn in
   the owner's **B** colour `DAT_00097631[player_no * 2]` (0x42fa7 `mov al, [eax*2 + 0x97631]`), the port
   used the A colour. (Wild creatures: cube black, types 12..14 cube blue - unchanged.) Evidence: player
   marker pixels ref 90 / port 89 in the map screen.
3. `render_landscape.cpp`: `render_get_slope_state` / `render_set_slope_state` (port-only accessors of
   the static slope low-pass, declared in the test; no behaviour change).

`reference_test` (movie 0, build_F, after the fixes): `OK: 0 divergence(s) from the original over 1393
ticks`. `render_test`, `sprites_test` pass; `mcport` builds. `hud_test` fails 4 checks that encode the
old cube (see requested changes) - its images are unchanged apart from the cube colours.

## Palette (for task C)

The DAC equals `data/palette.dat` in 1359 of 1396 frames (2617 of 2734 in the HUD set). The rest:
- **fade-in** at the movie start: ticks 413..416 all black, 417 / 418 / 419 = `palette * k / 4`
  (k = 1, 2, 3, truncated: entry 255 (43, 46, 63) -> (10, 11, 15), (21, 23, 31), (32, 34, 47)), full from 420;
- **red flash** (e.g. 1271..1278, 1382..1389, 1905..1914): red channel = 63 for the hold ticks, then
  `63 - ((63 - r) * j) / 4` for j = 1, 2, 3 (entry 100 r = 18: 52, 41, 30; entry 255 r = 43: 58, 53, 48),
  green / blue untouched. These are the palette_effect_update_33010 / fader numbers palette_fx.cpp
  should reproduce; `tick%05d.fb` + 0xfa00 has the exact 768 bytes per tick.

## Deviations / gaps

- Only 320x200 is covered (movie 0 is a mode-1 recording; a 640x480 reference would need a different
  recording because the castle capacity depends on the mode).
- Not exercised by the reference: textured sky, second surface (reflections), motion blur, 2x2
  smoothing, reduced view sizes, the help screen, the credits roll, anaglyph / SIRDS (not ported), the
  front end. The Config.pentium == 0 run turns textured sky / second surface off; a run with them on
  would need `-detail`-style options or patching the option bytes (not done).
- Frames past 2500 (HUD set) / 1013 (plain) are every 10th tick only; the in-between ticks are rendered
  by the port but not compared.
- The region split is the port's classification; a pixel the original drew as a thing but the port as
  terrain counts as "terrain" (moot now that all frames are identical).

## Requested shared-file changes

1. **Movie start: g_rng16 as in the original** (not mine: `sim_all.cpp` / `mcport/main.cpp` /
   `tests/reference_test.cpp`). The original generates the movie's level before the first playback tick
   loads the snapshot, and g_rng16 keeps the generator's value (0x2fea for level 38). The port's movie
   paths load the snapshot without it, so in-game retexturing draws a different sequence (map flag
   rotation bits from tick 800 on; `reference_test` shows them as "maps: N cells differ").
   - `src/tests/reference_test.cpp`, after `sim_prepare_movie();`:
     ```cpp
     sim_load_level(38);   // as `carpet -roll 1 -level 38`: leaves g_rng16 = 0x2fea for the movie's retexturing
     ```
   - `src/mcport/main.cpp`, demo branch, after `sim_prepare_movie();` and before `demo_open(...)`:
     ```cpp
     engine_load_level(38);   // movie 0 is level 38; the original generates it before the snapshot loads
     ```
     (In the retail attract mode the level generated before the movie is whatever game_main loads for
     Config.level at that point; for movie 0 the reference run used 38.)
2. `src/mcengine/render.h` (after `render_view`):
   ```cpp
   // Port: the carried-over slope low-pass of render_view (DAT_00093f7c / 80), for tests.
   void render_get_slope_state(int32_t *x, int32_t *y);
   void render_set_slope_state(int32_t x, int32_t y);
   ```
   (then the two declarations at the top of `render_reference_test.cpp` can go).
3. `src/tests/hud_test.cpp` lines 103..105 (the cube corners are the nearest entries to `c * 4 + 3`):
   ```cpp
   CHECK(ui_col_black() == palette_find_nearest(g_palette6, 3, 3, 3));
   CHECK(ui_col_white() == palette_find_nearest(g_palette6, 63, 63, 63));
   CHECK(ui_col_red() == palette_find_nearest(g_palette6, 63, 3, 3));
   ```

No hooks declared or installed; no TODO(port) added.

## Corrections for ENGINE.md / port_hud.md

- DAT_000acc18 colour cube: entry `[r << 8 | g << 4 | b]` = nearest palette index to the 6-bit colour
  `(r * 4 + 3, g * 4 + 3, b * 4 + 3)` (port_hud.md said `/ 4` without the +3). Black = entry 12 in
  palette.dat.
- ui_draw_radar_blips_42a20: owned creatures use the owner's B colour (`0x97631 + player_no * 2`);
  mana balls `0x97630 + player_no * 2 + Config+0x60`; projectiles / other effects the A colour.
- render_view_2f6e0's SIRDS path uses g_rng16 (DAT_0012dfb0), the terrain generator / retexturing RNG.
- g_rng16 is not saved in the GameState or the map dump; at a movie start it holds the value left by
  generating the movie's level (build_lightmap resets it to 0 and then draws), 0x2fea for level 38.
- GameState+0x2195..+0x21b8 during movie playback are the player's settings, not the snapshot's
  (demo_load_state_3c200 keeps them); with Config.pentium == 0 (DOSBox) textured sky and second surface
  are off.
- The sprite animation table at [DAT_000adf50] is {u16 count = 0x211, u32 records}, records 0x1c bytes
  (+4 handle != 0 = in use, +0x16 next frame, +0x1a sprite id), as port_sprites.md describes.
- Confirmed pixel-exactly: render_frame_1fab0 runs once per game_tick_update after the simulation, and
  the frame of tick N is drawn from the state dumped before it (the dump point of the state reference).

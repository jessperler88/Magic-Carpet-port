# Render references 2: 640x480, render options, front end (task D, port round 6, 2026-10-07)

## Answer for task B (settled first): the original never shows the front end in 640x480

Evidence (disassembly of the retail carpet.exe):

- `DAT_0012edae` (video mode flags, 1 = 320x200, 8 = 640x480) has exactly three writers:
  `config_parse_33750` (0x34032, always the value 1: the only path that would store 8 is the
  `[esp+0x80]` option at 0x33f51, and that local is only ever initialised to 0 at 0x3378f - dead; it
  would also set Config.flags |= 8, which makes game_main exit before the front end), and
  `video_toggle_resolution_33600` (1 <-> 8).
- `video_toggle_resolution_33600` has three callers: `game_main_32a00` (0x32b06 and the switch back after
  the front end), `player_function_keys_156b0` and `player_local_input_16660` (the in-game R key).
- `frontend_menu_loop_52070` has one caller, `game_main_32a00` 0x32b43, inside the block 0x32aef..0x32b4f
  that first does `mapmode_palette_save_30350; if (DAT_0012edae == 8) { video_toggle_resolution_33600();
  was_hires = 8; }` (0x32af4..0x32b0b). The front end therefore always starts in mode 1, and nothing it
  calls (fe_* / fli_* / text / dialog code) can reach the toggle. The loading screen
  (`title_screen_show_32db0`) also runs before the switch back (0x32b56..).
- `fe_init_state_51ed0` (before the outer loop, mode 1 from config_parse) draws nothing.

So every `DAT_0012edae & 1` / `== 8` branch inside the front-end functions (fe_*, the dialog / slot
drawing, the 640 font paths when reached from the front end) is **dead at run time**; port the 320x200
path only and close the item (`fe_frame` forcing mode 1 is exactly right). The 640x480 code in the
in-game renderer / HUD / book / map is live (R key) and is covered below.

## Summary

Three new sets of ground truth from the original (patched copies in DOSBox), all compared pixel by pixel
with the port and all **pixel-identical** after three fixes:

| reference | content | frames | before | after |
|---|---|---|---|---|
| `movie0_fb640` | movie 0 in **640x480** (flight HUD forced on) with the option schedule below | 3334 (every tick 413..3500, every 10th to 5960) | 2447 identical | **3334 / 3334** |
| `movie0_fbopt` | movie 0 in 320x200 (HUD on) with the option schedule: textured sky, reflections, 2x2 smoothing, motion blur light / heavy, view sizes 32 / 24 / 17, shadows off, either HUD part off, help screen, Config.pentium 1, credits roll, all on together | 3634 (every tick 413..3500, every 10th to 8960) | 3625 identical (after the two HUD fixes) | **3634 / 3634** |
| `fe` | a scripted tour of the **front end**: language (3), config (3), Bullfrog logo, title, main menu with hovers (4), load / save slot lists, name dialog, quit dialog, save dialog + text entry + save, load list + "load?" dialog, a level started, won (chat "RATTY", Shift+C) and left, the **level result screen** with statistics, main menu after a level, "New game?" dialog | 33 dumps | 29 identical (as found: config summary needs sndsetup, "New Game?" colour) | **33 / 33** (one fix emulated for task B, see below) |

The simulation matched the run's own state dumps on every compared tick of both movie runs (incl. 640x480,
where castle footprints and the book layout differ), the render options of every dump equal the port's,
the slope low-pass and the sprite animation records are equal after every render, and the credits state
(Config+0xa1..+0xa7) equals the dump on every frame.

Renderer fixes (hud.cpp): help-screen line spacing, credits roll in the draw pass; test harness: the
pointer the original draws into the back buffer at present (visible through the motion blur), new
`ui_draw_mouse_pointer` in ui_draw.cpp. Front end (task B's frontend.cpp, not edited): one fix requested.

## Files

| file | what |
|---|---|
| `tools/reference/fb/patch_carpet.py` | round 6: cave B writes width * height bytes; `--hires` (config_parse stores mode 8); `--schedule FILE` (poke schedule in cave B, sidecar `carpet.schedule.txt`); `--fe FILE` (cave C); more dead-code holes; checks that no fixup target points into a hole |
| `tools/reference/fb/fe_cave.py` (new) | cave C: scripted input + frame dumps of the front end at every present (layout in its docstring) |
| `tools/reference/fb/fe_script.py` (new) -> `fe_script.json` | the front-end tour (state-gated entries) |
| `tools/reference/fb/schedule_options.json` (new) | the render-option schedule of both movie runs |
| `tools/reference/fb/run_reference.py` | `--fe` (no -roll, `fe%05d.dat` -> `fe%05d.fe`, removes `CARPET.CD/LANGUAGE.INF` and the saves first), copies the sidecars (`schedule.txt`, `fe_script.txt`) |
| `tools/reference/fb/fb_info.py` | parses 640x480 dumps (size from the screen variables) |
| `tools/reference/fb/fe_info.py` (new) | lists front-end dumps (state, mouse, timer), `--png` |
| `src/tests/render_reference2_test.cpp` + `.cmake` (new) | movie runs with any mode / schedule; ctest `render_reference2_test` (640x480 set) and `render_reference_options` (320x200 set) |
| `src/tests/render_reference_fe_test.cpp` + `.cmake` (new) | the front-end tour against `fe_frame`; ctest `render_reference_fe_test` |
| `src/mcengine/hud.cpp` | help-screen spacing; credits roll in the draw pass; `g_hud_force_flight_hud` (port-only) |
| `src/mcengine/ui_draw.cpp` | `ui_draw_mouse_pointer` (mouse_cursor_set_sprite_5ba5c + mouse_cursor_draw_5b35c) |
| `src/mcengine/render.h` | declarations of the two above |
| `extracted/reference/movie0_fb640/` | 3334 `.fb` (308 KB each) + `.gam` + 55 `.map`, `schedule.txt`, 1.8 GB |
| `extracted/reference/movie0_fbopt/` | 3634 `.fb` + `.gam` + 85 `.map`, `schedule.txt`, 1.2 GB |
| `extracted/reference/fe/` | 33 `fe%05d.fe` (133 KB each) + `fe_script.txt`, 4.3 MB |

`extracted/refgame_fb/magic/carpet.exe` is left as the plain patch (`python -I patch_carpet.py`), which
reproduces all 1396 frame dumps of `movie0_fb` byte for byte (checked: a fresh run against the round-5 set,
0 differing files; `render_reference_test` on it 1396 / 1396).

Regenerate:

```
cd "C:/Magic Carpet/tools/reference/fb"
python -I patch_carpet.py --hires --hud --schedule schedule_options.json --every-until 3500 --fb-every-until 3500
python -I run_reference.py --out "C:/Magic Carpet/extracted/reference/movie0_fb640"        # ~95 s
python -I patch_carpet.py --hud --schedule schedule_options.json --every-until 3500 --fb-every-until 3500
python -I run_reference.py --out "C:/Magic Carpet/extracted/reference/movie0_fbopt"        # ~110 s
python -I fe_script.py && python -I patch_carpet.py --fe fe_script.json
python -I run_reference.py --fe --idle 120 --out "C:/Magic Carpet/extracted/reference/fe"  # ~3 min (the intro FLI plays)
python -I patch_carpet.py                                                                   # back to the plain patch
cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_D
cmake --build ../build_D --config Debug --target render_reference2_test render_reference_fe_test
cd ../build_D && ctest -C Debug -R render_reference
```

## 640x480 in game

Movie 0 was recorded in 320x200 and the in-game R key cannot help: during playback (`Config.flags & 4`)
`player_local_input_16660` is not called (game_tick_update_32e80 / player.cpp:1207), so the original never
switches resolution in a movie. Instead `--hires` patches config_parse_33750 at 0x3402b
(`mov eax, [esp+0xa4]` -> `mov eax, 8; nop; nop`): DAT_0012edae = 8 from the start, `-roll` skips the front
end, video_input_init sets VESA 0x101 (DOSBox `machine=svga_s3`), and the movie plays in 640x480 from its
first tick. game_main allocates no g_frame2 in mode 8 (0x32c35), so motion blur is inert there, exactly as in
a game started in 640x480. The castle footprints / capacities and the book layout of mode 8 make the game
diverge from what the recording meant (the run's last dump is tick 5960, then the program exits - cause not
investigated, the 320x200 runs go to 8960), but the
original and the port diverge identically: **every compared state dump equals the port's simulation**, which
also verifies the mode-8 branches of level_features / creatures / effects / input against the original.

Frame dumps of 640x480 are 307200 + 768 + ... bytes; the test takes width / height from the dumped screen
variables (0x12ed70 / 0x12ed78) and uses the hspr0-0 HUD set (`ui_draw_set_video_mode` after setting mode 8).

## Render options, help screen, credits (the schedule)

Cave B applies a **poke schedule** after the render of every playback tick (before the dump policy, so a
poke in place "from tick T" is written after render T - 1 and shows in the state dump of T): 8-byte entries
{tick, offset, kind, value} with kind 0 GameState, 1 Config, 2 the local PlayerRec, spread over dead code
with link entries. The test applies the same bytes at the same point (after `hud_tick_state` of the previous
tick). `schedule_options.json`, used for both runs:

| from tick | poke | what |
|---|---|---|
| 700 / 1000 | GameState+0x2197 = 1 / 0 | textured sky |
| 1000 / 1300 | +0x2195 = 1 / 0 | reflections (second surface) |
| 1300 / 1500 | +0x219d = 1 / 0 | 2x2 smoothing |
| 1500 / 1650 / 1800 | +0x219c = 1 / 2 / 0 | motion blur light / heavy / off |
| 1800 / 1900 / 2000 / 2100 | +0x2198 = 32 / 24 / 17 / 40 | view size |
| 2100 / 2200 | +0x2196 = 0 / 1 | shadows off |
| 2200 / 2300 / 2400 | +0x2199, +0x219a | either HUD part off (radar / icons + status bars) |
| 2420 / 2520 | PlayerRec+0x44a = 1 / 0 | help screen (input mode 1) |
| 2600 / 2900 | Config+8 = 1 / 0 | Config.pentium (automatic blur) |
| 2900 | Config+0xa1 = 3, +0xa2 = 5 | the credits roll (as the attract mode starts it) |
| 3200 / 3400 / 3500 | sky + reflections + smoothing, then blur instead of smoothing, then all off | combined |

The book / map episodes of the movie (input mode 2) fall into most windows, so every option was also
compared under the spell book and the map screen.

### Match per option / mode, before -> after (frames identical / compared)

| option window | 320x200 (`movie0_fbopt`) | 640x480 (`movie0_fb640`) |
|---|---|---|
| defaults (no sky / reflections, shadows on, full view, HUD on) | 387 / 387 | 387 / 387 |
| textured sky | 300 / 300 | 300 / 300 |
| reflections | 300 / 300 | 300 / 300 |
| 2x2 smoothing | 200 / 200 | 200 / 200 |
| motion blur light | 146 -> **150 / 150** | 150 / 150 (inert: no g_frame2 in mode 8) |
| motion blur heavy | 150 / 150 | 150 / 150 (inert) |
| view size 32 / 24 / 17 | 100 / 100 each | 100 / 100 each |
| shadows off | 100 / 100 | 100 / 100 |
| radar off / icons + bars off | 100 / 100 each | 100 / 100 each |
| help screen | 0 -> **100 / 100** (measured in 640x480; 320 after the fix) | 0 -> **100 / 100** |
| Config.pentium 1 | 300 / 300 (the automatic blur never triggers, see gaps) | 300 / 300 |
| credits roll (2900 to the end, all modes; the combined windows below also roll) | **847 / 847** | **547 / 547** (before: 60 of the 847 rolling frames incl. the combined windows) |
| sky + reflections + smoothing / + blur | 200 / 200, 95 -> **100 / 100** | 200 / 200, 100 / 100 |
| **all** | 3625 -> **3634 / 3634** | 2447 -> **3334 / 3334** |

("before" for 640x480 = the renderer as found with the final test harness: the 887 differing frames were the
100 help frames and 787 of the 847 frames with the credits roll; for 320x200 the run was made after the two
HUD fixes, its remaining 9 differing frames were the pointer under the blur.)

## Fixes

1. **Help screen spacing** (`hud.cpp`, `k_help_lines`): render_frame_1fab0's help case (0x204c6..0x20ac8)
   adds **two** extra line heights before "If you are experiencing slowness..." and before "Magic Carpet
   comes to you..." (`call 0x4abd0` twice at 0x2093c / 0x20944 and 0x2099b / 0x209a3), the port had one, so
   the last six lines sat one line too high (2638 pixels per frame in 640x480). The 320x200 and 640x480
   string sets (0x90040.. / 0x90060.., chosen per line by `DAT_0012edae & 1`) have the same text.
2. **Credits roll in the draw pass** (`hud.cpp`, render_frame tail): the state machine (0x210ef..0x212cf:
   state 3 counts down, 2 -> 1 resets the page, 1 draws the page and counts, an empty count skips to the next
   page **without drawing**) runs before the drawing in the same call. The port ran the transitions only in
   the state-writing pass and forced a draw of the current page in the draw-only pass, so the roll showed the
   previous page / nothing on the frames where the original switched. Now every pass runs the machine on
   locals; only the writing pass stores it. (The count loops stop on '!' only in the original; the port's
   extra '#' guard never triggers, the table ends "!" "#".)
3. **Test harness: the original's pointer is part of the back buffer.** vga_copy_320x200_610f0 (and the VESA
   copies) call mouse_cursor_hide_for_blit_5b7f8 -> mouse_cursor_draw_5b35c, which draws the pointer **into
   the back buffer** before the copy; its restore (mouse_cursor_restore_unlock_5b824) has no caller. The next
   frame overwrites it, except through the motion-blur blend: closing the book (pointer entry 1 at the centre)
   with blur on leaves a fading ghost of the pointer for 3-4 frames (81, 72, 40, 3 pixels at ticks 1599..1602
   and 3446..3449). New `ui_draw_mouse_pointer(fb, sprite, mouse_x, mouse_y)` in ui_draw.cpp reproduces it
   (64x64 cursor buffer pre-filled with 0xfe, sprite drawn with ui_draw_sprite at (0, 0), copy of every byte
   != 0xfe to (mouse >> 1 in 320x200), clipped at the right / bottom); render_reference2_test calls it at every
   present with entry 1 in input mode 2 and entry 0 (empty) otherwise. **mcport should present the same way**
   (requested change below).
4. `g_hud_force_flight_hud` (port-only, hud.cpp): the `--hud` override of the reference runs. The round-5 test
   hid Config bit 2 from all of render_frame, which also switches off the credits roll in the tail; the
   switch only bypasses the `test byte [cfg], 4` at 0x1fc82 as the patch does.

`render_reference_test`, `render_reference_hud`, `reference_test`, `reference_levels`, `reference_gen`,
`hud_test`, `render_test` pass unchanged after every change (build_D).

## Front end

### How the reference works (cave C)

Several front-end screens loop inside one frontend_menu_loop_52070 call (language screen, dialogs, text
entry), so the hook is the one place every displayed frame passes: vga_copy_320x200_610f0 right after the
pointer went into the back buffer (`mov edi, 0xa0000` at 0x61104 -> `call cave C`). Per present: write a
requested dump (`movie/fe%05d.dat`: the frame incl. pointer, DAC, the front-end background buffer
[DAT_000adf68], data 0x12ea00..0x12eeff and 0x9e300..0x9e5ff, Config, the screen variables, the local
PlayerRec), then run the script: entries {state, op, wait, a, b} fire in order when DAT_0012ed2e equals the
state and `wait` presents passed; ops move / left / right down-up / key down-up write the same globals the
int 33h / int 9 handlers write (g_mouse_x/y 0x9e5dc, click position 0x9e5d8, held 0x12ee14 / 12, click
0x12ee0e / 0c, g_key_down 0x12ee20, g_key_last 0x12eea0), op 8 requests a dump at the next present. The
cave's variables live in dead code (DOS/4GW's flat data selector writes code pages). DOSBox runs headless
with `machine=svga_s3`, no input; c:\carpet.cd is the copy's `CARPET.CD` (intro.pld, sndsetup.inf/.dat of
the shipped install: Soundblaster 16 / FM), language.inf removed so the language screen shows.

The port side (`render_reference_fe_test`) drives `fe_init(game, 6)` / `fe_frame` with the same script, one
fe_frame per present, and the same environment (save dir with intro.pld and the shipped sndsetup files,
Config.pentium 0 = no Intel logo). Differences in timing between "presents" and port frames are handled
explicitly, not hidden:
- the original's palette fades present nothing: a dump waits until the port's fade is over;
- the main menu's globe / hourglass FLICs advance every other frame (fe_main_menu_animate_53c40), so their
  phase is the number of presents: at a main-menu dump the port searches the next 64 frames for the phase
  once, after which it stays in phase with the original (most later dumps match without a search; dialogs
  freeze the same globe frame);
- the logo / title FLICs and the result screen's reveal are timer-paced: searched with the time running;
- the level started from the menu is not played: its in-game script entries (state 5) are consumed and the
  outcome (local PlayerRec with status and statistics, Config.level) is taken from the dump after it;
- palette differences are reported as "pixels of a different colour" (title: the original already has the
  title palette in the DAC while the first, all-black frame is shown; the port sets it one frame later -
  palette entry 0 is black in both, so nothing visible).

### Results

| dump | screen | as found | after |
|---|---|---|---|
| 1-3 | language: start, German selected, English selected | identical | identical |
| 4-6 | config: summary, input device page 1, page 0 | 610 px (sound summary) when the port has no sndsetup.dat; identical once the test provides the shipped c:\carpet.cd files (frontend.cpp reads them, round 6) | identical |
| 7 | Bullfrog logo (FLIC) | identical (phase +94 frames) | identical |
| 8 | title, first frame | identical (black frame; DAC timing only) | identical |
| 10-18 | main menu, hovers (quit, new game, start level), load slots, slot hover, back, save slots, back | identical (globe phase found once) | identical |
| 19-22 | name dialog, menu, quit dialog, menu | identical | identical |
| 23-29 | save slots, save dialog, text entry, saved list, load list, "load?" dialog, menu | identical | identical |
| 40 | level result screen (pperf, "1. Al Jahan", statistics, time) | identical (reveal searched) | identical |
| 30-31 | main menu after the level, hover start level (enabled) | identical | identical |
| 32 | "New Game? Yes/No" dialog | **329 px: the text is black in the original, grey in the port** | identical with the fix below emulated |
| 33 | menu | identical | identical |

### Requested fix for frontend.cpp (task B)

`fe_main_menu_init_53d30` initialises font1 with `ui_font_init_589d0(0x9e510, sfont1, [DAT_0012ecfc])`
(0x53db6..0x53dc6) **before** mainmenu.pal is loaded into that buffer. DAT_0012ecfc is the `*PALETTE` record
of the resource list 0x510d0 that frontend_menu_loop_52070 reloads (with mem_pool_init_61f80) whenever
DAT_0012ebdc is set - after every level: a fresh allocation. On the first visit the buffer still holds the
title's palette (font1 colours 0xff / 0xf7 in the dumps); after a level it is fresh memory and both colours
come out 0 (dumps 30 / 32: font1 at 0x9e510+6 = 00 00), so "New Game? Yes/No" (fe_draw_centered_text_57530,
font1) is drawn in colour 0 = black. The port keeps `s_pal` across the level. Fix in `fe_frame`
(frontend.cpp, the reload at the top, currently line ~2146):

```cpp
        if (g_fe_reload) {
            fe_load_pointers();
            std::memset(s_pal, 0, sizeof s_pal);   // *PALETTE is re-allocated with the resource list 0x510d0
            g_fe_reload = 0;
        }
```

(The program start also reloads; there the first screen loads its palette before any font is initialised,
so clearing is harmless. The memory content is the allocator's, not guaranteed zero: zero reproduces the
observed colours 0 / 0.) `render_reference_fe_test` emulates this after the level until it is applied
(`MC_RFE_NOFIX=1` shows the port as it is: dump 32 differs by 329 pixels).

Not covered by the tour: the multiplayer lobby (needs a NetBIOS driver: menu item 3 disabled in DOSBox), the
intro / outro FLICs (the intro plays through the asm player fli_file_play_5c264, which does not present
through vga_copy_320x200), the attract mode, the sound-setup wizard, joystick pages.

## Deviations / gaps

- Not ported, not referenced: anaglyph / SIRDS / interlaced stereo (mode_3d, opt_interlaced: render_view's
  other paths and vga_present_frame's colour-table combine), VFX1.
- Motion blur in 640x480: needs g_frame2, which only video_toggle_resolution_33600 allocates in mode 8 (R key in
  a 640 game); not reachable in a movie, not referenced. The automatic blur (Config.pentium, |speed| > 0x50 in
  render_view_2f6e0 at 0x2fcde) did not trigger: the flyer's target speed is clamped to +-0x50, so it needs a
  speed boost; the Config.pentium window is identical but does not exercise it.
- Reflections are compared on whatever water the camera sees in ticks 1000..1299 / 3200..3499 of movie 0.
- The level result screen of a lost level, the multiplayer screens and FLIC intro / outro are not in the tour.
- Build: with `${MC_SIM_ALL}` the tests compile task B's `net.cpp`, which gave two C4996 (`strncpy`)
  warnings during the round (not my file); my sources build with zero warnings.

## Hooks, extra functions, TODO(port)

- Extra functions: `ui_draw_mouse_pointer` (ui_draw.cpp, render.h), `g_hud_force_flight_hud` (hud.cpp,
  render.h). No hooks installed, no TODO(port) added.

## Requested shared-file changes

1. **mcport/main.cpp, in-game present** (and task B's in-game pointer work): draw the pointer into the
   game's frame buffer after `render_frame` and before presenting, and keep the frame buffer between frames
   (as the original's back buffer), so the motion blur shows the same ghost:
   ```cpp
   // vga_present_frame_2f480 -> vga_copy_320x200_610f0: the pointer is drawn into the back buffer
   {
       const PlayerRec &rec = g_state->players[g_state->local_player & 7];
       ui_draw_mouse_pointer(fb, ui_sprite(g_ui_pointers, rec.input_mode == 2 ? 1 : 0), g_mouse_x, g_mouse_y);
   }
   ```
   (Entry 0 is empty: in flight nothing is drawn. If task B introduces a "current pointer" for
   mouse_cursor_set_sprite_5ba5c, use that sprite instead of the input-mode rule.)
2. `src/mcengine/frontend.cpp` (task B): the `s_pal` clear above.
3. None else: `render.h` additions are mine; the new tests come with their own `.cmake` (globbed).

## mcport integration

Only item 1 above. The front end needs nothing new from D (its pointer is already drawn by `fe_frame`).

## Corrections to ENGINE.md / names

- The front end never runs in 640x480 (section at the top); the `[esp+0x80]` path of config_parse_33750
  (mode 8 + Config.flags |= 8) is dead.
- During movie playback player_local_input_16660 is not called, so the R key (video_toggle_resolution_33600)
  cannot act in a movie.
- render_frame_1fab0 help screen: two empty lines before "If you are experiencing slowness..." and before
  "Magic Carpet comes to you...", one before "Good Luck!"; strings 0x90040.. (320x200) and 0x90060..
  (640x480) per line, same text; first line at y = -lh/2 + lh.
- render_frame_1fab0 credits tail: transitions before drawing in the same call; a page whose count reached 0
  is skipped without drawing; loops stop on '!' (the table at 0x9861c ends "!" then "#").
- The software mouse pointer is drawn into the back buffer at every blit (mouse_cursor_hide_for_blit_5b7f8 ->
  mouse_cursor_draw_5b35c; `DAT_0009e5e2 != 0` skips it, `DAT_0009e5e4` = mouse present) and never restored
  there (mouse_cursor_restore_unlock_5b824 has no caller); size = the sprite's tab w / h (halved in
  320x200), hot spot top-left, bytes 0xfe transparent. In game the pointer is pointers entry 1 in the spell
  book (player_set_input_mode_3bb50) and entry 0 (empty, 0x0) otherwise (video_input_init_3ed60).
- Front-end `*PALETTE` DAT_0012ecfc belongs to the resource list 0x510d0 (sptrs.dat, sptrs.tab, *PALETTE),
  reloaded with a fresh pool (mem_pool_init_61f80) whenever DAT_0012ebdc is set; fe_main_menu_init_53d30
  initialises font1's colours from it before loading mainmenu.pal.
- fe_main_menu_animate_53c40 advances the globe / hourglass every other frame, not by the timer.
- The level result screen (state 5) sets the state to 2 at the start of its statistics part (still showing
  the statistics); in DOSBox it presents several thousand frames per timer second.
- DOSBox: cpu_detect gives Config.pentium 0, so the front end skips the Intel logo (state 7) and the game
  starts with textured sky / reflections off.
- Dead code (no caller, no fixup target, no rel branch into it) used for the caves, useful for later
  patches: projectile_line_of_fire_clear_466af, crt_heap_realloc_dpmi_block_70bf6, tmap_cache_add_4c460,
  input_mouse_set_range_4a170, thing_try_damage_116e0, terrain_set_cell_height_3d7d0,
  terrain_max_drop_around_2376f, ui_draw_map_43910, castle_near_thing_11820, ai_goal_repair_castle_12d70,
  thing_find_in_sight_of_class_3e9a0, ai_goal_creature_near_rival_13600, crab_target_nearest_mana_ball_1aef0,
  projectile_create_type13_long_383d0, effect_create_type37_397c0, players_clear_records_3bf60.

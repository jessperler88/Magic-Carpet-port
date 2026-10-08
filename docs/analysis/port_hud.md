# HUD and frame composition port (hud.cpp, ui_draw.cpp) - agent D report, round 4, 2026-10-07

Files: `src/mcengine/hud.h`, `hud.cpp` (render_frame and the HUD pieces), `ui_draw.h`, `ui_draw.cpp`
(2D layer: sprite tables, span blitter, rect fills, text, front-end font engine, colour cube),
`src/mcengine/tables/hud.tables` -> `gen/hud_tables.h`, `src/tests/hud_test.cpp` + `hud_test.cmake`
(`mc_test`, whole library). Build dir `build_D`: `hud_test.exe` -> `hud_test: OK` (exit 0), zero
warnings (/W4) in hud.cpp, ui_draw.cpp and the test.

What renders: the flight HUD (circular radar with blips and the castle line, castle / balloon / wizard
status panels, the two hand labels, notices / chat lines, PAUSED, level-won text), the spell book
(24 cells, selection, quick-key digits, "not enough mana" dimming, the selected spell as a label),
the map screen (rectangular radar, top-right view window, player / kills table), the help screen,
the movie indicator and the attract-mode credits roll, the Alt+V debug overlay; at 640x480 and
320x200 (UI coordinates in the 640-wide virtual screen, halved by the blitters as in the exe).

## Functions translated

| port | original | notes |
|---|---|---|
| `render_frame(fb, player)` | render_frame_1fab0 (6196 B) | jump table 0x1fa84: modes 0 / 3 flight, 1 help, 2 book then falls into 4 map; message jump table 0x1fa98; tail = "MOVIE: %d" + credits state machine (Config+0xa1..0xa7) |
| `render_set_view_window_top` | render_set_view_window_top_2f320 | see deviations (DAT_000b5810) |
| `ui_draw_radar` | ui_draw_radar_43610 | row table DAT_000b7480 from the circle profile 0xcdcb0; circular = BLEND[dest][SHADE[light][avg[type]]], rectangular = opaque |
| `ui_draw_radar_blips` | ui_draw_radar_blips_42a20 | jump tables 0x42970 (class 2..12) and 0x4299c (switch types); castle dotted line (isqrt / atan2); "reveal" = spell 5 being cast (names of the other wizards); centre cross darkened from shade level 0x2c |
| `ui_draw_map` | ui_draw_map_43910 | no caller in the exe; translated from the decompilation (320: 1 px per cell, 640: 2x2 with neighbour blends) - by eye only |
| `ui_draw_status_bars` | ui_draw_status_bars_219f0 | jump table 0x219c4 (castle level -> 0..3 balloons) |
| `ui_fill_bar` | ui_fill_bar_212f0 | `(x, y, max_w, h, value, colour)`, nothing below 2 px |
| `ui_draw_player_list` | ui_draw_player_list_21370 | HUD entries 85 / 86 |
| `ui_draw_thing_label` | ui_draw_thing_label_22870 | hand panels (0x1fe / 0x23e, y 2) and the selected book cell |
| `ui_draw_spell_panel_icon` | ui_draw_spell_panel_icon_22d80 | |
| `ui_draw_spell_icon` | ui_draw_spell_icon_22820 | entry 3 blended + icon tinted with 0xa6 |
| `ui_draw_debug_overlay` | ui_draw_debug_overlay_4ad80 | memory statistics left as TODO |
| `ui_set_font`, `ui_font_space_width`, `ui_font_line_height` | 4abe0, 4abc0, 4abd0 | font = sprite table; space = entry 0x21 |
| `ui_draw_text`, `ui_draw_text_background`, `ui_text_width` | 4a9a0, 4aa90, 4ab60 | |
| `ui_draw_sprite`, `ui_draw_glyph`, `ui_draw_sprite_rows` | ui_draw_sprite_60688 (= vga_fill_rect_606c0, ui_draw_icon_4d0dd), ui_draw_glyph_4d240, ui_draw_icon_rows_4d1c8 | through the span core of vga_draw_sprite_spans_6070d (flags 0 and 0x40) |
| `ui_draw_sprite_blend`, `_tint`, `_shaded` | ui_fill_rect_blend_224e0, ui_fill_rect_blend2_22610, ui_draw_sprite_shaded_22760 | the exe's names are wrong: all three walk span sprites |
| `gfx_fill_rect`, `gfx_fill_rows`, `gfx_put_pixel` | 60590 / 60610, 4ce83 / 4cea9, 60f3c / 60f7c | |
| `ui_shade_rect`, `ui_copy_block` | 23310, 23140 | |
| `vga_draw_box`, `vga_fill_line` | vga_draw_box_603f0 / vga_draw_rect_outline_640_604c0, vga_fill_rect_clipped_6abbc / gfx_fill_rect_clipped_6acd4 | |
| `palette_find_nearest`, `ui_colour_cube_build` | palette_find_nearest_60fc0, colour cube of data_load_all_334c0 | |
| `ui_sprite_lists_relocate` (+ `relocate_table`) | ui_sprite_lists_relocate_49de0, sprite_table_relocate_62ad0 / _x2_62a80 | the 320 tables get doubled size bytes |
| `ui_set_clip_rect`, `ui_get_clip_rect`, `ui_push/pop_clip_rect`, `ui_font_init`, `ui_fe_text_width`, `ui_fe_draw_text`, `ui_fe_draw_glyph` | 588b0, 588f0, 58930 / 58950, 589d0 (+ ui_font_relocate_glyphs_58f30), 58970, 58ab0, 58ba0 | front-end font engine; draw mode 0 only; nothing in the HUD calls it (not tested beyond compiling) |
| `debug_screenshot` | debug_screenshot_3ca00 | writes `scrNNNNN.ppm` instead of the "mhwanh" dump (Alt+X in the flight HUD) |
| `ui_draw_init`, `ui_draw_set_video_mode`, `ui_draw_shutdown` | data_load_all_334c0 / video_alloc_buffers_33480 / video_toggle_resolution_33600 (the loading part) | |

Not translated: text_split_lines_3ed30 (already in text.cpp), the blitter's unreachable flag paths
(DAT_0009e5c4 1 / 2 / 4 / 8 / 0x20: the blend table pointer DAT_0009e858 is null, nothing sets those
bits in game code), front-end glyph draw modes 0x4000 / 0x8000 (empty loops in the exe),
ui_text_edit_field_58290 (front end).

## Data files the HUD reads (established)

Tab list 0x9744c (relocated by 49de0 at start-up and after every resolution switch):
`data/pointers.dat/.tab` (DAT_000adfc0 / adfb8, mouse pointers), `data/font0` (DAT_000adf28),
`data/font1` (DAT_000adf2c), the HUD set DAT_000adf94 = `data/hspr0-0` (640x480, resource list 0x97370)
or `data/mspr0-0` (320x200, list 0x97294, sizes doubled at relocation), `data/building` (DAT_000adfb0).
`ui_draw_init` loads exactly these. Not read by carpet.exe: `data/bookbkg.dat` / `book.pal` (no
reference in the image); `data/font2` is referenced only from front-end code (0x5152a).

HUD sprite numbers used: 1 / 2 hand label idle / casting, 3 / 4 book cell idle / casting, 6 + spell id
icons, 30..39 quick-key digits, 40 left panel, 41 status panel, 42 heart / mana icons, 43 wizard,
44..50 castle levels (0x2b + level), 50..53 balloons (0x32 + n), 54 empty panel, 55 red (hit) panel,
0x3a + player castle blip, 0x42 + player balloon blip, 0x53 / 0x54 switch blips, 85 / 86 player list.

## Verification (hud_test, numbers from the last run)

By construction:
- Sprite tables: HUD sprite 3 is 64 x 37 (hspr) / 64 x 36 (mspr 32 x 18 doubled), the values
  input.cpp hard-coded for the book.
- Text: `ui_text_width("PAUSED!")` = sum of the font-1 .tab widths (28 at 640, 56 at 320 in 640-space),
  `ui_draw_text` returns x + width, all lit pixels inside the text box (59 pixels), line heights 7 / 14.
- Colour cube corners = `palette_find_nearest` of black / white / red.
- `gfx_fill_rect(10, 20, 30, 40)` fills exactly 1200 pixels at 640 and 300 (15 x 20 at (5, 10)) at 320.
- Radar: rectangular radar at yaw 0, scale 0x100 puts the camera cell's colour
  (SHADE[light][avg[type]]) exactly at the centre pixel and the cell 3 to the east 3 pixels right; the
  circular radar covers 12848 of 16384 pixels (pi/4 would be 12861), corners untouched.
- Clipping: 144 positions from -2000 to 32000 for every primitive (sprite, blend, tint, shade, text,
  text background, fill, pixel, shade rect, box, block copy, radar both kinds, map) in both modes:
  16 KB guard bands around the frame unchanged.

Snapshot (level 38, `engine_load_snapshot`):
- Flight HUD 640x480 / 320x200: pixels changed against the plain view: radar 13981 / 3463, status
  panels 16798 / 4201, hand labels 5596 / 1408, message line 113 / 113; bottom half of the view
  unchanged. A notice in message slot 1 counts its ticks 5 -> 4.
- Spell book: pointer on cells 0, 5, 23 in both modes: the HUD's `Config.spell_slot` equals
  `input_book_update_selection()` (input.cpp's copy of the same hit test).
- Map screen, help screen, debug overlay rendered with guard bands intact.
- Images (`build_D/Debug/hud_test_frame{,_book,_map,_help,_debug}_{640,320}.ppm`) looked at: panels,
  icons, bars, names and digits sit where expected, the 320 frames are the 640 layout at half size.

Not verified (no reference screenshot): exact pixel placement against DOSBox, the blips' colours
(by eye plausible: creatures black / player colours, mana balls gold), the credits roll (Config.flags
& 4 never set in the test), `ui_draw_map` (no caller), the front-end font engine.

Snapshot facts: `opt_hud_a` / `opt_hud_b` are 0 and both hands are empty (slot 0xff) in
gam00000.dat; the test switches the HUD on and puts book slots 0 / 1 into the hands.

## Deviations / port-only guards

- The original does not clip rect fills, shade rects, radar rows or the C span blitters (224e0 /
  22610 / 22760); the port clips everything to the target. Sprite clipping in the exe is against the
  2D clip rectangle, which is the whole screen in game, so visible output is the same.
- Thing pointers from unchecked indices (`things + idx * 0xa4` compared only against `things[0]`) are
  accepted only for indices 1..999. The empty-hand case (slot 0xff) reads `P.spell_slot[255]` from the
  GameState as the exe does (the next player's camera log) and then rejects the garbage index; the exe
  would follow the wild pointer.
- Division guards where the exe would fault: radar width 0, `mana_total` / `max_health` /
  `Config.total_mana` 0 (bars skipped), radar row with zero length (the exe loops 2^32 times).
- `gfx_fill_rect` with h == 0 draws nothing (the exe: 65536 rows).
- `palette_find_nearest` returns 0 when no entry beats 9999 (the exe returns an uninitialised local).
- Credits roll: the line scan also stops at "#" (the exe only at "!").
- `render_set_view_window_top` cannot update DAT_000b5810 (static in render_landscape.cpp); only
  render_view's motion-blur pass reads it.
- Help screen and debug-overlay texts are string literals in hud.cpp (copied from 0x90040.. /
  0x92978..); the credits are extracted (`g_hud_credit_ptrs` / `g_hud_credit_text`).
- The radar / blip row table DAT_000b7480 is a 512-row static; rows not written by the radar keep
  their previous value as in the exe.
- The local player is the `player` argument (the exe reads `GameState.local_player` everywhere; the
  reveal loop of the blips still compares with `local_player`).

## Game-state writes of render_frame_1fab0 (hud_tick_state / render_frame_draw split)

The original renders once per tick, so render_frame's writes are part of the simulated state (the
movie-0 reference dump of task E shows them: at tick 442 the book opens and P.spell_flash[0..23] go
0xff -> 0xf4 over 12 ticks). Every write found in render_frame_1fab0 and its callees, in execution order:

| write | where | condition |
|---|---|---|
| `P.castle_hit_flash--` (+0x187) | status panels | flight HUD drawn, opt_hud_b, P.castle with level != 0, flash > 0 and Config.tick_bits[0] |
| `P.damage_flash--` (+0x189) | status panels | as above, P.castle != 0 |
| `P.hit_flash--` (+0x188) | status panels | opt_hud_b, flash > 0 and tick_bits[0] |
| `P.spell_flash[type]--` (+0x34c) | ui_draw_thing_label_22870 / ui_draw_spell_panel_icon_22d80 | for every owned spell icon drawn whose owner's input_mode != 0 or whose flash > 0: the two hand labels (flight, opt_hud_b; drawn before the status panels), every owned book cell (mode 2, label for the selected one); not when the icon blinks off (cast_ticks 1..0x3f, duration > 0x40, tick_bits[0]) |
| `PlayerMsg.ticks--` (local player's slots, arg 0 / 2 / 3) | message lines | flight HUD drawn, !(Config.flags & 0x200), ticks > 0 |
| `Config.spell_slot` (+0x16) | book (mode 2) | always in mode 2: the cell under g_mouse_x / g_mouse_y whose spell is owned and affordable, else 0xff |
| `Config.credits_state` (+0xa1..+0xa7) | tail | Config.flags & 4 (attract movie) |

"Flight HUD drawn" = input mode 0 / 3, the local wizard's health >= 0 and !(Config.flags & 4).
Order inside one call: hand labels (left, right), status panels (castle, balloons, wizard), messages;
or book cells 0..23, selected label, Config.spell_slot. `debug_bits |= 1` (and 2 for the password) is
written by ui_draw_debug_overlay_4ad80, which game_tick_update calls separately after render_frame
(idempotent ORs, so calling it at frame rate is harmless).

Split: one internal pass with two flags. `render_frame(fb, p)` = writes + drawing (the original);
`hud_tick_state(p)` = writes only (no frame buffer: render_view, clears and screenshots skipped, the 2D
primitives return on a null target); `render_frame_draw(fb, p)` = pixels only. hud_test checks in the
book and in the flight HUD (a message, a hit flash) at both resolutions that hud_tick_state changes
exactly the bytes render_frame changes (25 / 3), render_frame_draw changes none, and that
render_frame_draw from the pre-write state draws the same pixels as render_frame. Data hud_tick_state
needs: g_mouse_x / g_mouse_y (book), Config.tick_bits, g_video_mode_flags (book origin y), the HUD
sprite 3 height (falls back to input_book_cell_h() when ui_draw_init was not called). Credits in a
draw-only pass show the current page (at a page change one frame differs from the combined call).
The integrator calls hud_tick_state(local) once per tick where game_tick_update_32e80 calls
render_frame (after the simulation of the tick; a reference dump taken before render_frame of tick N
shows the writes of tick N - 1), and render_frame_draw at frame rate.

## Hooks / globals

Declared: `g_hud_screenshot_dir` (where Alt+X writes), `g_hud_joystick_present` (DAT_0012ed40, 0:
draws the book cell outline under the pointer when set). `render_frame` writes `Config.spell_slot`
(mode 2), `g_book_cell_w / h` (input.h) from HUD sprite 3, and decrements the PlayerMsg ticks.

## TODO(port) call sites

`mem_stats_622a8()` and the memory-block list in the debug overlay; sound number DAT_0009e328 shown as 0.

## Requested shared-file changes

0. Tick driver (sim / player.cpp game tick, reference_test): `hud_tick_state(local)` once per tick
   (see "Game-state writes"); needs `#include "hud.h"` and hud.cpp + ui_draw.cpp in the link.
1. `mcport/main.cpp`: after `engine_init` call `ui_draw_init(game_dir)` (needs the palette loaded);
   replace `render_set_view_window + render_view` with `render_frame_draw(fb, g_state->local_player)`
   (or `render_frame` when nothing else calls hud_tick_state)
   (the camera then comes from the player's log, i.e. demo / play follow mode; keep the free camera for
   the viewer); after it, `ui_draw_debug_overlay()` (game_tick_update_32e80 order). The
   `input_book_update_selection()` call before the tick can go: render_frame sets Config.spell_slot
   one frame earlier, as the original does. After a resolution switch (R key /
   INPUT_REQ_TOGGLE_RESOLUTION) call `ui_draw_set_video_mode(game_dir)`.
2. `engine.cpp` (optional): call `ui_draw_init` at the end of `engine_init` and `ui_draw_shutdown` in
   `engine_shutdown`.
3. `render_landscape.cpp` / `render.h`: export DAT_000b5810 (`g_view_window_offset`) or move
   `render_set_view_window_top` there so the map screen's view window sets it too.
4. `input.cpp`: `g_book_cell_w / h` are now written every frame by the HUD; the fallback values can stay.

## Corrections / additions for ENGINE.md, mc_types.h, carpet_types.txt

- DAT_000acc18 is a 16 x 16 x 16 colour cube `[r << 8 | g << 4 | b]` (6-bit components / 4) of nearest
  palette indices built by data_load_all_334c0; the HUD's "colour constants" are entries of it:
  0xacc18 black, 0xadc17 white, 0xadb18 red, 0xacd08 green, 0xacc27 blue, 0xadb27 magenta; blinking text
  uses `cube[(Config+0x5f) * 0xfff]`.
- DAT_00097630: 8 player colour pairs {A, B}; `[p * 2 + tick bit]` blinks between them.
- DAT_000adfbc = current font; a font is just a span sprite table (glyph c = entry c + 1), +0xca / +0xcb
  = width / height of the space glyph. The FontDesc struct (0x9e508..) belongs to the front-end engine only.
- DAT_0009e5c4 = blit flag word of vga_draw_sprite_spans_6070d (0x40 = solid colour, set by
  ui_draw_text_4a9a0); DAT_0009e858 (blend table pointer for flag 4) is null and never written.
- ui_fill_rect_blend_224e0 / ui_fill_rect_blend2_22610 / ui_draw_sprite_shaded_22760 are span-sprite
  blitters (blend with the sprite / tint under the sprite / shade the sprite), not rectangle fills.
- render_frame_1fab0's mode switch is PlayerRec.input_mode (+0x44a): 0 / 3 flight, 1 help screen, 2 book
  (+ map), 4 map screen. ENGINE.md 2.1 "view mode +0x3855" is the same byte.
- GameState+0x2199 (`opt_hud_a`) gates radar + blips, +0x219a (`opt_hud_b`) the hand labels and status
  panels (F8 toggles both).
- Config+0xa1..0xa7 (`credits_state`): {u8 state 1 roll / 2 restart / 3 pause, i32 countdown, u16 line}
  over the 112 credit pointers at 0x9861c ("!" ends a page, "#" the list); Config+0x5e / 5f / 60 are
  tick_bits[0..2] (blink phases 1, 2, 3 ticks).
- The radar centre cross uses shade levels 0x2c.. (above the 0x20 the renderer uses).
- Spell id 5 being cast (`P.spell_thing[5]` with cast_ticks != 0) makes the radar show the other
  wizards' names and every balloon.
- data/bookbkg.dat and book.pal are not referenced by carpet.exe.

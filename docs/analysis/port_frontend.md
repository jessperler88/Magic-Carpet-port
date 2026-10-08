# Port round 5, task B: front end and save games

Files: `src/mcengine/frontend.h/.cpp`, `src/mcengine/savegame.h/.cpp`, `src/mcengine/tables/frontend.tables`
(generates `gen/frontend_tables.h`), `src/tests/frontend_test.cpp/.cmake`. Build dir `build_B`.
The test passes (exit 0) and builds with zero warnings at /W4.

## What was translated

All of `frontend_menu_loop_52070` and its screens. The port is frame-stepped: one `fe_frame()` per displayed
frame. Every blocking loop of the original is now a phase of a small per-screen state machine. These loops
are: the palette fades, FLI playback, the logo waits, the dialogs, the text fields and the timed reveal on
the result screen. A "blocker" (fade, FLI, wait or delay) runs one step per frame. The screen code then
continues where it stopped. The original's double buffering is kept: the back buffer `DAT_0012ed74` and the
background `DAT_000adf68` are swapped by pointer exactly as in the original. There are also the two screen
backups `ed04` / `ed08`, the mask, and the "VGA" image (what the last `vga_blit_backbuffer_320_610f0`
showed). `fe_frame` presents the VGA image plus the mouse pointer.

| original | port |
|---|---|
| fe_init_state_51ed0 | `fe_init_state` (static; `fe_init`) |
| frontend_menu_loop_52070 (+ jump table 0x52038, music_update_65b20) | `fe_frame`, `dispatch`, `fe_music_update` |
| fe_screen_config_521c0, fe_config_draw_52ab0, fe_config_draw_summary_52e20, fe_config_screen_init_53070, fe_config_screen_exit_531e0, fe_config_apply_input_device_59330 | `fe_screen_config` and its helpers |
| fe_check_shift_q_quit_52980, fe_wait_ticks_or_input_529d0 | `fe_check_shift_q_quit`, blocker `BK_WAIT` |
| fe_screen_main_menu_532b0, fe_main_menu_init_53d30, fe_main_menu_exit_54010, fe_main_menu_animate_53c40, fe_main_menu_draw_overlay_540c0, fe_menu_item_enabled_53bf0, fe_menu_select_prev/next_53b90/53bc0, fe_input_idle_check_53ad0, fe_highlight_masked_item_593c4, fe_build_bright_table_579f0 | `fe_screen_main_menu` (`mm_part_a` / `mm_part_b` / `mm_init_*` / `mm_exit_*`) and helpers |
| item handlers fe_menu_new_or_resume_game_57480, fe_menu_text_dialog_57580, fe_menu_multiplayer_57400, fe_menu_quit_57270, fe_menu_start_level_57450 | `mm_part_a` / `mm_modal_done` / `text_dialog_step` |
| fe_confirm_dialog_56e20, callbacks 0x54150 (load), fe_draw_centred_icon_57350 (quit), fe_draw_centered_text_57530 (new game) | `confirm_step`, `fe_load_confirm_draw`, ... |
| fe_save_slot_dialog_541f0, fe_text_entry_begin_54640, fe_dialog_draw_buttons_546e0, fe_draw_save_slot_names_54850 | `save_dialog_step`, `text_entry_begin`, ... |
| fe_dialog_draw_title_a/b_578a0/57930, fe_fli_composite_bg_579c0 | `fe_dialog_draw_title`, `fli_present` (composite) |
| ui_text_edit_field_58290, ui_str_prepend_58820, ui_str_delete_at_58880, gfx_fill_rect_clipped_5940c | `edit_begin` / `edit_step`, `str_prepend`, `str_delete`, `fe_fill_rect_clipped` |
| fe_input_poll_57cc0 (mouse mode) | `fe_input_poll` |
| fe_screen_intro_movie_54900, fe_screen_outro_movie_54ab0, fe_screen_bullfrog_logo_563c0, fe_screen_intel_logo_56510, fe_screen_title_56730, fe_flic_loop_frame_56670 | `fe_screen_intro` / `_outro` / `logo_screen` / `fe_screen_title` / `fe_flic_loop_frame` |
| fe_screen_language_56940 | `fe_screen_language` |
| fe_screen_level_result_55b00 | `fe_screen_level_result` |
| fe_screen_multiplayer_54bd0, fe_multiplayer_draw_slots_55210, fe_multiplayer_init_55630, fe_multiplayer_exit_557c0, fe_multiplayer_refresh_55870 | `fe_screen_multiplayer`, `fe_multiplayer_draw_slots`, `fe_multiplayer_refresh` |
| fe_savegame_read_names_58f60, fe_savegame_load_59030, fe_savegame_save_591d0 | `savegame_read_names`, `savegame_load`, `savegame_save` (savegame.cpp) |
| vga_draw_sprite_spans_6070d with its sprite window (gfx_set_clip_window_65c10), ui_draw_sprite_60688 | `fe_draw_sprite(_to)`, `fe_set_window` |
| mouse_cursor_set_sprite_5ba5c | `fe_cursor`: the sptrs entry is drawn into fb at the end of `fe_frame` (hot spot top-left, as in mouse_cursor_draw_5b35c) |
| ui_font_init_589d0 (with its palette argument) | `fe_font_init` (wraps `ui_font_init` and recomputes the colours from the front-end palette; `ui_draw.cpp`'s version uses `g_palette6`) |
| sprite_table_relocate_x2_62a80 / _62ad0 for the front-end tables | `fe_relocate` / `fe_relocate_plain` |
| flic_play_chunk_50dfd (main-menu globe / timer, title-02 loop) | task C's `fli_chunk_play` (fli.h), wrapped in `flic_play_chunk` |

Tables (`tables/frontend.tables`) hold the menu item table (0x5167c), the slot-name rects, the lobby slot
positions, the input-device pages, the language-flag positions, the scancode-to-ASCII tables and the key
filters, the level names (pointers 0x97490 and their strings), the default slot names (0x9e469), the
initial lobby slots (0x9e520) and the sound card names.

## Flow and the globals shared with task D / the integrator (frontend.h)

- `g_fe_leave` DAT_0009e504, `g_fe_reload` DAT_0012ebdc, `g_fe_game_in_progress` DAT_0009e500,
  `g_fe_session` DAT_0012ed30, `g_fe_lobby_players` DAT_0012ed31, `g_fe_network` DAT_0009e3c8,
  `g_fe_input_flags` DAT_0009e583, `g_fe_input_page` DAT_0012ed33. Also `fe_level_name(i)`,
  `fe_set_save_dir(save_dir, dos_game_dir)` and `g_fe_hook_sound_initialise`.
- **DAT_0009e500 is 1 in the initialised data, and no code ever sets it again.** Only three writes of 0
  exist: start level, resume, and load. So at program start, menu item 1 starts `Config.level` straight
  away. That is the command-line level or 0, and the spells are not cleared. Item 11 (start level) stays
  disabled until the first game has started. After that, item 1 asks "New Game? Yes/No". Yes means level
  0, `players[local].blk.spell_found` cleared and state 5. The front end does this itself. It is
  equivalent to D's `game_campaign_new`.
- Every level start sets state 5 (result screen) before leaving. `FE_START_LEVEL` returns
  `*level_out = Config.level`. D's `game_after_frontend` applies the level skip and does the fade. The
  front end does not fade on leaving, which matches frontend_menu_loop_52070. The next `fe_frame` resets
  DAT_0009e504 and reloads sptrs.dat (DAT_0012ebdc).
- `FE_START_DEMO = 3` is new. The attract "demo level" (attract phase 2) sets
  `Config.flags |= 0x24`, `Config.movie = 0`, `cfg+0xa1 = 3` and `cfg+0xa2 = 200`. It also saves the level
  and the 0x18 spell bytes (DAT_0012ed18 / DAT_0012ebc0). `*level_out` is the movie number. On the way
  back, the main-menu init restores the saved level and spells and clears 0x24.
- `FE_QUIT` covers three cases: Shift+Q on a logo or wait, "Quit to DOS" answered Yes, and the end of the
  outro (after a 0x1e0-tick delay). In each case `PlayerRec.quit` of the local player is 1.
- The result screen (state 5) reads `players[local].status`: bit 8 skips it, bit 2 plays levelw1/w2,
  bit 4 plays levelose. It then shows the P-block stats of the local player's Thing (`thing_player_block`):
  +0x167, 0x16f, 0x16b, 0x173 and 0x177 as "% 3d %%", and +0x17b as "%dh% 02dm %02ds". These are D's
  `GameLevelStats`, taken after `game_level_end`. The level name is `level_names[Config.level]`, read after
  the increment. The next state is 10 (outro) when `Config.level == 50`, otherwise 2.
- Campaign progress is `Config.level` plus `GameState+0x3bd6` (= `players[0].blk.spell_found`). The load
  and save code use `g_cfg` / `g_state` directly.

## Save games (savegame.h)

The format is the original's, byte for byte: a 142-byte record, laid out in savegame.h. On load,
`Config.level = checksum / version - DAT_0012ed30 - DAT_0012ed31`, and `g_fe_game_in_progress` is cleared.
A slot with a version other than 4 shows "--" and does not load. Slots are written to
`<save_dir>/carpet%02X.gam` and read from there first. `<dos_game_dir>/save/` is read only, and is used when
a slot is missing in save_dir. With no save dir, saving is disabled. The two marker files of `c:\carpet.cd`
also go into the save dir. `intro.pld` means the intro was seen once; from then on it can be skipped.
`language.inf` holds the chosen language, and the language screen is skipped when it exists.

## Verification (frontend_test, `build_B/Debug/fe_*.ppm`; I looked at every image)

- The format is checked by construction: field offsets in the encoded record, the decode round trip, the
  level formula, and a short record rejected.
- Scripted input drives the whole flow.
  - Language: a click on the German flag gives language 2. Back to English, then OK.
  - Config: the input-device page cycles 0, 1, 2, 4, 6, 0.
  - OK leads to Intel logo (7), Bullfrog logo (9), intro (0), title (8) and the main menu (2). Real FLI
    frames from task C are shown, and the waits are skipped with Space.
  - Menu hover by mask pixel; load-slot mode; right-click back.
  - Save into slot 1. The empty slot "--" starts text entry. End, Backspace and Shift+A/B/C type "ABC",
    then Enter twice. The file is 142 bytes.
  - Load it back through the menu and its Yes/No dialog: the level is restored (3 -> 17).
  - A hand-built DOS save in `<dos>/save/carpet03.gam` lists as "DOS Slot Four" and loads level 23 with
    spells. A version-3 file is rejected.
  - Name / call-name dialog. Quit dialog answered No.
  - Multiplayer lobby, with the network forced on: the session name cycles to CARPET1, then back.
  - "New Game? Yes": `FE_START_LEVEL`, level 0, state 5.
  - Result screen with all six lines; the time reads "1h 2m 03s".
  - Item 11 (start level) gives `FE_START_LEVEL 1`.
  - 0x12c0 idle ticks start the attract intro (state 0).
  - Result screen with level 50, then the outro, then `FE_QUIT` with quit = 1.
- The images look right: menu with globe and hourglass animations, slot names, scroll dialogs, lobby,
  pperf stats, the language flags with the selection frame, the config book.

## Deviations / gaps (TODO(port))

- The sound-setup wizard (`DAT_0012ed29` steps 0..6 and the writing of sndsetup.inf/.dat) is not ported.
  `fe_sndsetup_read_57af0` is replaced by "present". The config summary shows "Soundblaster 16" or
  "No sound", and "General midi" or "No music", depending on `g_sound_available` / `g_music_available`.
  The card ids are "NONE", so the I/O / IRQ / DMA lines are not drawn. A click on the summary would
  restart the wizard in the original; the port ignores it.
- There are no joystick or VFX1 drivers. `fe_config_apply_input_device_59330` keeps
  `g_fe_input_flags = 0`, so the pointer is always the "hardware" cursor that `fe_frame` draws.
- There is no network. Item 3 needs `g_fe_network`. In the lobby, Start calls the failing join (the
  original's -1 path: refresh and fade in). Back works.
- `cpu_detect_5ac80` is not re-run. The Intel logo depends on `Config.pentium` (1 in the port).
- Text field: when a key's ASCII table entry is 0 (Ctrl, Alt, keypad *), the original moves the cursor
  past the end of the string. The port ignores such keys.
- The FLI cue scripts are attached by task C's `fli_open`, by file name. The front end chooses abort and
  palette per call exactly as the original passes them: intro abortable only when intro.pld existed, the
  scroll dialogs `(0,0)`, the outro `(0,1)`, all others `(1,1)`.
- Input mode `DAT_0012ed2f` 0 / 1 (keyboard cursor) is unreachable in the original and not translated.
- The 640x480 path is not ported; the original switches to 320x200 before the menu loop. `fe_frame`
  temporarily forces `g_video_mode_flags = 1` while it draws. A frame buffer of at least 640x400 gets a
  pixel-doubled image.
- A text dialog over another screen keeps its state across frames, so an aborted dialog cannot leak. The
  original's use of the freed sfont2 in the last result-screen frame is avoided: it is freed after drawing.
- The result screen plays "sample 3" through `sound_stop_sample` plus `sound_play_sample_loud`. This is
  the equivalent of `sound_play_sample_65c70` (stop the same sample, start it at 0x7fff, flags 0x100).

## Integration (mcport/main.cpp; the integrator applies it)

```cpp
#include "frontend.h"
#include "game.h"
#include "palette_fx.h"
// ...
// New mode: `mcport [game_dir]` without a mode (or "menu") runs the whole game.
const bool menu = argc <= 2 || std::strcmp(argv[2], "menu") == 0;
// ...after engine_init, ui_draw_set_video_mode, input_reset, the platform init:
std::string save_dir = /* e.g. SDL_GetPrefPath("Bullfrog", "MagicCarpet") */;
fe_set_save_dir(save_dir.c_str(), game.c_str());        // DOS saves of the package are read too
fe_init(game.c_str(), 6);
enum { RUN_FE, RUN_LEVEL, RUN_DEMO } run = RUN_FE;
game_before_frontend();
// per frame, input fed exactly as in the play mode (key events, input_mouse_move with virtual_h(),
// button transitions; key *and button* releases held back until after fe_frame / the game tick, or a short
// click between two frames is lost: fe_input_poll_57cc0 derives clicks from the held state):
const uint32_t ticks = (uint32_t)((double)plat.ticks_us() * TICK_HZ / 1e6);    // DAT_0012eab4
if (run == RUN_FE) {
    int lvl = 0;
    const FeResult r = fe_frame(fb, ticks, &lvl);
    /* apply the deferred releases here */
    if (r == FE_QUIT) break;
    if (r == FE_START_LEVEL) { game_level_begin(game_after_frontend(lvl)); run = RUN_LEVEL; }
    if (r == FE_START_DEMO)  { sim_prepare_movie(); demo_open(game.c_str(), lvl); engine_tick(); run = RUN_DEMO; }
} else if (run == RUN_LEVEL) {
    const GameStatus s = game_level_tick();              /* + render_frame_draw as now */
    if (s != GAME_RUNNING) {
        game_level_end();
        if (s == GAME_QUIT) break;
        game_before_frontend(); fe_enter(5); run = RUN_FE;
    }
} else {                                                 // attract demo; any input ends it as input_changed does
    if (!engine_tick() || input_changed()) { game_before_frontend(); run = RUN_FE; }
}
if (palette_display_dirty()) {                           // the front end and the fades drive the display palette
    uint8_t rgb[768]; mc_palette_to_rgb(g_display_palette6, rgb); plat.set_palette(rgb);
    palette_display_clear_dirty();
}
```

`g_fe_hook_sound_initialise` can stay null; audio is initialised at start-up (task A's `audio_init`).

## Requested shared-file changes

None. CMake picks up the new sources through the globs, and `gen/frontend_tables.h` is new (regenerate it
with `python tools/port/gen_exe_tables.py frontend`). The test cmake lists its sources explicitly.

## Corrections to ENGINE.md / names

- The marker file that `fe_init_state_51ed0` checks is `c:\carpet.cd\intro.pld` (string 0x93148), not
  language.inf. Its existence sets DAT_0012ed35 bit 1, "intro already seen", which makes the intro
  skippable. If it is missing, an empty marker is written. `language.inf` belongs to
  fe_screen_language_56940.
- DAT_0009e500 starts as 1 and is never set (see above). "Resume" is the very first start, not a game in
  progress.
- 0x12ee21 is `g_key_down[1]` (**Esc**) and 0x12ee3c is `g_key_down[0x1c]` (Enter). The ENGINE.md list
  calls 0x12ee21 "Enter". So Esc on the config screen confirms and skips the logos; in the dialogs Esc is
  No and Enter is Yes.
- Main-menu item 2 (0x57580, "unanalysed") is `fe_menu_text_dialog_57580`. It runs the scroll animation,
  then edits "Enter your name:" (Config+0x1d, 30 chars) and "Enter your call-name:" (Config+0x3d, 8 chars,
  filtered keys).
- "New game" clears `PlayerRec[local]+0x7cb` = `blk.spell_found`. That is the same 0x18 bytes as the
  save game's GameState+0x3bd6 when local = 0.
- The level-result stats are P-block fields (+0x167 creatures killed %, +0x16f accuracy, +0x16b spells
  found, +0x173 mana, +0x177 overall, +0x17b time), read through the local player's Thing.
- fli_play_508f0's arguments are (abort_on_key → DAT_0012eabe, apply_palette → DAT_0009e44c, cue). Its
  memory-stream decoders skip value-0 pixels only through DAT_0009e468, which the main-menu init sets.
- vga_draw_sprite_spans_6070d positions are relative to the sprite window (DAT_0012ed88 / ed98). The
  dialogs set the window to (0x41, 0x4b, 0xbd, 0x2c) for their title sprites. `ui_draw.cpp`'s blitter
  ignores the window, which is correct for the HUD only.
- `ui_font_init_589d0` takes the palette as its third argument. The port's `ui_font_init` uses
  `g_palette6`; front-end callers must recompute the colours.
- In the original SS2 decoder (0x50f71), a "last byte" word falls through and uses the same word as the
  packet count. That is a bug the shipped files never trigger (task C's decoder handles it).

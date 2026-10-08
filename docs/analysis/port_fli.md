# Port round 5, task C: FLI player, cue scripts, palette (`fli.h/.cpp`, `palette_fx.h/.cpp`, `tables/fli.tables`, `tests/fli_test.*`)

The FLI player of the front end, the cue scripts that sync speech / sound / music / subtitles to FLI frames, the display palette with the original's stepped fade, the in-game palette flashes, the map-mode palette save / restore and the "smatitle" loading screen.

`fli.h` and `palette_fx.h` implement the round-5 contracts unchanged. Additions are appended below the fixed declarations (marked "additions by task C"); the only other header change is `#include <cstddef>` / `"render.h"`.

The decoder is C++ inside `fli.cpp`; there is no `mcdata/flic.c`. That avoids a change to the explicit mcdata list in `src/CMakeLists.txt`, and the decoder reproduces engine quirks that a generic FLIC decoder should not have.

## Functions translated

| original | port |
|---|---|
| `fli_play_508f0(abort_on_input, apply_palette, cue_script)` | `FliPlayer`: `fli_open` / `fli_open_memory`, `fli_frame_due`, `fli_next_frame`, `fli_abort` |
| `fli_error_unknown_frame_50520` (really *read frame chunk*) | `decode_next` (first half) |
| `movie_frame_present_50600` (decode switch + present) | `decode_next` (sub-chunk switch), `fli_next_frame` (palette / subtitle colour), `fli_blit` (blit incl. the 0x1a40 shift) |
| `movie_wait_frame_50430` | `fli_frame_due` (+ `fli_abort` for its abort exits) |
| `fli_mem_read_504f0` | `MemReader` (bounded) |
| `fli_mem_decode_color256_509f0` (types 4 and 11) | `decode_color` |
| `fli_mem_decode_ss2_50a90` (7) | `decode_ss2` |
| `fli_mem_decode_lc_50be0` (12) | `decode_lc` |
| `fli_mem_decode_brun_50d00` (15) | `decode_brun` |
| 50600 cases 13 BLACK / 16 COPY / 18 PSTAMP | inline in `decode_next` |
| `cue_script_step_17d80` | `cue_step` |
| `movie_subtitle_init_23600` / `movie_subtitle_show_236a0` / `movie_buffer_clear_236d0` / `movie_free_23700` / `movie_subtitle_set_colour_23740` | `subtitle_init` / `subtitle_show` / `subtitle_clear` / `subtitle_free` (also `fli_subtitle_free`) / `subtitle_set_colour` |
| `flic_set_dest_50de0`, `flic_play_chunk_50dfd`, `fli_decode_frame_50e88`, `fli_read_file_header_50f36`, `fli_skip_chunk_50f60`, `fli_decode_ss2_50f71`, `fli_decode_brun_51024` | `fli_chunk_set_dest`, `fli_chunk_play` (+ static `chunk_ss2`, `chunk_brun`) |
| `vga_palette_fade_61510` (blocking mode) | `palette_fade_start` + `palette_fade_step` / `palette_fade_active` |
| `vga_palette_fade_61510` (incremental mode, arg 3 = 1) | `palette_fade_incremental` |
| `vga_palette_fade_reset_61718` | `palette_fade_reset` |
| `vga_read_palette_61ec8` / `vga_set_palette_302f0` | reads of / `palette_display_set` into `g_display_palette6` |
| `palette_effect_update_33010` | `palette_effect_update` |
| `palette_fade_out_and_load_32e40` | `palette_fade_out_and_load` |
| `mapmode_palette_save_30350` / `mapmode_palette_restore_303b0` | `mapmode_palette_save` / `mapmode_palette_restore` |
| `title_screen_show_32db0` | `title_screen_show(game_dir, fb)` |

**Not ported:** `fli_file_play_5c264`, the hand-written file-stream asm player, with its helpers 5c40b..5cb50. It has **no reference anywhere in the image**: I scanned the whole image for E8/E9 rel32 targets and absolute dwords of 0x5c264. It is dead code, and so is the preload branch of cue op `L` (`DAT_0009e844` is only filled by that dead player).

## Which file uses which player

- **`fli_play_508f0`** (the only file-stream player in use) plays every `intro\*.dat` except title-02. The caller writes the path into `DAT_0009e708` with sprintf; the arguments come from the push sequences at the call sites:

| file | caller | cue script | abort on key (arg 1) | apply palette (arg 2) |
|---|---|---|---|---|
| `intro\intel.dat` | `fe_screen_intel_logo_56510` (Pentium only) | 0x51734 | 1 | 1 |
| `intro\logo.dat` | `fe_screen_bullfrog_logo_563c0` | 0x51b70 | 1 | 1 |
| `intro\title-01.dat` | `fe_screen_title_56730` (`DAT_0009e44e` = 1 when `DAT_0012ed36 & 2`) | 0x51ab8 | 1 | 1 |
| `intro\intro.dat` | `fe_screen_intro_movie_54900` | 0x5174c | `DAT_0012ed35 & 2` (0 the first time) | 1 |
| `intro\levelw1.dat` / `levelw2.dat` | `fe_screen_level_result_55b00` (won; w2 when `DAT_0012eab4` is odd) | 0x51b28 | 1 | 1 |
| `intro\levelose.dat` | `fe_screen_level_result_55b00` (lost) | 0x51710 | 1 | 1 |
| `intro\outro.dat` | `fe_screen_outro_movie_54ab0` | 0x51b88 | 0 | 1 |
| `intro\scroll.dat` | `fe_save_slot_dialog_541f0`, `fe_confirm_dialog_56e20`, `fe_menu_text_dialog_57580`, with callback `DAT_0012ea9c = fe_fli_composite_bg_579c0` | 0x516cc | 0 | 0 |

  `fli_cue_for_file()` returns this table; `fli_abort_on_input()` / `fli_apply_palette()` give the two flags for an open player.
- **Memory-stream player (`flic_play_chunk_50dfd`):**
  - the sprite / texture animations (already in `sprite_cache.cpp`);
  - `fe_main_menu_animate_53c40` and `fe_flic_loop_frame_56670` (`intro\title-02.dat`, loaded into `DAT_0012ecc4` through the resource list 0x51624);
  - `data\screens\globe.dat`, `timer.dat` and `scroll.dat`. Their frame-chunk size fields do not match their sub-chunks (SS2 sizes are wrong), so only this player, which continues where the SS2 data ends, can play them.

  It is exposed as `fli_chunk_set_dest` / `fli_chunk_play` for the front end.
- **This package's movies are placeholders.** `intro\intel.dat`, `intro.dat`, `levelw1.dat`, `levelw2.dat`, `levelose.dat` and `outro.dat` are **byte-identical** (md5 43add94f..., 296632 bytes): the 41-frame Intel "pentium" animation. The scripts were written for the CD movies; the intro script runs to frame 2944.

## The Bullfrog FLIC variant
- **Header.** It is 12 bytes: {u32 12, u16 0xAF12, u16 frames, u16 w, u16 h}, instead of Autodesk's 128. `fli_play` reads 12 bytes and starts at offset 12 whatever the size says.
- **Frame chunks.** Each is a standard 16-byte 0xF1FA header and sub-chunks {u32 size, u16 type}. The files hold `frames + 1` chunks (the last is the ring frame back to frame 0).
- **Frames shown.** `fli_play` loops `while (frame < frames - 1)`, so the last frame and the ring frame are **never shown**.
- **Palette.** COLOR256 chunks carry 6-bit values, and `fli_mem_decode_color256_509f0` ignores its 0x100 / 0x40 argument (no scaling).
- **Census of the shipped files** (all 320x200, 6-bit palette in frame 0):

| file | frames | chunk types |
|---|---|---|
| intel / intro / levelw1 / levelw2 / levelose / outro | 41 | 4 x1, 15 x1, 7 x39 shown |
| logo | 91 | 4, 15, 7 x88 (one empty frame) |
| title-01 | 150 | 4 x2 (two palette frames), 15, 7 x148 |
| scroll | 26 | 4, 15, 7 x22 (two empty frames) |
| title-02 | 4 | 4, 15, 7 x2 |

  No file uses COLOR64, LC, BLACK, COPY or PSTAMP.

## Pacing (`DAT_0012eab4`, `DAT_0009e704`)
- **Frame wait.** `movie_wait_frame_50430` waits until the tick counter `DAT_0012eab4` reaches the frame delay `DAT_0009e704` (unsigned compare), then **resets the counter to 0**. So a frame is shown `delay` ticks after the previous one was shown, and the first frame of a movie is shown at once (the counter has run since the last reset). The wait sits between decode and blit.
- **Tick rate.** The counter is incremented by the HMI timer event (`hmi_timer_add_event_5d093(0x78, timer_tick_isr_3411a)` = **120 Hz**) when sound or music is available, otherwise by the int 8 handler at 119.06 Hz. The port uses the contract's 119.06 Hz clock.
- **Delay.** `DAT_0009e704` is set **only** by cue op `A`. It is global (data value 5) and is not reset per movie, so the intro's frame 0 (whose `A 300` comes at frame 1) inherits the previous movie's delay. The port keeps this as `g_fli_frame_delay`.
- **Order.** The original loop is cue(n), read, decode, callback, wait, palette, blit. The port runs cue(0) in the first `fli_next_frame` and cue(n+1) at the end of `fli_next_frame(n)`, which is right after the original's blit. That makes sound timing and the delay used by `fli_frame_due` exact.
- **Two image buffers.** The cue's subtitle ops write into the decode buffer, so `fli_pixels` / `fli_blit` return a copy taken before that cue: the image the original blitted.
- **Abort.** The original's wait aborts on a key or click when arg 1 is set, and on any input change when `DAT_0009e44e` is set; it still shows the pending frame and then leaves the loop. The port's equivalent is `fli_abort()`: the frame becomes due at once, `fli_next_frame` shows it without running the next cue, and every later call returns false. The front end polls the input.
- **Durations at the original's speed (port clock):**
  - level win / lose: 3.3 s;
  - intel: 3.3 s;
  - logo: 7.5 s;
  - title-01: 6.2 s;
  - outro (placeholder): 2.6 s;
  - the CD intro script over 2944 frames: 17815 ticks = **149.6 s**.

## Cue scripts (`cue_script_step_17d80`, tables in `gen/fli_tables.h`)

**Records.** Each record is 7 bytes: {u16 frame, char op, i16 arg, u16 unused}. The index is `DAT_000938f4`, set to 0 by `fli_play`. On each call every record whose frame equals the frame counter `DAT_0012eac0` is executed. When the next record does not match: if a `Z` loop is active (`DAT_0012eaba` = track + 1) and `music_song_done()`, the track is restarted.

**No terminator.** The scripts are followed by other code-segment data. Each table therefore includes the first never-matching record (a frame below the previous one, or 0xffff), so the original's look-ahead read is reproduced exactly.

The ops come from the jump table at 0x17c90 (op − 0x41, 58 entries). Lower case is an alias for `ABELMRSTXZ` only.

| op | effect |
|---|---|
| `A` | frame delay `DAT_0009e704` = arg (ticks) |
| `B` | `music_stop`, `music_load_bank(arg)` (`music1-*` = the front-end songs) |
| `E` | `sound_stop_all`, `sound_load_bank(arg & 0xff)`: speech / effect banks `snds1..13` (**this replaces the game bank: set 0 must be reloaded before a level, which game_main does**) |
| `K` | clear the subtitle strip |
| `L` | (dead preload) then as `M` |
| `M` | `music_play_track(arg)`, loop off |
| `O` | subtitles on (only when `DAT_000938fc`) |
| `P` | subtitles off (`movie_free_23700`) |
| `Q` | subtitle = language text entry `arg` (`DAT_000adce8[arg]`, i.e. `text_get`) |
| `R` | if sfx available: `sound_play_sample(0, arg, -1)` (looping) |
| `S` | if sfx available: arg 0 → `sound_stop_all`, else `sound_play_sample_loud(0, arg)` |
| `T` | if sfx available: arg 0 → `sound_stop_all`, else `sound_stop_sample(0, arg)` |
| `X` | `music_stop`, loop off |
| `Z` | `music_play_track(arg)`, loop = arg + 1 |
| others (`I` appears once, at intro frame 421) | nothing |

**What the scripts do:**
- **logo:** bank 11, LOGO.RAW at frame 1.
- **intel:** bank 5, PENTEL.RAW at 1.
- **title-01:** bank 10, DOORLITE / CARPBLOB 13 times.
- **level won:** music bank 1, track 2 at 1; bank 6 sample 1 at frame 200 (beyond the placeholder's 40 frames).
- **level lost:** the same with bank 8 at frame 50.
- **outro:** bank 9, music track 2, samples at 30 / 43 / 130.
- **scroll:** delay 1 only.
- **intro:**
  - subtitles 0..16, in sync with the speech VOC1..VOC16 of banks 1 and 2;
  - music bank 1 tracks 1 → 2 (frame 290) → 3 as a `Z` loop (frame 996) → 2 (frame 2930);
  - effect banks 3, 4 and 12.

  All 56 sample references of the intro script lie inside the bank loaded before them.

## Subtitles (`movie_subtitle_*`)
- **When.** `DAT_000938fc` (`g_fli_subtitles_enabled`) is set by `sound_initialise_34140` when the language is not English, or when it is English and there is no digital sound. The platform must set it.
- **Op `O`.** Loads `data\screens\sfont1` (resource list 0x1f9f0), sets the font colour to the nearest white of `DAT_000adf90`, places the strip at back buffer + 0xe100 (row 180, 0x4b00 bytes) and switches the blit to back buffer + 0x1a40 (21 rows lower; `DAT_000938fd`).
- **Op `Q`.** Draws the text with `ui_draw_text_58ab0` in the clip rect (10, 180, 300, 50). On every applied palette change the colour is re-picked and the text redrawn.
- **Port.** The decode buffer is 320x240 (76800 bytes), so the strip lives where the original's did.
- **Visible consequence.** With a 320x200 FLI the decoder overwrites strip rows 180..199. The test image shows the logo's light streaks erasing part of the first subtitle line. The CD intro was presumably letterboxed, so the original never showed this.

## Palette
- **`g_display_palette6` is "the DAC".** `palette_fade_start(target, steps)` reads it (`vga_read_palette_61ec8`).
- **Each `palette_fade_step()`** sets `entry = start + (int16)(target − start) * k / steps` (idiv truncation) for k = 0 .. steps. That is steps + 1 frames; k = 0 repeats the start palette, as the original's first vsync does. The call that sets k = steps returns false.
- **No ticks during a fade.** The original blocks inside the fade, so the game loop must not tick while `palette_fade_active()`.
- **Incremental mode.** Only `palette_effect_update_33010` uses it (4 steps toward `g_palette6`). The first call reads the DAC and uses k = 0; each later call uses k + 1; the call that reaches k = steps clears `DAT_0009e860`. It returns k.
- **`palette_effect_update`, `Config.fade_stage`:**
  - 0 and 1: `palette_fade_out_and_load` (16-step fade to black, `title_flag_a = 0`, `data/palette.dat` → `g_palette6`);
  - 2: `palette_effect = 1`;
  - 3: the effect.
- **Effects** (entries 1..255 into `DAT_000b6b80`, then reset + DAC + effect 1):
  - 2 red;
  - 3 magenta;
  - 4 blue;
  - 5 dark;
  - 6 bright;
  - 7 grey.

  **Effects 5 and 6 compute red from the blue component** (`mov dl, [edx+eax+2]`); kept as is.
- **The return fade.** Effect 1 runs 5 ticks: k = 0 shows the flash palette once more, then k = 1..4.
- **`mapmode_palette_save` / `restore`** keep `GameState.mode_3d` in `DAT_00093fc0`. Leaving and entering the stereo modes (`stereo_mode_leave_2ff10` / `stereo_mode_enter_2ff50`: video mode and the anaglyph palette, renderer / platform side) go through `g_hook_stereo_leave` / `g_hook_stereo_enter`.
- **`title_screen_show`** loads `smatitle.pal` **into `g_palette6`**, which is the game palette. The next level's stage 0 reloads `palette.dat`.

## Verification (`fli_test`, mc_unit_test with `${MC_SIM_ALL}` + fli / palette_fx / ui_draw)

`fli_test` exits 0 and the build has zero warnings (clean rebuild). The `mcengine` library also builds with both files.

1. **The ten `intro\*.dat` through the contract.**
   - Frame counts: frames − 1 shown each, e.g. logo 90, title-01 149.
   - Size 320x200; the chunk census above.
   - 0 unknown chunks.
   - Frame 0 reports a palette change, and its palette equals the file's COLOR256 bytes.
   - Every shown frame is pixel-identical to the **independent translation of the memory-stream player** (`fli_chunk_play`) over the same file.
   - The two unshown chunks per file decode cleanly, and the ring frame gives frame 0 back (all 10 files).
   - The blit is clipped correctly (40x80 corner).
   - Pacing: logo delay 10 → not due at 9, due at 10.
   - Abort semantics, and a missing / non-FLIC file → nullptr.
2. **Memory player on `data\screens\globe / scroll / timer.dat` and `intro\title-02.dat`.** It decodes 31 / 27 / 4 / 5 frames, ending exactly at the end of each file.
3. **Cue scripts.**
   - Every record decodes to a known op with ascending frames, and every table ends with a stopper.
   - Name lookup is case- and slash-insensitive.
   - The intro script on a synthetic 3000-frame FLIC with a recording backend: 55 events; delay 5 at the end; music 1 → 2 → 3 (Z) → 2; the speech VOC1.. at the scripted frames (first: frame 20 VOC1 from bank 1, frame 129 VOC6 from bank 2). All 56 sample references are in range.
   - The real level-won / lost / logo / intel / title-01 / outro movies produce the events listed above, at the frames listed above.
4. **Palette.**
   - The 16-step fade against the formula for all 768 entries at every k; truncation toward zero checked explicitly (63 → 0, k = 1 gives 60, not 59); 17 calls; it ends exactly on the target.
   - Black fade: 33 calls.
   - Incremental mode: k = 0, 1, .., 4, then a restart at 0.
   - Effects 2..7 against hand formulas for entries 1..255, each followed by the 5-tick return.
   - Effects 0 and 8 do nothing.
   - The stage machine 0 → 3, including the fade-in from black (k = 1 gives `pal / 4`).
   - Mapmode save / restore, including SIRDS (no enter hook) and the forgotten saved value.
   - `title_screen_show`: image in the frame buffer, 33 fade frames, flags.
5. **Subtitles.**
   - A script O / Q 4 / Q 16 / K / P on logo.dat: the strip is active, both texts are drawn (2216 / 1946 pixels), and every strip pixel reaches the bottom 41 screen rows through the shifted blit.
   - The strip is gone after `P`; `fli_subtitle_free` works after an aborted intro.

**Images checked by eye** (`build_C/Debug/*.ppm`; PNG copies alongside): the pentium logo, the Bullfrog logo, the "Magic Carpet" title, the scroll dialog background, the globe, the hourglass, the smatitle CD image, and the subtitle frame (correct text and font, with the overwrite artefact described above).

**Not verified against the original at run time.** There is no DOSBox capture of a front-end movie; the evidence is the disassembly plus the cross-check of the two independent decoders.

## Deviations and gaps
- **Bounds checks.** The port adds bounds on all reads and writes:
  - COLOR writes beyond 768 bytes are dropped;
  - decoder writes are clipped to w * h;
  - reads past a chunk return 0;
  - a frame with a bad magic or size >= 0xfa00 ends the movie (the original prints "unknown frame" / "too big" forever);
  - BRUN count 0 ends the line (the original loops forever);
  - BRUN −128 copies 128 bytes (the original's byte abs makes it unusable);
  - steps 0 in a fade sets the target (the original divides by zero).

  None of these cases occurs in the shipped files.
- **Kept as in the original:**
  - a COPY chunk replaces its size with w * h, so the next sub-chunk would be read 6 bytes early;
  - SS2 "last byte" ends the line in the file player, and in the memory player reuses the word as the packet count;
  - the 0xF100 prefix branch of 50600 is unreachable;
  - the COLOR64 values are not scaled.
- **640x480.** The original decodes into the 320-pitch back buffer and blits 320x200 (`vga_copy_320x200_610f0`). `fli_blit(fb, x, y)` copies the 320x200 image wherever the front end wants it.
- **Not ported:**
  - the callback `DAT_0012ea9c` (the front end composites the scroll dialog itself: colour-0 replacement is idempotent, so compositing the player's image each frame gives the original's result);
  - the VFX1 VIP palette mirror (`vfx1_vip_set_palette_50180`) and its stereo-page writes;
  - `vga_wait_vsync_65b10`.
- **Shared state.** The palette buffer `DAT_0012e798`, the delay, the cue index and the subtitle state are module globals, as in the original. One player at a time is the intended use.

## Hooks
**Declared and owned by palette_fx:**
- `g_hook_stereo_leave` (`stereo_mode_leave_2ff10`; when null the display palette is set to `g_palette6`);
- `g_hook_stereo_enter` (`stereo_mode_enter_2ff50`, the anaglyph palette; when null nothing happens).

**Installed:** none.

## Extra functions (additions to the contract headers)
- **fli.h:**
  - `fli_open_memory`;
  - `fli_abort`;
  - `fli_abort_on_input`, `fli_apply_palette`;
  - `FliCueScript` / `fli_cue_for_file` / `fli_set_cue_script`;
  - `g_fli_frame_delay` (`DAT_0009e704`);
  - `g_fli_subtitles_enabled` (`DAT_000938fc`);
  - `fli_subtitle_text` / `fli_subtitle_active` / `fli_subtitle_free`;
  - `fli_chunk_census` / `fli_unknown_chunks`;
  - `FliChunkState` / `fli_chunk_set_dest` / `fli_chunk_play`.
- **palette_fx.h:**
  - `palette_fade_incremental`, `palette_fade_reset`;
  - `palette_fx_set_game_dir`;
  - `palette_effect_update`, `palette_effect_buffer`;
  - `palette_fade_out_and_load`;
  - `mapmode_palette_save` / `restore`;
  - `title_screen_show`.

## TODO(port) call sites
None in these files.

## Requested shared-file changes
1. **`src/CMakeLists.txt`:** `palette_fx.cpp` has no dependencies beyond `mc_globals` / `mcfile`. Add it to the simulation core so `player.cpp` can call it:
   ```cmake
   set(MC_SIM_CORE mcengine/thing.cpp ... mcengine/text.cpp mcengine/palette_fx.cpp)
   ```
2. **`player.cpp` `game_tick_sim()` (line ~1201)**, replacing the palette part of the TODO:
   ```cpp
   #include "palette_fx.h"
   ...
   void game_tick_sim() {
       if (g_state->mode_3d == 0) palette_effect_update();                  // palette_effect_update_33010
       // TODO(port): texture_anim_update_4be50 (not paused)
   ```
   - **What it writes.** `palette_effect_update` writes `Config.fade_stage` / `palette_effect` and, in stages 0 and 1, `GameState.title_flag_a` (+0x245) = 0, exactly as the original does.
   - **Movie 0.** The snapshot has +0x245 = 0 and the port's Config starts with `fade_stage` 0, so `reference_test` should be unaffected. Please run it.
   - **Level start.** game_main sets `Config.fade_stage = 0` at the start of every level loop (0x32f9c, task D).
3. **`player.cpp` `player_set_input_mode()` (line ~114)**, replacing the mapmode part of the TODO. The original order is cursor first, then 30350; in the other branch cursor, `4a000`, then 303b0:
   ```cpp
           if (mode == 2) {
               // mouse_cursor_set_sprite_5ba5c(pointers + 6): book pointer (front-end / platform)
               mapmode_palette_save();                                          // mapmode_palette_save_30350
           } else {
               // mouse_cursor_set_sprite_5ba5c(pointers): default pointer
               if (g_hook_input_mouse_center) g_hook_input_mouse_center();      // input_mouse_center_4a000
               mapmode_palette_restore();                                       // mapmode_palette_restore_303b0
           }
   ```
4. **mcport `main.cpp`** (integrator):
   ```cpp
   #include "palette_fx.h"
   #include "fli.h"
   palette_fx_set_game_dir(game_dir);
   g_fli_subtitles_enabled = (g_cfg->language != 0 || !g_sound_available);   // sound_initialise_34140
   palette_display_set(g_palette6);              // instead of uploading g_palette6 once
   // every displayed frame:
   if (palette_fade_active()) palette_fade_step();     // and do NOT tick the game / fe_frame this frame
   if (palette_display_dirty()) {
       uint8_t rgb[768]; mc_palette_to_rgb(g_display_palette6, rgb); plat.set_palette(rgb);
       palette_display_clear_dirty();
   }
   // task D's hooks:
   g_hook_game_fade_out = [] { palette_fade_start(nullptr, 0x10); };
   g_hook_game_title_screen = [] { title_screen_show(s_game_dir, s_fb); };   // then step the fade as above
   ```
   Task D's `game_before_frontend` should call `mapmode_palette_save()` where its TODO is.
5. **`input.cpp` F10 (`INPUT_REQ_3D_MODE_ON` / `RESTORE`):** the renderer side should install `g_hook_stereo_enter` / `g_hook_stereo_leave` when anaglyph is ported. Nothing to change now.
6. **`gen/fli_tables.h` is new.** It was generated from the new spec `tables/fli.tables` with `python tools/port/gen_exe_tables.py fli`; no other generated file was touched.

## Corrections to ENGINE.md / names
- **"FLI player" paragraph (Front end) is wrong about the call chain.**
  - `fli_play_508f0(abort_on_input, apply_palette, cue_script)` reads frames with `fli_error_unknown_frame_50520` and decodes / presents them with `movie_frame_present_50600`. It decodes **types 4, 7, 11, 12, 13, 15, 16, 18** (COLOR256 is not skipped).
  - `fli_next_frame_50dfd` / `fli_decode_frame_50e88` (export: `flic_play_chunk_50dfd`) are the separate memory-stream player. It decodes only 7 and 15 and skips 4; it is used by the texture animations and the front-end animations.
  - The path goes in `DAT_0009e708`.
- **Suggested names:**

| current | suggested |
|---|---|
| `fli_error_unknown_frame_50520` | `fli_read_frame_50520` |
| `movie_frame_present_50600` | `fli_decode_present_frame_50600` |
| `movie_wait_frame_50430` | `fli_wait_frame_50430` |

- **Globals:**

| global | meaning |
|---|---|
| `DAT_0009e44c` | apply-palette flag (arg 2) |
| `DAT_0012eabe` | abort on key / click (arg 1) |
| `DAT_0009e44e` | abort on any input change (`input_changed_34090`) |
| `DAT_0012eabc` | abort flag |
| `DAT_0012eac0` | frame counter |
| `DAT_0012eaa0..aa` | header |
| `DAT_0012e680..` | frame chunk header |
| `DAT_0012e798` | FLI palette buffer |
| `DAT_0012e698` | debug chunk-name string |
| `DAT_0009e450` | file position |
| `DAT_000938f4` | cue index |
| `DAT_0009e704` | frame delay in ticks (initial 5, set only by cue op `A`) |
| `DAT_0012eaba` | `Z` loop track + 1 |
| `DAT_000938fc` | subtitles enabled |
| `DAT_000938fd` | subtitle strip active / blit shifted by 0x1a40 |
| `DAT_0009390a` | subtitle initialised |
| `DAT_0009390c` | current subtitle text |
| `DAT_000adfd0` | subtitle font (colour1 at `DAT_000adfd6`) |
| `DAT_000b2e04` / `b2e0c` | strip pointer / length |

- **`fli_play` shows frames 0 .. frames − 2.** The FLIC header is the 12-byte Bullfrog variant.
- **`fli_file_play_5c264` is unreferenced dead code**, not merely uncalled from C.
- **The tick counter `DAT_0012eab4` runs at 120 Hz** (HMI timer event 0x78) whenever sound or music is available; it uses the 119.06 Hz int 8 handler only without sound.
- **`vga_palette_fade_61510` arithmetic and return value** are as in "Palette" above; the third argument selects the incremental mode.
- **`palette_effect_update_33010`:** the effect list and the blue-for-red bug of effects 5 and 6.
- **`title_screen_show_32db0`** loads `data/smatitle.dat` / `smatitle.pal`, the latter into the game palette `DAT_000adf90`.
- **`cue_script_step_17d80`:** the opcode table above. In the export's comment, `K`/`L` "play music" should read `L`/`M`, and `Z` is the looping track.
- **FORMATS.md:**
  - the 12-byte FLIC header;
  - `intro\*.dat` placeholders (six identical files) in this package;
  - `data\screens\globe/scroll/timer.dat` have inconsistent frame / SS2 sizes and are playable only by the memory-stream player.

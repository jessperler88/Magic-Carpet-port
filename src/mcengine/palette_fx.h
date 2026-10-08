// Display palette, fades and in-game palette effects (round 5, task C).
// vga_palette_fade_61510 (blocking DAC fade with vsync waits in the original) becomes a frame-stepped
// fader; palette_effect_update_33010 (damage / mana / spell flashes, Config.palette_effect) and
// mapmode_palette_save_30350 / mapmode_palette_restore_303b0 work on the display palette.
//
// CONTRACT between task B (front end, caller), the integrator (mcport uploads the palette) and task C
// (owner): the declarations below are fixed for round 5; task C implements them and may add more.
#pragma once
#include <cstdint>
#include "render.h"

// The palette the platform shows: 768 bytes of 6-bit DAC values. mcport uploads it when
// palette_display_dirty() is true and then calls palette_display_clear_dirty().
extern uint8_t g_display_palette6[768];
bool palette_display_dirty();
void palette_display_clear_dirty();
void palette_display_set(const uint8_t *pal6);          // copy + mark dirty

// Stepped fade of the display palette from its current contents towards `target6` (nullptr = black)
// in `steps` frames (the original: 0x10 steps fading out, 0x20 fading in). palette_fade_step() is
// called once per frame and returns true while the fade is still running.
void palette_fade_start(const uint8_t *target6, int steps);
bool palette_fade_step();
bool palette_fade_active();

// ==== additions by task C (round 5) =================================================================
// Report: docs/analysis/port_fli.md. "The DAC" of the original is g_display_palette6 here.
//
// vga_palette_fade_61510(target, steps, incremental): start = the DAC, then for k = 0 .. steps
// (steps + 1 vsync frames, k = 0 shows the start palette again):
//     entry = start + (int16)(target - start) * k / steps        (idiv: truncation toward zero)
// palette_fade_start / palette_fade_step are that loop with one k per call: every call sets one
// palette; the call that sets k == steps (the target) returns false and ends the fade. steps is a
// byte in the original (0 would divide by zero; the port sets the target at once).
// The game loop must not tick while palette_fade_active(): the original blocks inside the fade.
//
// The incremental mode (third argument 1, only palette_effect_update_33010 uses it): each call is one
// step; the first call (DAT_0009e860 == 0) reads the DAC and uses k = 0, later calls k + 1; the call
// that reaches k == steps clears DAT_0009e860. Returns k (the original returns the step counter).
int  palette_fade_incremental(const uint8_t *target6, int steps);
void palette_fade_reset();                      // vga_palette_fade_reset_61718: DAT_0009e860 = 0
// Where palette_fade_out_and_load_32e40 / title_screen_show_32db0 read their files (game dir).
void palette_fx_set_game_dir(const char *game_dir);

// palette_effect_update_33010: the first call of game_tick_update_32e80 (only when
// GameState.mode_3d == 0 - the caller tests it). State machine on Config.fade_stage:
//   0, 1: palette_fade_out_and_load_32e40 (blocking 16-step fade to black, GameState.title_flag_a = 0,
//         data/palette.dat -> g_palette6), stage + 1
//   2:    Config.palette_effect = 1, stage 3
//   3:    Config.palette_effect: 0 nothing; 1 incremental 4-step fade of the DAC to g_palette6, at
//         k == 4 palette_effect = 0; 2..7 build a tinted copy of g_palette6 (entries 1..255; entry 0
//         keeps the buffer's previous value) into DAT_000b6b80, reset the incremental fade, put it on
//         the DAC, palette_effect = 1:
//           2 red      R = 63
//           3 magenta  R = min(R + 0x30, 63), B = min(B + 0x40, 63)
//           4 blue     B = 63
//           5 dark     R = clamp(B - 0x20) (sic: reads B), G = clamp(G - 0x20), B = clamp(B - 0x20)
//           6 bright   R = clamp(B + 0x30) (sic), G = clamp(G + 0x20), B = clamp(B + 0x20)
//           7 grey     R = G = B = (R + G + B) / 3
void palette_effect_update();
const uint8_t *palette_effect_buffer();        // DAT_000b6b80 (tests)
// palette_fade_out_and_load_32e40: palette_fade_start(nullptr, 0x10), GameState.title_flag_a = 0,
// data/palette.dat into g_palette6 (needs palette_fx_set_game_dir; skipped without it).
void palette_fade_out_and_load();

// mapmode_palette_save_30350 (spell book opened, front end entered): when GameState.mode_3d != 0
// remember it (DAT_00093fc0), leave the stereo mode (stereo_mode_leave_2ff10: hook, by default the
// display palette is reset to g_palette6 - what 2ff10 does besides the video mode), put g_palette6 on
// the DAC and set mode_3d = 0. mapmode_palette_restore_303b0: mode_3d = the remembered value; when it
// is 1 (anaglyph) stereo_mode_enter_2ff50 (hook: the anaglyph palette, renderer side); forget it.
// (The VFX1 VIP stereo-page port writes of 30350 / 2ff50 are not ported.)
void mapmode_palette_save();
void mapmode_palette_restore();
extern void (*g_hook_stereo_leave)();          // stereo_mode_leave_2ff10 (null: display = g_palette6)
extern void (*g_hook_stereo_enter)();          // stereo_mode_enter_2ff50 (null: nothing)

// title_screen_show_32db0 (game_main before a level): data/smatitle.dat (64000 bytes, RNC) into `fb`
// (copied as 320x200 rows at the top-left; the original loads it into the back buffer and blits 200
// rows in 320x200 or 480 rows in 640x480 - the latter shows garbage below), data/smatitle.pal into
// g_palette6 (sic: the game palette, reloaded by the level's palette_fade_out_and_load), then a
// 32-step fade from the DAC to it (palette_fade_start: the caller steps it), GameState title_flag_a
// = 1, title_flag_b = title_flag_c = 0. False when a file is missing (nothing changed then).
bool title_screen_show(const char *game_dir, const FrameBuffer &fb);

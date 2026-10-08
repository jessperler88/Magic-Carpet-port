// FLI player (round 5, task C): Autodesk FLIC files of the front end and the level-result screens
// (intro/*.dat, levelw1/levelw2/levelose.dat), fli_play_508f0 / fli_next_frame_50dfd /
// fli_decode_frame_50e88 / fli_present_frame_50600, the asm player fli_file_play_5c264 and the cue
// scripts that sync sound / speech to frames (fli_event_script_17d80 / cue_script_step_17d80).
//
// CONTRACT between task B (front end, caller) and task C (owner): the declarations below are fixed
// for round 5. Task C implements them and may add functions; nobody changes these signatures.
// Frame-stepped, no blocking loops: the caller asks for the next frame when fli_frame_due() says so.
#pragma once
#include <cstddef>
#include <cstdint>
#include "render.h"

struct FliPlayer;   // opaque, owned by fli.cpp

// Opens <game_dir>/<rel_path> (RNC-compressed or raw FLIC). Returns nullptr when missing / invalid.
FliPlayer *fli_open(const char *game_dir, const char *rel_path);
// Port: a directory searched first by file name (e.g. the CD's CARPET/INTRO with the full movies; some
// packages ship intro / outro / levelw1 / levelw2 / levelose as copies of intel.dat). "" / nullptr = off.
void        fli_set_movie_dir(const char *dir);
const char *fli_movie_dir();
void       fli_close(FliPlayer *f);
int        fli_frame_count(const FliPlayer *f);
int        fli_width(const FliPlayer *f);
int        fli_height(const FliPlayer *f);
// Frame delay of the file in game timer ticks (the original paces by DAT_0012eab4).
int        fli_frame_delay_ticks(const FliPlayer *f);
// True when the frame after the current one should be shown at timer tick `now_ticks`
// (119.06 Hz PIT ticks, the same clock as Config+4).
bool       fli_frame_due(const FliPlayer *f, uint32_t now_ticks);
// Decodes the next frame into the player's own 8-bit image and palette. Returns false after the
// last frame. `palette_changed` (optional) is set when the frame carried a COLOR chunk.
bool       fli_next_frame(FliPlayer *f, uint32_t now_ticks, bool *palette_changed);
int        fli_current_frame(const FliPlayer *f);      // 0-based, -1 before the first frame
const uint8_t *fli_pixels(const FliPlayer *f);         // width * height palette indices
const uint8_t *fli_palette6(const FliPlayer *f);       // 768 bytes, 6-bit VGA DAC values
// Copies the current image into `fb` at (x, y), clipped (the original's present callback).
void       fli_blit(const FliPlayer *f, const FrameBuffer &fb, int x, int y);

// ==== additions by task C (round 5) =================================================================
// Report: docs/analysis/port_fli.md. How the contract maps onto fli_play_508f0(abort_on_input,
// apply_palette, cue_script) (the only FLI player the game uses; the file-stream asm player
// fli_file_play_5c264 has no caller anywhere in the image):
//
//   fli_play_508f0 loop                        port
//   ------------------------------------------ ------------------------------------------------------
//   frame = 0, cue index = 0, music loop = 0   fli_open (attaches the file's cue script by name)
//   while (!abort && frame < frames - 1) {     fli_next_frame returns false after frame frames - 2
//     cue_script_step_17d80(script)            cue of frame 0: first fli_next_frame; cue of frame n+1:
//                                              at the end of fli_next_frame(n) (= right after the blit,
//                                              as in the original)
//     fli_read_frame_50520 + decode (50600)    fli_next_frame
//     wait DAT_0012eab4 >= DAT_0009e704        fli_frame_due (delay = the cue's 'A' value)
//     palette (when apply_palette) + blit      caller: palette_display_set(fli_palette6) when
//   }                                          *palette_changed and fli_apply_palette(); fli_blit
//
// fli_pixels / fli_blit give the image as the original blitted it, i.e. before the next frame's cue ran
// (its subtitle ops write into the decode buffer; fli_subtitle_text / _active report the live state).
// The first frame is due at once (the original's tick counter is far beyond the delay when a FLI
// starts: it is only reset by the frame wait). fli_frame_count() is the header value; the original
// shows frames 0 .. count - 2 (the last frame and the ring frame are never shown).

// fli_open on a FLIC already in memory (copied; RNC-packed data is not unpacked). `name` selects the
// cue script as in fli_open (nullptr / unknown = none); `game_dir` is where the cue ops load banks.
FliPlayer *fli_open_memory(const char *game_dir, const uint8_t *data, size_t len, const char *name);
// Abort as the original's frame wait does (key / mouse when fli_abort_on_input(), any input change
// when DAT_0009e44e): the pending frame becomes due at once, fli_next_frame shows it without running
// the next cue, and every later call returns false.
void fli_abort(FliPlayer *f);
// fli_play_508f0's first two arguments for this file as the original's callers pass them:
// abort_on_input = DAT_0012eabe (any key / mouse click ends the movie), apply_palette = DAT_0009e44c
// (the COLOR chunks reach the DAC; false for intro\scroll.dat, whose palette is the dialog's).
// intro\intro.dat: abort only after the first viewing (fe_screen_intro_movie_54900: DAT_0012ed35 & 2)
// - the table says false, the front end decides.
bool fli_abort_on_input(const FliPlayer *f);
bool fli_apply_palette(const FliPlayer *f);
// Cue scripts (exe tables, gen/fli_tables.h): 7-byte records {u16 frame, char op, i16 arg, u16}.
struct FliCueScript {
    const char    *file;            // "intro\\intro.dat" (as the original's sprintf path)
    const uint8_t *records;         // record bytes, 7 per record
    int            count;           // records (the last one is the never-matching look-ahead record)
    uint32_t       addr;            // address in carpet.exe
    bool           abort_on_input;  // fli_play_508f0 arg 1
    bool           apply_palette;   // fli_play_508f0 arg 2
};
// The script the original plays with `rel_path` (case-insensitive, '/' or '\\'), or nullptr.
const FliCueScript *fli_cue_for_file(const char *rel_path);
// Replace the script fli_open attached (nullptr = no cue events). Call before the first frame.
void fli_set_cue_script(FliPlayer *f, const FliCueScript *script);
// DAT_0009e704: frame delay in timer ticks, set by cue op 'A'. Global as in the original (a script
// that sets no delay at frame 0 inherits the previous movie's; initial value 5).
extern int32_t g_fli_frame_delay;
// DAT_000938fc: subtitles enabled (sound_initialise_34140: language != English, or English without
// digital sound). Cue op 'O' only starts the subtitle strip when set. The platform sets it.
extern uint8_t g_fli_subtitles_enabled;
// Subtitle state (movie_subtitle_* 0x23600..0x23740): text shown in the strip (nullptr = none) and
// whether the strip is active (DAT_000938fd: fli_blit then shows the image from byte 0x1a40 on, i.e.
// 21 rows lower, so that the strip at rows 180..239 of the decode buffer reaches the screen bottom).
const char *fli_subtitle_text();
bool        fli_subtitle_active();
// movie_free_23700: subtitle strip off, font freed (fe_screen_intro_movie_54900 calls it after the
// intro whether or not the script reached its 'P').
void        fli_subtitle_free();
// Statistics of the frames decoded so far (tests): sub-chunks seen per type (index = type, 0..31;
// types >= 32 count in [0]), and how many were of a type the original does not decode.
const uint32_t *fli_chunk_census(const FliPlayer *f);
int            fli_unknown_chunks(const FliPlayer *f);

// ---- memory-stream chunk player (flic_set_dest_50de0 / flic_play_chunk_50dfd / fli_decode_frame_50e88)
// Used by the animated textures (sprite_cache.cpp has its own copy) and by the front-end animations
// (fe_main_menu_animate_53c40, fe_flic_loop_frame_56670: data\screens\globe.dat, timer.dat,
// scroll.dat, intro\title-02.dat held in memory). One call plays the chunks at `off` up to and
// including the next 0xF1FA frame: 0xAF12 headers set width / height, sub-chunks 7 (SS2, the end of
// its data decides where the next chunk starts - the size field is not used), 15 (BRUN) are decoded
// into `dest` (pitch = width), 4 and every other type are skipped by size. Returns the offset after
// the frame, or 0 for any other chunk magic (the original returns NULL), or when the data ends.
struct FliChunkState {
    uint16_t width = 0;         // DAT_0009e45e (pitch and row length)
    uint16_t height = 0;        // DAT_0009e460 (BRUN rows)
    uint8_t  transparent = 0;   // DAT_0009e468: 1 = BRUN / SS2 last-byte writes keep dest where the source is 0
};
void   fli_chunk_set_dest(FliChunkState *s, int width, int height);   // flic_set_dest_50de0
size_t fli_chunk_play(FliChunkState *s, const uint8_t *data, size_t len, size_t off,
                      uint8_t *dest, size_t dest_len);                 // flic_play_chunk_50dfd

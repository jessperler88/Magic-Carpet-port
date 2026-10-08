// Game-side sound manager of carpet.exe: the per-sound-id request table of sound_request_49720, the
// 32-channel bookkeeping of the sample layer (0x4f6f0..0x4f8a0, 0x5cbb0..0x5cea0, 0x627d0), the fade-in /
// fade-out steppers (0x4dfc0..0x4e400), sound_update_494b0 (one call per tick, after the simulation)
// and the music plumbing (music_update_1f800 mood fades, music_play_track_5c0a0, music_stop_1f960,
// music_fade_out_begin_49cd0). Everything below the HMI SOS API (hmi_digi_* / hmi_midi_* / hmi_timer_*)
// is the SoundBackend below: null by default, the platform (mcport, SDL audio) implements it.
// Owner: sound.cpp. Report: docs/analysis/port_sound.md. Bank format: mcdata/sndbank.h.
#pragma once
#include <cstdint>
#include "thing.h"
#include "input.h"      // g_sound_available / g_sound_on / g_music_available / g_music_on (DAT_0009e320/321/30c/30d), INPUT_REQ_*

struct mc_sndbank;

// ---- platform interface ---------------------------------------------------------------------------
enum MusicTimer { MUSIC_TIMER_MOOD = 0, MUSIC_TIMER_FADE_OUT = 1 };

struct SoundBackend {
    virtual ~SoundBackend() = default;
    // hmi_start_sample_64e7c: start `len` bytes of 8-bit unsigned mono PCM at `rate` Hz. volume 0..0x7fff,
    // pan 0..0xffff (0x7fff = centre, larger = right), loop_count = HMI wLoopCount (-1 = forever, 0 = once),
    // hmi_flags = the wSampleFlags word the game passes: 0x100 _VOLUME (use `volume`, every call has it), 0x200
    // _PANNING (use `pan`), 0x4000 _LOOPING (use `loop_count`); 0x300 positional one-shot, 0x4300 fade-in loop,
    // 0x4100 sound_play_sample loop, 0x100 cue-script sample. owner / sample are the game's (thing, sound id) pair, for
    // logging only. Returns a voice handle >= 0, or -1 when nothing could be started.
    virtual int  start(int owner, int sample, const uint8_t *data, uint32_t len, int rate, int volume, int pan,
                       int loop_count, int hmi_flags);
    virtual void stop(int handle);                      // hmi_stop_sample_65485
    virtual void set_volume(int handle, int volume);    // hmi_set_sample_volume_64d38, 0..0x7fff
    virtual bool is_playing(int handle);                // !hmi_sample_done_6291e
    // Port addition (no HMI counterpart): drop every voice at once, without a ramp, because the sample
    // memory the voices point into is about to be freed (sound_load_bank replacing the bank).
    virtual void flush();
    // Music: one HMI MIDI (HMP) file per track from data/music<set>-<device>.dat.
    virtual bool music_play(int track, const uint8_t *song, uint32_t len);   // snd_midi_init_song_5e53a + snd_midi_start_song_5eab0; false = error
    virtual void music_stop();                          // snd_midi_stop_song_5eb38 + snd_midi_all_notes_off_60035
    virtual bool music_done();                          // music_song_done_5cf40: the song has run to its end
    virtual void music_set_volume(int volume);          // snd_midi_set_volume_5ef56: master volume 0..0x7f
    virtual void music_set_layer_volume(int volume);    // controller 7 = volume on MIDI channels 3, 4, 5 (the combat layer), 0..0x7e
    // hmi_timer_add_event_5d093 / hmi_timer_remove_event_5d3a9: the game wants music_fade_tick(which) called
    // rate_hz times per second from now on (rate_hz = 0: stop calling it).
    virtual void music_timer(MusicTimer which, int rate_hz);
};
// The backend in use (never null: a do-nothing instance stands in until the platform installs one).
void          sound_set_backend(SoundBackend *backend);
SoundBackend *sound_backend();

// ---- registration / setup ---------------------------------------------------------------------------
// Installs g_hook_sound_request / g_hook_sound_fade (thing.h) and, when nothing else did, g_hook_input_platform
// with sound_handle_input_request. Clears the tables (sound_reset).
void sound_register_handlers();
// Clears the request table, the channel tables and the fade flags (fresh program state).
void sound_reset();
// sound_load_bank_5c990(set): data/snds<set>-<g_sound_quality>.dat/.tab into the manager's own bank
// (game_main always loads set 0 for the game; the cue script loads sets 1..13 for its speech). Returns
// false when missing (then the sample count is 0 and every request is dropped, as in the original).
bool sound_load_bank(const char *game_dir, int set);
// Use an externally owned bank instead (tests). Null = no bank.
void sound_set_bank(const mc_sndbank *bank);
const mc_sndbank *sound_bank();
int  sound_sample_count();                  // DAT_0012e246: records after the header record
extern uint8_t g_sound_quality;             // DAT_0009e328: 1 (default, 22050 Hz bank), 0 / 3 (11025 Hz banks); set by mem_init_pools_59500 from free memory
// The three sound / music requests input.cpp routes through g_hook_input_platform. Returns true when
// `what` was one of them (the platform's own hook should call this first and handle the rest).
bool sound_handle_input_request(InputPlatformRequest what, int arg);

// ---- the request side (called by game code through thing.h's sound_request / sound_fade) ---------------
// sound_request_49720(thing, player, sound): positional volume / pan for the local player's listener,
// then a mode 1 (restart) / 3 (play if idle) request in the per-sound-id slot, or a fade-in for the
// local-player loops (ids 1, 2, 5, 0x1f).
void sound_request_play(int thing, int player, int sound);
// sound_fade_player_sound_49c40(thing, player, sound): fade out loop `sound` (1, 2, 5 or 0x1f) when
// `player` is the local player.
void sound_fade_player_sound(int thing, int player, int sound);
// The positional half of sound_request_49720 on its own: volume (0x200..0x7fff) and pan (0..0xffff) of a
// sound at Thing `thing` for the listener players[local].thing. Returns false when the request would be
// dropped (thing flagged 0x80, farther than 48 cells, volume below 0x200). thing <= 0: 0x7fff / 0x7fff.
bool sound_locate(int thing, int *volume, int *pan);
// sound_priority_ok_49c20(new_volume, current_volume): a new request replaces the slot unless it is more
// than 8 quieter.
bool sound_priority_ok(int new_volume, int cur_volume);

// ---- per tick ----------------------------------------------------------------------------------------
// sound_update_494b0: fade steppers, then every pending request slot is played (the integrator calls it
// from game_tick_sim after the thing updates, player.cpp:1207). No-op when sound is off or paused.
void sound_update();
int  sound_playing_count();                 // sound_playing_count_49d50: active voices (0 when sound is off)
bool sound_sample_done(int owner, int sample);   // sound_sample_done_49d80: true when (owner, sample) is not playing

// ---- fades (0x4dfc0..0x4e400) --------------------------------------------------------------------------
void sound_update_fadein();                 // sound_update_fadein_4dfc0: +0x800 per tick up to the target
void sound_update_fadeout();                // sound_update_fadeout_4e2e0: -0x800 per tick, stop at <= 0x1000
void sound_request_fade_in(int owner, int sample, int mode, int target);   // sound_request_fade_in_4e0f0 (target 0..0x7f, mode 3 = start)
void sound_play_fade_in(int owner, int sample);                            // sound_play_fade_in_4e120
void sound_start_fade_out(int owner, int sample, int arg);                 // sound_start_fade_out_4e400

// ---- sample layer ---------------------------------------------------------------------------------------
void sound_restart_sample(int owner, int sample, int volume, int pan);     // sound_restart_sample_4f6f0 (mode 1)
void sound_play_if_idle(int owner, int sample, int volume, int pan);       // sound_play_if_idle_4f7a0 (mode 3)
void sound_play_simple(int owner, int sample, int volume, int pan);        // sound_play_simple_4f850 (mode 2)
int  sound_start_sample(int owner, int sample, int volume, int pan);       // sound_start_sample_4f8a0: 1 started, 0 no free channel
void sound_play_sample(int owner, int sample, int loop_count);             // sound_play_sample_5cbb0 (mode 4; -1 = loop)
void sound_play_sample_loud(int owner, int sample);                        // sound_play_sample_loud_5cd60 (cue script)
void sound_stop_sample(int owner, int sample);                             // sound_stop_sample_5cea0
void sound_stop_all();                                                     // sound_stop_all_5c040
void sound_set_sample_volume(int owner, int sample, int volume);           // sound_set_sample_volume_627d0 (0..0x80)
int  sound_count_playing();                                                // sound_count_playing_628a4

// ---- state (for tests and the debug overlay) ------------------------------------------------------------
enum { MC_SOUND_IDS = 47, MC_SOUND_CHANNELS = 32 };
struct SoundRequest {           // 0xb7ac0: 10 bytes per sound id
    uint16_t mode;              // +0 0 none, 1 restart, 2 simple, 3 play if idle, 4 loop / stop (volume 0x200)
    uint16_t pan;               // +2
    uint16_t volume;            // +4 cleared every tick by sound_update (the priority compare of the same tick)
    uint16_t owner;             // +6 owner thing of the request (0 for the local player's own sounds)
    uint16_t played;            // +8 set to 2 when the slot was played (never read)
};
struct SoundChannel {
    uint16_t owner, sample;     // DAT_0012e070[ch] {owner thing, sound id}
    uint16_t volume;            // DAT_0012e0f0[ch]: fades use 0..0x7fff; sound_start_sample stores volume >> 8 (sic)
    uint8_t  fadein;            // DAT_0012e270[ch]
    uint8_t  fadeout;           // DAT_0012e330[ch]
    uint16_t fadein_target;     // DAT_0012e2b0[ch] (0..0x7f; compared as (target << 8) - 1)
    uint16_t fadeout_arg;       // DAT_0012e2f0[ch] (stored, never read)
    uint16_t fadeout_volume;    // DAT_0012e350[ch]
    int      handle;            // backend voice (the HMI slot index in the original)
};
const SoundRequest *sound_request_slot(int sound);   // 0..46
const SoundChannel *sound_channel(int ch);           // 0..31
int sound_last_handle();                             // DAT_0012e1c4: result of the last start

// ---- music ---------------------------------------------------------------------------------------------
extern uint8_t  g_music_device;             // DAT_0012e06e: music<set>-<device> bank suffix, from the HMI MIDI device id in music_init_hmi_4d550:
                                            // 0 = OPL2 FM 0xa002 ("*.HMP"), 1 = MT-32 0xa004 ("*.ROL"), 2 = General MIDI: MPU-401 0xa001 / AWE32 0xa008 ("*.GEN")
extern uint16_t g_music_track;              // DAT_0009e312: playing track (0 = none)
extern uint16_t g_music_track_count;        // DAT_0009e316
extern uint16_t g_music_volume;             // DAT_0009e310 (100 after a start; not sent anywhere)
// music_load_bank_5c870(set): data/music<set>-<g_music_device>.dat/.tab (set 0 = the three game tracks).
bool music_load_bank(const char *game_dir, int set);
const mc_sndbank *music_bank();
void music_update(int mood);                // music_update_1f800: 1 calm / 2 combat (player_flyer_move_3fc00, P.combat_music > 0)
void music_stop();                          // music_stop_1f960
void music_play_track(int track);           // music_play_track_5c0a0 (1..count; no-op when == g_music_track)
bool music_song_done();                     // music_song_done_5cf40
void music_fade_out_begin();                // music_fade_out_begin_49cd0: 60 Hz master-volume ramp to 0, then music_stop
// The timer callbacks (music_mood_fade_cb_1f6d0 / music_fade_timer_cb_49ca0): the platform calls this at
// the rate the backend was told through music_timer(); the rate is 0 while nothing is fading.
void music_fade_tick(MusicTimer which);
int  music_fade_rate(MusicTimer which);
// game_main_32a00 level start: GameState+0x240 = (lcg16(level) % 3) + 1, then music_play_track of it
// (only when music is available, on, and a bank is loaded).
int  music_track_for_level(int level);
void music_start_level(int level);
int  music_level_track();                   // GameState+0x240 (what input.cpp's F2 / unpause replays)
// mood fade state (DAT_000938f8..fb)
struct MusicMoodState { uint8_t level, mood, active; int8_t dir; };
MusicMoodState music_mood_state();

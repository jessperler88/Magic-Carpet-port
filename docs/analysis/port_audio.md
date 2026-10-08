# Port round 5, task A: sound output (`mcport/audio_mixer.*`, `mcport/audio_sdl.*`, `mcdata/hmp.*`)

This task implements `SoundBackend` (sound.h) for the platform and adds music.

- **`mcdata/hmp.c`**: parses the HMI MIDI songs.
- **`mcport/audio_mixer.cpp`** (no SDL):
  - the software mixer for the 32 digital voices;
  - a translation of the HMI song sequencer of carpet.exe;
  - a trivial square-wave synth;
  - `AudioEngine`, the backend built from these pieces.
- **`mcport/audio_sdl.cpp`**: a thin layer on top of the above. It opens an SDL audio device and the Windows MIDI mapper, runs a 1 ms MIDI clock thread and exposes `audio_init` / `audio_update` / `audio_shutdown`.

`audio_test` builds with zero warnings and exits 0. `sound_test` still passes. `mcport` builds in `build_A`, and I ran a live check through a scratch harness: the SDL device opens at 44100 Hz, the MIDI mapper opens, the level track plays, and the mood fade runs.

## Functions translated

| original | port |
|---|---|
| `snd_midi_init_song_5e53a` (header check, track device auto-map) | `mc_hmp_parse`, `mc_hmp_track_plays`, `MusicSequencer::load_and_start` |
| `snd_midi_song_setup_5ec41` (track pointers, first delta, disabled tracks) | `MusicSequencer::rewind` |
| `snd_midi_read_varlen_5eedf` | `mc_hmp_read_varlen` |
| timer callback 0x66898 (not a function in Ghidra; far, `retf`): per-track counters, event lengths from tables 0x9fa0e / 0x9fa1e, FF 2F end of track, FF 51 skipped, song end | `MusicSequencer::tick`, `mc_hmp_event_length` |
| `snd_midi_start_song_5eab0` (timer event at header +0x38 Hz) | the sequencer clock (`AudioEngine::advance_music_us` / the render clock) |
| `hmi_midi_send_event_5d69b`, pass-through branch (flag 0xa2481 = 0, as `music_play_track_5c0a0` sets through `snd_midi_set_flag_5e4ee(0)`): CC7 scaled by master `v * 0xa2565 >> 7` | inside `MusicSequencer::tick` |
| `snd_midi_reset_channels_5e0d9` (pass-through: 7B, 79, E0 40 40, B 07 00 on each playing track's channel) | `MusicSequencer::reset_channels` |
| `snd_midi_stop_song_5eb38` + `snd_midi_clear_song_slot_5ea6d` + `snd_midi_all_notes_off_60035` | `MusicSequencer::stop` (= `music_stop` backend) |
| `snd_midi_set_volume_5ef56` (CC7 = master * 0x7f >> 7 on all 16 channels) | `MusicSequencer::set_master_volume` |
| `music_mood_fade_cb_1f6d0`'s send through `hmi_midi_send_event_5e4b8` (raw CC7 on channels 3, 4, 5) | `MusicSequencer::set_layer_volume` |
| `music_song_done_5cf40` (`!0x9f9ee[song]`) | `MusicSequencer::done` |
| `hmi_timer_add_event_5d093` / `hmi_timer_remove_event_5d3a9` for the two fade events | `AudioEngine::music_timer` + `pump_fade_timers` (game thread) |
| `hmi_start_sample_64e7c` / `hmi_stop_sample_65485` / `hmi_set_sample_volume_64d38` / `hmi_sample_done_6291e` | `AudioMixer` (the mixing itself lived in hmidrv.386, outside the exe; I chose the semantics from HMI SOS, see below) |

## HMP format (from the exe and the files)

The full layout is in `mcdata/hmp.h`.

- **Header.**
  - "HMIMIDIP"
  - +0x30: track count
  - +0x34: 120 (division, never read)
  - +0x38: sequencer rate in Hz, which is the song's ticks per second (120 in every file)
  - +0x3c: length in seconds
  - +0x40: u32[16] channel priorities
  - +0x80: u32[32][5] device list per track
  - +0x300: end-of-song callback
  - +0x308: the chunks. Each chunk is {u32 number, u32 length including the 12-byte header, u32 MIDI channel}, followed by the events.
- **Delta.** 7-bit groups, least significant group first. The **last** byte has bit 7 set.
- **No running status.** The event length comes from the status byte:
  - 8x / 9x / Bx / Ex: 3 bytes
  - Ax / Cx / Dx: 2 bytes (Ax is 2 in the HMI table)
  - F0: 0; F1: 1; F2: 2; F3: 1; F8..FF: 2
  - except FF 2F: 3 and FF 51: 5. The sequencer ignores tempo.
- **Timing.** Each timer tick increments every track's counter. An event fires once `delta <= counter`, the counter is then cleared, and delta-0 events follow in the same tick. So an event's absolute tick is the sum of the deltas, +1 if the track's first delta is 0. My first model was off by one tick here; the test caught it.
- **Song end.** The song ends when every playing track has reached FF 2F. The original then clears the active flag, resets the channels, removes the timer and rewinds. It does **not** loop: `music_update_1f800` sees `music_song_done` and restarts the track. The backend contract is the same.
- **Track device map.** The list 0xa000 matches the drivers 0xa000 / 0xa001 / 0xa008. An empty list maps to driver 0, so the track plays. The game passes map entries 0xff for tracks 0..15 (table at 0x9e65c) and 0 for tracks 16..31. In the shipped files every track lists {0xa002, 0xa000} or nothing, so all tracks play on OPL and on GM.
- **Events used.** The shipped songs only use 9x (note off is velocity 0), Bx (7, 10, and 116 / 117 in CSETUP), Cx, Ex and FF 2F.

## Verification (`audio_test`, all pass)

### 1. Mixer, by construction
- **Pan law.**
  - centre gives (32512·32767)>>15 on both sides;
  - pan 0 / 0xffff give hard left / right;
  - 0x4000 gives right (32767·32770)>>16;
  - volume 0x4000 halves the output;
  - volumes are clamped to 0..0x7fff.
- **Flags.** Without 0x200 the voice is centred. Without 0x100 it plays at full volume.
- **Resampled length** for 1000 samples:
  - 22050 to 44100 Hz: 2000 frames
  - 11025 to 44100 Hz: 4000 frames
  - 22050 to 48000 Hz: 2177 frames
  - 11025 to 48000 Hz: 4354 frames
  - 22050 to 11025 Hz: 500 frames
- **Linear interpolation.** I checked exact midpoints, the last sample being held on a one-shot, and interpolation across the loop point on a looping voice.
- **Loops.** Count 2 with 0x4000 gives 300 frames from 100 samples. Count 5 without 0x4000 gives 100 frames. Count −1 is still playing after 100000 frames.
- **Stop.** `is_playing` turns false at once. The ramp lasts 127 frames, and the largest step is 254 out of 32512 full scale. The slot is freed afterwards.
- **Volume changes** also ramp. Volume −1 (from `sound_set_sample_volume(…, 0)`) clamps to silent, and the voice keeps playing.
- **32-voice limit.** The 33rd start returns −1. A handle goes stale when its slot is reused. `len 0` (NULL.RAW) returns −1.
- **Engine.** I checked the master gain of 0x60 and the soft knee: linear up to 24576, then `K + d·R/(d+R)`.

### 2. HMP: all 21 tracks of the six music banks
| bank | track | trk | events | notes | ticks | s (header) |
|---|---|---|---|---|---|---|
| music0-2 | CGAME1.GEN | 7 | 6341 | 2948 | 31201 | 260.01 (260) |
| music0-2 | CGAME2.GEN | 12 | 7169 | 3174 | 18902 | 157.52 (157) |
| music0-2 | CGAME3.GEN | 8 | 3812 | 1888 | 13284 | 110.70 (110) |
| music0-2 | CSETUP.GEN | 13 | 3194 | 1122 | 13422 | 111.85 (111) |
| music1-2 | CINTRO4/5/6.GEN | 8/5/8 | 1695/505/4215 | 723/244/2089 | 30350/15070/13295 | 252.9/125.6/110.8 |

- The -0 (.HMP) and -1 (.ROL) banks are printed by the test as well.
- Every track: rate 120, no event the sequencer could not step over, every track ends with FF 2F, and the duration is within 1 s of the header's seconds.
- With argv[2], the test writes the GM banks as standard MIDI files `music<set>-2-<n>.mid` (division 120, tempo 1 s per quarter).

### 3. Sequencer
- **Timing.** CGAME1.GEN is played against a recording MIDI output. The sum of the send ticks of all channel events equals the sum of the parsed ticks. Every CC7 is scaled by master 0x40.
- **End of song.** The song is done after exactly 31201 ticks. Then come 4 reset messages per track. After that the song stays silent until it is restarted.
- **Layer and stop.** The layer volume sends B3 / B4 / B5 07 v. A stop sends the track resets plus 16 × (79, 7B).
- **Through sound.cpp.** `music_load_bank` (device 2) → `music_play_track(1)` → `music_update(1)` every 50 ms for 270 s → exactly one restart. Combat mood gives 63 × 3 layer messages up to 0x7e, after which the 60 Hz timer is removed.

### 4. Offline renders (written to `<scratch>/round5_A/`, for the user to listen to)
- **`movie0.wav`.** The first 60 s of movie 0 from the snapshot, 44100 Hz stereo, samples plus CGAME1 through the square synth.
  - 354 voices started, at most 22 sounding at once;
  - RMS 7671, peak 31529, 0 clipped samples (with master 0x80 it was 1 % clipped);
  - per-second RMS goes from 2900 up to 15600 in the big fight around 40–52 s.
- **`music0-2-1..4.wav` and `music1-2-1..3.wav`.** Every GM track through the square synth at 22050 Hz, full length (260 / 158 / 111 / 112 s; 253 / 126 / 111 s).
- **`.mid` files.** For listening through a real GM synth. These are the faithful option: the square synth is only a stand-in.

## Design decisions / deviations
- **HMI digital semantics.** The mixer was in hmidrv.386, not in the exe, so these follow the HMI SOS flag meanings:
  - 0x100 _VOLUME, 0x200 _PANNING, 0x4000 _LOOPING;
  - loop count n means n further passes, −1 means forever;
  - volume is linear, 0x7fff ≈ unity.
- **Pan law.** I chose a balance law: centre is unity on both sides and the far side falls linearly. This is a judgement; HMI's exact law is unknown.
- **No clicks.** The stop ramp is about 3 ms, and volume steps are smoothed. A stopped voice reports "not playing" at once and finishes its ramp in one of 16 extra slots.
- **Output level.** The master gain is 0x60 plus a soft knee, chosen for the output level rather than taken from the original. `MC_SOUND_VOLUME=n` overrides the gain.
- **Music output.** The General MIDI bank goes to `midiOutOpen(MIDI_MAPPER)`, with the sequencer clocked by a 1 ms thread (`timeBeginPeriod(1)`).
  - `MidiOut { send(status, d1, d2); panic(); renders(); render() }` is the seam for a later OPL / soft synth.
  - A rendering `MidiOut` runs sample-accurately in the audio callback (`CLOCK_RENDER`).
  - `MC_MUSIC=square` uses the square synth. So does a build with no MIDI device.
- **Channel mapping.** The pass-through branch only, as the game sets it. Channel stealing and priorities are unused, so they are not ported.
- **CC 116 / 117.** These are HMI-specific controllers in CSETUP. They go to the device unchanged; GM synths ignore them.
- **Master volume quirk.** `snd_midi_set_volume_5ef56` sends `master*0x7f>>7` on all channels. This overrides the song's own channel volumes during a fade-out, as in the original, because table 0xa2566 is never updated in pass-through mode.
- **Fade timers.** `music_fade_tick` runs on the game thread from `audio_update()`, by elapsed time, at most 64 calls per timer per call. Here the original's HMI timer was asynchronous.
  - Quirk kept from the original: if the mood flips 1 → 2 → 1 before the first fade tick, the layer level wraps below 0 (0xfe, 0xfc, …). In the original that needed two mood changes within one 60 Hz period. With a per-frame pump it is a little more likely.
- **Threading.** Every `SoundBackend` call takes the `AudioEngine` mutex. `music_timer` / `pump_fade_timers` are game-thread only.

## Fixes in the files task A owns
1. **`sound.cpp` `music_backend_play`.** It now passes the **whole** record (`mc_sndbank_record_data`, new in `sndbank.h/.c`) instead of length − 0x10. `music_play_track_5c0a0` gives the HMI only the pointer, and CSETUP.ROL's last chunk runs into the last 16 bytes. With the old length that song failed to parse, and the error path would have cleared `g_music_available`. The test checks this.
2. **`sound.h` / `sound.cpp`.** New `virtual void SoundBackend::flush()` (default no-op). `sound_load_bank` calls it before freeing the old bank: voices point into the bank memory, and the audio thread would read freed memory. This is a port addition; no existing signature changed.
3. **`sound.h` comments.** `g_music_device`: 0 = OPL2 0xa002 (*.HMP), 1 = MT-32 0xa004 (*.ROL), 2 = General MIDI, MPU-401 0xa001 / AWE32 0xa008 (*.GEN). The old comment said "0 GM, 1 OPL2, 2 AWE32", which was wrong; this confirms task G's note. The flags comment now gives the HMI meanings.

## Hooks
None declared. The platform installs its backend with `sound_set_backend`.

## TODO(port)
None.

## Requested shared-file changes

### 1. `src/mcport/main.cpp` (integrator)
```cpp
#include "audio_sdl.h"
#include "sound.h"
...
    // after plat.init(...) succeeded:
    audio_init(game.c_str());                 // sets g_sound_* / g_music_*, installs the SDL backend
    if (demo) {                               // the snapshot is already loaded: level 38 for movie 0
        sound_load_bank(game.c_str(), 0);
        music_load_bank(game.c_str(), 0);
        music_start_level(38);
    } else {                                  // until task D's game_level_begin() does this:
        sound_load_bank(game.c_str(), 0);
        music_load_bank(game.c_str(), 0);
        music_start_level(level);
    }
    // in the main loop, once per frame (e.g. right after the game-tick while loop):
    audio_update();
    // in the free-camera viewer, on L / K after engine_load_level(level): music_start_level(level);
    // before plat.shutdown():
    audio_shutdown();
```
Once task D's `game_level_begin` / `game_level_end` own the bank loads, `music_start_level` and `sound_stop_all` / `music_stop`, drop the level-start lines above. Keep `audio_init` / `audio_update` / `audio_shutdown`. The `INPUT_REQ_*` sound requests already reach `sound_handle_input_request` through `sound_register_handlers`, because mcport installs no input-platform hook of its own.

### 2. `src/tests/sound_test.cpp` (not mine; whoever owns it)
`tick_sound()` calls `sound_update()` and `music_update()` itself. `sim_all.cpp` now also installs `g_hook_sound_update` / `g_hook_music_update`, so in the replays they run **twice per tick**: the fades step twice. Drop the two calls from `tick_sound()`.

### 3. No CMake change needed
`hmp.c` / `sndbank.c` are already in mcdata, and the mcport glob picks up both new `.cpp` files.

## Corrections to ENGINE.md / FORMATS.md / names
- **Music bank suffix** (ENGINE.md "sndsetup.inf and HMI music initialisation", port_sound.md, FORMATS.md):
  - `music<set>-0` = OPL2 (*.HMP, 0xa002, also loads inst.bnk / drum.bnk);
  - `-1` = Roland MT-32 (*.ROL, 0xa004);
  - `-2` = General MIDI (*.GEN, 0xa001 MPU-401 and 0xa008 AWE32).
  - `music1-*` holds CINTRO4..6 (front end / intro).
- **FORMATS.md: add the HMP layout** from `mcdata/hmp.h`: the header fields, the reverse-order delta, no running status, the HMI event-length table, rate = +0x38 ticks per second.
- **Sequencer facts:**
  - the timer callback at 0x66898 should become a function, `snd_midi_song_timer_cb_66898`;
  - FF 51 tempo is ignored;
  - songs do not loop by themselves;
  - song end resets the channels (CC7 = 0) and rewinds;
  - `music_play_track_5c0a0` runs with channel mapping off;
  - `snd_midi_set_volume_5ef56` writes `master*0x7f>>7` on every channel in that mode;
  - the layer volume goes out through `hmi_midi_send_event_5e4b8`, raw, with no master scaling.
- **Data tables:** 0x9fa0e / 0x9fa1e are the HMI event-length tables; 0x9e65c is the track map (16 × 0xff, then 0).

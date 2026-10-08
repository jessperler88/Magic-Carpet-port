# Port round 4, task F: sound manager (`sound.h` / `sound.cpp`, `mcdata/sndbank.h` / `.c`)

This is the game-side sound manager of carpet.exe: the per-sound-id request table, the 32-channel bookkeeping, the fades, the sample layer, the bank loader and the music game logic. Everything below the HMI SOS API (`hmi_digi_*`, `hmi_midi_*`, `hmi_timer_*`) is now `struct SoundBackend` in `sound.h`. It is a class with virtual methods whose default instance does nothing. `sound_set_backend()` installs the platform's own instance (mcport / SDL later).

The bank parser is C in `mcdata/sndbank.c`, not C++ in sound.cpp. For this round the test compiles it through `tests/sound_test.cmake`. **The integrator must add it to the mcdata library** (see "Requested shared-file changes").

## Functions translated

| original | port |
|---|---|
| `sound_request_49720` | `sound_request_play` (installs `g_hook_sound_request`); positional part also exported as `sound_locate` |
| `sound_priority_ok_49c20` | `sound_priority_ok` |
| `sound_fade_player_sound_49c40` | `sound_fade_player_sound` (installs `g_hook_sound_fade`) |
| `sound_update_494b0` | `sound_update` |
| `sound_playing_count_49d50` | `sound_playing_count` |
| `sound_sample_done_49d80` | `sound_sample_done` |
| `sound_update_fadein_4dfc0` | `sound_update_fadein` |
| `sound_request_fade_in_4e0f0` | `sound_request_fade_in` |
| `sound_play_fade_in_4e120` | `sound_play_fade_in` |
| `sound_update_fadeout_4e2e0` | `sound_update_fadeout` |
| `sound_start_fade_out_4e400` | `sound_start_fade_out` |
| `sound_restart_sample_4f6f0` / `sound_play_if_idle_4f7a0` / `sound_play_simple_4f850` / `sound_start_sample_4f8a0` | same names without the suffix |
| `sound_play_sample_5cbb0` / `sound_play_sample_loud_5cd60` / `sound_stop_sample_5cea0` / `sound_stop_all_5c040` | same names |
| `sound_set_sample_volume_627d0` / `sound_count_playing_628a4` | same names |
| `sound_load_bank_5c990` + `sound_bank_relocate_5ca58` | `sound_load_bank(game_dir, set)` -> `mc_sndbank_load` |
| `music_load_bank_5c870` + `music_bank_relocate_5c924` | `music_load_bank(game_dir, set)` |
| `music_update_1f800` + its timer callback `1f6d0` (named here `music_mood_fade_cb_1f6d0`) | `music_update(mood)` + `music_fade_tick(MUSIC_TIMER_MOOD)` |
| `music_stop_1f960` | `music_stop` |
| `music_play_track_5c0a0` | `music_play_track` |
| `music_song_done_5cf40` | `music_song_done` -> `backend->music_done()` |
| `music_fade_timer_cb_49ca0` / `music_fade_out_begin_49cd0` / `music_fade_out_end_49d10` | `music_fade_tick(MUSIC_TIMER_FADE_OUT)` / `music_fade_out_begin` / static `music_fade_out_end` |
| game_main_32a00 0x32c67..0x32caf (level track) | `music_track_for_level`, `music_start_level`, `music_level_track` |

## Design

### Request side
`sound_request_49720(thing, player, id)` works in two steps:

1. **Position.** The listener is `players[local].thing`. A sound is dropped when:
   - its thing has flag 0x80, or
   - dist² > 0x9000000 (48 cells), or
   - its volume comes out below 0x200.

   The volume is `(range - dist) * 0x7fff / range` (idiv, clamped to 0x7fff), where `range = 12 * (0x400 - angle_diff(listener yaw, angle to source) / 2)`. That gives 0x3000 (48 cells) straight ahead and 0x1800 (24 cells) behind.

   The pan is 0x7fff when dist <= 0x140. Otherwise it is `0x7fff + ((d << 15) * turn_dir) / 512`, clamped to 0..0xffff, with d = diff folded to 0..0x200. In practice that is 0x7fff ± 64·d, so a source 90° to the right gives 0xffff.

   Thing index <= 0 means "no position": 0x7fff / 0x7fff.
2. **Request.** The jump table at 0x49664 is kept as a per-id table `g_sound_kind`:
   - **Mode 1 (restart)** and **mode 3 (play if idle)** requests go into the 10-byte slot `0xb7ac0[id]` = {mode, pan, volume, owner, played}. A request replaces the slot only if `sound_priority_ok(new volume, slot volume)` (new − cur >= −8).
   - **Ids 4 / 0xe / 0x1d (mode 1) and 0x11 (mode 3)** are local-player sounds: owner 0 for the local player, the thing's owner for player −1, otherwise dropped.
   - **Ids 1, 2 and 0x1f** (targets 0x46 / 0x46 / 0x55) are local-player loops started through `sound_request_fade_in(0, id, 3, target)`. They are skipped when the sound quality is 3.
   - **Id 5** (target 0x78) works the same way and is also allowed at quality 3.
   - **Ids 6 and 0x2e+** do nothing.

### Per tick
`sound_update` runs only when sound is available, on, and the game is not paused. It runs the fade-out stepper, then the fade-in stepper, then plays each pending slot (mode 1 restart, 2 simple, 3 if idle, 4 loop or stop at volume 0x200). After that it sets mode = 0 and played = 2, and **clears the volume of every slot**: it is the priority reference for the next tick only.

### Channel layer
There are 32 channels, each `{owner, sample}` plus volume / fade fields. A free channel is one where `!backend->is_playing(handle)`, i.e. `hmi_sample_done_6291e`. Every start fills the backend call from the record:
- data = `tab[id].+0x12`, length = `+0x1a - 0x10`;
- the bank's rate;
- the volume and pan;
- a loop count;
- the HMI flags word the original used (0x300 positional one-shot, 0x4300 fade-in loop, 0x4100 `sound_play_sample` loop, 0x100 cue script).

### Fades
- **Fade-in:** +0x800 per tick on the high byte (byte add, no carry), stopped at `(target << 8) - 1` or 0x7fff.
- **Fade-out:** −0x800 per tick from the saved volume; the voice is stopped at <= 0x1000 (signed 16-bit compare).

### Music
- **Level track.** The track is `(lcg16(level) % 3) + 1` (unsigned modulo of the sign-extended 16-bit LCG), stored at GameState+0x240. That offset is what input.cpp's F2 / unpause already replays.
- **`music_update(mood)`.** It restarts the song when it ended (calm, layer level 0). On a mood change it flips `DAT_000938fb` (±2) and arms the timer: 60 Hz to ramp up to 0x7e for combat (mood 2), 20 Hz to ramp down to 0 for calm (mood 1). Each tick of the timer writes controller 7 (volume) on MIDI channels 3, 4 and 5 (`backend->music_set_layer_volume`). In other words, the "combat music" is an extra layer of the same song faded in and out.
- **Fade-out.** `music_fade_out_begin` lowers the master volume by one step per 60 Hz tick from 0x7f. At 0 it stops the music and resets the volume to 0x7f.
- **Timers.** The two HMI timer events become `backend->music_timer(which, rate_hz)`. The platform then calls `music_fade_tick(which)` at that rate; `music_fade_rate()` reads the current rate.

### Input fit
`sound_handle_input_request` handles `INPUT_REQ_SOUND_STOP_ALL`, `INPUT_REQ_MUSIC_STOP` and `INPUT_REQ_MUSIC_PLAY(arg = track)` and returns false for everything else. `sound_register_handlers` installs it as `g_hook_input_platform` only if that hook is still null. The platform's own hook should call it first. The flags `g_sound_available` / `g_sound_on` / `g_music_available` / `g_music_on` (DAT_0009e320 / 321 / 30c / 30d) stay defined in input.cpp; sound.cpp uses them.

## Sample bank format (`mcdata/sndbank.h`)

`data/snds<set>-<quality>.dat` and `.tab` are both RNC-compressed. The `.tab` is made of 0x20-byte records:

| offset | content |
|---|---|
| +0x00 | char[18] source file name ("EXPLOD3.RAW") |
| +0x12 | u32 offset into the .dat (relocated to a pointer by 5ca58) |
| +0x16 | u16 0 |
| +0x18 | u16 0 |
| +0x1a | u32 length, a multiple of 16 (the game plays length − 0x10) |
| +0x1e | u16 0x5a in every sample record, 0 in record 0; never read |

- **Record 0** is a header (empty name, length = total size). The relocation starts at record 1, so the sample count is records − 1 and **sample index = sound id**.
- **Format.** The data is 8-bit unsigned mono PCM. There are no rate, bits or loop fields: looping is decided per call through the HMI flags and loop count.
- **Quality.** `<quality>` = `DAT_0009e328`, set by `mem_init_pools_59500` from free memory: 1 normally, 0 or 3 on small machines. `sound_digital_init_4da10` programs the driver at 22050 Hz for quality 1 and 11025 Hz for 0 and 3.
- **Variants.** In set 0, -1 is the 22 kHz bank (46 samples, 1.98 MB), -0 the same sounds at 11 kHz (45 samples), and -3 the low-memory 11 kHz bank in which the big loops (WAVES2, WHB03985) are NULL.RAW.
- **Which set a level selects.** None: game_main always calls `sound_load_bank_5c990(0)` (push 0 at 0x32bce) before a level. Sets 1..13 hold the speech of the cue scripts (`cue_script_step_17d80` ops 4 / 0x24), which belong to the front end and movies, not to the game.
- **Music banks.** `music<set>-<device>` uses the same container, with `<device>` = `DAT_0012e06e` from the HMI device id. music0-0 holds 4 HMP files ("HMIM" magic): CGAME1..3 are the three level tracks, CSETUP is the front-end track. The track count `DAT_0009e316` is therefore 4.

### WAV check
`sound_test <game> <dir>` wrote the first six samples of snds0-1 as WAV files into `round4_F/wav`:
- WAVES2 6.86 s, WHB03985 9.87 s, EXPLOD3 1.37 s, SELECTSP 1.03 s, FIRE 2.00 s (all at 22050 Hz) and NULL 16 bytes.
- The mean byte of every sample is 0x7e..0x81 (8-bit unsigned PCM).
- -0 / -1 sizes are in a 1:2 ratio, which confirms 11025 vs 22050 Hz.
- The full 16-byte padding at the end of some samples is zeros or garbage. That confirms the "−0x10" in the original.

## Verification

### Construction tests (`sound_test`, part 2; all pass)
- **Priority compare:** the −8 / −9 boundaries.
- **Positional volume and pan**, hand-computed from the disassembly:
  - 1 cell ahead = 32084 with pan centred;
  - 2 cells right / left = 30946 with pan 0xffff / 0;
  - 16 cells behind = 10922;
  - 45° at 4 cells: (0x2a00 − 0x5a8)·0x7fff/0x2a00 with pan 0x7fff + 0x4000;
  - the cut-offs: behind 24 cells dropped, 23 cells = 1365, ahead 47 cells audible, 49 cells dropped;
  - flag 0x80 drops the sound; a thing index <= 0 is played centred.
- **Request modes:** restart / if-idle / local-player variants, player −1, other players, NULL.RAW, an out-of-range id, and priority replacement.
- **Fade-in loops:** volume 0, loop −1, flags 0x4300, the targets; quality 3 suppresses ids 1 / 2.
- **`sound_update` playing the slots:**
  - slot volumes cleared afterwards;
  - play-if-idle dedup per owner;
  - restart stops the old voice;
  - channel volume = volume >> 8;
  - fade-in steps reach 0x45ff;
  - fade-out stops the voice after 7 steps;
  - re-requesting a playing loop only re-targets it.
- **Channel limits:** exhaustion at 32, then reuse after a stop; `set_sample_volume` writes (v << 8) − 1; stop-all; voice expiry over time.
- **Sound state:** sound off drops requests; paused keeps the slots pending.
- **Music:**
  - level 38 -> track 1, and tracks 1..3 for all levels;
  - the mood ramp 0 -> 0x7e at 60 Hz, back down at 20 Hz, and reversal mid-fade;
  - the song end restarts the song with the layer reset;
  - the 127-step fade-out ends in stop + volume reset;
  - the input requests.

### Level 38, the 412 ticks before the snapshot
Run with a recording backend. One voice lives len/rate seconds at 20 ticks per second; this only models channel reuse, the original mixes in real time.

- 1138 requests, 12 voices started, 4 stopped; music track 1.
- All 12 events:
```
  tick id name          thing owner   vol   pan
     0  1 WAVES2-.RAW       0     0     0 32767 loop 0x4300   (fade-in loop, then up to 0x45ff)
     0 14 GONG2.RAW         0     0 32767 32767 once 0x300
   116  3 EXPLOD3.RAW     874   888  4221 46719 once
   117  3 EXPLOD3.RAW     871   908 13619 41471 once
   133  3 EXPLOD3.RAW     598   800 12996 17407 once
   135 27 SPLASH2.RAW      18    53 26672 24319 once
   160 27 SPLASH2.RAW     349    53 24798 42431 once
   164 27 SPLASH2.RAW     870    53 25511  4607 once
   181 27 SPLASH2.RAW     852    53 25000 11135 once
   188 27 SPLASH2.RAW     907    53 24615 11327 once
   235 27 SPLASH2.RAW     753    53 24867 56319 once
   238 10 QUAKE4.RAW      345   345  2945 59647 once
```
- Most requests are dropped: the human wizard sits far from the action in the replay without input, and many requests repeat the same slot within one tick.

### Movie 0 from the snapshot (8551 ticks)
- 17628 requests, of which 8787 were out of range or too quiet.
- 12612 fade requests: `sound_fade` is called every tick by the game while a loop is not wanted, so this is expected.
- 2282 voices started, 1546 stopped, peak 23 simultaneous voices (<= 32).
- Music: 1 song start, 771 layer-volume writes, 6403 ticks in the combat mood (P.combat_music > 0).
- Voices by sound id:
  `1:1 2:1 3:590 4:102 9:689 10:2 14:108 15:7 16:1 17:41 19:10 23:51 24:51 25:2 28:30 29:1 30:8 31:3 33:79 34:89 35:72 36:78 40:264 41:2`
  (the big ones are 9 FIREBAL1, 3 EXPLOD3, 40 POSSHOT6 and the arrows 33..36).
- First 20 events:
```
     0  1 WAVES2-.RAW   thing   0 owner   0 vol     0 pan 32767 loop 0x4300
    28 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
    40 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
    44 40 POSSHOT6.RAW  thing 491 owner 479 vol 31860 pan 32767 once
    46 40 POSSHOT6.RAW  thing  18 owner 479 vol 31860 pan 32767 once
    47  4 SELECTSP.RAW  thing 479 owner 479 vol 32767 pan 32767 once
    48 40 POSSHOT6.RAW  thing 491 owner 479 vol 31860 pan 32767 once
    50 40 POSSHOT6.RAW  thing  25 owner 479 vol 31860 pan 32767 once
    52 40 POSSHOT6.RAW  thing  29 owner 479 vol 31856 pan 32767 once
    53  4 SELECTSP.RAW  thing 479 owner 479 vol 32767 pan 32767 once
    54 40 POSSHOT6.RAW  thing 491 owner 479 vol 31860 pan 32767 once
    67 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
    78 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
    84 40 POSSHOT6.RAW  thing  26 owner 479 vol 31857 pan 32767 once
    88  2 WHB03985.RAW  thing   0 owner   0 vol     0 pan 32767 loop 0x4300
    89  4 SELECTSP.RAW  thing 479 owner 479 vol 32767 pan 32767 once
   163 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
   175 14 GONG2.RAW     thing   0 owner   0 vol 32767 pan 32767 once
   180 19 SPEEDUP.RAW   thing 479 owner 479 vol 32767 pan 32767 once
   207 40 POSSHOT6.RAW  thing  58 owner 479 vol 32767 pan 32767 once
```
  The "thing" column is the thing passed to the last request of that id, as logged by the test's wrapper hook.
- Every volume is in 0..0x7fff, every pan in 0..0xffff and every sample id in 1..46.
- Build: `sound_test` builds with zero warnings (clean rebuild) and exits 0.

### Not verified
There is no reference from the original for the per-tick request stream. The positional math, the modes and the fades are checked only by construction from the disassembly. Nobody has listened to the samples; the WAVs are checked by size, rate and mean only.

## Deviations and gaps
- **Leftovers of the shared HMI start structure.** The original passes a global start structure (0x9e32c) that each path only partly rewrites, so the pan (+0x32) and loop count (+0xc) of an earlier call stay in it. The HMI flags decide whether those fields are read: 0x200 (_PANNING) for the pan, 0x4000 (_LOOPING) for the loop count. The port therefore passes centre pan when 0x200 is clear and loop 0 when 0x4000 is clear. This is a judgement from the HMI SOS flag meanings and was not traced through `hmi_start_sample_64e7c`'s mixer.
- **Unit mix kept from the original.** `sound_start_sample_4f8a0` stores `volume >> 8` in the channel volume table, while the fades work in 0..0x7fff. A positional one-shot that later gets a fade-out therefore fades from a tiny value.
- **HMI internals are the backend's job.** The HMP parser, mixing and the MIDI channel / controller details are not translated. The backend gets the song bytes (`music_play(track, data, len)`) and must loop or stop the song and report `music_done`. `music_play_track` returning false clears `g_music_available`, as the original's error path does after "Error : %s".
- **Not ported:** `music_init_hmi_4d550` / `sound_digital_init_4da10` / `sound_init_from_sndsetup_4d8f0` (driver set-up and the sndsetup.inf parsing). They are platform code: the platform sets `g_sound_available`, `g_music_available`, `g_sound_quality` and `g_music_device` itself.
- **`sound_stop_all_5c040` loop.** The original loops `while (!done) stop` per channel; the port bounds that loop with a guard of 32.
- **Front end and cue scripts.** `sound_play_sample_loud` and the bank sets 1..13 are exposed, but nothing in the port calls them yet.

## Hooks
- **Installed:** `g_hook_sound_request` (thing.h) -> `sound_request_play`, `g_hook_sound_fade` (thing.h) -> `sound_fade_player_sound`, and `g_hook_input_platform` (input.h) -> sound requests, but only when that hook is still null.
- **Declared:** none. The platform interface is `SoundBackend` / `sound_set_backend`.

## Extra functions
None outside the range. The level track code of game_main (0x32c67..0x32caf) is included as `music_track_for_level` / `music_start_level`.

## TODO(port) call sites
None in sound.cpp.

## Requested shared-file changes
1. **`src/CMakeLists.txt`:** add `mcdata/sndbank.c` to the mcdata library. Then drop it from `tests/sound_test.cmake`; until then that file lists it explicitly, and both at once would give duplicate symbols.
2. **`player.cpp:1207` (`game_tick_sim`):** call `sound_update()` after the thing updates, replacing the TODO there. Include `sound.h`, or add a hook in player.h if player.cpp must not depend on sound.cpp; `sound.cpp` is in MC_SIM_ALL only, not MC_SIM_CORE.
3. **`player.cpp:41`:** the `music_update` stub should call `music_update(mood)` from sound.h (same hook question as item 2).
4. **Level start (`sim_load_level` / engine level start):** do what game_main does:
   - `sound_load_bank(game_dir, 0)` and `music_load_bank(game_dir, 0)` once;
   - `music_start_level(level)` before the first tick;
   - `sound_stop_all()` + `music_stop()` when the level ends.
5. **mcport:** implement `SoundBackend` with SDL audio. The voices are 8-bit unsigned mono at the bank rate; the volume is 0..0x7fff; the pan is 0..0xffff with 0x7fff as centre. Call `music_fade_tick(which)` at `music_fade_rate(which)` Hz. Set `g_sound_available` / `g_music_available` / `g_sound_on` / `g_music_on` = 1 when audio opens. Route the `INPUT_REQ_*` hook through `sound_handle_input_request` first.
6. **`mc_types.h`:** name GameState+0x240 `int32_t level_music_track` (today it is inside `pad23d`).

## Corrections to ENGINE.md / carpet_types.txt
- **"Sound (HMI SOS, 32 digital channels)":**
  - The request record at 0xb7ac0 is {+0 mode (1 restart, 2 simple, 3 play if idle, 4 loop/stop), +2 pan, +4 volume (the priority reference, cleared every tick), +6 owner, +8 played flag (2)}.
  - The `.tab` record is name[18] / +0x12 offset / +0x1a length incl. 16 bytes of padding; record 0 is a header; sample index = sound id.
  - `DAT_0009e328` is the memory class / bank quality (1 = 22 kHz bank, 0 / 3 = 11 kHz, 3 = low-memory bank without the big loops), not the card type.
- **FORMATS.md:** `sndsX-Y`: Y = quality class 0 / 1 / 3 as above. Set 0 is the in-game bank; sets 1..13 are cue-script speech. music0-Y holds 4 HMP tracks: 3 level tracks + CSETUP.
- **Combat music:** `music_update_1f800`'s "combat" mood does not change tracks. It ramps controller 7 on MIDI channels 3..5 of the same song: 0 -> 0x7e at 60 Hz in steps of 2 for combat, back at 20 Hz for calm. The globals are `DAT_000938f8` (layer level), `938f9` (mood), `938fa` (timer active) and `938fb` (step ±2).
- **Function name:** `0x1f6d0` should be named `music_mood_fade_cb_1f6d0` (far callback, ends in retf).
- **Level track:** `GameState+0x240` is the level's music track, `(lcg(level) % 3) + 1`.

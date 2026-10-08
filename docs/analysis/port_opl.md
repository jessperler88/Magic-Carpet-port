# Port round 6, task A: OPL2 FM music (`mcport/opl_chip.*`, `mcport/opl_driver.*`, `tests/opl_test.*`)

The FM music banks `music<set>-0.dat` now play as they did on a Sound Blaster. No MIDI device is needed.

The work has three parts:

- **`mcport/opl_chip.h/.cpp`**: a software YM3812 (OPL2). I wrote it from the data sheet and the public register documentation; no emulator code was copied.
- **`mcport/opl_driver.h/.cpp`**:
  - `HmiOplDriver`, a translation of the OPL2 MIDI driver that is **linked into carpet.exe** (`snd_opl_*`, HMI device 0xa002);
  - `OplMidiOut`, the rendering `MidiOut` for `AudioEngine` (the seam round 5 left).
- **`mcport/audio_sdl.cpp`**: `MC_MUSIC=opl` is the new default. `MC_MUSIC=midi` keeps the General MIDI path through winmm.

Status:

- `opl_test` builds with zero warnings in my files and exits 0.
- `audio_test` and `sound_test` still pass.
- mcport builds in `build_A`. A live start printed `audio: sound on (44100 Hz), music OPL2 (FM bank)`.

**The driver's register stream is identical to the original's.** I ran the original `snd_opl_*` machine code from the carpet.exe image in a CPU emulator (unicorn) on the same MIDI streams and compared the writes:

- all 7 FM songs, played from start to end and then restarted and stopped: 291,908 writes;
- a 47,407-event random stream that covers every driver path: 375,606 writes.

The OPL driver is **not** in `hmimdrv.386`, contrary to the briefing. That file is "HMI MIDI Driver For Sound Master II" (`smii.com`, 2617 bytes). The OPL2 driver is the internal 0xa002 driver in carpet.exe (ENGINE.md "Internal OPL2 driver state").

## Functions translated (`HmiOplDriver`, mcport/opl_driver.cpp)

| original | port |
|---|---|
| far entry 0x69849 (fn0) -> `snd_opl_midi_event_6911c` | `HmiOplDriver::midi_event` |
| far entry 0x69878 (fn1) -> `snd_opl_driver_init_697e1` | `init` |
| far entry 0x698b3 (fn2) -> `snd_opl_driver_uninit_6977c` | `uninit` |
| far entry 0x698d1 (fn3) -> `snd_opl_reset_state_69319` | `reset` |
| far entry 0x698ef (fn4) -> `snd_opl_set_timbre_bank_69401` | `set_timbre_bank` |
| `snd_opl_pack_timbre_bank_6955a` | `pack_timbre_bank` |
| `snd_opl_pitch_bend_68619` | `pitch_bend` |
| `snd_opl_calc_bent_freq_686db` | `calc_bent_freq` |
| `snd_opl_controller_68893` | `controller` |
| `snd_opl_note_on_689da` | `note_on` |
| `snd_opl_note_off_68eca` | `note_off` |
| `snd_opl_all_notes_off_68fc9` | `all_notes_off` |
| `snd_opl_reset_controllers_69072` | `reset_controllers` |
| `snd_opl_init_chip_69940` | `init_chip` |
| `snd_opl_voice_set_freq_699e1` | `voice_set_freq` |
| `snd_opl_clear_voices_69a77` | `clear_voices` |
| `snd_opl_voice_key_on_69b31` | `voice_key_on` |
| `snd_opl_voice_key_off_69bf3` | `voice_key_off` |
| `snd_opl_silence_all_69c73` | `silence_all` |
| `snd_opl_load_timbre_69d61` | `load_timbre` |
| `snd_opl_clear_chip_ready_69f6a` | inline in `uninit` |
| `snd_opl_write_69f9c` (reg, val) | `OplWriter::opl_write` |
| `snd_opl_set_channel_volume_704cd` | `set_channel_volume` |
| `snd_opl_alloc_voice_70697` | `alloc_voice` |
| `snd_opl_program_change_7075e` | inline in `midi_event` / `set_timbre_bank` |
| `snd_midi_all_notes_off_60035`, the fn3 call at its end | `MidiOut::reset()`, called by `MusicSequencer::stop` |
| `music_init_hmi_4d550`, the 0xa002 branch (fn1, then fn4 inst.bnk, then fn4 drum.bnk) | `OplMidiOut::load_banks`, `load_opl_banks` (audio_sdl.cpp) |

- **Tables.** They are extracted, not retyped. The new spec is `src/mcengine/tables/opl.tables`, which generates `gen/opl_tables.h`:
  - `g_opl_slot_ops`: DAT_000a402d, 18 bytes (modulator / carrier operator per voice);
  - `g_opl_vel_level`: DAT_000a42dc, 64 bytes;
  - `g_opl_note_freq`: DAT_000a42f0, 140 dwords. This covers DAT_000a4320, DAT_000a44b8 and DAT_000a44bc.
- **Initial values.** The driver's globals start with the values from the image:
  - bend 0x40, bend range 2, channel volume 0x7f, voice velocity 0x7f;
  - **DAT_000a407c = 1**.

## How the HMI OPL driver turns MIDI into OPL writes (from the disassembly)

### Init (fn1)
- **Port check.** `init_chip` accepts port 0x388 or 0x380 only. On success it:
  - writes 0x01 = 0x20 (WSE);
  - sets 0xBD shadow = 0;
  - calls `clear_voices`: B0..B8 = 0, timbre flags cleared, 0xBD = shadow & 0xc0;
  - sets chip ready.
- **Second clear.** fn1 then calls `clear_voices` again, so 21 writes in total. The second clear also runs after a rejected port.

### Banks (fn4, twice)
- **Packing.** Each call packs the bank in place into register bytes (see the format below).
- **First call: the melodic bank.** It stores the name and data pointers and sends program 0 to channels 0..8.
- **Second call: the percussion bank.** It takes the name and data offsets **from the melodic bank's header** (sic). Both shipped banks have the same layout, so this is harmless.

### Event dispatch (fn0)
fn0 dispatches on `status & 0xf0`:

| status | handling |
|---|---|
| 0x90 | velocity > 0: note on; velocity 0: note off |
| 0x80 | note off |
| 0xB0 | controller |
| 0xC0 | `program[ch] = d1` |
| 0xE0 | pitch bend with the **MSB only** |
| other | ignored |

Controllers:

| controller | handling |
|---|---|
| 7 | channel volume |
| 0x40 | sustain |
| 0x66 | bend range in semitones (no RPN) |
| 0x79 | reset controllers |
| 0x7B | all notes off |

Every other controller is ignored: pan 10 and the HMI 116 / 117 have no effect, and the output is mono.

### Note on (melodic, channel != 9)
1. **Voice.** `v = alloc_voice(ch)`:
   - the first voice with no note;
   - else the first voice whose channel is the **lowest channel that never received a pitch bend** (`DAT_000a4124`, cleared only by fn3);
   - else `voice = channel` (`channel - 9` for channels 9..15). The names CSV says "note % 9"; that is wrong.
2. **Release the old note.** Key off. Then 5 × {0x80+carrier = SL/RR shadow | 0x0f, 0x80+modulator = shadow | 0x0f}: a fast release of the old note while the port writes take their time.
3. **Load the timbre.** The program is `DAT_000a4080[ch]`, at `melodic_data + program * 30`:
   - modulator: 0x20, 0x40, 0x60, 0x80, then 0xC0+v, then 0xE0;
   - carrier: 0x20, 0x60, 0x80, 0xE0. The carrier's 0x40 is written by the caller.
4. **Level.**
   - `x = (chvol << 7) / 127`
   - `lv = (velocity * x) >> 7`
   - `t = DAT_000a42dc[lv >> 1]`
   - `level = (0x2000 - (0x40 - t) * 2 * (0x40 - TL_carrier)) >> 7`
   - The carrier's 0x40 = (KSL bits | level).
   - **The modulator's 0x40 gets the same level when the packed FB/CON byte is 0** (FM without feedback). A timbre with feedback or additive connection keeps the modulator TL from the bank.
5. **Key on.** `DAT_000a42f0[note]` = (block << 10) | F-number: A0 = low byte, then B0 with the key bit clear, then B0 with the key bit set.
6. **Pending bend.** If `DAT_000a407c` (always 1) is set and the channel has had a bend, the frequency is rewritten with `calc_bent_freq`.

### Note on (channel 9)
- **Timbre.** `drum.bnk` record = note number. The timbre is loaded the same way; there is no check that the drum bank is loaded.
- **Level.** Only the carrier level is written.
- **Pitch.** The pitch comes from **byte +2 of the drum name record** (the "flags" byte of a standard BNK name record). Examples: 35 -> 12, 36 -> 48, 38 -> 60.
- **Bends.** None; pitch bends on channel 9 are ignored.

### Note off
- If the channel is sustained, the event is queued (at most 16).
- Otherwise every voice with the same note and channel is keyed off.

### Pitch bend
`calc_bent_freq` moves the note's F-number towards note ± range by `(msb - 0x40) / 64` in steps of 1/1000:
- unsigned 32-bit arithmetic;
- the octave wrap goes through the block bits and the helper tables at 0xa44b8 / 0xa44bc;
- the original's `%12` is a subtraction loop.

### Controller 7
For every sounding voice of the channel, the level is recomputed with the same formula:
- carrier always;
- modulator when the **melodic** program's FB/CON byte is 0. For channel 9 this means the melodic `program[9]`, also for drum voices.

### Rhythm mode
Never used. 0xBD is only ever written with 0, or with shadow & 0xc0. Drums are melodic voices with drum.bnk timbres.

## Driver quirks kept (all verified against the original code)
- **Overlapping shadows (found by the original-code comparison).** The SL/RR shadow DAT_000a3fdc is only 0x10 bytes before the F-number shadow DAT_000a3fec.
  - For voices 6..8 the carrier operators are 0x13..0x15 and the modulators 0x10..0x12. Their "SL/RR shadow" is the F-number low byte of voices 0..5.
  - `load_timbre` overwrites those bytes. The fast-release writes of a note on read them back.
  - Example: `0x93 = 0x9f` instead of `0x0f`.
  - The port keeps one shadow block with the original offsets.
- **Velocity aliasing.** Controllers 0x79 / 0x7B index the voice-velocity table DAT_000a41e8 by **channel**:
  - they set the velocity of voice <ch> to 0x7f;
  - for channels 9..15 they set DAT_000a420c[ch − 9] instead (that flag table is never read).
- **Sustain release.** It replays the queue from entry **[count]** down to [1]: one past the last entry, and never entry [0]. Entry [16] reads the first bytes of the velocity table.
- **Packing.** `pack_timbre_bank` packs only `count − 2` records. Programs 126 / 127 keep their raw BNK parameters as register bytes.
  - CSETUP sets program 126 on channel 10. That matters only through the controller 7 quirk above.
- **Notes 0..11.** `DAT_000a42f0[0..11]` overlaps the tail of the velocity table. The shipped songs never play melodic notes below 12; the lowest is 24.

## Deviations from the original
- **Sustain hang.** If a queued note-off's channel is still sustained when another channel releases its sustain, `note_off` re-queues the entry at [count]. The original then loops forever inside the timer interrupt.
  - The port stops the loop instead and leaves the queue as it was.
  - The shipped songs never send controller 0x40.
- **Out-of-table reads.** Reads outside the extracted tables return 0. The original reads unrelated memory there. This needs bend ranges above 12 or notes below 12 with a pending bend; the songs do neither.
  - With a note below 12 the original's `%12` loop would also spin about 357 million times.
- **Write timing.** `OplMidiOut` gives each register write 35 µs of chip time (`set_write_cost`, default 445/256 chip samples). That is the original's 6 + 35 status-port reads per write. A note on (24–27 writes) therefore spreads over about 0.9 ms, as on the hardware, and the fast release before the re-attack keeps its audible effect. `set_write_cost(0)` applies all of an event's writes at once.

## The software OPL2 (mcport/opl_chip.cpp)

- **Rate.** Chip clock 3.579545 MHz / 72 = 49715.9 Hz. One `sample()` produces the sum of the 9 channels, mono.
- **Phase.** A 20-bit accumulator. The increment is `((fnum + vibrato) << block) * MULT×2 / 2`, with MULT×2 = {1,2,4,…,20,20,24,24,30,30}.
- **Output.** A 10-bit phase indexes a 256-entry quarter log-sine table, -log2(sin) in 1/256 units. The total attenuation is added (× 8, since one attenuation unit of 0.1875 dB ≈ 8 log units). An exponent table, 2^(-i/256) × 4095, then a shift converts back.
- **Waveforms.** Sine, half sine, absolute sine, quarter-sine pulses. All four need WSE (reg 1 bit 5); without it the chip plays sine.
- **Envelope.**
  - Attenuation is 0..511 in 0.1875 dB steps.
  - Effective rate R = 4 × rate + Rof, capped at 63, where Rof = KSR ? key-scale number : KSN >> 2, and KSN = block × 2 + F-number bit 9 (bit 8 with NTS).
  - Rate 0 holds.
  - For R < 48 there is one step every 2^(12 − R/4) samples, with the 4/5/6/7-in-8 pattern for R & 3. For R ≥ 48 the step is pattern << (R/4 − 12) every sample.
  - Attack is exponential (`env += (~env * inc) >> 3`) and instant at R ≥ 60.
  - Decay runs to SL × 16 (SL 15 = 93 dB). Then EGT 1 holds; EGT 0 goes on at RR with the key still on.
  - Key on restarts the attack from the current level with phase 0.
- **TL and KSL.**
  - TL is in 0.75 dB steps.
  - KSL uses a base table in 3 dB/octave units: 0, 9, 12, 13.9, … 21 dB at block 7, minus 3 dB per octave below. KSL 1 = ×2 (3 dB/oct), 2 = ×1 (1.5 dB/oct), 3 = ×4 (6 dB/oct).
- **LFOs.**
  - Tremolo: a 210-step triangle of 64 samples per step (3.70 Hz), 0..26 units = 4.875 dB, or ÷4 with DAM 0.
  - Vibrato: 8 steps of 1024 samples (6.07 Hz), ±(F-number >> 7) × {0,1,2,1,0,−1,−2,−1} / 2 (DVB 1) or / 4 (DVB 0).
- **Modulation and feedback.**
  - FM: the modulator output (±4095) is added to the carrier's 10-bit phase as it is.
  - Feedback = (last two outputs) >> (9 − FB), i.e. 4π at FB 7 as in the data sheet.
- **Rhythm mode** (0xBD bit 5):
  - BD = channel 6, both operators;
  - HH = op 0x11, SD = 0x14, TOM = 0x12, CY = 0x15, with phases combined from the HH / CY phase bits and a 23-bit noise LFSR;
  - every rhythm voice is doubled;
  - the keys are OR-ed with the channel keys.
- **Timers and CSM.**
  - Timers: T1 counts every 288 clocks (80 µs), T2 every 1152 clocks. Status bits 7 / 6 / 5, masks and IRQ reset on reg 4.
  - CSM (reg 8 bit 7): a timer-1 overflow keys every channel for one sample.
- **Resampling.** `OplMidiOut::render` resamples linearly to the device rate. The mono output goes to both sides with gain 0x100 (one channel at full scale = ±4095); `MC_MUSIC_VOLUME` changes it.
- **Accuracy.** "Close by construction", not bit-exact. Known differences against the data sheet, all from the test below:
  - attack AR 14 is 0.34 ms against 0.38 ms;
  - decay and release run about 7 % slow (511 linear steps against the sheet's times);
  - vibrato depth is ±11.8 / ±5 cents against the sheet's 14 / 7.
  - The rhythm-mode noise and phase mix is my own construction from the documented behaviour; the test only proves that each instrument sounds on its own operator.

## Verification (`opl_test`, mc_unit_test with `${MC_SIM_ALL}` + `audio_mixer.cpp` + the two OPL files)

All checks pass: `OK: 0 failure(s)`. The run takes 2.4 s, or 8.6 s with the 60 s renders.

### 1. Chip by construction

**Frequency of one sine operator.** Measured over 1 s by interpolated zero crossings; every case is within 0.0003 %.

| fnum | block | MULT | Hz |
|---|---|---|---|
| 577 | 4 | 1 | 437.715 |
| 343 | 2 | 1 | 65.051 |
| 1023 | 7 | 1 | 6208.419 |
| 577 | 4 | 2 | 875.429 |
| 577 | 4 | 0 (½) | 218.857 |
| 400 | 3 | 15 | 2275.813 |
| 600 | 5 | 11 | 9103.252 |

**Envelope against the data-sheet tables** (data sheet in brackets).

| test | measured (data sheet) |
|---|---|
| attack AR 1 / 4 / 8 / 12 / 13 / 14 | 2801 (2826), 350.2 (353.3), 21.9 (22.08), 1.39 (1.40), 0.74 (0.70), 0.34 (0.38) ms |
| decay to 24 dB, DR 1 / 4 / 8 / 12 / 15 | 10546 (9820), 1318 (1228), 82.4 (76.7), 5.15 (4.80), 0.64 (0.60) ms |
| release, RR 2 / 6 / 10 / 14 | 21050 (19640), 1315 (1228), 82.2 (76.7), 5.13 (4.80) ms |

- All are within 10 %, except AR 14 at −10.0 %; its tolerance is 15 %.
- EGT 1 holds the sustain level. EGT 0 decays on.
- The KSR time ratio between R 19 and R 31 is 8.00.

**TL and KSL.**
- TL 0 / 8 / 16 / 32 give peaks 4095 / 2047 / 1023 / 255 (within 1.5 % of −6 dB steps).
- KSL at block 7 / 0x3ff:

  | KSL | units | dB |
  |---|---|---|
  | 0 | 0 | 0 |
  | 1 | 112 | 21 |
  | 2 | 56 | 10.5 |
  | 3 | 224 | 42 |

- One octave lower is −3 dB.
- The amplitude at −21 dB matches.

**Waveforms** (with WSE).

| waveform | min | max | zero samples |
|---|---|---|---|
| 0 | −4095 | 4095 | — |
| 1 | 0 | — | 50 % |
| 2 | 0 | — | 0 % |
| 3 | 0 | — | 50 %, sounding only in quarters 1 and 3 |

Without WSE all four are sines.

**Feedback.** The fundamental's share of the power falls from 1.000 at FB 0 to 0.962 (FB 2), 0.659 (FB 4), 0.115 (FB 5), 0.286 (FB 6) and 0.140 (FB 7). FM with the modulator at TL 16 gives 0.217.

**LFOs.**
- Tremolo: 4.89 dB peak to peak (DAM 1), 1.13 dB (DAM 0).
- Vibrato: ±11.8 cents (DVB 1), ±5.0 cents (DVB 0).

**Rhythm mode.**
- Each of BD / SD / TOM / CY / HH sounds alone on its own output (peaks 8190 / 8190 / 8190 / 8190 / 7842); the other four outputs stay at 0.
- Nothing sounds with the rhythm bit only, or with the instrument bits without the rhythm bit.
- Channel 6 plays melodically when rhythm mode is off.

**Timers.** Timer 1 at preset 0xff overflows after 4 samples (80 µs). Timer 2 at preset 0 overflows after 4096 samples. The IRQ reset clears the status.

### 2. Banks
- Both files are RNC: 5404 bytes unpacked, "ADLIB-", version 0.0, 128 used / 128 instruments.
- Names are at 0x1c and data at 0x61c. Each name's index equals its record number.
- Every parameter is in range.
- inst.bnk has 108 distinct names (piano1.i, piano3.i, elecvibe, …). drum.bnk has 15 (blank.in for unused notes, bdc1, SBBD, sn1, SBSN1, …).
- The packing of record 0 matches my hand calculation: 01 4f f1 53 06 / 11 00 d2 74.
- Programs 126 / 127 stay unpacked.
- Drum pitch bytes: 35 -> 12, 36 -> 48, 38 -> 60.

### 3. Driver against hand-derived register writes
Checked: fn1 init (21 writes), a rejected port, note on 60 at velocity 127 / 64 (24 writes each), note off, controller 7, pitch bend up / down (crossing the octave) / centre, a drum note (24 writes), the modulator level of an FB/CON-0 timbre, voice stealing in all three stages, all notes off, reset controllers, fn3 reset (19 writes) and sustain.

### 3b. Against the original machine code
`<scratch>/round6_A/opl_orig.py` loads the relocated carpet.exe image into unicorn with a flat GDT. It calls the original near functions with the original stack arguments:

- `snd_opl_driver_init_697e1(-, -, far &port)`
- `snd_opl_set_timbre_bank_69401(-, -, far bank)` (inst.bnk, then drum.bnk)
- `snd_opl_midi_event_6911c(-, -, far event)`
- `snd_opl_reset_state_69319()`

It intercepts `snd_opl_write_69f9c` at its first instruction.

`opl_test <game> <dir>` writes the event streams: every song from start to end, master volume 0x60 at tick 3000, layer volume 0x50 at tick 3100, master volume back at tick 6000, then a restart, 2400 ticks and `MusicSequencer::stop`.

**Result: IDENTICAL for every stream.**

| stream | writes |
|---|---|
| CGAME1 | 67,917 |
| CGAME2 | 70,990 |
| CGAME3 | 44,440 |
| CSETUP | 30,011 |
| CINTRO4 | 22,214 |
| CINTRO5 | 7,338 |
| CINTRO6 | 48,998 |
| random stream (40,000 generated events incl. sustain batches, controllers 0x66 / 0x79 / 0x7B / 10, programs 0..127, drums, velocity 0, resets) | 375,606 |

The first version differed in six of the seven songs, within the first 1600 writes of each (CINTRO5 was identical). The difference was the shadow overlap described above.

### 4. Songs through MusicSequencer -> OplMidiOut
Rendered through `AudioEngine` at 44100 Hz, CLOCK_RENDER, 60 s each with argv[2]. Every track plays on device 0xa002. None clipped.

| song | tracks | notes / 60 s | voices max / average | RMS | peak |
|---|---|---|---|---|---|
| music0-0 CGAME1 | 7 | 624 | 9 / 6.2 | 5740 | 23567 |
| music0-0 CGAME2 | 9 | 774 | 9 / 4.5 | 2954 | 15089 |
| music0-0 CGAME3 | 8 | 785 | 9 / 4.3 | 4479 | 20662 |
| music0-0 CSETUP | 11 | 497 (958 bends) | 9 / 4.6 | 4249 | 19919 |
| music1-0 CINTRO4 | 8 | 440 | 9 / 6.2 | 5491 | 24566 |
| music1-0 CINTRO5 | 5 | 124 | 6 / 5.6 | 5390 | 14687 |
| music1-0 CINTRO6 | 6 | 907 | 8 / 4.6 | 4129 | 16369 |

- The test also prints the programs, the note range and the controllers of each song.
- The lowest melodic note is 24.
- A spectrum of CINTRO5 shows the harmonic series of 45.8 Hz: note 30 = F#1, slightly flat, as in the driver's F-number table.

### 5. Game side
- `g_music_device 0` -> `music_load_bank` loads music0-0 (4 tracks).
- `music_play_track(2)` sounds in 40 of 40 chunks of 100 ms.
- `music_stop` -> 16 × (79, 7B) + fn3 reset: all voices are free, and 2 s later the peak is 0.

### Renders for listening
`<scratchpad>\round6_A\music0-0-{1..4}.wav` and `music1-0-{1..3}.wav`: 60 s each, 44100 Hz, 16-bit stereo (mono content).

### What I could not do: a DOSBox register capture
- I set up `extracted/refgame_opl/` with my scratch `opl_capture.py`:
  - a copy of `MagicCarpet/magic` and the bundled DOSBox 0.74-3;
  - carpet.exe patched by `patch_carpet.py` with no dumps;
  - the shipped `CARPET.CD/SNDSETUP.INF`, which already selects `MUSIC = SBLAST 388`, i.e. this driver.
- DOSBox 0.74-3 starts its raw-OPL capture (`caprawopl`, .dro) only on the Ctrl+Alt+F7 hotkey. The injected key events (WScript `SendKeys`, then `keybd_event` with scan codes) did not reach the DOSBox window, and no .dro was written. I stopped there so as not to send more keys to the desktop.
- The original-code comparison above is a stronger check of the driver: it has no timing noise and covers whole songs.
- What remains unverified against hardware is the **synthesis** (the chip), which is by construction only. A DOSBox or real-hardware WAV of the same song would be the next reference.

## Hooks
None.

## TODO(port)
None.

## Files (all mine)
- **New:**
  - `src/mcport/opl_chip.h`, `src/mcport/opl_chip.cpp`
  - `src/mcport/opl_driver.h`, `src/mcport/opl_driver.cpp`
  - `src/tests/opl_test.cpp`, `src/tests/opl_test.cmake`
  - `src/mcengine/tables/opl.tables`, generating `src/mcengine/gen/opl_tables.h`
  - this report
- **Changed:**
  - `src/mcport/audio_mixer.h`: new `virtual void MidiOut::reset() {}`.
  - `src/mcport/audio_mixer.cpp`: `MusicSequencer::stop` calls `out_->reset()` after the 16 × (79, 7B), as `snd_midi_all_notes_off_60035` calls driver fn3.
  - `src/mcport/audio_sdl.h/.cpp`:
    - `MC_MUSIC=opl|midi|square|0` and `MC_MUSIC_VOLUME`;
    - the OPL2 output, with `data/inst.bnk` + `data/drum.bnk` loaded through `mc_read_unpacked`;
    - g_music_device 0 / HMI 0xa002 for OPL, 2 / 0xa001 for GM;
    - the SDL device is now opened whenever sound or music is wanted.
- **Unchanged:** `sound.h/.cpp`, `hmp.*` and `sndbank.*`. `music_load_bank` already picks the suffix from `g_music_device`.

## Music output and default
1. `MC_MUSIC=opl` (default) plays the FM bank through the OPL2 driver and emulator into the SDL mix.
2. If the FM files are missing or SDL audio did not open, it falls back to the MIDI mapper, then to the square synth.
3. `MC_MUSIC=midi` tries winmm first, then OPL, then the square synth.

**Recommended default: opl.**
- It is how most players heard the game: the shipped SNDSETUP.INF says SBLAST.
- It needs no device.
- It is deterministic and mixed with the effects by the same mixer under the same volume control.
- It sounds the same on every machine; the Windows GS synth varies and lags.

To flip the default, swap the two `use_opl()` / `use_midi()` orders in `audio_init`.

## Requested shared-file changes
1. **`src/mcport/main.cpp`: no change needed.** It already calls `audio_init(s_game.c_str())`, `audio_update()` and `audio_shutdown()` (round-5 integration). The output choice is made inside `audio_init`. The integrator may want to list the environment variables in the usage text:
   ```cpp
   // MC_MUSIC=opl (default: FM bank on the emulated OPL2) | midi (GM bank via the Windows MIDI mapper) | square | 0
   // MC_MUSIC_VOLUME=n   OPL2 music gain, 0x100 = default (256)
   ```
2. **`src/mcengine/gen/opl_tables.h`.** I generated it; the integrator regenerates with `python tools/port/gen_exe_tables.py opl`.
3. **CMake: no change.** The mcport glob picks up `opl_*.cpp`, and `tests/opl_test.cmake` declares the test.
4. **Optional, for reproducibility.** Copy `<scratch>/round6_A/opl_orig.py` to `tools/port/opl_orig.py`. Its paths are absolute and `pip install unicorn` is needed. Usage: `opl_test <game> <dir>`, then `python tools/port/opl_orig.py <dir>`.
5. **Build note.** `build_A` shows C4996 warnings from `mcengine/net.cpp` (strncpy) and `mcport/net_tcp.cpp` (getenv). They are task B's files; mine have none.

## Corrections to ENGINE.md / FORMATS.md / names (for task E / the integrator)

### Where the driver lives
- The OPL2 driver for HMI device 0xa002 is **internal to carpet.exe**.
- `hmimdrv.386` is "HMI MIDI Driver For Sound Master II" (`smii.com`), unrelated to the shipped SNDSETUP.INF.
- The shipped `CARPET.CD/SNDSETUP.INF` reads `MUSIC = SBLAST 388 0 0`, which is device 0xa002 and therefore the FM bank music<set>-0.
- `music_init_hmi_4d550` sets `DAT_0012e06f = 1` only on that path.

### Function names and comments
- **`snd_opl_alloc_voice_70697`.** Fix the comment: the fallback is `voice = channel` (`channel − 9` for 9..15), not `note % 9`. The stealing takes the first voice whose channel is the lowest channel with `DAT_000a4124 == 0` (no pitch bend received since the last fn3 reset).
- **`snd_opl_note_on_689da`.**
  - Arguments are (note, velocity, channel).
  - DAT_000a407c is initialised to 1 and never written, so a pending bend is always applied at note on.
  - The modulator level is written when the packed timbre byte +0xe (FB << 1 | CON) is 0.
  - The drum path does not test DAT_000a4074. It takes the pitch from byte +2 of the drum name record and writes only the carrier level.
- **`snd_opl_calc_bent_freq_686db(bend, note, voice)`.** The tables are `DAT_000a4320[note − 12 ± range]` = `DAT_000a42f0[note ± range]`, `DAT_000a44b8[range]` (down) and `DAT_000a44bc[11 − note % 12]` (up past the octave).
- **`snd_opl_controller_68893`.** CC 0x40 release replays queue entries [count]..[1] (off by one), and can loop forever when another channel is sustained. CC 0x66 sets the bend range directly.
- **`snd_opl_all_notes_off_68fc9` / `snd_opl_reset_controllers_69072`.** They write `DAT_000a41e8[channel]` (voice velocities indexed by channel) and `DAT_000a420c[channel]`, a flag table that is never read.
- **`snd_opl_pack_timbre_bank_6955a`.** Packs records 0 .. count − 3 (the last two stay raw).
- **`snd_opl_set_timbre_bank_69401`.** The percussion bank's name / data pointers use the **melodic** bank's header offsets.
- **`snd_opl_write_69f9c(reg, val)`.** cdecl. It writes the index to port 0x388, then 6 status reads, then the data to 0x389, then 35 status reads.
- **Thunk 0x69878 (fn1)** passes the far pointer `ds:0x13196c` to `snd_opl_driver_init_697e1`, after storing its own argument there. The driver reads the port from it.
- **Rhythm mode is never enabled.** 0xBD is only written as 0 or as shadow & 0xc0.

### Tables and globals
| address | meaning |
|---|---|
| 0xa402d | 18 bytes: per voice the modulator / carrier operator offsets {0,3}, {1,4}, {2,5}, {8,11}, … {18,21} |
| 0xa42dc | 64-byte velocity -> level table (0x3f … 0) |
| 0xa42f0 | `u32[128]` note -> (block << 10) \| F-number. [12..107] = 0xa4320, 8 octaves from C with F-numbers 0x157..0x287. [108..114] are high block-7 values. [0..11] overlap the velocity table |
| 0xa44bc.. | half F-numbers for the octave wrap |
| 0xa3fbc[0x20] | KSL/TL shadow by operator |
| 0xa3fdc[0x10] | SL/RR shadow by operator, overlapping 0xa3fec |
| 0xa3fec[9] | A0 shadow |
| 0xa3ff5[9] | B0 shadow |

Proposed labels for the globals:

| address | label |
|---|---|
| 0xa401c | `g_opl_voice_note` |
| 0xa40c0 | `g_opl_voice_channel` |
| 0xa4080 | `g_opl_channel_program` |
| 0xa40e4 | `g_opl_channel_bend` |
| 0xa4124 | `g_opl_channel_bent` |
| 0xa4164 | `g_opl_bend_range` |
| 0xa41a8 | `g_opl_channel_volume` |
| 0xa41e8 | `g_opl_voice_velocity` |
| 0xa420c | `g_opl_volume_set` |
| 0xa424c | `g_opl_sustain` |
| 0xa42a8 | `g_opl_sustain_queue_count` |
| 0xa42ac | `g_opl_sustain_queue` |
| 0xa407c | `g_opl_apply_bend_on_note` (= 1) |
| 0xa41a4 | `g_opl_next_bank_is_drums` |
| 0xa4074 | `g_opl_drum_bank_loaded` |

### FORMATS.md: `data/inst.bnk`, `data/drum.bnk` (AdLib .BNK, RNC-packed, 5404 bytes unpacked)

**Header.**

| offset | type | field |
|---|---|---|
| +0 | u8, u8 | version (0.0) |
| +2 | char[6] | "ADLIB-" |
| +8 | u16 | records used (128) |
| +0xa | u16 | records (128) |
| +0xc | u32 | name table offset (0x1c) |
| +0x10 | u32 | data offset (0x61c) |
| +0x14 | — | 8 bytes padding |

**Name record (12 bytes).**

| offset | type | field |
|---|---|---|
| +0 | u16 | data index (= record number here) |
| +2 | u8 | flags in standard BNK; in drum.bnk the **pitch note** the HMI driver plays the drum at |
| +3 | char[9] | name, e.g. "piano1.i", "SBBD.ins" |

**Data record (30 bytes).**

| offset | field |
|---|---|
| +0 | percussive flag (unused) |
| +1 | voice number (unused) |
| +2..+0xe | the 13 modulator parameters: KSL, MULT, FB, AR, SL, EG(T), DR, RR, TL, AM, VIB, KSR, CON |
| +0xf..+0x1b | the same 13 parameters for the carrier |
| +0x1c | modulator waveform |
| +0x1d | carrier waveform |

CON is the register bit as stored: 0 = FM, 1 = additive; organs and whistles have 1. The carrier's FB / CON are unused.

**The driver's in-place packing.**

| byte | operator / register | value |
|---|---|---|
| +0xb | modulator, reg 0x20 | AM<<7 \| VIB<<6 \| EG<<5 \| KSR<<4 \| MULT |
| +2 | modulator, reg 0x40 | KSL<<6 \| TL |
| +5 | modulator, reg 0x60 | AR<<4 \| DR |
| +6 | modulator, reg 0x80 | SL<<4 \| RR |
| +0xe | reg 0xC0 | FB<<1 \| CON |
| +0x18 | carrier, reg 0x20 | AM<<7 \| VIB<<6 \| EG<<5 \| KSR<<4 \| MULT |
| +0xf | carrier, reg 0x40 | KSL<<6 \| TL |
| +0x12 | carrier, reg 0x60 | AR<<4 \| DR |
| +0x13 | carrier, reg 0x80 | SL<<4 \| RR |
| +0x1c / +0x1d | modulator / carrier, reg 0xE0 | waveform |

- Melodic: program p = data record p.
- Drums (MIDI channel 10): note n = data record n, played at the name record's pitch byte. Unused notes are "blank.in" (TL 63).

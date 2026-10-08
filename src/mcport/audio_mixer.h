// Platform audio without SDL (round 5, task A): the software mixer for the digital voices, the HMP
// music sequencer (a translation of the HMI sequencer in carpet.exe), a trivial square-wave synth,
// and AudioEngine = SoundBackend (mcengine/sound.h) built from them. audio_sdl.cpp puts an SDL audio
// device and the Windows MIDI mapper under it; tests drive it offline (AudioEngine::render).
// Report: docs/analysis/port_audio.md. Floating point is allowed here (platform code), but the mixer
// is integer so the tests can check it exactly.
#pragma once
#include <cstdint>
#include <mutex>
#include <vector>
#include "sound.h"
#include "hmp.h"

// ---- MIDI output ------------------------------------------------------------------------------------
// Where the sequencer sends its events (the HMI MIDI driver's fn0 "send event"). An external device
// (winmm) only implements send(); a software synth also renders into the mix.
struct MidiOut {
    virtual ~MidiOut() = default;
    // One channel message: 2-byte messages (Cx, Dx) pass d2 = 0.
    virtual void send(uint8_t status, uint8_t d1, uint8_t d2) = 0;
    virtual void panic() {}                       // silence everything at once (shutdown / device change)
    // The HMI driver's fn3 "reset" (snd_midi_all_notes_off_60035 calls it after the 16 x (79, 7B)): only
    // the internal OPL2 driver does anything (snd_opl_reset_state_69319); a GM device needs nothing.
    virtual void reset() {}
    virtual bool renders() const { return false; }
    // Add `frames` stereo frames at `rate` Hz into lr (interleaved, 16-bit scale).
    virtual void render(int32_t * /*lr*/, int /*frames*/, int /*rate*/) {}
};

// ---- digital voices -----------------------------------------------------------------------------------
// 32 voices of 8-bit unsigned mono PCM at any rate, resampled with linear interpolation to the output
// rate. Volume 0..0x7fff (linear, 0x7fff ~ unity), pan 0..0xffff (0 left, 0x7fff centre, 0xffff
// right; balance law: the far side falls linearly, the near side stays at unity), HMI loop count
// (-1 forever, n = n further passes) honoured when the flags have 0x4000 (_LOOPING); the volume is
// honoured when they have 0x100 (_VOLUME), the pan when they have 0x200 (_PANNING). stop() and volume
// changes ramp the gain (RAMP_SAMPLES for full scale) so nothing clicks; a stopped voice is
// "not playing" at once and finishes its ramp in one of the extra slots. Not thread-safe by itself
// (AudioEngine locks).
class AudioMixer {
public:
    enum { VOICES = 32, SLOTS = 48, RAMP_SAMPLES = 128, FLAG_VOLUME = 0x100, FLAG_PANNING = 0x200, FLAG_LOOPING = 0x4000 };
    void set_output_rate(int hz) { out_rate_ = hz > 0 ? hz : 44100; }
    int  output_rate() const { return out_rate_; }
    // Mix gain on the sum, 0x100 = unity (default 0x60: one voice at full volume = -8.5 dBFS; the game's
    // ambient loops and explosions overlap a lot - movie 0 clipped 1 % of its samples at 0x80).
    void set_master(int q8) { master_q8_ = q8; }
    int  master() const { return master_q8_; }

    int  start(const uint8_t *data, uint32_t len, int rate, int volume, int pan, int loop_count, int flags);
    void stop(int handle);
    void set_volume(int handle, int volume);
    bool is_playing(int handle) const;
    void stop_all_now();                           // drop every voice without a ramp (sample memory goes away)
    int  playing() const;                          // voices that report is_playing
    int  sounding() const;                         // voices still producing output (incl. stop ramps)
    // Add `frames` stereo frames into lr (interleaved int32, 16-bit scale, before the master gain).
    void mix(int32_t *lr, int frames);

    // Gains of the pan law in Q15 (exposed for the tests): left / right gain of `volume` at `pan`.
    static void pan_gains(int volume, int pan, int *gl, int *gr);

private:
    struct Voice {
        enum State : uint8_t { FREE, PLAYING, RAMPING } state = FREE;
        const uint8_t *data = nullptr;
        uint32_t len = 0;
        uint64_t pos = 0;          // 32.16 position in source samples
        uint64_t step = 0;         // 32.16 source samples per output frame
        int loops = 0;             // further passes; -1 forever
        int gl = 0, gr = 0;        // current gains Q15
        int tl = 0, tr = 0;        // target gains Q15
        int volume = 0, pan = 0x7fff;
        uint16_t gen = 0;
    };
    Voice v_[SLOTS];
    uint16_t next_gen_ = 1;
    int out_rate_ = 44100;
    int master_q8_ = 0x60;
    Voice *lookup(int handle);
    const Voice *lookup(int handle) const;
};

// ---- music sequencer ----------------------------------------------------------------------------------
// The HMI song player of carpet.exe with channel mapping off (music_play_track_5c0a0 passes 0 to
// snd_midi_set_flag_5e4ee): snd_midi_init_song_5e53a + snd_midi_song_setup_5ec41 (load), the timer
// callback 0x66898 (tick, rate = the song's header +0x38 Hz), the pass-through branch of
// hmi_midi_send_event_5d69b (controller 7 scaled by the master volume: v * master >> 7),
// snd_midi_reset_channels_5e0d9 (song end / stop), snd_midi_stop_song_5eb38,
// snd_midi_all_notes_off_60035, snd_midi_set_volume_5ef56 (master volume: controller 7 =
// master * 0x7f >> 7 on all 16 channels, the per-channel volume table 0xa2566 keeps its initial 0x7f in
// pass-through mode) and the layer volume of music_mood_fade_cb_1f6d0 (controller 7 = value, raw,
// on channels 3, 4, 5 through hmi_midi_send_event_5e4b8). The song does not loop by itself: at its end
// it stops, resets the channels and rewinds; music_update_1f800 sees music_done() and restarts it.
class MusicSequencer {
public:
    ~MusicSequencer();
    void set_output(MidiOut *out) { out_ = out; }
    MidiOut *output() const { return out_; }
    // HMI device id of the driver the tracks are mapped to (mc_hmp_track_plays). Default 0xa001.
    void set_device(uint32_t id) { device_ = id; }
    bool load_and_start(const uint8_t *song, uint32_t len);   // copies the bytes; false = not an HMP
    void stop();                                    // music_stop: stop song if playing, clear, all notes off
    bool done() const { return !active_; }          // music_song_done_5cf40
    void set_master_volume(int v);                  // snd_midi_set_volume_5ef56 (0..0x7f)
    void set_layer_volume(int v);                   // music_mood_fade_cb_1f6d0: controller 7 on channels 3..5
    void tick();                                    // one HMI timer event of the song
    int  rate() const { return song_ok_ ? (int)song_.rate : 0; }
    bool loaded() const { return song_ok_; }
    const mc_hmp_song &song() const { return song_; }
    uint32_t ticks_played() const { return ticks_; }
    int  master_volume() const { return master_; }

private:
    void send(uint8_t st, uint8_t d1, uint8_t d2) { if (out_) out_->send(st, d1, d2); }
    void reset_channels();
    void rewind();
    void unload();
    MidiOut *out_ = nullptr;
    uint32_t device_ = MC_HMP_DEVICE_MPU401;
    std::vector<uint8_t> bytes_;
    mc_hmp_song song_{};
    bool song_ok_ = false;
    bool active_ = false;                           // DAT_0009f9ee[song]
    int  master_ = 0x7f;                            // DAT_000a2565
    struct Track { bool on = false; uint32_t idx = 0; uint32_t counter = 0; };
    Track tr_[MC_HMP_MAX_TRACKS];
    int  tracks_left_ = 0;                          // DAT_0009f8da[song]
    uint32_t ticks_ = 0;
};

// ---- square-wave synth --------------------------------------------------------------------------------
// A deliberately trivial General MIDI stand-in for listening tests and machines without a MIDI device:
// polyphonic pulse waves (duty from the program), velocity, controller 7 / 10, pitch bend +-2
// semitones, a short decay envelope, noise bursts on channel 10 (drums). Not the game's sound.
class SquareSynth : public MidiOut {
public:
    void send(uint8_t status, uint8_t d1, uint8_t d2) override;
    void panic() override;
    bool renders() const override { return true; }
    void render(int32_t *lr, int frames, int rate) override;
    int  notes_on() const;
private:
    struct Note { bool on = false, held = false; uint8_t ch = 0, key = 0, vel = 0; double phase = 0, env = 0; uint32_t age = 0, noise = 1; };
    struct Chan { uint8_t program = 0, volume = 100, pan = 64; int bend = 0x2000; };
    Note n_[32];
    Chan c_[16];
    uint32_t age_ = 0;
};

// ---- the backend -------------------------------------------------------------------------------------
// SoundBackend for mcport and the tests: AudioMixer for the voices, MusicSequencer -> MidiOut for the
// music, the two HMI timer events for the fades. Every SoundBackend call and render() lock one mutex, so
// the game thread, the audio callback and a MIDI timer thread can share it.
//   Music clock: CLOCK_RENDER advances the sequencer inside render() (sample accurate; needed for a
//   rendering MidiOut and for offline tests), CLOCK_EXTERNAL leaves it to advance_music_us() (a timer
//   thread driving an external MIDI device).
//   Fade timers (music_fade_tick at music_fade_rate Hz): pump_fade_timers(elapsed_us) on the game thread.
class AudioEngine : public SoundBackend {
public:
    enum Clock { CLOCK_RENDER, CLOCK_EXTERNAL };
    AudioEngine();
    void set_output_rate(int hz);
    int  output_rate() const { return mixer_.output_rate(); }
    void set_midi(MidiOut *out, Clock clock);
    void set_music_device(uint32_t hmi_id);

    // SoundBackend
    int  start(int owner, int sample, const uint8_t *data, uint32_t len, int rate, int volume, int pan,
               int loop_count, int hmi_flags) override;
    void stop(int handle) override;
    void set_volume(int handle, int volume) override;
    bool is_playing(int handle) override;
    void flush() override;
    bool music_play(int track, const uint8_t *song, uint32_t len) override;
    void music_stop() override;
    bool music_done() override;
    void music_set_volume(int volume) override;
    void music_set_layer_volume(int volume) override;
    void music_timer(MusicTimer which, int rate_hz) override;

    // Produce `frames` stereo int16 frames (the SDL callback / offline render). The sum passes a soft knee
    // above SOFT_KNEE (soft_clip) before the int16 clamp.
    enum { SOFT_KNEE = 24576 };
    static int soft_clip(int32_t x);
    void render(int16_t *out, int frames);
    // CLOCK_EXTERNAL: advance the sequencer by real time.
    void advance_music_us(uint64_t us);
    // Game thread: call music_fade_tick(which) as often as the rates ask for `elapsed_us` of time
    // (at most 64 calls per timer per pump).
    void pump_fade_timers(uint64_t elapsed_us);

    // inspection (tests)
    AudioMixer &mixer() { return mixer_; }
    MusicSequencer &sequencer() { return seq_; }
    int  voices_started() const { return starts_; }
    int  timer_rate(MusicTimer w) const { return timer_rate_[w]; }
    std::mutex &lock() { return mu_; }

private:
    void advance_music_frames(int frames);   // CLOCK_RENDER, under the lock
    std::mutex mu_;
    AudioMixer mixer_;
    MusicSequencer seq_;
    Clock clock_ = CLOCK_RENDER;
    uint64_t music_acc_ = 0;                 // CLOCK_RENDER: frames * rate accumulator (units of 1/out_rate s)
    uint64_t music_us_acc_ = 0;              // CLOCK_EXTERNAL: microseconds * rate
    int timer_rate_[2] = {0, 0};
    uint64_t timer_acc_[2] = {0, 0};         // microseconds * rate
    int starts_ = 0;
    std::vector<int32_t> acc_;
};

// Write interleaved 16-bit PCM as a .wav (channels 1 or 2). Returns true on success.
bool audio_write_wav(const char *path, const int16_t *pcm, size_t frames, int channels, int rate);

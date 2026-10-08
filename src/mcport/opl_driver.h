// The internal OPL2 MIDI driver of carpet.exe (HMI device 0xa002, round 6, task A) and the MidiOut that
// plays the FM music bank music<set>-0 through it on the software OPL2 (opl_chip.h).
//
// HmiOplDriver is a translation of snd_opl_* (0x68619..0x6a0e6, 0x704cd..0x7075e; the five far entries
// 0x69849 / 0x69878 / 0x698b3 / 0x698d1 / 0x698ef are fn0 send / fn1 init / fn2 uninit / fn3 reset /
// fn4 set instrument data of the HMI driver table). It turns MIDI events into OPL register writes exactly
// as the original: AdLib .BNK timbres packed into register bytes, program change -> timbre, channel 10
// (index 9) -> drum.bnk record = note with the pitch from the name record's byte +2, 9 melodic voices
// (rhythm mode never used), voice allocation / stealing, the F-number table, pitch bend with its range,
// velocity * channel volume -> carrier TL. Every quirk of the original is kept (see port_opl.md
// "Driver quirks"). Integer only; the state arrays carry the original globals' names.
//
// OplMidiOut is the MidiOut (audio_mixer.h) for AudioEngine: send() -> driver fn0, reset() -> fn3, the
// register writes go to the chip with a small per-write delay (the original's port I/O waits), render()
// runs the chip at 49716 Hz and resamples (linear) to the device rate, mono on both sides.
// Report: docs/analysis/port_opl.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include "audio_mixer.h"
#include "opl_chip.h"

// Where the driver's register writes go (snd_opl_write_69f9c: index port 0x388, data port 0x389).
struct OplWriter {
    virtual ~OplWriter() = default;
    virtual void opl_write(uint8_t reg, uint8_t val) = 0;
};

class HmiOplDriver {
public:
    explicit HmiOplDriver(OplWriter *w = nullptr);    // the globals' initial values from the image
    HmiOplDriver(const HmiOplDriver &) = delete;
    HmiOplDriver &operator=(const HmiOplDriver &) = delete;
    void set_writer(OplWriter *w) { w_ = w; }

    // fn1 init (snd_opl_driver_init_697e1): port 0x388 or 0x380, else 1 (no chip writes)
    int  init(uint32_t port = 0x388);
    void uninit();                                      // fn2 (snd_opl_driver_uninit_6977c)
    void reset();                                       // fn3 (snd_opl_reset_state_69319)
    // fn4 sosMIDISetInsData (snd_opl_set_timbre_bank_69401): the first call takes the melodic bank
    // (inst.bnk), the second the percussion bank (drum.bnk). The bank is copied and packed in place
    // (snd_opl_pack_timbre_bank_6955a); the bytes are the RNC-unpacked .bnk file.
    void set_timbre_bank(const uint8_t *bnk, size_t len);
    // fn0 (snd_opl_midi_event_6911c): one MIDI channel message, ev = {status, d1, d2}
    void midi_event(const uint8_t ev[3]);

    // ---- inspection (tests) ----
    int  voice_note(int v) const { return a401c_[v]; }            // DAT_000a401c: 0 = free
    int  voice_channel(int v) const { return (int)a40c0_[v]; }    // DAT_000a40c0
    int  program(int ch) const { return (int)a4080_[ch]; }        // DAT_000a4080
    int  channel_volume(int ch) const { return (int)a41a8_[ch]; } // DAT_000a41a8
    bool drum_bank_loaded() const { return a4074_ != 0; }
    const std::vector<uint8_t> &melodic_bank() const { return mel_; }
    const std::vector<uint8_t> &drum_bank() const { return drum_; }
    // the packed 30-byte timbre the driver uses for program p / drum note n (nullptr when outside the bank)
    const uint8_t *melodic_timbre(int p) const { return bank_ptr(mel_, mel_data_ + (size_t)p * 0x1e, 0x1e); }
    const uint8_t *drum_timbre(int n) const { return bank_ptr(drum_, drum_data_ + (size_t)n * 0x1e, 0x1e); }
    int  drum_pitch(int n) const { const uint8_t *r = bank_ptr(drum_, drum_names_ + (size_t)n * 0xc + 2, 1); return r ? *r : 0; }

    // snd_opl_calc_bent_freq_686db, exposed for the tests
    uint32_t calc_bent_freq(uint32_t bend, uint32_t note, uint32_t voice) const;

private:
    void write(uint8_t reg, uint8_t val) { if (w_) w_->opl_write(reg, val); }   // snd_opl_write_69f9c
    static const uint8_t *bank_ptr(const std::vector<uint8_t> &b, size_t off, size_t n) {
        return off + n <= b.size() ? b.data() + off : nullptr;
    }
    static uint32_t freq(uint32_t idx);                 // DAT_000a42f0[idx], 0 outside the extracted table
    void pitch_bend(uint8_t ch, uint8_t msb);           // snd_opl_pitch_bend_68619
    void controller(uint8_t ch, uint8_t cc, uint8_t val); // snd_opl_controller_68893
    void note_on(uint8_t note, uint8_t vel, uint8_t ch);  // snd_opl_note_on_689da
    void note_off(const uint8_t ev[3]);                 // snd_opl_note_off_68eca: ev = {note, velocity, channel}
    void all_notes_off(uint32_t ch);                    // snd_opl_all_notes_off_68fc9
    void reset_controllers(uint32_t ch);                // snd_opl_reset_controllers_69072
    int  init_chip(uint32_t port);                      // snd_opl_init_chip_69940
    void voice_set_freq(uint8_t v, uint32_t f);         // snd_opl_voice_set_freq_699e1
    void clear_voices();                                // snd_opl_clear_voices_69a77
    void voice_key_on(uint8_t v, uint32_t f);           // snd_opl_voice_key_on_69b31
    int  voice_key_off(uint8_t v);                      // snd_opl_voice_key_off_69bf3
    int  silence_all();                                 // snd_opl_silence_all_69c73
    void load_timbre(const uint8_t *t, uint8_t v);      // snd_opl_load_timbre_69d61
    void set_channel_volume(uint32_t ch, uint8_t vol);  // snd_opl_set_channel_volume_704cd
    uint8_t alloc_voice(uint8_t ch, uint8_t note) const; // snd_opl_alloc_voice_70697
    static void pack_timbre_bank(std::vector<uint8_t> &b); // snd_opl_pack_timbre_bank_6955a
    uint8_t carrier_level(uint8_t v, uint32_t chvol) const;   // the level formula shared by 689da / 704cd
    static uint8_t mod_op(uint8_t v);                   // DAT_000a402d[v * 2]
    static uint8_t car_op(uint8_t v);                   // DAT_000a402e[v * 2]

    OplWriter *w_ = nullptr;
    // ---- the driver's globals (initial values as in the image) ----
    uint32_t a3fa8_ = 0x388;                            // port
    // The register shadows are one block in the original and overlap: DAT_000a3fbc[0x20] KSL / TL by
    // operator offset, DAT_000a3fdc SL / RR by operator offset - only 0x10 bytes before DAT_000a3fec (0xA0+v
    // F-number shadow), so the SL / RR shadows of operators 0x10..0x15 (voices 6..8) ARE the F-number
    // shadows of voices 0..5: load_timbre overwrites them and the fast-release writes of note_on read them
    // back. DAT_000a3ff5 = 0xB0+v shadow (block / key-on).
    uint8_t  sh_[0x42] = {};
    uint8_t *const a3fbc_ = sh_;
    uint8_t *const a3fdc_ = sh_ + 0x20;
    uint8_t *const a3fec_ = sh_ + 0x30;
    uint8_t *const a3ff5_ = sh_ + 0x39;
    uint8_t  a4010_ = 0;                                // 0xBD shadow
    uint8_t  a4011_[11] = {};                           // timbre loaded flags
    uint8_t  a401c_[9] = {};                            // note playing per voice (0 = free)
    uint8_t  a402b_ = 0;                                // chip ready
    uint8_t  a402c_ = 0;
    uint32_t a4040_ = 0;                                // driver open
    uint32_t a4044_ = 0;                                // melodic bank count
    uint32_t a405e_ = 0;
    uint32_t a4074_ = 0;                                // percussion bank loaded
    uint32_t a407c_ = 1;                                // never written: pending bends are applied at note on
    uint32_t a4080_[16] = {};                           // program per channel
    uint32_t a40c0_[9] = {};                            // channel per voice
    uint32_t a40e4_[16];                                // pitch bend MSB per channel (0x40 centre)
    uint32_t a4124_[16] = {};                           // a pitch bend was received on the channel
    uint32_t a4164_[16];                                // bend range in semitones (default 2)
    uint32_t a41a4_ = 0;                                // set_timbre_bank: next call is the percussion bank
    uint32_t a41a8_[16];                                // channel volume (controller 7)
    // DAT_000a41e8[9] velocity per voice followed directly by DAT_000a420c[16] "volume set" flags: the
    // controller 0x79 / 0x7b handlers index the velocity table with the channel (0..15), i.e. they write
    // the velocity of voice <ch> and, for channels 9..15, DAT_000a420c[ch - 9]. One array keeps that.
    uint32_t a41e8_[9 + 16];
    uint32_t a424c_[16] = {};                           // sustain (controller 0x40)
    uint32_t a42a8_ = 0;                                // queued note-offs while sustained
    uint8_t  a42ac_[18 * 3] = {};                       // the queue (16 entries; [16] = the velocity table's first bytes)
    // the banks (copies, packed in place) and the offsets the driver keeps
    std::vector<uint8_t> mel_, drum_;
    size_t mel_names_ = 0, mel_data_ = 0;               // DAT_000a4052, DAT_000a4058 (offsets into mel_)
    size_t drum_names_ = 0, drum_data_ = 0;             // DAT_000a4068, DAT_000a406e (offsets into drum_)
};

// The FM music output: HmiOplDriver on OplChip, rendered into the mix.
class OplMidiOut : public MidiOut, private OplWriter {
public:
    OplMidiOut();
    // What music_init_hmi_4d550 does for device 0xa002: driver init (fn1), set instrument data (fn4) with
    // inst.bnk, then with drum.bnk. Bytes = the RNC-unpacked files. False when a bank is missing / short.
    bool load_banks(const uint8_t *inst, size_t inst_len, const uint8_t *drum, size_t drum_len);
    void send(uint8_t status, uint8_t d1, uint8_t d2) override;
    void reset() override;                              // driver fn3 (snd_midi_all_notes_off_60035 ends with it)
    void panic() override;
    bool renders() const override { return true; }
    void render(int32_t *lr, int frames, int rate) override;

    // Output gain on the chip's sum (0x100: one channel at full scale = +-4095 of 16-bit); default 0x100.
    void set_gain(int q8) { gain_q8_ = q8; }
    // Chip time each register write takes, in 1/256 chip samples (default 445 = 35 us: the original's
    // 6 + 35 status-port reads around the two port writes). 0 = all writes of an event at once.
    void set_write_cost(uint32_t q8) { write_cost_q8_ = q8; }
    OplChip &chip() { return chip_; }
    HmiOplDriver &driver() { return drv_; }
    uint64_t writes() const { return writes_; }

private:
    void opl_write(uint8_t reg, uint8_t val) override;
    void flush_writes();                                // apply every queued write now (reset / panic)
    struct W { uint64_t at_q8; uint8_t reg, val; };
    OplChip chip_;
    HmiOplDriver drv_;
    std::deque<W> q_;
    uint64_t chip_time_q8_ = 0;                         // chip samples produced so far, << 8
    uint64_t cursor_q8_ = 0;                            // time stamp of the next write
    uint32_t write_cost_q8_ = 445;
    int gain_q8_ = 0x100;
    uint64_t writes_ = 0;
    // resampler: position between the last two chip samples in 1/65536
    int32_t prev_ = 0, cur_ = 0;
    uint32_t frac_ = 0x10000;
    int step_rate_ = 0;
    uint32_t step_ = 0;
    void chip_sample();
};

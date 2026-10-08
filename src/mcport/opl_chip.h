// Software YM3812 (OPL2) for the FM music (round 6, task A). Written from the YM3812 application manual
// and the public register documentation (no code from other emulators): 9 two-operator channels, rhythm
// mode (bass drum, snare, tom, cymbal, hi-hat on channels 6..8), all registers (0x01 WSE / test,
// 0x02..0x04 timers, 0x08 CSM / NTS, 0x20..0x35 AM VIB EGT KSR MULT, 0x40..0x55 KSL TL, 0x60..0x75 AR DR,
// 0x80..0x95 SL RR, 0xA0..0xB8 F-number / block / key-on, 0xBD AM / VIB depth + rhythm, 0xC0..0xC8
// feedback / connection, 0xE0..0xF5 waveform), the envelope generator with the rate / key-scale rules,
// KSL, tremolo and vibrato LFOs, the four waveforms, operator feedback.
//
// The chip runs at its own rate (clock 3.579545 MHz / 72 = 49715.9 Hz) and produces one mono sample per
// call of sample(); OplMidiOut (opl_driver.h) resamples it to the device rate. The arithmetic follows the
// chip's log-domain structure (a log-sine table, an exponent table, attenuation in 0.1875 dB units); it
// is close to the real chip by construction and by the published tables, not bit-exact. Platform code:
// the two tables are built with floating point once, the synthesis is integer.
// Report: docs/analysis/port_opl.md.
#pragma once
#include <cstdint>

class OplChip {
public:
    static constexpr int CLOCK = 3579545;                       // Hz (NTSC colour burst crystal)
    static constexpr double RATE = CLOCK / 72.0;                // 49715.9 Hz
    enum EnvState : uint8_t { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

    OplChip();
    void reset();                                               // power-on state (all registers 0)
    void write(uint8_t reg, uint8_t val);
    uint8_t read_status() const { return status_; }             // port 0x388 read: IRQ / timer flags
    int32_t sample();                                           // one output sample (sum of the channels)

    // ---- inspection (tests) ----
    // Operator by register offset 0..0x15 (6, 7, 0xe, 0xf unused).
    int  env_attenuation(int op) const { return op_[op].env; }  // 0..511, 0.1875 dB units
    EnvState env_state(int op) const { return (EnvState)op_[op].state; }
    int  total_attenuation(int op) const;                       // envelope + TL + KSL (+ tremolo now), clamped 0..511
    uint8_t reg(int r) const { return regs_[r & 0xff]; }
    int32_t channel_output(int ch) const { return ch_out_[ch]; } // last sample's output of channel 0..8
    int32_t rhythm_output(int which) const { return rhy_out_[which]; } // last sample: 0 BD, 1 SD, 2 TOM, 3 CY, 4 HH
    uint64_t samples() const { return samples_; }

private:
    struct Op {
        // registers
        bool am = false, vib = false, egt = false, ksr = false;
        uint8_t mult = 0, ksl = 0, tl = 0, ar = 0, dr = 0, sl = 0, rr = 0, ws = 0;
        // state
        uint32_t phase = 0;                // 20-bit phase accumulator (10 bits of the cycle above 10 bits of fraction)
        uint16_t env = 511;                // attenuation 0..511
        uint8_t state = ENV_OFF;
        uint8_t key = 0;                   // bit 0 channel key-on (0xB0), bit 1 rhythm key (0xBD), bit 2 CSM
        int32_t out[2] = {0, 0};           // last two outputs (feedback)
    };
    struct Chan {
        uint16_t fnum = 0;
        uint8_t block = 0, fb = 0;
        bool key = false, cnt = false;
    };

    void write_op(int op, int base, uint8_t val);
    void set_key(int op, int bit, bool on);
    void env_step(Op &o, const Chan &c);
    void phase_step(Op &o, const Chan &c);
    int  ksl_atten(const Op &o, const Chan &c) const;
    int  op_level(const Op &o, const Chan &c) const;            // total attenuation incl. tremolo, 0..511
    int32_t op_wave(const Op &o, uint32_t phase10, int level) const;
    int32_t op_out(Op &o, const Chan &c, int32_t pm);           // pm = phase offset in 1/1024 cycle
    void timers_step();

    Op op_[22];
    Chan ch_[9];
    uint8_t regs_[256];
    uint8_t status_ = 0;
    bool wse_ = false, csm_ = false, nts_ = false, dam_ = false, dvb_ = false, rhythm_ = false;
    uint32_t env_counter_ = 0;             // global envelope clock (samples)
    uint32_t lfo_counter_ = 0;             // tremolo / vibrato clock (samples)
    uint32_t noise_ = 1;                   // 23-bit noise LFSR (rhythm)
    uint32_t timer_cycles_ = 0;            // chip clock cycles towards the next 80 us timer tick
    uint8_t  t1_count_ = 0, t2_count_ = 0, t2_div_ = 0, t_running_ = 0;
    bool     csm_pulse_ = false, csm_key_ = false;
    int32_t  ch_out_[9] = {};
    int32_t  rhy_out_[5] = {};
    uint64_t samples_ = 0;
};

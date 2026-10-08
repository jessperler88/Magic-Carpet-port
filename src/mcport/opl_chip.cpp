// Software YM3812 (OPL2). See opl_chip.h. Written from the YM3812 application manual / public register
// documentation; the structure (log-sine + exponent lookup, 9-bit attenuation in 0.1875 dB steps, the
// envelope rate = 4 * R + Rof rule with a 4-step fractional pattern, KSL in 3 dB/octave base units,
// tremolo 3.7 Hz / 1 or 4.8 dB, vibrato 6.1 Hz / 7 or 14 cents) is the data sheet's.
#include "opl_chip.h"
#include <cmath>
#include <cstring>

namespace {

// Quarter-wave log-sine: -log2(sin(x)) in 1/256 units (one unit = 6.02 / 256 dB = 0.0235 dB), and the
// exponent table 2^(-i/256) at 12-bit full scale. Built once; the chip only does integer lookups.
struct Tables {
    uint16_t logsin[256];
    uint16_t exp[256];
    Tables() {
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < 256; i++) {
            double s = std::sin((i + 0.5) * pi / 512.0);
            logsin[i] = (uint16_t)std::lround(-std::log2(s) * 256.0);
            exp[i] = (uint16_t)std::lround(4095.0 * std::pow(2.0, -i / 256.0));
        }
    }
};
const Tables &tables() { static const Tables t; return t; }

// MULT 0..15 -> frequency multiple x2 (0 = 1/2; 11 = 10, 13 = 12, 14 = 15 per the data sheet)
const uint8_t kMultX2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
// KSL base attenuation for F-number bits 9..6 at block 7, in 0.375 dB units (3 dB/octave below);
// 0, 9, 12, 13.875, 15, 16.125, 16.875, 17.625, 18, 18.75, 19.125, 19.5, 19.875, 20.25, 20.625, 21 dB.
const uint8_t kKslRom[16] = {0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56};
// Envelope increments over 8 clocks for rate fraction R & 3 (4, 5, 6, 7 steps in 8).
const uint8_t kEnvPattern[4][8] = {
    {1, 0, 1, 0, 1, 0, 1, 0}, {1, 0, 1, 0, 1, 1, 1, 0}, {1, 1, 1, 0, 1, 1, 1, 0}, {1, 1, 1, 1, 1, 1, 1, 0}};
// Vibrato: 8 steps of 1024 samples (6.07 Hz), offset in units of F-number >> 7 (x2).
const int8_t kVibTab[8] = {0, 1, 2, 1, 0, -1, -2, -1};

// operator register offset -> channel, -1 = unused offset
int op_channel(int off) {
    if (off < 0 || off > 0x15) return -1;
    int r = off & 7;
    if (r > 5) return -1;
    return (off >> 3) * 3 + r % 3;
}
bool op_is_carrier(int off) { return (off & 7) >= 3; }

} // namespace

OplChip::OplChip() { reset(); }

void OplChip::reset() {
    tables();
    for (auto &o : op_) o = Op{};
    for (auto &c : ch_) c = Chan{};
    std::memset(regs_, 0, sizeof regs_);
    status_ = 0;
    wse_ = csm_ = nts_ = dam_ = dvb_ = rhythm_ = false;
    env_counter_ = lfo_counter_ = 0;
    noise_ = 1;
    timer_cycles_ = 0;
    t1_count_ = t2_count_ = t2_div_ = 0;
    t_running_ = 0;
    csm_pulse_ = csm_key_ = false;
    std::memset(ch_out_, 0, sizeof ch_out_);
    std::memset(rhy_out_, 0, sizeof rhy_out_);
    samples_ = 0;
}

void OplChip::set_key(int op, int bit, bool on) {
    Op &o = op_[op];
    uint8_t old = o.key;
    o.key = on ? (uint8_t)(o.key | bit) : (uint8_t)(o.key & ~bit);
    if (!old && o.key) {                    // key on: restart the attack from the current level, phase 0
        o.state = ENV_ATTACK;
        o.phase = 0;
    } else if (old && !o.key && o.state != ENV_OFF) {
        o.state = ENV_RELEASE;
    }
}

void OplChip::write_op(int op, int base, uint8_t val) {
    Op &o = op_[op];
    switch (base) {
    case 0x20:
        o.am = (val & 0x80) != 0; o.vib = (val & 0x40) != 0; o.egt = (val & 0x20) != 0; o.ksr = (val & 0x10) != 0;
        o.mult = val & 0x0f;
        break;
    case 0x40: o.ksl = val >> 6; o.tl = val & 0x3f; break;
    case 0x60: o.ar = val >> 4; o.dr = val & 0x0f; break;
    case 0x80: o.sl = val >> 4; o.rr = val & 0x0f; break;
    case 0xe0: o.ws = val & 3; break;
    default: break;
    }
}

void OplChip::write(uint8_t reg, uint8_t val) {
    uint8_t old = regs_[reg];
    regs_[reg] = val;
    switch (reg & 0xf0) {
    case 0x00:
        if (reg == 0x01) wse_ = (val & 0x20) != 0;
        else if (reg == 0x04) {
            if (val & 0x80) { status_ = 0; regs_[4] = old; break; }   // IRQ reset: only clears the flags
            if ((val & 1) && !(t_running_ & 1)) t1_count_ = regs_[2];
            if ((val & 2) && !(t_running_ & 2)) { t2_count_ = regs_[3]; t2_div_ = 0; }
            t_running_ = val & 3;
        } else if (reg == 0x08) {
            csm_ = (val & 0x80) != 0; nts_ = (val & 0x40) != 0;
        }
        break;
    case 0x20: case 0x30: case 0x40: case 0x50: case 0x60: case 0x70: case 0x80: case 0x90: case 0xe0: case 0xf0: {
        int off = reg & 0x1f;
        if (op_channel(off) < 0) break;
        write_op(off, reg >= 0xe0 ? 0xe0 : (reg & 0xe0), val);
        break;
    }
    case 0xa0:
        if ((reg & 0x0f) <= 8) {
            Chan &c = ch_[reg & 0x0f];
            c.fnum = (uint16_t)((c.fnum & 0x300) | val);
        }
        break;
    case 0xb0:
        if (reg == 0xbd) {
            dam_ = (val & 0x80) != 0; dvb_ = (val & 0x40) != 0;
            rhythm_ = (val & 0x20) != 0;
            // rhythm keys (bit 1 of Op::key): BD = channel 6 both operators, SD 0x14, TOM 0x12, CY 0x15, HH 0x11
            bool r = rhythm_;
            set_key(0x10, 2, r && (val & 0x10)); set_key(0x13, 2, r && (val & 0x10));
            set_key(0x14, 2, r && (val & 0x08));
            set_key(0x12, 2, r && (val & 0x04));
            set_key(0x15, 2, r && (val & 0x02));
            set_key(0x11, 2, r && (val & 0x01));
        } else if ((reg & 0x0f) <= 8) {
            int ch = reg & 0x0f;
            Chan &c = ch_[ch];
            c.fnum = (uint16_t)((c.fnum & 0xff) | ((val & 3) << 8));
            c.block = (val >> 2) & 7;
            c.key = (val & 0x20) != 0;
            int m = (ch / 3) * 8 + ch % 3;
            set_key(m, 1, c.key);
            set_key(m + 3, 1, c.key);
        }
        break;
    case 0xc0:
        if ((reg & 0x0f) <= 8) {
            Chan &c = ch_[reg & 0x0f];
            c.fb = (val >> 1) & 7;
            c.cnt = (val & 1) != 0;
        }
        break;
    default: break;
    }
}

int OplChip::ksl_atten(const Op &o, const Chan &c) const {
    if (!o.ksl) return 0;
    int v = kKslRom[c.fnum >> 6] - 8 * (7 - c.block);
    if (v <= 0) return 0;
    static const uint8_t shift[4] = {0, 1, 0, 2};         // KSL 1 = 3 dB/oct, 2 = 1.5 dB/oct, 3 = 6 dB/oct
    return o.ksl == 2 ? v : v << shift[o.ksl];
}

int OplChip::op_level(const Op &o, const Chan &c) const {
    int a = o.env + (o.tl << 2) + ksl_atten(o, c);
    if (o.am) {
        uint32_t step = (lfo_counter_ >> 6) % 210;           // 210 steps of 64 samples = 3.70 Hz
        int tri = (int)(step < 105 ? step : 209 - step) >> 2; // 0..26 -> 4.875 dB
        a += dam_ ? tri : tri >> 2;                         // DAM 0: ~1.2 dB
    }
    return a > 511 ? 511 : a;
}

int OplChip::total_attenuation(int op) const {
    int ch = op_channel(op);
    if (ch < 0) return 511;
    return op_level(op_[op], ch_[ch]);
}

int32_t OplChip::op_wave(const Op &o, uint32_t phase10, int level) const {
    const Tables &t = tables();
    uint32_t p = phase10 & 0x3ff;
    int ws = wse_ ? o.ws : 0;
    bool neg = false;
    uint32_t idx = (p & 0x100) ? (~p & 0xff) : (p & 0xff);
    switch (ws) {
    case 0: neg = (p & 0x200) != 0; break;                       // sine
    case 1: if (p & 0x200) return 0; break;                      // half sine
    case 2: break;                                               // absolute sine
    default: if (p & 0x100) return 0; idx = p & 0xff; break;     // quarter sine pulses ("pseudo saw")
    }
    uint32_t L = t.logsin[idx] + ((uint32_t)level << 3);
    uint32_t sh = L >> 8;
    if (sh >= 13) return 0;
    int32_t v = t.exp[L & 0xff] >> sh;
    return neg ? -v : v;
}

int32_t OplChip::op_out(Op &o, const Chan &c, int32_t pm) {
    if (o.state == ENV_OFF) return 0;
    return op_wave(o, (o.phase >> 10) + (uint32_t)pm, op_level(o, c));
}

void OplChip::phase_step(Op &o, const Chan &c) {
    int f = c.fnum;
    if (o.vib) {
        int d = (c.fnum >> 7) * kVibTab[(lfo_counter_ >> 10) & 7];
        f += dvb_ ? d / 2 : d / 4;                                 // DVB 1: ~14 cents, 0: ~7 cents
    }
    if (f < 0) f = 0;
    uint32_t inc = (((uint32_t)f << c.block) * kMultX2[o.mult]) >> 1;
    o.phase = (o.phase + inc) & 0xfffff;
}

void OplChip::env_step(Op &o, const Chan &c) {
    int rate;
    switch (o.state) {
    case ENV_ATTACK:  rate = o.ar; break;
    case ENV_DECAY:   rate = o.dr; break;
    case ENV_RELEASE: rate = o.rr; break;
    default: return;                                            // OFF, SUSTAIN: hold
    }
    if (o.state == ENV_ATTACK && o.env == 0) { o.state = ENV_DECAY; rate = o.dr; }
    if (!rate) return;
    int ksn = (c.block << 1) | ((c.fnum >> (nts_ ? 8 : 9)) & 1);
    int R = rate * 4 + (o.ksr ? ksn : ksn >> 2);
    if (R > 63) R = 63;
    int hi = R >> 2, inc;
    if (hi < 12) {
        int sh = 12 - hi;
        if (env_counter_ & ((1u << sh) - 1)) return;
        inc = kEnvPattern[R & 3][(env_counter_ >> sh) & 7];
    } else {
        inc = kEnvPattern[R & 3][env_counter_ & 7] << (hi - 12);
    }
    if (o.state == ENV_ATTACK) {
        if (R >= 60) { o.env = 0; o.state = ENV_DECAY; return; }
        if (!inc) return;
        int e = o.env;
        e += (-(e + 1) * inc) >> 3;                              // exponential approach: ~env * inc / 8
        if (e <= 0) { e = 0; o.state = ENV_DECAY; }
        o.env = (uint16_t)e;
        return;
    }
    if (!inc) return;
    int e = o.env + inc;
    if (o.state == ENV_DECAY) {
        int sl = (o.sl == 15 ? 31 : o.sl) << 4;                  // 3 dB steps, SL 15 = 93 dB
        if (e >= sl) { e = sl; o.state = o.egt ? ENV_SUSTAIN : ENV_RELEASE; }   // EGT 0: percussive, decays on at RR
    } else if (e >= 511) {
        e = 511;
        if (!o.key) o.state = ENV_OFF;
    }
    o.env = (uint16_t)(e > 511 ? 511 : e);
}

void OplChip::timers_step() {
    timer_cycles_ += 72;
    while (timer_cycles_ >= 288) {                               // one 80 us timer clock (288 chip cycles)
        timer_cycles_ -= 288;
        if (t_running_ & 1) {
            if (++t1_count_ == 0) {
                t1_count_ = regs_[2];
                if (!(regs_[4] & 0x40)) status_ |= 0xc0;
                if (csm_) csm_pulse_ = true;
            }
        }
        if (++t2_div_ == 4) {                                    // 320 us
            t2_div_ = 0;
            if ((t_running_ & 2) && ++t2_count_ == 0) {
                t2_count_ = regs_[3];
                if (!(regs_[4] & 0x20)) status_ |= 0xa0;
            }
        }
    }
}

int32_t OplChip::sample() {
    // CSM: a timer 1 overflow keys every channel on for one sample
    if (csm_key_) {
        for (int ch = 0; ch < 9; ch++) { int m = (ch / 3) * 8 + ch % 3; set_key(m, 4, false); set_key(m + 3, 4, false); }
        csm_key_ = false;
    }
    if (csm_pulse_) {
        for (int ch = 0; ch < 9; ch++) { int m = (ch / 3) * 8 + ch % 3; set_key(m, 4, true); set_key(m + 3, 4, true); }
        csm_pulse_ = false;
        csm_key_ = true;
    }

    int32_t total = 0;
    int last = rhythm_ ? 6 : 9;
    for (int ch = 0; ch < last; ch++) {
        Chan &c = ch_[ch];
        int m = (ch / 3) * 8 + ch % 3;
        Op &mo = op_[m], &co = op_[m + 3];
        int32_t fbv = c.fb ? (mo.out[0] + mo.out[1]) >> (9 - c.fb) : 0;
        int32_t a = op_out(mo, c, fbv);
        mo.out[1] = mo.out[0]; mo.out[0] = a;
        // FM: the modulator's output (+-4095) is added to the carrier's 10-bit phase as it is (+-4 cycles at
        // TL 0); the feedback (sum of the last two outputs >> (9 - FB)) is +-2 cycles = 4 pi at FB 7, the
        // data sheet's value.
        int32_t b = op_out(co, c, c.cnt ? 0 : a);
        ch_out_[ch] = c.cnt ? a + b : b;
        total += ch_out_[ch];
    }
    if (rhythm_) {
        // BD: channel 6 (the modulator only modulates; with CNT 1 the carrier sounds alone)
        {
            Chan &c = ch_[6];
            Op &mo = op_[0x10], &co = op_[0x13];
            int32_t fbv = c.fb ? (mo.out[0] + mo.out[1]) >> (9 - c.fb) : 0;
            int32_t a = op_out(mo, c, fbv);
            mo.out[1] = mo.out[0]; mo.out[0] = a;
            rhy_out_[0] = op_out(co, c, c.cnt ? 0 : a) * 2;
        }
        // HH (0x11) and SD (0x14) on channel 7, TOM (0x12) and CY (0x15) on channel 8: the cymbal / hi-hat
        // phase is a combination of bits of the HH and CY phase counters and the noise generator.
        uint32_t ph = op_[0x11].phase >> 10, pc = op_[0x15].phase >> 10;
        uint32_t n = noise_ & 1;
        uint32_t res = (((ph >> 2) ^ (ph >> 7)) | (ph >> 3) | ((pc >> 3) ^ (pc >> 5))) & 1;
        uint32_t p_hh = (res << 9) | ((res ^ n) ? 0xd0u : 0x34u);
        uint32_t b8 = (ph >> 8) & 1;
        uint32_t p_sd = (b8 << 9) | ((b8 ^ n) << 8);
        uint32_t p_cy = (res << 9) | 0x100u;
        const Op &hh = op_[0x11], &sd = op_[0x14], &cy = op_[0x15];
        rhy_out_[4] = hh.state == ENV_OFF ? 0 : op_wave(hh, p_hh, op_level(hh, ch_[7])) * 2;
        rhy_out_[1] = sd.state == ENV_OFF ? 0 : op_wave(sd, p_sd, op_level(sd, ch_[7])) * 2;
        rhy_out_[2] = op_out(op_[0x12], ch_[8], 0) * 2;
        rhy_out_[3] = cy.state == ENV_OFF ? 0 : op_wave(cy, p_cy, op_level(cy, ch_[8])) * 2;
        ch_out_[6] = rhy_out_[0];
        ch_out_[7] = rhy_out_[4] + rhy_out_[1];
        ch_out_[8] = rhy_out_[2] + rhy_out_[3];
        total += ch_out_[6] + ch_out_[7] + ch_out_[8];
    } else {
        std::memset(rhy_out_, 0, sizeof rhy_out_);
    }

    // advance: envelopes, phases, LFOs, noise, timers
    for (int off = 0; off < 22; off++) {
        int ch = op_channel(off);
        if (ch < 0) continue;
        env_step(op_[off], ch_[ch]);
        phase_step(op_[off], ch_[ch]);
    }
    env_counter_++;
    lfo_counter_++;
    uint32_t bit = ((noise_ >> 22) ^ (noise_ >> 8)) & 1;          // 23-bit LFSR, taps 23 and 9
    noise_ = ((noise_ << 1) | bit) & 0x7fffff;
    timers_step();
    samples_++;
    return total;
}

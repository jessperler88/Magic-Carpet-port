// The internal OPL2 MIDI driver of carpet.exe (HMI device 0xa002) and OplMidiOut. See opl_driver.h.
// Each function below is a translation of the named original (all arguments on the stack, cdecl; the
// event-pointer functions take far pointers to 3-byte scratch buffers, here plain bytes).
#include "opl_driver.h"
#include "gen/opl_tables.h"
#include <algorithm>
#include <cstring>

// ======================================================================================================
// HmiOplDriver
// ======================================================================================================
HmiOplDriver::HmiOplDriver(OplWriter *w) : w_(w) {
    for (int i = 0; i < 16; i++) { a40e4_[i] = 0x40; a4164_[i] = 2; a41a8_[i] = 0x7f; }
    for (int i = 0; i < 9 + 16; i++) a41e8_[i] = i < 9 ? 0x7f : 0;
    // DAT_000a42ac + 16 * 3 is the start of the velocity table DAT_000a42dc (read by the sustain release)
    std::memcpy(a42ac_ + 16 * 3, g_opl_vel_level, 3);
}

uint8_t HmiOplDriver::mod_op(uint8_t v) { return g_opl_slot_ops[(v * 2) % 18]; }
uint8_t HmiOplDriver::car_op(uint8_t v) { return g_opl_slot_ops[(v * 2 + 1) % 18]; }

uint32_t HmiOplDriver::freq(uint32_t idx) {
    return idx < sizeof g_opl_note_freq / sizeof g_opl_note_freq[0] ? g_opl_note_freq[idx] : 0;
}

// snd_opl_calc_bent_freq_686db(bend, note, voice): the note's F-number moved towards the note +- the
// channel's bend range by (bend - 0x40) / 64 (in 1/1000 steps), with the octave wrap through the block
// bits. All arithmetic unsigned 32-bit as in the original.
uint32_t HmiOplDriver::calc_bent_freq(uint32_t bend, uint32_t note, uint32_t voice) const {
    uint32_t n = note - 12;                              // [ebp+0x18] -= 12
    uint32_t m = n % 12;                                 // the original subtracts 12 in a loop (same result)
    uint32_t f = freq(note);                             // DAT_000a4320[note - 12]
    uint32_t blk = f & 0x1c00, fn = f & 0x3ff;
    uint32_t range = a4164_[a40c0_[voice % 9] & 0x0f];
    uint32_t d, k;
    if (bend < 0x40) {
        k = ((0x3f - bend) * 1000u) >> 6;
        d = f - freq(note - range);
        if (d > 0x2cf) d = (fn - freq(range + 114)) & 0x3ff;   // DAT_000a44b8[range]
        d = d * k / 1000u;
        f -= d;
    } else {
        k = ((bend - 0x40) * 1000u) >> 6;
        d = freq(note + range) - f;
        if (d > 0x2cf) {                                 // crosses the octave: next block, F-number halved
            blk += 0x400;
            f = blk | freq(115 + 11 - m);                // DAT_000a44bc[11 - note % 12]
            d = freq(note + range) - f;
        }
        d = d * k / 1000u;
        f += d;
    }
    return f;
}

// snd_opl_pitch_bend_68619(ev = {channel, msb})
void HmiOplDriver::pitch_bend(uint8_t ch, uint8_t msb) {
    if (ch >= 0x10 || ch == 9) return;
    a40e4_[ch] = msb;
    a4124_[ch] = 1;
    for (uint8_t v = 0; v < 9; v++) {
        if (a401c_[v] == 0 || a40c0_[v] != ch) continue;
        voice_set_freq(v, calc_bent_freq(msb, a401c_[v], v));
    }
}

// snd_opl_controller_68893(ev = {channel, controller, value})
void HmiOplDriver::controller(uint8_t ch, uint8_t cc, uint8_t val) {
    switch (cc) {
    case 0x07: set_channel_volume(ch, val); break;
    case 0x40:
        a424c_[ch] = val;
        if (val == 0) {
            // replays the queue from entry [count] down to [1] (sic: one past the last entry each time)
            while (a42a8_ != 0) {
                uint32_t n = a42a8_;
                note_off(a42ac_ + n * 3);
                if (a42a8_ != n) {
                    // the entry's channel is still sustained: note_off queued it again at [n] and the
                    // original loops here forever (inside the timer interrupt). Port: stop instead.
                    a42a8_ = n;
                    break;
                }
                a42a8_--;
            }
        }
        break;
    case 0x66: a4164_[ch] = val; break;                  // bend range (RPN-less)
    case 0x79: reset_controllers(ch); break;
    case 0x7b: all_notes_off(ch); break;
    default: break;
    }
}

// The carrier level of snd_opl_note_on_689da / snd_opl_set_channel_volume_704cd:
// level = (0x2000 - (0x40 - vel_tab[(vel * ((chvol << 7) / 0x7f) >> 7) >> 1]) * 2 * (0x40 - TL)) >> 7.
uint8_t HmiOplDriver::carrier_level(uint8_t v, uint32_t chvol) const {
    uint32_t x = (chvol << 7) / 0x7f;
    uint8_t lv = (uint8_t)((a41e8_[v] * x) >> 7);
    uint32_t tl = a3fbc_[car_op(v)] & 0x3f;
    uint32_t t = g_opl_vel_level[(lv >> 1) & 0x3f];
    t = ((0x40 - t) << 7) >> 6;
    t = t * (0x40 - tl);
    t = (0x2000 - t) >> 7;
    return (uint8_t)t;
}

// snd_opl_note_on_689da(note, velocity, channel)
void HmiOplDriver::note_on(uint8_t note, uint8_t vel, uint8_t ch) {
    if (ch >= 0x10) return;
    const bool drum = ch == 9;                           // percussion bank (no check that it is loaded)
    uint8_t c = ch;
    uint8_t v = alloc_voice(ch, note);
    voice_key_off(v);
    a40c0_[v] = ch;
    for (int i = 0; i < 5; i++) {                        // fast release on the old note (RR = 15), 5 times
        write((uint8_t)(0x80 + car_op(v)), (uint8_t)(a3fdc_[car_op(v)] | 0x0f));
        write((uint8_t)(0x80 + mod_op(v)), (uint8_t)(a3fdc_[mod_op(v)] | 0x0f));
    }
    const uint8_t *t = drum ? bank_ptr(drum_, drum_data_ + (size_t)note * 0x1e, 0x1e)
                            : bank_ptr(mel_, mel_data_ + (size_t)a4080_[c] * 0x1e, 0x1e);
    load_timbre(t, v);
    a41e8_[v] = vel;
    uint8_t level = carrier_level(v, a41a8_[c]);
    if (!drum) {
        // the modulator gets the same level when the timbre's FB / CON byte is 0 (FM, no feedback)
        if (t && t[0xe] == 0)
            write((uint8_t)(0x40 + mod_op(v)), (uint8_t)((a3fbc_[mod_op(v)] & 0xc0) | level));
    }
    write((uint8_t)(0x40 + car_op(v)), (uint8_t)((a3fbc_[car_op(v)] & 0xc0) | level));
    if (drum) {
        const uint8_t *p = bank_ptr(drum_, drum_names_ + (size_t)note * 0xc + 2, 1);   // name record byte +2
        voice_key_on(v, freq(p ? *p : 0));
        a401c_[v] = note;
        return;
    }
    voice_key_on(v, freq(note));
    a401c_[v] = note;
    if (a407c_ != 0 && a4124_[c] != 0) voice_set_freq(v, calc_bent_freq(a40e4_[c], note, v));
}

// snd_opl_note_off_68eca(ev = {note, velocity, channel})
void HmiOplDriver::note_off(const uint8_t ev[3]) {
    uint8_t ch = ev[2];
    if (ch >= 0x10) return;
    if (a424c_[ch] != 0 && a42a8_ < 0x10) {              // sustained: queue it
        std::memcpy(a42ac_ + a42a8_ * 3, ev, 3);
        a42a8_++;
        return;
    }
    for (uint8_t v = 0; v < 9; v++) {
        if (a401c_[v] != ev[0] || a40c0_[v] != ch) continue;
        voice_key_off(v);
        a401c_[v] = 0;
    }
}

// snd_opl_all_notes_off_68fc9(channel): controller 0x7b
void HmiOplDriver::all_notes_off(uint32_t ch) {
    if (ch >= 0x10 && !(ch == 9 && a4074_)) return;
    a41a8_[ch] = 0x7f;
    a41e8_[ch] = 0x7f;                                   // (sic) indexed by channel
    a41e8_[9 + ch] = 0;                                  // DAT_000a420c[ch]
    a424c_[ch] = 0;
    for (uint8_t v = 0; v < 9; v++) {
        if (a40c0_[v] != ch) continue;
        voice_key_off(v);
        a401c_[v] = 0;
    }
}

// snd_opl_reset_controllers_69072(channel): controller 0x79
void HmiOplDriver::reset_controllers(uint32_t ch) {
    if (ch >= 0x10 && !(ch == 9 && a4074_)) return;
    a41a8_[ch] = 0x7f;
    a41e8_[ch] = 0x7f;                                   // (sic) indexed by channel
    a41e8_[9 + ch] = 0;
    a424c_[ch] = 0;
    a40e4_[ch] = 0x40;
    a4164_[ch] = 2;
}

// snd_opl_midi_event_6911c (fn0)
void HmiOplDriver::midi_event(const uint8_t ev[3]) {
    uint8_t ch = ev[0] & 0x0f;
    switch (ev[0] & 0xf0) {
    case 0x90:
        if (ev[2] != 0) { note_on(ev[1], ev[2], ch); break; }
        [[fallthrough]];
    case 0x80: {
        uint8_t off[3] = {ev[1], ev[2], ch};             // DAT_000a4296..98
        note_off(off);
        break;
    }
    case 0xb0: controller(ch, ev[1], ev[2]); break;      // DAT_000a429b..9d
    case 0xc0: a4080_[ch] = ev[1]; break;                // snd_opl_program_change_7075e({program, channel})
    case 0xe0: pitch_bend(ch, ev[2]); break;             // DAT_000a429e..9f: the MSB only
    default: break;
    }
}

// snd_opl_reset_state_69319 (fn3)
void HmiOplDriver::reset() {
    silence_all();
    a41a4_ = 0;
    for (int v = 0; v < 9; v++) { a401c_[v] = 0; a40c0_[v] = 0; a41e8_[v] = 0x7f; }
    for (int ch = 0; ch < 16; ch++) {
        a40e4_[ch] = 0x40; a4080_[ch] = 0; a4124_[ch] = 0; a41a8_[ch] = 0x7f; a41e8_[9 + ch] = 0;
    }
}

// snd_opl_set_timbre_bank_69401 (fn4)
void HmiOplDriver::set_timbre_bank(const uint8_t *bnk, size_t len) {
    std::vector<uint8_t> b(bnk, bnk + len);
    pack_timbre_bank(b);
    auto u32 = [](const std::vector<uint8_t> &x, size_t o) -> uint32_t {
        return o + 4 <= x.size() ? (uint32_t)x[o] | ((uint32_t)x[o + 1] << 8) | ((uint32_t)x[o + 2] << 16) | ((uint32_t)x[o + 3] << 24) : 0;
    };
    if (a41a4_ == 0) {
        a41a4_ = 1;
        mel_ = std::move(b);
        a4044_ = mel_.size() >= 10 ? (uint32_t)(int32_t)(int16_t)(mel_[8] | (mel_[9] << 8)) : 0;
        mel_names_ = u32(mel_, 0xc);
        mel_data_ = u32(mel_, 0x10);
        a405e_ = 1;
        for (uint8_t v = 0; v < 9; v++) a4080_[v] = 0;   // snd_opl_program_change_7075e({0, v})
    } else {
        a41a4_ = 0;
        drum_ = std::move(b);
        // (sic) the offsets come from the melodic bank's header (the same in the shipped banks)
        drum_names_ = u32(mel_, 0xc);
        drum_data_ = u32(mel_, 0x10);
        a4074_ = 1;
    }
}

// snd_opl_pack_timbre_bank_6955a: the AdLib .BNK instrument record (2 bytes, 13 modulator parameters,
// 13 carrier parameters, 2 wave selects) packed in place into register bytes. Records 0 .. count - 3
// (sic: the last two records stay unpacked).
void HmiOplDriver::pack_timbre_bank(std::vector<uint8_t> &b) {
    if (b.size() < 0x14) return;
    int32_t count = (int16_t)(b[8] | (b[9] << 8));
    size_t off = (size_t)b[0x10] | ((size_t)b[0x11] << 8) | ((size_t)b[0x12] << 16) | ((size_t)b[0x13] << 24);
    for (int32_t i = 0; i < count - 2; i++) {
        if (off + 0x1e > b.size()) break;
        uint8_t *r = b.data() + off;
        r[0x0b] = (uint8_t)((r[0x0b] << 7) | (r[0x0c] << 6) | (r[0x07] << 5) | (r[0x0d] << 4) | r[0x03]);
        r[0x02] = (uint8_t)((r[0x02] << 6) | r[0x0a]);
        r[0x05] = (uint8_t)((r[0x05] << 4) | r[0x08]);
        r[0x06] = (uint8_t)((r[0x06] << 4) | r[0x09]);
        r[0x0e] = (uint8_t)((r[0x04] << 1) | r[0x0e]);
        r[0x18] = (uint8_t)((r[0x18] << 7) | (r[0x19] << 6) | (r[0x14] << 5) | (r[0x1a] << 4) | r[0x10]);
        r[0x0f] = (uint8_t)((r[0x0f] << 6) | r[0x17]);
        r[0x12] = (uint8_t)((r[0x12] << 4) | r[0x15]);
        r[0x13] = (uint8_t)((r[0x13] << 4) | r[0x16]);
        off += 0x1e;
    }
}

// snd_opl_driver_uninit_6977c (fn2)
void HmiOplDriver::uninit() {
    init_chip(a3fa8_);
    silence_all();
    a402b_ = 0;                                          // snd_opl_clear_chip_ready_69f6a
    a4040_ = 0;
}

// snd_opl_driver_init_697e1 (fn1)
int HmiOplDriver::init(uint32_t port) {
    int r = init_chip(port);
    clear_voices();
    a4040_ = 1;
    return r;
}

// snd_opl_init_chip_69940
int HmiOplDriver::init_chip(uint32_t port) {
    if (port != 0x388 && port != 0x380) return 1;
    a3fa8_ = port;
    write(0x01, 0x20);                                   // waveform select enable
    a4010_ = 0;
    clear_voices();
    a402b_ = 1;
    return 0;
}

// snd_opl_voice_set_freq_699e1
void HmiOplDriver::voice_set_freq(uint8_t v, uint32_t f) {
    a3fec_[v] = (uint8_t)f;
    a3ff5_[v] = (uint8_t)((f >> 8) | 0x20);
    write((uint8_t)(0xa0 + v), a3fec_[v]);
    write((uint8_t)(0xb0 + v), a3ff5_[v]);
}

// snd_opl_clear_voices_69a77
void HmiOplDriver::clear_voices() {
    for (uint8_t v = 0; v < 9; v++) { a3ff5_[v] = 0; write((uint8_t)(0xb0 + v), a3ff5_[v]); }
    std::memset(a4011_, 0, sizeof a4011_);
    a402c_ = 0;
    a4010_ &= 0xc0;
    write(0xbd, a4010_);
}

// snd_opl_voice_key_on_69b31: frequency, key off, key on
void HmiOplDriver::voice_key_on(uint8_t v, uint32_t f) {
    a3fec_[v] = (uint8_t)f;
    a3ff5_[v] = (uint8_t)((f >> 8) | 0x20);
    write((uint8_t)(0xa0 + v), a3fec_[v]);
    write((uint8_t)(0xb0 + v), (uint8_t)(a3ff5_[v] & 0xdf));
    write((uint8_t)(0xb0 + v), a3ff5_[v]);
    a401c_[v] = 1;
}

// snd_opl_voice_key_off_69bf3
int HmiOplDriver::voice_key_off(uint8_t v) {
    if (a401c_[v] == 0) return 6;
    a3ff5_[v] &= 0xdf;
    write((uint8_t)(0xb0 + v), a3ff5_[v]);
    a401c_[v] = 0;
    return 0;
}

// snd_opl_silence_all_69c73
int HmiOplDriver::silence_all() {
    if (a402b_ == 0) return 2;
    a4010_ = 0;
    write(0xbd, a4010_);
    for (uint8_t v = 0; v < 9; v++) write((uint8_t)(0xb0 + v), (uint8_t)(a3ff5_[v] & 0xdf));   // shadow unchanged
    for (uint8_t v = 0; v < 9; v++) write((uint8_t)(0x40 + car_op(v)), 0xff);
    std::memset(a4011_, 0, sizeof a4011_);
    return 0;
}

// snd_opl_load_timbre_69d61(timbre, voice): modulator 0x20 / 0x40 / 0x60 / 0x80, 0xC0, 0xE0, then the
// carrier 0x20 / 0x60 / 0x80 / 0xE0 (its 0x40 is written by the caller); KSL/TL and SL/RR shadows.
void HmiOplDriver::load_timbre(const uint8_t *t, uint8_t v) {
    static const uint8_t zero[0x1e] = {};
    if (!t) t = zero;                                    // (outside the bank: not reachable with the shipped data)
    uint8_t mo = mod_op(v), co = car_op(v);
    a3fdc_[co] = t[0x13];
    a3fdc_[mo] = t[0x06];
    write((uint8_t)(0x20 + mo), t[0x0b]);
    write((uint8_t)(0x40 + mo), t[0x02]);
    a3fbc_[mo] = t[0x02];
    write((uint8_t)(0x60 + mo), t[0x05]);
    write((uint8_t)(0x80 + mo), t[0x06]);
    write((uint8_t)(0xc0 + v), t[0x0e]);
    write((uint8_t)(0xe0 + mo), t[0x1c]);
    write((uint8_t)(0x20 + co), t[0x18]);
    a3fbc_[co] = t[0x0f];
    write((uint8_t)(0x60 + co), t[0x12]);
    write((uint8_t)(0x80 + co), t[0x13]);
    write((uint8_t)(0xe0 + co), t[0x1d]);
    a4011_[v] = 1;
}

// snd_opl_set_channel_volume_704cd(channel, volume): controller 7
void HmiOplDriver::set_channel_volume(uint32_t ch, uint8_t vol) {
    if (ch >= 0x10) return;
    a41a8_[ch] = vol;
    a41e8_[9 + ch] = 1;                                  // DAT_000a420c[ch]
    for (uint8_t v = 0; v < 9; v++) {
        if (a401c_[v] == 0 || a40c0_[v] != ch) continue;
        uint8_t level = carrier_level(v, vol);
        // (sic) the melodic timbre of the channel's program decides, also for channel 9's drum voices
        const uint8_t *t = bank_ptr(mel_, mel_data_ + (size_t)a4080_[a40c0_[v]] * 0x1e, 0x1e);
        if (t && t[0xe] == 0)
            write((uint8_t)(0x40 + mod_op(v)), (uint8_t)((a3fbc_[mod_op(v)] & 0xc0) | level));
        write((uint8_t)(0x40 + car_op(v)), (uint8_t)((a3fbc_[car_op(v)] & 0xc0) | level));
    }
}

// snd_opl_alloc_voice_70697(channel, note): a free voice; else the first voice of the lowest channel
// that never received a pitch bend; else voice = channel (channel - 9 for 9..15).
uint8_t HmiOplDriver::alloc_voice(uint8_t ch, uint8_t) const {
    for (uint8_t v = 0; v < 9; v++)
        if (a401c_[v] == 0) return v;
    for (uint32_t c = 0; c < 16; c++) {
        if (a4124_[c] != 0) continue;
        for (uint8_t v = 0; v < 9; v++)
            if (a40c0_[v] == c) return v;
    }
    return ch >= 9 ? (uint8_t)(ch - 9) : ch;
}

// ======================================================================================================
// OplMidiOut
// ======================================================================================================
OplMidiOut::OplMidiOut() { drv_.set_writer(this); }

bool OplMidiOut::load_banks(const uint8_t *inst, size_t inst_len, const uint8_t *drum, size_t drum_len) {
    if (!inst || !drum || inst_len < 0x14 || drum_len < 0x14) return false;
    drv_.init(0x388);
    drv_.set_timbre_bank(inst, inst_len);
    drv_.set_timbre_bank(drum, drum_len);
    flush_writes();
    return true;
}

void OplMidiOut::send(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t ev[3] = {status, d1, d2};
    drv_.midi_event(ev);
}

void OplMidiOut::reset() { drv_.reset(); }

void OplMidiOut::panic() {
    drv_.reset();
    flush_writes();
}

void OplMidiOut::opl_write(uint8_t reg, uint8_t val) {
    uint64_t at = std::max(cursor_q8_, chip_time_q8_);
    q_.push_back({at, reg, val});
    cursor_q8_ = at + write_cost_q8_;
    writes_++;
}

void OplMidiOut::flush_writes() {
    for (const W &w : q_) chip_.write(w.reg, w.val);
    q_.clear();
    cursor_q8_ = chip_time_q8_;
}

void OplMidiOut::chip_sample() {
    while (!q_.empty() && q_.front().at_q8 <= chip_time_q8_) {
        chip_.write(q_.front().reg, q_.front().val);
        q_.pop_front();
    }
    prev_ = cur_;
    cur_ = chip_.sample();
    chip_time_q8_ += 256;
}

void OplMidiOut::render(int32_t *lr, int frames, int rate) {
    if (rate <= 0) return;
    if (rate != step_rate_) {
        step_rate_ = rate;
        step_ = (uint32_t)(OplChip::RATE * 65536.0 / rate + 0.5);
        frac_ = 0;
    }
    for (int f = 0; f < frames; f++) {
        frac_ += step_;
        while (frac_ >= 0x10000) { frac_ -= 0x10000; chip_sample(); }
        int64_t s = (int64_t)prev_ + (((int64_t)(cur_ - prev_) * frac_) >> 16);
        int32_t o = (int32_t)((s * gain_q8_) >> 8);
        lr[2 * f] += o;
        lr[2 * f + 1] += o;
    }
}

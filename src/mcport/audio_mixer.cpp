// Platform audio without SDL (round 5, task A). See audio_mixer.h; report docs/analysis/port_audio.md.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "audio_mixer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// =====================================================================================================
// AudioMixer
// =====================================================================================================
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void AudioMixer::pan_gains(int volume, int pan, int *gl, int *gr) {
    int vol = clampi(volume, 0, 0x7fff);
    int p = clampi(pan, 0, 0xffff);
    // balance law: centre (0x7fff / 0x8000) = unity on both sides, the far side falls linearly to 0
    int64_t lq = std::min<int64_t>(65536, 2 * (int64_t)(65535 - p));
    int64_t rq = std::min<int64_t>(65536, 2 * (int64_t)p + 2);
    *gl = (int)((vol * lq) >> 16);
    *gr = (int)((vol * rq) >> 16);
}

AudioMixer::Voice *AudioMixer::lookup(int h) {
    if (h < 0) return nullptr;
    int slot = h & 63;
    if (slot >= SLOTS) return nullptr;
    Voice &v = v_[slot];
    if (v.state != Voice::PLAYING || v.gen != (uint16_t)((unsigned)h >> 6)) return nullptr;
    return &v;
}
const AudioMixer::Voice *AudioMixer::lookup(int h) const { return const_cast<AudioMixer *>(this)->lookup(h); }

int AudioMixer::start(const uint8_t *data, uint32_t len, int rate, int volume, int pan, int loop_count, int flags) {
    if (!data || len == 0 || rate <= 0) return -1;
    if (playing() >= VOICES) return -1;                       // the HMI driver had 32 channel records
    int slot = -1;
    for (int i = 0; i < SLOTS; i++) if (v_[i].state == Voice::FREE) { slot = i; break; }
    if (slot < 0) {                                           // every slot busy: steal the quietest stop ramp
        int best = 0x10000;
        for (int i = 0; i < SLOTS; i++)
            if (v_[i].state == Voice::RAMPING && v_[i].gl + v_[i].gr < best) { best = v_[i].gl + v_[i].gr; slot = i; }
        if (slot < 0) return -1;
    }
    Voice &v = v_[slot];
    v = Voice{};
    v.state = Voice::PLAYING;
    v.data = data;
    v.len = len;
    v.pos = 0;
    v.step = ((uint64_t)(unsigned)rate << 16) / (uint64_t)(unsigned)out_rate_;
    v.loops = (flags & FLAG_LOOPING) ? loop_count : 0;
    v.volume = (flags & FLAG_VOLUME) ? clampi(volume, 0, 0x7fff) : 0x7fff;
    v.pan = (flags & FLAG_PANNING) ? clampi(pan, 0, 0xffff) : 0x7fff;
    pan_gains(v.volume, v.pan, &v.tl, &v.tr);
    v.gl = v.tl; v.gr = v.tr;
    v.gen = next_gen_;
    next_gen_ = (uint16_t)((next_gen_ + 1) & 0x7fff);
    if (next_gen_ == 0) next_gen_ = 1;
    return (int)(((unsigned)v.gen << 6) | (unsigned)slot);
}

void AudioMixer::stop(int h) {
    Voice *v = lookup(h);
    if (!v) return;
    v->state = Voice::RAMPING;
    v->tl = v->tr = 0;
}

void AudioMixer::set_volume(int h, int volume) {
    Voice *v = lookup(h);
    if (!v) return;
    v->volume = clampi(volume, 0, 0x7fff);
    pan_gains(v->volume, v->pan, &v->tl, &v->tr);
}

bool AudioMixer::is_playing(int h) const { return lookup(h) != nullptr; }

void AudioMixer::stop_all_now() {
    for (auto &v : v_) v.state = Voice::FREE;
}

int AudioMixer::playing() const {
    int n = 0;
    for (auto &v : v_) n += v.state == Voice::PLAYING;
    return n;
}
int AudioMixer::sounding() const {
    int n = 0;
    for (auto &v : v_) n += v.state != Voice::FREE;
    return n;
}

void AudioMixer::mix(int32_t *lr, int frames) {
    const int step_g = 0x8000 / RAMP_SAMPLES;
    for (auto &v : v_) {
        if (v.state == Voice::FREE) continue;
        const uint64_t end = (uint64_t)v.len << 16;
        for (int f = 0; f < frames; f++) {
            uint32_t idx = (uint32_t)(v.pos >> 16);
            int frac = (int)(v.pos & 0xffff);
            int s0 = ((int)v.data[idx] - 128) << 8;
            int s1;
            if (idx + 1 < v.len) s1 = ((int)v.data[idx + 1] - 128) << 8;
            else if (v.loops != 0) s1 = ((int)v.data[0] - 128) << 8;
            else s1 = s0;
            int s = s0 + (int)(((int64_t)(s1 - s0) * frac) >> 16);
            // gain ramps (stop, volume changes)
            if (v.gl != v.tl) v.gl = v.gl < v.tl ? std::min(v.tl, v.gl + step_g) : std::max(v.tl, v.gl - step_g);
            if (v.gr != v.tr) v.gr = v.gr < v.tr ? std::min(v.tr, v.gr + step_g) : std::max(v.tr, v.gr - step_g);
            lr[2 * f]     += (s * v.gl) >> 15;
            lr[2 * f + 1] += (s * v.gr) >> 15;
            if (v.state == Voice::RAMPING && v.gl == 0 && v.gr == 0) { v.state = Voice::FREE; break; }
            v.pos += v.step;
            if (v.pos >= end) {
                if (v.loops != 0) {
                    v.pos -= end;
                    if (v.pos >= end) v.pos %= end;
                    if (v.loops > 0) v.loops--;
                } else {
                    v.state = Voice::FREE;                    // hmi_sample_done from now on
                    break;
                }
            }
        }
    }
}

// =====================================================================================================
// MusicSequencer
// =====================================================================================================
MusicSequencer::~MusicSequencer() { unload(); }

void MusicSequencer::unload() {
    if (song_ok_) mc_hmp_free(&song_);
    song_ok_ = false;
    active_ = false;
    bytes_.clear();
}

// snd_midi_song_setup_5ec41: every track back to its first event with the counter cleared; tracks the
// device map rejected stay off and are not counted.
void MusicSequencer::rewind() {
    tracks_left_ = 0;
    for (uint32_t t = 0; t < MC_HMP_MAX_TRACKS; t++) {
        tr_[t] = Track{};
        if (t < song_.track_count && mc_hmp_track_plays(&song_, (int)t, device_)) {
            tr_[t].on = true;
            tracks_left_++;
        }
    }
}

// snd_midi_init_song_5e53a + snd_midi_start_song_5eab0
bool MusicSequencer::load_and_start(const uint8_t *song, uint32_t len) {
    unload();
    if (!song || len == 0) return false;
    bytes_.assign(song, song + len);
    if (!mc_hmp_parse(bytes_.data(), bytes_.size(), &song_)) { bytes_.clear(); return false; }
    song_ok_ = true;
    rewind();
    ticks_ = 0;
    active_ = true;                                   // DAT_0009f9ee = 1 once the timer event is installed
    return true;
}

// snd_midi_reset_channels_5e0d9 (pass-through branch): per playing track, on the track's own channel:
// all notes off, reset controllers, pitch bend 0x40 / 0x40, volume 0.
void MusicSequencer::reset_channels() {
    for (uint32_t t = 0; t < song_.track_count; t++) {
        if (!mc_hmp_track_plays(&song_, (int)t, device_)) continue;
        uint8_t ch = (uint8_t)(song_.tracks[t].channel & 0x0f);
        send((uint8_t)(0xb0 | ch), 0x7b, 0);
        send((uint8_t)(0xb0 | ch), 0x79, 0);
        send((uint8_t)(0xe0 | ch), 0x40, 0x40);
        send((uint8_t)(0xb0 | ch), 0x07, 0);
    }
}

// music_stop: snd_midi_stop_song_5eb38 (when not done) + snd_midi_clear_song_slot_5ea6d +
// snd_midi_all_notes_off_60035.
void MusicSequencer::stop() {
    if (song_ok_ && active_) {
        reset_channels();
        active_ = false;
        rewind();
    }
    unload();
    for (int ch = 0; ch < 16; ch++) {
        send((uint8_t)(0xb0 | ch), 0x79, 0);
        send((uint8_t)(0xb0 | ch), 0x7b, 0);
    }
    if (out_) out_->reset();                          // driver fn3 (the OPL2 driver: snd_opl_reset_state_69319)
}

// snd_midi_set_volume_5ef56
void MusicSequencer::set_master_volume(int v) {
    master_ = v & 0xff;
    for (int ch = 0; ch < 16; ch++)
        send((uint8_t)(0xb0 | ch), 7, (uint8_t)((master_ * 0x7f) >> 7));   // 0xa2566[ch] stays 0x7f in pass-through mode
}

// music_mood_fade_cb_1f6d0: B3 / B4 / B5 07 level through hmi_midi_send_event_5e4b8 (no master scaling)
void MusicSequencer::set_layer_volume(int v) {
    for (int ch = 3; ch <= 5; ch++) send((uint8_t)(0xb0 | ch), 7, (uint8_t)(v & 0x7f));
}

// The timer callback at 0x66898 for one song.
void MusicSequencer::tick() {
    if (!song_ok_ || !active_) return;
    ticks_++;
    for (uint32_t t = 0; t < song_.track_count; t++) {
        Track &k = tr_[t];
        if (!k.on) continue;
        const mc_hmp_track &ht = song_.tracks[t];
        k.counter++;
        while (k.idx < ht.event_count && ht.events[k.idx].delta <= k.counter) {
            const mc_hmp_event &e = ht.events[k.idx++];
            k.counter = 0;
            if (e.status == 0xff) {
                if (e.d1 != 0x2f) continue;               // FF 51 (tempo) and the rest: stepped over
                k.idx = ht.event_count;                   // track pointer = 0
                break;
            }
            if (e.status >= 0xf0) continue;               // not in the shipped songs
            // hmi_midi_send_event_5d69b with channel mapping off: controller 7 scaled by the master volume
            if ((e.status & 0xf0) == 0xb0 && e.d1 == 7) send(e.status, 7, (uint8_t)((master_ * e.d2) >> 7));
            else send(e.status, e.d1, e.d2);
        }
        if (k.idx >= ht.event_count) {                    // end of track (FF 2F, or the data ran out)
            k.on = false;
            if (--tracks_left_ <= 0) {
                // song end: DAT_0009f9ee = 0, reset the channels, remove the timer event, rewind
                active_ = false;
                reset_channels();
                rewind();
                return;
            }
        }
    }
}

// =====================================================================================================
// SquareSynth
// =====================================================================================================
void SquareSynth::send(uint8_t st, uint8_t d1, uint8_t d2) {
    int ch = st & 0x0f;
    Chan &c = c_[ch];
    switch (st & 0xf0) {
    case 0x90:
        if (d2) {
            int slot = -1;
            for (int i = 0; i < 32; i++) if (!n_[i].on) { slot = i; break; }
            if (slot < 0) {                               // steal the oldest
                uint32_t oldest = 0xffffffffu;
                for (int i = 0; i < 32; i++) if (n_[i].age < oldest) { oldest = n_[i].age; slot = i; }
            }
            Note &n = n_[slot];
            n = Note{};
            n.on = n.held = true; n.ch = (uint8_t)ch; n.key = d1; n.vel = d2; n.env = 1.0; n.age = ++age_;
            n.noise = 0x1234u + d1;
            break;
        }
        [[fallthrough]];
    case 0x80:
        for (auto &n : n_) if (n.on && n.held && n.ch == ch && n.key == d1) n.held = false;
        break;
    case 0xb0:
        if (d1 == 7) c.volume = d2;
        else if (d1 == 10) c.pan = d2;
        else if (d1 == 121) c.bend = 0x2000;
        else if (d1 == 123 || d1 == 120) for (auto &n : n_) if (n.on && n.ch == ch) n.held = false;
        break;
    case 0xc0: c.program = d1; break;
    case 0xe0: c.bend = d1 | (d2 << 7); break;
    default: break;
    }
}

void SquareSynth::panic() { for (auto &n : n_) n.on = false; }

int SquareSynth::notes_on() const { int k = 0; for (auto &n : n_) k += n.on; return k; }

void SquareSynth::render(int32_t *lr, int frames, int rate) {
    if (rate <= 0) return;
    const double rel = std::exp(-1.0 / (0.06 * rate));          // release
    const double dec = std::exp(-1.0 / (0.6 * rate));           // held decay towards 0.35
    const double drum = std::exp(-1.0 / (0.05 * rate));
    for (auto &n : n_) {
        if (!n.on) continue;
        const Chan &c = c_[n.ch];
        double amp = 2600.0 * (n.vel / 127.0) * (c.volume / 127.0);
        double pl = c.pan <= 64 ? 1.0 : (127 - c.pan) / 63.0;
        double pr = c.pan >= 64 ? 1.0 : c.pan / 64.0;
        bool is_drum = n.ch == 9;
        double semis = n.key - 69 + (c.bend - 0x2000) / 4096.0;    // +-2 semitones
        double inc = 440.0 * std::pow(2.0, semis / 12.0) / rate;
        double duty = 0.5 - 0.125 * (c.program % 3);
        for (int f = 0; f < frames; f++) {
            double w;
            if (is_drum) {
                n.noise = n.noise * 1664525u + 1013904223u;
                w = ((n.noise >> 16) & 0x7fff) / 16384.0 - 1.0;
                n.env *= drum;
            } else {
                n.phase += inc;
                n.phase -= std::floor(n.phase);
                w = n.phase < duty ? 1.0 : -1.0;
                n.env = n.held ? 0.35 + (n.env - 0.35) * dec : n.env * rel;
            }
            double s = w * amp * n.env;
            lr[2 * f]     += (int32_t)(s * pl);
            lr[2 * f + 1] += (int32_t)(s * pr);
        }
        if (n.env < 0.001) n.on = false;
    }
}

// =====================================================================================================
// AudioEngine
// =====================================================================================================
AudioEngine::AudioEngine() { seq_.set_device(MC_HMP_DEVICE_MPU401); }

void AudioEngine::set_output_rate(int hz) {
    std::lock_guard<std::mutex> g(mu_);
    mixer_.set_output_rate(hz);
    music_acc_ = 0;
}

void AudioEngine::set_midi(MidiOut *out, Clock clock) {
    std::lock_guard<std::mutex> g(mu_);
    seq_.set_output(out);
    clock_ = clock;
    music_acc_ = music_us_acc_ = 0;
}

void AudioEngine::set_music_device(uint32_t hmi_id) {
    std::lock_guard<std::mutex> g(mu_);
    seq_.set_device(hmi_id);
}

int AudioEngine::start(int, int, const uint8_t *data, uint32_t len, int rate, int volume, int pan, int loop_count, int hmi_flags) {
    std::lock_guard<std::mutex> g(mu_);
    int h = mixer_.start(data, len, rate, volume, pan, loop_count, hmi_flags);
    if (h >= 0) starts_++;
    return h;
}
void AudioEngine::stop(int handle)                 { std::lock_guard<std::mutex> g(mu_); mixer_.stop(handle); }
void AudioEngine::set_volume(int handle, int vol)  { std::lock_guard<std::mutex> g(mu_); mixer_.set_volume(handle, vol); }
bool AudioEngine::is_playing(int handle)           { std::lock_guard<std::mutex> g(mu_); return mixer_.is_playing(handle); }
void AudioEngine::flush()                          { std::lock_guard<std::mutex> g(mu_); mixer_.stop_all_now(); }

bool AudioEngine::music_play(int, const uint8_t *song, uint32_t len) {
    std::lock_guard<std::mutex> g(mu_);
    music_acc_ = music_us_acc_ = 0;
    return seq_.load_and_start(song, len);
}
void AudioEngine::music_stop()                     { std::lock_guard<std::mutex> g(mu_); seq_.stop(); }
bool AudioEngine::music_done()                     { std::lock_guard<std::mutex> g(mu_); return seq_.done(); }
void AudioEngine::music_set_volume(int v)          { std::lock_guard<std::mutex> g(mu_); seq_.set_master_volume(v); }
void AudioEngine::music_set_layer_volume(int v)    { std::lock_guard<std::mutex> g(mu_); seq_.set_layer_volume(v); }

// hmi_timer_add_event_5d093 / hmi_timer_remove_event_5d3a9: a new event starts with an empty accumulator.
// Game thread only (sound.cpp and pump_fade_timers), so no lock.
void AudioEngine::music_timer(MusicTimer which, int rate_hz) {
    timer_rate_[which] = rate_hz > 0 ? rate_hz : 0;
    timer_acc_[which] = 0;
}

void AudioEngine::pump_fade_timers(uint64_t elapsed_us) {
    for (int w = 0; w < 2; w++) {
        int rate = timer_rate_[w];
        if (!rate) { timer_acc_[w] = 0; continue; }
        timer_acc_[w] += elapsed_us * (uint64_t)rate;
        int n = 0;
        while (timer_acc_[w] >= 1000000u && timer_rate_[w] == rate) {
            timer_acc_[w] -= 1000000u;
            music_fade_tick((MusicTimer)w);              // may change the rate (fade end): the loop stops then
            if (++n == 64) { timer_acc_[w] = 0; break; }
        }
    }
}

void AudioEngine::advance_music_us(uint64_t us) {
    std::lock_guard<std::mutex> g(mu_);
    if (clock_ != CLOCK_EXTERNAL) return;
    int rate = seq_.rate();
    if (!rate || seq_.done()) { music_us_acc_ = 0; return; }
    music_us_acc_ += us * (uint64_t)rate;
    int n = 0;
    while (music_us_acc_ >= 1000000u) {
        music_us_acc_ -= 1000000u;
        seq_.tick();
        if (++n == 2 * rate) { music_us_acc_ = 0; break; }   // more than 2 s behind: drop the backlog
    }
}

void AudioEngine::advance_music_frames(int frames) {
    MidiOut *out = seq_.output();
    bool synth = out && out->renders();
    const uint64_t orate = (uint64_t)mixer_.output_rate();
    int done = 0;
    while (done < frames) {
        int rate = seq_.rate();
        if (!rate || seq_.done()) {
            if (synth) out->render(acc_.data() + 2 * done, frames - done, (int)orate);
            music_acc_ = 0;
            break;
        }
        uint64_t need = music_acc_ >= orate ? 0 : (orate - music_acc_ + (uint64_t)rate - 1) / (uint64_t)rate;
        int n = (int)std::min<uint64_t>(need, (uint64_t)(frames - done));
        if (n > 0 && synth) out->render(acc_.data() + 2 * done, n, (int)orate);
        music_acc_ += (uint64_t)n * (uint64_t)rate;
        done += n;
        while (music_acc_ >= orate) { music_acc_ -= orate; seq_.tick(); }
    }
}

// Linear up to the knee, then a hyperbola that approaches full scale: y = K + d * R / (d + R) with
// d = |x| - K, R = 32767 - K.
int AudioEngine::soft_clip(int32_t x) {
    const int64_t K = SOFT_KNEE, R = 32767 - SOFT_KNEE;
    int64_t a = x < 0 ? -(int64_t)x : x;
    if (a <= K) return (int)x;
    int64_t d = a - K;
    int64_t y = K + d * R / (d + R);
    if (y > 32767) y = 32767;
    return (int)(x < 0 ? -y : y);
}

void AudioEngine::render(int16_t *out, int frames) {
    std::lock_guard<std::mutex> g(mu_);
    if ((int)acc_.size() < 2 * frames) acc_.resize((size_t)(2 * frames));
    std::fill(acc_.begin(), acc_.begin() + 2 * frames, 0);
    mixer_.mix(acc_.data(), frames);
    int m = mixer_.master();
    for (int i = 0; i < 2 * frames; i++) acc_[(size_t)i] = (int32_t)(((int64_t)acc_[(size_t)i] * m) >> 8);
    if (clock_ == CLOCK_RENDER) advance_music_frames(frames);        // the synth adds after the master gain
    for (int i = 0; i < 2 * frames; i++) out[i] = (int16_t)soft_clip(acc_[(size_t)i]);
}

// =====================================================================================================
bool audio_write_wav(const char *path, const int16_t *pcm, size_t frames, int channels, int rate) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    auto put16 = [f](unsigned v) { std::fputc((int)(v & 0xff), f); std::fputc((int)((v >> 8) & 0xff), f); };
    auto put32 = [&](unsigned v) { put16(v & 0xffff); put16(v >> 16); };
    unsigned bytes = (unsigned)(frames * (size_t)channels * 2);
    std::fwrite("RIFF", 1, 4, f); put32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); put32(16); put16(1); put16((unsigned)channels);
    put32((unsigned)rate); put32((unsigned)(rate * channels * 2)); put16((unsigned)(channels * 2)); put16(16);
    std::fwrite("data", 1, 4, f); put32(bytes);
    std::fwrite(pcm, 2, frames * (size_t)channels, f);              // little-endian host
    std::fclose(f);
    return true;
}

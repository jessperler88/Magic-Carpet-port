// Time control scheduling (timectl.h; port round 10 task D).
#include "timectl.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <string>

namespace {
int clamp_speed(int s) { return s < TIME_SPEED_MIN ? TIME_SPEED_MIN : s > TIME_SPEED_MAX ? TIME_SPEED_MAX : s; }
}

void TimeControl::set_paused(bool on) {
    paused_ = on;
    if (!on) steps_ = 0;
}

void TimeControl::step(int n) {
    if (n < 1) n = 1;
    paused_ = true;
    const long long s = (long long)steps_ + n;
    steps_ = s > 1000000 ? 1000000 : (int)s;
}

void TimeControl::set_speed16(int s) { speed_ = s == TIME_SPEED_FASTEST ? TIME_SPEED_FASTEST : clamp_speed(s); }

bool TimeControl::set_speed_text(const char *text) {
    if (!text) return false;
    std::string t;
    for (const char *p = text; *p; p++)
        if (!std::isspace((unsigned char)*p)) t += (char)std::tolower((unsigned char)*p);
    if (t == "max" || t == "fast" || t == "fastest" || t == "0") { set_speed16(TIME_SPEED_FASTEST); return true; }
    if (t == "normal" || t == "x1") { normal(); return true; }
    if (!t.empty() && t[0] == 'x') t.erase(0, 1);
    if (t.empty()) return false;
    const char *s = t.c_str();
    char *end = nullptr;
    const double a = std::strtod(s, &end);
    if (end == s) return false;
    double v = a;
    if (*end == '/') {
        const char *d = end + 1;
        const double b = std::strtod(d, &end);
        if (end == d || b <= 0) return false;
        v = a / b;
    }
    if (*end != 0 || !(v > 0)) return false;
    const long q = std::lround(v * TIME_SPEED_X1);
    set_speed16(q < TIME_SPEED_MIN ? TIME_SPEED_MIN : q > TIME_SPEED_MAX ? TIME_SPEED_MAX : (int)q);   // never 0 (= fastest)
    return true;
}

void TimeControl::faster() {
    if (speed_ == TIME_SPEED_FASTEST) return;
    if (speed_ >= TIME_SPEED_MAX) { speed_ = TIME_SPEED_FASTEST; return; }
    // to the next power of two above (x1.5 -> x2)
    int p = TIME_SPEED_MIN;
    while (p <= speed_) p *= 2;
    speed_ = clamp_speed(p);
}

void TimeControl::slower() {
    if (speed_ == TIME_SPEED_FASTEST) { speed_ = TIME_SPEED_MAX; return; }
    int p = TIME_SPEED_MAX;
    while (p >= speed_ && p > TIME_SPEED_MIN) p /= 2;
    speed_ = clamp_speed(p);
}

std::string TimeControl::speed_name(int s) {
    if (s == TIME_SPEED_FASTEST) return "max";
    char b[32];
    if (s % TIME_SPEED_X1 == 0) std::snprintf(b, sizeof b, "x%d", s / TIME_SPEED_X1);
    else if (TIME_SPEED_X1 % s == 0) std::snprintf(b, sizeof b, "x1/%d", TIME_SPEED_X1 / s);
    else std::snprintf(b, sizeof b, "x%g", (double)s / TIME_SPEED_X1);
    return b;
}

std::string TimeControl::describe() const {
    if (paused_) {
        if (steps_ == 0) return "paused";
        char b[48];
        std::snprintf(b, sizeof b, "paused, %d step%s", steps_, steps_ == 1 ? "" : "s");
        return b;
    }
    return speed_ == TIME_SPEED_X1 ? std::string() : speed_name(speed_);
}

uint64_t TimeControl::period_us(uint64_t base) const {
    if (paused_ || speed_ == TIME_SPEED_FASTEST || speed_ == TIME_SPEED_X1) return base;
    const uint64_t p = base * TIME_SPEED_X1 / (uint64_t)speed_;
    return p ? p : 1;
}

int TimeControl::frame_cap(int base_cap) const {
    if (speed_ == TIME_SPEED_FASTEST) return 1 << 30;
    if (speed_ <= TIME_SPEED_X1) return base_cap;
    return base_cap * speed_ / TIME_SPEED_X1;
}

void TimeControl::begin_frame(uint64_t now, uint64_t *next_tick) {
    behind_ = false;
    if (paused_ || speed_ == TIME_SPEED_FASTEST) *next_tick = now;
}

bool TimeControl::want_tick(uint64_t now, uint64_t *next_tick, uint64_t base_period, int base_cap, int t, uint64_t spent) {
    const bool over_budget = t > 0 && spent >= budget_us;
    if (paused_) {
        if (steps_ <= 0) return false;
        if (over_budget) { behind_ = true; return false; }
        return true;
    }
    if (speed_ == TIME_SPEED_FASTEST) {
        if (over_budget) { behind_ = true; return false; }
        return true;
    }
    const int cap = frame_cap(base_cap);
    if (now < *next_tick) return false;
    if (t >= cap) { behind_ = true; return false; }
    if (speed_ > TIME_SPEED_X1 && over_budget) { behind_ = true; return false; }
    *next_tick += period_us(base_period);
    if (t + 1 == cap) behind_ = true;          // as the loop's `if (t == 4) next_tick = now`
    return true;
}

void TimeControl::tick_ran() {
    if (paused_ && steps_ > 0) steps_--;
}

void TimeControl::end_frame(uint64_t now, uint64_t *next_tick) {
    if (paused_ || speed_ == TIME_SPEED_FASTEST || behind_) *next_tick = now;
    behind_ = false;
}

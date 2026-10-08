// Time control for mcport's main loop (Phase 4, round 10 task D; port-only, nothing here exists in carpet.exe).
//
// Pause / resume, single steps of N ticks, fast-forward (x2 .. x64 or "as fast as possible") and slow motion
// (x1/2 .. x1/16). It only decides *when* main.cpp runs the next simulation tick; the ticks themselves are
// the same ticks in the same order, so a run with pauses, steps and speed changes produces the same per-tick
// state as one without (timectl_test checks the per-tick checksums of a headless mcport run). It is not the
// game's own speed key F3 (Config.substeps: 4 / 16 thing updates per tick, which changes the simulation).
// main.cpp refuses it in a network game (the peers' ticks are locked together).
//
// Pure scheduling maths, no SDL (timectl_test links it alone).
//
// Frame loop use:
//   tc.begin_frame(now, &next_tick);
//   for (int t = 0; tc.want_tick(now, &next_tick, base_period, base_cap, t, spent_us()); t++) { tick; tc.tick_ran(); }
//   tc.end_frame(now, &next_tick);
//   alpha = pace_alpha(now, next_tick - tc.period_us(base_period), tc.period_us(base_period));
#pragma once
#include <cstdint>
#include <string>

constexpr int TIME_SPEED_X1 = 16;        // speeds are in 1/16: 16 = x1 (the configured tick rate)
constexpr int TIME_SPEED_MIN = 1;        // x1/16
constexpr int TIME_SPEED_MAX = 1024;     // x64
constexpr int TIME_SPEED_FASTEST = 0;    // as fast as possible: ticks back to back, one frame per time budget

class TimeControl {
public:
    // ---- commands (keys, console `time ...`, MC_TIME_SPEED) ----
    void set_paused(bool on);
    void toggle_pause() { set_paused(!paused_); }
    // Pause (if running) and queue n more ticks (n >= 1; at most 1000000 pending).
    void step(int n);
    void set_speed16(int s);              // TIME_SPEED_FASTEST or 1..1024 (clamped)
    // "4", "x4", "0.5", "1/4", "x1/2", "max" / "fast" (as fast as possible), "normal" (x1). False = not a speed.
    bool set_speed_text(const char *text);
    void faster();                        // x1/16 .. x64 doubling, then "as fast as possible"
    void slower();                        // halving down to x1/16; from "as fast as possible" to x64
    void normal() { set_speed16(TIME_SPEED_X1); }
    // Back to running at x1 with nothing pending (a network game, a new mode).
    void reset() { *this = TimeControl{}; }

    bool paused() const { return paused_; }
    int pending_steps() const { return steps_; }
    int speed16() const { return speed_; }
    bool fastest() const { return speed_ == TIME_SPEED_FASTEST; }
    // True when the loop runs exactly as without time control (running at x1).
    bool neutral() const { return !paused_ && speed_ == TIME_SPEED_X1; }
    // "" when neutral; else "paused", "paused, 3 steps", "x4", "x1/2", "x1.5", "max".
    std::string describe() const;
    static std::string speed_name(int speed16);   // "x1", "x4", "x1/2", "x1.5", "max"

    // ---- scheduling (microseconds) ----
    // The period between ticks at the current speed (base / speed; the base period when paused / fastest).
    uint64_t period_us(uint64_t base_period_us) const;
    // At most this many ticks per frame at the current speed: `base_cap` (main.cpp: 4 in a level, 32 for
    // movies / the viewer) scaled up with the speed.
    int frame_cap(int base_cap) const;
    void begin_frame(uint64_t now_us, uint64_t *next_tick_us);
    // Whether to run tick number `t` (0-based) of this frame now; `spent_us` = time this frame has spent so
    // far (the budget applies only above x1, to steps and to "as fast as possible": `t == 0` always runs).
    // Advances *next_tick_us by one period for a clocked tick.
    bool want_tick(uint64_t now_us, uint64_t *next_tick_us, uint64_t base_period_us, int base_cap, int t, uint64_t spent_us);
    void tick_ran();                      // after each tick want_tick allowed (consumes a step)
    // After the frame's ticks: a clock that fell behind (cap or budget reached), a pause and "as fast as
    // possible" restart the clock at `now` (no catching up), as the loop did at its 4-tick cap.
    void end_frame(uint64_t now_us, uint64_t *next_tick_us);
    // Frames hold the interpolation (pacing.h TickInterp::hold) while paused with nothing to step.
    bool holding() const { return paused_ && steps_ == 0; }

    uint64_t budget_us = 40000;           // time per frame spent ticking (x2 .. fastest, steps)

private:
    bool paused_ = false;
    int steps_ = 0;
    int speed_ = TIME_SPEED_X1;
    bool behind_ = false;                 // this frame hit the cap / the budget
};

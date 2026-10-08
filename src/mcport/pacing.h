// Frame pacing for mcport (Phase 3, round 7 task D; port-only, nothing here exists in carpet.exe).
//
// The simulation runs at a fixed tick rate (MC_TICK_HZ, default 25); the display runs at whatever
// vsync / fps_cap allows. With g_settings.interpolate the frame drawn between tick N-1 and tick N shows
// the state at alpha = (time since tick N was due) / tick period, i.e. one tick behind, lerped:
//   - the local camera (player_camera of the two ticks: angles wrap at 0x800, positions at 0x10000,
//     pitch / roll / zoom lerped; a jump - teleport, respawn, level change - snaps to the new tick);
//   - per Thing slot the position after the previous tick (RenderInterp.prev_pos), identity-checked
//     (a slot freed and reallocated between the ticks, or that jumped, gets prev = current: no lerp).
// Everything here only reads the game state; nothing the tick computes depends on it.
//
// No SDL: the unit test (tests/pacing_test.cpp) links this file with ${MC_SIM_ALL}.
#pragma once
#include <cstdint>
#include <vector>
#include "render.h"   // Camera, RenderInterp

// Largest per-tick move that is still lerped (world units; a cell is 0x100). The player's flyer and
// the fastest projectiles move well under 2 cells per tick; teleports / respawns move further.
constexpr int PACE_JUMP_XY = 0x400;   // 4 cells on x or y (16-bit wrap-aware)
constexpr int PACE_JUMP_Z  = 0x800;   // 64 height units of the height map (* 0x20)

// alpha in 16.16 (0 .. 0x10000) of `now` between the tick that is due at `last_us` and the next one.
uint32_t pace_alpha(uint64_t now_us, uint64_t last_us, uint64_t period_us);

// Lerps with exact end points: alpha 0 returns a, alpha >= 0x10000 returns b.
int pace_lerp(int a, int b, uint32_t alpha);           // plain (pitch, zoom, z)
int pace_lerp16(int a, int b, uint32_t alpha);         // 16-bit world coordinate: shortest way round the
                                                       // torus, result sign-extended (as player_camera)
int pace_lerp_angle(int a, int b, uint32_t alpha);     // 0x800 angle: shortest way, result & 0x7ff
int pace_lerp_roll(int a, int b, uint32_t alpha);      // signed rate used as roll: wrap-aware, unmasked
bool pace_camera_jump(const Camera &a, const Camera &b);
// Interpolated camera; `masked_xy` keeps cam_x / cam_y in 0..0xffff (the free camera) instead of
// sign-extended (player_camera). A jump returns b.
Camera pace_lerp_camera(const Camera &a, const Camera &b, uint32_t alpha, bool masked_xy = false);

// Identity + position of one Thing slot as seen after a tick.
struct PaceSlot {
    int16_t x, y, z;
    uint8_t cls, type;
    uint16_t owner;
    uint32_t gen = 0;               // thing_slot_generation: another allocation of the slot never lerps
};
// True when `cur` may be drawn lerped from `pre` (same live Thing, no jump).
bool pace_slot_continues(const PaceSlot &pre, const PaceSlot &cur);

// Tick-to-tick interpolation state of the game view.
class TickInterp {
public:
    // Forget the previous tick (level start / load, mode change): until the next tick, frames show
    // the current state (RenderInterp.active false).
    void reset();
    // Call right before / after every simulation tick. `local` = the local player (camera).
    void before_tick(int local);
    void after_tick(int local);
    // A due tick that did not run (paused movie, ended movie): the next frames show the current state
    // without moving back and forth.
    void hold();
    bool valid() const { return valid_; }
    // Fill `ri` for a frame at `alpha`. With `camera` the interpolated player camera replaces
    // player_camera(local) in hud.cpp's frame_pass (draw-only pass).
    void fill(RenderInterp &ri, uint32_t alpha, bool camera) const;
    // Statistics for the tests / the title bar.
    int lerped_slots() const { return lerped_; }
    int snapped_slots() const { return snapped_; }
    bool camera_snapped() const { return cam_snap_; }
    const Camera &camera_prev() const { return cam_prev_; }
    const Camera &camera_cur() const { return cam_cur_; }
    const int16_t (*prev_pos() const)[3] { return reinterpret_cast<const int16_t (*)[3]>(prev_.data()); }
    int slots() const { return (int)pre_.size(); }

private:
    bool valid_ = false;
    bool have_pre_ = false;
    std::vector<PaceSlot> pre_;     // snapshot before the tick
    std::vector<int16_t> prev_;     // 3 per slot: the position to lerp from (= current when no lerp)
    Camera cam_pre_{}, cam_prev_{}, cam_cur_{};
    bool cam_snap_ = false;
    int lerped_ = 0, snapped_ = 0;
};

// Read a slot from the pool (thing_at / thing_pool_slots).
PaceSlot pace_read_slot(int idx);

// Frame limiter for g_settings.fps_cap (0 = off): returns the time to wait (us) before the next
// frame may start, given when the current one started.
uint64_t pace_cap_wait_us(uint64_t frame_start_us, uint64_t now_us, int fps_cap);

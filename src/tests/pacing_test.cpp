#define _CRT_SECURE_NO_WARNINGS
// Frame pacing test (port round 7, task D): mcport/pacing.cpp with ${MC_SIM_ALL}, no renderer, no SDL.
//  1. the interpolation math by construction: alpha, 16-bit wrap, 0x800 angle wrap, roll, alpha 0 / 1
//     give the ticks exactly, camera jump detection, slot identity (reuse / jump);
//  2. TickInterp around real ticks of level 38 (400 ticks): every call leaves the game state untouched
//     (GameState + Config + cell lists + g_rng16 hashed before / after), prev_pos is the pre-tick
//     position of every continuing slot, and injected slot reuse / jumps / moves are classified right;
//  3. headless mcport (SDL_VIDEODRIVER=dummy) `play 0` twice, without and with MC_INTERPOLATE=1:
//     the per-tick state checksums (MC_TICK_LOG) are identical and the second run drew interpolated
//     frames. SKIP when mcport.exe is not built (MCPORT_EXE).
// argv[1] = game dir.
#include "pacing.h"
#include "sim.h"
#include "thing.h"
#include "player.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static bool same_cam(const Camera &a, const Camera &b) {
    return a.cam_x == b.cam_x && a.cam_y == b.cam_y && a.yaw == b.yaw && a.cam_z == b.cam_z && a.pitch == b.pitch &&
           a.roll == b.roll && a.zoom == b.zoom;
}

static void test_math() {
    // alpha
    CHECK(pace_alpha(1000, 1000, 40000) == 0);
    CHECK(pace_alpha(999, 1000, 40000) == 0);
    CHECK(pace_alpha(21000, 1000, 40000) == 0x8000);
    CHECK(pace_alpha(41000, 1000, 40000) == 0x10000);
    CHECK(pace_alpha(99999, 1000, 40000) == 0x10000);
    CHECK(pace_alpha(5, 0, 0) == 0);

    // plain lerp, exact end points
    CHECK(pace_lerp(10, 30, 0) == 10 && pace_lerp(10, 30, 0x10000) == 30 && pace_lerp(10, 30, 0x8000) == 20);
    CHECK(pace_lerp(30, 10, 0x8000) == 20 && pace_lerp(-7, 9, 0x20000) == 9);

    // 16-bit world coordinates: the short way round the torus, sign-extended like player_camera
    CHECK(pace_lerp16(0x7ff0, -0x7ff0, 0x8000) == -0x8000);        // 0x7ff0 -> 0x8010 through 0x8000
    CHECK(pace_lerp16(-100, 100, 0x8000) == 0);
    CHECK(pace_lerp16(-0x7ff0, 0x7ff0, 0x8000) == -0x8000);         // the other direction
    CHECK(pace_lerp16(0x7ff0, -0x7ff0, 0) == 0x7ff0 && pace_lerp16(0x7ff0, -0x7ff0, 0x10000) == -0x7ff0);
    for (int a = -0x8000; a < 0x8000; a += 0x1234)
        for (int b = -0x8000; b < 0x8000; b += 0x0fed) {
            CHECK(pace_lerp16(a, b, 0) == a);
            CHECK(pace_lerp16(a, b, 0x10000) == b);
            const int m = pace_lerp16(a, b, 0x8000);
            const int da = (int16_t)(uint16_t)(m - a), db = (int16_t)(uint16_t)(b - m);
            CHECK(da - db >= -1 && da - db <= 1);                    // halfway on the short path
        }

    // 0x800 angles
    CHECK(pace_lerp_angle(0x7f0, 0x010, 0x8000) == 0x000);
    CHECK(pace_lerp_angle(0x010, 0x7f0, 0x8000) == 0x000);
    CHECK(pace_lerp_angle(0x100, 0x300, 0x8000) == 0x200);
    CHECK(pace_lerp_angle(0x7f0, 0x010, 0) == 0x7f0 && pace_lerp_angle(0x7f0, 0x010, 0x10000) == 0x010);
    for (int a = 0; a < 0x800; a += 0x37)
        for (int b = 0; b < 0x800; b += 0x53) {
            CHECK(pace_lerp_angle(a, b, 0) == a && pace_lerp_angle(a, b, 0x10000) == b);
            const int m = pace_lerp_angle(a, b, 0x4000);
            CHECK(m >= 0 && m < 0x800);
        }
    // roll (signed yaw rate): wrap-aware, unmasked, exact ends
    CHECK(pace_lerp_roll(-10, 20, 0x8000) == 5);
    CHECK(pace_lerp_roll(0x7fb, 5, 0x10000) == 5 && pace_lerp_roll(0x7fb, 5, 0) == 0x7fb);
    CHECK((pace_lerp_roll(0x7fb, 5, 0x8000) & 0x7ff) == 0);

    // cameras
    Camera a{0x1000, -0x2000, 0x7f0, 0x900, 20, -6, 0x100}, b = a;
    b.cam_x += 0x80; b.cam_y -= 0x40; b.yaw = 0x010; b.cam_z += 0x40; b.pitch = 40; b.roll = 6; b.zoom = 0x120;
    CHECK(same_cam(pace_lerp_camera(a, b, 0), a));
    CHECK(same_cam(pace_lerp_camera(a, b, 0x10000), b));
    {
        const Camera m = pace_lerp_camera(a, b, 0x8000);
        CHECK(m.cam_x == 0x1040 && m.cam_y == -0x2020 && m.yaw == 0 && m.cam_z == 0x920 && m.pitch == 30 && m.roll == 0 &&
              m.zoom == 0x110);
    }
    // across the map edge (sign-extended and masked representations)
    {
        Camera p = a, q = a;
        p.cam_x = -0x40; q.cam_x = 0x40;
        CHECK(!pace_camera_jump(p, q));
        CHECK(pace_lerp_camera(p, q, 0x8000).cam_x == 0);
        p.cam_x = 0xffc0; q.cam_x = 0x0040;                                    // the free camera's 0..0xffff
        CHECK(pace_lerp_camera(p, q, 0x8000, true).cam_x == 0);
        CHECK(pace_lerp_camera(p, q, 0x4000, true).cam_x == 0xffe0);
        CHECK(pace_lerp_camera(p, q, 0, true).cam_x == 0xffc0 && pace_lerp_camera(p, q, 0x10000, true).cam_x == 0x40);
    }
    // jumps: teleport / respawn snap to the new tick at every alpha
    {
        Camera q = a;
        q.cam_x += PACE_JUMP_XY + 1;
        CHECK(pace_camera_jump(a, q));
        CHECK(same_cam(pace_lerp_camera(a, q, 0x100), q));
        q = a; q.cam_y -= PACE_JUMP_XY;
        CHECK(!pace_camera_jump(a, q));
        q = a; q.cam_z += PACE_JUMP_Z + 1;
        CHECK(pace_camera_jump(a, q));
    }

    // slot identity
    PaceSlot s{100, 200, 300, 5, 7, 42};
    PaceSlot t = s;
    t.x += 0x80;
    CHECK(pace_slot_continues(s, t));
    t = s; t.cls = 0;                         CHECK(!pace_slot_continues(s, t));   // freed
    t = s; PaceSlot f = s; f.cls = 0;         CHECK(!pace_slot_continues(f, t));   // newly allocated
    t = s; t.type = 8;                        CHECK(!pace_slot_continues(s, t));   // reused, other type
    t = s; t.owner = 43;                      CHECK(!pace_slot_continues(s, t));   // reused, other owner
    t = s; t.gen++;                           CHECK(!pace_slot_continues(s, t));   // reused by an equal Thing
    t = s; t.y = (int16_t)(t.y - PACE_JUMP_XY - 1); CHECK(!pace_slot_continues(s, t));   // jump
    t = s; t.x = (int16_t)-0x7ff0; s.x = 0x7ff0;  CHECK(pace_slot_continues(s, t));   // across the map edge
    // frame limiter
    CHECK(pace_cap_wait_us(1000, 1000, 0) == 0);
    CHECK(pace_cap_wait_us(1000, 1000, 100) == 10000);
    CHECK(pace_cap_wait_us(1000, 12000, 100) == 0);
}

// FNV-1a over everything a tick reads or writes that the interpolation could touch.
static uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static uint32_t state_hash() {
    uint32_t h = 2166136261u;
    h = fnv(h, g_state, sizeof *g_state);
    h = fnv(h, g_cfg, sizeof *g_cfg);
    h = fnv(h, g_cell_things, sizeof(uint16_t) * MC_MAP_CELLS);
    h = fnv(h, g_map_height, MC_MAP_CELLS);
    h = fnv(h, &g_rng16, sizeof g_rng16);
    return h;
}

static void test_sim() {
    if (!sim_load_level(38)) { std::printf("FAIL: level 38 did not load\n"); g_fail++; return; }
    TickInterp ip;
    RenderInterp ri;
    const int local = g_state->local_player & 7;
    long lerped = 0, snapped = 0, cam_snaps = 0, ticks = 0;
    for (int tick = 0; tick < 400; tick++) {
        uint32_t h = state_hash();
        ip.before_tick(local);
        CHECK(state_hash() == h);
        std::vector<PaceSlot> pre((size_t)thing_pool_slots());
        for (int i = 0; i < thing_pool_slots(); i++) pre[(size_t)i] = pace_read_slot(i);
        const Camera cam_pre = player_camera(local);
        game_tick_sim();
        ticks++;
        h = state_hash();
        ip.after_tick(local);
        CHECK(state_hash() == h);
        ip.fill(ri, 0x8000, true);
        CHECK(state_hash() == h);
        CHECK(ri.active && ri.have_camera && ri.alpha == 0x8000 && ri.prev_count == thing_pool_slots());
        // prev_pos: the pre-tick position of every continuing slot, the current one otherwise
        int bad = 0;
        for (int i = 1; i < thing_pool_slots(); i++) {
            const PaceSlot cur = pace_read_slot(i);
            const bool cont = pace_slot_continues(pre[(size_t)i], cur);
            const PaceSlot &from = cont ? pre[(size_t)i] : cur;
            if (ri.prev_pos[i][0] != from.x || ri.prev_pos[i][1] != from.y || ri.prev_pos[i][2] != from.z) bad++;
        }
        CHECK(bad == 0);
        lerped += ip.lerped_slots(); snapped += ip.snapped_slots();
        // camera: alpha 0 = the previous tick (unless it jumped), alpha 1 = this tick
        const Camera cam_cur = player_camera(local);
        ip.fill(ri, 0, true);
        if (ip.camera_snapped()) { cam_snaps++; std::printf("  camera snap at tick %d: %d,%d,%d -> %d,%d,%d\n", tick + 1, cam_pre.cam_x, cam_pre.cam_y, cam_pre.cam_z, cam_cur.cam_x, cam_cur.cam_y, cam_cur.cam_z); CHECK(same_cam(ri.camera, cam_cur)); }
        else CHECK(same_cam(ri.camera, cam_pre));
        ip.fill(ri, 0x10000, true);
        CHECK(same_cam(ri.camera, cam_cur));
        ip.fill(ri, 0x8000, false);
        CHECK(!ri.have_camera);
    }
    {   // cost of the snapshots (per tick, the whole pool)
        const auto c0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 1000; k++) { ip.before_tick(local); ip.after_tick(local); }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count() / 1000;
        std::printf("before_tick + after_tick over %d slots: %.1f us\n", thing_pool_slots(), us);
    }
    std::printf("sim: %ld ticks, per tick %.1f live slots lerped, %.2f snapped (new / freed / jumped), %ld camera snaps\n",
                ticks, (double)lerped / ticks, (double)snapped / ticks, cam_snaps);
    CHECK(lerped > 0);

    // Injected cases on live slots: reuse with another identity, a jump, a small move.
    std::vector<int> live;
    for (int i = 1; i < thing_pool_slots() && live.size() < 3; i++)
        if (thing_at((unsigned)i)->cls != 0 && thing_at((unsigned)i)->cls != 3) live.push_back(i);
    CHECK(live.size() == 3);
    if (live.size() == 3) {
        ip.before_tick(local);
        Thing *a = thing_at((unsigned)live[0]), *b = thing_at((unsigned)live[1]), *c = thing_at((unsigned)live[2]);
        const PaceSlot c0 = pace_read_slot(live[2]);
        // a: freed and allocated again in the same tick (the free stack is LIFO: the same slot)
        const uint8_t a_cls = a->cls, a_type = a->type;
        thing_free(a);
        Thing *n = thing_alloc();
        CHECK(n == a);
        n->cls = a_cls; n->type = (uint8_t)(a_type + 1);
        // b: teleported
        b->x = (uint16_t)(b->x + 0x3000);
        // c: moved a little
        c->x = (uint16_t)(c->x + 0x40); c->z = (int16_t)(c->z + 0x10);
        ip.after_tick(local);
        ip.fill(ri, 0x8000, false);
        CHECK(ri.prev_pos[live[0]][0] == (int16_t)n->x && ri.prev_pos[live[0]][1] == (int16_t)n->y);
        CHECK(ri.prev_pos[live[1]][0] == (int16_t)b->x);
        CHECK(ri.prev_pos[live[2]][0] == c0.x && ri.prev_pos[live[2]][2] == c0.z);
        // hold(): a due tick that did not run - no lerp back to the previous positions
        ip.hold();
        ip.fill(ri, 0x4000, true);
        CHECK(ri.prev_pos[live[2]][0] == (int16_t)c->x && ri.prev_pos[live[2]][2] == c->z);
        CHECK(same_cam(ri.camera, player_camera(local)));
        // reset(): nothing to interpolate from
        ip.reset();
        ip.fill(ri, 0x4000, true);
        CHECK(!ri.active && !ri.have_camera && ri.prev_pos == nullptr);
    }
}

#ifdef MCPORT_EXE
// Runs mcport headless; returns the lines of its tick log ("" entries when it failed).
static std::vector<std::string> run_mcport(const std::string &game_dir, bool interpolate, const std::filesystem::path &dir) {
    std::vector<std::string> lines;
#ifdef _WIN32
    const std::filesystem::path log = dir / (interpolate ? "ticks_lerp.log" : "ticks_plain.log");
    std::error_code ec;
    std::filesystem::remove(log, ec);
    _putenv_s("SDL_VIDEODRIVER", "dummy");
    _putenv_s("SDL_AUDIODRIVER", "dummy");
    _putenv_s("MC_SOUND", "0");
    _putenv_s("MC_MUSIC", "0");
    _putenv_s("MC_TICK_HZ", "100");
    _putenv_s("MC_QUIT_AFTER_TICKS", "300");
    _putenv_s("MC_INTERPOLATE", interpolate ? "1" : "0");
    _putenv_s("MC_TICK_LOG", log.string().c_str());
    _putenv_s("MC_SAVE_DIR", dir.string().c_str());    // config / saves in the test dir, not the user's
    _putenv_s("MC_SHOT", interpolate ? "150,151,152" : "");
    // faithful settings apart from the interpolation (config.h: the command line overrides the env)
    std::string cmd = std::string("\"") + MCPORT_EXE + "\" \"" + game_dir + "\" play 0 --faithful --set pacing.interpolate=" +
                      (interpolate ? "1" : "0");
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    const std::string cwd = dir.string();
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        std::printf("FAIL: could not start %s\n", MCPORT_EXE);
        g_fail++;
        return lines;
    }
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        std::printf("FAIL: mcport did not finish in 120 s\n");
        g_fail++;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (code != 0) { std::printf("FAIL: mcport exit code %lu\n", (unsigned long)code); g_fail++; }
    std::ifstream f(log);
    for (std::string l; std::getline(f, l);) lines.push_back(l);
#else
    (void)game_dir; (void)interpolate; (void)dir;
#endif
    return lines;
}
#endif

static void test_mcport(const char *game_dir) {
#ifdef MCPORT_EXE
    std::error_code ec;
    if (!std::filesystem::exists(MCPORT_EXE, ec)) { std::printf("SKIP mcport run: %s not built\n", MCPORT_EXE); return; }
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "mc_pacing_test";
    if (const char *e = std::getenv("MC_PACING_DIR")) dir = e;
    std::filesystem::create_directories(dir, ec);
    const std::vector<std::string> plain = run_mcport(game_dir, false, dir);
    const std::vector<std::string> lerp = run_mcport(game_dir, true, dir);
    // the last line is "frames N lerped M ticks T"
    CHECK(plain.size() == 301 && lerp.size() == 301);
    if (plain.size() != 301 || lerp.size() != 301) return;
    int diff = 0, first = -1;
    for (int i = 0; i < 300; i++)
        if (plain[(size_t)i] != lerp[(size_t)i]) { diff++; if (first < 0) first = i + 1; }
    long frames = 0, lerped = 0, ticks = 0;
    std::sscanf(lerp[300].c_str(), "frames %ld lerped %ld ticks %ld", &frames, &lerped, &ticks);
    std::printf("mcport play 0, 300 ticks at 100 Hz: %d of 300 tick checksums differ (first %d); interpolated run: %ld frames, "
                "%ld drawn between ticks (%s)\n", diff, first, frames, lerped, dir.string().c_str());
    std::printf("  plain run: %s\n", plain[300].c_str());
    CHECK(diff == 0);
    CHECK(lerped > 0);
#else
    (void)game_dir;
    std::printf("SKIP mcport run: MCPORT_EXE not defined\n");
#endif
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    test_math();
    std::printf("math: %s\n", g_fail ? "FAILED" : "ok");
    if (!sim_init(game_dir)) { std::printf("SKIP sim: sim_init failed for %s\n", game_dir); return g_fail ? 1 : 0; }
    sim_register_gameplay();
    test_sim();
    test_mcport(game_dir);
    std::printf("%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

// timectl_test (port round 10, task D; docs/analysis/port_timectl.md): the main loop's time control, the tick
// profiler, the screenshot writer and a headless mcport run with pause / step / fast-forward.
//   1. TimeControl scheduling maths (mcport/timectl.h): at x1 running it schedules exactly as the loop did
//      (up to 4 due ticks per frame, clock restarted after the 4th) over 20000 random frames; pause, steps,
//      x4 / x1/2 / x64 rates over simulated seconds, "as fast as possible" bounded by the frame budget; the
//      speed ladder, text parsing, descriptions.
//   2. Tick profiler (mcengine/tick_profile.h): level 38 for 300 ticks with and without g_tick_profile gives
//      identical per-tick state checksums; the profile has every phase / the creature and effect classes.
//   3. PNG writer (mcport/screenshot.h): round trips, multi-block (> 64 KiB) images, CRC of a known string.
//   4. mcport headless (when built): `play 0 --faithful` for 300 ticks plain, with injected keys (Pause, End
//      steps, PageUp / PageDown / Insert speeds, Ctrl+F11 screenshot) and with MC_TIME_SPEED=max: the three
//      per-tick checksum logs are identical; the log shows the time commands; the screenshot PNG decodes;
//      mcport.log was rotated to mcport.1.log.
#include "timectl.h"
#include "screenshot.h"
#include "tick_profile.h"
#include "sim.h"
#include "thing.h"
#include "player.h"
#include "net.h"
#include "mc_globals.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static std::string env_or_empty(const char *name) {
#ifdef _WIN32
    char *v = nullptr;
    size_t n = 0;
    std::string r;
    if (_dupenv_s(&v, &n, name) == 0 && v) { r = v; std::free(v); }
    return r;
#else
    const char *v = std::getenv(name);
    return v ? v : "";
#endif
}

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static uint32_t s_lcg = 12345;
static uint32_t rnd(uint32_t n) { s_lcg = s_lcg * 1103515245u + 12345u; return (s_lcg >> 8) % n; }

// ---- 1. scheduling ----------------------------------------------------------------------------------------
// The loop before round 10 (main.cpp RUN_LEVEL): returns the ticks run.
static int old_loop(uint64_t now, uint64_t *next, uint64_t period) {
    int t = 0;
    while (now >= *next && t < 4) { *next += period; t++; }
    if (t == 4) *next = now;
    return t;
}
static int new_loop(TimeControl &tc, uint64_t now, uint64_t *next, uint64_t period, int cap = 4, uint64_t tick_cost = 0) {
    int t = 0;
    tc.begin_frame(now, next);
    while (tc.want_tick(now, next, period, cap, t, (uint64_t)t * tick_cost)) { t++; tc.tick_ran(); }
    tc.end_frame(now, next);
    return t;
}
// Ticks run over `secs` simulated seconds of frames every `frame_us`.
static long run_for(TimeControl &tc, double secs, uint64_t frame_us, uint64_t period, uint64_t tick_cost = 0) {
    uint64_t now = 1000000, next = now;
    long ticks = 0;
    const uint64_t end = now + (uint64_t)(secs * 1e6);
    for (; now < end; now += frame_us) ticks += new_loop(tc, now, &next, period, 4, tick_cost);
    return ticks;
}

static void test_schedule() {
    // x1 running: identical to the old loop, frame by frame, over random frame intervals (0.1 .. 250 ms)
    {
        TimeControl tc;
        uint64_t now = 5000000, n_old = now, n_new = now;
        const uint64_t period = 40000;
        int diff = 0;
        long total = 0;
        for (int f = 0; f < 20000; f++) {
            now += 100 + rnd(f % 50 == 0 ? 250000 : 30000);
            const int a = old_loop(now, &n_old, period), b = new_loop(tc, now, &n_new, period);
            if (a != b || n_old != n_new) diff++;
            total += a;
        }
        std::printf("x1: 20000 random frames, %ld ticks, %d frames differ from the old loop\n", total, diff);
        CHECK(diff == 0);
        CHECK(tc.neutral() && tc.describe().empty());
    }
    const uint64_t period = 40000;          // 25 Hz
    // pause: nothing runs, the clock follows `now`; steps run at once, one frame
    {
        TimeControl tc;
        uint64_t now = 1000000, next = now;
        CHECK(new_loop(tc, now, &next, period) == 1);
        tc.set_paused(true);
        CHECK(tc.paused() && tc.holding() && tc.describe() == "paused");
        now += 500000;
        CHECK(new_loop(tc, now, &next, period) == 0 && next == now);
        tc.step(3);
        CHECK(tc.describe() == "paused, 3 steps" && !tc.holding());
        now += 1000;
        CHECK(new_loop(tc, now, &next, period) == 3 && next == now && tc.pending_steps() == 0 && tc.holding());
        tc.step(1); tc.step(1);
        CHECK(tc.pending_steps() == 2);
        // a step budget: 50 steps at 1 ms each stop at the 40 ms budget (t == 0 always runs)
        tc.step(48);
        now += 1000;
        CHECK(new_loop(tc, now, &next, period, 4, 1000) == 40);
        CHECK(tc.pending_steps() == 10);
        now += 1000;
        CHECK(new_loop(tc, now, &next, period, 4, 1000) == 10);
        tc.set_paused(false);                   // resuming: the clock starts at `now` (no burst of missed ticks)
        now += 1000;
        CHECK(new_loop(tc, now, &next, period) == 1 && next == now - 1000 + period);
        CHECK(tc.pending_steps() == 0 && !tc.paused());
        tc.step(5); tc.set_paused(false);       // resume drops pending steps
        CHECK(tc.pending_steps() == 0);
    }
    // rates over 10 simulated seconds at 60 fps (16.667 ms frames)
    {
        TimeControl tc;
        const long x1 = run_for(tc, 10, 16667, period);
        tc.set_speed16(64);                     // x4
        const long x4 = run_for(tc, 10, 16667, period);
        tc.set_speed16(8);                      // x1/2
        const long xh = run_for(tc, 10, 16667, period);
        tc.set_speed16(1024);                   // x64: 1600 ticks/s, ~27 per frame (cap 256)
        const long x64 = run_for(tc, 10, 16667, period);
        tc.set_speed16(1024);
        const long x64_slow = run_for(tc, 10, 16667, period, 2000);   // 2 ms per tick: the 40 ms budget allows 20 / frame
        tc.set_speed16(TIME_SPEED_FASTEST);
        const long fast = run_for(tc, 1, 16667, period, 100);          // 0.1 ms per tick: 400 per frame
        std::printf("10 s at 60 fps, 25 Hz: x1 %ld, x4 %ld, x1/2 %ld, x64 %ld (x64 at 2 ms/tick %ld); fastest 1 s at 0.1 ms/tick: %ld\n",
                    x1, x4, xh, x64, x64_slow, fast);
        CHECK(x1 >= 249 && x1 <= 251);
        CHECK(x4 >= 998 && x4 <= 1001);
        CHECK(xh >= 124 && xh <= 126);
        CHECK(x64 >= 15970 && x64 <= 16001);
        CHECK(x64_slow >= 599 * 20 && x64_slow <= 600 * 20 + 1);
        CHECK(fast >= 60 * 400 - 400 && fast <= 61 * 400);
        CHECK(tc.period_us(period) == period);
        tc.set_speed16(64);
        CHECK(tc.period_us(period) == 10000 && tc.frame_cap(4) == 16);
        tc.set_speed16(4);
        CHECK(tc.period_us(period) == 160000 && tc.frame_cap(4) == 4);
    }
    // ladder, parsing, names
    {
        TimeControl tc;
        std::string seq;
        for (int i = 0; i < 8; i++) { tc.faster(); seq += TimeControl::speed_name(tc.speed16()) + " "; }
        std::printf("faster: %s\n", seq.c_str());
        CHECK(seq == "x2 x4 x8 x16 x32 x64 max max ");
        seq.clear();
        for (int i = 0; i < 12; i++) { tc.slower(); seq += TimeControl::speed_name(tc.speed16()) + " "; }
        std::printf("slower: %s\n", seq.c_str());
        CHECK(seq == "x64 x32 x16 x8 x4 x2 x1 x1/2 x1/4 x1/8 x1/16 x1/16 ");
        tc.normal();
        CHECK(tc.speed16() == TIME_SPEED_X1);
        struct { const char *text; bool ok; int speed; } k[] = {
            {"4", true, 64}, {"x4", true, 64}, {"0.5", true, 8}, {"1/4", true, 4}, {"x1/2", true, 8}, {"max", true, 0},
            {"FAST", true, 0}, {"normal", true, 16}, {"1.5", true, 24}, {"100", true, 1024}, {"0.01", true, 1},
            {"", false, -1}, {"abc", false, -1}, {"1/0", false, -1}, {"-2", false, -1}, {"4x", false, -1}};
        for (const auto &e : k) {
            TimeControl c;
            const bool ok = c.set_speed_text(e.text);
            if (ok != e.ok || (ok && c.speed16() != e.speed)) {
                std::printf("FAIL speed text '%s': %d %d\n", e.text, ok, c.speed16());
                g_fail++;
            }
        }
        tc.set_speed16(24);
        CHECK(tc.describe() == "x1.5");
        tc.faster();
        CHECK(tc.speed16() == 32);
        tc.set_speed16(24);
        tc.slower();
        CHECK(tc.speed16() == 16);
        CHECK(TimeControl::speed_name(0) == "max" && TimeControl::speed_name(1) == "x1/16" && TimeControl::speed_name(1024) == "x64");
    }
}

// ---- 2. tick profiler ----------------------------------------------------------------------------------------
static void test_profiler() {
    std::vector<uint32_t> plain, prof;
    TickProfile p;
    TickProfileWindow w;
    for (int pass = 0; pass < 2; pass++) {
        if (!sim_load_level(38)) { std::printf("FAIL: level 38 did not load\n"); g_fail++; return; }
        g_tick_profile = pass ? &p : nullptr;
        const int64_t t0 = tick_profile_now_ns();
        for (int t = 0; t < 300; t++) {
            if (pass) tick_profile_begin(&p);
            game_tick_sim();
            if (pass) { tick_profile_end(&p); w.add(p); }
            (pass ? prof : plain).push_back(net_state_checksum());
        }
        g_tick_profile = nullptr;
        std::printf("profiler %s: 300 ticks in %.2f ms\n", pass ? "on" : "off", (double)(tick_profile_now_ns() - t0) / 1e6);
    }
    int diff = 0;
    for (size_t i = 0; i < plain.size(); i++) diff += plain[i] != prof[i];
    std::printf("profiler: level 38, 300 ticks: %d checksums differ with the profiler on\n", diff);
    std::printf("  %s\n", w.format(6).c_str());
    std::printf("  %s\n", w.json().c_str());
    CHECK(diff == 0 && plain.size() == 300);
    CHECK(w.ticks == 300);
    CHECK(w.phase_ns[TP_THINGS] > 0 && w.total_ns >= w.phase_ns[TP_THINGS]);
    CHECK(w.cls_calls[5] > 0 && w.cls_calls[10] > 0 && w.cls_calls[3] > 0);
    int64_t cls_sum = w.lists_ns;
    for (int c = 0; c < TP_CLASSES; c++) cls_sum += w.cls_ns[c];
    CHECK(cls_sum <= w.phase_ns[TP_THINGS]);
    int64_t phase_sum = 0;
    for (int i = 0; i < TP_PHASES; i++) phase_sum += w.phase_ns[i];
    CHECK(phase_sum <= w.total_ns);
    const std::string js = w.json();
    CHECK(js.front() == '{' && js.back() == '}' && js.find("\"creature\"") != std::string::npos);
    CHECK(w.format().rfind("tick ", 0) == 0);
    TickProfileWindow empty;
    CHECK(empty.format().empty());
}

// ---- 3. PNG ----------------------------------------------------------------------------------------------------
static void test_png(const std::filesystem::path &dir) {
    CHECK(shot_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    for (const int sz : {1, 7, 160, 300}) {
        const int w = sz, h = sz * 2 / 3 + 1;
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        for (auto &b : rgb) b = (uint8_t)rnd(256);
        const std::vector<uint8_t> png = shot_encode_png(w, h, rgb.data());
        int dw = 0, dh = 0;
        std::vector<uint8_t> back;
        const bool ok = shot_decode_png(png, &dw, &dh, &back);
        CHECK(ok && dw == w && dh == h && back == rgb);
        std::vector<uint8_t> bad = png;
        bad[bad.size() / 2] ^= 1;
        CHECK(!shot_decode_png(bad, &dw, &dh, &back));
    }
    // a file a viewer can open: a gradient
    std::vector<uint8_t> g(256 * 64 * 3);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 256; x++) { uint8_t *p = &g[((size_t)y * 256 + x) * 3]; p[0] = (uint8_t)x; p[1] = (uint8_t)(y * 4); p[2] = (uint8_t)(255 - x); }
    const std::string f = (dir / "timectl_gradient.png").string();
    CHECK(shot_write_png(f.c_str(), 256, 64, g.data()));
    CHECK(shot_resolve_path("d", "a", 3) == (std::filesystem::path("d") / "a.png").string());
    CHECK(shot_resolve_path("d", "b.ppm", 3) == (std::filesystem::path("d") / "b.ppm").string());
    CHECK(shot_resolve_path("d", "", 7).find("_7.png") != std::string::npos);
}

// ---- 4. mcport headless ------------------------------------------------------------------------------------------
#ifdef MCPORT_EXE
static std::vector<std::string> read_lines(const std::filesystem::path &p) {
    std::vector<std::string> v;
    std::ifstream f(p);
    for (std::string l; std::getline(f, l);) v.push_back(l);
    return v;
}
static std::string read_all(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
// Runs mcport headless (play 0, faithful, 300 ticks at 100 Hz); returns the tick log lines.
static std::vector<std::string> run_mcport(const std::string &game_dir, const std::filesystem::path &dir, const char *name,
                                           const char *test_input, const char *speed) {
    std::vector<std::string> lines;
#ifdef _WIN32
    const std::filesystem::path log = dir / (std::string(name) + ".log");
    std::error_code ec;
    std::filesystem::remove(log, ec);
    _putenv_s("SDL_VIDEODRIVER", "dummy");
    _putenv_s("SDL_AUDIODRIVER", "dummy");
    _putenv_s("MC_SOUND", "0");
    _putenv_s("MC_MUSIC", "0");
    _putenv_s("MC_TICK_HZ", "100");
    _putenv_s("MC_QUIT_AFTER_TICKS", "300");
    _putenv_s("MC_TICK_LOG", log.string().c_str());
    _putenv_s("MC_SAVE_DIR", dir.string().c_str());
    _putenv_s("MC_SHOT", "");
    _putenv_s("MC_SHOT_DIR", (dir / "shots").string().c_str());
    _putenv_s("MC_TEST_INPUT", test_input);
    _putenv_s("MC_TIME_SPEED", speed);
    _putenv_s("MC_TICK_PROFILE", "100");
    std::string cmd = std::string("\"") + MCPORT_EXE + "\" \"" + game_dir + "\" play 0 --faithful";
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
    if (WaitForSingleObject(pi.hProcess, 180000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        std::printf("FAIL: mcport did not finish in 180 s\n");
        g_fail++;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (code != 0) { std::printf("FAIL: mcport exit code %lu\n", (unsigned long)code); g_fail++; }
    lines = read_lines(log);
#else
    (void)game_dir; (void)dir; (void)name; (void)test_input; (void)speed;
#endif
    return lines;
}
#endif

static void test_mcport(const char *game_dir, const std::filesystem::path &dir) {
#ifdef MCPORT_EXE
    std::error_code ec;
    if (!std::filesystem::exists(MCPORT_EXE, ec)) { std::printf("SKIP mcport run: %s not built\n", MCPORT_EXE); return; }
    std::filesystem::remove(dir / "mcport.log", ec);
    std::filesystem::remove(dir / "mcport.1.log", ec);
    std::filesystem::remove_all(dir / "shots", ec);
    const std::vector<std::string> plain = run_mcport(game_dir, dir, "ticks_plain", "", "");
    // SDL scancodes: Pause 72, End 77, PageUp 75, PageDown 78, Insert 73, LCtrl 224, F11 68.
    // Frames: pause at 30, three single steps, resume at x4, slower x2, back to x1, pause again, step, the
    // screenshot (Ctrl+F11), resume.
    const char *keys = "30@k72+40@k77+44@k77+48@k77+52@k72+54@k75+56@k75+70@k78+90@k73+110@k72+120@k77+124@k77+"
                       "130@k224+130@k68+140@k72";
    const std::vector<std::string> ctl = run_mcport(game_dir, dir, "ticks_timectl", keys, "");
    const std::string log1 = read_all(dir / "mcport.log");
    const std::vector<std::string> fast = run_mcport(game_dir, dir, "ticks_fast", "", "max");
    const std::string log2 = read_all(dir / "mcport.log");
    const std::string log_prev = read_all(dir / "mcport.1.log");
    CHECK(plain.size() == 301 && ctl.size() == 301 && fast.size() == 301);
    if (plain.size() != 301 || ctl.size() != 301 || fast.size() != 301) return;
    int d1 = 0, d2 = 0;
    for (int i = 0; i < 300; i++) {
        d1 += plain[(size_t)i] != ctl[(size_t)i];
        d2 += plain[(size_t)i] != fast[(size_t)i];
    }
    std::printf("mcport play 0, 300 ticks: with pause / steps / speeds %d checksums differ, as fast as possible %d\n", d1, d2);
    std::printf("  plain: %s\n  time control: %s\n  fastest: %s\n", plain[300].c_str(), ctl[300].c_str(), fast[300].c_str());
    CHECK(d1 == 0 && d2 == 0);
    // the commands reached the time control (the log of the second run, kept as mcport.1.log by the third)
    for (const char *s : {"time: paused (speed x1)", "time: paused, 1 step", "time: running (speed x1)", "speed x2)", "speed x4)",
                          "time: x2 (speed x2)", "screenshot ", "profile ticks"}) {
        const bool found = log_prev.find(s) != std::string::npos;
        if (!found) { std::printf("FAIL: '%s' not in the second run's mcport.1.log\n", s); g_fail++; }
    }
    CHECK(log1 == log_prev);                                      // rotation kept the previous run's file
    CHECK(log2.find("mcport: 300 ticks") != std::string::npos);
    // the screenshot decodes
    int shots = 0;
    for (const auto &e : std::filesystem::directory_iterator(dir / "shots", ec)) {
        int w = 0, h = 0;
        std::vector<uint8_t> rgb;
        const std::string data = read_all(e.path());
        const bool ok = shot_decode_png(std::vector<uint8_t>(data.begin(), data.end()), &w, &h, &rgb);
        std::printf("  screenshot %s: %dx%d %s\n", e.path().filename().string().c_str(), w, h, ok ? "ok" : "BAD");
        CHECK(ok && w > 0 && h > 0);
        shots++;
    }
    CHECK(shots == 1);
#else
    (void)game_dir; (void)dir;
    std::printf("SKIP mcport run: MCPORT_EXE not defined\n");
#endif
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "mc_timectl_test";
    if (const std::string e = env_or_empty("MC_TIMECTL_DIR"); !e.empty()) dir = e;
    std::filesystem::create_directories(dir, ec);
    test_schedule();
    test_png(dir);
    std::printf("schedule / png: %s\n", g_fail ? "FAILED" : "ok");
    if (!sim_init(game_dir)) {
        std::printf("SKIP sim: sim_init failed for %s\n", game_dir);
        return g_fail ? 1 : 0;
    }
    sim_register_gameplay();
    test_profiler();
    test_mcport(game_dir, dir);
    std::printf("%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

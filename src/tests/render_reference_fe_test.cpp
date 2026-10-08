// Front-end pixel comparison against the original (round 6, task D).
//
// The reference: extracted/reference/fe/fe%05d.fe, dumps of the retail carpet.exe in DOSBox driven by a
// scripted input tour of the front end (tools/reference/fb/fe_cave.py + fe_script.py: patch_carpet.py --fe,
// run_reference.py --fe). Each dump is the frame the original presented (back buffer with the pointer,
// right before vga_copy_320x200_610f0 copies it), the DAC, the front-end background buffer and the
// front-end / mouse / key globals (fe_cave.py has the layout). The script is in fe_script.txt next to the
// dumps: entries "state op wait a b", gated on the front-end state and counted in presents.
//
// The test drives the port's front end (fe_frame, frontend.cpp) with the same script: the same raw input
// globals are written after a frame (mouse position, click position, held / click flags, key state), an
// entry fires when fe_state() equals its state and `wait` frames passed since the previous one (plus, for
// a dump, until no palette fade runs), and at a dump the port renders the frame with now_ticks = the
// original's g_timer_ticks of that dump (the globe / hourglass animations of the main menu follow the
// timer), then compares the 320x200 frame and the palette with the dump. Diff images (reference | port |
// diff) go to argv[3] as rfe_<n>.ppm.
//
// argv: [1] game dir  [2] dump dir (default extracted/reference/fe)  [3] output dir (default .)
// Env: MC_RFE_LENIENT=1: differing frames do not fail the test. MC_RFE_TRACE=1: a line per frame.
//      MC_RFE_NOFIX=1: do not emulate the requested frontend.cpp fix (s_pal cleared on the reload after a level).
// Exit: 0 = SKIP (no dumps) or every dump matched (or lenient); 1 = a dump differs or was not reached.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "frontend.h"
#include "savegame.h"
#include "palette_fx.h"
#include "input.h"
#include "text.h"
#include "sound.h"
#include "mc_globals.h"
#include "thing.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#ifndef MC_REFERENCE_FE_DIR
#define MC_REFERENCE_FE_DIR MC_REPO_DIR "/extracted/reference/fe"
#endif

// fe_cave.py dump layout
static constexpr size_t O_SCREEN = 0, O_DAC = 64000, O_BG = 64768, O_G12 = 128768, O_G9E = 130048, O_CFG = 130816;
static constexpr size_t DUMP_SIZE = 131072 + 12;

struct Entry { int state, op, wait, a, b; };

static bool load_script(const std::string &dir, std::vector<Entry> *out) {
    mc_blob b;
    if (!mc_read_file((dir + "/fe_script.txt").c_str(), &b)) return false;
    std::string s(reinterpret_cast<const char *>(b.data), b.len);
    mc_blob_free(&b);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        Entry en{};
        if (std::sscanf(s.substr(pos, e - pos).c_str(), "%d %d %d %d %d", &en.state, &en.op, &en.wait, &en.a, &en.b) == 5)
            out->push_back(en);
        pos = e + 1;
    }
    return !out->empty();
}

static void write_diff_ppm(const std::string &path, const uint8_t *ref, const uint8_t *ref_pal, const uint8_t *port,
                           const uint8_t *port_pal) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const int W = 320, H = 200;
    std::fprintf(f, "P6\n%d %d\n255\n", W * 3 + 8, H);
    std::vector<uint8_t> row((size_t)(W * 3 + 8) * 3);
    auto put = [&](int x, int c, const uint8_t *pal, uint8_t *o) {
        o[x * 3 + 0] = (uint8_t)((pal[c * 3 + 0] & 63) * 255 / 63);
        o[x * 3 + 1] = (uint8_t)((pal[c * 3 + 1] & 63) * 255 / 63);
        o[x * 3 + 2] = (uint8_t)((pal[c * 3 + 2] & 63) * 255 / 63);
    };
    for (int y = 0; y < H; y++) {
        std::fill(row.begin(), row.end(), (uint8_t)0x40);
        for (int x = 0; x < W; x++) {
            const int i = y * W + x;
            put(x, ref[i], ref_pal, row.data());
            put(W + 4 + x, port[i], port_pal, row.data());
            uint8_t *o = row.data() + (2 * W + 8 + x) * 3;
            if (ref[i] != port[i]) { o[0] = 255; o[1] = 0; o[2] = 0; }
            else { put(0, ref[i], ref_pal, o); o[0] /= 3; o[1] /= 3; o[2] /= 3; }
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    const std::string dir = argc > 2 ? argv[2] : MC_REFERENCE_FE_DIR;
    const std::string out_dir = argc > 3 ? argv[3] : ".";
    const bool strict = std::getenv("MC_RFE_LENIENT") == nullptr;

    std::vector<Entry> script;
    if (!load_script(dir, &script)) {
        std::printf("SKIP: %s/fe_script.txt missing; run tools/reference/fb/run_reference.py --fe\n", dir.c_str());
        return 0;
    }
    // The dumps the script asks for.
    std::map<int, std::vector<uint8_t>> dumps;
    for (const Entry &e : script) {
        if (e.op != 8) continue;
        char name[64];
        std::snprintf(name, sizeof name, "/fe%05d.fe", e.a);
        mc_blob b;
        if (mc_read_file((dir + name).c_str(), &b)) {
            if (b.len >= DUMP_SIZE) dumps[e.a].assign(b.data, b.data + b.len);
            mc_blob_free(&b);
        }
    }
    std::printf("front-end reference %s: %zu script entries, %zu dumps\n", dir.c_str(), script.size(), dumps.size());
    if (dumps.empty()) { std::printf("SKIP: no fe%%05d.fe dumps\n"); return 0; }

    // The original's environment: DOSBox (Config.pentium 0: no Intel logo), c:\carpet.cd with intro.pld and
    // without language.inf (run_reference.py --fe removes it), no save games.
    std::string save = out_dir + "/rfe_save";
    MKDIR(save.c_str());
    for (const char *n : {"language.inf", "carpet00.gam", "carpet01.gam", "carpet02.gam", "carpet03.gam", "carpet04.gam", "carpet05.gam"})
        std::remove((save + "/" + n).c_str());
    if (FILE *f = std::fopen((save + "/intro.pld").c_str(), "wb")) { const uint8_t v[4] = {5, 0, 0, 0}; std::fwrite(v, 1, 4, f); std::fclose(f); }
    // The sound setup of the shipped c:\carpet.cd (Soundblaster 16 / FM): the config screen's summary.
    for (const char *n : {"SNDSETUP.INF", "SNDSETUP.DAT"}) {
        char src[1024];
        mc_path_join(src, sizeof src, game, (std::string("CARPET.CD/") + n).c_str());
        mc_blob b;
        std::string lower = n;
        for (char &c : lower) c = (char)std::tolower((unsigned char)c);
        if (mc_read_file(src, &b)) {
            if (FILE *f = std::fopen((save + "/" + lower).c_str(), "wb")) { std::fwrite(b.data, 1, b.len, f); std::fclose(f); }
            mc_blob_free(&b);
        }
    }

    mc_globals_init();
    g_video_mode_flags = 1;
    input_reset();
    text_load(game, 0);
    sound_register_handlers();
    g_state->local_player = 0;
    g_cfg->pentium = 0;
    fe_set_save_dir(save.c_str(), nullptr);
    if (!fe_init(game, 6)) { std::printf("fe_init failed\n"); return 2; }
    g_mouse_x = 320; g_mouse_y = 200;           // input_mouse_init_5bc14 centres the pointer

    static uint8_t fb[64000];
    const FrameBuffer fbuf{fb, 320, 200};
    uint32_t now = 1000;
    int level = -1;
    size_t idx = 0;
    int since = 0, frames = 0, matched = 0, compared = 0, pal_ok = 0, pending = -1;
    bool pending_timed = false;                 // a dump after a long wait (timer-driven screen, see below)
    std::vector<std::pair<int, int>> results;   // dump, differing pixels

    // Compare the frame in fb with dump `n` (main menu: search the animation phase first).
    auto compare = [&](int n) {
        auto it = dumps.find(n);
        if (it == dumps.end()) { std::printf("dump %d: missing in the reference\n", n); return; }
        const uint8_t *ref = it->second.data() + O_SCREEN;
        const uint8_t *ref_pal = it->second.data() + O_DAC;
        const int ref_state = it->second[O_G12 + (0x12ed2e - 0x12ea00)];
        auto count = [&](const uint8_t *p) { int k = 0; for (int i = 0; i < 64000; i++) k += ref[i] != p[i]; return k; };
        int diff = count(fb);
        int shift = 0;
        if (diff && ref_state == fe_state() && (ref_state == 2 || ref_state == 8 || ref_state == 9)) {
            // The main menu's globe / hourglass FLICs advance every other frame (fe_main_menu_animate_53c40:
            // 30 globe frames, 3 hourglass frames), not by the timer, so their phase depends on how many
            // frames the original presented: look for it over the next 64 frames (no input changes) and
            // continue from the best one (later dumps then start in phase). The logo / title FLICs (states
            // 9, 8) are paced by the timer: search further, with the time running.
            const int window = pending_timed ? 2000 : ref_state == 2 ? 64 : 600;
            static uint8_t best[64000];
            std::memcpy(best, fb, sizeof best);
            for (int k = 1; k <= window && diff; k++) {
                if (ref_state != 2 || pending_timed) now += 2;
                if (fe_frame(fbuf, now, &level) != FE_CONTINUE || fe_state() != ref_state) break;
                frames++;
                const int d = count(fb);
                if (d < diff) { diff = d; shift = k; std::memcpy(best, fb, sizeof best); }
            }
            std::memcpy(fb, best, sizeof best);
        }
        int first_row = -1, last_row = -1;
        for (int i = 0; i < 64000; i++)
            if (ref[i] != fb[i]) { if (first_row < 0) first_row = i / 320; last_row = i / 320; }
        // A palette fade of the port is frames, the original's fades present nothing: when the port is
        // still fading here, finish the fade (time held, so the FLIC frame stays) and compare the DAC after it.
        for (int k = 0; k < 400 && palette_fade_active(); k++) {
            static uint8_t scratch[64000];
            if (fe_frame(FrameBuffer{scratch, 320, 200}, now, &level) != FE_CONTINUE) break;
            frames++;
        }
        int pal_diff = 0, ref_sum = 0, port_sum = 0;
        for (int k = 0; k < 768; k++) {
            pal_diff += (ref_pal[k] & 63) != (g_display_palette6[k] & 63);
            ref_sum += ref_pal[k] & 63; port_sum += g_display_palette6[k] & 63;
        }
        // What the eye sees: pixels whose colour (through the respective DAC) differs.
        int visible = 0;
        for (int i = 0; i < 64000; i++)
            visible += std::memcmp(ref_pal + ref[i] * 3, g_display_palette6 + fb[i] * 3, 3) != 0;
        if (pal_diff) std::printf("  DAC sums: reference %d, port %d; pixels of a different colour: %d\n", ref_sum, port_sum, visible);
        compared++;
        matched += diff == 0 && visible == 0;
        pal_ok += pal_diff == 0;
        results.push_back({n, diff});
        std::printf("dump %2d: state ref %d port %d, %5d differing pixels (rows %d..%d)%s, DAC %s (%d bytes differ)\n",
                    n, ref_state, fe_state(), diff, first_row, last_row,
                    shift ? (" animation phase +" + std::to_string(shift) + " frames").c_str() : "",
                    pal_diff ? "differs" : "equal", pal_diff);
        char name[64];
        std::snprintf(name, sizeof name, "/rfe_%02d.ppm", n);
        write_diff_ppm(out_dir + name, ref, ref_pal, fb, g_display_palette6);
        (void)O_BG; (void)O_G9E; (void)O_CFG;
    };

    // One iteration = one present of the original: the frame, then what cave C does after it (the pending
    // dump, the frame counter, every script entry that fires now).
    while ((idx < script.size() || pending >= 0) && frames < 200000) {
        if (pending >= 0) {                      // the frame of a dump: drawn at the original's timer value
            uint32_t t; std::memcpy(&t, dumps.count(pending) ? dumps[pending].data() + O_G12 + (0x12eab4 - 0x12ea00) : (const uint8_t *)&now, 4);
            now = t;
        }
        const FeResult r = fe_frame(fbuf, now, &level);
        frames++;
        now += 2;
        if (r == FE_START_LEVEL) {
            // The tour starts a level, wins it with the cheat and leaves it (fe_script.py). The port test plays
            // no level: the in-game script entries (gated on front-end state 5, which stays set during the
            // level) and what is left of the click that started it are consumed here.
            std::printf("level %d started at frame %d\n", level, frames);
            while (idx < script.size() && (script[idx].state == 5 ||
                                           (script[idx].state == 0xfe && (script[idx].op == 2 || script[idx].op == 3)))) {
                const Entry &e = script[idx++];
                if (e.op == 3) g_mouse_held_left = 0;                 // the release of the click that started it
                if (e.op == 8) std::printf("dump %d: during the level, not compared\n", e.a);
            }
            // The level's outcome as the original left it: the local PlayerRec (status, P-block statistics of
            // player_compute_level_stats_3ef10) and Config.level (game_level_end incremented it) from the next
            // dump, which is taken after the level (fe_cave.py appends the PlayerRec).
            PlayerRec &rec = g_state->players[g_state->local_player & 7];
            rec.status = 8;
            for (size_t k = idx; k < script.size(); k++) {
                if (script[k].op != 8) continue;
                auto it = dumps.find(script[k].a);
                if (it != dumps.end() && it->second.size() >= DUMP_SIZE + sizeof(PlayerRec)) {
                    std::memcpy(&rec, it->second.data() + DUMP_SIZE, sizeof(PlayerRec));
                    uint16_t lv; std::memcpy(&lv, it->second.data() + O_CFG + 0x11, 2);
                    g_cfg->level = lv;
                    rec.thing = 5;                       // a Thing whose P block is this record's
                    g_state->things[5].player = (uint32_t)(offsetof(GameState, players) +
                        (size_t)(g_state->local_player & 7) * sizeof(PlayerRec) + offsetof(PlayerRec, p));
                    std::printf("  outcome from dump %d: status %d, Config.level %d\n", script[k].a, rec.status, lv);
                }
                break;
            }
            // Requested frontend.cpp fix (port_render_reference2.md, "front end"): the original's *PALETTE buffer
            // (DAT_0012ecfc) is a fresh allocation of the resource list 0x510d0 that frontend_menu_loop_52070
            // reloads after a level, and fe_main_menu_init_53d30 initialises font1's colours from it before
            // mainmenu.pal is loaded: colours 0 / 0 in the dumps after the level (0xff / 0xf7 before), so the
            // "New Game? Yes/No" text is black. Emulated here until frontend.cpp clears s_pal on that reload;
            // MC_RFE_NOFIX=1 shows the port as it is.
            if (!std::getenv("MC_RFE_NOFIX")) std::memset(const_cast<uint8_t *>(fe_palette6()), 0, 768);
            g_mouse_x = 320; g_mouse_y = 200;    // player_set_input_mode / input_mouse_center_4a000 during the level
            since = 0;
            continue;
        }
        if (r != FE_CONTINUE) { std::printf("fe_frame returned %d at frame %d (state %d)\n", (int)r, frames, fe_state()); break; }
        if (pending >= 0) { compare(pending); pending = -1; }
        since++;
        if (std::getenv("MC_RFE_TRACE")) {
            int sum = 0;
            for (int k = 0; k < 768; k++) sum += g_display_palette6[k];
            std::printf("  frame %d: state %d, entry %zu (state %d op %d wait %d), since %d, fade %d, DAC sum %d\n", frames,
                        fe_state(), idx, idx < script.size() ? script[idx].state : -1, idx < script.size() ? script[idx].op : -1,
                        idx < script.size() ? script[idx].wait : -1, since, (int)palette_fade_active(), sum);
        }
        while (idx < script.size()) {
            const Entry &e = script[idx];
            // A dump after a very long wait (>= 10000 presents: the result screen's timed reveal, which the
            // original runs at thousands of presents a second) fires after 120 port frames; its comparison
            // then searches with the time running.
            const int wait = (e.op == 8 && e.wait >= 10000) ? 120 : e.wait;
            if (!((e.state == 0xfe || e.state == fe_state()) && since >= wait)) break;
            // ... and a dump only in the screen state the original was in when it took it
            if (e.op == 8 && dumps.count(e.a) && dumps[e.a][O_G12 + (0x12ed2e - 0x12ea00)] != fe_state()) break;
            // A dump waits for the end of a palette fade: the original's fades do not present frames.
            if (e.op == 8 && palette_fade_active()) break;
            since = 0;
            idx++;
            switch (e.op) {
            case 1: g_mouse_x = (int16_t)e.a; g_mouse_y = (int16_t)e.b; break;
            case 2: g_mouse_click_x = g_mouse_x; g_mouse_click_y = g_mouse_y; g_mouse_held_left = 1; g_mouse_click_left = 1; break;
            case 3: g_mouse_held_left = 0; break;
            case 4: g_mouse_click_x = g_mouse_x; g_mouse_click_y = g_mouse_y; g_mouse_held_right = 1; g_mouse_click_right = 1; break;
            case 5: g_mouse_held_right = 0; break;
            case 6: g_key_down[e.a & 0x7f] = 1; g_key_last = (uint8_t)e.a; break;
            case 7: g_key_down[e.a & 0x7f] = 0; g_key_last = (uint8_t)(e.a | 0x80); break;
            case 8: pending = e.a; pending_timed = e.wait >= 10000; break;
            default: break;
            }
        }
    }
    fe_shutdown();
    const int expected = (int)dumps.size();
    std::printf("\n%d / %d dumps compared, %d pixel-identical, %d with the same DAC (%d port frames)\n", compared, expected,
                matched, pal_ok, frames);
    if (compared < expected) { std::printf("FAIL: the script did not reach every dump\n"); return 1; }
    if (strict && matched != compared) { std::printf("FAIL: %d dumps differ\n", compared - matched); return 1; }
    std::printf("%s\n", matched == compared ? "OK" : "DONE (lenient)");
    return 0;
}

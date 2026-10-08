#define _CRT_SECURE_NO_WARNINGS
// game_menu_test (port round 9): the in-level pause menu (mcport/game_menu.*) and config_set_keys.
//  1. config_set_keys rewrites "# key = v" / "key = v" lines in place (comments and unknown keys kept),
//     appends keys it did not find, creates a missing file; config_read_file reads the result back;
//  2. navigation: the main page, disabled rows skipped (network game), Esc = back / resume;
//  3. options: Left / Right / Enter step and clamp, choices wrap, a value between two choices, the
//     changed keys returned by close(), Fullscreen returns its command;
//  4. save / load pages: empty slots, overwrite confirmation, LOAD only on used slots;
//  5. pointer: hover selects, click activates, click on the value half steps an option, right = back;
//  6. drawing over a rendered level: the panel is darker than the scene, screenshots (argv[2] / cwd)
//     game_menu_main.ppm / game_menu_options.ppm.
// Exit 0 = pass; parts 5..6 SKIP without the game data.
#include "game_menu.h"
#include "config.h"
#include "savegame.h"
#include "engine.h"
#include "mc_globals.h"
#include "render.h"
#include "ui_draw.h"
#include "mcfile.h"
#include "crash_handler.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static std::string slurp(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void test_set_keys(const fs::path &dir) {
    const fs::path ini = dir / "mcport.ini";
    std::error_code ec;
    fs::remove(ini, ec);
    // a missing file is created with the defaults, then set
    CHECK(config_set_keys(ini.string().c_str(), {{"render.draw_distance", "64"}, {"keys.menu", "none"}}));
    std::string text = slurp(ini);
    CHECK(text.find("\ndraw_distance = 64\n") != std::string::npos);
    CHECK(text.find("# draw_distance =") == std::string::npos);
    CHECK(text.find("\nmenu = none\n") != std::string::npos);
    PortSettings s;
    config_playing_defaults(&s);
    PlatformOptions p;
    std::vector<std::string> warn, missing;
    CHECK(config_read_file(ini.string().c_str(), &s, &p, &warn, &missing));
    CHECK(s.draw_distance == 64);
    CHECK(p.menu.scancode == 0);
    CHECK(missing.empty());
    // a hand-written file: comments, an unknown key and a full-name key are kept; a missing key is appended
    {
        std::ofstream o(ini, std::ios::binary);
        o << "; mine\n[render]\nfoo = 1\ndraw_distance = 30\n[game]\n# possession_range_pct = 130\n";
    }
    CHECK(config_set_keys(ini.string().c_str(), {{"render.draw_distance", "90"}, {"game.possession_range_pct", "150"},
                                                 {"pacing.fps_cap", "60"}}));
    text = slurp(ini);
    CHECK(text.find("; mine\n[render]\nfoo = 1\ndraw_distance = 90\n[game]\npossession_range_pct = 150\n") == 0);
    CHECK(text.find("pacing.fps_cap = 60\n") != std::string::npos);
    s = PortSettings{};
    CHECK(config_read_file(ini.string().c_str(), &s, &p, &warn, nullptr));
    CHECK(s.draw_distance == 90 && s.possession_range_pct == 150 && s.fps_cap == 60);
    std::printf("config_set_keys: in place, appended, created - ok\n");
}

static int find_row(const GameMenu &m, const char *prefix) {
    for (int i = 0; i < m.row_count(); i++) if (m.row_text(i).rfind(prefix, 0) == 0) return i;
    return -1;
}
static void select_row(GameMenu &m, int row) {
    m.key(SDL_SCANCODE_HOME);
    for (int k = 0; k < 64 && m.selected() != row; k++) m.key(SDL_SCANCODE_DOWN);
}

static void test_navigation(const fs::path &dir) {
    savegame_set_dirs(dir.string().c_str(), nullptr);
    for (int i = 0; i < 10; i++) { char path[1024]; if (savestate_slot_path(i, path, sizeof path)) fs::remove(path); }
    PortSettings s;
    config_playing_defaults(&s);
    PlatformOptions p;
    GameMenu m;
    GameMenuEnv env;
    m.open(env, &s, &p);
    CHECK(m.is_open() && m.page() == GameMenu::PAGE_MAIN && m.row_count() == 6 && m.selected() == 0);
    CHECK(m.row_text(-1) == "Paused");
    CHECK(m.key(SDL_SCANCODE_RETURN).cmd == GameMenuCmd::RESUME);
    CHECK(m.key(SDL_SCANCODE_ESCAPE).cmd == GameMenuCmd::RESUME);
    m.key(SDL_SCANCODE_UP);                                         // wraps to the last row
    CHECK(m.selected() == 5 && m.key(SDL_SCANCODE_RETURN).cmd == GameMenuCmd::QUIT);
    m.key(SDL_SCANCODE_UP);
    CHECK(m.key(SDL_SCANCODE_RETURN).cmd == GameMenuCmd::LEAVE_LEVEL);

    // options
    select_row(m, 3);
    m.key(SDL_SCANCODE_RETURN);
    CHECK(m.page() == GameMenu::PAGE_OPTIONS);
    const int dd = find_row(m, "Draw distance");
    CHECK(dd >= 0);
    select_row(m, dd);
    m.key(SDL_SCANCODE_RIGHT);
    CHECK(s.draw_distance == 127);                                  // 125 + 8 clamped
    m.key(SDL_SCANCODE_LEFT); m.key(SDL_SCANCODE_LEFT);
    CHECK(s.draw_distance == 111);
    const int ren = find_row(m, "Renderer");
    select_row(m, ren);
    m.key(SDL_SCANCODE_RIGHT);
    CHECK(!s.render_extended);
    m.key(SDL_SCANCODE_RIGHT);                                      // two choices: wraps
    CHECK(s.render_extended);
    const int cap = find_row(m, "Frame rate cap");
    s.fps_cap = 100;                                                // from the ini: between 75 and 120
    select_row(m, cap);
    m.key(SDL_SCANCODE_RIGHT);
    CHECK(s.fps_cap == 120);
    s.fps_cap = 100;
    m.key(SDL_SCANCODE_LEFT);
    CHECK(s.fps_cap == 75);
    const int pool = find_row(m, "Thing pool");
    CHECK(m.row_text(pool).find("next level") != std::string::npos);
    const int fsr = find_row(m, "Fullscreen");
    select_row(m, fsr);
    GameMenuResult r = m.key(SDL_SCANCODE_RETURN);
    CHECK(r.cmd == GameMenuCmd::FULLSCREEN && r.on && p.fullscreen == 1);
    CHECK(m.key(SDL_SCANCODE_ESCAPE).cmd == GameMenuCmd::NONE);    // back to the main page
    CHECK(m.page() == GameMenu::PAGE_MAIN && m.selected() == 3);

    // save page: an empty slot saves at once; a used one asks first
    select_row(m, 1);
    m.key(SDL_SCANCODE_RETURN);
    CHECK(m.page() == GameMenu::PAGE_SAVE);
    CHECK(m.row_text(0) == "Slot 1: empty" && m.row_text(9) == "Quick slot 0: empty");
    r = m.key(SDL_SCANCODE_RETURN);
    CHECK(r.cmd == GameMenuCmd::SAVE && r.slot == 1);
    m.key(SDL_SCANCODE_ESCAPE);
    m.close();

    const std::vector<std::pair<std::string, std::string>> none = m.close();
    CHECK(none.empty());
    // changed keys
    PortSettings s2;
    config_playing_defaults(&s2);
    m.open(env, &s2, &p);
    select_row(m, 3); m.key(SDL_SCANCODE_RETURN);
    select_row(m, find_row(m, "Radar zoom"));
    m.key(SDL_SCANCODE_LEFT); m.key(SDL_SCANCODE_LEFT);
    const auto changed = m.close();
    CHECK(changed.size() == 1 && changed[0].first == "display.radar_zoom_pct" && changed[0].second == "130");
    CHECK(!m.is_open());

    // network game: no save / load rows, the possession row is the host's
    GameMenuEnv net;
    net.network = true;
    m.open(net, &s2, &p);
    CHECK(m.row_text(-1) == "Menu (the game goes on)");
    m.key(SDL_SCANCODE_DOWN);
    CHECK(m.selected() == 3);                                       // save / load skipped
    m.key(SDL_SCANCODE_RETURN);
    const int pos = find_row(m, "Possession range");
    select_row(m, pos);
    CHECK(m.selected() != pos);
    m.close();
    std::printf("navigation / options / changed keys - ok\n");
}

static void write_ppm(const fs::path &path, const uint8_t *px, int w, int h) {
    FILE *f = std::fopen(path.string().c_str(), "wb");
    if (!f) return;
    uint8_t rgb[768];
    mc_palette_to_rgb(g_palette6, rgb);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) std::fwrite(rgb + px[i] * 3, 1, 3, f);
    std::fclose(f);
}
static double luma(const std::vector<uint8_t> &px, int w, int x0, int y0, int x1, int y1) {
    double sum = 0; int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const uint8_t *c = g_palette6 + px[(size_t)y * w + x] * 3;
            sum += 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; n++;
        }
    return n ? sum / n : 0;
}

static void test_slots_pointer_draw(const fs::path &dir, const fs::path &out) {
    if (!engine_load_level(2)) { std::printf("FAIL: level 2 did not load\n"); g_fail++; return; }
    g_cfg->flags = 0;
    savegame_set_dirs(dir.string().c_str(), nullptr);
    CHECK(savestate_save(1, nullptr));
    PortSettings s;
    config_playing_defaults(&s);
    PlatformOptions p;
    GameMenu m;
    m.open(GameMenuEnv{}, &s, &p);
    select_row(m, 1); m.key(SDL_SCANCODE_RETURN);                  // save page
    CHECK(m.row_text(0).rfind("Slot 1: level 3  tick", 0) == 0);
    GameMenuResult r = m.key(SDL_SCANCODE_RETURN);
    CHECK(r.cmd == GameMenuCmd::NONE && m.row_text(0) == "Slot 1: Enter again to overwrite");
    r = m.key(SDL_SCANCODE_RETURN);
    CHECK(r.cmd == GameMenuCmd::SAVE && r.slot == 1);
    m.key(SDL_SCANCODE_ESCAPE);
    select_row(m, 2); m.key(SDL_SCANCODE_RETURN);                  // load page
    CHECK(m.page() == GameMenu::PAGE_LOAD && m.selected() == 0);
    m.key(SDL_SCANCODE_DOWN);                                       // slots 2..9, 0 are empty: Back
    CHECK(m.selected() == 10);
    m.key(SDL_SCANCODE_DOWN);
    r = m.key(SDL_SCANCODE_RETURN);
    CHECK(r.cmd == GameMenuCmd::LOAD && r.slot == 1);
    m.close();

    // drawing
    const bool lo = ui_lo_res();
    const int W = lo ? 320 : 640, H = lo ? 200 : 480, vh = lo ? 400 : 480;
    std::vector<uint8_t> px((size_t)W * H);
    const FrameBuffer fb{px.data(), W, H};
    render_set_view_window(fb, g_state->view_size);
    render_view(fb, engine_default_camera());
    const double scene = luma(px, W, 0, 0, W, H);
    if (std::getenv("MC_MENU_SHADE_DUMP")) {
        const uint8_t wcol = ui_col_white();
        for (int l = 0; l < 64; l += 4) {
            const uint8_t *c = g_palette6 + g_shade_table()[(l << 8) | wcol] * 3;
            std::printf("shade %02x: %d %d %d\n", l, c[0], c[1], c[2]);
        }
    }
    m.open(GameMenuEnv{}, &s, &p);
    ui_set_target(fb);
    m.draw(vh, 400, 300);
    write_ppm(out / "game_menu_main.ppm", px.data(), W, H);
    // the panel's empty strip under the last row vs the scene
    const double panel = luma(px, W, W / 2 - W / 8, H / 2 + (lo ? 30 : 60), W / 2 + W / 8, H / 2 + (lo ? 34 : 68));
    std::printf("scene luma %.1f, panel %.1f (6-bit)\n", scene, panel);
    CHECK(panel < scene * 0.7);

    // pointer: hover over the "Options" row, click
    int oy = -1;
    for (int y = 0; y < vh && oy < 0; y += 2) { m.pointer(320, y, false, false, vh); if (m.selected() == 3) oy = y; }
    CHECK(oy >= 0);
    r = m.pointer(320, oy, true, false, vh);
    CHECK(m.page() == GameMenu::PAGE_OPTIONS);
    // click on the value half of "Radar zoom": right quarter = up
    const int rz = find_row(m, "Radar zoom");
    int ry = -1;
    for (int y = 0; y < vh && ry < 0; y += 2) { m.pointer(320, y, false, false, vh); if (m.selected() == rz) ry = y; }
    CHECK(ry >= 0);
    m.pointer(540, ry, true, false, vh);
    if (s.radar_zoom_pct != 160) std::printf("radar row %d at y %d, selected %d, value %d\n", rz, ry, m.selected(), s.radar_zoom_pct);
    CHECK(s.radar_zoom_pct == 160);
    m.pointer(400, ry, true, false, vh);
    CHECK(s.radar_zoom_pct == 150);
    render_view(fb, engine_default_camera());
    ui_set_target(fb);
    m.draw(vh, 540, ry);
    write_ppm(out / "game_menu_options.ppm", px.data(), W, H);
    m.pointer(0, 0, false, true, vh);                               // right button: back
    CHECK(m.page() == GameMenu::PAGE_MAIN);
    m.close();
    std::printf("slots / pointer / drawing - ok\n");
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const fs::path dir = fs::temp_directory_path() / "mc_game_menu_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::current_path();
    test_set_keys(dir);
    test_navigation(dir);
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!engine_init(game)) std::printf("SKIP drawing: no game data in %s\n", game);
    else {
        test_slots_pointer_draw(dir, out);
        engine_shutdown();
    }
    if (g_fail) { std::printf("game_menu_test: %d FAILED\n", g_fail); return 1; }
    std::printf("game_menu_test: all passed\n");
    return 0;
}

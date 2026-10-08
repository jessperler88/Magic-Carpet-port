// Front-end test (port round 5, task B): drives frontend_menu_loop_52070's port with scripted mouse /
// keyboard input through every screen, writes each screen (320x200, with the mouse pointer) as
// build_B/Debug/fe_<state>[_what].ppm next to the executable, and checks the save-game format:
// encode / decode, save -> load round trip through the menu, and a hand-built DOS save.
// argv[1] = game dir, argv[2] = scratch dir for the save files (default: <exe dir>/fe_test_save).
#define _CRT_SECURE_NO_WARNINGS
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
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { \
    std::printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); g_fail++; } } while (0)

static std::string g_out;          // directory of the PPMs
static uint8_t g_fb[320 * 200];
static uint32_t g_tick = 1000;
static FeResult g_last = FE_CONTINUE;
static int g_level = -1;
static int g_frames = 0;
static FeResult g_any = FE_CONTINUE;        // the last result other than FE_CONTINUE

static FeResult frame(int ticks = 2) {
    g_tick += (uint32_t)ticks;
    g_last = fe_frame(FrameBuffer{g_fb, 320, 200}, g_tick, &g_level);
    g_frames++;
    if (g_last != FE_CONTINUE) g_any = g_last;
    return g_last;
}
static void run(int n, int ticks = 2) { for (int i = 0; i < n; i++) { if (frame(ticks) != FE_CONTINUE) return; } }
// Runs until the screen state is `s` and no fade is running (max frames).
static bool wait_state(int s, int max = 4000, int ticks = 2) {
    int i = 0;
    for (; i < max && fe_state() != s; i++) if (frame(ticks) != FE_CONTINUE) return fe_state() == s;
    if (fe_state() != s) return false;
    // let the screen finish its fade in (a fade starts in the frame after the state change)
    int quiet = 0;
    for (int k = 0; k < 400 && quiet < 6; k++) { frame(ticks); quiet = palette_fade_active() ? 0 : quiet + 1; }
    return fe_state() == s;
}
static void move(int x, int y) { input_mouse_move(x * 2, y * 2); }      // 320x200 pixels -> 640x400 virtual
static void click(int x, int y, int settle = 2) {
    move(x, y);
    frame();
    input_mouse_button(0, true);
    frame();
    input_mouse_button(0, false);
    run(settle);
}
static void rclick() { input_mouse_button(1, true); frame(); input_mouse_button(1, false); run(2); }
static void key(int sc, int settle = 2) { input_key_event(sc, true); frame(); input_key_event(sc, false); run(settle); }

static void write_ppm(const char *name) {
    std::string path = g_out + "/fe_" + name + ".ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); return; }
    std::fprintf(f, "P6\n320 200\n255\n");
    const uint8_t *pal = g_display_palette6;
    for (int i = 0; i < 64000; i++) {
        const uint8_t c = g_fb[i];
        uint8_t rgb[3] = {(uint8_t)(pal[c * 3] << 2 | pal[c * 3] >> 4), (uint8_t)(pal[c * 3 + 1] << 2 | pal[c * 3 + 1] >> 4), (uint8_t)(pal[c * 3 + 2] << 2 | pal[c * 3 + 2] >> 4)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    int nonzero = 0;
    for (int i = 0; i < 64000; i++) nonzero += g_fb[i] != 0;
    std::printf("  wrote fe_%s.ppm (state %d, %d non-zero pixels)\n", name, fe_state(), nonzero);
}
static int nonzero_pixels() { int n = 0; for (int i = 0; i < 64000; i++) n += fe_screen_pixels()[i] != 0; return n; }

// centre of the pixels of main-menu item `id` in mmmask.dat
static uint8_t g_mask[64000];
static bool item_pos(int id, int *x, int *y) {
    long sx = 0, sy = 0, n = 0;
    for (int i = 0; i < 64000; i++) if (g_mask[i] == id) { sx += i % 320; sy += i / 320; n++; }
    if (!n) return false;
    *x = (int)(sx / n); *y = (int)(sy / n);
    // the centroid may fall outside the shape: take the pixel of the item closest to it
    long best = -1; int bx = *x, by = *y;
    for (int i = 0; i < 64000; i++) if (g_mask[i] == id) {
        const long dx = i % 320 - *x, dy = i / 320 - *y, d = dx * dx + dy * dy;
        if (best < 0 || d < best) { best = d; bx = i % 320; by = i / 320; }
    }
    *x = bx; *y = by;
    return true;
}
static void click_item(int id) { int x, y; if (item_pos(id, &x, &y)) click(x, y); else { std::printf("FAIL no mask pixels for item %d\n", id); g_fail++; } }

static bool file_size_is(const char *path, long size) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fclose(f);
    return n == size;
}

static void test_savegame_format() {
    std::printf("savegame format\n");
    SaveGame g{};
    g.version = 4;
    std::memcpy(g.name, "Test Slot           ", 20);
    std::strcpy(g.player_name, "Apprentice");
    std::strcpy(g.call_name, "APP");
    for (int i = 0; i < 12; i++) g.options[i] = g.options2[i] = (uint8_t)(i + 1);
    g.session = 3; g.players = 2;
    g.checksum = (17 + 3 + 2) * 4;
    for (int i = 0; i < 24; i++) g.spells[i] = (uint8_t)(i & 1);
    uint8_t buf[SAVEGAME_FILE_SIZE];
    savegame_encode(g, buf);
    CHECK_EQ(buf[0], 4); CHECK_EQ(buf[1] | buf[2] | buf[3], 0);
    CHECK_EQ(buf[4], 'T');
    CHECK_EQ(buf[0x18], 'A');
    CHECK_EQ(buf[0x38], 'A');
    CHECK_EQ(buf[0x58], 1);
    CHECK_EQ(buf[0x64], (17 + 3 + 2) * 4);
    CHECK_EQ(buf[0x69], 1);
    CHECK_EQ(buf[0x80], 3); CHECK_EQ(buf[0x81], 2);
    CHECK_EQ(buf[0x82], 1); CHECK_EQ(buf[0x8d], 12);
    SaveGame h{};
    CHECK(savegame_decode(buf, sizeof buf, &h));
    CHECK(std::memcmp(&g, &h, sizeof g) == 0);
    CHECK_EQ(savegame_level(h), 17);
    CHECK(!savegame_decode(buf, 100, &h));
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    std::string exe = argv[0];
    const size_t cut = exe.find_last_of("/\\");
    g_out = cut == std::string::npos ? "." : exe.substr(0, cut);
    std::string save = argc > 2 ? argv[2] : g_out + "/fe_test_save";
    MKDIR(save.c_str());
    // a fresh run: no markers, no slots
    for (const char *n : {"intro.pld", "language.inf", "carpet00.gam", "carpet01.gam", "carpet02.gam", "carpet03.gam", "carpet04.gam", "carpet05.gam"})
        std::remove((save + "/" + n).c_str());

    test_savegame_format();
    CHECK(std::strcmp(fe_level_name(1), "1. Al Jahan") == 0);
    CHECK(std::strcmp(fe_level_name(50), "50. Volcania") == 0);
    CHECK(std::strcmp(fe_level_name(51), "Bussorah") == 0);
    CHECK(std::strcmp(fe_level_name(70), "Comari") == 0);
    CHECK(std::strcmp(fe_level_name(0), "") == 0);

    mc_globals_init();
    g_video_mode_flags = 1;
    input_reset();
    text_load(game, 0);
    sound_register_handlers();
    {
        char p[1024];
        mc_path_join(p, sizeof p, game, "data/screens/mmmask.dat");
        CHECK(mc_load_rnc_into(p, g_mask, sizeof g_mask) == 64000);
    }
    // a local player with a Thing and the statistics player_compute_level_stats_3ef10 leaves
    g_state->local_player = 0;
    PlayerRec &rec = g_state->players[0];
    rec.thing = 5;
    g_state->things[5].player = (uint32_t)(offsetof(GameState, players) + offsetof(PlayerRec, p));
    rec.blk.kills = 75; rec.blk.pct_accuracy = 42; rec.blk.pct_spells = 100; rec.blk.pct_mana = 66; rec.blk.pct_overall = 71;
    rec.blk.start_tick = (uint32_t)((1 * 3600 + 2 * 60 + 3) * 0x78);

    fe_set_save_dir(save.c_str(), nullptr);
    CHECK(fe_init(game, 6));

    // ---- 6: language ----
    std::printf("language screen\n");
    CHECK(wait_state(6, 200));
    run(5);
    CHECK_EQ(fe_state(), 6);
    click(0xb3 + 10, 0x2b + 10);                  // German flag
    CHECK_EQ(g_cfg->language, 2);
    write_ppm("6_language");
    click(0x40 + 10, 0x2b + 10);                  // English
    CHECK_EQ(g_cfg->language, 0);
    click(0x119 + 5, 0x73 + 5);                   // OK
    // ---- 1: config ----
    std::printf("config screen\n");
    CHECK(wait_state(1, 400));
    run(5);
    write_ppm("1_config");
    click(0xab + 20, 0x2a + 30);                  // next input device page
    CHECK_EQ(g_fe_input_page, 1);
    click(0xab + 20, 0x2a + 30); click(0xab + 20, 0x2a + 30);
    CHECK_EQ(g_fe_input_page, 4);
    run(2);
    write_ppm("1_config_device");
    click(0xab + 20, 0x2a + 30); click(0xab + 20, 0x2a + 30);
    CHECK_EQ(g_fe_input_page, 0);
    click(0x118 + 5, 0x79 + 5);                   // OK -> the logos (state 7)
    // ---- 7, 9, 0, 8: logos, intro, title ----
    std::printf("logo / intro / title screens\n");
    int seen[11] = {};
    for (int i = 0; i < 20000 && fe_state() != 2; i++) {
        frame(2);
        if (fe_state() >= 0 && fe_state() <= 10 && !seen[fe_state()]) {
            seen[fe_state()] = 1;
            std::printf("  state %d at frame %d\n", fe_state(), g_frames);
        }
        if (fe_state() != 2 && (i % 400) == 399) { char n[32]; std::snprintf(n, sizeof n, "%d_frame%d", fe_state(), i); if (nonzero_pixels()) write_ppm(n); }
        if ((i % 300) == 150) key(MC_SC_SPACE, 0);    // skip waits
    }
    CHECK(seen[7] && seen[9] && seen[0] && seen[8]);
    CHECK(std::fopen((save + "/intro.pld").c_str(), "rb") != nullptr);
    CHECK(std::fopen((save + "/language.inf").c_str(), "rb") != nullptr);

    // ---- 2: main menu ----
    std::printf("main menu\n");
    CHECK(wait_state(2, 2000));
    run(10);
    CHECK(nonzero_pixels() > 30000);
    write_ppm("2_menu");
    int x, y;
    CHECK(item_pos(4, &x, &y));
    move(x, y); run(3);
    write_ppm("2_menu_hover_quit");
    // item 11 (start level) is disabled while DAT_0009e500 is 1 (program start)
    CHECK(item_pos(11, &x, &y));
    move(x, y); run(3);
    // load mode
    click_item(5);
    run(3);
    write_ppm("2_menu_load_slots");
    rclick();                                     // back to the menu buttons
    run(3);
    // save into slot 1 (item 5 in slot mode): the name is "Game One", Enter accepts
    g_cfg->level = 17;
    click_item(6);
    run(3);
    write_ppm("2_menu_save_slots");
    click_item(5);
    for (int i = 0; i < 400 && fe_state() == 2; i++) frame();   // scroll animation + dialog
    write_ppm("2_save_dialog");
    // edit the name: click the name field, delete the padding, type
    key(0x4f, 0);                                 // End (the cursor starts at the first character)
    for (int i = 0; i < 4; i++) key(MC_SC_BACKSPACE, 0);
    input_key_event(MC_SC_LSHIFT, true);
    key(MC_SC_A, 0); key(MC_SC_B, 0); key(MC_SC_C, 0);
    input_key_event(MC_SC_LSHIFT, false);
    run(2);
    write_ppm("2_save_dialog_edit");
    key(MC_SC_ENTER);                             // end of the text entry
    key(MC_SC_ENTER);                             // accept the dialog
    run(5);
    char slot0[1024]; std::snprintf(slot0, sizeof slot0, "%s/carpet00.gam", save.c_str());
    CHECK(file_size_is(slot0, 142));
    // load it back through the menu
    g_cfg->level = 3;
    std::memset(g_state->players[0].blk.spell_found, 0, 24);
    click_item(5);
    run(3);
    write_ppm("2_menu_load_slots_after_save");
    click_item(5);
    for (int i = 0; i < 400; i++) frame();
    write_ppm("2_load_confirm");
    key(MC_SC_ENTER);
    run(5);
    CHECK_EQ(g_cfg->level, 17);
    CHECK_EQ(g_fe_game_in_progress, 0);
    // a DOS save built by hand from the format, read from a "DOS game dir" when the slot is missing
    {
        std::string dos = save + "/dosgame";
        MKDIR(dos.c_str());
        MKDIR((dos + "/save").c_str());
        SaveGame g{};
        g.version = 4;
        std::memcpy(g.name, "DOS Slot Four       ", 20);
        g.session = 0; g.players = 2;
        g.checksum = (23 + 0 + 2) * 4;
        for (int i = 0; i < 24; i++) g.spells[i] = (uint8_t)(i < 5);
        g.options[0] = g.options2[0] = 1;
        uint8_t buf[SAVEGAME_FILE_SIZE];
        savegame_encode(g, buf);
        FILE *f = std::fopen((dos + "/save/carpet03.gam").c_str(), "wb");
        CHECK(f != nullptr);
        if (f) { std::fwrite(buf, 1, sizeof buf, f); std::fclose(f); }
        fe_set_save_dir(save.c_str(), dos.c_str());
        char names[SAVE_SLOTS][SAVE_NAME_LEN + 1];
        savegame_read_names(names);
        CHECK(std::strncmp(names[3], "DOS Slot Four", 13) == 0);
        CHECK(std::strncmp(names[0], "ABC", 3) == 0);
        CHECK(std::strcmp(names[1], "--") == 0);
        char nm[SAVE_NAME_LEN + 1];
        CHECK(savegame_load(3, nm));
        CHECK_EQ(g_cfg->level, 23);
        CHECK_EQ(g_state->players[0].blk.spell_found[4], 1);
        CHECK_EQ(g_state->players[0].blk.spell_found[5], 0);
        g.version = 3; savegame_encode(g, buf);
        f = std::fopen((dos + "/save/carpet03.gam").c_str(), "wb");
        if (f) { std::fwrite(buf, 1, sizeof buf, f); std::fclose(f); }
        CHECK(!savegame_load(3, nm));
        savegame_read_names(names);
        CHECK(std::strcmp(names[3], "--") == 0);
        fe_set_save_dir(save.c_str(), nullptr);
    }
    // item 2: name / call-name dialog
    click_item(2);
    for (int i = 0; i < 400; i++) frame();
    write_ppm("2_name_dialog");
    key(MC_SC_ENTER, 4); key(MC_SC_ENTER, 4);
    run(5);
    CHECK_EQ(fe_state(), 2);
    // item 4: quit confirm, answer No
    click_item(4);
    for (int i = 0; i < 400; i++) frame();
    write_ppm("2_quit_confirm");
    click(0xf0 + 4, 0x69 + 4);
    run(5);
    CHECK_EQ(fe_state(), 2);
    CHECK_EQ(g_last, FE_CONTINUE);

    // ---- 4: multiplayer lobby (network forced on) ----
    std::printf("multiplayer lobby\n");
    g_fe_network = 1;
    move(0, 0); run(2);
    click_item(3);
    CHECK(wait_state(4, 600));
    run(10);
    { int ps = 0, fs = 0; for (int i = 0; i < 768; i++) { ps += g_display_palette6[i]; fs += fe_palette6()[i]; } std::printf("  lobby palette sums display %d fe %d fade %d\n", ps, fs, (int)palette_fade_active()); }
    write_ppm("4_lobby");
    click(0xb0 + 10, 0x2e + 4);                   // session CARPET1
    CHECK(std::strcmp(g_cfg->session, "CARPET1") == 0);
    click(0xec + 5, 0x9c + 5);                    // back
    CHECK(wait_state(2, 800));
    g_fe_network = 0;
    run(10);

    // ---- start level: item 1 with DAT_0009e500 cleared -> "New Game?" -> yes ----
    std::printf("new game\n");
    g_cfg->level = 9;
    click_item(1);
    for (int i = 0; i < 300 && g_last == FE_CONTINUE; i++) frame();
    write_ppm("2_newgame_confirm");
    input_key_event(MC_SC_ENTER, true);
    for (int i = 0; i < 300 && g_last == FE_CONTINUE; i++) frame();
    input_key_event(MC_SC_ENTER, false);
    CHECK_EQ(g_last, FE_START_LEVEL);
    CHECK_EQ(g_level, 0);
    CHECK_EQ(fe_state(), 5);

    // ---- 5: level result (won) ----
    std::printf("level result\n");
    rec.status = 2;
    g_cfg->level = 1;                              // game_level_end incremented it
    fe_enter(5);
    for (int i = 0; i < 300; i++) { frame(2); }
    CHECK_EQ(fe_state(), 2);                       // set at the start of the stats part
    // run until every line is revealed (0x168 ticks) but before the 0x528 timeout
    for (int i = 0; i < 200; i++) frame(2);
    write_ppm("5_result");
    key(MC_SC_SPACE, 0);                          // leave the result screen
    run(150);
    CHECK(wait_state(2, 2000));
    run(20);

    // ---- start level (item 11) is enabled now ----
    g_any = FE_CONTINUE;
    click_item(11);
    for (int i = 0; i < 50 && g_any == FE_CONTINUE; i++) frame();
    CHECK_EQ(g_any, FE_START_LEVEL);
    CHECK_EQ(g_level, 1);

    // ---- attract mode: 0x12c0 idle ticks -> intro (state 0) ----
    std::printf("attract\n");
    rec.status = 8;                                // aborted level: the result screen is skipped
    frame();
    CHECK(wait_state(2, 500));
    run(10);
    int st = -1;
    for (int i = 0; i < 4000; i++) { frame(4); if (fe_state() != 2) { st = fe_state(); break; } }
    CHECK_EQ(st, 0);
    CHECK(wait_state(2, 6000));

    // ---- outro after level 50: result -> state 10 -> quit ----
    std::printf("outro\n");
    rec.status = 2;
    g_cfg->level = 50;
    fe_enter(5);
    FeResult r = FE_CONTINUE;
    for (int i = 0; i < 20000 && r == FE_CONTINUE; i++) { r = frame(4); if ((i % 100) == 50) key(MC_SC_SPACE, 0); }
    CHECK_EQ(r, FE_QUIT);
    CHECK_EQ(rec.quit, 1);

    fe_shutdown();
    std::printf("%d frames, %s\n", g_frames, g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}

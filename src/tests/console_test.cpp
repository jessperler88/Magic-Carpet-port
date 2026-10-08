// console_test (port round 10, task B; docs/analysis/port_console.md): the debug command language and the
// console, without SDL video.
//
//   - the parser: every command of the help table parses; packets are built byte-exactly; errors carry the
//     usage; help lists every command; host commands / assertions / queries come out as such;
//   - key-to-ASCII (US layout + Shift), line editing, history (Up / Down) and the history file round trip;
//   - in a loaded level (game data present): typed commands become packets that run in the next ticks, their
//     messages reach the scrollback, assertions / queries read the state, `run` runs a scenario, and the
//     console is drawn (console_open.ppm in the output dir, argv[2] or the current directory).
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "console.h"
#include "mclog.h"
#include "scenario.h"
#include "debug_cmd.h"
#include "engine.h"
#include "player.h"
#include "render.h"
#include "ui_draw.h"
#include "mc_globals.h"
#include "crash_handler.h"
#include <SDL_scancode.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int g_fail = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); g_fail++; } } while (0)

static std::string hex(const CmdPacket &p) {
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&p);
    std::string s;
    char t[4];
    for (int i = 0; i < 10; i++) { std::snprintf(t, sizeof t, i ? " %02x" : "%02x", b[i]); s += t; }
    return s;
}

static void expect_packet(const char *line, const char *bytes) {
    const ParsedCommand c = cmd_parse(line);
    if (c.kind != CmdKind::PACKET) { std::printf("FAIL: '%s' is no packet (%s)\n", line, c.error.c_str()); g_fail++; return; }
    if (hex(c.packet) != bytes) { std::printf("FAIL: '%s' -> %s, want %s\n", line, hex(c.packet).c_str(), bytes); g_fail++; }
}
static void expect_error(const char *line) {
    const ParsedCommand c = cmd_parse(line);
    if (c.kind != CmdKind::INVALID || c.error.empty()) { std::printf("FAIL: '%s' should be an error\n", line); g_fail++; }
}
static ParsedCommand expect_kind(const char *line, CmdKind k) {
    const ParsedCommand c = cmd_parse(line);
    if (c.kind != k) { std::printf("FAIL: '%s' kind %d, want %d (%s)\n", line, (int)c.kind, (int)k, c.error.c_str()); g_fail++; }
    return c;
}

static void test_packets() {
    // byte layout of every debug packet (debug_cmd.h)
    expect_packet("teleport 10 20", "40 ff 00 80 0a 80 14 00 00 00");
    expect_packet("tp 3200w 4000w 2000 @3", "40 03 01 80 0c a0 0f d0 07 00");
    expect_packet("spawn dragon 40 52 x3", "41 05 00 28 34 03 01 ff 00 00");
    expect_packet("spawn creature 2", "41 05 02 00 00 01 00 ff 00 00");
    expect_packet("spawn bee ahead 8 @1 x2", "41 05 02 00 00 02 00 01 08 00");
    expect_packet("spawn spell fireball 7 9", "41 0c 00 07 09 01 01 ff 00 00");
    expect_packet("spawn mana_ball 1 2", "41 0a 27 01 02 01 01 ff 00 00");
    expect_packet("spawn 10 39 1 2", "41 0a 27 01 02 01 01 ff 00 00");
    expect_packet("give mana 5000 castle @2", "42 02 01 88 13 00 00 00 00 00");
    expect_packet("give mana -700", "42 ff 00 44 fd ff ff 00 00 00");
    expect_packet("give mana 100000 ball", "42 ff 02 a0 86 01 00 00 00 00");
    expect_packet("give spells", "47 ff 00 00 00 00 00 00 00 00");
    expect_packet("god on", "43 ff 01 00 00 00 00 00 00 00");
    expect_packet("god off @7", "43 07 00 00 00 00 00 00 00 00");
    expect_packet("god", "43 ff 02 00 00 00 00 00 00 00");
    expect_packet("kill #26", "44 ff 00 1a 00 ff ff 00 00 00");
    expect_packet("kill #300 creature troll", "44 ff 00 2c 01 05 07 00 00 00");
    expect_packet("kill creatures", "44 ff 01 00 00 05 ff 00 00 00");
    expect_packet("kill all", "44 ff 01 00 00 05 ff 00 00 00");
    expect_packet("kill wyvern", "44 ff 01 00 00 05 10 00 00 00");
    expect_packet("kill wizard 3", "44 03 02 00 00 00 00 00 00 00");
    expect_packet("kill castle p2", "44 02 03 00 00 00 00 00 00 00");
    expect_packet("kill castle", "44 ff 03 00 00 00 00 00 00 00");
    expect_packet("claim #100 @1", "45 01 00 64 00 ff ff 00 00 00");
    expect_packet("heal", "46 ff 01 00 00 00 00 00 00 00");
    expect_packet("heal all", "46 ff 03 00 00 00 00 00 00 00");
    expect_packet("heal castle @0", "46 00 02 00 00 00 00 00 00 00");
    expect_packet("spells @4", "47 04 00 00 00 00 00 00 00 00");
    expect_packet("win", "48 ff 00 00 00 00 00 00 00 00");
    expect_packet("lose mark", "48 ff 01 01 00 00 00 00 00 00");
    expect_packet("damage 300 castle", "49 ff 01 2c 01 00 00 00 00 00");
    expect_packet("damage 50000", "49 ff 00 50 c3 00 00 00 00 00");
    expect_packet("cmd 0x1e 2", "1e 02 00 00 00 00 00 00 00 00");
    expect_packet("cheat 6", "1e 06 00 00 00 00 00 00 00 00");
    expect_packet("book", "14 02 00 00 00 00 00 00 00 00");
    expect_packet("close", "14 00 00 00 00 00 00 00 00 00");
    expect_packet("packet 0x40 1 2 3", "40 01 02 03 00 00 00 00 00 00");
    expect_packet("  Teleport 1 2   # a comment", "40 ff 00 80 01 80 02 00 00 00");
    // the builders directly (the API the other tools call)
    CHECK(hex(debug_cmd_teleport(0x1234, 0x5678)) == "40 ff 00 34 12 78 56 00 00 00");
    CHECK(hex(debug_cmd_spawn(5, 0, 1, 2, true)) == "41 05 00 01 02 01 01 ff 00 00");
    CHECK(hex(debug_cmd_kill_slot(1001, 3, 1)) == "44 ff 00 e9 03 03 01 00 00 00");
    CHECK(std::string(debug_cmd_name(DEBUG_CMD_DAMAGE)) == "damage");
    CHECK(debug_cmd_describe(debug_cmd_teleport(0x1234, 0x5678)) == "teleport self -> 4660,22136 (ground)");
    std::printf("packets - ok\n");
}

static void test_parser() {
    // errors (with the usage)
    const char *bad[] = {"spawn", "spawn nothing", "spawn dragon 1", "spawn dragon x99", "teleport 1", "teleport a b",
                         "give", "give mana", "give mana 5 nowhere", "god maybe", "kill", "kill #x", "claim", "heal everything",
                         "win now", "damage", "cmd", "cheat 9", "packet", "packet 300", "time warp", "save 12", "load", "cam",
                         "cam to 1", "overlay", "inspect", "run", "assert_count creature", "assert_health me 5",
                         "assert_mana nobody == 5", "assert_pos me 1", "assert_status me happy", "assert_god me", "assert_x",
                         "wait", "frobnicate", "god on @9", "where nobody", "find unicorn"};
    for (const char *b : bad) expect_error(b);
    CHECK(cmd_parse("spawn").error.find("usage: spawn CLASS") != std::string::npos);
    CHECK(cmd_parse("frobnicate").error.find("help") != std::string::npos);
    expect_kind("", CmdKind::EMPTY);
    expect_kind("   # only a comment", CmdKind::EMPTY);

    // host commands
    ParsedCommand c = expect_kind("time step 5", CmdKind::HOST);
    CHECK(c.host.op == HostOp::TIME && c.host.sub == "step" && c.host.n == 5);
    c = expect_kind("time step", CmdKind::HOST);
    CHECK(c.host.n == 1);
    c = expect_kind("time speed 1/4", CmdKind::HOST);
    CHECK(c.host.sub == "speed" && c.host.text == "1/4");
    c = expect_kind("time pause", CmdKind::HOST);
    CHECK(c.host.sub == "pause");
    c = expect_kind("time run", CmdKind::HOST);
    CHECK(c.host.sub == "resume");
    c = expect_kind("time", CmdKind::HOST);
    CHECK(c.host.sub == "toggle");
    c = expect_kind("shot castle_view", CmdKind::HOST);
    CHECK(c.host.op == HostOp::SHOT && c.host.text == "castle_view");
    c = expect_kind("save 3", CmdKind::HOST);
    CHECK(c.host.op == HostOp::SAVE && c.host.n == 3);
    c = expect_kind("load 0", CmdKind::HOST);
    CHECK(c.host.op == HostOp::LOAD && c.host.n == 0);
    CHECK(expect_kind("sync", CmdKind::HOST).host.op == HostOp::SYNC);
    CHECK(expect_kind("dump", CmdKind::HOST).host.op == HostOp::DUMP);
    c = expect_kind("cam to 10 20", CmdKind::HOST);
    CHECK(c.host.op == HostOp::CAM && c.host.sub == "to" && c.host.x == 0x0a80 && c.host.y == 0x1480);
    c = expect_kind("cam follow #12", CmdKind::HOST);
    CHECK(c.host.sub == "follow" && c.host.n == 12);
    c = expect_kind("cam free", CmdKind::HOST);
    CHECK(c.host.sub == "free" && c.host.n == -1);
    c = expect_kind("overlay grid on", CmdKind::HOST);
    CHECK(c.host.op == HostOp::OVERLAY && c.host.sub == "grid" && c.host.on == 1);
    c = expect_kind("overlay labels", CmdKind::HOST);
    CHECK(c.host.on == -1);
    c = expect_kind("inspect #5", CmdKind::HOST);
    CHECK(c.host.op == HostOp::INSPECT && c.host.sub == "slot" && c.host.n == 5);
    c = expect_kind("inspect cursor", CmdKind::HOST);
    CHECK(c.host.sub == "cursor");
    c = expect_kind("run tests/x.scn", CmdKind::HOST);
    CHECK(c.host.op == HostOp::RUN && c.host.text == "tests/x.scn");
    CHECK(expect_kind("quit", CmdKind::HOST).host.op == HostOp::QUIT);
    c = expect_kind("echo hello  there", CmdKind::HOST);
    CHECK(c.host.op == HostOp::ECHO && c.host.text == "hello  there");

    // assertions
    c = expect_kind("assert_count creature dragon >= 3", CmdKind::ASSERT);
    CHECK(c.check.kind == AssertKind::COUNT && c.check.cls == 5 && c.check.type == 0 && c.check.op == OP_GE && c.check.value == 3);
    c = expect_kind("assert_count spell == 24", CmdKind::ASSERT);
    CHECK(c.check.cls == 12 && c.check.type == -1);
    c = expect_kind("assert_health p2 < max", CmdKind::ASSERT);
    CHECK(c.check.who.kind == CmdWho::PLAYER && c.check.who.n == 2 && c.check.value_max && c.check.op == OP_LT);
    c = expect_kind("assert_mana castle1 != 0", CmdKind::ASSERT);
    CHECK(c.check.who.kind == CmdWho::CASTLE && c.check.who.n == 1 && c.check.op == OP_NE);
    c = expect_kind("assert_alive #300", CmdKind::ASSERT);
    CHECK(c.check.who.kind == CmdWho::SLOT && c.check.who.n == 300);
    c = expect_kind("assert_pos me 10 20 2", CmdKind::ASSERT);
    CHECK(c.check.x == 0x0a80 && c.check.y == 0x1480 && c.check.r == 0x200);
    expect_kind("assert_dead slot7", CmdKind::ASSERT);
    expect_kind("assert_status me won", CmdKind::ASSERT);
    expect_kind("assert_god p0 on", CmdKind::ASSERT);

    // queries, help, scenario-only verbs
    expect_kind("where", CmdKind::QUERY);
    expect_kind("where castle3", CmdKind::QUERY);
    expect_kind("find creature townie", CmdKind::QUERY);
    expect_kind("count effect mana_ball", CmdKind::QUERY);
    expect_kind("players", CmdKind::QUERY);
    expect_kind("queue", CmdKind::QUERY);
    CHECK(expect_kind("wait 10", CmdKind::WAIT).wait == 10);
    c = expect_kind("fly 100 200", CmdKind::INPUT);
    CHECK(c.input.verb == InputVerb::FLY && c.input.a == 100 && c.input.b == 200);
    expect_kind("steer 10 -5", CmdKind::INPUT);
    expect_kind("cast right", CmdKind::INPUT);
    expect_kind("face_class 5 -1 0x800", CmdKind::INPUT);
    const std::string help = expect_kind("help", CmdKind::TEXT).text;
    const char *names[] = {"teleport", "spawn", "give", "god", "kill", "claim", "heal", "spells", "win", "lose", "damage", "cmd", "cheat",
                           "packet", "time", "shot", "save", "load", "sync", "dump", "cam", "overlay", "inspect", "run", "quit",
                           "echo", "where", "find", "count", "players", "queue", "assert_health", "assert_mana", "assert_count",
                           "assert_alive", "assert_dead", "assert_pos", "assert_status", "assert_god", "wait", "fly", "face_class"};
    for (const char *n : names) {
        if (help.find(n) == std::string::npos) { std::printf("FAIL: help lacks %s\n", n); g_fail++; }
        const std::string h = cmd_help(n);
        if (h.find("no command") != std::string::npos || h.size() < 10) { std::printf("FAIL: help %s: %s\n", n, h.c_str()); g_fail++; }
    }
    CHECK(cmd_help("spawn").find("spawn CLASS [TYPE]") == 0);
    CHECK(cmd_help("assert").find("assert_count") != std::string::npos);
    CHECK(cmd_help("nonsense").find("no command") != std::string::npos);
    const std::vector<std::string> comp = cmd_complete("assert_");
    CHECK(comp.size() == 8);
    CHECK(cmd_complete("tel").size() == 1 && cmd_complete("tel")[0] == "teleport");

    // names
    std::vector<std::string> w = {"creature", "wyvern"};
    size_t i = 0;
    int cls, type;
    CHECK(cmd_parse_class_type(w, &i, &cls, &type, true) && cls == 5 && type == 16 && i == 2);
    w = {"castle"};
    i = 0;
    CHECK(cmd_parse_class_type(w, &i, &cls, &type, true) && cls == 3 && type == 2);
    w = {"spell", "castle"};
    i = 0;
    CHECK(cmd_parse_class_type(w, &i, &cls, &type, true) && cls == 12 && type == 16);
    w = {"standing_stone"};
    i = 0;
    CHECK(cmd_parse_class_type(w, &i, &cls, &type, true) && cls == 2 && type == 1);
    CHECK(cmd_thing_name(5, 0) == "Creature Dragon (5,0)");

    // scenario grammar: header, ticks, ranges, cursor
    Scenario s;
    CHECK(scenario_parse("name t\nrts seed 7 bots 2 map 55\nstop 90\n5 god on\n10-12 damage 5\nwait 3\nheal\n20-30 steer 1 2\n"
                         "assert_alive me\n", "t.scn", &s));
    CHECK(s.rts && s.rts_params.seed == 7 && s.rts_params.bots == 2 && s.rts_params.map == 55 &&
          (s.rts_params.flags & MODE_PARAM_DEBUG) && s.stop == 90 && s.title == "t");
    CHECK(s.items.size() == 6 && s.inputs.size() == 1);
    CHECK(s.items[0].tick == 4 && s.items[0].cmd.verb == "heal" && s.items[0].line == 7);   // cursor 1 + wait 3
    CHECK(s.items[1].tick == 4 && s.items[1].cmd.kind == CmdKind::ASSERT && s.items[1].packets_before == 1);
    CHECK(s.items[2].tick == 5 && s.items[3].tick == 10 && s.items[5].tick == 12);
    CHECK(s.inputs[0].tick == 20 && s.inputs[0].tick_end == 30);
    CHECK(!scenario_parse("level 99\n", "x.scn", &s) && s.error.find("x.scn:1:") == 0);
    CHECK(!scenario_parse("level 1\n5 run other.scn\n", "x.scn", &s) && s.error.find("x.scn:2:") == 0);
    CHECK(!scenario_parse("level 1\n5\n", "x.scn", &s));
    std::printf("parser / help / scenario grammar - ok\n");
}

static void type_text(Console &c, const char *text) {
    for (const char *p = text; *p; p++) {
        const char ch = *p;
        int sc = 0;
        bool shift = false;
        if (ch >= 'a' && ch <= 'z') sc = SDL_SCANCODE_A + (ch - 'a');
        else if (ch >= 'A' && ch <= 'Z') { sc = SDL_SCANCODE_A + (ch - 'A'); shift = true; }
        else if (ch >= '1' && ch <= '9') sc = SDL_SCANCODE_1 + (ch - '1');
        else if (ch == '0') sc = SDL_SCANCODE_0;
        else if (ch == ' ') sc = SDL_SCANCODE_SPACE;
        else if (ch == '#') { sc = SDL_SCANCODE_3; shift = true; }
        else if (ch == '@') { sc = SDL_SCANCODE_2; shift = true; }
        else if (ch == '_') { sc = SDL_SCANCODE_MINUS; shift = true; }
        else if (ch == '-') sc = SDL_SCANCODE_MINUS;
        else if (ch == '.') sc = SDL_SCANCODE_PERIOD;
        else if (ch == '/') sc = SDL_SCANCODE_SLASH;
        else if (ch == '=') sc = SDL_SCANCODE_EQUALS;
        else if (ch == '>') { sc = SDL_SCANCODE_PERIOD; shift = true; }
        CHECK(sc != 0);
        c.key(sc, shift ? KEYMOD_SHIFT : 0);
    }
}

static void test_keys_history(const fs::path &dir) {
    // key -> ASCII
    CHECK(console_key_ascii(SDL_SCANCODE_A, false) == 'a' && console_key_ascii(SDL_SCANCODE_A, true) == 'A');
    CHECK(console_key_ascii(SDL_SCANCODE_1, false) == '1' && console_key_ascii(SDL_SCANCODE_1, true) == '!');
    CHECK(console_key_ascii(SDL_SCANCODE_3, true) == '#' && console_key_ascii(SDL_SCANCODE_2, true) == '@');
    CHECK(console_key_ascii(SDL_SCANCODE_0, true) == ')' && console_key_ascii(SDL_SCANCODE_MINUS, true) == '_');
    CHECK(console_key_ascii(SDL_SCANCODE_SLASH, false) == '/' && console_key_ascii(SDL_SCANCODE_SEMICOLON, true) == ':');
    CHECK(console_key_ascii(SDL_SCANCODE_KP_7, false) == '7' && console_key_ascii(SDL_SCANCODE_BACKSLASH, false) == '\\');
    CHECK(console_key_ascii(SDL_SCANCODE_F1, false) == 0 && console_key_ascii(SDL_SCANCODE_RETURN, false) == 0);

    // editing
    std::error_code ec;
    fs::remove(dir / "console_history.txt", ec);
    Console c;
    c.init(dir.string());
    CHECK(c.history().empty());
    CHECK(c.is_toggle(SDL_SCANCODE_GRAVE, 0) && !c.is_toggle(SDL_SCANCODE_GRAVE, KEYMOD_SHIFT));
    c.key(SDL_SCANCODE_A, 0);                                   // closed: ignored
    CHECK(c.line().empty());
    c.open();
    type_text(c, "spwan");
    CHECK(c.line() == "spwan" && c.cursor() == 5);
    c.key(SDL_SCANCODE_LEFT, 0); c.key(SDL_SCANCODE_LEFT, 0); c.key(SDL_SCANCODE_LEFT, 0);
    c.key(SDL_SCANCODE_BACKSPACE, 0);                           // "swan"
    c.key(SDL_SCANCODE_RIGHT, 0);
    type_text(c, "p");                                          // "swpan"? no: cursor after 'w' -> "swpan"
    CHECK(c.line() == "swpan");
    c.key(SDL_SCANCODE_HOME, 0);
    c.key(SDL_SCANCODE_DELETE, 0);
    CHECK(c.line() == "wpan");
    c.key(SDL_SCANCODE_U, KEYMOD_CTRL);
    CHECK(c.line().empty());
    type_text(c, "tel");
    c.key(SDL_SCANCODE_TAB, 0);
    CHECK(c.line() == "teleport ");
    c.key(SDL_SCANCODE_C, KEYMOD_CTRL);
    type_text(c, "echo first");
    c.key(SDL_SCANCODE_RETURN, 0);
    type_text(c, "echo second");
    c.key(SDL_SCANCODE_RETURN, 0);
    type_text(c, "help spawn");
    c.key(SDL_SCANCODE_RETURN, 0);
    CHECK(c.history().size() == 3 && c.history()[0] == "echo first");
    type_text(c, "draft");
    c.key(SDL_SCANCODE_UP, 0);
    CHECK(c.line() == "help spawn");
    c.key(SDL_SCANCODE_UP, 0);
    c.key(SDL_SCANCODE_UP, 0);
    c.key(SDL_SCANCODE_UP, 0);
    CHECK(c.line() == "echo first");
    c.key(SDL_SCANCODE_DOWN, 0);
    CHECK(c.line() == "echo second");
    c.key(SDL_SCANCODE_DOWN, 0);
    c.key(SDL_SCANCODE_DOWN, 0);
    CHECK(c.line() == "draft");
    bool saw_second = false, saw_usage = false;
    for (const std::string &l : c.scrollback_text()) {
        if (l == "second") saw_second = true;
        if (l.find("spawn CLASS [TYPE]") == 0) saw_usage = true;
    }
    CHECK(saw_second && saw_usage);
    c.key(SDL_SCANCODE_ESCAPE, 0);
    CHECK(!c.is_open());

    // the history file round trip (one line per command, the same line twice in a row kept once)
    {
        Console d;
        d.init(dir.string());
        CHECK(d.history().size() == 3 && d.history()[2] == "help spawn");
        d.open();
        type_text(d, "help spawn");
        d.key(SDL_SCANCODE_RETURN, 0);
        CHECK(d.history().size() == 3);
        d.execute("echo not typed");                               // execute() alone does not touch the history
        CHECK(d.history().size() == 3);
    }
    std::ifstream f(dir / "console_history.txt");
    std::string l;
    int n = 0;
    while (std::getline(f, l)) n++;
    CHECK(n == 3);
    // a long file is trimmed to the last 500 lines at start
    {
        std::ofstream o(dir / "console_history.txt", std::ios::trunc);
        for (int k = 0; k < 700; k++) o << "echo " << k << "\n";
    }
    Console e;
    e.init(dir.string());
    CHECK(e.history().size() == 500 && e.history().front() == "echo 200" && e.history().back() == "echo 699");
    std::printf("keys / editing / history - ok\n");
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

static bool scroll_has(const Console &c, const char *text) {
    for (const std::string &l : c.scrollback_text())
        if (l.find(text) != std::string::npos) return true;
    return false;
}

static void tick(Console &c, int n) {
    for (int i = 0; i < n; i++) {
        debug_cmd_pump();          // mcport's sim_tick: the next queued packet, the tick, the messages logged
        game_tick_sim();
        debug_cmd_tick();          // until g_hook_debug_tick is in game_tick_sim (idempotent)
        for (const std::string &m : debug_cmd_take_messages()) mclog(MCLOG_INFO, "%s", m.c_str());
        c.after_tick();
    }
}

static void test_in_level(const fs::path &dir, const fs::path &out) {
    if (!engine_load_level(1)) { std::printf("FAIL: level 1 did not load\n"); g_fail++; return; }
    g_cfg->flags = 0;
    g_hook_player_local_input = nullptr;
    Console c;
    c.init(dir.string());
    c.open();
    c.execute("god on");
    c.execute("spawn wyvern 60 60 x2");
    c.execute("teleport 60 70");
    CHECK(debug_cmd_pending() == 3);
    CHECK(scroll_has(c, "queued: god on for self"));
    tick(c, 1);                                     // tick 1: the join packets, nothing of ours
    CHECK(debug_cmd_pending() == 3);
    tick(c, 3);
    CHECK(debug_cmd_pending() == 0);
    CHECK(scroll_has(c, "god on for player 0"));
    CHECK(scroll_has(c, "spawned 2 x Creature Wyvern"));
    CHECK(scroll_has(c, "teleported player 0 to 15488,18048"));
    c.execute("assert_count wyvern == 2");
    CHECK(scroll_has(c, "ok: count Creature Wyvern (5,16) = 2"));
    c.execute("assert_pos me 60 70 0");
    CHECK(!scroll_has(c, "FAILED"));
    c.execute("assert_god me off");
    CHECK(scroll_has(c, "FAILED: god of p0 is on"));
    c.execute("where me");
    CHECK(scroll_has(c, "at cell 60,70"));
    c.execute("players");
    CHECK(scroll_has(c, "p0* active slot"));
    c.execute("find wyvern");
    CHECK(scroll_has(c, "2 live Creature Wyvern"));
    c.execute("count creature");
    // host commands are collected for main.cpp
    c.execute("shot here");
    c.execute("time step 3");
    std::vector<HostCmd> h = c.take_host_commands();
    CHECK(h.size() == 2 && h[0].op == HostOp::SHOT && h[1].op == HostOp::TIME && h[1].n == 3);
    CHECK(c.take_host_commands().empty());
    c.execute("fly 1 2");
    CHECK(scroll_has(c, "works in scenario files only"));

    // `run`: a scenario from now on (its header ignored); a host command of the file reaches main.cpp
    {
        std::ofstream o(dir / "mini.scn");
        o << "level 5\nkill wyvern\nwait 1\nassert_count wyvern == 0\nshot mini\n2 assert_alive me\n";
    }
    c.execute("run " + (dir / "mini").string());
    CHECK(c.scenario_running());
    CHECK(scroll_has(c, "its level / rts header is ignored"));
    tick(c, 4);
    CHECK(!c.scenario_running());
    CHECK(scroll_has(c, "mini.scn: 2 assertion(s) passed, 0 failed"));
    h = c.take_host_commands();
    CHECK(h.size() == 1 && h[0].op == HostOp::SHOT && h[0].text == "mini");
    CHECK(c.exit_code() == 0);

    // drawing (the console over the level view)
    for (int k = 0; k < 30; k++) c.print("scrollback line " + std::to_string(k));
    c.execute("help");                              // long lines: wrapped at the panel width
    type_text(c, "spawn dragon 40 52 x3");
    c.key(SDL_SCANCODE_LEFT, 0);
    const bool lo = ui_lo_res();
    const int W = lo ? 320 : 640, H = lo ? 200 : 480, vh = lo ? 400 : 480;
    std::vector<uint8_t> px((size_t)W * H);
    const FrameBuffer fb{px.data(), W, H};
    render_set_view_window(fb, g_state->view_size);
    render_view(fb, player_camera(g_state->local_player));
    ui_set_target(fb);
    const UiSpriteTable *font = g_ui_font;
    c.draw(vh);
    CHECK(g_ui_font == font);                       // the font is restored
    write_ppm(out / "console_open.ppm", px.data(), W, H);
    std::printf("console in a level (console_open.ppm) - ok\n");
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    mclog_set_console(false);
    const fs::path dir = fs::temp_directory_path() / "mc_console_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::current_path();
    test_packets();
    test_parser();
    test_keys_history(dir);
    const char *game = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!engine_init(game)) std::printf("SKIP the level part: no game data in %s\n", game);
    else {
        test_in_level(dir, out);
        engine_shutdown();
    }
    if (g_fail) { std::printf("console_test: %d FAILED\n", g_fail); return 1; }
    std::printf("console_test: all passed\n");
    return 0;
}

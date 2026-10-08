// Unit test for input.cpp (local input: device state -> command packet).
//  1. the feeding API against the behaviour of the two interrupt handlers (by construction),
//  2. player_queue_command / player_mouse_steer rules (hand-computed from the disassembly),
//  3. every input mode on a generated level 38, driven through game_tick_sim() with the hook
//     installed (keys, mouse, spell book, chat line, function keys, cheats, creature_kill_all),
//  4. the movie check: for each of the 8551 packets the human player recorded in movie/mvi00000.dat a
//     device state is reconstructed, fed through the feeding API, and player_local_input() must
//     produce the recorded 10 bytes; plus the statistical test of the steering formula.
// argv[1] = game dir.
#define _CRT_SECURE_NO_WARNINGS
#include "sim.h"
#include "input.h"
#include "settings.h"
#include "player.h"
#include "mcfile.h"
#include "gen/input_tables.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); g_fail++; } } while (0)

static PlayerRec *lrec() { return &g_state->players[g_state->local_player & 7]; }
static CmdPacket *lpk() { return &g_state->commands[g_state->local_player & 7]; }
static Thing *lthing() { return thing_at(lrec()->thing % MC_THING_SLOTS); }

// ---- 1. the interrupt handlers -----------------------------------------------------------------

static void test_feeding() {
    input_reset();
    CHECK_EQ(g_mouse_x, 320); CHECK_EQ(g_mouse_y, 200);
    // make / break
    input_key_event(MC_SC_UP, true);
    CHECK_EQ(g_key_down[0x48], 1); CHECK_EQ(g_key_last, 0x48); CHECK_EQ(g_key_first, 0x48);
    input_key_event(MC_SC_UP, false);
    CHECK_EQ(g_key_down[0x48], 0); CHECK_EQ(g_key_last, 0xc8);      // the break code stays as "last key"
    CHECK_EQ(g_key_first, 0x48);                                    // only the first make code is kept
    // an extended key: e0 2a e0 48 (fake shift + key). The fake shift must not press Shift.
    input_key_raw(0xe0);
    CHECK_EQ(g_key_last, 0xe0);
    input_key_raw(0x2a);
    CHECK_EQ(g_key_down[0x2a], 0); CHECK_EQ(g_key_last, 0x80);
    input_key_raw(0xe0);
    input_key_raw(0x48);
    CHECK_EQ(g_key_down[0x48], 1); CHECK_EQ(g_key_last, 0x48);
    input_key_raw(0xe0); input_key_raw(0xc8); input_key_raw(0xe0); input_key_raw(0xaa);
    CHECK_EQ(g_key_down[0x48], 0); CHECK_EQ(g_key_down[0x2a], 0); CHECK_EQ(g_key_last, 0x80);
    // a real left shift (no prefix) does press
    input_key_event(MC_SC_LSHIFT, true);
    CHECK_EQ(g_key_down[0x2a], 1);
    input_key_event(MC_SC_LSHIFT, false);
    // the 0x100 flag feeds the prefix: right Ctrl lands on the same entry as left Ctrl
    input_key_event(MC_SC_CTRL | MC_SC_EXT, true);
    CHECK_EQ(g_key_down[0x1d], 1); CHECK_EQ(g_key_last, 0x1d);
    input_key_event(MC_SC_CTRL | MC_SC_EXT, false);
    CHECK_EQ(g_key_down[0x1d], 0);

    // mouse: position clamp of the handler (x <= 0x27e, y <= 0x1de), no lower clamp
    input_reset();
    input_mouse_event(1, 700, 500);
    CHECK_EQ(g_mouse_x, 0x27e); CHECK_EQ(g_mouse_y, 0x1de); CHECK_EQ(g_mouse_moved, 1);
    input_mouse_event(1, 100, 50);
    // press: click event + held; the click position is the unclamped event position
    input_mouse_event(2, 100, 50);
    CHECK_EQ(g_mouse_click_left, 1); CHECK_EQ(g_mouse_held_left, 1);
    CHECK_EQ(g_mouse_click_x, 100); CHECK_EQ(g_mouse_click_y, 50);
    CHECK_EQ(g_mouse_dbl_timer, 0x19); CHECK_EQ(g_mouse_dbl_click, 0);
    // consumer clears the click; while the button stays down no new click appears
    g_mouse_click_left = 0;
    input_mouse_event(2, 120, 60);
    CHECK_EQ(g_mouse_click_left, 0); CHECK_EQ(g_mouse_held_left, 1);
    input_mouse_event(4, 120, 60);
    CHECK_EQ(g_mouse_held_left, 0);
    // second press while the double-click timer is loaded
    input_mouse_event(2, 130, 70);
    CHECK_EQ(g_mouse_click_left, 1); CHECK_EQ(g_mouse_dbl_click, 1); CHECK_EQ(g_mouse_click_x, 130);
    // a pending (unconsumed) click is not overwritten by a later press
    input_mouse_event(4, 130, 70);
    input_mouse_event(2, 300, 300);
    CHECK_EQ(g_mouse_click_x, 130); CHECK_EQ(g_mouse_held_left, 1);
    // right and middle
    input_mouse_button(1, true);
    CHECK_EQ(g_mouse_click_right, 1); CHECK_EQ(g_mouse_held_right, 1); CHECK_EQ(g_mouse_click_x, 300);
    input_mouse_button(1, false);
    CHECK_EQ(g_mouse_click_right, 1); CHECK_EQ(g_mouse_held_right, 0);
    input_mouse_button(2, true);
    CHECK_EQ(g_mouse_click_middle, 1); CHECK_EQ(g_mouse_held_middle, 1);
    input_mouse_button(2, false);
    CHECK_EQ(g_mouse_held_middle, 0);
    // without a driver nothing happens
    input_reset();
    g_mouse_present = 0;
    input_mouse_event(2, 5, 5);
    CHECK_EQ(g_mouse_click_left, 0); CHECK_EQ(g_mouse_x, 320);
    g_mouse_present = 1;
    // input_mouse_move clamps to the driver range, then the handler clamps
    uint16_t mode = g_video_mode_flags;
    g_video_mode_flags = 1;
    input_mouse_move(-5, 1000);
    CHECK_EQ(g_mouse_x, 0); CHECK_EQ(g_mouse_y, 400);
    g_video_mode_flags = 8;
    input_mouse_move(1000, 1000);
    CHECK_EQ(g_mouse_x, 0x27e); CHECK_EQ(g_mouse_y, 0x1de);
    // input_mouse_center / set_pos
    input_mouse_center();
    CHECK_EQ(g_mouse_x, 320); CHECK_EQ(g_mouse_y, 240);
    g_video_mode_flags = 1;
    input_mouse_center();
    CHECK_EQ(g_mouse_x, 320); CHECK_EQ(g_mouse_y, 200);
    input_mouse_set_pos(900, 900);
    CHECK_EQ(g_mouse_x, 0x27e); CHECK_EQ(g_mouse_y, 0x18e);
    input_mouse_set_pos(-1, 5);                                     // x == -1: ignored
    CHECK_EQ(g_mouse_y, 0x18e);
    input_mouse_set_pos(0, 0);                                      // <= 0: the variables keep their value
    CHECK_EQ(g_mouse_x, 0x27e); CHECK_EQ(g_mouse_y, 0x18e);
    g_video_mode_flags = mode;

    // input_snapshot / input_changed
    input_reset();
    input_snapshot();
    CHECK_EQ(input_changed(), 0);
    input_mouse_move(321, 200);
    CHECK_EQ(input_changed(), 1);
    CHECK_EQ(input_changed(), 1);                                   // latched
    input_snapshot();
    CHECK_EQ(input_changed(), 0);
    input_key_event(MC_SC_SPACE, true);
    CHECK_EQ(input_changed(), 1);
    input_snapshot();
    input_mouse_button(0, true);
    CHECK_EQ(input_changed(), 1);
    input_reset();
    std::printf("feeding API: ok\n");
}

// ---- 2. queue rules and the steering formula ---------------------------------------------------

static void clear_packet() { std::memset(lpk(), 0, sizeof(CmdPacket)); }

static void test_queue_and_steer() {
    input_reset();
    PlayerRec *rec = lrec();
    Thing *t = lthing();
    const uint16_t status0 = rec->status;
    const int32_t health0 = t->health;
    const uint8_t state0 = t->state;
    const uint16_t flags0 = g_cfg->flags;
    char name0[sizeof rec->name];
    std::memcpy(name0, rec->name, sizeof name0);

    // key bits accumulate under command 6
    clear_packet();
    player_queue_command(6, 1); player_queue_command(6, 8); player_queue_command(6, 0x10);
    CHECK_EQ(lpk()->cmd, 6); CHECK_EQ(lpk()->bits, 0x19); CHECK_EQ(lpk()->arg, 0);
    // a later command does not replace it ...
    player_queue_command(0x14, 2);
    CHECK_EQ(lpk()->cmd, 6); CHECK_EQ(lpk()->arg, 0);
    // ... except the unconditional ones (0, 1, 2, 0x1a)
    player_queue_command(2, 0);
    CHECK_EQ(lpk()->cmd, 2);
    // the first command of a tick wins: key bits are dropped afterwards
    clear_packet();
    player_queue_command(0x14, 2);
    player_queue_command(6, 1);
    CHECK_EQ(lpk()->cmd, 0x14); CHECK_EQ(lpk()->arg, 2); CHECK_EQ(lpk()->bits, 0);
    // same command again: argument replaced (0x14..0x19, 0x11) or or-ed (4)
    player_queue_command(0x14, 0);
    CHECK_EQ(lpk()->arg, 0);
    clear_packet();
    player_queue_command(4, 8); player_queue_command(4, 0x10);
    CHECK_EQ(lpk()->cmd, 4); CHECK_EQ(lpk()->arg, 0x18);
    clear_packet();
    player_queue_command(0x11, 'A'); player_queue_command(0x11, 'B');
    CHECK_EQ(lpk()->cmd, 0x11); CHECK_EQ(lpk()->arg, 'B');
    // commands the function never queues
    static const int never[] = {3, 7, 8, 9, 0xd, 0x1f, 0x100, 0xffff};
    for (int c : never) {
        clear_packet();
        player_queue_command(c, 5);
        CHECK_EQ(lpk()->cmd, 0); CHECK_EQ(lpk()->arg, 0);
    }
    // 0xf: only while the wizard lies dead (health < 0, state 3)
    clear_packet();
    player_queue_command(0xf, 0);
    CHECK_EQ(lpk()->cmd, 0);
    t->health = -1;
    player_queue_command(0xf, 0);
    CHECK_EQ(lpk()->cmd, 0);
    t->state = 3;
    player_queue_command(0xf, 0);
    CHECK_EQ(lpk()->cmd, 0xf);
    t->health = health0; t->state = state0;
    // 0x1b needs status bit 2 (won), 0x1c bit 4 (lost)
    clear_packet();
    rec->status = 0;
    player_queue_command(0x1b, 0); player_queue_command(0x1c, 0);
    CHECK_EQ(lpk()->cmd, 0);
    rec->status = 4;
    player_queue_command(0x1b, 0);
    CHECK_EQ(lpk()->cmd, 0);
    player_queue_command(0x1c, 0);
    CHECK_EQ(lpk()->cmd, 0x1c);
    clear_packet();
    rec->status = 2;
    player_queue_command(0x1c, 0);
    CHECK_EQ(lpk()->cmd, 0);
    player_queue_command(0x1b, 0);
    CHECK_EQ(lpk()->cmd, 0x1b);
    rec->status = status0;
    // 0x1e: the name "chronicle" (exact case, 9 characters) or the cheat gate
    clear_packet();
    g_cfg->flags &= ~0x8000;
    std::strcpy(rec->name, "Chronicle");
    player_queue_command(0x1e, 3);
    CHECK_EQ(lpk()->cmd, 0);
    std::strcpy(rec->name, "chronicles");
    player_queue_command(0x1e, 3);
    CHECK_EQ(lpk()->cmd, 0x1e); CHECK_EQ(lpk()->arg, 3);
    clear_packet();
    std::strcpy(rec->name, "nobody");
    player_queue_command(0x1e, 3);
    CHECK_EQ(lpk()->cmd, 0);
    g_cfg->flags |= 0x8000;
    player_queue_command(0x1e, 7);
    CHECK_EQ(lpk()->cmd, 0x1e); CHECK_EQ(lpk()->arg, 7);
    g_cfg->flags = flags0;
    std::memcpy(rec->name, name0, sizeof name0);

    // steering: (x - 320) * 128 / 320 and -(y - 200) * 128 / 200 resp. -(y - 240) * 128 / 240,
    // truncated toward zero, clamped to +-127; only when Config.substeps == 0
    const uint16_t mode = g_video_mode_flags;
    const uint8_t sub0 = g_cfg->substeps;
    g_cfg->substeps = 0;
    struct { int mode, mx, my, sx, sy; } cases[] = {
        {1, 320, 200, 0, 0},     {1, 322, 198, 0, 1},     {1, 323, 197, 1, 1},   {1, 317, 203, -1, -1},
        {1, 0, 0, -127, 127},    {1, 638, 400, 127, -127}, {1, 638, 398, 127, -126}, {1, 3, 399, -126, -127},
        {1, 480, 100, 64, 64},   {1, 160, 300, -64, -64},
        {8, 320, 240, 0, 0},     {8, 0, 0, -127, 127},     {8, 638, 478, 127, -126}, {8, 480, 120, 64, 64},
        {8, 321, 241, 0, 0},     {8, 323, 238, 1, 1},      {8, 317, 242, -1, -1},
    };
    for (const auto &c : cases) {
        clear_packet();
        g_video_mode_flags = (uint16_t)c.mode;
        g_mouse_x = (int16_t)c.mx; g_mouse_y = (int16_t)c.my;
        player_mouse_steer();
        if (lpk()->steer_x != c.sx || lpk()->steer_y != c.sy) {
            std::printf("FAIL steer mode %d (%d,%d): got (%d,%d) want (%d,%d)\n", c.mode, c.mx, c.my,
                        lpk()->steer_x, lpk()->steer_y, c.sx, c.sy);
            g_fail++;
        }
    }
    clear_packet();
    g_cfg->substeps = 1;
    g_mouse_x = 600; g_mouse_y = 10;
    player_mouse_steer();
    CHECK_EQ(lpk()->steer_x, 0); CHECK_EQ(lpk()->steer_y, 0);       // fast-forward: no steering
    g_cfg->substeps = sub0;
    g_video_mode_flags = mode;
    clear_packet();
    input_reset();
    std::printf("queue rules / steering formula: ok\n");
}

// ---- 3. a scripted session on level 38 ---------------------------------------------------------

static CmdPacket s_sent;            // the packet player_local_input produced in the last tick
static int s_platform_req[8];
static int s_platform_arg = 0;
static int s_joystick_polls = 0, s_joystick_mode = -1;
static void capture_hook() {
    player_local_input();
    s_sent = *lpk();
}
static void platform_hook(InputPlatformRequest what, int arg) { s_platform_req[(int)what & 7]++; s_platform_arg = arg; }
static void joystick_hook(int mode, int, int, int, int, int, int) { s_joystick_polls++; s_joystick_mode = mode; }
static void tick() { game_tick_sim(); }
// One key press that lasts one tick (the release arrives after the tick, as on real hardware).
static void tap_tick(int sc) {
    input_key_event(sc, true);
    tick();
    input_key_event(sc, false);
}
static const char *msg_text() { return lrec()->messages[g_state->local_player & 7].text; }

static void test_session(const char *game_dir) {
    input_reset();
    CHECK(input_text_load(game_dir, 0));
    CHECK(std::strcmp(input_text(MC_TEXT_SHADOWS_ON), "Shadows On") == 0);
    CHECK(std::strcmp(input_text(MC_TEXT_SPEED_SUPER_FAST), "speed super fast.") == 0);
    CHECK(std::strcmp(input_text(MC_TEXT_MUSIC_OFF), "Music Off") == 0);
    CHECK(std::strcmp(input_text(MC_TEXT_BLUR_HEAVY), "Heavy Speed Blur On") == 0);

    g_video_mode_flags = 8;
    g_cfg->flags = 0;
    CHECK(sim_load_level(38));
    // The join command copies the configured name into the record: "chronicle" opens the cheats.
    std::strcpy(g_cfg->save_str_a, "x");
    std::strcpy(g_cfg->save_str_b, "chronicle");
    g_hook_player_local_input = capture_hook;
    g_hook_input_platform = platform_hook;
    g_hook_input_joystick_poll = joystick_hook;

    // first tick: the pending join command (cmd 1) blocks all input and swallows clicks
    input_mouse_button(0, true); input_mouse_button(1, true);
    input_key_event(MC_SC_ENTER, true);
    tick();
    CHECK_EQ(s_sent.cmd, 1);
    CHECK_EQ(g_mouse_click_left, 0); CHECK_EQ(g_mouse_click_right, 0);
    CHECK_EQ(g_key_last, 0x1c);                                     // the key itself stays pending
    input_mouse_button(0, false); input_mouse_button(1, false);
    input_key_event(MC_SC_ENTER, false);
    CHECK(lrec()->thing != 0);
    CHECK_EQ(lrec()->is_computer, 0);
    CHECK(std::strcmp(lrec()->name, "chronicle") == 0);
    Thing *t = lthing();
    PlayerBlock *P = player_block(t);
    const int book0 = P->slot_left;

    // idle tick: mouse centred -> an all-zero packet
    input_mouse_center();
    tick();
    { CmdPacket zero{}; CHECK(std::memcmp(&s_sent, &zero, sizeof zero) == 0); }
    CHECK_EQ(s_joystick_mode, 0);

    // flying: Up held -> bits 1 under cmd 6, target speed climbs to the maximum
    input_key_event(MC_SC_UP, true);
    for (int i = 0; i < 8; i++) tick();
    CHECK_EQ(s_sent.cmd, 6); CHECK_EQ(s_sent.bits, 1);
    CHECK_EQ(P->input_bits, 1u);
    CHECK_EQ(P->target_speed, 0x50);
    const Pos p0 = *thing_pos(t);
    const uint16_t yaw0 = t->yaw;
    // mouse to the right and up: steer bytes, the wizard turns
    input_mouse_move(480, 120);
    for (int i = 0; i < 10; i++) tick();
    CHECK_EQ(s_sent.steer_x, 64); CHECK_EQ(s_sent.steer_y, 64);
    CHECK(t->yaw != yaw0);
    CHECK(t->x != p0.x || t->y != p0.y);
    // Left + Right + Down together with Up
    input_key_event(MC_SC_LEFT | MC_SC_EXT, true);
    input_key_event(MC_SC_DOWN, true);
    tick();
    CHECK_EQ(s_sent.bits, 1 | 2 | 4);
    input_key_event(MC_SC_LEFT | MC_SC_EXT, false);
    input_key_event(MC_SC_DOWN, false);
    input_key_event(MC_SC_RIGHT, true);
    tick();
    tick();
    CHECK_EQ(s_sent.bits, 1 | 8);
    CHECK(P->strafe_speed > 0);
    input_key_event(MC_SC_RIGHT, false);
    input_key_event(MC_SC_UP, false);
    input_mouse_center();
    tick();
    CHECK_EQ(s_sent.cmd, 0); CHECK_EQ(s_sent.bits, 0);

    // a click without a spell in the hand still sends the fire bit (slot 0xff reads things[0])
    if (book0 == 0xff) {
        input_mouse_button(0, true);
        tick();
        CHECK_EQ(s_sent.cmd, 6); CHECK_EQ(s_sent.bits, 0x10);
        CHECK_EQ(g_mouse_click_left, 0);
        tick();                                                     // held, nothing being cast: no bit
        CHECK_EQ(s_sent.bits, 0);
        input_mouse_button(0, false);
    }

    // Alt+F1: cheat 1 (all spells) because the name matches
    input_key_event(MC_SC_ALT, true);
    tap_tick(MC_SC_F1);
    CHECK_EQ(s_sent.cmd, 0x1e); CHECK_EQ(s_sent.arg, 1);
    CHECK_EQ(g_key_last, 0xbb);
    // while Alt is held the arrow keys and the buttons are not looked at
    input_key_event(MC_SC_UP, true);
    input_mouse_button(0, true);
    tick();
    CHECK_EQ(s_sent.cmd, 0); CHECK_EQ(s_sent.bits, 0);
    CHECK_EQ(g_mouse_click_left, 1);                                // the click stays pending
    input_key_event(MC_SC_ALT, false);
    input_key_event(MC_SC_UP, false);
    input_mouse_button(0, false);
    g_mouse_click_left = 0;
    int spells = 0;
    for (int i = 0; i < 24; i++) if (P->spell_slot[i] != 0) spells++;
    CHECK_EQ(spells, 24);
    tick();     // player_rebuild_spell_index runs in the flyer's update
    for (int id = 0; id < 24; id++) CHECK(P->spell_thing[id] != 0);

    // 1..0 quick-select into the left hand, Ctrl+1..0 into the right
    tap_tick(MC_SC_3);
    CHECK_EQ(s_sent.cmd, 0x18); CHECK_EQ(s_sent.arg, 2);
    CHECK_EQ(P->slot_left, (int8_t)P->hotkey_slot[2]);
    input_key_event(MC_SC_CTRL, true);
    tap_tick(MC_SC_0);
    CHECK_EQ(s_sent.cmd, 0x19); CHECK_EQ(s_sent.arg, 9);
    CHECK_EQ(P->slot_right, (int8_t)P->hotkey_slot[9]);
    input_key_event(MC_SC_CTRL, false);

    // firing: a click sends the bit once; holding keeps it only while the spell is being cast
    {
        Thing *spell = thing_at((unsigned)P->spell_slot[P->slot_left]);
        const int16_t cast0 = spell->cast_ticks;
        const uint8_t sf0 = spell->spell_flags, b0 = spell->burst, m0 = spell->unk3e;
        spell->spell_flags = 0; spell->cast_ticks = 0; spell->burst = 0; spell->unk3e = 0;
        input_mouse_button(0, true);
        lpk()->cmd = 0;
        player_local_input();
        CHECK_EQ(lpk()->cmd, 6); CHECK_EQ(lpk()->bits, 0x10); CHECK_EQ(g_mouse_click_left, 0);
        clear_packet();
        player_local_input();                                       // held, idle spell: nothing
        CHECK_EQ(lpk()->cmd, 0);
        spell->cast_ticks = 5;                                      // held while casting: keeps firing
        player_local_input();
        CHECK_EQ(lpk()->bits, 0x10);
        clear_packet();
        spell->cast_ticks = 0; spell->unk3e = 3; spell->burst = 1;  // burst spell with shots queued
        player_local_input();
        CHECK_EQ(lpk()->bits, 0x10);
        clear_packet();
        spell->burst = 0;
        player_local_input();
        CHECK_EQ(lpk()->bits, 0);
        spell->spell_flags = 1; spell->cast_ticks = 5;              // spell_flags 1: clicks only
        player_local_input();
        CHECK_EQ(lpk()->bits, 0);
        input_mouse_button(0, false);
        input_mouse_button(0, true);
        player_local_input();
        CHECK_EQ(lpk()->bits, 0x10);
        input_mouse_button(0, false);
        // right hand, with a held left button that does not fire
        clear_packet();
        input_mouse_button(1, true);
        player_local_input();
        CHECK_EQ(lpk()->cmd, 6); CHECK_EQ(lpk()->bits, 0x20); CHECK_EQ(g_mouse_click_right, 0);
        input_mouse_button(1, false);
        clear_packet();
        spell->cast_ticks = cast0; spell->spell_flags = sf0; spell->burst = b0; spell->unk3e = m0;
    }

    // Enter opens the spell book (cmd 0x14 arg 2); arrow keys of that tick are dropped
    input_key_event(MC_SC_UP, true);
    tap_tick(MC_SC_ENTER);
    CHECK_EQ(s_sent.cmd, 0x14); CHECK_EQ(s_sent.arg, 2); CHECK_EQ(s_sent.bits, 0);
    CHECK_EQ(lrec()->input_mode, 2);
    input_key_event(MC_SC_UP, false);
    // book: hover every cell, the hit test finds it; no packet without a click, no steering
    for (int cell = 0; cell < 24; cell++) {
        int x, y;
        input_book_cell_origin(cell, &x, &y);
        input_mouse_move(x + 1, y + 1);
        input_book_update_selection();
        CHECK_EQ(g_cfg->spell_slot, cell);
        input_mouse_move(x + 0x3f, y + input_book_cell_h() - 1);
        input_book_update_selection();
        CHECK_EQ(g_cfg->spell_slot, cell);
    }
    // 320x200: the book starts at y 0xa2 and the rows are 36 high (mspr0-0.tab sizes doubled)
    g_video_mode_flags = 1;
    CHECK_EQ(input_book_cell_h(), 36);
    input_mouse_move(0x180 + 3 * 0x40 + 10, 0xa2 + 5 * 36 + 35);
    input_book_update_selection();
    CHECK_EQ(g_cfg->spell_slot, 23);
    input_mouse_move(0x180, 0xa2 - 1);
    input_book_update_selection();
    CHECK_EQ(g_cfg->spell_slot, 0xff);
    input_mouse_move(0x1c0, 0xa2 + 36);
    input_book_update_selection();
    CHECK_EQ(g_cfg->spell_slot, 5);
    g_video_mode_flags = 8;
    CHECK_EQ(input_book_cell_h(), 37);
    input_mouse_move(0x17f, 0xc2 + 5);
    input_book_update_selection();
    CHECK_EQ(g_cfg->spell_slot, 0xff);
    input_mouse_move(0x180, 0xc2 + 6 * input_book_cell_h());
    input_book_update_selection();
    CHECK_EQ(g_cfg->spell_slot, 0xff);
    tick();
    { CmdPacket zero{}; CHECK(std::memcmp(&s_sent, &zero, sizeof zero) == 0); }
    CHECK_EQ(s_joystick_mode, 1);
    // a click outside the cells is consumed without a command
    input_mouse_button(0, true);
    tick();
    CHECK_EQ(s_sent.cmd, 0); CHECK_EQ(g_mouse_click_left, 0);
    input_mouse_button(0, false);
    // 5 on a cell: bind that spell to quick-select key 5 (cmd 0x17 arg 4 + the book slot)
    {
        int x, y;
        input_book_cell_origin(13, &x, &y);
        input_mouse_move(x + 30, y + 10);
        input_book_update_selection();
        tap_tick(MC_SC_5);
        CHECK_EQ(s_sent.cmd, 0x17); CHECK_EQ(s_sent.arg, 4);
        CHECK(s_sent.pad2 < 24);
        CHECK_EQ(thing_at((unsigned)P->spell_slot[s_sent.pad2 % 24])->type, g_input_book_order[13]);
        CHECK_EQ(P->hotkey_slot[4], s_sent.pad2);
        CHECK_EQ(lrec()->input_mode, 2);                            // the book stays open
    }
    // right click on a cell: that spell into the right hand, book closes
    {
        int x, y;
        input_book_cell_origin(6, &x, &y);
        input_mouse_move(x + 5, y + 20);
        input_book_update_selection();
        input_mouse_button(1, true);
        tick();
        input_mouse_button(1, false);
        CHECK_EQ(s_sent.cmd, 0x16); CHECK(s_sent.arg < 24);
        CHECK_EQ(s_sent.steer_x, 0); CHECK_EQ(s_sent.steer_y, 0);
        CHECK_EQ(P->slot_right, s_sent.arg);
        CHECK_EQ(thing_at((unsigned)P->spell_slot[P->slot_right])->type, g_input_book_order[6]);
        CHECK_EQ(lrec()->input_mode, 0);
    }
    // both buttons at once open the book, a left click picks, Ctrl+5 recalls the bound spell
    input_mouse_button(0, true); input_mouse_button(1, true);
    tick();
    CHECK_EQ(s_sent.cmd, 0x14); CHECK_EQ(s_sent.arg, 2);
    input_mouse_button(0, false); input_mouse_button(1, false);
    CHECK_EQ(lrec()->input_mode, 2);
    {
        int x, y;
        input_book_cell_origin(21, &x, &y);
        input_mouse_move(x + 60, y + 30);
        input_book_update_selection();
        input_mouse_button(0, true);
        tick();
        input_mouse_button(0, false);
        CHECK_EQ(s_sent.cmd, 0x15);
        CHECK_EQ(thing_at((unsigned)P->spell_slot[P->slot_left])->type, g_input_book_order[21]);
        CHECK_EQ(lrec()->input_mode, 0);
    }
    input_key_event(MC_SC_CTRL, true);
    tap_tick(MC_SC_5);
    input_key_event(MC_SC_CTRL, false);
    CHECK_EQ(s_sent.cmd, 0x19); CHECK_EQ(s_sent.arg, 4);
    CHECK_EQ(thing_at((unsigned)P->spell_slot[P->slot_right])->type, g_input_book_order[13]);
    // Enter closes an open book (cmd 0x14 arg 0)
    tap_tick(MC_SC_ENTER);
    CHECK_EQ(lrec()->input_mode, 2);
    tap_tick(MC_SC_ENTER);
    CHECK_EQ(s_sent.cmd, 0x14); CHECK_EQ(s_sent.arg, 0);
    CHECK_EQ(lrec()->input_mode, 0);
    input_mouse_center();

    // chat line: I, then letters / digits / space / backspace, Enter sends. "RATTY" opens the gate.
    tap_tick(MC_SC_I);
    CHECK_EQ(s_sent.cmd, 0x10);
    CHECK_EQ(lrec()->input_mode, 3);
    static const int typed[] = {MC_SC_R, MC_SC_A, MC_SC_X, MC_SC_BACKSPACE, MC_SC_T, MC_SC_T, MC_SC_Y};
    for (int sc : typed) {
        tap_tick(sc);
        CHECK_EQ(s_sent.cmd, 0x11);
        CHECK_EQ(s_sent.arg, g_input_scancode_ascii[sc]);
        tick();                                                     // the break code is swallowed
        CHECK_EQ(s_sent.cmd, 0); CHECK_EQ(g_key_last, 0);
    }
    CHECK(std::strcmp(msg_text(), "RATTY") == 0);
    tap_tick(MC_SC_ESC);                                            // Esc is not a text key (table entry 0x27)
    CHECK_EQ(s_sent.cmd, 0);
    tap_tick(MC_SC_F3);                                             // function keys do not work while typing
    CHECK_EQ(g_cfg->substeps, 0);
    tap_tick(MC_SC_ENTER);
    CHECK_EQ(s_sent.cmd, 0x13);
    CHECK_EQ(lrec()->input_mode, 0);
    CHECK(g_cfg->flags & 0x8000);

    // function keys: notices and option bytes
    tap_tick(MC_SC_F3);
    CHECK_EQ(g_cfg->substeps, 1);
    CHECK(std::strcmp(msg_text(), "speed fast.") == 0);
    CHECK_EQ(lrec()->messages[0].ticks, 0x32); CHECK_EQ(lrec()->messages[0].arg, 2);
    input_mouse_move(600, 100);
    tick();
    CHECK_EQ(s_sent.steer_x, 0); CHECK_EQ(s_sent.steer_y, 0);       // no steering in the fast modes
    tap_tick(MC_SC_F3);
    CHECK_EQ(g_cfg->substeps, 2);
    CHECK(std::strcmp(msg_text(), "speed super fast.") == 0);
    tap_tick(MC_SC_F3);
    CHECK_EQ(g_cfg->substeps, 0);
    CHECK(std::strcmp(msg_text(), "speed normal.") == 0);
    input_mouse_center();
    {
        GameState *st = g_state;
        const uint8_t smooth = st->opt_smooth, refl = st->opt_second_surface, sky = st->opt_textured_sky;
        const uint8_t shadows = st->opt_shadows, hud_a = st->opt_hud_a, hud_b = st->opt_hud_b;
        tap_tick(MC_SC_F4);
        CHECK_EQ(st->opt_smooth, smooth ^ 1);
        CHECK(std::strcmp(msg_text(), st->opt_smooth ? "Soften On" : "Soften Off") == 0);
        tap_tick(MC_SC_F5);
        CHECK_EQ(st->opt_second_surface, refl ^ 1);
        CHECK(std::strcmp(msg_text(), st->opt_second_surface ? "Reflections On" : "Reflections Off") == 0);
        tap_tick(MC_SC_F6);
        CHECK_EQ(st->opt_textured_sky, sky ^ 1);
        CHECK(std::strcmp(msg_text(), st->opt_textured_sky ? "Sky On" : "Sky Off") == 0);
        tap_tick(MC_SC_F7);
        CHECK_EQ(st->opt_shadows, shadows ^ 1);
        CHECK(std::strcmp(msg_text(), st->opt_shadows ? "Shadows On" : "Shadows Off") == 0);
        tap_tick(MC_SC_F8);
        CHECK_EQ(st->opt_hud_a, hud_a ^ 1); CHECK_EQ(st->opt_hud_b, hud_b ^ 1);
        CHECK(std::strcmp(msg_text(), st->opt_hud_a ? "Icons and Map On" : "Icons and Map Off") == 0);
        // gates: a cleared "allowed" byte makes the key do nothing (but still consumes it)
        st->opt_allowed[8] = 0;
        tap_tick(MC_SC_F4);
        CHECK_EQ(st->opt_smooth, smooth ^ 1);
        st->opt_allowed[8] = 1;
        tap_tick(MC_SC_F4); tap_tick(MC_SC_F5); tap_tick(MC_SC_F6); tap_tick(MC_SC_F7); tap_tick(MC_SC_F8);
        CHECK_EQ(st->opt_smooth, smooth); CHECK_EQ(st->opt_second_surface, refl);
        CHECK_EQ(st->opt_textured_sky, sky); CHECK_EQ(st->opt_shadows, shadows); CHECK_EQ(st->opt_hud_a, hud_a);
        // F9 / F10 need the second screen buffer
        uint8_t *frame2 = g_frame2;
        g_frame2 = nullptr;
        tap_tick(MC_SC_F9); tap_tick(MC_SC_F10);
        CHECK_EQ(st->opt_motion_blur, 0); CHECK_EQ(st->mode_3d, 0);
        static uint8_t dummy_frame[4];
        g_frame2 = dummy_frame;
        tap_tick(MC_SC_F9);
        CHECK_EQ(st->opt_motion_blur, 1); CHECK(std::strcmp(msg_text(), "Light Speed Blur On") == 0);
        tap_tick(MC_SC_F9);
        CHECK_EQ(st->opt_motion_blur, 2); CHECK(std::strcmp(msg_text(), "Heavy Speed Blur On") == 0);
        tap_tick(MC_SC_F9);
        CHECK_EQ(st->opt_motion_blur, 0); CHECK(std::strcmp(msg_text(), "Speed Blur Off") == 0);
        st->view_size = 0x20;
        tap_tick(MC_SC_F10);
        CHECK_EQ(st->mode_3d, 1); CHECK_EQ(st->view_size, 0x28);
        CHECK_EQ(s_platform_req[INPUT_REQ_3D_MODE_ON], 1);
        tap_tick(MC_SC_LBRACKET);                                   // [ ] do nothing in a 3D mode
        tap_tick(MC_SC_RBRACKET);
        CHECK_EQ(st->view_size, 0x28);
        tap_tick(MC_SC_F10);
        CHECK_EQ(st->mode_3d, 2); CHECK_EQ(s_platform_req[INPUT_REQ_3D_MODE_RESTORE], 1);
        tap_tick(MC_SC_F10);
        CHECK_EQ(st->mode_3d, 0);
        g_frame2 = frame2;
        // [ ]: view size 0x11..0x28
        tap_tick(MC_SC_LBRACKET);
        CHECK_EQ(st->view_size, 0x28);
        for (int i = 0; i < 30; i++) tap_tick(MC_SC_RBRACKET);
        CHECK_EQ(st->view_size, 0x11);
        tap_tick(MC_SC_LBRACKET);
        CHECK_EQ(st->view_size, 0x12);
        for (int i = 0; i < 30; i++) tap_tick(MC_SC_LBRACKET);
        CHECK_EQ(st->view_size, 0x28);
        // Alt+H: the interlaced stereo option
        input_key_event(MC_SC_ALT, true);
        tap_tick(MC_SC_H);
        CHECK_EQ(st->opt_interlaced, 1);
        tap_tick(MC_SC_H);
        CHECK_EQ(st->opt_interlaced, 0);
        // Alt+V (with the gate): cmd 4 arg 8 toggles the debug overlay bit
        tap_tick(MC_SC_V);
        CHECK_EQ(s_sent.cmd, 4); CHECK_EQ(s_sent.arg, 8); CHECK_EQ(lrec()->flags, 8);
        tap_tick(MC_SC_V);
        CHECK_EQ(lrec()->flags, 0);
        tap_tick(MC_SC_M);
        CHECK_EQ(s_sent.cmd, 4); CHECK_EQ(s_sent.arg, 0x10);
        tap_tick(MC_SC_M);
        tap_tick(MC_SC_S);
        CHECK_EQ(s_sent.cmd, 0xa);
        input_key_event(MC_SC_ALT, false);
        // F1 / F2: only when the platform reports sound / music
        tap_tick(MC_SC_F1); tap_tick(MC_SC_F2);
        CHECK_EQ(g_sound_on, 0); CHECK_EQ(g_music_on, 0);
        g_sound_available = g_music_available = 1;
        tap_tick(MC_SC_F1);
        CHECK_EQ(g_sound_on, 1); CHECK(std::strcmp(msg_text(), "Sound On") == 0);
        tap_tick(MC_SC_F2);
        CHECK_EQ(g_music_on, 1); CHECK(std::strcmp(msg_text(), "Music On") == 0);
        CHECK_EQ(s_platform_req[INPUT_REQ_MUSIC_PLAY], 1);
        // R: resolution switch request
        tap_tick(MC_SC_R);
        CHECK_EQ(s_platform_req[INPUT_REQ_TOGGLE_RESOLUTION], 1);
        // P: pause stops sound and music; P again restarts the music
        const int stops0 = s_platform_req[INPUT_REQ_SOUND_STOP_ALL];
        input_key_event(MC_SC_UP, true);
        tap_tick(MC_SC_P);
        CHECK_EQ(g_cfg->paused & 1, 1);
        CHECK_EQ(s_platform_req[INPUT_REQ_MUSIC_STOP], 1);
        CHECK_EQ(s_platform_req[INPUT_REQ_SOUND_STOP_ALL], stops0 + 1);
        for (int i = 0; i < 5; i++) tick();
        CHECK_EQ(s_sent.bits, 1);                                   // input goes on while paused
        tap_tick(MC_SC_P);
        CHECK_EQ(g_cfg->paused & 1, 0);
        CHECK_EQ(s_platform_req[INPUT_REQ_MUSIC_PLAY], 2);
        input_key_event(MC_SC_UP, false);
        tap_tick(MC_SC_F1); tap_tick(MC_SC_F2);
        CHECK_EQ(g_sound_on, 0); CHECK_EQ(g_music_on, 0);
        CHECK(std::strcmp(msg_text(), "Music Off") == 0);
        g_sound_available = g_music_available = 0;
    }

    // creature_kill_all: everything on the per-type lists of the last update dies
    {
        int creatures = 0, listed = 0;
        for (int i = 1; i < MC_THING_SLOTS; i++) {
            const Thing *c = thing_at(i);
            if (c->cls == 5 && c->health >= 0) creatures++;
        }
        for (int l = 0; l < 20; l++)
            for (uint32_t i = g_cfg->creature_lists[l]; i != 0; i = thing_at(i)->next) listed++;
        creature_kill_all();
        int alive = 0;
        for (int i = 1; i < MC_THING_SLOTS; i++) {
            const Thing *c = thing_at(i);
            if (c->cls == 5 && c->health >= 0 && c->state != 0x78) alive++;
        }
        std::printf("creature_kill_all: %d creatures alive before, %d on the lists, %d alive after\n", creatures, listed, alive);
        CHECK(listed > 50);
        CHECK_EQ(alive, 0);
        CHECK(t->health >= 0);                                      // the players are not on those lists
    }

    // Shift keys: K kills the wizard; Space asks for the respawn once it lies dead
    input_key_event(MC_SC_RSHIFT, true);
    tap_tick(MC_SC_K);
    input_key_event(MC_SC_RSHIFT, false);
    CHECK(t->health < 0);
    int waited = 0;
    while (t->state != 3 && waited < 400) { tick(); waited++; }
    CHECK_EQ(t->state, 3);
    // dead: Enter does not open the book
    tap_tick(MC_SC_ENTER);
    CHECK_EQ(s_sent.cmd, 0);
    CHECK_EQ(lrec()->input_mode, 0);
    tap_tick(MC_SC_SPACE);
    CHECK_EQ(s_sent.cmd, 0xf);
    CHECK(t->health >= 0);
    // (without a castle the respawn request marks the level lost: status |= 0xc)
    CHECK_EQ(lrec()->status & 0xc, 0xc);
    lrec()->status = 0;
    // Shift+C (gate) marks the level won, Esc then leaves it (cmd 0x1b -> status 10)
    input_key_event(MC_SC_LSHIFT, true);
    tap_tick(MC_SC_C);
    input_key_event(MC_SC_LSHIFT, false);
    CHECK_EQ(lrec()->status & 2, 2);
    tap_tick(MC_SC_ESC);
    CHECK_EQ(s_sent.cmd, 0x1b);
    CHECK_EQ(lrec()->status, 10);
    // Esc otherwise: cmd 0x1d (leave)
    lrec()->status = 0;
    lrec()->active = 1;
    tap_tick(MC_SC_ESC);
    CHECK_EQ(s_sent.cmd, 0x1d);
    CHECK_EQ(lrec()->status, 8);
    lrec()->status = 0;
    lrec()->active = 1;
    // Shift+R restarts the level, Shift+Q quits
    input_key_event(MC_SC_LSHIFT, true);
    tap_tick(MC_SC_R);
    CHECK_EQ(lrec()->status & 0xc, 0xc);
    lrec()->status = 0;
    tap_tick(MC_SC_Q);
    input_key_event(MC_SC_LSHIFT, false);
    CHECK_EQ(s_sent.cmd, 2);
    CHECK_EQ(lrec()->quit, 1);

    // a computer-controlled local player: only F1 (held) is read
    lrec()->is_computer = 1;
    clear_packet();
    input_key_event(MC_SC_UP, true);
    player_local_input();
    CHECK_EQ(lpk()->cmd, 0);
    input_key_event(MC_SC_F1, true);
    player_local_input();
    CHECK_EQ(lpk()->cmd, 2);
    clear_packet();
    lrec()->is_computer = 0;
    // mode 1: Enter or both buttons leave it (cmd 0x14 arg 0)
    input_reset();
    lrec()->input_mode = 1;
    input_key_event(MC_SC_ENTER, true);
    player_local_input();
    CHECK_EQ(lpk()->cmd, 0x14); CHECK_EQ(lpk()->arg, 0); CHECK_EQ(g_key_last, 0);
    clear_packet();
    input_mouse_button(0, true); input_mouse_button(1, true);
    player_local_input();
    CHECK_EQ(lpk()->cmd, 0x14); CHECK_EQ(g_mouse_click_left, 0); CHECK_EQ(g_mouse_click_right, 0);
    clear_packet();
    lrec()->input_mode = 5;                                         // unknown mode: nothing
    input_key_event(MC_SC_ENTER, true);
    player_local_input();
    CHECK_EQ(lpk()->cmd, 0);
    lrec()->input_mode = 0;

    g_hook_input_platform = nullptr;
    g_hook_input_joystick_poll = nullptr;
    g_hook_player_local_input = nullptr;
    g_cfg->save_str_a[0] = 0;
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    g_cfg->substeps = 0;
    input_reset();
    std::printf("scripted session on level 38: done (%d joystick polls)\n", s_joystick_polls);
}

// ---- 4. the movie ------------------------------------------------------------------------------

struct Movie {
    std::vector<CmdPacket> pk;
    int players = 4;
    int ticks() const { return (int)((pk.size() + players - 1) / players); }
    const CmdPacket *at(int tick, int player) const {
        size_t i = (size_t)tick * players + player;
        return i < pk.size() ? &pk[i] : nullptr;
    }
};
static Movie s_movie;
static int s_movie_tick = 0;
static std::vector<int> s_pre_x[255], s_pre_y[255];     // mouse positions per steer value (index v + 127)
static bool s_keys[4];                                  // Up, Down, Left, Right currently held
static int s_ok[256], s_bad[256];                       // per recorded command
static int s_mode_ticks[8];
static int s_bad_printed = 0;
static int s_open_enter = 0, s_open_buttons = 0, s_fire_clicks = 0, s_no_mouse_pos = 0;

static void build_steer_preimages(int mode) {
    const uint16_t mode0 = g_video_mode_flags;
    g_video_mode_flags = (uint16_t)mode;
    const int max_y = mode == 1 ? 0x190 : 0x1de;        // driver range / handler clamp
    for (auto &v : s_pre_x) v.clear();
    for (auto &v : s_pre_y) v.clear();
    for (int m = 0; m <= 0x27e; m++) {
        clear_packet();
        g_mouse_x = (int16_t)m; g_mouse_y = 0;
        player_mouse_steer();
        s_pre_x[lpk()->steer_x + 127].push_back(m);
    }
    for (int m = 0; m <= max_y; m++) {
        clear_packet();
        g_mouse_x = 0; g_mouse_y = (int16_t)m;
        player_mouse_steer();
        s_pre_y[lpk()->steer_y + 127].push_back(m);
    }
    clear_packet();
    g_video_mode_flags = mode0;
}

// The book cell whose click yields book slot `slot` (what cmd 0x15 / 0x16 carry), or -1.
static int cell_for_slot(int slot) {
    const PlayerBlock *P = player_block(lthing());
    for (int cell = 0; cell < 24; cell++) {
        int want = (int16_t)P->spell_thing[g_input_book_order[cell]];
        if (want == 0) continue;
        for (int i = 0; i < 24; i++) {
            if (P->spell_slot[i] == want) {
                if (i == slot) return cell;
                break;
            }
        }
    }
    return -1;
}

static void set_key(int idx, int sc, bool want) {
    if (s_keys[idx] == want) return;
    input_key_event(sc, want);
    s_keys[idx] = want;
}

// Called in place of player_local_input by game_tick_sim: reconstruct a device state for the
// recorded packet of this tick, run the real input code, compare, then let the recorded packets
// drive the simulation.
static void movie_hook() {
    const CmdPacket *rec = s_movie.at(s_movie_tick, 0);
    if (!rec) return;
    const int mode = lrec()->input_mode;
    s_mode_ticks[mode & 7]++;
    bool have_state = true;

    if (mode == 0) {
        // steering -> a mouse position; the arrow keys follow the key bits (press / release events)
        static const std::vector<int> none;
        const std::vector<int> &xs = rec->steer_x == -128 ? none : s_pre_x[rec->steer_x + 127];
        const std::vector<int> &ys = rec->steer_y == -128 ? none : s_pre_y[rec->steer_y + 127];
        if (xs.empty() || ys.empty()) { have_state = false; s_no_mouse_pos++; }
        else input_mouse_move(xs[xs.size() / 2], ys[ys.size() / 2]);
        const bool key_cmd = rec->cmd == 6;
        set_key(0, MC_SC_UP, key_cmd && (rec->bits & 1));
        set_key(1, MC_SC_DOWN, key_cmd && (rec->bits & 2));
        set_key(2, MC_SC_LEFT, key_cmd && (rec->bits & 4));
        set_key(3, MC_SC_RIGHT, key_cmd && (rec->bits & 8));
        // fire bits -> a fresh click of that button (release + press)
        if (g_mouse_held_left) input_mouse_button(0, false);
        if (g_mouse_held_right) input_mouse_button(1, false);
        if (key_cmd && (rec->bits & 0x10)) { input_mouse_button(0, true); s_fire_clicks++; }
        if (key_cmd && (rec->bits & 0x20)) { input_mouse_button(1, true); s_fire_clicks++; }
        if (rec->cmd == 0x14 && rec->arg == 2) {
            // the book: Enter or both buttons, alternately
            if (((s_open_enter + s_open_buttons) & 1) == 0) { input_key_event(MC_SC_ENTER, true); s_open_enter++; }
            else { input_mouse_button(0, true); input_mouse_button(1, true); s_open_buttons++; }
        } else if (rec->cmd == 2) {
            input_key_event(MC_SC_LSHIFT, true);
            input_key_event(MC_SC_Q, true);
        } else if (rec->cmd != 0 && rec->cmd != 6) {
            have_state = false;
        }
    } else if (mode == 2) {
        if (g_mouse_held_left) input_mouse_button(0, false);
        if (g_mouse_held_right) input_mouse_button(1, false);
        for (int k = 0; k < 4; k++) {
            static const int sc[4] = {MC_SC_UP, MC_SC_DOWN, MC_SC_LEFT, MC_SC_RIGHT};
            set_key(k, sc[k], false);
        }
        int cell = s_movie_tick % 24;                   // hover somewhere on the book
        if (rec->cmd == 0x15 || rec->cmd == 0x16) cell = cell_for_slot(rec->arg);
        else if (rec->cmd != 0) have_state = false;
        if (cell < 0) have_state = false;
        else {
            int x, y;
            input_book_cell_origin(cell, &x, &y);
            input_mouse_move(x + 7 + (s_movie_tick * 13) % 50, y + 3 + (s_movie_tick * 7) % (input_book_cell_h() - 6));
            input_book_update_selection();
            if (rec->cmd == 0x15) input_mouse_button(0, true);
            if (rec->cmd == 0x16) input_mouse_button(1, true);
        }
    } else {
        have_state = false;
    }

    clear_packet();
    player_local_input();
    const bool same = have_state && std::memcmp(lpk(), rec, sizeof *rec) == 0;
    if (same) s_ok[rec->cmd]++;
    else {
        s_bad[rec->cmd]++;
        if (s_bad_printed++ < 12) {
            const CmdPacket *g = lpk();
            std::printf("  tick %d mode %d%s: recorded cmd %02x arg %02x p2 %02x steer %d,%d bits %02x | produced cmd %02x arg %02x p2 %02x steer %d,%d bits %02x\n",
                        s_movie_tick, mode, have_state ? "" : " (no device state found)", rec->cmd, rec->arg, rec->pad2,
                        rec->steer_x, rec->steer_y, rec->bits, g->cmd, g->arg, g->pad2, g->steer_x, g->steer_y, g->bits);
        }
    }
    // releases of the one-tick keys arrive after the input code ran
    if (g_key_down[MC_SC_ENTER]) input_key_event(MC_SC_ENTER, false);
    if (g_key_down[MC_SC_Q]) input_key_event(MC_SC_Q, false);
    if (g_key_down[MC_SC_LSHIFT]) input_key_event(MC_SC_LSHIFT, false);
    // the recording drives the simulation
    for (int p = 0; p < s_movie.players; p++) {
        const CmdPacket *r = s_movie.at(s_movie_tick, p);
        if (r) g_state->commands[p] = *r;
    }
}

static bool load_movie(const char *game_dir) {
    char path[1024];
    mc_blob f;
    mc_path_join(path, sizeof path, game_dir, "movie/mvi00000.dat");
    if (!mc_read_file(path, &f)) { std::printf("movie: %s missing\n", path); return false; }
    s_movie.pk.resize(f.len / sizeof(CmdPacket));
    std::memcpy(s_movie.pk.data(), f.data, s_movie.pk.size() * sizeof(CmdPacket));
    mc_blob_free(&f);
    return true;
}

// The statistical test of the steering formula: a steer value that N mouse pixels map onto must
// turn up N times as often in the recording. x: 2 or 3 pixels per value (ratio 1.5); y: 1 or 2
// pixels per value (ratio 2) under the formula of the mode the recording was made in.
static double steer_ratio(const std::vector<int> *pre, int lo_n, int hi_n, bool y_axis) {
    long values[2] = {0, 0}, hits[2] = {0, 0};
    int count[255] = {};
    for (size_t i = 0; i < s_movie.pk.size(); i += (size_t)s_movie.players) {
        const CmdPacket &p = s_movie.pk[i];
        if ((p.cmd != 0 && p.cmd != 6) || (p.steer_x == 0 && p.steer_y == 0)) continue;
        const int v = y_axis ? p.steer_y : p.steer_x;
        if (v >= -127) count[v + 127]++;
    }
    for (int v = -120; v <= 120; v++) {
        if (v == 0) continue;
        int n = (int)pre[v + 127].size();
        int g = n == lo_n ? 0 : n == hi_n ? 1 : -1;
        if (g < 0) continue;
        values[g]++;
        hits[g] += count[v + 127];
    }
    if (values[0] == 0 || values[1] == 0 || hits[0] == 0) return 0.0;
    double per_lo = (double)hits[0] / (double)values[0], per_hi = (double)hits[1] / (double)values[1];
    std::printf("    %ld values with %d pixel(s): %.1f hits each; %ld values with %d pixels: %.1f hits each; ratio %.2f\n",
                values[0], lo_n, per_lo, values[1], hi_n, per_hi, per_hi / per_lo);
    return per_hi / per_lo;
}

static void test_movie(const char *game_dir) {
    if (!load_movie(game_dir)) { g_fail++; return; }
    CHECK(sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat"));
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    g_cfg->substeps = 0;
    s_movie.players = g_state->player_count;
    CHECK_EQ(s_movie.players, 4);
    CHECK_EQ(g_state->local_player, 0);
    CHECK_EQ(lrec()->is_computer, 0);
    CHECK_EQ(lrec()->input_mode, 0);
    const int ticks = s_movie.ticks();
    std::printf("movie: %zu packets, %d ticks, %d players\n", s_movie.pk.size(), ticks, s_movie.players);
    CHECK_EQ(ticks, 8551);
    // The snapshot was written after the input code of its tick ran: the packet waiting in the
    // state is the first packet of the recording.
    CHECK(std::memcmp(&g_state->commands[0], &s_movie.pk[0], sizeof(CmdPacket)) == 0);

    // -- the steering statistics (decides which screen mode the recording was made in) --
    std::printf("  steering histogram of the recording against the port's formula:\n");
    build_steer_preimages(1);
    int unreachable_lo = 0, unreachable_hi = 0;
    auto unreachable = [&]() {
        int n = 0;
        for (size_t i = 0; i < s_movie.pk.size(); i += (size_t)s_movie.players) {
            const CmdPacket &p = s_movie.pk[i];
            if (p.steer_x == -128 || p.steer_y == -128 || s_pre_x[p.steer_x + 127].empty() || s_pre_y[p.steer_y + 127].empty()) n++;
        }
        return n;
    };
    unreachable_lo = unreachable();
    std::printf("   x (both modes):\n");
    double rx = steer_ratio(s_pre_x, 2, 3, false);
    std::printf("   y, 640x400 formula (g_video_mode_flags == 1):\n");
    double ry_lo = steer_ratio(s_pre_y, 1, 2, true);
    build_steer_preimages(8);
    unreachable_hi = unreachable();
    std::printf("   y, 640x480 formula:\n");
    double ry_hi = steer_ratio(s_pre_y, 1, 2, true);
    std::printf("  packets whose steering no mouse position yields: %d (640x400), %d (640x480)\n", unreachable_lo, unreachable_hi);
    // The pointer is centred when the book closes (player_set_input_mode -> input_mouse_center):
    // the first flight packet after a pick carries steer 0, 0 unless the mouse moved in that tick.
    {
        int closes = 0, centred = 0, hover_steer = 0, book = 0;
        bool open = false;
        for (int tk = 0; tk < ticks; tk++) {
            const CmdPacket &p = *s_movie.at(tk, 0);
            if (open && p.cmd == 0 && (p.steer_x != 0 || p.steer_y != 0)) hover_steer++;
            if (open) book++;
            if (p.cmd == 0x14 && p.arg == 2) open = true;
            if (p.cmd == 0x15 || p.cmd == 0x16) {
                open = false;
                closes++;
                const CmdPacket *next = s_movie.at(tk + 1, 0);
                if (next && next->steer_x == 0 && next->steer_y == 0) centred++;
            }
        }
        std::printf("  book closed %d x, next packet has steer 0,0 %d x; %d book ticks, %d of them with steering\n",
                    closes, centred, book, hover_steer);
        CHECK_EQ(closes, 54);
        CHECK(centred >= 45);
        CHECK_EQ(hover_steer, 0);
    }
    CHECK(rx > 1.3 && rx < 1.7);            // expected 1.5
    CHECK(ry_lo > 1.7 && ry_lo < 2.3);      // expected 2.0
    CHECK(ry_hi > 0.7 && ry_hi < 1.3);      // the other mode's formula does not fit (would be 2.0)
    CHECK_EQ(unreachable_lo, 0);

    // -- the reconstruction, in the mode the statistics point at --
    const uint16_t mode0 = g_video_mode_flags;
    g_video_mode_flags = 1;
    build_steer_preimages(1);
    input_reset();
    std::memset(s_keys, 0, sizeof s_keys);
    clear_packet();
    g_hook_player_local_input = movie_hook;
    for (s_movie_tick = 0; s_movie_tick < ticks; s_movie_tick++) game_tick_sim();
    g_hook_player_local_input = nullptr;
    g_video_mode_flags = mode0;

    int ok = 0, bad = 0;
    std::printf("  human player's packets reproduced by player_local_input from a reconstructed device state:\n");
    for (int c = 0; c < 256; c++) {
        if (s_ok[c] + s_bad[c] == 0) continue;
        std::printf("    cmd 0x%02x: %5d of %5d\n", c, s_ok[c], s_ok[c] + s_bad[c]);
        ok += s_ok[c]; bad += s_bad[c];
    }
    std::printf("  total %d of %d reproduced, %d not; ticks in flight mode %d, in the spell book %d, other %d\n",
                ok, ok + bad, bad, s_mode_ticks[0], s_mode_ticks[2],
                s_mode_ticks[1] + s_mode_ticks[3] + s_mode_ticks[4] + s_mode_ticks[5] + s_mode_ticks[6] + s_mode_ticks[7]);
    std::printf("  book opened by Enter %d x, by both buttons %d x; %d fire clicks; %d packets without a mouse position\n",
                s_open_enter, s_open_buttons, s_fire_clicks, s_no_mouse_pos);
    CHECK_EQ(ok + bad, 8551);
    CHECK_EQ(bad, 0);
    CHECK_EQ(lrec()->quit, 1);                          // the last packet is the quit command
    input_reset();
}

// Port (settings.h keys_wasd / keys_book_tab): W / S / A / D fly like the arrows, Tab opens and closes the
// book like Enter. Off (the default, the original) they do nothing in flight; Alt+S stays the quick save.
static void test_flight_keys() {
    input_reset();
    g_video_mode_flags = 8;
    g_cfg->flags = 0;
    CHECK(sim_load_level(38));
    g_hook_player_local_input = capture_hook;
    g_hook_input_platform = platform_hook;
    g_hook_input_joystick_poll = joystick_hook;
    input_mouse_center();
    tick();                                                         // the join
    tick();
    const int keys[4] = {MC_SC_W, MC_SC_S, MC_SC_A, MC_SC_D};
    const uint8_t bits[4] = {1, 2, 4, 8};
    // original: nothing
    for (int k = 0; k < 4; k++) {
        input_key_event(keys[k], true);
        tick();
        CHECK_EQ(s_sent.cmd, 0); CHECK_EQ(s_sent.bits, 0);
        input_key_event(keys[k], false);
    }
    tap_tick(MC_SC_TAB);
    CHECK_EQ(s_sent.cmd, 0);
    CHECK_EQ(lrec()->input_mode, 0);
    // on
    g_settings.keys_wasd = true;
    g_settings.keys_book_tab = true;
    for (int k = 0; k < 4; k++) {
        input_key_event(keys[k], true);
        tick();
        CHECK_EQ(s_sent.cmd, 6); CHECK_EQ(s_sent.bits, bits[k]);
        input_key_event(keys[k], false);
    }
    input_key_event(MC_SC_W, true);                                 // with an arrow: the same bits, or-ed
    input_key_event(MC_SC_RIGHT, true);
    tick();
    CHECK_EQ(s_sent.bits, 1 | 8);
    input_key_event(MC_SC_W, false);
    input_key_event(MC_SC_RIGHT, false);
    tick();
    CHECK_EQ(s_sent.bits, 0);
    input_key_event(MC_SC_ALT, true);                               // Alt+S: the quick save, no movement
    tap_tick(MC_SC_S);
    CHECK_EQ(s_sent.cmd, 0x0a); CHECK_EQ(s_sent.bits, 0);
    input_key_event(MC_SC_ALT, false);
    tick();
    tap_tick(MC_SC_TAB);                                            // the book opens ...
    CHECK_EQ(s_sent.cmd, 0x14); CHECK_EQ(s_sent.arg, 2);
    CHECK_EQ(lrec()->input_mode, 2);
    tap_tick(MC_SC_TAB);                                            // ... and closes
    CHECK_EQ(s_sent.cmd, 0x14); CHECK_EQ(s_sent.arg, 0);
    CHECK_EQ(lrec()->input_mode, 0);
    g_settings = PortSettings{};
    std::printf("flight keys: W/S/A/D and Tab ignored by default; with keys_wasd / keys_book_tab they fly and open / close the book\n");
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    input_register_handlers();
    CHECK(g_hook_player_local_input == player_local_input);

    test_feeding();
    if (!sim_load_level(38)) { std::printf("level 38 failed to load\n"); return 2; }
    game_tick_sim();                                    // the join command spawns the players
    test_queue_and_steer();
    test_session(game_dir);
    test_movie(game_dir);
    test_flight_keys();

    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}

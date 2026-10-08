// Local input of the human player: the device state the original's interrupt handlers maintain, the
// feeding API the platform layer calls instead of those handlers, and the per-tick translation of that
// state into the local player's command packet (carpet.exe 0x15590..0x17a80).
// Owner: input.cpp. Report with the full binding table: docs/analysis/port_input.md.
//
// Platform layer, per frame (details in the report):
//   1. for every OS event: input_key_event / input_mouse_move / input_mouse_button
//   2. while the spell book is open (players[local].input_mode == 2): input_book_update_selection()
//   3. game_tick_sim()   -> calls player_local_input() through g_hook_player_local_input
// Nothing has to be cleared by the platform: the consumer clears the events it uses.
#pragma once
#include <cstdint>
#include "mc_types.h"
#include "text.h"

// ---- device state (plain globals with the original meaning) ------------------------------------
// Keyboard, maintained by input_keyboard_isr_4fa28 (int 9).
extern uint8_t  g_key_down[128];        // 0x12ee20: 1 while the key with this set-1 scancode is down
extern uint8_t  g_key_last;             // DAT_0012eea0: last byte read from the keyboard: a make code
                                        // (1..0x7f), a break code (make | 0x80), 0xe0, or 0x80 for a
                                        // swallowed fake shift. Consumers compare it with make codes and
                                        // write 0 once they used it.
extern uint8_t  g_key_prev_raw;         // DAT_0012e396: the byte before (0xe0 prefix detection)
extern uint8_t  g_key_first;            // DAT_0012e397: first make code since it was last zeroed
// Mouse, maintained by mouse_event_callback_5b86c (int 33h event handler). Coordinates are in the
// 640-wide virtual screen: 640 x 400 when g_video_mode_flags == 1 (320x200), 640 x 480 otherwise.
extern int16_t  g_mouse_x, g_mouse_y;               // DAT_0009e5dc / DAT_0009e5de
extern int16_t  g_mouse_click_x, g_mouse_click_y;   // DAT_0009e5d8 / DAT_0009e5da: position of the last click event
extern uint16_t g_mouse_click_left;     // DAT_0012ee0e: left button went down (cleared by the consumer)
extern uint16_t g_mouse_click_right;    // DAT_0012ee0c
extern uint16_t g_mouse_click_middle;   // DAT_0012ee0a
extern uint16_t g_mouse_held_left;      // DAT_0012ee14: 1 while the button is down
extern uint16_t g_mouse_held_right;     // DAT_0012ee12
extern uint16_t g_mouse_held_middle;    // DAT_0012ee10
extern int16_t  g_mouse_dbl_timer;      // DAT_0012ee06: loaded with the double-click window on a press
extern int16_t  g_mouse_dbl_click;      // DAT_0012ee08: 1 when a press arrived while the timer ran
extern uint32_t g_mouse_event_mask;     // DAT_0012ede0: int 33h condition mask of the last event
extern uint8_t  g_mouse_moved;          // DAT_0009e5e3: set by every mouse event
extern uint32_t g_mouse_present;        // DAT_0009e5e4: 1 once the driver was found (the port starts with 1)

// Sound / music switches the function keys toggle (the sound system is not ported: the platform
// sets "available" when it can play something and reads the "on" flags).
extern uint8_t  g_sound_available;      // DAT_0009e320
extern uint8_t  g_sound_on;             // DAT_0009e321 (F1)
extern uint8_t  g_music_available;      // DAT_0009e30c
extern uint8_t  g_music_on;             // DAT_0009e30d (F2)
// DAT_000adf48 != 0: the sky texture is loaded (gate of F6). The port's g_sky is an array, so 1.
extern uint8_t  g_sky_available;
// Spell-book cell size: width / height of sprite 3 of the HUD sprite table DAT_000adf94, in the
// 640-wide virtual screen. The table is data/hspr0-0.tab in the 640x480 mode (64 x 37) and
// data/mspr0-0.tab with every size doubled at load in the 320x200 mode (32 x 18 -> 64 x 36;
// sprite_tab_relocate_62a80). 0 = use those shipped values by g_video_mode_flags; the HUD owner may
// store the values of the table it actually loaded.
extern uint8_t  g_book_cell_w;          // [DAT_000adf94] + 0x16
extern uint8_t  g_book_cell_h;          // [DAT_000adf94] + 0x17
int input_book_cell_w();
int input_book_cell_h();

// ---- feeding API (what the interrupt handlers did) ---------------------------------------------
// input_keyboard_install_4fb46 + the start values of the mouse variables: everything released.
void input_reset();
// input_keyboard_isr_4fa28 for one byte from port 0x60 (make code, make | 0x80, or the 0xe0 prefix).
void input_key_raw(uint8_t byte);
// One key transition. `scancode` is the set-1 make code 1..0x7f (table below); add MC_SC_EXT (0x100)
// for an "extended" key if the 0xe0 prefix byte should be fed first as the hardware does (it makes no
// difference to the game: the prefix only clears g_key_down[0x60]). Forward auto-repeat as repeated
// "down" events: the game relies on typematic repeat for held [ ] and for text entry.
void input_key_event(int scancode, bool down);
// mouse_event_callback_5b86c: `mask` is the int 33h condition mask (1 moved, 2 / 4 left pressed /
// released, 8 / 0x10 right, 0x20 / 0x40 middle), x / y the position in the 640-wide virtual screen
// (the driver scaling `>> 3` of the 640x480 mode is the platform's job).
void input_mouse_event(unsigned mask, int x, int y);
// Convenience wrappers around input_mouse_event. Coordinates outside the virtual screen are clamped
// like the mouse driver's range does (0..640, 0..400 / 0..480) before the handler's own clamp.
void input_mouse_move(int x, int y);
void input_mouse_button(int button, bool down);      // 0 left, 1 right, 2 middle; at the current position
// input_mouse_set_pos_4a030 / input_mouse_center_4a000: the game moves the pointer itself when the
// spell book closes (player_set_input_mode). The hook lets the platform warp the OS pointer.
void input_mouse_set_pos(int x, int y);
void input_mouse_center();
extern void (*g_hook_input_mouse_warp)(int x, int y);

// ---- the per-tick input code -------------------------------------------------------------------
// Installs g_hook_player_local_input (player.h). No Thing handlers are bound here.
void input_register_handlers();

void player_local_input();                      // player_local_input_16660
void player_function_keys();                    // player_function_keys_156b0
void player_mouse_steer();                      // player_mouse_steer_15590
void player_queue_command(int cmd, int arg);    // player_queue_command_17270(cmd, arg)
void creature_kill_all();                       // creature_kill_all_17ff0 (cheat command 0x1e / 7)

// The spell-book hit test of the HUD (render_frame_1fab0, 0x20b44..0x20d90, input mode 2): sets
// Config.spell_slot to the book cell under the mouse (0xff = none). The original does it while it
// draws the book; until the HUD is ported the platform calls this before the tick.
void input_book_update_selection();
// Top-left corner of book cell 0..23 in the virtual screen (the layout the hit test uses).
void input_book_cell_origin(int cell, int *x, int *y);

// input_snapshot_34060 / input_changed_34090: "did the user touch anything" latch of the movie
// player (connect input_changed to g_hook_demo_input_changed, call input_snapshot when a movie starts).
void input_snapshot();
int  input_changed();

// ---- hooks for the parts that are not ported (null = skipped) -----------------------------------
enum InputPlatformRequest {
    INPUT_REQ_SOUND_STOP_ALL,       // sound_stop_all_5c040()
    INPUT_REQ_MUSIC_STOP,           // music_stop_1f960()
    INPUT_REQ_MUSIC_PLAY,           // music_play_track_5c0a0(arg = level track, GameState+0x240)
    INPUT_REQ_TOGGLE_RESOLUTION,    // video_toggle_resolution_33600() (R)
    INPUT_REQ_3D_MODE_ON,           // render_mapmode_palette_2ff50() (F10: mode_3d 0 -> 1)
    INPUT_REQ_3D_MODE_RESTORE,      // video_restore_mode_2ff10()     (F10: mode_3d 1 -> 2)
};
extern void (*g_hook_input_platform)(InputPlatformRequest what, int arg);
// input_joystick_poll_5a4e0(cursor_mode, x0, y0, x1, y1, step_x, step_y): may press the arrow keys /
// mouse buttons in the device state above or move the pointer inside the rectangle (book mode).
extern void (*g_hook_input_joystick_poll)(int cursor_mode, int x0, int y0, int x1, int y1, int step_x, int step_y);

// ---- language strings (data/etext.dat ...) -----------------------------------------------------
// The table lives in text.h (text_load / text_get) since round 4; these two names are kept for the
// callers of round 3. The indices of the function-key notices stay here.
inline bool        input_text_load(const char *game_dir, int language) { return text_load(game_dir, language); }
inline const char *input_text(int index) { return text_get(index); }
enum {
    MC_TEXT_SHADOWS_ON = 37, MC_TEXT_SHADOWS_OFF, MC_TEXT_REFLECTIONS_ON, MC_TEXT_REFLECTIONS_OFF,
    MC_TEXT_SOFTEN_ON, MC_TEXT_SOFTEN_OFF, MC_TEXT_SKY_ON, MC_TEXT_SKY_OFF, MC_TEXT_ICONS_ON,
    MC_TEXT_ICONS_OFF, MC_TEXT_STEREO_ON, MC_TEXT_STEREO_OFF, MC_TEXT_BLUR_LIGHT, MC_TEXT_BLUR_HEAVY,
    MC_TEXT_BLUR_OFF,
    MC_TEXT_SPEED_NORMAL = 63, MC_TEXT_SPEED_FAST, MC_TEXT_SPEED_SUPER_FAST,
    MC_TEXT_SOUND_ON = 75, MC_TEXT_SOUND_OFF, MC_TEXT_MUSIC_ON, MC_TEXT_MUSIC_OFF,
};

// ---- set-1 scancodes the game uses -------------------------------------------------------------
enum : int {
    MC_SC_ESC = 0x01,
    MC_SC_1 = 0x02, MC_SC_2, MC_SC_3, MC_SC_4, MC_SC_5, MC_SC_6, MC_SC_7, MC_SC_8, MC_SC_9, MC_SC_0 = 0x0b,
    MC_SC_BACKSPACE = 0x0e, MC_SC_TAB = 0x0f,
    MC_SC_Q = 0x10, MC_SC_W, MC_SC_E, MC_SC_R, MC_SC_T, MC_SC_Y, MC_SC_U, MC_SC_I, MC_SC_O, MC_SC_P,
    MC_SC_LBRACKET = 0x1a, MC_SC_RBRACKET = 0x1b, MC_SC_ENTER = 0x1c, MC_SC_CTRL = 0x1d,
    MC_SC_A = 0x1e, MC_SC_S, MC_SC_D, MC_SC_F, MC_SC_G, MC_SC_H, MC_SC_J, MC_SC_K, MC_SC_L,
    MC_SC_LSHIFT = 0x2a,
    MC_SC_Z = 0x2c, MC_SC_X, MC_SC_C, MC_SC_V, MC_SC_B, MC_SC_N, MC_SC_M,
    MC_SC_RSHIFT = 0x36, MC_SC_ALT = 0x38, MC_SC_SPACE = 0x39,
    MC_SC_F1 = 0x3b, MC_SC_F2, MC_SC_F3, MC_SC_F4, MC_SC_F5, MC_SC_F6, MC_SC_F7, MC_SC_F8, MC_SC_F9, MC_SC_F10 = 0x44,
    MC_SC_UP = 0x48, MC_SC_LEFT = 0x4b, MC_SC_RIGHT = 0x4d, MC_SC_DOWN = 0x50,
    MC_SC_EXT = 0x100,      // flag for input_key_event: send the 0xe0 prefix first
};
static_assert(MC_SC_9 == 0x0a && MC_SC_P == 0x19 && MC_SC_I == 0x17 && MC_SC_L == 0x26 && MC_SC_H == 0x23);
static_assert(MC_SC_V == 0x2f && MC_SC_M == 0x32 && MC_SC_F7 == 0x41 && MC_SC_F9 == 0x43);

// Modifier / context of a binding.
enum : uint8_t {
    MC_BIND_PLAIN = 0,          // neither Ctrl, Alt nor Shift held
    MC_BIND_CTRL  = 1,          // g_key_down[0x1d] (either Ctrl: the right one is e0 1d)
    MC_BIND_ALT   = 2,          // g_key_down[0x38] (either Alt)
    MC_BIND_SHIFT = 3,          // g_key_down[0x2a] or g_key_down[0x36]
    MC_BIND_ANY   = 4,          // modifiers are not looked at
};
enum : uint8_t {
    MC_BIND_HELD     = 1,       // level triggered: read from g_key_down every tick
    MC_BIND_FLIGHT   = 2,       // input mode 0 (and 4)
    MC_BIND_BOOK     = 4,       // input mode 2 (spell book)
    MC_BIND_MODE1    = 8,       // input mode 1
    MC_BIND_TEXT     = 0x10,    // input mode 3 (chat line)
    MC_BIND_CHEAT    = 0x20,    // needs the cheat gate Config.flags & 0x8000 (chat line "RATTY")
    MC_BIND_SINGLE   = 0x40,    // not in games with Config.flags & 0x10 (network / custom)
    MC_BIND_WATCH    = 0x80,    // only while the local player is computer controlled (PlayerRec.is_computer)
};
struct InputBinding {
    uint8_t     scancode;       // set-1 make code (g_key_last / g_key_down index)
    uint8_t     modifier;       // MC_BIND_PLAIN ..
    uint8_t     context;        // MC_BIND_* flags
    uint8_t     cmd;            // command queued (0 = none: a local effect)
    const char *key;
    const char *effect;
};
// Every key the in-game input code reads, in the order the code tests them. Mouse: see the report.
inline constexpr InputBinding g_input_bindings[] = {
    // flight (mode 0): held keys -> CmdPacket.bits through command 6
    {MC_SC_UP,       MC_BIND_ANY,   MC_BIND_HELD | MC_BIND_FLIGHT, 6,    "Up",        "fly faster (bits 1)"},
    {MC_SC_DOWN,     MC_BIND_ANY,   MC_BIND_HELD | MC_BIND_FLIGHT, 6,    "Down",      "fly slower / backwards (bits 2)"},
    {MC_SC_LEFT,     MC_BIND_ANY,   MC_BIND_HELD | MC_BIND_FLIGHT, 6,    "Left",      "slide left (bits 4)"},
    {MC_SC_RIGHT,    MC_BIND_ANY,   MC_BIND_HELD | MC_BIND_FLIGHT, 6,    "Right",     "slide right (bits 8)"},
    // flight: key presses
    {MC_SC_1,        MC_BIND_PLAIN, MC_BIND_FLIGHT, 0x18, "1..0",      "quick-select spell 1..10 into the left hand (cmd 0x18, arg key - 1)"},
    {MC_SC_1,        MC_BIND_CTRL,  MC_BIND_FLIGHT, 0x19, "Ctrl+1..0", "quick-select spell 1..10 into the right hand (cmd 0x19)"},
    {MC_SC_ENTER,    MC_BIND_PLAIN, MC_BIND_FLIGHT, 0x14, "Enter",     "open the spell book (cmd 0x14 arg 2; not while dead)"},
    {MC_SC_SPACE,    MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x0f, "Space", "respawn when dead (cmd 0xf), otherwise leave a won level (cmd 0x1b)"},
    {MC_SC_ESC,      MC_BIND_PLAIN, MC_BIND_FLIGHT, 0x1d, "Esc",       "leave a won level (cmd 0x1b), otherwise leave the game (cmd 0x1d)"},
    {MC_SC_I,        MC_BIND_PLAIN, MC_BIND_FLIGHT, 0x10, "I",         "start a chat line (cmd 0x10)"},
    {MC_SC_LBRACKET, MC_BIND_PLAIN, MC_BIND_FLIGHT, 0,    "[",         "view size + 1 (max 0x28), not in a 3D mode"},
    {MC_SC_RBRACKET, MC_BIND_PLAIN, MC_BIND_FLIGHT, 0,    "]",         "view size - 1 (min 0x11), not in a 3D mode"},
    {MC_SC_R,        MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "R", "toggle 320x200 / 640x480 (if allowed and not in a 3D mode)"},
    {MC_SC_P,        MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_SINGLE, 0, "P", "pause (Config.paused ^= 1; stops sound and music)"},
    // function keys (modes 0, 1, 2)
    {MC_SC_F1,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F1",  "sound effects on / off"},
    {MC_SC_F2,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F2",  "music on / off"},
    {MC_SC_F3,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_SINGLE, 0, "F3", "game speed normal / fast / super fast (Config.substeps 0 / 1 / 2 = 1 / 4 / 16 updates per tick)"},
    {MC_SC_F4,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F4",  "soften (2x2 smoothing) on / off"},
    {MC_SC_F5,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F5",  "reflections on / off"},
    {MC_SC_F6,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F6",  "textured sky on / off"},
    {MC_SC_F7,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F7",  "shadows on / off"},
    {MC_SC_F8,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F8",  "icons and map on / off"},
    {MC_SC_F9,       MC_BIND_PLAIN, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0, "F9",  "speed blur off / light / heavy"},
    {MC_SC_F10,      MC_BIND_PLAIN, MC_BIND_FLIGHT, 0,    "F10",       "3D mode off / 1 (red-blue) / 2"},
    // Shift
    {MC_SC_Q,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x02, "Shift+Q", "quit to the menu (cmd 2)"},
    {MC_SC_R,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_SINGLE, 0, "Shift+R", "restart the level (status |= 0xc, own castle forgotten)"},
    {MC_SC_K,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_SINGLE, 0, "Shift+K", "kill the own wizard (health = -1)"},
    {MC_SC_L,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_SINGLE, 0, "Shift+L", "destroy the own castle (health = -1)"},
    {MC_SC_E,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_CHEAT, 0x1a, "Shift+E", "everybody quits (cmd 0x1a)"},
    {MC_SC_C,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_CHEAT, 0, "Shift+C", "mark the level as won (status |= 2)"},
    {MC_SC_F,        MC_BIND_SHIFT, MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_CHEAT, 0, "Shift+F", "mark the level as lost (status |= 4)"},
    // Alt
    {MC_SC_F1,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F1", "cheat 1: all spells (needs the player name \"chronicle\" or the cheat gate)"},
    {MC_SC_F2,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F2", "cheat 2: more mana"},
    {MC_SC_F3,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F3", "cheat 3: destroy all other wizards"},
    {MC_SC_F4,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F4", "cheat 4: destroy all other castles"},
    {MC_SC_F5,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F5", "cheat 5: destroy all other balloons"},
    {MC_SC_F6,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F6", "cheat 6: heal"},
    {MC_SC_F7,       MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK, 0x1e, "Alt+F7", "cheat 7: kill all creatures"},
    {MC_SC_V,        MC_BIND_ALT,   MC_BIND_FLIGHT | MC_BIND_MODE1 | MC_BIND_BOOK | MC_BIND_CHEAT, 0x04, "Alt+V", "toggle the debug overlay (cmd 4 arg 8: PlayerRec.flags ^= 8)"},
    {MC_SC_R,        MC_BIND_ALT,   MC_BIND_FLIGHT, 0x0c, "Alt+R",     "start recording a movie (cmd 0xc)"},
    {MC_SC_S,        MC_BIND_ALT,   MC_BIND_FLIGHT, 0x0a, "Alt+S",     "quick save (cmd 0xa)"},
    {MC_SC_H,        MC_BIND_ALT,   MC_BIND_FLIGHT, 0,    "Alt+H",     "toggle the interlaced stereo (VFX1 headset) option"},
    {MC_SC_M,        MC_BIND_ALT,   MC_BIND_FLIGHT, 0x04, "Alt+M",     "cmd 4 arg 0x10 (PlayerRec.flags ^= 0x10)"},
    {MC_SC_F,        MC_BIND_ALT,   MC_BIND_FLIGHT, 0x04, "Alt+F",     "cmd 4 arg 0x20 (PlayerRec.flags ^= 0x20)"},
    // spell book (mode 2)
    {MC_SC_1,        MC_BIND_ANY,   MC_BIND_BOOK,   0x17, "1..0",      "assign the spell under the pointer to quick-select key 1..10 (cmd 0x17)"},
    {MC_SC_ENTER,    MC_BIND_ANY,   MC_BIND_BOOK | MC_BIND_MODE1, 0x14, "Enter", "close the book / leave mode 1 (cmd 0x14 arg 0)"},
    // chat line (mode 3)
    {MC_SC_ENTER,    MC_BIND_ANY,   MC_BIND_TEXT,   0x13, "Enter",     "send the chat line (cmd 0x13)"},
    {MC_SC_BACKSPACE, MC_BIND_ANY,  MC_BIND_TEXT,   0x11, "Backspace", "delete the last character (cmd 0x11 arg 8)"},
    {MC_SC_A,        MC_BIND_ANY,   MC_BIND_TEXT,   0x11, "A-Z 0-9 Space", "append the character (cmd 0x11 arg = g_input_scancode_ascii[scancode])"},
    // computer-controlled local player (attract mode)
    {MC_SC_F1,       MC_BIND_ANY,   MC_BIND_HELD | MC_BIND_WATCH, 0x02, "F1", "quit (cmd 2)"},
};

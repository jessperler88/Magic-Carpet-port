// Game controller support (port round 7, task E; docs/analysis/port_settings.md). Port-only: the original
// reads a PC game-port joystick (input_joystick_poll_5a4e0: axes -> arrow keys or the pointer, two
// buttons -> the mouse buttons). Here a modern pad (SDL_GameController: Xbox / PlayStation / Switch
// layouts) is mapped onto the same device state the keyboard and the mouse feed (input.h), so the game
// code sees nothing new:
//
//   flight     right stick = the pointer's offset from the screen centre (the mouse steering), left stick =
//              Up / Down (faster / slower) and Left / Right (slide) as held arrow keys, buttons = bound
//              actions (keys, mouse buttons = casting, quick-select cycling, save / load)
//   spell book left stick / d-pad move the pointer, A = left click (spell -> left hand), X = right click,
//   (and map)  B = Enter (close); the triggers keep their bindings
//   menus      left stick / d-pad move the pointer, A = left click, X = right click, B = Esc
//
// The mapping (GamepadMapper) is pure and SDL-free (config_test exercises it); gamepad_open / _poll are
// the SDL half (hot-plug: the first attached game controller is used, re-scanned when it goes away).
#pragma once
#include <cstdint>

// A key with modifiers: SDL scancode (SDL_Scancode value) + KEYMOD_* bits. scancode 0 = unbound.
enum : uint8_t { KEYMOD_CTRL = 1, KEYMOD_SHIFT = 2, KEYMOD_ALT = 4 };
struct KeyChord {
    int     scancode = 0;
    uint8_t mods = 0;
    bool operator==(const KeyChord &o) const { return scancode == o.scancode && mods == o.mods; }
};

enum PadInput : int {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LSTICK, PAD_RSTICK, PAD_LB, PAD_RB,
    PAD_DPAD_UP, PAD_DPAD_DOWN, PAD_DPAD_LEFT, PAD_DPAD_RIGHT,
    PAD_LT, PAD_RT,                         // the triggers, as buttons (pad.trigger_threshold)
    PAD_INPUT_COUNT
};
// Config names of the inputs ("a", "b", ..., "lt", "rt"); index = PadInput.
extern const char *const g_pad_input_names[PAD_INPUT_COUNT];

struct PadAction {
    enum Kind : uint8_t {
        NONE,
        KEY,                // press `key` (a game key, e.g. Enter, Space, Ctrl+1) while the button is held
        CAST_LEFT,          // left mouse button (left hand) while held
        CAST_RIGHT,         // right mouse button (right hand)
        SPELL_NEXT,         // quick-select key 1..0 after / before the last one -> left hand (key 1..0)
        SPELL_PREV,
        SPELL_NEXT_RIGHT,   // the same into the right hand (Ctrl+1..0)
        SPELL_PREV_RIGHT,
        SAVE_QUICK,         // port: save the state into slot 0 / load it (GamepadFrame.port_action)
        LOAD_QUICK,
    } kind = NONE;
    KeyChord key;
};

struct GamepadConfig {
    bool  enabled = true;
    float deadzone_left = 0.25f;     // 0..0.9 of full deflection, radial
    float deadzone_right = 0.12f;
    float stick_threshold = 0.5f;    // flight: left-stick deflection that holds an arrow key (0.1..0.95)
    float trigger_threshold = 0.3f;  // a trigger counts as pressed above this (0.05..0.95)
    float steer_sensitivity = 1.0f;  // right stick: pointer offset = deflection^curve * sensitivity (0.1..4)
    float steer_curve = 1.5f;        // response exponent (1 = linear .. 3)
    bool  invert_y = false;          // right stick up = pointer down
    float cursor_speed = 700.0f;     // book / menus: pointer speed at full deflection, 640-space pixels / s
    PadAction bind[PAD_INPUT_COUNT]; // flight bindings (gamepad_default_bindings)
    GamepadConfig();
};
void gamepad_default_bindings(GamepadConfig *c);

// Raw device state (SDL ranges converted: sticks -1..1 with +y = down, triggers 0..1).
struct GamepadRaw {
    bool  connected = false;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    bool  button[PAD_LT] = {};       // PAD_A .. PAD_DPAD_RIGHT
};

enum PadContext { PAD_CTX_FLIGHT, PAD_CTX_BOOK, PAD_CTX_MENU, PAD_CTX_NONE };

struct GamepadFrame {
    struct KeyEvent { int scancode; bool down; };   // SDL scancodes, in order (feed like keyboard events)
    KeyEvent events[48];
    int   event_count = 0;
    bool  mouse_l = false, mouse_r = false;         // held (OR them with the real mouse buttons)
    bool  steer_active = false;                     // flight: the right stick is out of its dead zone
    bool  steer_released = false;                   // ... it just returned: re-centre the pointer once
    float steer_x = 0, steer_y = 0;                 // -1..1 pointer offset from the centre (+y = down)
    float cursor_dx = 0, cursor_dy = 0;             // book / menus: pointer motion this frame (640-space)
    enum { PORT_NONE, PORT_SAVE_QUICK, PORT_LOAD_QUICK } port_action = PORT_NONE;
    bool  any_input = false;                        // something was pressed / moved (attract-demo abort)
};

// Pure mapping: one call per frame with the device state, the context and the frame time.
class GamepadMapper {
public:
    GamepadFrame update(const GamepadRaw &raw, PadContext ctx, double dt_seconds, const GamepadConfig &cfg);
    void reset();                    // forget everything held (no events)
    int  spell_index() const { return spell_; }
private:
    struct Held { PadAction act; int key_scancode; bool active; };
    void press(GamepadFrame &f, int input, const PadAction &a);
    void release(GamepadFrame &f, int input);
    static void key(GamepadFrame &f, int sc, bool down);
    Held held_[PAD_INPUT_COUNT] = {};
    bool arrow_[4] = {};             // up, down, left, right held by the left stick
    bool prev_[PAD_INPUT_COUNT] = {};
    bool steering_ = false;
    int  spell_ = -1, spell_right_ = -1;   // last quick-select key index 0..9 (left / right hand)
    PadContext ctx_ = PAD_CTX_NONE;
};

// ---- SDL half (gamepad.cpp; not compiled with MC_GAMEPAD_NO_SDL) ---------------------------------
bool gamepad_init();                 // SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)
void gamepad_shutdown();
// Read the open controller (opening the first attached one when none is open: hot-plug). Call once per
// frame after the platform pumped the events. False (raw.connected false) without a controller.
bool gamepad_poll(GamepadRaw *raw);
const char *gamepad_name();          // "" without a controller

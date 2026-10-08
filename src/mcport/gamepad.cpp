// Game controller -> the game's keyboard / mouse device state (gamepad.h). Port-only.
#include "gamepad.h"
#include <SDL_scancode.h>
#include <cmath>
#include <cstring>

const char *const g_pad_input_names[PAD_INPUT_COUNT] = {
    "a", "b", "x", "y", "back", "guide", "start", "lstick", "rstick", "lb", "rb",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "lt", "rt",
};

static PadAction act(PadAction::Kind k) { PadAction a; a.kind = k; return a; }
static PadAction key_act(int sc, uint8_t mods = 0) { PadAction a; a.kind = PadAction::KEY; a.key.scancode = sc; a.key.mods = mods; return a; }

void gamepad_default_bindings(GamepadConfig *c) {
    for (PadAction &a : c->bind) a = PadAction{};
    c->bind[PAD_A] = key_act(SDL_SCANCODE_RETURN);                    // spell book + map (cmd 0x14)
    c->bind[PAD_B] = key_act(SDL_SCANCODE_SPACE);                     // respawn / leave a won level
    c->bind[PAD_X] = act(PadAction::SPELL_PREV_RIGHT);                // quick-select into the right hand
    c->bind[PAD_Y] = act(PadAction::SPELL_NEXT_RIGHT);
    c->bind[PAD_LB] = act(PadAction::SPELL_PREV);                     // quick-select into the left hand
    c->bind[PAD_RB] = act(PadAction::SPELL_NEXT);
    c->bind[PAD_LT] = act(PadAction::CAST_LEFT);
    c->bind[PAD_RT] = act(PadAction::CAST_RIGHT);
    c->bind[PAD_BACK] = key_act(SDL_SCANCODE_ESCAPE);                 // leave the level
    c->bind[PAD_START] = key_act(SDL_SCANCODE_P);                     // pause
    c->bind[PAD_DPAD_UP] = key_act(SDL_SCANCODE_LEFTBRACKET);         // view size + 1
    c->bind[PAD_DPAD_DOWN] = key_act(SDL_SCANCODE_RIGHTBRACKET);      // view size - 1
}

GamepadConfig::GamepadConfig() { gamepad_default_bindings(this); }

// ---- mapping -----------------------------------------------------------------------------------
void GamepadMapper::reset() {
    std::memset(held_, 0, sizeof held_);
    std::memset(arrow_, 0, sizeof arrow_);
    std::memset(prev_, 0, sizeof prev_);
    steering_ = false;
    ctx_ = PAD_CTX_NONE;
}

void GamepadMapper::key(GamepadFrame &f, int sc, bool down) {
    if (f.event_count < (int)(sizeof f.events / sizeof f.events[0])) f.events[f.event_count++] = {sc, down};
}

static int quick_key(int index) { return index == 9 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + index; }   // key 1..9, 0

static void mods(GamepadFrame &f, uint8_t m, bool down, void (*k)(GamepadFrame &, int, bool)) {
    if (m & KEYMOD_CTRL) k(f, SDL_SCANCODE_LCTRL, down);
    if (m & KEYMOD_SHIFT) k(f, SDL_SCANCODE_LSHIFT, down);
    if (m & KEYMOD_ALT) k(f, SDL_SCANCODE_LALT, down);
}

void GamepadMapper::press(GamepadFrame &f, int input, const PadAction &a) {
    Held &h = held_[input];
    h = Held{a, 0, true};
    switch (a.kind) {
    case PadAction::KEY:
        if (!a.key.scancode) break;
        mods(f, a.key.mods, true, key);
        key(f, a.key.scancode, true);
        h.key_scancode = a.key.scancode;
        break;
    case PadAction::SPELL_NEXT: case PadAction::SPELL_PREV:
    case PadAction::SPELL_NEXT_RIGHT: case PadAction::SPELL_PREV_RIGHT: {
        const bool right = a.kind == PadAction::SPELL_NEXT_RIGHT || a.kind == PadAction::SPELL_PREV_RIGHT;
        const bool next = a.kind == PadAction::SPELL_NEXT || a.kind == PadAction::SPELL_NEXT_RIGHT;
        int &idx = right ? spell_right_ : spell_;
        idx = next ? (idx + 1) % 10 : (idx <= 0 ? 9 : idx - 1);
        if (right) key(f, SDL_SCANCODE_LCTRL, true);
        h.key_scancode = quick_key(idx);
        key(f, h.key_scancode, true);
        break;
    }
    case PadAction::SAVE_QUICK: f.port_action = GamepadFrame::PORT_SAVE_QUICK; break;
    case PadAction::LOAD_QUICK: f.port_action = GamepadFrame::PORT_LOAD_QUICK; break;
    default: break;
    }
}

void GamepadMapper::release(GamepadFrame &f, int input) {
    Held &h = held_[input];
    if (!h.active) return;
    switch (h.act.kind) {
    case PadAction::KEY:
        if (h.key_scancode) {
            key(f, h.key_scancode, false);
            mods(f, h.act.key.mods, false, key);
        }
        break;
    case PadAction::SPELL_NEXT: case PadAction::SPELL_PREV:
        key(f, h.key_scancode, false);
        break;
    case PadAction::SPELL_NEXT_RIGHT: case PadAction::SPELL_PREV_RIGHT:
        key(f, h.key_scancode, false);
        key(f, SDL_SCANCODE_LCTRL, false);
        break;
    default: break;
    }
    h = Held{};
}

// Radial dead zone: 0 inside, rescaled to 0..1 outside.
static void deadzone(float x, float y, float dz, float *ox, float *oy, float *mag) {
    const float m = std::sqrt(x * x + y * y);
    if (m <= dz || m <= 0.0f) { *ox = *oy = *mag = 0.0f; return; }
    float s = (m - dz) / (1.0f - dz);
    if (s > 1.0f) s = 1.0f;
    *ox = x / m * s;
    *oy = y / m * s;
    *mag = s;
}

GamepadFrame GamepadMapper::update(const GamepadRaw &raw, PadContext ctx, double dt, const GamepadConfig &cfg) {
    GamepadFrame f;
    static const int arrow_sc[4] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
    auto set_arrow = [&](int i, bool on) {
        if (arrow_[i] == on) return;
        arrow_[i] = on;
        key(f, arrow_sc[i], on);
    };
    if (!raw.connected || !cfg.enabled || ctx == PAD_CTX_NONE) {
        for (int i = 0; i < PAD_INPUT_COUNT; i++) release(f, i);
        for (int i = 0; i < 4; i++) set_arrow(i, false);
        f.steer_released = steering_;
        steering_ = false;
        std::memset(prev_, 0, sizeof prev_);
        ctx_ = ctx;
        return f;
    }
    if (ctx != ctx_ && ctx != PAD_CTX_FLIGHT) {
        for (int i = 0; i < 4; i++) set_arrow(i, false);
        f.steer_released = steering_;
        steering_ = false;
    }
    ctx_ = ctx;

    bool now[PAD_INPUT_COUNT];
    for (int i = 0; i < PAD_LT; i++) now[i] = raw.button[i];
    now[PAD_LT] = raw.lt > cfg.trigger_threshold;
    now[PAD_RT] = raw.rt > cfg.trigger_threshold;

    // The cursor contexts own A / X / B and the d-pad; everything else keeps the flight binding.
    auto action_for = [&](int i) -> PadAction {
        if (ctx == PAD_CTX_FLIGHT) return cfg.bind[i];
        switch (i) {
        case PAD_A: return act(PadAction::CAST_LEFT);
        case PAD_X: return act(PadAction::CAST_RIGHT);
        case PAD_B: return key_act(ctx == PAD_CTX_BOOK ? SDL_SCANCODE_RETURN : SDL_SCANCODE_ESCAPE);
        case PAD_DPAD_UP: case PAD_DPAD_DOWN: case PAD_DPAD_LEFT: case PAD_DPAD_RIGHT: return PadAction{};
        default: return cfg.bind[i];
        }
    };
    for (int i = 0; i < PAD_INPUT_COUNT; i++) {
        if (now[i] && !prev_[i]) { press(f, i, action_for(i)); f.any_input = true; }
        else if (!now[i] && prev_[i]) release(f, i);
        prev_[i] = now[i];
    }
    for (int i = 0; i < PAD_INPUT_COUNT; i++) {
        if (!held_[i].active) continue;
        if (held_[i].act.kind == PadAction::CAST_LEFT) f.mouse_l = true;
        if (held_[i].act.kind == PadAction::CAST_RIGHT) f.mouse_r = true;
    }

    float lx, ly, lm, rx, ry, rm;
    deadzone(raw.lx, raw.ly, cfg.deadzone_left, &lx, &ly, &lm);
    deadzone(raw.rx, raw.ry, cfg.deadzone_right, &rx, &ry, &rm);
    if (lm > 0 || rm > 0) f.any_input = true;
    if (ctx == PAD_CTX_FLIGHT) {
        const float th = cfg.stick_threshold;
        set_arrow(0, ly < -th);
        set_arrow(1, ly > th);
        set_arrow(2, lx < -th);
        set_arrow(3, lx > th);
        if (rm > 0) {
            float s = std::pow(rm, cfg.steer_curve) * cfg.steer_sensitivity;
            if (s > 1.0f) s = 1.0f;
            f.steer_active = true;
            f.steer_x = rx / rm * s;
            f.steer_y = ry / rm * s * (cfg.invert_y ? -1.0f : 1.0f);
            steering_ = true;
        } else if (steering_) {
            f.steer_released = true;
            steering_ = false;
        }
    } else {
        float vx = lx, vy = ly;
        if (raw.button[PAD_DPAD_LEFT]) vx -= 1.0f;
        if (raw.button[PAD_DPAD_RIGHT]) vx += 1.0f;
        if (raw.button[PAD_DPAD_UP]) vy -= 1.0f;
        if (raw.button[PAD_DPAD_DOWN]) vy += 1.0f;
        if (vx > 1.0f) vx = 1.0f;
        if (vx < -1.0f) vx = -1.0f;
        if (vy > 1.0f) vy = 1.0f;
        if (vy < -1.0f) vy = -1.0f;
        f.cursor_dx = (float)(vx * cfg.cursor_speed * dt);
        f.cursor_dy = (float)(vy * cfg.cursor_speed * dt);
        if (vx != 0 || vy != 0) f.any_input = true;
    }
    return f;
}

// ---- SDL half ------------------------------------------------------------------------------------
#ifndef MC_GAMEPAD_NO_SDL
#include <SDL.h>
#include <string>

static SDL_GameController *s_pad = nullptr;
static uint32_t s_last_scan = 0;
static bool s_scanned = false;
static std::string s_name;

bool gamepad_init() { return SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0; }

void gamepad_shutdown() {
    if (s_pad) SDL_GameControllerClose(s_pad);
    s_pad = nullptr;
    s_name.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

const char *gamepad_name() { return s_name.c_str(); }

bool gamepad_poll(GamepadRaw *raw) {
    *raw = GamepadRaw{};
    if (s_pad && !SDL_GameControllerGetAttached(s_pad)) {
        SDL_GameControllerClose(s_pad);
        s_pad = nullptr;
        s_name.clear();
    }
    if (!s_pad) {
        const uint32_t now = SDL_GetTicks();
        if (s_scanned && now - s_last_scan < 500) return false;       // re-scan twice a second (hot-plug)
        s_scanned = true;
        s_last_scan = now;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (!SDL_IsGameController(i)) continue;
            s_pad = SDL_GameControllerOpen(i);
            if (s_pad) {
                const char *n = SDL_GameControllerName(s_pad);
                s_name = n ? n : "game controller";
                break;
            }
        }
        if (!s_pad) return false;
    }
    auto axis = [](SDL_GameControllerAxis a) {
        const float v = SDL_GameControllerGetAxis(s_pad, a) / 32767.0f;
        return v < -1.0f ? -1.0f : v;
    };
    raw->connected = true;
    raw->lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    raw->ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    raw->rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    raw->ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    raw->lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    raw->rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    static const SDL_GameControllerButton map[PAD_LT] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    };
    for (int i = 0; i < PAD_LT; i++) raw->button[i] = SDL_GameControllerGetButton(s_pad, map[i]) != 0;
    return true;
}
#endif

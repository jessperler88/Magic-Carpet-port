// Tick profiler host helpers (tick_profile.h; port round 10 task D). Nothing here runs inside the tick.
#include "tick_profile.h"
#include <algorithm>
#include <cstdio>

const char *tick_profile_phase_name(int phase) {
    static const char *const k[TP_PHASES] = {"palette", "input", "commands", "win", "things", "mode", "sound", "frame"};
    return (unsigned)phase < (unsigned)TP_PHASES ? k[phase] : "?";
}

const char *tick_profile_class_name(int cls) {
    // Thing.cls (docs/ENGINE.md): 2 scenery, 3 player, 5 creature, 7 weather, 9 projectile, 10 effect,
    // 11 switch, 12 spell; the others are unnamed
    static const char *const k[TP_CLASSES] = {"c0", "c1", "scenery", "player", "c4", "creature", "c6", "weather",
                                              "c8", "projectile", "effect", "switch", "spell", "c13", "c14", "c15"};
    return (unsigned)cls < (unsigned)TP_CLASSES ? k[cls] : "?";
}

void TickProfileWindow::add(const TickProfile &p) {
    for (int i = 0; i < TP_PHASES; i++) phase_ns[i] += p.phase_ns[i];
    lists_ns += p.lists_ns;
    for (int i = 0; i < TP_CLASSES; i++) { cls_ns[i] += p.cls_ns[i]; cls_calls[i] += p.cls_calls[i]; }
    total_ns += p.total_ns;
    worst_ns = std::max(worst_ns, p.total_ns);
    ticks++;
}

static void appendf(std::string *s, const char *fmt, double v) {
    char b[64];
    std::snprintf(b, sizeof b, fmt, v);
    *s += b;
}
// ms with 2 decimals, the leading zero dropped (".04"), for the compact F11 line
static void append_ms(std::string *s, double ms) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", ms);
    *s += (b[0] == '0' && b[1] == '.') ? b + 1 : b;
}

std::string TickProfileWindow::format(int max_classes) const {
    if (ticks <= 0) return "";
    const double n = (double)ticks;
    std::string s = "tick ";
    appendf(&s, "%.2f", (double)total_ns / n / 1e6);
    s += "/";
    appendf(&s, "%.2f", (double)worst_ns / 1e6);
    s += " ms:";
    static const struct { int phase; const char *label; } k_short[] = {
        {TP_INPUT, "in"}, {TP_COMMANDS, "cmd"}, {TP_WIN, "win"}, {TP_THINGS, "things"},
        {TP_MODE, "mode"}, {TP_SOUND, "snd"}, {TP_FRAME, "hud"}, {TP_PALETTE, "pal"}};
    for (const auto &e : k_short) {
        const double ms = (double)phase_ns[e.phase] / n / 1e6;
        if (e.phase != TP_THINGS && ms < 0.005) continue;
        s += " ";
        s += e.label;
        s += " ";
        append_ms(&s, ms);
        if (e.phase == TP_THINGS) {
            int order[TP_CLASSES];
            for (int i = 0; i < TP_CLASSES; i++) order[i] = i;
            std::stable_sort(order, order + TP_CLASSES, [&](int a, int b) { return cls_ns[a] > cls_ns[b]; });
            s += " (lists ";
            append_ms(&s, (double)lists_ns / n / 1e6);
            int shown = 0;
            for (int i = 0; i < TP_CLASSES && shown < max_classes; i++) {
                const int c = order[i];
                if (cls_calls[c] == 0 || cls_ns[c] * 100 < total_ns) continue;
                s += " ";
                s += tick_profile_class_name(c);
                s += " ";
                append_ms(&s, (double)cls_ns[c] / n / 1e6);
                shown++;
            }
            s += ")";
        }
    }
    return s;
}

std::string TickProfileWindow::json() const {
    const double n = ticks > 0 ? (double)ticks : 1.0;
    char b[128];
    std::string s;
    std::snprintf(b, sizeof b, "{\"ticks\":%ld,\"avg_us\":%.2f,\"worst_us\":%.2f,\"phases_us\":{", ticks,
                  (double)total_ns / n / 1e3, (double)worst_ns / 1e3);
    s += b;
    for (int i = 0; i < TP_PHASES; i++) {
        std::snprintf(b, sizeof b, "%s\"%s\":%.2f", i ? "," : "", tick_profile_phase_name(i), (double)phase_ns[i] / n / 1e3);
        s += b;
    }
    std::snprintf(b, sizeof b, "},\"lists_us\":%.2f,\"classes\":{", (double)lists_ns / n / 1e3);
    s += b;
    bool first = true;
    for (int i = 0; i < TP_CLASSES; i++) {
        if (cls_calls[i] == 0) continue;
        std::snprintf(b, sizeof b, "%s\"%s\":{\"us\":%.2f,\"calls\":%.1f}", first ? "" : ",", tick_profile_class_name(i),
                      (double)cls_ns[i] / n / 1e3, (double)cls_calls[i] / n);
        s += b;
        first = false;
    }
    s += "}}";
    return s;
}

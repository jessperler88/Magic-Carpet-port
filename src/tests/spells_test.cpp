// Unit test for spells.cpp (class 12): the cast helpers, every phase-0 cast handler, the dropped /
// level pickup phases, and the spell Things of the engine's own snapshot (movie/gam00000.dat, 413
// ticks into level 38) against a level 38 the port generates and runs for the same 412 updates.
// Ends with smoke runs (scripted casting on several levels, the shipped movie) that print statistics.
//
// The class-9 / class-10 update handlers belong to other subsystems and are not linked here: a
// projectile a spell launches stays where it was created, which is what the field checks want; the
// long runs delete such Things after 60 ticks (reaper) so the pool does not fill up.
// argv[1] = game dir.
#include "sim.h"
#include "spells.h"
#include "player.h"
#include "demo.h"
#include "mc_math.h"
#include "gen/dispatch_tables.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { \
    std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); g_fail++; } } while (0)

// ---- sound request capture ---------------------------------------------------------------------

struct Snd { int thing, player, sound; };
static std::vector<Snd> g_snd;
static void snd_capture(int thing, int player, int sound) { g_snd.push_back({thing, player, sound}); }
static int snd_count(int sound) {
    int n = 0;
    for (const Snd &s : g_snd) if (s.sound == sound) n++;
    return n;
}

// ---- creation spies: count what the spells create and remember when (for the reaper) ------------

static ThingCreateFn g_orig_proj[20];
static ThingCreateFn g_orig_puff;
static int g_made_proj[20];
static int g_made_puff;
static int g_birth[MC_THING_SLOTS];
static int g_now = 0;

template <int T> static Thing *proj_spy(const Pos *pos) {
    Thing *t = g_orig_proj[T] ? g_orig_proj[T](pos) : nullptr;
    if (t) { g_made_proj[T]++; g_birth[thing_index(t)] = g_now; }
    return t;
}
static Thing *puff_spy(const Pos *pos) {
    Thing *t = g_orig_puff ? g_orig_puff(pos) : nullptr;
    if (t) { g_made_puff++; g_birth[thing_index(t)] = g_now; }
    return t;
}
template <int... T> static void install_proj_spies(std::integer_sequence<int, T...>) {
    ((g_orig_proj[T] = thing_create_fn(9, T)), ...);
    (thing_register_create(g_dispatch_b_cls9[T].handler, proj_spy<T>), ...);
}
static void install_spies() {
    install_proj_spies(std::make_integer_sequence<int, 20>{});
    g_orig_puff = thing_create_fn(10, 2);
    thing_register_create(g_dispatch_b_cls10[2].handler, puff_spy);
}
static void spies_reset() {
    std::memset(g_made_proj, 0, sizeof g_made_proj);
    g_made_puff = 0;
    for (int &b : g_birth) b = -1;
    g_now = 0;
}
// After every tick of a long run: projectiles and smoke puffs created through the spies are deleted
// when they are 60 ticks old (nothing else ends them in this build).
static int reap() {
    int n = 0;
    g_now++;
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        Thing *t = thing_at(i);
        bool ours = t->cls == 9 || (t->cls == 10 && t->type == 2);
        if (!ours) { g_birth[i] = -1; continue; }
        if (g_birth[i] >= 0 && g_now - g_birth[i] > 60 && !(t->flags & 0x400)) { thing_mark_delete(t); n++; }
    }
    return n;
}

// ---- world helpers -----------------------------------------------------------------------------

static Thing *wiz() { return thing_at(g_state->players[0].thing); }
static int slot_of(Thing *w, int id) {
    PlayerBlock *P = player_block(w);
    for (int i = 0; i < 24; i++) {
        int32_t idx = P->spell_slot[i];
        if (idx > 0 && idx < MC_THING_SLOTS && thing_at((unsigned)idx)->type == id) return i;
    }
    return -1;
}
static Thing *spell_of(Thing *w, int id) {
    int s = slot_of(w, id);
    return s < 0 ? nullptr : thing_at((unsigned)player_block(w)->spell_slot[s]);
}
static void give_mana(Thing *w, int32_t amount) {
    player_block(w)->mana = amount;         // mana_totals_update rebuilds Thing.mana_total from this
    w->mana_total = amount;
    w->mana = amount;
}
static void tick(int cmd = 0, int arg = 0, int bits = 0, int steer_x = 0) {
    CmdPacket &c = g_state->commands[0];
    c.cmd = (uint8_t)cmd; c.arg = (uint8_t)arg; c.bits = (uint8_t)bits; c.steer_x = (int8_t)steer_x;
    game_tick_sim();
}
// Level start: the first tick executes the "join" packets (spawns the wizards), the second one the
// "access all spells" cheat (command 0x1e / 1) for player 0.
static bool start_level(int level, bool all_spells) {
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    if (!sim_load_level(level)) return false;
    game_tick_sim();                        // the packets players_init_records queued (cmd 1 = join)
    if (all_spells) tick(0x1e, 1);
    return wiz() != thing_at(0);
}
static std::vector<int> things_of(int cls) {
    std::vector<int> v;
    for (int i = 1; i < MC_THING_SLOTS; i++) if (thing_at(i)->cls == cls) v.push_back(i);
    return v;
}
static std::vector<int> new_things(int cls, const std::vector<int> &before) {
    std::vector<int> v;
    for (int i : things_of(cls)) {
        bool old = false;
        for (int b : before) if (b == i) old = true;
        if (!old) v.push_back(i);
    }
    return v;
}
static int live_count() {
    int n = 0;
    for (int i = 1; i < MC_THING_SLOTS; i++) if (thing_at(i)->cls != 0) n++;
    return n;
}
static void check_pool() { CHECK_EQ(thing_free_count() + live_count(), MC_THING_SLOTS - 1); }
static bool same_pos(const Pos &a, const Pos &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
// Thing comparison / restore that leaves the list and cell links alone (other Things come and go in
// the same cell during a test).
static bool same_thing(const Thing &a, const Thing &b) {
    Thing x = a, y = b;
    x.next = y.next = 0;
    x.cell_next = y.cell_next = 0;
    x.cell_prev = y.cell_prev = 0;
    return std::memcmp(&x, &y, sizeof x) == 0;
}
static void restore(Thing *t, const Thing &saved) {
    uint32_t next = t->next;
    uint16_t cn = t->cell_next, cp = t->cell_prev;
    *t = saved;
    t->next = next; t->cell_next = cn; t->cell_prev = cp;
}
static Thing *any_castle() {
    for (int i = 1; i < MC_THING_SLOTS; i++)
        if (thing_at(i)->cls == 3 && thing_at(i)->type == 2) return thing_at(i);
    return nullptr;
}
static Thing *ai_with_castle() {
    for (int p = 1; p < g_state->player_count; p++) {
        Thing *t = thing_at(g_state->players[p].thing);
        if (player_block(t)->castle != 0) return t;
    }
    return nullptr;
}

// A caster pose that makes every copied field distinguishable.
static void pose(Thing *w) {
    Pos p = *thing_pos(w);
    p.z = (int16_t)(terrain_height_at(&p) + 0x600);
    thing_move_to(w, &p);
    w->yaw = 0x123;
    w->pitch = 0x7c0;
    w->speed_cur = 0x30;
    w->flags = (w->flags & ~0x300u) | 0x200;
    w->health = w->max_health;
    player_block(w)->aim_charge = 77;
    give_mana(w, 1000000);
    w->mana_cost = 55;
}

// ---- 1. cast helpers ---------------------------------------------------------------------------

static void test_helpers() {
    Thing *w = wiz();
    pose(w);
    Thing *sp = spell_of(w, 0);
    CHECK(sp != nullptr);
    if (!sp) return;
    const Thing sp0 = *sp, w0 = *w;

    // spell_can_cast: the mana is only needed when the cast starts
    g_snd.clear();
    sp->cast_ticks = sp->duration;
    w->mana = sp->mana_total;
    CHECK_EQ(spell_can_cast(sp, w), 1);
    w->mana = sp->mana_total - 1;
    CHECK_EQ(spell_can_cast(sp, w), 0);
    CHECK_EQ(g_snd.size(), 1);
    if (g_snd.size() == 1) CHECK(g_snd[0].thing == 0 && g_snd[0].player == 0 && g_snd[0].sound == 0x1d);
    sp->cast_ticks = (int16_t)(sp->duration - 1);
    CHECK_EQ(spell_can_cast(sp, w), 1);
    w->mana = 0;
    CHECK_EQ(spell_can_cast(sp, w), 1);
    w->mana = -1;
    CHECK_EQ(spell_can_cast(sp, w), 0);
    w->mana = 1000;
    w->health = -1;
    CHECK_EQ(spell_can_cast(sp, w), 0);
    w->health = 0;
    CHECK_EQ(spell_can_cast(sp, w), 1);
    // castle mana: player 0 has no castle
    sp->mana_cost = 500;
    CHECK_EQ(player_block(w)->castle, 0);
    CHECK_EQ(spell_can_cast(sp, w), 0);
    CHECK_EQ(snd_count(0x1d), 4);
    // ... an AI wizard of level 38 has one
    Thing *ai = ai_with_castle();
    CHECK(ai != nullptr);
    if (ai) {
        Thing *castle = thing_at(player_block(ai)->castle);
        const int32_t castle_mana = castle->mana, ai_mana = ai->mana;
        ai->mana = 1000;
        g_snd.clear();
        castle->mana = 499;
        CHECK_EQ(spell_can_cast(sp, ai), 0);
        CHECK(g_snd.size() == 1 && g_snd[0].player == player_block(ai)->player_no);
        castle->mana = 500;
        CHECK_EQ(spell_can_cast(sp, ai), 1);
        castle->mana = castle_mana;
        ai->mana = ai_mana;
    }
    restore(sp, sp0);
    restore(w, w0);

    // spell_charge_mana
    sp->cast_ticks = sp->duration;
    w->mana_cost = 100;
    CHECK_EQ(spell_charge_mana(sp, w), 1);
    CHECK_EQ(w->mana_cost, -sp->mana_total);
    CHECK_EQ(spell_charge_mana(sp, w), 1);
    CHECK_EQ(w->mana_cost, -2 * sp->mana_total);
    sp->cast_ticks = (int16_t)(sp->duration - 1);
    CHECK_EQ(spell_charge_mana(sp, w), 0);
    CHECK_EQ(w->mana_cost, -2 * sp->mana_total);        // a pending charge is kept
    w->mana_cost = 100;
    CHECK_EQ(spell_charge_mana(sp, w), 0);
    CHECK_EQ(w->mana_cost, 0);                          // no regeneration during a cast
    sp->cast_ticks = 0;
    w->mana_cost = 100;
    CHECK_EQ(spell_charge_mana(sp, w), 0);
    CHECK_EQ(w->mana_cost, 100);                        // idle spell: untouched
    restore(sp, sp0);
    restore(w, w0);

    // spell_projectile_origin: yaw 0 looks along -y, so the right hand is +x and the left hand -x
    w->yaw = 0;
    for (int hand = 0; hand < 3; hand++) {
        Thing *p = thing_create(thing_pos(w), 9, 0);
        Thing *seg = thing_create(thing_pos(w), 9, 0);
        CHECK(p && seg);
        if (!p || !seg) break;
        p->child = thing_index(seg);
        w->flags = (w->flags & ~0x300u) | (hand == 0 ? 0x100u : hand == 1 ? 0x200u : 0u);
        spell_projectile_origin(w, p);
        int want_dx = hand == 0 ? -0x100 : hand == 1 ? 0x100 : 0;
        CHECK_EQ((int16_t)(p->x - w->x), want_dx);
        CHECK_EQ(p->y, w->y);
        CHECK_EQ(p->z, w->z);
        CHECK_EQ((int16_t)(seg->x - w->x), want_dx);    // the segment chain follows
        CHECK(p->flags & 4);
        thing_free(seg);
        thing_free(p);
    }
    // under ground: the projectile stays at the caster
    {
        Pos low = *thing_pos(w);
        low.z = -0x4000;
        Thing *p = thing_create(&low, 9, 0);
        CHECK(p != nullptr);
        if (p) {
            w->flags = (w->flags & ~0x300u) | 0x100;
            spell_projectile_origin(w, p);
            CHECK(same_pos(*thing_pos(p), low));
            thing_free(p);
        }
    }
    restore(w, w0);
    std::printf("helpers: spell_can_cast / spell_charge_mana / spell_projectile_origin cases pass\n");
}

// ---- 2. the projectile spells ------------------------------------------------------------------

enum Shape { BURST, SINGLE, QUAKE };
enum Dmg { DMG_SPELL, DMG_DEFAULT, DMG_7D0, DMG_SKELETON };
enum Aux { AUX_AIM, AUX_C8, AUX_NONE };
struct Launch {
    int id; const char *name; Shape shape; int proj, impact_cls, impact_type;
    bool add_speed; Dmg dmg; Aux aux; bool pitch; int dist; bool ground; int sound;
};
// Re-stated from the disassembly listing, handler by handler (not derived from spells.cpp).
static const Launch k_launch[] = {
    {0,  "fireball",      BURST,  0x00, 10, 0x00, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 9},     // 47130
    {3,  "possession",    BURST,  0x01, 10, 0x0c, true,  DMG_DEFAULT,  AUX_C8,   true,  0x2800, false, 0x28},  // 475b0
    {6,  "earthquake",    QUAKE,  0x02, 10, 0x0f, true,  DMG_SPELL,    AUX_AIM,  false, 0x1000, true,  9},     // 47840
    {7,  "meteor",        SINGLE, 0x03, 10, 0x11, true,  DMG_SPELL,    AUX_AIM,  true,  0x2800, false, 0xf},   // 479f0
    {8,  "volcano",       SINGLE, 0x04, 10, 0x09, true,  DMG_SPELL,    AUX_AIM,  false, 0x1000, true,  0xf},   // 47b90
    {9,  "crater",        SINGLE, 0x05, 10, 0x0b, true,  DMG_SPELL,    AUX_AIM,  false, 0x1000, true,  0xf},   // 47d40
    {11, "rubber band",   SINGLE, 0x07, 10, 0x1a, false, DMG_SPELL,    AUX_NONE, true,  0x2800, false, 9},     // 480e0
    {13, "steal mana",    SINGLE, 0x08, 10, 0x19, true,  DMG_7D0,      AUX_AIM,  true,  0x4000, false, 9},     // 482f0
    {15, "lightning",     SINGLE, 0x09, 10, 0x17, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 0x17},  // 48510
    {17, "skeleton",      SINGLE, 0x0b, 10, 0x24, true,  DMG_SKELETON, AUX_AIM,  true,  0x4000, false, 9},     // 488a0
    {18, "thunderbolt",   SINGLE, 0x0c, 9,  0x09, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 9},     // 48a70
    {19, "mana magnet",   BURST,  0x11, 10, 0x36, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 0x28},  // 48c20
    {20, "fire wall",     BURST,  0x10, 10, 0x35, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 9},     // 48de0
    {22, "smart bomb",    BURST,  0x12, 10, 0x37, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 0},     // 49140
    {23, "mini fireball", BURST,  0x00, 10, 0x00, true,  DMG_SPELL,    AUX_AIM,  true,  0x4000, false, 9},     // 492e0
};

static Pos expect_origin(const Thing *c) {
    Pos o = *thing_pos(c);
    if (c->flags & 0x100)      math_rotate_offset(&o, (c->yaw - 0x200) & 0x7ff, 0, 0x100);
    else if (c->flags & 0x200) math_rotate_offset(&o, (c->yaw + 0x200) & 0x7ff, 0, 0x100);
    if ((int16_t)terrain_height_at(&o) > o.z) o = *thing_pos(c);
    return o;
}

static void check_projectile(const Launch &L, const Thing *p, const Thing *w, const Thing *sp, int aim_before) {
    CHECK_EQ(p->cls, 9);
    CHECK_EQ(p->type, L.proj);
    CHECK_EQ(p->impact_cls, L.impact_cls);
    CHECK_EQ(p->impact_type, L.impact_type);
    CHECK_EQ(p->owner, w->owner);
    CHECK_EQ(p->speed_cur, 0x180 + (L.add_speed ? w->speed_cur : 0));
    CHECK_EQ(p->speed_base, 0x180);
    CHECK_EQ(p->mana, sp->mana);
    switch (L.dmg) {
    case DMG_SPELL:    CHECK_EQ(p->damage, sp->damage); break;
    case DMG_DEFAULT:  CHECK_EQ(p->damage, 100); break;        // thing_alloc's default
    case DMG_7D0:      CHECK_EQ(p->damage, 0x7d0); break;
    case DMG_SKELETON: CHECK_EQ(p->damage, 1); break;          // player 0 has no castle
    }
    CHECK_EQ(p->aux, L.aux == AUX_AIM ? aim_before : L.aux == AUX_C8 ? 0xc8 : 0);
    CHECK_EQ(player_block(w)->aim_charge, L.aux == AUX_NONE ? aim_before : 0);
    CHECK_EQ(p->yaw, w->yaw);
    CHECK_EQ(p->pitch, w->pitch);
    Pos o = expect_origin(w);
    CHECK(!(o.x == w->x && o.y == w->y));                       // the hand offset really applied
    CHECK_EQ(p->x, o.x);
    CHECK_EQ(p->y, o.y);
    CHECK_EQ(p->z, o.z + w->ext_h);
    Pos h = *thing_pos(w);
    math_rotate_offset(&h, w->yaw, L.pitch ? w->pitch : 0, L.dist);
    if (L.ground) h.z = (int16_t)terrain_height_at(&h);
    CHECK_EQ(p->home.x, h.x);
    CHECK_EQ(p->home.y, h.y);
    CHECK_EQ(p->home.z, h.z);
    CHECK(p->flags & 4);                                        // linked into the cell map
}

static void test_launches() {
    for (const Launch &L : k_launch) {
        int fail0 = g_fail;
        Thing *w = wiz();
        pose(w);
        PlayerBlock *P = player_block(w);
        Thing *sp = spell_of(w, L.id);
        CHECK(sp != nullptr);
        if (!sp) continue;
        CHECK_EQ(sp->state, L.id * 3);
        CHECK_EQ(sp->caster, thing_index(w));
        ThingUpdateFn fn = thing_update_fn(12, sp->state);     // through the table: checks the binding
        CHECK(fn != nullptr);
        if (!fn) continue;
        const Thing sp0 = *sp;
        const std::vector<int> before = things_of(9);

        // idle: nothing happens
        fn(sp);
        CHECK(same_thing(*sp, sp0));
        CHECK_EQ(new_things(9, before).size(), 0);

        // launch tick
        g_snd.clear();
        sp->cast_ticks = sp->duration;
        const int aim = P->aim_charge;
        fn(sp);
        std::vector<int> made = new_things(9, before);
        CHECK_EQ(made.size(), 1);
        if (made.size() == 1) {
            check_projectile(L, thing_at(made[0]), w, sp, aim);
            if (L.sound) {
                CHECK_EQ(g_snd.size(), 1);
                if (g_snd.size() == 1) CHECK(g_snd[0].thing == made[0] && g_snd[0].player == -1 && g_snd[0].sound == L.sound);
            } else {
                CHECK_EQ(g_snd.size(), 0);
            }
        }
        CHECK_EQ(w->mana_cost, -sp->mana_total);
        CHECK_EQ(sp->cast_ticks, sp->duration - 1);
        CHECK_EQ(sp->burst, 0);

        // the remaining ticks: no second projectile; the mana rate is zeroed, except by the earthquake
        int calls = 1;
        w->mana_cost = 55;
        if (sp->cast_ticks > 0) {
            fn(sp);
            calls++;
            CHECK_EQ(w->mana_cost, L.shape == QUAKE ? 55 : 0);
        }
        while (sp->cast_ticks > 0 && calls < 1000) { fn(sp); calls++; }
        CHECK_EQ(calls, sp->duration);
        CHECK_EQ(new_things(9, before).size(), 1);
        fn(sp);
        CHECK(same_thing(*sp, sp0));                            // the spell Thing is back to idle, bit for bit
        for (int i : new_things(9, before)) thing_free(thing_at(i));

        // not enough mana at the start: refused with the "cannot" sound, the cast ends at once
        g_snd.clear();
        w->mana = sp->mana_total - 1;
        w->mana_cost = 55;
        sp->cast_ticks = sp->duration;
        fn(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(new_things(9, before).size(), 0);
        CHECK_EQ(w->mana_cost, 55);
        CHECK(g_snd.size() == 1 && g_snd[0].sound == 0x1d && g_snd[0].player == 0 && g_snd[0].thing == 0);

        // no caster: the count-down runs, nothing else
        g_snd.clear();
        sp->caster = 0;
        sp->cast_ticks = sp->duration;
        fn(sp);
        CHECK_EQ(sp->cast_ticks, sp->duration - 1);
        CHECK_EQ(new_things(9, before).size(), 0);
        CHECK_EQ(g_snd.size(), 0);
        restore(sp, sp0);
        std::printf("launch %-13s (spell %2d, %3d ticks, %6d mana): projectile %2d, impact %2d/%02x  %s\n", L.name, L.id,
                    sp->duration, sp->mana_total, L.proj, L.impact_cls, L.impact_type, g_fail == fail0 ? "ok" : "FAILED");
    }

    // Burst: Thing.burst = n fires n + 1 projectiles on the launch tick and charges each of them.
    {
        Thing *w = wiz();
        pose(w);
        Thing *sp = spell_of(w, 0);
        const std::vector<int> before = things_of(9);
        sp->cast_ticks = sp->duration;
        sp->burst = 2;
        spell_fireball_update(sp);
        CHECK_EQ(new_things(9, before).size(), 3);
        CHECK_EQ(w->mana_cost, -3 * sp->mana_total);
        CHECK_EQ(sp->burst, 0);
        CHECK_EQ(sp->cast_ticks, sp->duration - 1);
        // only the first shot carries the aim charge (it is consumed)
        std::vector<int> made = new_things(9, before);
        int with_aim = 0;
        for (int i : made) if (thing_at(i)->aux == 77) with_aim++;
        CHECK_EQ(with_aim, 1);
        // a negative burst count: nothing fires, the count is reset
        sp->cast_ticks = sp->duration;
        sp->burst = 0x80;
        w->mana_cost = 55;
        spell_fireball_update(sp);
        CHECK_EQ(new_things(9, before).size(), 3);
        CHECK_EQ(w->mana_cost, 55);
        CHECK_EQ(sp->burst, 0);
        sp->cast_ticks = 0;
        for (int i : made) thing_free(thing_at(i));
    }

    // Skeleton with a castle that can hold the army's mana: damage field = the spell's mana_total.
    {
        Thing *ai = ai_with_castle();
        Thing *sp = spell_of(wiz(), 17);
        CHECK(ai && sp);
        if (ai && sp) {
            Thing *castle = thing_at(player_block(ai)->castle);
            const Thing sp0 = *sp, ai0 = *ai;
            const int32_t total0 = castle->mana_total;
            const uint8_t aim0 = player_block(ai)->aim_charge;
            sp->caster = thing_index(ai);
            ai->mana = 1000000;
            for (int pass = 0; pass < 2; pass++) {
                castle->mana_total = pass == 0 ? sp->mana_total : sp->mana_total - 1;
                const std::vector<int> before = things_of(9);
                sp->cast_ticks = sp->duration;
                spell_skeleton_s51_update(sp);
                std::vector<int> made = new_things(9, before);
                CHECK_EQ(made.size(), 1);
                if (made.size() == 1) {
                    CHECK_EQ(thing_at(made[0])->damage, pass == 0 ? (uint16_t)sp->mana_total : 1);
                    CHECK_EQ(thing_at(made[0])->owner, ai->owner);
                    thing_free(thing_at(made[0]));
                }
            }
            castle->mana_total = total0;
            player_block(ai)->aim_charge = aim0;
            restore(sp, sp0);
            restore(ai, ai0);
        }
    }
}

// ---- 3. the spells that act on the caster ------------------------------------------------------

static void test_self_spells() {
    Thing *w = wiz();
    PlayerBlock *P = player_block(w);

    // Heal (spell 1): 5 % of the maximum per tick, mana_total charged every tick, ends when full.
    {
        pose(w);
        Thing *sp = spell_of(w, 1);
        const Thing sp0 = *sp;
        CHECK_EQ(w->max_health, 10000);
        w->health = 7600;
        sp->cast_ticks = sp->duration;
        g_snd.clear();
        w->mana_cost = 55;
        spell_heal_s3_update(sp);
        CHECK_EQ(w->health, 8100);
        CHECK_EQ(w->mana_cost, -1000);
        CHECK(g_snd.size() == 1 && g_snd[0].thing == thing_index(w) && g_snd[0].sound == 0x19);
        spell_heal_s3_update(sp);
        CHECK_EQ(w->health, 8600);
        CHECK_EQ(w->mana_cost, -2000);                      // accumulates until the wizard's update applies it
        w->mana_cost = 55;
        spell_heal_s3_update(sp);
        spell_heal_s3_update(sp);
        CHECK_EQ(w->health, 9600);
        spell_heal_s3_update(sp);
        CHECK_EQ(w->health, 10000);                         // clamped
        CHECK_EQ(sp->cast_ticks, sp->duration - 5);
        spell_heal_s3_update(sp);                           // full: the cast ends
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(g_snd.size(), 1);
        // without mana_total mana even a running cast stops
        w->health = 5000;
        sp->cast_ticks = 10;
        w->mana = sp->mana_total - 1;
        spell_heal_s3_update(sp);
        CHECK_EQ(w->health, 5000);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK(same_thing(*sp, sp0));
    }

    // Speed-up (spell 2) and its mirror (spell 21).
    for (int pass = 0; pass < 2; pass++) {
        pose(w);
        const int sign = pass == 0 ? 1 : -1;
        Thing *sp = spell_of(w, pass == 0 ? 2 : 21);
        ThingUpdateFn fn = thing_update_fn(12, sp->state);
        const Thing sp0 = *sp;
        const std::vector<int> before = things_of(10);
        const int base = w->speed_base;
        CHECK_EQ(base, 0x50);
        P->accelerating = 0;
        P->target_speed = 0x20;
        sp->cast_ticks = sp->duration;
        sp->tick = 0;
        g_snd.clear();
        fn(sp);
        CHECK_EQ(P->target_speed, sign * base * 3);
        CHECK_EQ(w->speed_cur, sign * base * 3);
        CHECK(sp->flags & 0x80);
        CHECK(g_snd.size() == 1 && g_snd[0].thing == thing_index(w) && g_snd[0].sound == 0x13);
        CHECK_EQ(w->mana_cost, -sp->mana_total);
        // a smoke puff (effect 2) on every 4th tick of the spell Thing, with 4x the lifetime
        std::vector<int> puffs = new_things(10, before);
        CHECK_EQ(puffs.size(), 1);
        if (puffs.size() == 1) {
            Thing *e = thing_at(puffs[0]);
            Thing *ref = thing_create(thing_pos(w), 10, 2);
            CHECK(ref != nullptr);
            if (ref) {
                CHECK_EQ(e->type, 2);
                CHECK_EQ(e->owner, w->owner);
                CHECK_EQ(e->health, ref->health * 4);
                CHECK(same_pos(*thing_pos(e), *thing_pos(w)));
                thing_free(ref);
            }
        }
        sp->tick = 1;
        fn(sp);
        CHECK_EQ(P->target_speed, sign * base * 2);
        CHECK_EQ(w->speed_cur, sign * base * 2);
        CHECK(sp->flags & 0x80);
        CHECK_EQ(new_things(10, before).size(), 1);
        fn(sp);                                             // cast_ticks == duration - 2: the sound flag drops
        CHECK(!(sp->flags & 0x80));
        CHECK_EQ(g_snd.size(), 1);
        // a speed key ends the cast: base speed again (the mirror spell leaves the wizard reversing)
        P->accelerating = 1;
        fn(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(P->target_speed, sign * base);
        CHECK_EQ(w->speed_cur, sign * base);
        // the natural end
        P->accelerating = 0;
        sp->cast_ticks = 2;
        fn(sp);
        CHECK_EQ(P->target_speed, sign * base * 2);
        fn(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(P->target_speed, sign * base);
        sp->tick = sp0.tick;
        CHECK(same_thing(*sp, sp0));
        for (int i : new_things(10, before)) thing_free(thing_at(i));
        P->target_speed = 0;
        w->speed_cur = 0;
    }

    // Shield (4), Rebound (14), Invisible (12), Beyond sight (5).
    {
        pose(w);
        Thing *sh = spell_of(w, 4), *rb = spell_of(w, 14), *inv = spell_of(w, 12), *bs = spell_of(w, 5);
        const Thing sh0 = *sh, rb0 = *rb, inv0 = *inv, bs0 = *bs;
        const uint32_t flags0 = w->flags;
        CHECK(!(flags0 & 0xc020));
        sh->cast_ticks = 3;
        spell_shield_update(sh);
        CHECK_EQ(w->flags, flags0 | 0x4000);
        CHECK_EQ(sh->cast_ticks, 2);
        w->flags = flags0;
        w->mana = -1;                                       // cannot cast any more
        spell_shield_update(sh);
        CHECK_EQ(w->flags, flags0);
        CHECK_EQ(sh->cast_ticks, 0);
        w->mana = 1000000;

        rb->cast_ticks = 2;
        spell_rebound_update(rb);
        CHECK_EQ(w->flags, flags0 | 0x8000);
        spell_rebound_update(rb);
        CHECK_EQ(rb->cast_ticks, 0);
        CHECK_EQ(w->flags, flags0 | 0x8000);                // still set after the last cast tick ...
        spell_rebound_update(rb);
        CHECK_EQ(w->flags, flags0);                         // ... and cleared by the first idle one

        inv->cast_ticks = inv->duration;
        w->mana_cost = 55;
        spell_invisible_s36_update(inv);
        CHECK_EQ(w->flags, flags0 | 0x20);
        CHECK_EQ(w->mana_cost, -inv->mana_total);
        spell_invisible_s36_update(inv);
        CHECK_EQ(inv->cast_ticks, inv->duration - 2);
        w->flags &= ~0x20u;                                 // somebody else cleared it (another cast): the cast ends
        spell_invisible_s36_update(inv);
        CHECK_EQ(inv->cast_ticks, 0);
        inv->cast_ticks = 1;
        w->flags |= 0x20;
        spell_invisible_s36_update(inv);
        CHECK_EQ(w->flags, flags0);
        // the dummy player block stays all zero (the handler writes its P+0x14b through the spell)
        for (size_t i = 0; i < sizeof g_dummy_player_block; i++) CHECK_EQ(g_dummy_player_block[i], 0);

        bs->cast_ticks = bs->duration;
        w->mana_cost = 55;
        spell_beyond_sight_s15_update(bs);
        CHECK_EQ(w->mana_cost, -bs->mana_total);
        CHECK_EQ(bs->cast_ticks, bs->duration - 1);
        CHECK_EQ(w->flags, flags0);
        restore(sh, sh0); restore(rb, rb0); restore(inv, inv0); restore(bs, bs0);
    }

    // Teleport (10). Without a castle: 0x4000 units in a direction drawn from the spell's own RNG.
    {
        pose(w);
        Thing *sp = spell_of(w, 10);
        const Thing sp0 = *sp;
        const Pos from = *thing_pos(w);
        CHECK_EQ(P->castle, 0);
        P->target_speed = 0x50;
        sp->cast_ticks = sp->duration;
        g_snd.clear();
        spell_teleport_s30_update(sp);
        uint32_t r = mc_lcg(sp0.rng);
        CHECK_EQ(sp->rng, r);
        Pos want = from;
        math_rotate_offset(&want, (int)(r & 0x7ff), 0, 0x4000);
        CHECK(w->x == want.x && w->y == want.y && w->z == from.z);
        int d = pos_dist_xy(&from, thing_pos(w)) / 0x100;
        CHECK(d == 0x3f || d == 0x40);                      // 0x4000 minus rounding
        CHECK(sp->home.x == want.x && sp->home.y == want.y && sp->home.z == 0);
        CHECK_EQ(P->target_speed, 0);
        CHECK(g_snd.size() == 1 && g_snd[0].thing == thing_index(w) && g_snd[0].sound == 0x16);
        CHECK_EQ(w->mana_cost, -sp->mana_total);
        CHECK(w->flags & 4);
        // the other ticks only count down (and hold the wizard when the cast ends)
        const Pos there = *thing_pos(w);
        for (int guard = 0; sp->cast_ticks > 1 && guard < 1000; guard++) spell_teleport_s30_update(sp);
        P->target_speed = 0x50;
        spell_teleport_s30_update(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(P->target_speed, 0);
        CHECK(same_pos(*thing_pos(w), there));
        CHECK_EQ(sp->rng, r);                               // one draw per teleport

        // With a castle: there, and with the next cast back to where the wizard came from.
        Thing *castle = any_castle();
        CHECK(castle != nullptr);
        if (castle) {
            P->castle = thing_index(castle);
            sp->home.z = 0;
            const Pos start = *thing_pos(w);
            CHECK(start.z != 0);
            sp->cast_ticks = sp->duration;
            spell_teleport_s30_update(sp);
            CHECK(same_pos(*thing_pos(w), *thing_pos(castle)));
            CHECK(same_pos(sp->home, start));
            sp->cast_ticks = sp->duration;
            spell_teleport_s30_update(sp);
            CHECK(same_pos(*thing_pos(w), start));
            CHECK_EQ(sp->home.z, 0);
            CHECK_EQ(sp->rng, r);                           // no draw with a castle
            P->castle = 0;
        }
        thing_move_to(w, &from);
        sp->cast_ticks = 0;
        sp->rng = sp0.rng;
        sp->home = sp0.home;
        CHECK(same_thing(*sp, sp0));
    }

    // Castle (16): the seed projectile; the spell then stays "busy" at duration - 1.
    {
        pose(w);
        Thing *sp = spell_of(w, 16);
        const Thing sp0 = *sp;
        const std::vector<int> before = things_of(9);
        g_snd.clear();
        sp->cast_ticks = sp->duration;
        const int aim = P->aim_charge;
        spell_castle_s48_update(sp);
        std::vector<int> made = new_things(9, before);
        CHECK_EQ(made.size(), 1);
        CHECK_EQ(sp->cast_ticks, sp->duration - 1);
        CHECK_EQ(w->mana_cost, -sp->mana_total);
        if (made.size() == 1) {
            Thing *p = thing_at(made[0]);
            CHECK_EQ(p->type, 0xa);
            CHECK(p->impact_cls == 3 && p->impact_type == 2);           // no castle yet: becomes one
            CHECK_EQ(p->target, 0);
            CHECK_EQ(p->damage, sp->damage);
            CHECK_EQ(p->mana, sp->mana);
            CHECK_EQ(p->owner, w->owner);
            CHECK_EQ(p->aux, aim);
            CHECK_EQ(P->aim_charge, 0);
            CHECK_EQ(p->speed_cur, 0x180 + w->speed_cur);
            CHECK(p->yaw == w->yaw && p->pitch == w->pitch);
            Pos o = expect_origin(w);
            CHECK(p->x == o.x && p->y == o.y && p->z == o.z + w->ext_h);
            Pos h = *thing_pos(w);
            math_rotate_offset(&h, w->yaw, 0, 0x1000);
            h.z = (int16_t)terrain_height_at(&h);
            CHECK(same_pos(p->home, h));
            CHECK(g_snd.size() == 1 && g_snd[0].thing == made[0] && g_snd[0].sound == 0xf);
        }
        // busy: further updates change nothing
        const Thing busy = *sp;
        for (int i = 0; i < 300; i++) spell_castle_s48_update(sp);
        CHECK(same_thing(*sp, busy));
        CHECK_EQ(new_things(9, before).size(), 1);
        // ... until the caster cannot cast (dead): released
        w->health = -1;
        spell_castle_s48_update(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        w->health = w->max_health;
        for (int i : made) thing_free(thing_at(i));

        // with a castle: the seed flies to it and carries the upgrade effect
        Thing *castle = any_castle();
        if (castle) {
            P->castle = thing_index(castle);
            sp->cast_ticks = sp->duration;
            spell_castle_s48_update(sp);
            made = new_things(9, before);
            CHECK_EQ(made.size(), 1);
            if (made.size() == 1) {
                Thing *p = thing_at(made[0]);
                CHECK(p->impact_cls == 10 && p->impact_type == 0x2b);
                CHECK_EQ(p->target, thing_index(castle));
                CHECK(p->home.x == 0 && p->home.y == 0 && p->home.z == 0);
                thing_free(p);
            }
            P->castle = 0;
        }
        // no mana for it: refused, cast_ticks drops to 0 (not to duration - 1)
        sp->cast_ticks = sp->duration;
        w->mana = sp->mana_total - 1;
        spell_castle_s48_update(sp);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(new_things(9, before).size(), 0);
        restore(sp, sp0);
    }
    std::printf("self spells: heal, speed-up / reverse, shield, rebound, invisible, beyond sight, teleport, castle pass\n");
}

// ---- 4. dropped spells and level pickups -------------------------------------------------------

static bool find_cell(bool water, Pos *out) {
    Thing *w = wiz();
    for (int cy = 0; cy < 256; cy++)
        for (int cx = 0; cx < 256; cx++) {
            Pos p{(uint16_t)(cx * 256 + 0x80), (uint16_t)(cy * 256 + 0x80), 0};
            if (pos_dist_xy(&p, thing_pos(w)) < 0x1000) continue;
            bool wet = (terrain_cell_flag_bit(&p) & 1) && terrain_height_at(&p) == 0;
            bool dry = !(terrain_cell_flag_bit(&p) & 1) && terrain_height_at(&p) > 0x100 && g_cell_things[mc_cell(cx, cy)] == 0;
            if (water ? wet : dry) { *out = p; return true; }
        }
    return false;
}

static void test_pickups() {
    Thing *w = wiz();
    PlayerBlock *P = player_block(w);
    pose(w);
    tick();                                                 // fresh per-class lists (the player list)
    CHECK(g_cfg->player_list != 0);
    w = wiz();

    // Take spell 7 out of the book and drop it the way player_dying_update does.
    Thing *sp = spell_of(w, 7);
    CHECK(sp != nullptr);
    if (!sp) return;
    const int slot = slot_of(w, 7);
    P->spell_slot[slot] = 0;
    player_rebuild_spell_index(w);
    CHECK_EQ(P->spell_thing[7], 0);
    ThingUpdateFn fn1 = thing_update_fn(12, 7 * 3 + 1);
    CHECK(fn1 != nullptr);
    if (!fn1) return;

    // (a) on dry land, away from the wizard: falls 0x80 per tick, lies on the ground, expires
    Pos land{};
    CHECK(find_cell(false, &land));
    {
        int ground = terrain_height_at(&land);
        land.z = (int16_t)(ground + 0x1c0);
        thing_move_to(sp, &land);
        sp->flags &= ~1u;
        sp->state = 7 * 3 + 1;
        sp->caster = 0;
        sp->health = 50;
        sp->tick = 0;
        fn1(sp);
        CHECK_EQ(sp->z, ground + 0x140);
        fn1(sp); fn1(sp);
        CHECK_EQ(sp->z, ground + 0x40);
        fn1(sp);
        CHECK_EQ(sp->z, ground);
        CHECK_EQ(sp->health, 46);
        CHECK_EQ(sp->state, 7 * 3 + 1);
        for (int i = 0; i < 45; i++) fn1(sp);
        CHECK_EQ(sp->health, 1);
        CHECK(!(sp->flags & 0x400));
        fn1(sp);
        CHECK(sp->flags & 0x400);                           // 50 ticks: marked for deletion
        CHECK_EQ(sp->z, ground);
        sp->flags &= ~0x400u;
        // health 0 = no expiry (level-placed spells)
        sp->health = 0;
        for (int i = 0; i < 300; i++) fn1(sp);
        CHECK(!(sp->flags & 0x400) && sp->health == 0 && sp->z == ground);
        CHECK(!(sp->flags & 1));                            // the local player does not own spell 7 now
    }

    // (b) over water: sinks (0x80 per tick above the surface, 0x20 below) and is deleted at -0x300
    Pos sea{};
    if (find_cell(true, &sea)) {
        sea.z = 0x100;
        thing_move_to(sp, &sea);
        sp->health = 0;
        int n = 0;
        while (!(sp->flags & 0x400) && n < 1000) { fn1(sp); n++; }
        CHECK_EQ(sp->z, -0x300);
        CHECK_EQ(n, 2 + 0x300 / 0x20);
        sp->flags &= ~0x400u;
        std::printf("pickup: a dropped spell over water sinks and is deleted after %d ticks\n", n);
    } else {
        std::printf("pickup: no water cell found, sinking not tested\n");
        g_fail++;
    }

    // (c) at the wizard: picked up on a tick with (Thing.tick & 3) == 0
    {
        Thing *ai = thing_at(g_state->players[1].thing);
        PlayerBlock *Q = player_block(ai);
        CHECK_EQ(ai->type, 1);
        const uint16_t q_thing = Q->spell_thing[7], q_want = Q->ai_want_spell[7];
        const uint8_t q_allowed = Q->ai_allowed[7];
        Q->spell_thing[7] = 0; Q->ai_want_spell[7] = 0; Q->ai_allowed[7] = 1;
        thing_move_to(sp, thing_pos(w));
        sp->health = 77;
        sp->flags |= 0x40000;                               // "sealed": costs no castle mana once owned
        sp->mana_cost = 1234;
        std::memset(P->hotkey_slot, 0, 4);
        P->hotkey_slot[4] = 0xff; P->hotkey_slot[5] = 0xff;
        P->slot_left = 3;
        g_snd.clear();
        sp->tick = 1;
        fn1(sp);
        CHECK_EQ(sp->state, 7 * 3 + 1);                     // not on this tick
        CHECK_EQ(Q->ai_want_spell[7], 0);
        w->health = -1;
        sp->tick = 4;
        thing_move_to(sp, thing_pos(w));                    // (it falls 0x80 per update)
        fn1(sp);
        CHECK_EQ(sp->state, 7 * 3 + 1);                     // a dead flyer picks nothing up
        w->health = w->max_health;
        sp->tick = 8;
        thing_move_to(sp, thing_pos(w));
        fn1(sp);
        CHECK_EQ(sp->state, 7 * 3);
        CHECK_EQ(sp->caster, thing_index(w));
        CHECK(sp->flags & 1);
        CHECK_EQ(sp->mana_cost, 0);
        CHECK_EQ(sp->health, 74);                           // the countdown simply stops
        CHECK_EQ(P->spell_slot[slot], thing_index(sp));     // the first empty slot is the one we emptied
        CHECK_EQ(P->slot_left, slot);
        CHECK_EQ(P->hotkey_slot[4], slot);
        CHECK_EQ(P->hotkey_slot[5], 0xff);
        CHECK(g_snd.size() == 1 && g_snd[0].thing == thing_index(w) && g_snd[0].player == -1 && g_snd[0].sound == 0x12);
        CHECK_EQ(Q->ai_want_spell[7], 200);                 // the AI wizard now wants that spell
        Q->spell_thing[7] = q_thing; Q->ai_want_spell[7] = q_want; Q->ai_allowed[7] = q_allowed;
        sp->flags &= ~0x40000u;
        player_rebuild_spell_index(w);
        CHECK_EQ(P->spell_thing[7], thing_index(sp));
    }

    // (d) a second copy of a spell the wizard owns: flagged for the local player, never picked up
    {
        Thing *dup = thing_create(thing_pos(w), 12, 7);
        CHECK(dup != nullptr);
        if (dup) {
            dup->state = 7 * 3 + 1;
            dup->tick = 0;
            CHECK(!(dup->flags & 1));
            g_snd.clear();
            fn1(dup);
            CHECK(dup->flags & 1);
            CHECK_EQ(dup->state, 7 * 3 + 1);
            CHECK_EQ(dup->caster, 0);
            CHECK_EQ(g_snd.size(), 0);
            thing_free(dup);
        }
    }

    // (e) a full book: not picked up
    {
        Thing *other = spell_of(w, 9);
        const int s9 = slot_of(w, 9);
        P->spell_slot[s9] = thing_index(spell_of(w, 0));    // slot occupied by a second fireball entry
        player_rebuild_spell_index(w);
        CHECK_EQ(P->spell_thing[9], 0);
        other->state = 9 * 3 + 1;
        other->caster = 0;
        other->flags &= ~1u;
        other->tick = 0;
        thing_move_to(other, thing_pos(w));
        thing_update_fn(12, other->state)(other);
        CHECK_EQ(other->state, 9 * 3 + 1);
        P->spell_slot[s9] = 0;
        thing_move_to(other, thing_pos(w));
        thing_update_fn(12, other->state)(other);           // with the slot free again it is taken
        CHECK_EQ(other->state, 9 * 3);
        CHECK_EQ(P->spell_slot[s9], thing_index(other));
        player_rebuild_spell_index(w);
    }

    // (f) phase 2, the level pickup: taken like a dropped spell, and a fresh copy stays behind
    {
        Thing *own = spell_of(w, 13);
        const int s13 = slot_of(w, 13);
        P->spell_slot[s13] = 0;
        player_rebuild_spell_index(w);
        thing_free(own);
        Thing *pk = thing_create(thing_pos(w), 12, 13);
        CHECK(pk != nullptr);
        if (pk) {
            pk->state = (uint8_t)(pk->state + 2);           // what level_spawn_thing_record does (SwiId 2)
            pk->tick = 0;
            const std::vector<int> before = things_of(12);
            ThingUpdateFn fn2 = thing_update_fn(12, pk->state);
            CHECK(fn2 != nullptr);
            if (fn2) fn2(pk);
            CHECK_EQ(pk->state, 13 * 3);
            CHECK_EQ(pk->caster, thing_index(w));
            CHECK_EQ(P->spell_slot[s13], thing_index(pk));
            std::vector<int> made = new_things(12, before);
            CHECK_EQ(made.size(), 1);
            if (made.size() == 1) {
                Thing *n = thing_at(made[0]);
                CHECK_EQ(n->type, 13);
                CHECK_EQ(n->state, 13 * 3 + 2);
                CHECK_EQ(n->caster, 0);
                CHECK(same_pos(*thing_pos(n), *thing_pos(pk)));
                CHECK(!(n->flags & 0x40001));
                CHECK_EQ(n->sprite, 0x4d);
                CHECK_EQ(n->mana_total, pk->mana_total);
                // the copy is itself a pickup, but not for a wizard who has the spell
                player_rebuild_spell_index(w);
                n->tick = 0;
                if (fn2) fn2(n);
                CHECK_EQ(n->state, 13 * 3 + 2);
                CHECK(n->flags & 1);
                CHECK_EQ(new_things(12, before).size(), 1);
                thing_free(n);
            }
        }
        player_rebuild_spell_index(w);
    }

    // The Table A oddity: state 68 (phase 2 of spell 22) is bound to the cast handler 0x49140.
    CHECK(thing_update_fn(12, 68) == thing_update_fn(12, 66));
    CHECK(thing_update_fn(12, 68) != thing_update_fn(12, 71));
    CHECK(thing_update_fn(12, 67) == thing_update_fn(12, 1));
    int bound = 0;
    for (int s = 0; s < 72; s++) if (thing_update_fn(12, s)) bound++;
    CHECK_EQ(bound, 72);
    std::printf("pickup: dropped / level pickup cases pass; all 72 class-12 Table A records are bound\n");
}

// ---- 5. casting through the real tick (player_cast_spell -> handler -> flyer update) ------------

// The flyer update applies Thing.mana_cost; record what it finds there on every update.
static std::vector<int32_t> g_rates;
static void wizard_update_spy(Thing *t) {
    if (t == wiz()) g_rates.push_back(t->mana_cost);
    player_type0_s0_update(t);
}
static int count_rate(int32_t v) {
    int n = 0;
    for (int32_t r : g_rates) if (r == v) n++;
    return n;
}
// mana after the recorded updates: mana += rate, clamped to 0..total (player_type0_s0_update)
static int32_t apply_rates(int32_t mana, int32_t total) {
    for (int32_t r : g_rates) {
        mana += r;
        if (mana < 0) mana = 0;
        if (mana > total) mana = total;
    }
    return mana;
}

static void test_ticks() {
    CHECK(start_level(38, true));
    thing_register_update(0x402c0, wizard_update_spy);
    Thing *w = wiz();
    PlayerBlock *P = player_block(w);
    CHECK_EQ(w->mana, 1000);
    const int32_t regen = 100;                              // mana_total / 2000, at least 100

    // Fireball held for 60 ticks with 1000 mana: fires while there is mana for a shot.
    {
        const std::vector<int> before = things_of(9);
        tick(0x15, slot_of(w, 0));
        CHECK_EQ(P->slot_left, slot_of(w, 0));
        g_rates.clear();
        g_snd.clear();
        const int32_t mana0 = w->mana;
        int launches = 0, prev = 0;
        std::vector<int> launch_ticks;
        for (int i = 0; i < 60; i++) {
            tick(6, 0, 0x10);
            int now = (int)new_things(9, before).size();
            if (now != prev) launch_ticks.push_back(i);
            launches += now - prev;
            prev = now;
            CHECK(w->mana >= 0 && w->mana <= w->mana_total);
        }
        for (int i = 0; i < 8; i++) tick();                 // let the last cast and charge run out
        CHECK_EQ(w->mana, apply_rates(mana0, 1000));
        CHECK_EQ(count_rate(-200), launches);               // every shot is charged exactly once
        for (int32_t r : g_rates) CHECK(r == -200 || r == 0 || r == regen);
        // 5 shots empty the 1000 mana; then 100 mana per tick pay for a shot every 3rd tick: the
        // flyer arms the spell on the mana it has *before* its own update applies the last charge,
        // so every shot is followed by one armed-but-refused cast (the "cannot" sound 0x1d), which
        // ends the cast at once and lets the mana regenerate.
        CHECK_EQ(launches, 5 + 18);
        CHECK_EQ(snd_count(0x1d), launches - 5);            // none after the first four and the last shot
        std::printf("ticks: fireball held 60 ticks from 1000 mana: %d shots (at ticks", launches);
        for (int t : launch_ticks) std::printf(" %d", t);
        std::printf("), %d refused, rates: %d x -200, %d x 0, %d x +%d\n", snd_count(0x1d), count_rate(-200), count_rate(0),
                    count_rate(regen), regen);
        for (int i : new_things(9, before)) {
            CHECK_EQ(thing_at(i)->type, 0);
            CHECK_EQ(thing_at(i)->owner, w->owner);
            thing_free(thing_at(i));
        }
    }

    // One tap of a long spell: 1 launch, mana_total charged once, no regeneration for the other
    // duration - 1 cast ticks - except the earthquake, which regenerates during its cast.
    for (int id : {7, 6}) {
        give_mana(w, 50000);
        tick(0x15, slot_of(w, id));
        for (int i = 0; i < 3; i++) tick();
        const std::vector<int> before = things_of(9);
        Thing *sp = spell_of(w, id);
        g_rates.clear();
        const int32_t mana0 = w->mana;
        tick(6, 0, 0x10);
        for (int i = 0; i < sp->duration + 5; i++) tick();
        CHECK_EQ(new_things(9, before).size(), 1);
        CHECK_EQ(count_rate(-sp->mana_total), 1);
        CHECK_EQ(count_rate(0), id == 6 ? 0 : sp->duration - 1);
        CHECK_EQ(count_rate(100), (int)g_rates.size() - 1 - count_rate(0));
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(w->mana, apply_rates(mana0, 50000));
        std::printf("ticks: spell %d tapped: 1 projectile, rates over %zu updates: 1 x %d, %d x 0, %d x +100 (duration %d)\n", id,
                    g_rates.size(), -sp->mana_total, count_rate(0), count_rate(100), sp->duration);
        for (int i : new_things(9, before)) thing_free(thing_at(i));
    }

    // Shield through a real hit: a quarter of the damage, paid again in mana (player_apply_hits).
    {
        give_mana(w, 50000);
        tick(0x15, slot_of(w, 4));
        P->invuln_timer = 0;
        tick(6, 0, 0x10);
        Thing *sh = spell_of(w, 4);
        CHECK(sh->cast_ticks > 0);
        tick();
        CHECK(w->flags & 0x4000);
        w->damage_slots[0].amount = 400;
        w->damage_slots[0].attacker = g_state->players[1].thing;
        const int32_t hp = w->health;
        tick();
        CHECK_EQ(w->health, hp - 100);
        // the flag is consumed by the hit and set again by the spell's next tick
        int with_flag = 0;
        for (int i = 0; i < 20; i++) { tick(); if (w->flags & 0x4000) with_flag++; }
        CHECK_EQ(with_flag, 20);
        w->health = w->max_health;
    }

    // Speed-up through the real flyer: 3x on the first tick, 2x afterwards, base speed at the end.
    {
        give_mana(w, 50000);
        tick(0x15, slot_of(w, 2));
        for (int i = 0; i < 20; i++) tick(6, 0, 1);         // accelerate to the top speed
        CHECK_EQ(P->target_speed, 0x50);
        const std::vector<int> before = things_of(10);
        const Pos p0 = *thing_pos(w);
        tick(6, 0, 0x10);
        Thing *sp = spell_of(w, 2);
        CHECK(sp->cast_ticks >= sp->duration - 1);
        int max_speed = w->speed_cur, ticks_fast = w->speed_cur > 0x50 ? 1 : 0;
        for (int i = 0; i < sp->duration + 10; i++) {
            tick();
            if (w->speed_cur > max_speed) max_speed = w->speed_cur;
            if (w->speed_cur > 0x50) ticks_fast++;
        }
        CHECK_EQ(max_speed, 0xf0);
        CHECK(ticks_fast >= sp->duration - 2 && ticks_fast <= sp->duration + 1);
        CHECK_EQ(sp->cast_ticks, 0);
        CHECK_EQ(P->target_speed, 0x50);
        CHECK_EQ(w->speed_cur, 0x50);
        int puffs = (int)new_things(10, before).size();
        CHECK(puffs >= sp->duration / 4 - 1 && puffs <= sp->duration / 4 + 1);
        std::printf("ticks: speed-up: top speed %d (base 80), faster than base for %d ticks, %d smoke puffs, wizard moved %d cells\n",
                    max_speed, ticks_fast, puffs, pos_dist_xy(&p0, thing_pos(w)) / 256);
        for (int i : new_things(10, before)) thing_free(thing_at(i));
    }
    thing_register_update(0x402c0, player_type0_s0_update);
}

// ---- 6. the snapshot ---------------------------------------------------------------------------

struct SnapSpell { Thing t; int index; int owner_player; };

static int player_of_thing(int thing) {
    for (int p = 0; p < g_state->player_count; p++)
        if (g_state->players[p].thing == thing) return p;
    return -1;
}

static void test_snapshot() {
    CHECK(sim_load_snapshot("movie/gam00000.dat", "movie/map00000.dat"));
    g_cfg->flags = 0;
    g_cfg->paused = 0;
    std::vector<SnapSpell> snap;
    int by_phase[3] = {};
    for (int i = 1; i < MC_THING_SLOTS; i++) {
        const Thing *t = thing_at(i);
        if (t->cls != 12) continue;
        snap.push_back({*t, i, t->caster ? player_of_thing(t->caster) : -1});
        by_phase[t->state % 3]++;
        // invariants of every spell Thing the original produced
        CHECK(t->type < 24);
        CHECK_EQ(t->state / 3, t->type);
        CHECK(t->cast_ticks >= 0 && t->cast_ticks <= t->duration);
        CHECK_EQ(t->player, 0);                             // spells never get a player block
        CHECK_EQ(t->burst, 0);
        if (t->duration) CHECK_EQ(t->mana, t->mana_total / t->duration);
        if (t->state % 3 == 0) {
            CHECK(t->caster != 0);
            const Thing *c = thing_at(t->caster);
            CHECK(c->cls == 3 && c->type <= 1);
            CHECK_EQ(player_block(c)->spell_thing[t->type], i);
            CHECK(t->flags & 1);
        } else {
            CHECK_EQ(t->caster, 0);
            Pos p = *thing_pos(t);
            if (!(terrain_cell_flag_bit(&p) & 1)) CHECK_EQ(t->z, terrain_height_at(&p));   // lies on the ground
        }
    }
    CHECK_EQ(snap.size(), 39);
    std::printf("snapshot: %zu spells: %d owned (phase 0), %d dropped (phase 1), %d level pickups (phase 2)\n", snap.size(),
                by_phase[0], by_phase[1], by_phase[2]);

    // Step the snapshot: every spell is idle and the AI is not linked, so 500 updates must leave all
    // 39 spell Things unchanged except Thing.tick (no RNG draw, no countdown, no flag change).
    {
        check_pool();
        g_snd.clear();
        for (int i = 0; i < 500; i++) thing_update_all();
        check_pool();
        int changed = 0;
        for (const SnapSpell &s : snap) {
            Thing now = *thing_at(s.index);
            CHECK_EQ(now.tick, (uint8_t)(s.t.tick + 500));
            now.tick = s.t.tick;
            if (!same_thing(now, s.t)) changed++;
        }
        CHECK_EQ(changed, 0);
        CHECK_EQ(snd_count(0x1d) + snd_count(0x12), 0);
        std::printf("snapshot: 500 updates, %d of 39 spell Things changed (expected 0)\n", changed);
    }

    // The same moment reached by the port: level 38, "access all spells" on tick 390 (the snapshot's
    // cheat spells have had 23 updates: Thing.tick - (index & 0xff)), compared after the update of
    // tick 412 - the snapshot was written while tick 413's commands were processed.
    CHECK(start_level(38, false));
    for (int guard = 0; g_state->players[0].tick < 389 && guard < 1000; guard++) tick();
    tick(0x1e, 1);
    CHECK_EQ(g_state->players[0].tick, 390);
    for (int guard = 0; g_state->players[0].tick < 412 && guard < 1000; guard++) tick();
    CHECK_EQ(things_of(12).size(), 39);
    int same_index = 0, compared = 0, mismatched = 0;
    std::vector<bool> used(MC_THING_SLOTS, false);
    for (const SnapSpell &s : snap) {
        // the counterpart: same owner player, type and phase
        int found = 0;
        for (int i = 1; i < MC_THING_SLOTS && !found; i++) {
            const Thing *t = thing_at(i);
            if (t->cls != 12 || used[i] || t->type != s.t.type || t->state != s.t.state) continue;
            int owner = t->caster ? player_of_thing(t->caster) : -1;
            if (owner == s.owner_player) found = i;
        }
        CHECK(found != 0);
        if (!found) continue;
        used[found] = true;
        compared++;
        Thing a = *thing_at(found), b = s.t;
        if (found == s.index) {
            same_index++;
            CHECK_EQ(a.tick, b.tick);                       // same number of updates since the level start
            CHECK_EQ(a.rng, b.rng);                         // no draw in 412 ticks (seed + index at creation)
        }
        // not comparable: list / cell links, indices, per-index seeds, and the Castle spell capacity
        // of the AI wizards (their castles grew; the AI is not ported)
        a.owner = b.owner = 0;                              // = own index
        a.caster = b.caster = 0;
        a.rng = b.rng = 0;
        a.tick = b.tick = 0;
        if (s.t.type == 16 && s.owner_player > 0) { a.mana_total = b.mana_total = 0; a.mana = b.mana = 0; }
        if (!same_thing(a, b)) {
            mismatched++;
            std::printf("  snapshot spell %d (type %d state %d owner %d) differs from port spell %d:", s.index, s.t.type, s.t.state,
                        s.owner_player, found);
            const uint8_t *pa = reinterpret_cast<const uint8_t *>(&a), *pb = reinterpret_cast<const uint8_t *>(&b);
            for (size_t k = 0x18; k < sizeof a; k++) if (pa[k] != pb[k]) std::printf(" +0x%zx(%02x/%02x)", k, pb[k], pa[k]);
            std::printf("\n");
        }
    }
    CHECK_EQ(compared, 39);
    CHECK_EQ(mismatched, 0);
    std::printf("snapshot vs port (level 38, 412 updates, cheat on tick 390): %d spells compared byte for byte "
                "(minus links / indices), %d differ; %d have the same Thing index (tick and rng equal too)\n",
                compared, mismatched, same_index);
    CHECK(same_index >= 15);                                // the level spell and the 14 AI spells
}

// ---- 7. smoke runs -----------------------------------------------------------------------------

static void print_unported() {
    std::printf("   handlers dispatched but not ported (other subsystems):\n");
    int missing = thing_dispatch_report(stdout);
    std::printf("   %d distinct\n", missing);
}

// Player 0 with all 24 spells: every 60 ticks the next spell goes into the left hand, the fire key
// is held for 12 ticks, then the wizard flies on a little.
static void scripted_run(int level, int ticks) {
    if (!start_level(level, true)) { std::printf("smoke: level %d failed to load\n", level); g_fail++; return; }
    thing_dispatch_reset_stats();
    spies_reset();
    Thing *w = wiz();
    int have = 0;
    for (int id = 0; id < 24; id++) if (spell_of(w, id)) have++;
    CHECK_EQ(have, 24);
    if (have != 24) return;
    uint32_t spell_rng[24];
    for (int id = 0; id < 24; id++) spell_rng[id] = spell_of(w, id)->rng;
    int min_spells = 1000, max_live = 0, reaped = 0;
    int shield = 0, rebound = 0, invisible = 0, active[24] = {};
    long long mana_spent = 0;
    g_snd.clear();
    for (int i = 0; i < ticks; i++) {
        const int id = (i / 60) % 24, step = i % 60;
        w = wiz();
        if (step == 0) give_mana(w, 200000);
        int cmd = 6, arg = 0, bits = 0;
        if (step == 0) { cmd = 0x15; arg = slot_of(w, id); }
        if (step >= 2 && step < 14) bits |= 0x10;           // hold the left hand for 12 ticks
        if (step >= 30 && step < 40) bits |= 1;
        if (step >= 45 && step < 55) bits |= 2;
        const int32_t mana0 = w->mana;
        tick(cmd, arg, bits, (i / 200) % 2 ? 30 : -30);
        reaped += reap();
        if (w->mana < mana0) mana_spent += mana0 - w->mana;
        if (w->flags & 0x4000) shield++;
        if (w->flags & 0x8000) rebound++;
        if (w->flags & 0x20) invisible++;
        // invariants
        CHECK(w->cls == 3 && w->state == 0);
        CHECK(w->mana >= 0 && w->mana <= w->mana_total);
        CHECK(w->yaw < 0x800);
        int n12 = 0, live = 0;
        for (int k = 1; k < MC_THING_SLOTS; k++) {
            const Thing *t = thing_at(k);
            if (t->cls == 0) continue;
            live++;
            if (t->cls != 12) continue;
            n12++;
            CHECK(t->cast_ticks >= 0 && t->cast_ticks <= t->duration);
            CHECK_EQ(t->state / 3, t->type);
            CHECK_EQ(t->burst, 0);
            if (t->cast_ticks > 0 && t->caster == thing_index(w)) active[t->type]++;
        }
        if (n12 < min_spells) min_spells = n12;
        if (live > max_live) max_live = live;
        if (i % 500 == 499) check_pool();
        if (g_fail > 30) break;
    }
    const int teleports = snd_count(0x16);
    // only the teleport spell draws from a spell Thing's RNG: once per cast (player 0 has no castle)
    for (int id = 0; id < 24; id++) {
        uint32_t r = spell_rng[id];
        if (id == 10) { for (int k = 0; k < teleports; k++) r = mc_lcg(r); }
        CHECK_EQ(spell_of(wiz(), id)->rng, r);
    }
    std::printf("smoke level %2d, %d ticks: projectiles created by type:", level, ticks);
    for (int k = 0; k < 20; k++) if (g_made_proj[k]) std::printf(" %x:%d", k, g_made_proj[k]);
    std::printf("\n   ticks with cast_ticks > 0 by spell id:");
    for (int id = 0; id < 24; id++) std::printf(" %d:%d", id, active[id]);
    std::printf("\n   smoke puffs %d, teleports %d, ticks with shield %d / rebound %d / invisible %d, refused (sound 0x1d) %d, "
                "picked up (0x12) %d,\n   mana spent %lld, spell Things alive >= %d, most live things %d, reaped %d\n",
                g_made_puff, teleports, shield, rebound, invisible, snd_count(0x1d), snd_count(0x12), mana_spent, min_spells,
                max_live, reaped);
    for (const Launch &L : k_launch) CHECK(g_made_proj[L.proj] > 0);
    CHECK(g_made_proj[0xa] > 0);                            // the castle seed
    CHECK(teleports > 0 && shield > 0 && rebound > 0 && invisible > 0 && g_made_puff > 0);
    print_unported();
}

static void movie_run(const char *game_dir) {
    g_cfg->flags = 0;
    if (!demo_open(game_dir, 0)) { std::printf("movie: mvi00000.dat missing, skipped\n"); return; }
    thing_dispatch_reset_stats();
    spies_reset();
    g_snd.clear();
    int ticks = 0, fired = 0, reaped = 0, active[24] = {};
    long long mana_spent = 0;
    int32_t prev_mana = 0;
    while (ticks < 9000 && !(g_state->players[g_state->local_player].status & 8)) {
        game_tick_sim();                                    // the first step replaces the state by the snapshot
        if (ticks == 0) { g_cfg->paused = 0; spies_reset(); }
        reaped += reap();
        Thing *w = wiz();
        PlayerBlock *P = player_block(w);
        if (P->input_bits & 0x30) fired++;
        if (ticks > 0 && w->mana < prev_mana) mana_spent += prev_mana - w->mana;
        prev_mana = w->mana;
        for (int id = 0; id < 24; id++) {
            Thing *sp = spell_of(w, id);
            if (!sp) continue;
            if (sp->cast_ticks > 0) active[id]++;
            CHECK(sp->cast_ticks >= 0 && sp->cast_ticks <= sp->duration);
        }
        CHECK(w->mana >= 0 && w->mana <= w->mana_total);
        if (ticks % 1000 == 999) check_pool();
        if (g_fail > 30) break;
        ticks++;
    }
    std::printf("movie: %d ticks replayed with the spell handlers, fire keys down in %d ticks, mana spent %lld\n", ticks, fired,
                mana_spent);
    std::printf("   projectiles created by type:");
    int total = 0;
    for (int k = 0; k < 20; k++) if (g_made_proj[k]) { std::printf(" %x:%d", k, g_made_proj[k]); total += g_made_proj[k]; }
    std::printf(" (total %d)\n   ticks with cast_ticks > 0 by spell id:", total);
    for (int id = 0; id < 24; id++) if (active[id]) std::printf(" %d:%d", id, active[id]);
    std::printf("\n   smoke puffs %d, teleports (sound 0x16) %d, refused (0x1d) %d, picked up (0x12) %d, reaped %d\n", g_made_puff,
                snd_count(0x16), snd_count(0x1d), snd_count(0x12), reaped);
    CHECK(ticks > 8000);
    print_unported();
    demo_close();
    g_cfg->flags = 0;
}

int main(int argc, char **argv) {
    mc_install_crash_handler();
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    if (!sim_init(game_dir)) { std::printf("sim_init failed\n"); return 2; }
    spells_register_handlers();
    g_hook_sound_request = snd_capture;
    install_spies();
    spies_reset();

    if (!start_level(38, true)) { std::printf("level 38 failed to load\n"); return 2; }
    {
        Thing *w = wiz();
        int have = 0;
        for (int id = 0; id < 24; id++) if (spell_of(w, id)) have++;
        CHECK_EQ(have, 24);
        CHECK(w->cls == 3 && w->type == 0);
        if (have != 24) { std::printf("spells_test: the wizard has %d spells, cannot continue\n", have); return 1; }
    }
    test_helpers();
    test_launches();
    test_self_spells();
    test_pickups();
    test_ticks();
    test_snapshot();
    scripted_run(38, 3000);
    scripted_run(0, 2500);
    scripted_run(12, 2500);
    scripted_run(24, 2500);
    movie_run(game_dir);

    std::printf("%s: %d failure(s)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}

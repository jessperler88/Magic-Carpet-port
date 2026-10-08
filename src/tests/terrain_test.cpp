// Regression test for terrain_gen.cpp: generates level 38 and compares the five maps with the
// engine's own dump MagicCarpet/magic/movie/map00000.dat (taken after level start, so castle
// footprints - flag 0x80 - and level-start terrain effects legitimately differ).
// argv[1] = game dir.
// Level 38 has 152 level-start terrain effects (wizard castles, walls, canyons, ridges, raise effects)
// that reshape about 15% of the map, so the comparison distinguishes three zones: castle footprints
// (reference flag 0x80), their 9-cell neighbourhood plus 9 cells around every terrain-effect
// THING_INIT ("effect zone"), and the rest ("pristine"). Exit 0 when the pristine height map matches
// >= 97% and at least 90% of all height mismatches fall into the first two zones; the real target,
// printed explicitly, is an exact match of all maps in the pristine zone. Also writes PGM/PPM dumps
// and a raw copy of our maps (type, height, light, flags) next to the exe.
#define _CRT_SECURE_NO_WARNINGS
#include "mc_globals.h"
#include "mc_math.h"
#include "terrain.h"
#include "mcfile.h"
#include "rnc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int kLevel = 38;

// levels.tab / levels.dat lookup + RNC unpack into g_state->level (as engine_load_level does).
static bool load_level(const char *game_dir, int index) {
    char path[1024];
    mc_blob tab, dat;
    mc_path_join(path, sizeof path, game_dir, "levels/levels.tab");
    if (!mc_read_file(path, &tab)) { std::fprintf(stderr, "cannot read %s\n", path); return false; }
    mc_path_join(path, sizeof path, game_dir, "levels/levels.dat");
    if (!mc_read_file(path, &dat)) { std::fprintf(stderr, "cannot read %s\n", path); mc_blob_free(&tab); return false; }
    bool ok = false;
    size_t n = tab.len / 4;
    if (index >= 0 && (size_t)index + 1 < n) {
        uint32_t a, b;
        std::memcpy(&a, tab.data + index * 4, 4);
        std::memcpy(&b, tab.data + (index + 1) * 4, 4);
        if (b <= a) b = (uint32_t)dat.len;
        if (a < dat.len && b <= dat.len) {
            long got = rnc_unpack(dat.data + a, b - a, reinterpret_cast<uint8_t *>(&g_state->level), sizeof(LevelData));
            if (got == (long)sizeof(LevelData)) ok = true;
            else std::fprintf(stderr, "level %d unpack returned %ld\n", index, got);
        }
    }
    mc_blob_free(&tab);
    mc_blob_free(&dat);
    return ok;
}

static bool is_terrain_effect(const ThingInit &t) {
    // level_run_terrain_effects_34fa0: class 10 types 9..0xb, 0x1b..0x20, 0x2d, 0x32, 0x33 (+0x21/0x22)
    return t.cls == 10 && ((t.model >= 9 && t.model <= 0xb) || (t.model >= 0x1b && t.model <= 0x22) ||
                           t.model == 0x2d || t.model == 0x32 || t.model == 0x33);
}

static void mark_square(std::vector<uint8_t> &m, int x, int y, int r) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            m[mc_cell(x + dx, y + dy)] = 1;
}

// Cells within Chebyshev distance 9 of a reference flag-0x80 cell (castle footprints plus the ring
// the castle code smooths: height changes reach 8 cells out on level 38) or within 9 of a level-start
// terrain effect (walls, canyons, ridges, wizard castles, raise effects: measured radius <= 9).
static const int kCastleRadius = 9, kEffectRadius = 9;
static std::vector<uint8_t> effect_zone(const uint8_t *ref_flags) {
    std::vector<uint8_t> zone(MC_MAP_CELLS, 0);
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i)
        if (ref_flags[i] & 0x80) mark_square(zone, i & 0xff, i >> 8, kCastleRadius);
    for (const ThingInit &t : g_state->level.things)
        if (is_terrain_effect(t)) mark_square(zone, t.x, t.y, kEffectRadius);
    return zone;
}

static int wrap_dist(int a, int b) { int d = (a - b) & 0xff; return d > 128 ? 256 - d : d; }

// Nearest THING_INIT of any class to a cell, for diagnosing mismatches in the pristine zone.
static void print_nearest_thing(unsigned cell) {
    int best = 999; const ThingInit *bt = nullptr;
    for (const ThingInit &t : g_state->level.things) {
        if (t.cls == 0) continue;
        int d = wrap_dist(t.x, cell & 0xff); int dy = wrap_dist(t.y, cell >> 8); if (dy > d) d = dy;
        if (d < best) { best = d; bt = &t; }
    }
    if (bt) std::printf("  [nearest thing: class %u type 0x%02x at (%u,%u), distance %d]", bt->cls, bt->model, bt->x, bt->y, best);
}

struct CmpResult {
    unsigned matched = 0, total = 0;                 // whole map
    unsigned pristine_total = 0, pristine_matched = 0;
    unsigned mism_built = 0, mism_zone = 0, mism_far = 0;
};

template <typename T>
static CmpResult compare_map(const char *name, const T *ref, const T *ours, const uint8_t *ref_flags,
                             const std::vector<uint8_t> &zone, unsigned mask = ~0u) {
    CmpResult r;
    r.total = MC_MAP_CELLS;
    int shown = 0;
    std::printf("== %s\n", name);
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) {
        bool built = (ref_flags[i] & 0x80) != 0, in_zone = zone[i] != 0;
        if (!built && !in_zone) ++r.pristine_total;
        if (((unsigned)ref[i] & mask) == ((unsigned)ours[i] & mask)) {
            ++r.matched;
            if (!built && !in_zone) ++r.pristine_matched;
            continue;
        }
        if (built) ++r.mism_built;
        else if (in_zone) ++r.mism_zone;
        else ++r.mism_far;
        if (shown < 10 && !built && !in_zone) {
            ++shown;
            std::printf("   pristine-zone mismatch (%3u,%3u): ref 0x%02x ours 0x%02x  ref_flags 0x%02x",
                        i & 0xff, i >> 8, (unsigned)ref[i] & mask, (unsigned)ours[i] & mask, ref_flags[i]);
            print_nearest_thing(i);
            std::printf("\n");
        }
    }
    unsigned mism = r.total - r.matched;
    std::printf("   matched %u / %u (%.3f%%); pristine zone %u / %u%s\n", r.matched, r.total,
                100.0 * r.matched / r.total, r.pristine_matched, r.pristine_total,
                r.pristine_matched == r.pristine_total ? " EXACT" : "");
    if (mism)
        std::printf("   mismatches %u: on flag-0x80 cells %u, in the effect zone %u, in the pristine zone %u\n",
                    mism, r.mism_built, r.mism_zone, r.mism_far);
    return r;
}

static void write_pgm(const std::string &path, const uint8_t *data, int scale) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P5\n256 256\n255\n");
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) { int v = data[i] * scale; std::fputc(v > 255 ? 255 : v, f); }
    std::fclose(f);
}

// Grey = reference value, red = mismatch in the pristine zone, yellow = mismatch on a castle / effect
// cell, blue tint = castle footprint that matches.
static void write_diff_ppm(const std::string &path, const uint8_t *ref, const uint8_t *ours,
                           const uint8_t *ref_flags, const std::vector<uint8_t> &zone) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n256 256\n255\n");
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) {
        uint8_t g = (uint8_t)(ref[i] > 0x7f ? 0xff : ref[i] * 2);
        uint8_t px[3] = { g, g, g };
        bool legit = (ref_flags[i] & 0x80) || zone[i];
        if (ref[i] != ours[i]) { px[0] = 255; px[1] = legit ? 255 : 0; px[2] = 0; }
        else if (ref_flags[i] & 0x80) { px[2] = 255; }
        std::fwrite(px, 1, 3, f);
    }
    std::fclose(f);
}

int main(int argc, char **argv) {
    const char *game_dir = argc > 1 ? argv[1] : MC_DEFAULT_GAME_DIR;
    mc_globals_init();
    if (!load_level(game_dir, kLevel)) return 2;

    const GenMap &g = g_state->level.gen;
    std::printf("level %d GEN_MAP: seed 0x%x off 0x%x raise %d gnarl %d river %d sourc %d snlin %d snflt %d bhlin %d bhflt %d rkste %d\n",
                kLevel, g.seed, g.off, g.raise, g.gnarl, g.river, g.sourc, g.snlin, g.snflt, g.bhlin, g.bhflt, g.rkste);

    // Level-start terrain effects (class 10 things that reshape the terrain), for the integrator.
    int effects = 0, by_type[256] = {};
    for (const ThingInit &t : g_state->level.things)
        if (is_terrain_effect(t)) { ++effects; ++by_type[t.model & 0xff]; }
    std::printf("   %d level-start terrain effects:", effects);
    for (int m = 0; m < 256; ++m) if (by_type[m]) std::printf(" type 0x%02x x%d", m, by_type[m]);
    std::printf("\n");

    terrain_build(g);

    char path[1024];
    mc_path_join(path, sizeof path, game_dir, "movie/map00000.dat");
    mc_blob dump;
    if (!mc_read_file(path, &dump)) { std::fprintf(stderr, "cannot read %s\n", path); return 2; }
    const size_t expect = 0x10000 * 4 + 0x20000 + 0x12c2;
    if (dump.len != expect) { std::fprintf(stderr, "%s: size %zu, expected %zu\n", path, dump.len, expect); return 2; }
    const uint8_t  *ref_type   = dump.data;
    const uint8_t  *ref_height = dump.data + 0x10000;
    const uint8_t  *ref_light  = dump.data + 0x20000;
    const uint8_t  *ref_flags  = dump.data + 0x30000;
    const uint16_t *ref_cells  = reinterpret_cast<const uint16_t *>(dump.data + 0x40000);
    uint16_t ref_rng16;
    std::memcpy(&ref_rng16, dump.data + 0x60000, 2);

    unsigned built = 0;
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) built += (ref_flags[i] & 0x80) != 0;
    std::vector<uint8_t> zone = effect_zone(ref_flags);
    unsigned zone_count = 0;
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) zone_count += zone[i] && !(ref_flags[i] & 0x80);
    std::printf("reference: %u cells flagged 0x80, %u more in the effect zone, %u pristine; dump g_rng16 0x%04x, ours 0x%04x\n",
                built, zone_count, MC_MAP_CELLS - built - zone_count, ref_rng16, g_rng16);

    CmpResult rh = compare_map("g_map_height", ref_height, g_map_height, ref_flags, zone);
    CmpResult rf = compare_map("g_map_flags (all bits)", ref_flags, g_map_flags, ref_flags, zone);
    CmpResult rc = compare_map("g_map_flags (class bits 0-2)", ref_flags, g_map_flags, ref_flags, zone, 7u);
    CmpResult rt = compare_map("g_map_type", ref_type, g_map_type, ref_flags, zone);
    CmpResult rl = compare_map("g_map_light", ref_light, g_map_light, ref_flags, zone);
    // g_cell_things holds thing links after level start; the generator leaves it zero.
    unsigned cells_nonzero = 0;
    for (unsigned i = 0; i < MC_MAP_CELLS; ++i) cells_nonzero += ref_cells[i] != 0;
    std::printf("== g_cell_things: reference has %u non-zero cells (things placed at level start); ours all zero\n", cells_nonzero);

    std::string exe = argv[0];
    size_t slash = exe.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? std::string(".") : exe.substr(0, slash + 1);
    {   // raw dump of our maps for offline analysis (type, height, light, flags: 4 x 64 KB)
        FILE *f = std::fopen((dir + "terrain_ours.bin").c_str(), "wb");
        if (f) {
            std::fwrite(g_map_type, 1, MC_MAP_CELLS, f); std::fwrite(g_map_height, 1, MC_MAP_CELLS, f);
            std::fwrite(g_map_light, 1, MC_MAP_CELLS, f); std::fwrite(g_map_flags, 1, MC_MAP_CELLS, f);
            std::fclose(f);
        }
    }
    write_pgm(dir + "terrain_height_ours.pgm", g_map_height, 1);
    write_pgm(dir + "terrain_height_ref.pgm", ref_height, 1);
    write_pgm(dir + "terrain_type_ours.pgm", g_map_type, 1);
    write_pgm(dir + "terrain_type_ref.pgm", ref_type, 1);
    write_diff_ppm(dir + "terrain_height_diff.ppm", ref_height, g_map_height, ref_flags, zone);
    write_diff_ppm(dir + "terrain_type_diff.ppm", ref_type, g_map_type, ref_flags, zone);
    std::printf("wrote PGM/PPM dumps and terrain_ours.bin to %s\n", dir.c_str());

    bool exact_h = rh.mism_far == 0, exact_f = rf.mism_far == 0, exact_t = rt.mism_far == 0, exact_l = rl.mism_far == 0;
    std::printf("pristine zone (outside castle footprints + %d and terrain effects + %d): height %s, flags %s (class bits %s), type %s, light %s\n",
                kCastleRadius, kEffectRadius,
                exact_h ? "EXACT" : "DIFFERS", exact_f ? "EXACT" : "DIFFERS", rc.mism_far == 0 ? "EXACT" : "DIFFERS",
                exact_t ? "EXACT" : "DIFFERS", exact_l ? "EXACT" : "DIFFERS");

    unsigned mism = rh.total - rh.matched;
    double pristine_pct = rh.pristine_total ? 100.0 * rh.pristine_matched / rh.pristine_total : 0.0;
    bool pct_ok = pristine_pct >= 97.0;
    bool concentrated = mism == 0 || (double)(rh.mism_built + rh.mism_zone) / mism >= 0.9;
    bool pass = pct_ok && concentrated;
    std::printf("%s (height: whole map %.3f%%, pristine zone %.3f%%, %.1f%% of the mismatches on castle / effect cells)\n",
                pass ? "PASS" : "FAIL", 100.0 * rh.matched / rh.total, pristine_pct,
                mism ? 100.0 * (rh.mism_built + rh.mism_zone) / mism : 100.0);
    mc_blob_free(&dump);
    mc_globals_shutdown();
    return pass ? 0 : 1;
}

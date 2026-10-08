#include "level.h"
#include "mcfile.h"
#include "rnc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t  rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, int32_t v) { uint32_t u = (uint32_t)v; p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8); p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int mc_level_parse(const uint8_t *d, size_t len, mc_level *lv) {
    if (len != MC_LEVEL_SIZE) return 0;
    int32_t *g = &lv->gen.unk0;
    for (int i = 0; i < 12; i++) g[i] = rd32(d + i * 4);
    memcpy(lv->reserved, d + 0x30, sizeof lv->reserved);
    for (int i = 0; i < MC_THING_COUNT; i++) {
        const uint8_t *p = d + MC_THINGS_OFFSET + i * 18;
        uint16_t *t = &lv->things[i].cls;
        for (int k = 0; k < 9; k++) t[k] = rd16(p + k * 2);
    }
    for (int i = 0; i < 6; i++) lv->footer[i] = rd16(d + MC_FOOTER_OFFSET + i * 2);
    return 1;
}

void mc_level_write(const mc_level *lv, uint8_t *d) {
    memset(d, 0, MC_LEVEL_SIZE);
    const int32_t *g = &lv->gen.unk0;
    for (int i = 0; i < 12; i++) wr32(d + i * 4, g[i]);
    memcpy(d + 0x30, lv->reserved, sizeof lv->reserved);
    for (int i = 0; i < MC_THING_COUNT; i++) {
        uint8_t *p = d + MC_THINGS_OFFSET + i * 18;
        const uint16_t *t = &lv->things[i].cls;
        for (int k = 0; k < 9; k++) wr16(p + k * 2, t[k]);
    }
    for (int i = 0; i < 6; i++) wr16(d + MC_FOOTER_OFFSET + i * 2, lv->footer[i]);
}

int mc_level_load(const char *game_dir, int index, mc_level *out) {
    char path[1024];
    mc_blob tab, dat;
    mc_path_join(path, sizeof path, game_dir, "levels/levels.tab");
    if (!mc_read_file(path, &tab)) return 0;
    mc_path_join(path, sizeof path, game_dir, "levels/levels.dat");
    if (!mc_read_file(path, &dat)) { mc_blob_free(&tab); return 0; }
    size_t n = tab.len / 4;
    int ok = 0;
    if ((size_t)index + 1 < n) {
        uint32_t a = (uint32_t)rd32(tab.data + index * 4);
        uint32_t b = (uint32_t)rd32(tab.data + (index + 1) * 4);
        if (b <= a) b = (uint32_t)dat.len;
        if (a < dat.len && b <= dat.len && b > a) {
            uint8_t buf[MC_LEVEL_SIZE];
            long got = rnc_unpack(dat.data + a, b - a, buf, sizeof buf);
            if (got == MC_LEVEL_SIZE) ok = mc_level_parse(buf, (size_t)got, out);
        }
    }
    mc_blob_free(&tab);
    mc_blob_free(&dat);
    return ok;
}

int mc_level_active_things(const mc_level *lv) {
    int n = 0;
    for (int i = 0; i < MC_THING_COUNT; i++) if (lv->things[i].cls) n++;
    return n;
}

const char *mc_class_name(int cls) {
    switch (cls) {
    case MC_CLASS_SCENERY: return "Scenery";
    case MC_CLASS_PLAYER: return "Player";
    case MC_CLASS_CREATURE: return "Creature";
    case MC_CLASS_WEATHER: return "Weather";
    case MC_CLASS_EFFECT: return "Effect";
    case MC_CLASS_SWITCH: return "Switch";
    case MC_CLASS_SPELL: return "Spell";
    default: return "?";
    }
}

static const char *creatures[] = {"Dragon","Vulture","Bee","Worm","Archer","Crab","Kraken","Troll","Griffon",
    "Skeleton","Emu","Genie","Builder","Townie","Trader","?","Wyvern"};
static const char *spells[] = {"Fireball","Heal","Alliance","Possession","Shield","Beyond sight","Earthquake",
    "Meteor","Volcano","Crater","Teleport","Rubber band","Invisible","Steal mana","Rebound","Lightning","Castle",
    "Skeleton","Thunderbolt","Mana magnet","Fire wall","Reverse speed","Smart bomb","Mini fireball"};
static const char *scenery[] = {"Tree","Standing stone","Dolmen","Bad stone","2D dome","2D dome"};

const char *mc_model_name(int cls, int model) {
    switch (cls) {
    case MC_CLASS_CREATURE: return (model >= 0 && model < 17) ? creatures[model] : "?";
    case MC_CLASS_SPELL: return (model >= 0 && model < 24) ? spells[model] : "?";
    case MC_CLASS_SCENERY: return (model >= 0 && model < 6) ? scenery[model] : "?";
    case MC_CLASS_PLAYER: return (model >= 4 && model <= 11) ? "Flyer" : "?";
    case MC_CLASS_EFFECT:
        switch (model) {
        case 29: return "Path"; case 39: return "Mana ball"; case 45: return "Wizard";
        case 28: return "Wall"; case 31: return "Canyon"; case 50: return "Ridge node";
        case 14: return "Black smoke"; case 13: return "White smoke"; case 34: return "Teleport";
        default: return "Effect";
        }
    default: return "?";
    }
}

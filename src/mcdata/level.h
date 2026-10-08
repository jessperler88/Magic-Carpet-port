/* LEVxxxxx.DAT: 38,812-byte level records (see docs/FORMATS.md). */
#ifndef MC_LEVEL_H
#define MC_LEVEL_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_LEVEL_SIZE     38812
#define MC_THING_COUNT    2095
#define MC_THINGS_OFFSET  0x442
#define MC_FOOTER_OFFSET  0x9790

typedef struct {
    int32_t unk0, seed, off, raise, gnarl, river, sourc, snlin, snflt, bhlin, bhflt, rkste;
} mc_gen_map;

typedef struct {
    uint16_t cls, model, x, y, dis_id, swi_sz, swi_id, parent, child;
} mc_thing_init;

typedef struct {
    mc_gen_map     gen;
    uint8_t        reserved[MC_THINGS_OFFSET - 0x30];
    mc_thing_init  things[MC_THING_COUNT];
    uint16_t       footer[6];
} mc_level;

enum mc_thing_class {
    MC_CLASS_SCENERY = 2, MC_CLASS_PLAYER = 3, MC_CLASS_CREATURE = 5, MC_CLASS_WEATHER = 7,
    MC_CLASS_EFFECT = 10, MC_CLASS_SWITCH = 11, MC_CLASS_SPELL = 12,
};

/* Parse a decompressed 38,812-byte level. Returns 0 on size mismatch. */
int mc_level_parse(const uint8_t *data, size_t len, mc_level *out);
/* Serialise back (inverse of parse). `out` must hold MC_LEVEL_SIZE bytes. */
void mc_level_write(const mc_level *lv, uint8_t *out);
/* Load level `index` from <game_dir>/levels/levels.dat + levels.tab. */
int mc_level_load(const char *game_dir, int index, mc_level *out);
/* Number of non-empty THING_INIT slots. */
int mc_level_active_things(const mc_level *lv);

const char *mc_class_name(int cls);
const char *mc_model_name(int cls, int model);

#ifdef __cplusplus
}
#endif
#endif

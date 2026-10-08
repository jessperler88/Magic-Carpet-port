#include "tmaps.h"
#include "rnc.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int mc_tmap_set_load(const char *game_dir, mc_tmap_set *s) {
    char path[1024];
    mc_blob tab;
    memset(s, 0, sizeof *s);
    mc_path_join(path, sizeof path, game_dir, "data/tmaps.dat");
    if (!mc_read_file(path, &s->dat)) return 0;
    mc_path_join(path, sizeof path, game_dir, "data/tmaps.tab");
    if (!mc_read_file(path, &tab)) { mc_blob_free(&s->dat); return 0; }
    size_t n = tab.len / 10;
    s->entries = (mc_tmap_entry *)malloc(n * sizeof(mc_tmap_entry) + 1);
    s->count = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = tab.data + i * 10;
        mc_tmap_entry e = { rd32(p), rd32(p + 4), rd16(p + 8) };
        if (e.unpacked_size == 0) continue;
        s->entries[s->count++] = e;
    }
    mc_blob_free(&tab);
    return 1;
}

void mc_tmap_set_free(mc_tmap_set *s) {
    mc_blob_free(&s->dat);
    free(s->entries);
    s->entries = NULL; s->count = 0;
}

int mc_tmap_get(const mc_tmap_set *s, size_t index, mc_tmap *t) {
    memset(t, 0, sizeof *t);
    if (index >= s->count) return 0;
    const mc_tmap_entry *e = &s->entries[index];
    if (e->offset >= s->dat.len) return 0;
    const uint8_t *chunk = s->dat.data + e->offset;
    size_t avail = s->dat.len - e->offset;
    uint8_t *raw;
    size_t raw_len;
    if (rnc_is_rnc(chunk, avail)) {
        int err;
        raw = rnc_unpack_alloc(chunk, avail, &raw_len, &err);
        if (!raw) return 0;
    } else {
        raw_len = e->unpacked_size < avail ? e->unpacked_size : avail;
        raw = (uint8_t *)malloc(raw_len);
        memcpy(raw, chunk, raw_len);
    }
    if (raw_len < 6) { free(raw); return 0; }
    t->kind = raw[0]; t->unk = raw[1];
    t->width = rd16(raw + 2); t->height = rd16(raw + 4);
    if ((size_t)t->width * t->height + 6 > raw_len) { free(raw); return 0; }
    t->pixels = raw + 6;
    t->raw = raw; t->raw_len = raw_len;
    return 1;
}

void mc_tmap_free(mc_tmap *t) { free(t->raw); memset(t, 0, sizeof *t); }

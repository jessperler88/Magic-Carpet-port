#include "sprite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mc_sprite_set_load(const char *game_dir, const char *name, mc_sprite_set *s) {
    char path[1024];
    mc_blob tab;
    memset(s, 0, sizeof *s);
    snprintf(path, sizeof path, "%s.dat", name);
    char full[1024];
    mc_path_join(full, sizeof full, game_dir, path);
    if (!mc_read_unpacked(full, &s->dat)) return 0;
    snprintf(path, sizeof path, "%s.tab", name);
    mc_path_join(full, sizeof full, game_dir, path);
    if (!mc_read_unpacked(full, &tab)) { mc_blob_free(&s->dat); return 0; }
    s->count = mc_tab_parse(&tab, &s->entries);
    mc_blob_free(&tab);
    return 1;
}

void mc_sprite_set_free(mc_sprite_set *s) {
    mc_blob_free(&s->dat);
    free(s->entries);
    s->entries = NULL; s->count = 0;
}

size_t mc_sprite_decode(const mc_sprite_set *s, size_t index, uint8_t *dst, size_t dst_cap) {
    if (index >= s->count) return 0;
    const mc_tab_entry *e = &s->entries[index];
    int w = e->width, h = e->height;
    if (!w || !h || (size_t)(w * h) > dst_cap) return 0;
    memset(dst, 0, (size_t)(w * h));
    const uint8_t *d = s->dat.data;
    size_t p = e->offset, n = s->dat.len;
    for (int y = 0; y < h; y++) {
        int x = 0;
        for (;;) {
            if (p >= n) return p - e->offset;
            int8_t b = (int8_t)d[p++];
            if (b == 0) break;
            if (b > 0) {
                for (int i = 0; i < b && p < n; i++, p++)
                    if (x + i < w) dst[y * w + x + i] = d[p];
                x += b;
            } else {
                x += -b;
            }
        }
    }
    return p - e->offset;
}

void mc_sprite_blit(const uint8_t *src, int w, int h, uint8_t *surface, int sw, int sh, int x, int y) {
    for (int j = 0; j < h; j++) {
        int sy = y + j;
        if (sy < 0 || sy >= sh) continue;
        for (int i = 0; i < w; i++) {
            int sx = x + i;
            if (sx < 0 || sx >= sw) continue;
            uint8_t v = src[j * w + i];
            if (v) surface[sy * sw + sx] = v;
        }
    }
}

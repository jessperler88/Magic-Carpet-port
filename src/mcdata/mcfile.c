#include "mcfile.h"
#include "rnc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mc_read_file(const char *path, mc_blob *out) {
    out->data = NULL; out->len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return 0; }
    buf[n] = 0;
    out->data = buf; out->len = (size_t)n;
    return 1;
}

int mc_read_unpacked(const char *path, mc_blob *out) {
    mc_blob raw;
    if (!mc_read_file(path, &raw)) return 0;
    const uint8_t *p = raw.data;
    size_t len = raw.len;
    if (len > 8 && memcmp(p, "BULLFROG", 8) == 0 && rnc_is_rnc(p + 8, len - 8)) { p += 8; len -= 8; }
    if (rnc_is_rnc(p, len)) {
        size_t ulen; int err;
        uint8_t *u = rnc_unpack_alloc(p, len, &ulen, &err);
        free(raw.data);
        if (!u) return 0;
        out->data = u; out->len = ulen;
        return 1;
    }
    *out = raw;
    return 1;
}

void mc_blob_free(mc_blob *b) { free(b->data); b->data = NULL; b->len = 0; }

long mc_load_rnc_into(const char *path, uint8_t *dest, size_t cap) {
    mc_blob raw;
    if (!mc_read_file(path, &raw)) return 0;
    long n;
    if (rnc_is_rnc(raw.data, raw.len)) {
        n = rnc_unpack(raw.data, raw.len, dest, cap);
        if (n == RNC_ERR_OUTBUF) n = -1;
        else if (n < 0) n = -2;
    } else if (raw.len > cap) {
        n = -1;
    } else {
        memcpy(dest, raw.data, raw.len);
        n = (long)raw.len;
    }
    mc_blob_free(&raw);
    return n;
}

void mc_path_join(char *buf, size_t cap, const char *game_dir, const char *rel) {
    snprintf(buf, cap, "%s/%s", game_dir, rel);
    for (char *c = buf; *c; c++) if (*c == '\\') *c = '/';
}

size_t mc_tab_parse(const mc_blob *tab, mc_tab_entry **entries) {
    size_t n = tab->len / 6;
    mc_tab_entry *e = (mc_tab_entry *)malloc(n * sizeof(mc_tab_entry) + 1);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = tab->data + i * 6;
        e[i].offset = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        e[i].width = p[4];
        e[i].height = p[5];
    }
    *entries = e;
    return n;
}

void mc_palette_to_rgb(const uint8_t *pal6, uint8_t *rgb8) {
    for (int i = 0; i < 768; i++) rgb8[i] = (uint8_t)((pal6[i] << 2) | (pal6[i] >> 4));
}

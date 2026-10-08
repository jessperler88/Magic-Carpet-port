#include "sndbank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

size_t mc_sndbank_parse(const uint8_t *tab, size_t tab_len, const uint8_t *dat, size_t dat_len, mc_sndbank *b) {
    memset(b, 0, sizeof *b);
    size_t n = tab_len / MC_SNDBANK_RECORD_SIZE;
    if (n == 0) return 0;
    b->records = (mc_sndbank_record *)calloc(n, sizeof(mc_sndbank_record));
    if (!b->records) return 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = tab + i * MC_SNDBANK_RECORD_SIZE;
        mc_sndbank_record *o = &b->records[i];
        memcpy(o->name, r, 18);
        o->name[18] = 0;
        o->offset = rd32(r + 0x12);
        o->length = rd32(r + 0x1a);
        o->unk1e  = (uint16_t)rd16(r + 0x1e);
    }
    b->record_count = n;
    if (dat_len) {
        b->dat.data = (uint8_t *)malloc(dat_len);
        if (!b->dat.data) { free(b->records); memset(b, 0, sizeof *b); return 0; }
        memcpy(b->dat.data, dat, dat_len);
        b->dat.len = dat_len;
    }
    return n;
}

int mc_sndbank_load_named(const char *game_dir, const char *prefix, int set, int n, int rate, mc_sndbank *b) {
    char rel[64], full[1024];
    mc_blob dat, tab;
    memset(b, 0, sizeof *b);
    /* sound_load_bank_5c990: sprintf("data/snds%d-%d.dat", set, DAT_0009e328) and the .tab */
    snprintf(rel, sizeof rel, "data/%s%d-%d.dat", prefix, set, n);
    mc_path_join(full, sizeof full, game_dir, rel);
    if (!mc_read_unpacked(full, &dat)) return 0;
    snprintf(rel, sizeof rel, "data/%s%d-%d.tab", prefix, set, n);
    mc_path_join(full, sizeof full, game_dir, rel);
    if (!mc_read_unpacked(full, &tab)) { mc_blob_free(&dat); return 0; }
    size_t count = mc_sndbank_parse(tab.data, tab.len, dat.data, dat.len, b);
    mc_blob_free(&tab);
    mc_blob_free(&dat);
    if (!count) return 0;
    b->set = set;
    b->quality = n;
    b->rate = rate;
    return 1;
}

int mc_sndbank_load(const char *game_dir, int set, int quality, mc_sndbank *b) {
    /* sound_digital_init_4da10: quality 1 -> 22050 Hz, 0 and 3 -> 11025 Hz (every shipped bank is
     * 8-bit; the 16-bit device ids are downgraded to their 8-bit variant in the same switch). */
    int rate = quality == 1 ? 22050 : 11025;
    return mc_sndbank_load_named(game_dir, "snds", set, quality, rate, b);
}

void mc_sndbank_free(mc_sndbank *b) {
    mc_blob_free(&b->dat);
    free(b->records);
    memset(b, 0, sizeof *b);
}

const uint8_t *mc_sndbank_sample(const mc_sndbank *b, int index, uint32_t *len) {
    if (len) *len = 0;
    if (!b || index < 1 || (size_t)index >= b->record_count) return NULL;
    const mc_sndbank_record *r = &b->records[index];
    if (r->length < MC_SNDBANK_PADDING || r->offset >= b->dat.len) return NULL;
    uint32_t n = r->length - MC_SNDBANK_PADDING;             /* sound_start_sample_4f8a0: +0x1a - 0x10 */
    if ((size_t)r->offset + n > b->dat.len) n = (uint32_t)(b->dat.len - r->offset);
    if (len) *len = n;
    return b->dat.data + r->offset;
}

const uint8_t *mc_sndbank_record_data(const mc_sndbank *b, int index, uint32_t *len) {
    if (len) *len = 0;
    if (!b || index < 1 || (size_t)index >= b->record_count) return NULL;
    const mc_sndbank_record *r = &b->records[index];
    if (r->offset >= b->dat.len) return NULL;
    uint32_t n = r->length;
    if ((size_t)r->offset + n > b->dat.len) n = (uint32_t)(b->dat.len - r->offset);
    if (len) *len = n;
    return b->dat.data + r->offset;
}

const char *mc_sndbank_name(const mc_sndbank *b, int index) {
    if (!b || index < 0 || (size_t)index >= b->record_count) return "";
    return b->records[index].name;
}

static void put16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); }
static void put32(FILE *f, unsigned v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int mc_sndbank_write_wav(const mc_sndbank *b, int index, int rate, const char *path) {
    uint32_t n;
    const uint8_t *s = mc_sndbank_sample(b, index, &n);
    if (!s) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, (unsigned)rate); put32(f, (unsigned)rate); put16(f, 1); put16(f, 8);
    fwrite("data", 1, 4, f); put32(f, n);
    fwrite(s, 1, n, f);
    fclose(f);
    return 1;
}

/* HMP song parser (round 5, task A). Layout and rules: see hmp.h. */
#include "hmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* snd_midi_read_varlen_5eedf: 7 bits per byte, least significant group first, the byte with bit 7
 * set is the last one. */
size_t mc_hmp_read_varlen(const uint8_t *p, size_t avail, uint32_t *value) {
    uint32_t v = 0;
    unsigned shift = 0;
    size_t n = 0;
    while (n < avail) {
        uint8_t b = p[n++];
        if (shift < 32) v |= (uint32_t)(b & 0x7f) << shift;
        shift += 7;
        if (b & 0x80) { *value = v; return n; }
    }
    *value = v;
    return 0;
}

/* The two length tables of the timer callback 0x66898: by high nibble at 0x9fa0e (status < 0xf0)
 * and by low nibble at 0x9fa1e (0xf0..0xff); FF 2F -> 3, FF 51 -> 5. */
static const uint8_t k_len_hi[16] = { 0,0,0,0,0,0,0,0, 3,3,2,3,2,2,3,0 };
static const uint8_t k_len_fx[16] = { 0,1,2,1,0,0,0,0, 2,2,2,2,2,2,2,2 };

int mc_hmp_event_length(uint8_t status, uint8_t d1) {
    if (status < 0xf0) return k_len_hi[status >> 4];
    if (status == 0xff) {
        if (d1 == 0x2f) return 3;
        if (d1 == 0x51) return 5;
    }
    return k_len_fx[status & 0x0f];
}

static void parse_track(const uint8_t *data, size_t len, mc_hmp_track *t, mc_hmp_song *s) {
    /* worst case one event per 2 bytes */
    size_t cap = len / 2 + 2;
    t->events = (mc_hmp_event *)calloc(cap, sizeof(mc_hmp_event));
    if (!t->events) return;
    size_t p = 0;
    uint32_t sum = 0;
    while (p < len && t->event_count < cap) {
        uint32_t delta;
        size_t n = mc_hmp_read_varlen(data + p, len - p, &delta);
        if (!n) { s->bad_events++; break; }
        p += n;
        if (p >= len) { s->bad_events++; break; }
        uint8_t st = data[p];
        uint8_t d1 = p + 1 < len ? data[p + 1] : 0;
        uint8_t d2 = p + 2 < len ? data[p + 2] : 0;
        int el = st < 0x80 ? 0 : mc_hmp_event_length(st, d1);
        if (el <= 0 || p + (size_t)el > len) { s->bad_events++; break; }   /* the sequencer would hang or run off */
        mc_hmp_event *e = &t->events[t->event_count++];
        /* the first event fires at tick max(1, delta), every later one `delta` ticks after the
         * previous one: sum of the deltas, + 1 when the first delta was 0 */
        if (t->event_count == 1 && delta == 0) sum = 1;
        else sum += delta;
        e->delta = delta;
        e->tick = sum;
        e->status = st;
        e->d1 = el > 1 ? d1 : 0;
        e->d2 = el > 2 ? d2 : 0;
        e->len = (uint8_t)el;
        if ((st & 0xf0) == 0x90 && el == 3 && d2) s->note_ons++;
        t->end_tick = e->tick;
        p += (size_t)el;
        if (st == 0xff && d1 == 0x2f) { t->has_end = 1; break; }
    }
}

int mc_hmp_parse(const uint8_t *data, size_t len, mc_hmp_song *s) {
    memset(s, 0, sizeof *s);
    if (!data || len < MC_HMP_HEADER_SIZE || memcmp(data, "HMIMIDIP", 8) != 0) return 0;
    s->track_count = rd32(data + 0x30);
    s->division = rd32(data + 0x34);
    s->rate = rd32(data + 0x38);
    s->seconds = rd32(data + 0x3c);
    for (int i = 0; i < 16; i++) s->priority[i] = rd32(data + 0x40 + 4 * i);
    if (s->track_count > MC_HMP_MAX_TRACKS || s->rate == 0) { memset(s, 0, sizeof *s); return 0; }
    size_t pos = MC_HMP_HEADER_SIZE;
    for (uint32_t i = 0; i < s->track_count; i++) {
        mc_hmp_track *t = &s->tracks[i];
        if (pos + 12 > len) { mc_hmp_free(s); return 0; }
        uint32_t clen = rd32(data + pos + 4);
        if (clen < 12 || pos + clen > len) { mc_hmp_free(s); return 0; }
        t->chunk = rd32(data + pos);
        t->channel = rd32(data + pos + 8);
        for (int k = 0; k < 5; k++) t->devices[k] = rd32(data + 0x80 + i * 0x14 + 4 * k);
        t->data_offset = (uint32_t)(pos + 12);
        t->data_len = clen - 12;
        parse_track(data + pos + 12, clen - 12, t, s);
        if (!t->events) { mc_hmp_free(s); return 0; }
        if (t->end_tick > s->duration_ticks) s->duration_ticks = t->end_tick;
        pos += clen;
    }
    s->bytes_used = (uint32_t)pos;
    return 1;
}

void mc_hmp_free(mc_hmp_song *s) {
    for (int i = 0; i < MC_HMP_MAX_TRACKS; i++) free(s->tracks[i].events);
    memset(s, 0, sizeof *s);
}

/* snd_midi_init_song_5e53a, the loop over the track map: entry 0xff (tracks 0..15 in the map
 * music_play_track_5c0a0 passes at 0x9e65c) -> search the track's device list for the driver; an
 * empty list maps to driver 0; no match disables the track. Entries 16..31 of that map are 0. */
int mc_hmp_track_plays(const mc_hmp_song *s, int t, uint32_t device) {
    if (t < 0 || (uint32_t)t >= s->track_count) return 0;
    if (t >= 16) return 1;
    const uint32_t *d = s->tracks[t].devices;
    if (d[0] == 0) return 1;
    for (int k = 0; k < 5 && d[k] != 0; k++) {
        if (d[k] == MC_HMP_DEVICE_ANY_GM) {
            if (device == 0xa000u || device == MC_HMP_DEVICE_MPU401 || device == MC_HMP_DEVICE_AWE32) return 1;
        } else if (d[k] == device) {
            return 1;
        }
    }
    return 0;
}

/* ---- standard MIDI export ------------------------------------------------------------------------ */
static void put_be32(FILE *f, uint32_t v) { fputc((int)(v >> 24), f); fputc((int)(v >> 16) & 0xff, f); fputc((int)(v >> 8) & 0xff, f); fputc((int)v & 0xff, f); }
static void put_be16(FILE *f, uint32_t v) { fputc((int)(v >> 8) & 0xff, f); fputc((int)v & 0xff, f); }
static size_t vlq(uint8_t *out, uint32_t v) {
    uint8_t tmp[5];
    size_t n = 0;
    do { tmp[n++] = (uint8_t)(v & 0x7f); v >>= 7; } while (v && n < 5);
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(tmp[n - 1 - i] | (i + 1 < n ? 0x80 : 0));
    return n;
}

int mc_hmp_write_midi(const mc_hmp_song *s, uint32_t device, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t ntr = 0;
    for (uint32_t i = 0; i < s->track_count; i++) if (mc_hmp_track_plays(s, (int)i, device)) ntr++;
    fwrite("MThd", 1, 4, f); put_be32(f, 6); put_be16(f, 1); put_be16(f, ntr + 1); put_be16(f, s->rate);
    /* tempo track: 1 000 000 us per quarter, division = rate -> one tick = 1 / rate s */
    static const uint8_t tempo[] = { 0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40, 0x00, 0xff, 0x2f, 0x00 };
    fwrite("MTrk", 1, 4, f); put_be32(f, sizeof tempo); fwrite(tempo, 1, sizeof tempo, f);
    for (uint32_t i = 0; i < s->track_count; i++) {
        if (!mc_hmp_track_plays(s, (int)i, device)) continue;
        const mc_hmp_track *t = &s->tracks[i];
        uint8_t *buf = (uint8_t *)malloc((size_t)t->event_count * 9 + 8);
        if (!buf) { fclose(f); return 0; }
        size_t n = 0;
        uint32_t last = 0;
        for (uint32_t k = 0; k < t->event_count; k++) {
            const mc_hmp_event *e = &t->events[k];
            if (e->status == 0xff && e->d1 != 0x2f) continue;     /* only the end of track is kept */
            if (e->status >= 0xf0 && e->status != 0xff) continue;
            n += vlq(buf + n, e->tick - last);
            last = e->tick;
            if (e->status == 0xff) { buf[n++] = 0xff; buf[n++] = 0x2f; buf[n++] = 0x00; continue; }
            buf[n++] = e->status;
            /* Ax is 2 bytes in the HMI table but 3 in MIDI: pad with 0 */
            int ml = ((e->status & 0xf0) == 0xc0 || (e->status & 0xf0) == 0xd0) ? 2 : 3;
            buf[n++] = e->d1;
            if (ml == 3) buf[n++] = e->d2;
        }
        if (!t->has_end) { n += vlq(buf + n, 0); buf[n++] = 0xff; buf[n++] = 0x2f; buf[n++] = 0x00; }
        fwrite("MTrk", 1, 4, f); put_be32(f, (uint32_t)n); fwrite(buf, 1, n, f);
        free(buf);
    }
    fclose(f);
    return 1;
}

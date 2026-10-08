/* HMP song parser (round 5, task A): the HMI SOS MIDI files ("HMIMIDIP") inside
 * data/music<set>-<device>.dat (one per .tab record, see sndbank.h). The layout and the event
 * rules come from the HMI sequencer linked into carpet.exe: snd_midi_init_song_5e53a (header,
 * track device map), snd_midi_song_setup_5ec41 (track pointers, first delta),
 * snd_midi_read_varlen_5eedf (delta encoding), the timer callback at 0x66898 (event lengths
 * from the tables at 0x9fa0e / 0x9fa1e, end of track, song end) and snd_midi_start_song_5eab0
 * (timer rate = header +0x38).
 *
 *   +0x000 char[8]   "HMIMIDIP" (the rest of the 0x20 bytes is zero)
 *   +0x020 u32       file length (0 in the shipped files)
 *   +0x030 u32       track (chunk) count, <= 32
 *   +0x034 u32       120 in every file (MIDI-style division; not read by the sequencer)
 *   +0x038 u32       sequencer rate: HMI timer events per second = song ticks per second (120)
 *   +0x03c u32       song length in whole seconds (informational)
 *   +0x040 u32[16]   per MIDI channel priority (channel stealing, used only with channel mapping on)
 *   +0x080 u32[32][5] per track: up to 5 HMI device ids the track plays on (0 terminates):
 *                    0xa000 = any General MIDI device (0xa000 / 0xa001 MPU-401 / 0xa008 AWE32),
 *                    other ids must equal the driver's id; an empty list plays everywhere
 *   +0x300 u32,u16   end-of-song far callback (written by sosMIDIInitSong from the init struct)
 *   +0x308           the tracks, back to back: u32 chunk number, u32 chunk length (incl. this
 *                    12-byte header), u32 MIDI channel of the track (used for the reset at song
 *                    end), then the events.
 *
 * Event = delta + MIDI event. The delta is little-endian groups of 7 bits, the LAST byte has
 * bit 7 set (the reverse of standard MIDI). There is no running status: every event has its status
 * byte and its length comes from the status: 8x/9x/Bx/Ex 3 bytes, Ax/Cx/Dx 2 (sic: Ax is 2 in the
 * HMI table), F0 0 (sic), F1 1, F2 2, F3 1, F8..FF 2 except FF 2F (end of track) 3 and FF 51
 * (tempo, ignored by the sequencer) 5. The shipped songs use only 9x (note off = velocity 0), Bx
 * (7 volume, 10 pan, 116 / 117), Cx, Ex and FF 2F.
 *
 * Timing (0x66898): every timer event increments each track's counter; an event fires when its
 * delta <= the counter, then the counter is cleared and events with delta 0 follow in the same
 * tick. The first tick is tick 1, so the first event fires at tick max(1, delta) and every later
 * one `delta` ticks after the previous one (absolute tick = sum of the deltas, + 1 when the track's
 * first delta is 0). The song ends when every playing track has reached FF 2F. */
#ifndef MC_HMP_H
#define MC_HMP_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_HMP_MAX_TRACKS   32
#define MC_HMP_HEADER_SIZE  0x308
#define MC_HMP_DEVICE_ANY_GM 0xa000u  /* _MIDI_ANY? in the track device list */
#define MC_HMP_DEVICE_MPU401 0xa001u
#define MC_HMP_DEVICE_OPL2   0xa002u
#define MC_HMP_DEVICE_MT32   0xa004u
#define MC_HMP_DEVICE_AWE32  0xa008u

typedef struct {
    uint32_t delta;         /* ticks after the previous event of the track (as stored) */
    uint32_t tick;          /* absolute tick (1 = first timer event) at which the sequencer sends it */
    uint8_t  status, d1, d2;
    uint8_t  len;           /* event length in bytes as the sequencer steps over it (1..5) */
} mc_hmp_event;

typedef struct {
    uint32_t chunk;         /* +0 chunk number */
    uint32_t channel;       /* +8 MIDI channel of the track */
    uint32_t devices[5];    /* header +0x80 + track * 0x14 */
    uint32_t data_offset;   /* file offset of the first event */
    uint32_t data_len;      /* event bytes (chunk length - 12) */
    mc_hmp_event *events;   /* every event up to and including FF 2F */
    uint32_t event_count;
    uint32_t end_tick;      /* tick of the last event */
    int      has_end;       /* the track ends with FF 2F */
} mc_hmp_track;

typedef struct {
    uint32_t track_count;   /* +0x30 */
    uint32_t division;      /* +0x34 */
    uint32_t rate;          /* +0x38: ticks per second */
    uint32_t seconds;       /* +0x3c */
    uint32_t priority[16];  /* +0x40 */
    mc_hmp_track tracks[MC_HMP_MAX_TRACKS];
    uint32_t duration_ticks;     /* largest end_tick */
    uint32_t bytes_used;         /* offset after the last chunk */
    uint32_t bad_events;         /* events the sequencer could not step over (length 0, data byte as status) */
    uint32_t note_ons;           /* 9x with velocity > 0, all tracks */
} mc_hmp_song;

/* Parse an HMP image. Returns 1 on success (song filled, free with mc_hmp_free), 0 when the magic
 * or the chunk layout is wrong. */
int  mc_hmp_parse(const uint8_t *data, size_t len, mc_hmp_song *out);
void mc_hmp_free(mc_hmp_song *song);

/* snd_midi_read_varlen_5eedf: decode an HMI delta at p (at most `avail` bytes). Returns the
 * number of bytes used, 0 when the terminating byte is missing. */
size_t mc_hmp_read_varlen(const uint8_t *p, size_t avail, uint32_t *value);
/* The sequencer's length of the event starting with `status` (`d1` = the byte after it, for FF). */
int  mc_hmp_event_length(uint8_t status, uint8_t d1);
/* snd_midi_init_song_5e53a's track auto-map (track map entry 0xff, as music_play_track_5c0a0 passes
 * for tracks 0..15): does track `t` play on the driver with HMI device id `device`? Tracks 16..31
 * get map entry 0 (driver 0) and always play. */
int  mc_hmp_track_plays(const mc_hmp_song *song, int t, uint32_t device);

/* Write the song as a standard MIDI file (format 1, division = rate, tempo 1 000 000 us per quarter,
 * so a tick lasts 1 / rate s), only the tracks that play on `device`. Returns 1 on success. For
 * listening to the music outside the game. */
int  mc_hmp_write_midi(const mc_hmp_song *song, uint32_t device, const char *path);

#ifdef __cplusplus
}
#endif
#endif

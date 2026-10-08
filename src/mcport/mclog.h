// mcport's log (Phase 4, round 10 task D; port-only). mcport code only: engine code keeps printf / stderr.
//
//   mclog(MCLOG_INFO, "state saved to slot %d", slot);
//
// Every line goes to
//   - the log file <save dir>/mcport.log (mclog_open; the previous run's file is kept as mcport.1.log),
//     as "[   12.345] I message" (seconds since mclog_open, level letter E / W / I / D);
//   - the console: stdout for info / debug, stderr for warnings / errors, the message alone as mcport
//     printed it before (no prefix), so tools that read mcport's stdout see the same lines;
//   - a ring buffer of the last MCLOG_HISTORY lines with sequence numbers (mclog_since: the in-game
//     console's scrollback reads it) and any registered sinks (callbacks).
// Lines above the level threshold (mclog_set_level; default info, MC_LOG_LEVEL=error|warn|info|debug) are
// dropped everywhere. Thread-safe (the console's stdin reader thread may log). A message may hold several
// lines ('\n'); each becomes its own log line. No SDL (mclog_test links it alone).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum MclogLevel : int { MCLOG_ERROR = 0, MCLOG_WARN = 1, MCLOG_INFO = 2, MCLOG_DEBUG = 3 };

constexpr int MCLOG_HISTORY = 512;

#if defined(__GNUC__) || defined(__clang__)
#define MCLOG_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define MCLOG_PRINTF(a, b)
#endif

void mclog(int level, const char *fmt, ...) MCLOG_PRINTF(2, 3);
void mclog_str(int level, const char *msg);

// Opens <dir>/mcport.log for this run, after renaming an existing mcport.log to mcport.1.log (replacing the
// older one). `dir` "" / null = no file. Returns false when the file cannot be created (logging goes on
// without it). Writes a header line with the date and `banner`.
bool mclog_open(const char *dir, const char *banner = nullptr);
void mclog_close();
const std::string &mclog_path();                // the open file ("" = none)

void mclog_set_level(int level);                // lines with level > this are dropped
int  mclog_level();
bool mclog_parse_level(const char *text, int *level);   // "error" "warn" "info" "debug" or 0..3
const char *mclog_level_name(int level);
void mclog_set_console(bool on);                // mirror to stdout / stderr (default on)

struct MclogLine {
    uint64_t seq;                               // 1, 2, ... in the order logged
    int level;
    double time_s;                              // seconds since mclog_open (or the first line)
    std::string text;
};
// Lines with seq > `after` still in the ring buffer (oldest first); returns the last seq (the next `after`).
uint64_t mclog_since(uint64_t after, std::vector<MclogLine> *out);

// Sinks: called after the line was written, outside the log's lock (a sink may log; that line reaches it too,
// keep it from recursing). Returns an id for mclog_remove_sink.
using MclogSink = void (*)(const MclogLine &line, void *user);
int  mclog_add_sink(MclogSink sink, void *user);
void mclog_remove_sink(int id);

// Tests: forget everything (closes the file, clears the ring buffer and the sinks, level info, console on).
void mclog_reset();

// mcport's log (mclog.h; port round 10 task D).
#include "mclog.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <utility>

namespace {

struct SinkEntry { int id; MclogSink fn; void *user; };

struct LogState {
    std::mutex mu;
    FILE *file = nullptr;
    std::string path;
    int level = MCLOG_INFO;
    bool console = true;
    uint64_t seq = 0;
    std::deque<MclogLine> ring;
    std::vector<SinkEntry> sinks;
    int next_sink = 1;
    bool have_t0 = false;
    std::chrono::steady_clock::time_point t0;
};

LogState &st() {
    static LogState s;
    return s;
}

const char k_letter[] = {'E', 'W', 'I', 'D'};

int clamp_level(int l) { return l < MCLOG_ERROR ? MCLOG_ERROR : l > MCLOG_DEBUG ? MCLOG_DEBUG : l; }

FILE *open_write(const std::string &path) {
    FILE *f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, path.c_str(), "w") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "w");
#endif
    return f;
}

// One line (no '\n' inside), the lock held by the caller; returns it for the sinks.
MclogLine emit_locked(LogState &s, int level, const char *text, size_t len) {
    if (!s.have_t0) { s.t0 = std::chrono::steady_clock::now(); s.have_t0 = true; }
    MclogLine l;
    l.seq = ++s.seq;
    l.level = level;
    l.time_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.t0).count();
    l.text.assign(text, len);
    if (s.file) {
        std::fprintf(s.file, "[%9.3f] %c %s\n", l.time_s, k_letter[level], l.text.c_str());
        std::fflush(s.file);
    }
    if (s.console) {
        FILE *out = level <= MCLOG_WARN ? stderr : stdout;
        std::fprintf(out, "%s\n", l.text.c_str());
        std::fflush(out);
    }
    s.ring.push_back(l);
    while (s.ring.size() > (size_t)MCLOG_HISTORY) s.ring.pop_front();
    return l;
}

}  // namespace

void mclog_str(int level, const char *msg) {
    level = clamp_level(level);
    LogState &s = st();
    std::vector<MclogLine> lines;
    std::vector<SinkEntry> sinks;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        if (level > s.level) return;
        std::string m = msg ? msg : "";
        if (!m.empty() && m.back() == '\n') m.pop_back();          // "text\n" is one line
        for (size_t start = 0;;) {
            const size_t nl = m.find('\n', start);
            const size_t end = nl == std::string::npos ? m.size() : nl;
            size_t n = end - start;
            if (n && m[start + n - 1] == '\r') n--;
            lines.push_back(emit_locked(s, level, m.data() + start, n));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        sinks = s.sinks;
    }
    for (const MclogLine &l : lines)
        for (const SinkEntry &e : sinks) e.fn(l, e.user);
}

void mclog(int level, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof buf) {                       // long message: format again at its size
        std::string big((size_t)n + 1, '\0');
        va_start(ap, fmt);
        std::vsnprintf(&big[0], big.size(), fmt, ap);
        va_end(ap);
        mclog_str(level, big.c_str());
        return;
    }
    mclog_str(level, n < 0 ? fmt : buf);
}

bool mclog_open(const char *dir, const char *banner) {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
    s.path.clear();
    if (!dir || !*dir) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path d(dir);
    fs::create_directories(d, ec);
    const fs::path cur = d / "mcport.log", old = d / "mcport.1.log";
    if (fs::exists(cur, ec)) {
        fs::remove(old, ec);
        fs::rename(cur, old, ec);
    }
    s.file = open_write(cur.string());
    if (!s.file) return false;
    s.path = cur.string();
    s.t0 = std::chrono::steady_clock::now();
    s.have_t0 = true;
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char date[64];
    std::strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
    std::fprintf(s.file, "# mcport log %s%s%s\n", date, banner ? " - " : "", banner ? banner : "");
    std::fflush(s.file);
    return true;
}

void mclog_close() {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
    s.path.clear();
}

const std::string &mclog_path() { return st().path; }

void mclog_set_level(int level) {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    s.level = clamp_level(level);
}

int mclog_level() {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.level;
}

bool mclog_parse_level(const char *text, int *level) {
    if (!text) return false;
    static const char *const names[] = {"error", "warn", "info", "debug"};
    for (int i = 0; i < 4; i++) {
        bool eq = true;
        size_t k = 0;
        for (; text[k] && names[i][k]; k++)
            if ((char)(text[k] | 0x20) != names[i][k]) { eq = false; break; }
        if (eq && !text[k] && !names[i][k]) { *level = i; return true; }
    }
    if (std::strcmp(text, "warning") == 0) { *level = MCLOG_WARN; return true; }
    if (text[0] >= '0' && text[0] <= '3' && !text[1]) { *level = text[0] - '0'; return true; }
    return false;
}

const char *mclog_level_name(int level) {
    static const char *const names[] = {"error", "warn", "info", "debug"};
    return names[clamp_level(level)];
}

void mclog_set_console(bool on) {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    s.console = on;
}

uint64_t mclog_since(uint64_t after, std::vector<MclogLine> *out) {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    for (const MclogLine &l : s.ring)
        if (l.seq > after && out) out->push_back(l);
    return s.seq;
}

int mclog_add_sink(MclogSink sink, void *user) {
    if (!sink) return 0;
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    const int id = s.next_sink++;
    s.sinks.push_back({id, sink, user});
    return id;
}

void mclog_remove_sink(int id) {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    for (size_t i = 0; i < s.sinks.size(); i++)
        if (s.sinks[i].id == id) { s.sinks.erase(s.sinks.begin() + (std::ptrdiff_t)i); return; }
}

void mclog_reset() {
    LogState &s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
    s.path.clear();
    s.level = MCLOG_INFO;
    s.console = true;
    s.seq = 0;
    s.ring.clear();
    s.sinks.clear();
    s.have_t0 = false;
}

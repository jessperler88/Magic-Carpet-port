// mclog_test (port round 10, task D; docs/analysis/port_timectl.md): mcport's log (mcport/mclog.h) - level
// threshold and parsing, the file with its header and rotation (mcport.log -> mcport.1.log), multi-line
// messages, long messages, the ring buffer (mclog_since), sinks (add / remove, a sink that logs), threads.
#include "mclog.h"
#include "crash_handler.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string env_or_empty(const char *name) {
#ifdef _WIN32
    char *v = nullptr;
    size_t n = 0;
    std::string r;
    if (_dupenv_s(&v, &n, name) == 0 && v) { r = v; std::free(v); }
    return r;
#else
    const char *v = std::getenv(name);
    return v ? v : "";
#endif
}

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static std::string read_all(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string r = ss.str();                                 // the log is a text file: CRLF on Windows
    std::string out;
    for (char c : r) if (c != '\r') out += c;
    return out;
}
static int count_of(const std::string &s, const std::string &what) {
    int n = 0;
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) n++;
    return n;
}

struct SinkLog { std::vector<MclogLine> lines; };
static void sink_fn(const MclogLine &l, void *user) { static_cast<SinkLog *>(user)->lines.push_back(l); }
static int s_echo_depth = 0;
static void echo_sink(const MclogLine &l, void *) {     // a sink that logs itself (must not deadlock)
    if (s_echo_depth > 0 || l.text.rfind("echo:", 0) == 0) return;
    s_echo_depth++;
    mclog(MCLOG_DEBUG, "echo: %s", l.text.c_str());
    s_echo_depth--;
}

int main() {
    mc_install_crash_handler();
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "mc_mclog_test";
    if (const std::string e = env_or_empty("MC_MCLOG_DIR"); !e.empty()) dir = e;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // level names
    int lv = -1;
    CHECK(mclog_parse_level("error", &lv) && lv == MCLOG_ERROR);
    CHECK(mclog_parse_level("WARN", &lv) && lv == MCLOG_WARN);
    CHECK(mclog_parse_level("warning", &lv) && lv == MCLOG_WARN);
    CHECK(mclog_parse_level("Info", &lv) && lv == MCLOG_INFO);
    CHECK(mclog_parse_level("debug", &lv) && lv == MCLOG_DEBUG);
    CHECK(mclog_parse_level("3", &lv) && lv == 3);
    CHECK(!mclog_parse_level("verbose", &lv) && !mclog_parse_level("", &lv) && !mclog_parse_level("4", &lv));
    CHECK(std::string(mclog_level_name(MCLOG_WARN)) == "warn");

    // run 1: the file, the threshold, the ring buffer
    mclog_reset();
    mclog_set_console(false);
    CHECK(mclog_open(dir.string().c_str(), "run one"));
    CHECK(mclog_path() == (dir / "mcport.log").string());
    CHECK(mclog_level() == MCLOG_INFO);
    mclog(MCLOG_INFO, "hello %d", 1);
    mclog(MCLOG_DEBUG, "hidden debug line");                 // above the default threshold
    mclog(MCLOG_WARN, "a warning");
    mclog(MCLOG_ERROR, "an error %s", "here");
    mclog(MCLOG_INFO, "two\nlines\n");                        // two log lines, the final newline dropped
    mclog_set_level(MCLOG_DEBUG);
    mclog(MCLOG_DEBUG, "visible debug line");
    mclog_set_level(MCLOG_WARN);
    mclog(MCLOG_INFO, "hidden info line");
    mclog_set_level(MCLOG_INFO);
    const std::string big(3000, 'x');
    mclog(MCLOG_INFO, "big %s end", big.c_str());             // longer than the 1024-byte format buffer
    std::vector<MclogLine> got;
    const uint64_t last = mclog_since(0, &got);
    CHECK(got.size() == 7 && last == 7);
    if (got.size() == 7) {
        CHECK(got[0].text == "hello 1" && got[0].level == MCLOG_INFO && got[0].seq == 1);
        CHECK(got[1].text == "a warning" && got[1].level == MCLOG_WARN);
        CHECK(got[3].text == "two" && got[4].text == "lines");
        CHECK(got[5].text == "visible debug line" && got[5].level == MCLOG_DEBUG);
        CHECK(got[6].text == "big " + big + " end");
        CHECK(got[6].time_s >= got[0].time_s);
    }
    got.clear();
    CHECK(mclog_since(5, &got) == 7 && got.size() == 2);
    mclog_close();
    const std::string f1 = read_all(dir / "mcport.log");
    std::printf("run 1 file:\n%s", f1.substr(0, 400).c_str());
    CHECK(f1.rfind("# mcport log ", 0) == 0 && f1.find(" - run one\n") != std::string::npos);
    CHECK(f1.find("] I hello 1\n") != std::string::npos);
    CHECK(f1.find("] W a warning\n") != std::string::npos);
    CHECK(f1.find("] E an error here\n") != std::string::npos);
    CHECK(f1.find("] I two\n") != std::string::npos && f1.find("] I lines\n") != std::string::npos);
    CHECK(f1.find("] D visible debug line\n") != std::string::npos);
    CHECK(f1.find("hidden") == std::string::npos);
    CHECK(count_of(f1, "\n") == 8);                           // header + 7 lines

    // run 2: rotation; sinks; a sink that logs; the ring buffer limit
    CHECK(mclog_open(dir.string().c_str(), "run two"));
    CHECK(read_all(dir / "mcport.1.log") == f1);
    mclog(MCLOG_INFO, "second run");
    SinkLog s1, s2;
    const int id1 = mclog_add_sink(sink_fn, &s1);
    const int id2 = mclog_add_sink(sink_fn, &s2);
    const int id3 = mclog_add_sink(echo_sink, nullptr);
    CHECK(id1 && id2 && id3 && id1 != id2);
    mclog_set_level(MCLOG_DEBUG);
    mclog(MCLOG_INFO, "to the sinks");
    mclog_remove_sink(id2);
    mclog(MCLOG_WARN, "only sink one");
    mclog_remove_sink(id3);
    // s1: "to the sinks", "echo: to the sinks", "only sink one", "echo: only sink one"; s2: the first two
    CHECK(s1.lines.size() == 4 && s2.lines.size() == 2);
    if (s1.lines.size() == 4) {
        CHECK(s1.lines[0].text == "to the sinks" && s1.lines[1].text == "echo: to the sinks" && s1.lines[1].level == MCLOG_DEBUG);
        CHECK(s1.lines[2].text == "only sink one" && s1.lines[2].level == MCLOG_WARN);
    }
    mclog_remove_sink(id1);                                  // (sink_fn is not thread-safe)
    for (int i = 0; i < MCLOG_HISTORY + 50; i++) mclog(MCLOG_DEBUG, "fill %d", i);
    got.clear();
    const uint64_t seq = mclog_since(0, &got);
    CHECK(got.size() == (size_t)MCLOG_HISTORY && got.back().seq == seq && got.front().seq == seq - MCLOG_HISTORY + 1);
    // threads: lines are whole and counted
    {
        std::vector<std::thread> th;
        for (int t = 0; t < 4; t++)
            th.emplace_back([t] { for (int i = 0; i < 200; i++) mclog(MCLOG_INFO, "thread %d line %d", t, i); });
        for (auto &t : th) t.join();
        got.clear();
        CHECK(mclog_since(seq, &got) == seq + 800 && got.size() == (size_t)MCLOG_HISTORY);
    }
    mclog_close();
    const std::string f2 = read_all(dir / "mcport.log");
    CHECK(f2.find("] I second run\n") != std::string::npos && f2.find("] W only sink one\n") != std::string::npos);
    CHECK(count_of(f2, "] I thread ") == 800);
    // run 3: mcport.1.log is replaced by run 2's file
    CHECK(mclog_open(dir.string().c_str()));
    mclog_close();
    CHECK(read_all(dir / "mcport.1.log") == f2);
    // no directory: no file, logging goes on
    CHECK(!mclog_open(""));
    mclog(MCLOG_INFO, "no file");
    CHECK(mclog_path().empty());
    mclog_set_console(true);
    mclog(MCLOG_INFO, "mclog_test: console line (stdout)");
    mclog_reset();
    std::printf("%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

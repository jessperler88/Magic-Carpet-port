// TCP NetTransport (round 6, task B). See net_tcp.h for the design; the NetBIOS semantics it keeps are
// listed in docs/analysis/port_net.md.
//
// Wire format: every TCP stream carries frames {u32 length (little endian, of type + payload), u8 type,
// payload}. Name-server control streams: REG / UNREG / LOOKUP and their replies. Session streams: one
// CALL_REQ / CALL_ACK handshake, then DATA frames (one NetBIOS message each); closing the stream is the
// hang-up.
#define _CRT_SECURE_NO_WARNINGS
#include "net_tcp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static int sock_close(sock_t s) { return closesocket(s); }
static int sock_err() { return WSAGetLastError(); }
static bool err_would_block(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
static void sock_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
static const sock_t BAD_SOCK = -1;
static int sock_close(sock_t s) { return close(s); }
static int sock_err() { return errno; }
static bool err_would_block(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EALREADY; }
static void sock_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace {

enum : uint8_t {
    F_REG = 1, F_UNREG = 2, F_LOOKUP = 3, F_REG_REPLY = 0x81, F_LOOKUP_REPLY = 0x83,
    F_CALL_REQ = 10, F_CALL_ACK = 0x8a, F_DATA = 11,
};
enum ConnKind { CK_NS_PEER, CK_NS_CLIENT, CK_INCOMING, CK_CALLING, CK_SESSION };
enum PendKind { P_ADD_NAME, P_LISTEN, P_CALL_LOOKUP, P_CALL_CONNECT, P_RECEIVE };
constexpr uint8_t NRC_SYSTEM = 0x40;     // "unusual network condition": no name server
constexpr uint8_t NRC_BADLSN = 0x08;     // invalid local session number

std::string key15(const char *name) {    // the 15 significant characters of a NetBIOS name
    char b[16];
    std::memset(b, ' ', 15);
    b[15] = 0;
    for (int i = 0; i < 15 && name[i]; i++) b[i] = name[i];
    return std::string(b, 15);
}
void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
void put32(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

struct Conn {
    int id = 0;
    sock_t s = BAD_SOCK;
    ConnKind kind = CK_INCOMING;
    std::vector<uint8_t> in, out;
    bool dead = false;
    bool connecting = false;
    uint32_t peer_ip = 0;      // network order
    uint64_t op = 0;           // CK_CALLING: the CALL operation
    uint8_t lsn = 0;           // CK_SESSION
};

struct Pend {
    NetNcb *ncb;
    PendKind kind;
    uint64_t op;
};

struct Session {
    int conn = 0;
    std::string local, remote;
    std::deque<std::vector<uint8_t>> q;
    bool closed = false;
};

struct NsEntry {
    int owner;                 // conn id of the registering instance, 0 = this one
    uint32_t ip;               // network order, 0 = this machine
    uint16_t port;
};

struct NsReq {
    uint8_t type;
    uint64_t op;
    std::string name;
};

} // namespace

struct TcpTransport::Impl {
    std::string host;
    uint16_t port = 30600;
    bool started = false, wsa = false, ns_local = false;
    std::string err;
    sock_t ns_listen = BAD_SOCK, sess_listen = BAD_SOCK;
    uint16_t sess_port = 0;
    uint32_t ns_ip = 0;                  // the name server's address (network order) as this instance reaches it
    int ns_conn = 0;                     // conn id of the control connection (client side)
    std::map<int, Conn> conns;
    int next_conn = 1;
    std::vector<Pend> pend;
    uint64_t next_op = 1;
    std::map<uint8_t, Session> sessions;
    uint8_t next_lsn = 1;
    std::vector<std::string> names;      // the local name table
    uint8_t name_num = 1;
    std::map<std::string, NsEntry> ns_table;   // name server only
    std::deque<NsReq> ns_reqs;           // requests sent on the control connection, answered in order

    // ---- plumbing ----
    Conn &add_conn(sock_t s, ConnKind k) {
        const int id = next_conn++;
        Conn &c = conns[id];
        c.id = id;
        c.s = s;
        c.kind = k;
        sock_nonblock(s);
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof one);
        return c;
    }
    Conn *conn(int id) { auto it = conns.find(id); return it == conns.end() ? nullptr : &it->second; }
    void send_frame(Conn &c, uint8_t type, const std::vector<uint8_t> &payload) {
        put32(c.out, (uint32_t)payload.size() + 1);
        c.out.push_back(type);
        c.out.insert(c.out.end(), payload.begin(), payload.end());
        flush(c);
    }
    void flush(Conn &c) {
        while (!c.out.empty() && !c.dead && !c.connecting) {
            const int n = (int)::send(c.s, reinterpret_cast<const char *>(c.out.data()), (int)c.out.size(), 0);
            if (n > 0) { c.out.erase(c.out.begin(), c.out.begin() + n); continue; }
            if (n < 0 && err_would_block(sock_err())) break;
            c.dead = true;
        }
    }
    Pend *find_pend(uint64_t op) {
        for (Pend &p : pend) if (p.op == op) return &p;
        return nullptr;
    }
    void finish(uint64_t op, uint8_t code) {
        for (size_t i = 0; i < pend.size(); i++) {
            if (pend[i].op != op) continue;
            pend[i].ncb->cplt = code;
            pend.erase(pend.begin() + (long)i);
            return;
        }
    }
    // An NCB submitted again, or cancelled: forget its pending operation (and a call's connection).
    bool drop_pending(NetNcb *ncb) {
        for (size_t i = 0; i < pend.size(); i++) {
            if (pend[i].ncb != ncb) continue;
            if (pend[i].kind == P_CALL_CONNECT) {
                for (auto &kv : conns)
                    if (kv.second.kind == CK_CALLING && kv.second.op == pend[i].op) kv.second.dead = true;
            }
            pend.erase(pend.begin() + (long)i);
            return true;
        }
        return false;
    }
    uint8_t new_lsn() {
        for (int i = 0; i < 254; i++) {
            const uint8_t l = next_lsn;
            next_lsn = (uint8_t)(next_lsn == 254 ? 1 : next_lsn + 1);
            if (!sessions.count(l)) return l;
        }
        return 0;
    }
    bool has_session_with(const std::string &local, const std::string &remote) {
        for (auto &kv : sessions)
            if (!kv.second.closed && kv.second.local == local && kv.second.remote == remote) return true;
        return false;
    }
    bool calling(const std::string &local, const std::string &remote) {
        for (Pend &p : pend)
            if ((p.kind == P_CALL_LOOKUP || p.kind == P_CALL_CONNECT) &&
                key15(p.ncb->name) == local && key15(p.ncb->callname) == remote) return true;
        return false;
    }

    // ---- start-up ----
    bool open_listener(sock_t &s, uint16_t want, uint16_t *got, bool exclusive) {
        s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == BAD_SOCK) return false;
#ifdef _WIN32
        if (exclusive) { BOOL on = TRUE; setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on), sizeof on); }
#else
        (void)exclusive;
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        // MC_NET_BIND=<IPv4> listens on that address only (the tests use 127.0.0.1: a loopback listener
        // raises no Windows Firewall prompt); default all interfaces.
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (const char *b = std::getenv("MC_NET_BIND")) {
            in_addr ia{};
            if (inet_pton(AF_INET, b, &ia) == 1) a.sin_addr = ia;
        }
        a.sin_port = htons(want);
        if (::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(s, 16) != 0) {
            sock_close(s);
            s = BAD_SOCK;
            return false;
        }
        if (got) {
            socklen_t len = sizeof a;
            getsockname(s, reinterpret_cast<sockaddr *>(&a), &len);
            *got = ntohs(a.sin_port);
        }
        sock_nonblock(s);
        return true;
    }
    static bool resolve(const std::string &h, uint32_t *ip) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(h.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
        *ip = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(res);
        return true;
    }
    static bool is_loopback(uint32_t ip) { return (ntohl(ip) >> 24) == 127; }
    // Blocking connect with a timeout (start-up only).
    sock_t connect_to(uint32_t ip, uint16_t p, int timeout_ms) {
        sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == BAD_SOCK) return BAD_SOCK;
        sock_nonblock(s);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = ip;
        a.sin_port = htons(p);
        if (::connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 && !err_would_block(sock_err())) {
            sock_close(s);
            return BAD_SOCK;
        }
        fd_set w, e;
        FD_ZERO(&w); FD_ZERO(&e);
        FD_SET(s, &w); FD_SET(s, &e);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (select((int)s + 1, nullptr, &w, &e, &tv) <= 0 || FD_ISSET(s, &e)) { sock_close(s); return BAD_SOCK; }
        int soerr = 0;
        socklen_t len = sizeof soerr;
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soerr), &len);
        if (soerr != 0) { sock_close(s); return BAD_SOCK; }
        return s;
    }
    bool become_name_server() {
        if (!open_listener(ns_listen, port, nullptr, true)) return false;
        ns_local = true;
        ns_ip = htonl(INADDR_LOOPBACK);
        return true;
    }
    bool join_name_server(uint32_t ip) {
        sock_t s = connect_to(ip, port, 3000);
        if (s == BAD_SOCK) return false;
        Conn &c = add_conn(s, CK_NS_CLIENT);
        ns_conn = c.id;
        ns_ip = ip;
        return true;
    }
    bool start() {
        if (started) return true;
#ifdef _WIN32
        if (!wsa) {
            WSADATA w;
            if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { err = "WSAStartup failed"; return false; }
            wsa = true;
        }
#endif
        if (!open_listener(sess_listen, 0, &sess_port, false)) { err = "cannot open the session port"; return false; }
        uint32_t ip = htonl(INADDR_LOOPBACK);
        if (!host.empty() && !resolve(host, &ip)) { err = "cannot resolve " + host; close_everything(); return false; }
        bool ok;
        if (host.empty()) ok = become_name_server() || join_name_server(ip);
        else ok = join_name_server(ip) || (is_loopback(ip) && become_name_server());
        if (!ok) { err = "no name server at " + (host.empty() ? std::string("127.0.0.1") : host) + ":" + std::to_string(port); close_everything(); return false; }
        started = true;
        return true;
    }
    void close_everything() {
        for (auto &kv : conns) if (kv.second.s != BAD_SOCK) sock_close(kv.second.s);
        conns.clear();
        if (ns_listen != BAD_SOCK) sock_close(ns_listen);
        if (sess_listen != BAD_SOCK) sock_close(sess_listen);
        ns_listen = sess_listen = BAD_SOCK;
        for (Pend &p : pend) p.ncb->cplt = NRC_CMDCAN;
        pend.clear();
        sessions.clear();
        names.clear();
        ns_table.clear();
        ns_reqs.clear();
        ns_local = false;
        ns_conn = 0;
        started = false;
    }

    // ---- name service ----
    // NS side: answer a lookup for a requester whose control stream (or this process, c == nullptr) asks.
    void ns_resolve(const std::string &name, Conn *c, uint8_t *code, uint32_t *ip, uint16_t *p) {
        auto it = ns_table.find(name);
        if (it == ns_table.end()) { *code = NRC_NOCALL; *ip = 0; *p = 0; return; }
        *code = 0;
        *p = it->second.port;
        *ip = it->second.ip;
        if (*ip == 0 || is_loopback(*ip)) {
            // registered from the name server's machine: reachable at the name server's own address
            if (c) {
                sockaddr_in a{};
                socklen_t len = sizeof a;
                getsockname(c->s, reinterpret_cast<sockaddr *>(&a), &len);
                *ip = a.sin_addr.s_addr;
            } else {
                *ip = htonl(INADDR_LOOPBACK);
            }
        }
    }
    void ns_handle(Conn &c, uint8_t type, const uint8_t *p, size_t n) {
        if (n < 15) return;
        const std::string name(reinterpret_cast<const char *>(p), 15);
        if (type == F_REG && n >= 17) {
            uint8_t code = 0;
            if (ns_table.count(name)) code = NRC_INUSE;
            else ns_table[name] = NsEntry{c.id, c.peer_ip, get16(p + 15)};
            send_frame(c, F_REG_REPLY, {code});
        } else if (type == F_UNREG) {
            auto it = ns_table.find(name);
            if (it != ns_table.end() && it->second.owner == c.id) ns_table.erase(it);
        } else if (type == F_LOOKUP) {
            uint8_t code; uint32_t ip; uint16_t port16;
            ns_resolve(name, &c, &code, &ip, &port16);
            std::vector<uint8_t> r{code};
            put32(r, ip);
            put16(r, port16);
            send_frame(c, F_LOOKUP_REPLY, r);
        }
    }
    void ns_reply(uint8_t type, const uint8_t *p, size_t n) {
        if (ns_reqs.empty() || n < 1) return;
        NsReq rq = ns_reqs.front();
        ns_reqs.pop_front();
        Pend *pe = find_pend(rq.op);
        if (type == F_REG_REPLY) {
            if (p[0] == 0) {
                if (pe) {
                    names.push_back(rq.name);
                    pe->ncb->num = name_num++;
                    finish(rq.op, 0);
                } else {                      // cancelled meanwhile: give the name back
                    std::vector<uint8_t> v(rq.name.begin(), rq.name.end());
                    if (Conn *c = conn(ns_conn)) send_frame(*c, F_UNREG, v);
                }
            } else if (pe) {
                finish(rq.op, p[0]);
            }
        } else if (type == F_LOOKUP_REPLY && n >= 7) {
            if (!pe) return;
            if (p[0] != 0) { finish(rq.op, NRC_NOCALL); return; }
            uint32_t ip = get32(p + 1);
            if (ip == 0) ip = ns_ip;
            start_call(rq.op, ip, get16(p + 5));
        }
    }

    // ---- sessions ----
    void start_call(uint64_t op, uint32_t ip, uint16_t p) {
        Pend *pe = find_pend(op);
        if (!pe) return;
        sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == BAD_SOCK) { finish(op, NRC_NOCALL); return; }
        Conn &c = add_conn(s, CK_CALLING);
        c.op = op;
        c.peer_ip = ip;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = ip;
        a.sin_port = htons(p);
        pe->kind = P_CALL_CONNECT;
        if (::connect(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) {
            if (!err_would_block(sock_err())) { c.dead = true; return; }
            c.connecting = true;
            return;
        }
        call_connected(c);
    }
    void call_connected(Conn &c) {
        c.connecting = false;
        Pend *pe = find_pend(c.op);
        if (!pe) { c.dead = true; return; }
        std::vector<uint8_t> v;
        const std::string me = key15(pe->ncb->name), them = key15(pe->ncb->callname);
        v.insert(v.end(), me.begin(), me.end());
        v.insert(v.end(), them.begin(), them.end());
        send_frame(c, F_CALL_REQ, v);
    }
    // An incoming CALL_REQ {caller, callee}: complete a matching LISTEN or reject.
    void incoming_call(Conn &c, const uint8_t *p, size_t n) {
        if (n < 30) { c.dead = true; return; }
        const std::string caller(reinterpret_cast<const char *>(p), 15);
        const std::string callee(reinterpret_cast<const char *>(p + 15), 15);
        Pend *listen = nullptr;
        for (Pend &pe : pend) {
            if (pe.kind != P_LISTEN || key15(pe.ncb->name) != callee) continue;
            if (pe.ncb->callname[0] == '*' || key15(pe.ncb->callname) == caller) { listen = &pe; break; }
        }
        bool ok = listen != nullptr;
        if (ok && has_session_with(callee, caller)) ok = false;
        // Both sides calling each other at once: only the call from the smaller name is accepted, so
        // every pair of players ends up with exactly one session.
        if (ok && calling(callee, caller) && !(caller < callee)) ok = false;
        if (!ok) {
            send_frame(c, F_CALL_ACK, {NRC_REJECTED});
            c.dead = true;            // closed after the flush below
            return;
        }
        const uint8_t lsn = new_lsn();
        Session &s = sessions[lsn];
        s.conn = c.id;
        s.local = callee;
        s.remote = caller;
        c.kind = CK_SESSION;
        c.lsn = lsn;
        send_frame(c, F_CALL_ACK, {0});
        NetNcb *ncb = listen->ncb;
        ncb->lsn = lsn;
        std::memcpy(ncb->callname, caller.data(), 15);
        ncb->callname[15] = 0;
        finish(listen->op, 0);
    }
    void call_ack(Conn &c, const uint8_t *p, size_t n) {
        Pend *pe = find_pend(c.op);
        const uint8_t code = n >= 1 ? p[0] : NRC_REJECTED;
        if (!pe) { c.dead = true; return; }
        if (code != 0) { c.dead = true; finish(c.op, code); return; }
        const uint8_t lsn = new_lsn();
        Session &s = sessions[lsn];
        s.conn = c.id;
        s.local = key15(pe->ncb->name);
        s.remote = key15(pe->ncb->callname);
        c.kind = CK_SESSION;
        c.lsn = lsn;
        pe->ncb->lsn = lsn;
        finish(c.op, 0);
    }
    bool deliver(NetNcb *ncb, Session &s) {
        if (!s.q.empty()) {
            std::vector<uint8_t> m = std::move(s.q.front());
            s.q.pop_front();
            uint8_t code = 0;
            size_t len = m.size();
            if (len > ncb->length) { len = ncb->length; code = NRC_INCOMP; }
            if (len && ncb->buffer) std::memcpy(ncb->buffer, m.data(), len);
            ncb->length = (uint16_t)len;
            ncb->cplt = code;
            return true;
        }
        if (s.closed) { ncb->cplt = NRC_SCLOSED; return true; }
        return false;
    }
    void complete_receives() {
        for (size_t i = 0; i < pend.size();) {
            Pend &pe = pend[i];
            if (pe.kind == P_RECEIVE) {
                auto it = sessions.find(pe.ncb->lsn);
                if (it == sessions.end()) { pe.ncb->cplt = NRC_BADLSN; pend.erase(pend.begin() + (long)i); continue; }
                if (deliver(pe.ncb, it->second)) { pend.erase(pend.begin() + (long)i); continue; }
            }
            i++;
        }
    }

    void on_frame(Conn &c, uint8_t type, const uint8_t *p, size_t n) {
        switch (c.kind) {
        case CK_NS_PEER: ns_handle(c, type, p, n); break;
        case CK_NS_CLIENT: ns_reply(type, p, n); break;
        case CK_INCOMING: if (type == F_CALL_REQ) incoming_call(c, p, n); else c.dead = true; break;
        case CK_CALLING: if (type == F_CALL_ACK) call_ack(c, p, n); else c.dead = true; break;
        case CK_SESSION:
            if (type == F_DATA) {
                auto it = sessions.find(c.lsn);
                if (it != sessions.end()) it->second.q.emplace_back(p, p + n);
            }
            break;
        }
    }
    void on_dead(Conn &c) {
        switch (c.kind) {
        case CK_NS_PEER:
            for (auto it = ns_table.begin(); it != ns_table.end();)
                it = it->second.owner == c.id ? ns_table.erase(it) : std::next(it);
            break;
        case CK_NS_CLIENT:                    // the name server left: what was asked of it fails
            for (NsReq &rq : ns_reqs) finish(rq.op, rq.type == F_REG ? NRC_SYSTEM : NRC_NOCALL);
            ns_reqs.clear();
            ns_conn = 0;
            break;
        case CK_CALLING: finish(c.op, NRC_NOCALL); break;
        case CK_SESSION: {
            auto it = sessions.find(c.lsn);
            if (it != sessions.end()) it->second.closed = true;
            break;
        }
        case CK_INCOMING: break;
        }
    }
    void read_conn(Conn &c) {
        uint8_t buf[8192];
        for (;;) {
            const int n = (int)::recv(c.s, reinterpret_cast<char *>(buf), (int)sizeof buf, 0);
            if (n > 0) { c.in.insert(c.in.end(), buf, buf + n); continue; }
            if (n < 0 && err_would_block(sock_err())) break;
            c.dead = true;
            break;
        }
        size_t at = 0;
        while (c.in.size() - at >= 5) {
            const uint32_t len = get32(&c.in[at]);
            if (len == 0 || len > (1u << 24)) { c.dead = true; break; }
            if (c.in.size() - at - 4 < len) break;
            const uint8_t type = c.in[at + 4];
            const std::vector<uint8_t> payload(c.in.begin() + (long)(at + 5), c.in.begin() + (long)(at + 4 + len));
            at += 4 + len;
            on_frame(c, type, payload.data(), payload.size());
        }
        c.in.erase(c.in.begin(), c.in.begin() + (long)at);
    }

    void pump(int timeout_ms) {
        if (!started) return;
        fd_set r, w, e;
        FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
        sock_t maxs = 0;
        auto add = [&](sock_t s, fd_set *set) { FD_SET(s, set); if (s > maxs) maxs = s; };
        if (ns_listen != BAD_SOCK) add(ns_listen, &r);
        if (sess_listen != BAD_SOCK) add(sess_listen, &r);
        for (auto &kv : conns) {
            Conn &c = kv.second;
            if (c.dead) continue;
            add(c.s, &r);
            if (c.connecting || !c.out.empty()) add(c.s, &w);
            if (c.connecting) add(c.s, &e);
        }
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        const int ready = select((int)maxs + 1, &r, &w, &e, &tv);
        if (ready > 0) {
            auto accept_on = [&](sock_t ls, ConnKind k) {
                for (;;) {
                    sockaddr_in a{};
                    socklen_t len = sizeof a;
                    sock_t s = ::accept(ls, reinterpret_cast<sockaddr *>(&a), &len);
                    if (s == BAD_SOCK) break;
                    Conn &c = add_conn(s, k);
                    c.peer_ip = a.sin_addr.s_addr;
                }
            };
            if (ns_listen != BAD_SOCK && FD_ISSET(ns_listen, &r)) accept_on(ns_listen, CK_NS_PEER);
            if (sess_listen != BAD_SOCK && FD_ISSET(sess_listen, &r)) accept_on(sess_listen, CK_INCOMING);
            std::vector<int> ids;
            for (auto &kv : conns) ids.push_back(kv.first);
            for (int id : ids) {
                Conn *c = conn(id);
                if (!c || c->dead) continue;
                if (c->connecting && (FD_ISSET(c->s, &w) || FD_ISSET(c->s, &e))) {
                    int soerr = 0;
                    socklen_t len = sizeof soerr;
                    getsockopt(c->s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soerr), &len);
                    if (soerr != 0 || FD_ISSET(c->s, &e)) { c->dead = true; continue; }
                    call_connected(*c);
                }
                if (FD_ISSET(c->s, &r)) read_conn(*c);
                if (!c->dead && FD_ISSET(c->s, &w)) flush(*c);
            }
        }
        // retire dead connections (their last frames were flushed above when the socket allowed it)
        for (auto it = conns.begin(); it != conns.end();) {
            Conn &c = it->second;
            if (!c.dead) { ++it; continue; }
            flush_final(c);
            on_dead(c);
            sock_close(c.s);
            it = conns.erase(it);
        }
        complete_receives();
    }
    void flush_final(Conn &c) {               // a rejection ACK queued on a connection about to close
        if (c.out.empty() || c.connecting) return;
        ::send(c.s, reinterpret_cast<const char *>(c.out.data()), (int)c.out.size(), 0);
        c.out.clear();
    }

    int submit(NetNcb *ncb) {
        if (!start()) return -1;
        drop_pending(ncb);
        ncb->retcode = 0;
        const uint64_t op = next_op++;
        switch (ncb->cmd) {
        case NCB_INVALID:
            ncb->retcode = NRC_ILLCMD;
            ncb->cplt = NRC_ILLCMD;
            return 0;
        case NCB_ADD_NAME: {
            const std::string name = key15(ncb->name);
            for (const std::string &n : names) if (n == name) { ncb->cplt = NRC_DUPNAME; return 0; }
            if (ns_local) {
                if (ns_table.count(name)) { ncb->cplt = NRC_INUSE; return 0; }
                ns_table[name] = NsEntry{0, 0, sess_port};
                names.push_back(name);
                ncb->num = name_num++;
                ncb->cplt = 0;
                return 0;
            }
            Conn *c = conn(ns_conn);
            if (!c) { ncb->cplt = NRC_SYSTEM; return 0; }
            std::vector<uint8_t> v(name.begin(), name.end());
            put16(v, sess_port);
            send_frame(*c, F_REG, v);
            ns_reqs.push_back(NsReq{F_REG, op, name});
            pend.push_back(Pend{ncb, P_ADD_NAME, op});
            ncb->cplt = NRC_PENDING;
            return 0;
        }
        case NCB_DELETE_NAME: {
            const std::string name = key15(ncb->name);
            for (size_t i = 0; i < names.size(); i++) if (names[i] == name) { names.erase(names.begin() + (long)i); break; }
            if (ns_local) {
                auto it = ns_table.find(name);
                if (it != ns_table.end() && it->second.owner == 0) ns_table.erase(it);
            } else if (Conn *c = conn(ns_conn)) {
                send_frame(*c, F_UNREG, std::vector<uint8_t>(name.begin(), name.end()));
            }
            ncb->cplt = 0;
            return 0;
        }
        case NCB_LISTEN:
            pend.push_back(Pend{ncb, P_LISTEN, op});
            ncb->cplt = NRC_PENDING;
            return 0;
        case NCB_CALL: {
            const std::string callee = key15(ncb->callname);
            pend.push_back(Pend{ncb, P_CALL_LOOKUP, op});
            ncb->cplt = NRC_PENDING;
            if (ns_local) {
                uint8_t code; uint32_t ip; uint16_t p;
                ns_resolve(callee, nullptr, &code, &ip, &p);
                if (code != 0) finish(op, NRC_NOCALL);
                else start_call(op, ip, p);
                return 0;
            }
            Conn *c = conn(ns_conn);
            if (!c) { finish(op, NRC_NOCALL); return 0; }
            send_frame(*c, F_LOOKUP, std::vector<uint8_t>(callee.begin(), callee.end()));
            ns_reqs.push_back(NsReq{F_LOOKUP, op, callee});
            return 0;
        }
        case NCB_HANGUP: {
            auto it = sessions.find(ncb->lsn);
            if (it == sessions.end() || ncb->lsn == 0) { ncb->cplt = NRC_BADLSN; return 0; }
            if (Conn *c = conn(it->second.conn)) { flush(*c); c->dead = true; }
            sessions.erase(it);
            ncb->cplt = 0;
            return 0;
        }
        case NCB_SEND: {
            auto it = sessions.find(ncb->lsn);
            if (it == sessions.end() || ncb->lsn == 0) { ncb->cplt = NRC_BADLSN; return 0; }
            Conn *c = conn(it->second.conn);
            if (it->second.closed || !c || c->dead) { ncb->cplt = NRC_SCLOSED; return 0; }
            send_frame(*c, F_DATA, std::vector<uint8_t>(ncb->buffer, ncb->buffer + ncb->length));
            ncb->cplt = 0;
            return 0;
        }
        case NCB_RECEIVE: {
            auto it = sessions.find(ncb->lsn);
            if (it == sessions.end() || ncb->lsn == 0) { ncb->cplt = NRC_BADLSN; return 0; }
            if (deliver(ncb, it->second)) return 0;
            pend.push_back(Pend{ncb, P_RECEIVE, op});
            ncb->cplt = NRC_PENDING;
            return 0;
        }
        case NCB_CANCEL: {
            NetNcb *t = ncb->cancel_target;
            if (t && drop_pending(t)) { t->cplt = NRC_CMDCAN; ncb->cplt = 0; }
            else ncb->cplt = 0x24;            // command completed while cancel occurring
            return 0;
        }
        default:
            ncb->cplt = NRC_ILLCMD;
            return 0;
        }
    }
};

TcpTransport::TcpTransport(const char *host, uint16_t port) : d(new Impl) {
    d->host = host ? host : "";
    d->port = port ? port : 30600;
}
TcpTransport::~TcpTransport() {
    d->close_everything();
#ifdef _WIN32
    if (d->wsa) WSACleanup();
#endif
}
bool TcpTransport::present() { return d->start(); }
int  TcpTransport::submit(NetNcb *ncb) { return d->submit(ncb); }
void TcpTransport::poll(int timeout_ms) { d->pump(timeout_ms); }
void TcpTransport::close_all() { d->close_everything(); }
bool TcpTransport::is_name_server() const { return d->ns_local; }
uint16_t TcpTransport::session_port() const { return d->sess_port; }
const char *TcpTransport::last_error() const { return d->err.c_str(); }

TcpTransport *net_tcp_from_env(bool force) {
    const char *host = std::getenv("MC_NET_HOST");
    const char *port = std::getenv("MC_NET_PORT");
    const char *on = std::getenv("MC_NET");
    const bool want = force || (host && *host) || (on && *on && std::strcmp(on, "0") != 0);
    if (!want) return nullptr;
    const long p = port ? std::strtol(port, nullptr, 10) : 30600;
    return new TcpTransport(host ? host : "", (uint16_t)(p > 0 && p < 65536 ? p : 30600));
}

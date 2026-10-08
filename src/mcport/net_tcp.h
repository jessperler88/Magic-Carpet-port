// TCP NetTransport for mcport (round 6, task B): NetBIOS name service + sessions over TCP/IP (net.h).
//
// One instance is the name server: it owns the TCP port (MC_NET_PORT, default 30600) and keeps the
// name table (NetBIOS "add name" / "delete name" / the name lookup of "call"). The others keep a control
// connection to it. Every instance also listens on an ephemeral port for sessions: a "call" looks the
// name up at the name server and connects straight to the called instance, so every pair of players
// has its own TCP connection (the full mesh net_session_join_4f030 builds), and the game survives the
// name server's player leaving.
//
// Who is the name server: with an empty host (MC_NET_HOST unset) the instance tries to bind the port;
// if another instance on this machine already has it, it connects to 127.0.0.1 instead. With a host
// given it connects there (and becomes the name server itself only if the host is this machine and
// nobody answers). The lobby's session name ("CARPET<n>") is part of every NetBIOS name, so several
// games can share one name server.
//
// No Thing of the game is touched here; the transport is platform code (no SDL, Winsock / BSD sockets).
#pragma once
#include "net.h"
#include <cstdint>
#include <memory>

class TcpTransport : public NetTransport {
public:
    // host: name server address ("" = this machine, try to become it); port: its TCP port.
    TcpTransport(const char *host, uint16_t port);
    ~TcpTransport() override;
    bool present() override;
    int  submit(NetNcb *ncb) override;
    void poll(int timeout_ms) override;
    void close_all() override;

    bool is_name_server() const;
    uint16_t session_port() const;         // the ephemeral port calls connect to
    const char *last_error() const;

    struct Impl;
private:
    std::unique_ptr<Impl> d;
};

// mcport helper: the transport configured from the environment (MC_NET_HOST, MC_NET_PORT), or nullptr
// when networking is not requested (MC_NET unset / 0 and no host given).
TcpTransport *net_tcp_from_env(bool force);

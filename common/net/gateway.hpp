#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <ankerl/unordered_dense.h>

#include "common/net/queues.hpp"
#include "common/net/wire.hpp"


/* Accepts TCP connections from market participants and turns their binary,
   fixed-size inbound messages into InboundCommands pushed onto `outbound_`.
   Runs its own epoll loop; call Run() from a dedicated thread.

   Simplifying assumptions (toy scope): each connection sends messages that
   already carry the correct userId_ - there's no connection/user handshake
   or authentication, and messages on one connection are processed strictly
   in the order they were sent (no reordering), so per-connection framing is
   FIFO. Ordering across different connections follows epoll delivery order. */
class Gateway {
public:
    Gateway(std::uint16_t port, InboundQueue& outbound);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    /* Binds, listens, and runs the accept/read loop until Stop() is called
       from another thread. Blocking. */
    void Run();

    // Thread-safe; unblocks a concurrent Run().
    void Stop();

private:
    struct Connection {
        std::array<std::byte, sizeof(InboundWireMessage)> buffer_{};
        std::size_t filled_ = 0;
    };

    void AcceptConnections();
    void ReadFrom(int fd);
    void CloseConnection(int fd);
    void CloseAllConnections();
    void Dispatch(const InboundWireMessage& message);

    std::uint16_t port_;
    InboundQueue& outbound_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    std::atomic<bool> running_{true};
    ankerl::unordered_dense::map<int, Connection> connections_;
};

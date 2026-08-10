#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <netinet/in.h>

#include "common/net/queues.hpp"


/* Drains a MarketDataQueue and sends each already wire-encoded
   WireMarketDataMessage as one UDP datagram to a multicast group -
   reinterpret_cast straight onto the socket, no serialization framework.

   `Local` (default true): force multicast traffic out via loopback rather
   than letting the kernel pick the default-route interface. Every
   participant in this toy exchange is local (the gateway only listens on
   127.0.0.1), and multicast loopback through a physical interface is
   unreliable on plenty of real networks/drivers even for host-to-itself
   delivery - lo doesn't have that problem. Set Local = false to preserve
   the old behavior (let the OS pick the outgoing interface via the default
   route) - e.g. if this publisher's group is actually meant to reach other
   machines. */
template <bool Local = true>
class MarketDataPublisher {
public:
    MarketDataPublisher(const std::string& multicastAddress, std::uint16_t port);
    ~MarketDataPublisher();

    MarketDataPublisher(const MarketDataPublisher&) = delete;
    MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;

    /* Runs until `matchingEngineFinished` is observed true AND `queue` is
       empty, so every event already produced still gets sent before this
       thread stops. Set `matchingEngineFinished` only after the matching
       engine thread has been joined, so no more pushes can race the empty
       check. */
    void Run(MarketDataQueue& queue, std::atomic<bool>& matchingEngineFinished);

private:
    int socketFd_ = -1;
    sockaddr_in destination_{};
};

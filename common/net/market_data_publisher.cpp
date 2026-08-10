#include "common/net/market_data_publisher.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>


template <bool Local>
MarketDataPublisher<Local>::MarketDataPublisher(const std::string& multicastAddress, std::uint16_t port) {
    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        spdlog::error("[market_data] socket() failed");
        throw std::runtime_error("MarketDataPublisher: socket() failed");
    }

    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(port);
    if (inet_pton(AF_INET, multicastAddress.c_str(), &destination_.sin_addr) != 1) {
        close(socketFd_);
        spdlog::error("[market_data] invalid multicast address {}", multicastAddress);
        throw std::runtime_error("MarketDataPublisher: invalid multicast address " + multicastAddress);
    }

    // Keep multicast traffic on the local network by default.
    unsigned char ttl = 1;
    setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    if constexpr (Local) {
        /* Every participant in this toy exchange is local (the gateway only
           listens on 127.0.0.1), so force multicast out via loopback rather
           than letting the kernel pick the default-route interface (typically
           the real NIC/Wi-Fi). Multicast loopback through a physical interface
           is unreliable on plenty of real networks/drivers even for
           host-to-itself delivery; lo doesn't have that problem. */
        in_addr loopbackInterface{};
        loopbackInterface.s_addr = htonl(INADDR_LOOPBACK);
        setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_IF, &loopbackInterface, sizeof(loopbackInterface));

        unsigned char loopEnabled = 1;
        setsockopt(socketFd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopEnabled, sizeof(loopEnabled));
    }

    spdlog::info("[market_data] publishing to {}:{}", multicastAddress, port);
}

template <bool Local>
MarketDataPublisher<Local>::~MarketDataPublisher() {
    if (socketFd_ >= 0) {
        close(socketFd_);
    }
}

template <bool Local>
void MarketDataPublisher<Local>::Run(MarketDataQueue& queue, std::atomic<bool>& matchingEngineFinished) {
    while (true) {
        WireMarketDataMessage msg;
        if (!queue.pop(msg)) {
            if (matchingEngineFinished.load(std::memory_order_acquire)) {
                break;  // matching engine is done and queue is now empty: nothing left to send
            }
            std::this_thread::yield();
            continue;
        }

        ssize_t sent = sendto(socketFd_, reinterpret_cast<const char*>(&msg), sizeof(msg), 0,
                              reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
        if (sent < 0) {
            spdlog::error("[market_data] sendto() failed: {}", std::strerror(errno));
        } else {
            spdlog::debug("[market_data] published event ({} bytes)", sizeof(msg));
        }
    }
    spdlog::info("[market_data] stopped");
}

// bool has exactly two possible values - instantiate both explicitly here
// rather than forcing this whole implementation into the header.
template class MarketDataPublisher<true>;
template class MarketDataPublisher<false>;

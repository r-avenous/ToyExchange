#include "common/net/gateway.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <spdlog/spdlog.h>


namespace {

void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

Gateway::Gateway(std::uint16_t port, InboundQueue& outbound)
    : port_(port), outbound_(outbound) {}

Gateway::~Gateway() {
    /* Run() already closes everything on a clean shutdown; this is just a
       safety net (Run() never called, or it threw before finishing). */
    Stop();
    CloseAllConnections();
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

void Gateway::Stop() {
    running_.store(false, std::memory_order_relaxed);
}

void Gateway::Run() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        spdlog::error("[gateway] socket() failed");
        throw std::runtime_error("Gateway: socket() failed");
    }

    int reuse = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[gateway] bind() failed: {}", std::strerror(errno));
        throw std::runtime_error(std::string("Gateway: bind() failed: ") + std::strerror(errno));
    }
    if (listen(listenFd_, SOMAXCONN) < 0) {
        spdlog::error("[gateway] listen() failed");
        throw std::runtime_error("Gateway: listen() failed");
    }
    SetNonBlocking(listenFd_);

    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        spdlog::error("[gateway] epoll_create1() failed");
        throw std::runtime_error("Gateway: epoll_create1() failed");
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev);

    spdlog::info("[gateway] listening on tcp:{}", port_);

    std::array<epoll_event, 64> events{};
    while (running_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epollFd_, events.data(), static_cast<int>(events.size()), /*timeout_ms=*/100);
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == listenFd_) {
                AcceptConnections();
            } else {
                ReadFrom(events[i].data.fd);
            }
        }
    }

    CloseAllConnections();
    close(epollFd_);
    epollFd_ = -1;
    close(listenFd_);
    listenFd_ = -1;
    spdlog::info("[gateway] stopped");
}

void Gateway::AcceptConnections() {
    for (;;) {
        int fd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK);
        if (fd < 0) {
            break;  // no more pending connections (EAGAIN/EWOULDBLOCK)
        }

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
        connections_[fd] = Connection{};
        spdlog::info("[gateway] accepted connection fd={}", fd);
    }
}

void Gateway::ReadFrom(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    Connection& conn = it->second;

    for (;;) {
        std::size_t remaining = conn.buffer_.size() - conn.filled_;
        ssize_t n = read(fd, conn.buffer_.data() + conn.filled_, remaining);
        if (n > 0) {
            conn.filled_ += static_cast<std::size_t>(n);
            if (conn.filled_ == conn.buffer_.size()) {
                Dispatch(*reinterpret_cast<const InboundWireMessage*>(conn.buffer_.data()));
                conn.filled_ = 0;  // ready for the next fixed-size message
            }
            if (static_cast<std::size_t>(n) < remaining) {
                break;  // drained the socket for now
            }
        } else if (n == 0) {
            CloseConnection(fd);
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                CloseConnection(fd);
            }
            break;
        }
    }
}

void Gateway::CloseConnection(int fd) {
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(fd);
    spdlog::info("[gateway] closed connection fd={}", fd);
}

void Gateway::CloseAllConnections() {
    if (connections_.empty()) {
        return;
    }
    spdlog::info("[gateway] closing {} connection(s)", connections_.size());
    for (const auto& [fd, conn] : connections_) {
        close(fd);
    }
    connections_.clear();
}

void Gateway::Dispatch(const InboundWireMessage& message) {
    InboundCommand cmd = DecodeInboundMessage(message);
    spdlog::debug("[gateway] dispatched {}", std::holds_alternative<OrderAdd>(cmd) ? "OrderAdd" : "OrderCancel");
    while (!outbound_.push(cmd)) {
        std::this_thread::yield();  // matching engine thread will drain it
    }
}

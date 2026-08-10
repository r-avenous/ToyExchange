#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/engine/user.hpp"


/* Assumed fixed for now: every participant sends messages carrying a userId_
   already in [0, MARKET_PARTICIPANT_COUNT). */
inline constexpr std::int64_t MARKET_PARTICIPANT_COUNT = 100;

// Every market participant, indexed by UserId.
class UserRegistry {
public:
    UserRegistry() {
        users_.reserve(static_cast<std::size_t>(MARKET_PARTICIPANT_COUNT));
        for (std::int64_t i = 0; i < MARKET_PARTICIPANT_COUNT; ++i) {
            users_.emplace_back(UserId(i));
        }
    }

    User& Get(UserId id) {
        return users_.at(static_cast<std::size_t>(id.value()));
    }

    /* Writes one CSV row per (user, ticker) holding that was ever touched -
       users/tickers with no trading history are omitted. */
    void WriteSnapshot(const std::string& path) const {
        std::ofstream out(path, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("UserRegistry::WriteSnapshot: failed to open " + path);
        }

        out << "userId,ticker,position,avgCost,realizedPnl,unrealizedPnl\n";
        for (const User& user : users_) {
            for (const auto& [ticker, holding] : user.Inventory()) {
                out << user.Id().value() << ',' << ticker << ','
                    << holding.volume_.value() << ',' << holding.avgCost_.value() << ','
                    << holding.realizedPnl_.value() << ',' << holding.unrealizedPnl_.value() << '\n';
            }
        }
    }

private:
    std::vector<User> users_;
};

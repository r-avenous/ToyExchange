#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>

#include "common/core/messages.hpp"
#include "common/core/types.hpp"
#include "common/engine/matching_engine.hpp"


/* Position of `ticker` within INSTRUMENTS, or INSTRUMENTS.size() if
   `ticker` isn't a listed instrument. */
inline std::size_t TickerIndex(const Ticker& ticker) {
    return static_cast<std::size_t>(
        std::ranges::distance(INSTRUMENTS.begin(), 
            std::ranges::find(INSTRUMENTS, ticker))
        );
}

/* requests for an unlisted ticker are silently dropped. */
class MatchingEngineRouter {
public:
    template <typename TradeHandler>
    void OnOrderAdd(const OrderAdd& order, TradeHandler&& onTrade) {
        const std::size_t index = TickerIndex(order.ticker_);
        if (index < engines_.size()) {
            engines_[index].OnOrderAdd(order, std::forward<TradeHandler>(onTrade));
        }
    }

    void OnOrderCancel(const OrderCancel& cancel) {
        const std::size_t index = TickerIndex(cancel.ticker_);
        if (index < engines_.size()) {
            engines_[index].OnOrderCancel(cancel);
        }
    }

    // Empty if `ticker` isn't a listed instrument (or its book has never
    // had liquidity yet).
    std::optional<Price> MidPrice(const Ticker& ticker) const {
        const std::size_t index = TickerIndex(ticker);
        return (index < engines_.size()) ? engines_[index].MidPrice() : std::nullopt;
    }

private:
    std::array<MatchingEngine, INSTRUMENTS.size()> engines_;
};

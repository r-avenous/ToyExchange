#include "common/workers/inventory_thread.hpp"

#include <thread>

#include <spdlog/spdlog.h>


void RunInventoryThread(TradeQueue& trades, UserRegistry& registry, const MatchingEngineRouter& router,
                         std::atomic<bool>& matchingEngineFinished) {
    while (true) {
        Trade trade;
        if (!trades.pop(trade)) {
            if (matchingEngineFinished.load(std::memory_order_acquire)) {
                break;  // matching engine is done and trades is now empty: nothing left to settle
            }
            std::this_thread::yield();
            continue;
        }

        const Side makerSide = (trade.takerSide_ == Side::Buy) ? Side::Sell : Side::Buy;

        User& maker = registry.Get(trade.makerId_);
        User& taker = registry.Get(trade.takerId_);
        maker.ApplyFill(trade.ticker_, makerSide, trade.executionPrice_, trade.volume_);
        taker.ApplyFill(trade.ticker_, trade.takerSide_, trade.executionPrice_, trade.volume_);

        if (const std::optional<Price> mid = router.MidPrice(trade.ticker_); mid.has_value()) {
            maker.RefreshUnrealizedPnl(trade.ticker_, *mid);
            taker.RefreshUnrealizedPnl(trade.ticker_, *mid);
        }

        spdlog::debug("[inventory] settled trade {} maker={} taker={} volume={}",
                      trade.ticker_.c_str(), trade.makerId_.value(), trade.takerId_.value(), trade.volume_.value());
    }
    spdlog::info("[inventory] stopped");
}

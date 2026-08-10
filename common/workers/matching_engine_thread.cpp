#include "common/workers/matching_engine_thread.hpp"

#include <thread>
#include <variant>

#include <spdlog/spdlog.h>

#include "common/net/wire.hpp"


namespace {

/* Backpressure: if a downstream queue is momentarily full, yield and retry
   rather than dropping the message. There's only one producer per queue, so
   this can't deadlock against ourselves. */
template <typename Queue, typename T>
void PushBlocking(Queue& queue, const T& value) {
    while (!queue.push(value)) {
        std::this_thread::yield();
    }
}

}  // namespace

void RunMatchingEngineThread(MatchingEngineRouter& router, InboundQueue& inbound,
                              MarketDataQueue& marketData, TradeQueue& trades,
                              std::atomic<bool>& gatewayFinished) {
    while (true) {
        InboundCommand cmd;
        if (!inbound.pop(cmd)) {
            if (gatewayFinished.load(std::memory_order_acquire)) {
                break;  // gateway is done and inbound is now empty: nothing left to process
            }
            std::this_thread::yield();
            continue;
        }

        if (const auto* add = std::get_if<OrderAdd>(&cmd)) {
            spdlog::debug("[matching_engine] OrderAdd id={} user={} {} side={} price={} volume={}",
                          add->orderId_.value(), add->userId_.value(), add->ticker_.c_str(),
                          add->side_ == Side::Buy ? "Buy" : "Sell", add->price_.value(), add->volume_.value());

            PushBlocking(marketData, EncodeOrderAddEvent(*add));
            router.OnOrderAdd(*add, [&](const Trade& trade) {
                spdlog::info("[matching_engine] trade {} maker={} taker={} price={} volume={}",
                             trade.ticker_.c_str(), trade.makerId_.value(), trade.takerId_.value(),
                             trade.executionPrice_.value(), trade.volume_.value());

                PushBlocking(marketData, EncodeTradeEvent(trade));
                PushBlocking(trades, trade);
            });
        } else {
            const auto& cancel = std::get<OrderCancel>(cmd);
            spdlog::debug("[matching_engine] OrderCancel id={} user={} {}",
                          cancel.orderId_.value(), cancel.userId_.value(), cancel.ticker_.c_str());

            router.OnOrderCancel(cancel);
            PushBlocking(marketData, EncodeOrderCancelEvent(cancel));
        }
    }
    spdlog::info("[matching_engine] stopped");
}

#pragma once

#include <cstddef>
#include <deque>
#include <flat_map>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <utility>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "common/core/messages.hpp"
#include "common/core/seqlock.hpp"
#include "common/core/types.hpp"


/* Price-time-priority matching engine */
class MatchingEngine {
public:
    /* Matches `order` against the resting book, invoking `onTrade(trade)`
       on the spot for each trade it generates, in generation order. 
       Any unfilled remainder rests in the book as a new resting order. */
    template <typename TradeHandler>
    void OnOrderAdd(const OrderAdd& order, TradeHandler&& onTrade);

    /* Removes a resting order. A no-op if the order is unknown (already
       filled/cancelled) or `cancel` isn't from the order's owner. */
    void OnOrderCancel(const OrderCancel& cancel);

    /* The current (best bid + best ask) / 2, snapshotted through a seqlock */
    std::optional<Price> MidPrice() const;

private:
    struct RestingOrder {
        OrderId orderId_;
        UserId userId_;
        Volume volume_;
    };
    struct OrderLocation {
        Side side_;
        Price price_;
        UserId userId_;
    };

    /* Preallocated arena backing this instrument's book */
    static constexpr std::size_t ARENA_SIZE = 4 * 1024 * 1024;  // 4 MiB
    std::unique_ptr<std::byte[]> arena_ = std::make_unique<std::byte[]>(ARENA_SIZE);
    std::pmr::monotonic_buffer_resource arenaResource_{
        arena_.get(), 
        ARENA_SIZE, 
        std::pmr::new_delete_resource()
    };
    std::pmr::unsynchronized_pool_resource pool_{&arenaResource_};

    using PriceLevel = std::pmr::deque<RestingOrder>;  // front = oldest (time priority)
    using Bids = std::flat_map<Price, PriceLevel, std::greater<>,
                                std::pmr::vector<Price>, std::pmr::vector<PriceLevel>>;  // best (highest) bid first
    using Asks = std::flat_map<Price, PriceLevel, std::less<>,
                                std::pmr::vector<Price>, std::pmr::vector<PriceLevel>>;  // best (lowest) ask first
    using OrderIndex = ankerl::unordered_dense::map<OrderId, OrderLocation, ankerl::unordered_dense::hash<OrderId>,
                                                      std::equal_to<OrderId>,
                                                      std::pmr::polymorphic_allocator<std::pair<OrderId, OrderLocation>>>;

    /* Walks `book` best-price-first, filling `incoming` against it while
       `priceCrosses(incoming.price_, levelPrice)` holds, calling
       `onTrade(trade)` immediately for each fill. Returns whatever volume
       is left unfilled. */
    template <typename Book, typename PriceCrosses, typename TradeHandler>
    Volume Match(Book& book, const OrderAdd& incoming, Volume remaining,
                 PriceCrosses priceCrosses, TradeHandler&& onTrade);

    // Rests the unfilled remainder of `order` in the appropriate book.
    void Rest(const OrderAdd& order, Volume remaining);

    template <typename Book>
    void EraseFromBook(Book& book, Price price, OrderId orderId);

    /* Recomputes the mid price from the current top of book and publishes
       it via midPrice_. Call after any mutation that could move the top of
       either book. */
    void UpdateMidPrice();

    Bids bids_{&pool_};
    Asks asks_{&pool_};
    OrderIndex orderIndex_{&pool_};
    Seqlock<std::optional<Price>> midPrice_;
};

// -- template member definitions, must stay visible wherever OnOrderAdd is instantiated --

template <typename Book, typename PriceCrosses, typename TradeHandler>
Volume MatchingEngine::Match(Book& book, const OrderAdd& incoming, Volume remaining,
                              PriceCrosses priceCrosses, TradeHandler&& onTrade) {
    while (remaining > Volume(0) && !book.empty()) {
        auto levelIt = book.begin();
        if (!priceCrosses(incoming.price_, levelIt->first)) {
            break;  // best resting price no longer crosses; nothing more to match
        }

        PriceLevel& level = levelIt->second;
        while (remaining > Volume(0) && !level.empty()) {
            RestingOrder& resting = level.front();
            const Volume traded = std::min(remaining, resting.volume_);

            onTrade(Trade{
                .makerId_ = resting.userId_,
                .takerId_ = incoming.userId_,
                .makerOrderId_ = resting.orderId_,
                .takerOrderId_ = incoming.orderId_,
                .ticker_ = incoming.ticker_,
                .takerSide_ = incoming.side_,
                .executionPrice_ = levelIt->first,  // trades print at the resting (maker) price
                .volume_ = traded,
            });

            remaining -= traded;
            resting.volume_ -= traded;

            if (resting.volume_ == Volume(0)) {
                orderIndex_.erase(resting.orderId_);
                level.pop_front();
            }
        }

        if (level.empty()) {
            book.erase(levelIt);
        }
    }
    return remaining;
}

template <typename TradeHandler>
void MatchingEngine::OnOrderAdd(const OrderAdd& order, TradeHandler&& onTrade) {
    Volume remaining = order.volume_;

    if (order.side_ == Side::Buy) {
        remaining = Match(asks_, order, remaining,
                           [](Price buyPrice, Price askPrice) { return buyPrice >= askPrice; }, onTrade);
    } else {
        remaining = Match(bids_, order, remaining,
                           [](Price sellPrice, Price bidPrice) { return sellPrice <= bidPrice; }, onTrade);
    }

    if (remaining > Volume(0)) {
        Rest(order, remaining);
    }

    UpdateMidPrice();
}

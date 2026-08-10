#include "common/engine/matching_engine.hpp"

/* Match() and OnOrderAdd() are templates on the trade handler and live in
   the header - they must stay visible wherever OnOrderAdd() is instantiated. */


void MatchingEngine::Rest(const OrderAdd& order, Volume remaining) {
    PriceLevel& level = (order.side_ == Side::Buy) ? bids_[order.price_] : asks_[order.price_];
    level.push_back(RestingOrder{
        .orderId_ = order.orderId_,
        .userId_ = order.userId_,
        .volume_ = remaining,
    });

    orderIndex_[order.orderId_] = OrderLocation{
        .side_ = order.side_,
        .price_ = order.price_,
        .userId_ = order.userId_,
    };
}

template <typename Book>
void MatchingEngine::EraseFromBook(Book& book, Price price, OrderId orderId) {
    auto levelIt = book.find(price);
    if (levelIt == book.end()) {
        return;
    }

    PriceLevel& level = levelIt->second;
    std::erase_if(level, [&](const RestingOrder& o) { return o.orderId_ == orderId; });

    if (level.empty()) {
        book.erase(levelIt);
    }
}

void MatchingEngine::OnOrderCancel(const OrderCancel& cancel) {
    auto it = orderIndex_.find(cancel.orderId_);
    if (it == orderIndex_.end() || it->second.userId_ != cancel.userId_) {
        return;  // unknown order, or the cancel didn't come from its owner
    }

    const OrderLocation location = it->second;
    if (location.side_ == Side::Buy) {
        EraseFromBook(bids_, location.price_, cancel.orderId_);
    } else {
        EraseFromBook(asks_, location.price_, cancel.orderId_);
    }

    orderIndex_.erase(it);
    UpdateMidPrice();
}

void MatchingEngine::UpdateMidPrice() {
    if (!bids_.empty() && !asks_.empty()) {
        const std::int64_t mid = (bids_.begin()->first.value() + asks_.begin()->first.value()) / 2;
        midPrice_.Store(Price(mid));
    } else if (!bids_.empty()) {
        midPrice_.Store(bids_.begin()->first);
    } else if (!asks_.empty()) {
        midPrice_.Store(asks_.begin()->first);
    }
    // else: both sides empty - leave the last known mid price in place.
}

std::optional<Price> MatchingEngine::MidPrice() const {
    return midPrice_.Load();
}

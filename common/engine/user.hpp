#pragma once

#include <cmath>

#include <ankerl/unordered_dense.h>

#include "common/core/types.hpp"


/* A user's current exposure to one ticker: position, cost basis, and P&L,
   tracked with the average-cost method. avgCost_ is the cost basis
   (USD/share) of the currently open position (positive volume_ = long,
   negative = short). */
struct Holding {
    Volume volume_{};
    USD avgCost_{};
    USD realizedPnl_{};
    USD unrealizedPnl_{};  // stale until RefreshUnrealizedPnl() is called
};

class User {
public:
    explicit User(UserId id) : id_(id) {}

    UserId Id() const {
        return id_;
    }

    // Current position in `ticker` (Volume(0) if never traded).
    Volume Position(const Ticker& ticker) const {
        auto it = inventory_.find(ticker);
        return (it == inventory_.end()) ? Volume(0) : it->second.volume_;
    }

    USD RealizedPnl(const Ticker& ticker) const {
        auto it = inventory_.find(ticker);
        return (it == inventory_.end()) ? USD(0) : it->second.realizedPnl_;
    }

    USD UnrealizedPnl(const Ticker& ticker) const {
        auto it = inventory_.find(ticker);
        return (it == inventory_.end()) ? USD(0) : it->second.unrealizedPnl_;
    }

    // Every ticker this user has ever traded, and its current Holding.
    const ankerl::unordered_dense::map<Ticker, Holding>& Inventory() const {
        return inventory_;
    }

    /* Applies a fill of `tradeVolume` shares of `ticker` at `execPrice` on
       `side`, updating position/avgCost_ and realizing P&L on whatever
       portion closes an existing opposite-direction position (no risk
       checks - the resulting position may go negative/short). */
    void ApplyFill(const Ticker& ticker, Side side, Price execPrice, Volume tradeVolume) {
        Holding& holding = inventory_[ticker];
        const std::int64_t signedDelta = (side == Side::Buy) ? tradeVolume.value() : -tradeVolume.value();
        const std::int64_t oldVolume = holding.volume_.value();
        const double execUsd = ToUsd(execPrice).value();

        const bool sameDirection = (oldVolume == 0) || ((oldVolume > 0) == (signedDelta > 0));
        if (sameDirection) {
            // Flat or extending an existing position: roll the average cost forward.
            const double oldAbs = static_cast<double>(std::abs(oldVolume));
            const double addAbs = static_cast<double>(std::abs(signedDelta));
            holding.avgCost_ = USD((holding.avgCost_.value() * oldAbs + execUsd * addAbs) / (oldAbs + addAbs));
        } else {
            // Reducing, closing, or reversing: realize P&L on the closed portion.
            const std::int64_t closedAmount = std::min<std::int64_t>(std::abs(oldVolume), std::abs(signedDelta));
            const double pnlPerUnit = (oldVolume > 0) ? (execUsd - holding.avgCost_.value())
                                                       : (holding.avgCost_.value() - execUsd);
            holding.realizedPnl_ = USD(holding.realizedPnl_.value() + pnlPerUnit * static_cast<double>(closedAmount));
        }

        const std::int64_t newVolume = oldVolume + signedDelta;
        const bool flippedSign = (oldVolume > 0 && newVolume < 0) || (oldVolume < 0 && newVolume > 0);
        if (!sameDirection && flippedSign) {
            holding.avgCost_ = USD(execUsd);  // the excess opens a fresh position at the fill price
        } else if (newVolume == 0) {
            holding.avgCost_ = USD(0);
        }
        holding.volume_ = Volume(newVolume);
    }

    /* Marks the open position in `ticker` to market using `midPrice`,
       refreshing its stored unrealizedPnl_. No-op if `ticker` was never
       traded. */
    void RefreshUnrealizedPnl(const Ticker& ticker, Price midPrice) {
        auto it = inventory_.find(ticker);
        if (it == inventory_.end()) {
            return;
        }
        Holding& holding = it->second;
        const double mid = ToUsd(midPrice).value();
        holding.unrealizedPnl_ = USD(static_cast<double>(holding.volume_.value()) * (mid - holding.avgCost_.value()));
    }

private:
    UserId id_;
    ankerl::unordered_dense::map<Ticker, Holding> inventory_;
};

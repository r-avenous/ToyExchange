#pragma once

#include "common/core/types.hpp"


// Inbound request to add a new resting limit order.
struct OrderAdd {
    OrderId orderId_;
    UserId userId_;
    Ticker ticker_;
    Side side_;
    Price price_;
    Volume volume_;
};

/* Inbound request to remove a previously added order. Only the owning user
   (userId_) may cancel it. `ticker_` picks which per-instrument book to
   route the cancel to. */
struct OrderCancel {
    OrderId orderId_;
    UserId userId_;
    Ticker ticker_;
};

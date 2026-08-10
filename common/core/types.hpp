#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include <boost/static_string.hpp>

#include "common/core/strong_type.hpp"


namespace detail {

struct PriceTag {};
struct VolumeTag {};
struct UserIdTag {};
struct OrderIdTag {};
struct UsdTag {};

}  // namespace detail

using Price = StrongType<std::int64_t, detail::PriceTag>;  // downscaled USD, see PRICE_MULTIPLIER
using Volume = StrongType<std::int64_t, detail::VolumeTag>;
using UserId = StrongType<std::int64_t, detail::UserIdTag>;
using OrderId = StrongType<std::int64_t, detail::OrderIdTag>;
using USD = StrongType<double, detail::UsdTag>;

inline constexpr std::int64_t PRICE_MULTIPLIER { 1'000'000 };

inline USD ToUsd(Price price) {
    return USD(static_cast<double>(price.value()) / static_cast<double>(PRICE_MULTIPLIER));
}

inline Price ToPrice(USD usd) {
    return Price(static_cast<std::int64_t>(std::llround(usd.value() * static_cast<double>(PRICE_MULTIPLIER))));
}

inline constexpr std::size_t MAX_TICKER_LENGTH { 15 };
using Ticker = boost::static_string<MAX_TICKER_LENGTH>;

/* The closed set of tradable equities, for now. Extend this list as more
   instruments come into scope. */
inline constexpr std::array<Ticker, 4> INSTRUMENTS {
    Ticker("AAPL"),
    Ticker("GOOG"),
    Ticker("MSFT"),
    Ticker("AMZN"),
};

enum class Side {
    Buy,
    Sell,
};

struct Order {
    UserId userId_;
    OrderId orderId_;
    Price price_;
    Volume volume_;
};

struct Trade {
    UserId makerId_;
    UserId takerId_;
    OrderId makerOrderId_;
    OrderId takerOrderId_;
    Ticker ticker_;
    Side takerSide_;  // the maker's side is always the opposite
    Price executionPrice_;
    Volume volume_;
};

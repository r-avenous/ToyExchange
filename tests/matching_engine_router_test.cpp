#include <cassert>
#include <iostream>
#include <vector>

#include "common/engine/matching_engine_router.hpp"


namespace {

OrderAdd MakeOrder(OrderId id, UserId user, const Ticker& ticker, Side side, Price price, Volume volume) {
    return OrderAdd{
        .orderId_ = id, .userId_ = user, .ticker_ = ticker,
        .side_ = side, .price_ = price, .volume_ = volume,
    };
}

std::vector<Trade> AddOrder(MatchingEngineRouter& router, const OrderAdd& order) {
    std::vector<Trade> trades;
    router.OnOrderAdd(order, [&](const Trade& trade) { trades.push_back(trade); });
    return trades;
}

void testOrdersForDifferentTickersNeverCross() {
    // Same price, same crossing volumes, but different tickers: these must
    // never trade against each other.
    MatchingEngineRouter router;
    AddOrder(router, MakeOrder(OrderId(1), UserId(1), "AAPL", Side::Sell, Price(100), Volume(10)));
    auto trades = AddOrder(router, MakeOrder(OrderId(2), UserId(2), "GOOG", Side::Buy, Price(100), Volume(10)));
    assert(trades.empty());
}

void testOrdersForTheSameTickerStillCross() {
    MatchingEngineRouter router;
    AddOrder(router, MakeOrder(OrderId(1), UserId(1), "AAPL", Side::Sell, Price(100), Volume(10)));
    auto trades = AddOrder(router, MakeOrder(OrderId(2), UserId(2), "AAPL", Side::Buy, Price(100), Volume(10)));
    assert(trades.size() == 1);
    assert(trades[0].ticker_ == Ticker("AAPL"));
}

void testMidPriceIsIndependentPerTicker() {
    MatchingEngineRouter router;
    assert(!router.MidPrice("AAPL").has_value());
    assert(!router.MidPrice("GOOG").has_value());

    AddOrder(router, MakeOrder(OrderId(1), UserId(1), "AAPL", Side::Buy, Price(100), Volume(10)));
    AddOrder(router, MakeOrder(OrderId(2), UserId(2), "GOOG", Side::Buy, Price(500), Volume(10)));

    assert(router.MidPrice("AAPL") == Price(100));
    assert(router.MidPrice("GOOG") == Price(500));
}

void testCancelRoutesToTheOrderOwnTicker() {
    MatchingEngineRouter router;
    AddOrder(router, MakeOrder(OrderId(1), UserId(1), "AAPL", Side::Sell, Price(100), Volume(10)));

    // A cancel claiming the wrong ticker for this order id must not remove
    // it from AAPL's book (it gets routed to GOOG's book instead, where
    // order id 1 doesn't exist).
    router.OnOrderCancel(OrderCancel{.orderId_ = OrderId(1), .userId_ = UserId(1), .ticker_ = "GOOG"});
    auto stillThere = AddOrder(router, MakeOrder(OrderId(2), UserId(2), "AAPL", Side::Buy, Price(100), Volume(10)));
    assert(stillThere.size() == 1);  // order 1 was never actually cancelled

    MatchingEngineRouter router2;
    AddOrder(router2, MakeOrder(OrderId(1), UserId(1), "AAPL", Side::Sell, Price(100), Volume(10)));
    router2.OnOrderCancel(OrderCancel{.orderId_ = OrderId(1), .userId_ = UserId(1), .ticker_ = "AAPL"});
    auto gone = AddOrder(router2, MakeOrder(OrderId(2), UserId(2), "AAPL", Side::Buy, Price(100), Volume(10)));
    assert(gone.empty());  // this time it really was cancelled
}

void testUnlistedTickerIsSilentlyIgnored() {
    MatchingEngineRouter router;
    auto trades = AddOrder(router, MakeOrder(OrderId(1), UserId(1), "ZZZZ", Side::Buy, Price(100), Volume(10)));
    assert(trades.empty());
    assert(!router.MidPrice("ZZZZ").has_value());
}

}  // namespace

int main()
{
    testOrdersForDifferentTickersNeverCross();
    testOrdersForTheSameTickerStillCross();
    testMidPriceIsIndependentPerTicker();
    testCancelRoutesToTheOrderOwnTicker();
    testUnlistedTickerIsSilentlyIgnored();

    std::cout << "all tests passed\n";
    return 0;
}

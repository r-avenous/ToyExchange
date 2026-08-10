#include <cassert>
#include <iostream>
#include <vector>

#include "common/engine/matching_engine.hpp"


namespace {

OrderAdd MakeOrder(OrderId id, UserId user, Side side, Price price, Volume volume) {
    return OrderAdd{
        .orderId_ = id, .userId_ = user, .ticker_ = "AAPL",
        .side_ = side, .price_ = price, .volume_ = volume,
    };
}

/* OnOrderAdd() calls its handler on the spot instead of returning a vector;
   tests still want the whole list, so collect it here. */
std::vector<Trade> AddOrder(MatchingEngine& engine, const OrderAdd& order) {
    std::vector<Trade> trades;
    engine.OnOrderAdd(order, [&](const Trade& trade) { trades.push_back(trade); });
    return trades;
}

void testRestsWhenNoCross() {
    MatchingEngine engine;
    auto trades = AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Buy, Price(100), Volume(10)));
    assert(trades.empty());
}

void testFullFill() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(100), Volume(10)));

    auto trades = AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Buy, Price(100), Volume(10)));
    assert(trades.size() == 1);
    assert(trades[0].makerId_ == UserId(1));
    assert(trades[0].takerId_ == UserId(2));
    assert(trades[0].executionPrice_ == Price(100));
    assert(trades[0].volume_ == Volume(10));
    assert(trades[0].takerSide_ == Side::Buy);
}

void testPartialFillRestsRemainder() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(101), Volume(10)));

    auto trades = AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Buy, Price(102), Volume(15)));
    assert(trades.size() == 1);
    assert(trades[0].volume_ == Volume(10));
    assert(trades[0].executionPrice_ == Price(101));  // executes at the resting (maker) price

    /* The remaining 5 shares of order 2 should now be resting as a bid @102,
       so a new sell @102 should fill against it. */
    auto moreTrades = AddOrder(engine, MakeOrder(OrderId(3), UserId(3), Side::Sell, Price(102), Volume(5)));
    assert(moreTrades.size() == 1);
    assert(moreTrades[0].makerId_ == UserId(2));
    assert(moreTrades[0].takerId_ == UserId(3));
    assert(moreTrades[0].volume_ == Volume(5));
}

void testCancelRemovesRestingOrder() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(100), Volume(10)));
    engine.OnOrderCancel(OrderCancel{.orderId_ = OrderId(1), .userId_ = UserId(1), .ticker_ = "AAPL"});

    auto trades = AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Buy, Price(100), Volume(10)));
    assert(trades.empty());  // nothing left resting to match against
}

void testCancelIgnoresWrongOwner() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(100), Volume(10)));
    engine.OnOrderCancel(OrderCancel{.orderId_ = OrderId(1), .userId_ = UserId(999), .ticker_ = "AAPL"});  // not the owner

    auto trades = AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Buy, Price(100), Volume(10)));
    assert(trades.size() == 1);  // order 1 was never actually cancelled
}

void testPriceTimePriority() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(100), Volume(5)));
    AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Sell, Price(100), Volume(5)));

    auto trades = AddOrder(engine, MakeOrder(OrderId(3), UserId(3), Side::Buy, Price(100), Volume(5)));
    assert(trades.size() == 1);
    assert(trades[0].makerId_ == UserId(1));  // order 1 was resting first (FIFO)
}

void testMidPriceEmptyUntilTwoSidedLiquidity() {
    MatchingEngine engine;
    assert(!engine.MidPrice().has_value());

    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Buy, Price(100), Volume(10)));
    assert(engine.MidPrice() == Price(100));  // one-sided: mid = that side's best price

    AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Sell, Price(104), Volume(10)));
    assert(engine.MidPrice() == Price(102));  // (100 + 104) / 2
}

void testMidPriceHoldsLastKnownValueWhenBookEmpties() {
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Buy, Price(100), Volume(10)));
    AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Sell, Price(100), Volume(10)));  // fully fills, book now empty
    assert(engine.MidPrice() == Price(100));  // stale-but-last-known, not reset
}

void testOnTradeCalledOnTheSpotForEachFill() {
    /* Two resting sells get consumed by one buy: the handler should be
       invoked twice, once per fill, in FIFO order, before OnOrderAdd returns. */
    MatchingEngine engine;
    AddOrder(engine, MakeOrder(OrderId(1), UserId(1), Side::Sell, Price(100), Volume(5)));
    AddOrder(engine, MakeOrder(OrderId(2), UserId(2), Side::Sell, Price(100), Volume(5)));

    std::vector<UserId> makersSeenInOrder;
    engine.OnOrderAdd(MakeOrder(OrderId(3), UserId(3), Side::Buy, Price(100), Volume(10)),
                       [&](const Trade& trade) { makersSeenInOrder.push_back(trade.makerId_); });

    assert(makersSeenInOrder.size() == 2);
    assert(makersSeenInOrder[0] == UserId(1));
    assert(makersSeenInOrder[1] == UserId(2));
}

}  // namespace

int main()
{
    testRestsWhenNoCross();
    testFullFill();
    testPartialFillRestsRemainder();
    testCancelRemovesRestingOrder();
    testCancelIgnoresWrongOwner();
    testPriceTimePriority();
    testMidPriceEmptyUntilTwoSidedLiquidity();
    testMidPriceHoldsLastKnownValueWhenBookEmpties();
    testOnTradeCalledOnTheSpotForEachFill();

    std::cout << "all tests passed\n";
    return 0;
}

#include <cassert>
#include <cmath>
#include <iostream>

#include "common/engine/user.hpp"


namespace {

bool ApproxEqual(double a, double b, double epsilon = 1e-6) {
    return std::fabs(a - b) < epsilon;
}

void testDefaultPositionIsZero() {
    User user(UserId(1));
    assert(user.Position("AAPL") == Volume(0));
    assert(ApproxEqual(user.RealizedPnl("AAPL").value(), 0.0));
    assert(ApproxEqual(user.UnrealizedPnl("AAPL").value(), 0.0));
}

void testApplyFillAccumulatesSameDirection() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(110)), Volume(10));
    assert(user.Position("AAPL") == Volume(20));
    // Average cost of 10@100 and 10@110 is 105.
    const auto it = user.Inventory().find(Ticker("AAPL"));
    assert(it != user.Inventory().end());
    assert(ApproxEqual(it->second.avgCost_.value(), 105.0));
}

void testApplyFillCanGoNegative() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    user.ApplyFill("AAPL", Side::Sell, ToPrice(USD(100)), Volume(15));
    assert(user.Position("AAPL") == Volume(-5));
}

void testPositionsAreIndependentPerTicker() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    user.ApplyFill("GOOG", Side::Buy, ToPrice(USD(140)), Volume(3));
    assert(user.Position("AAPL") == Volume(10));
    assert(user.Position("GOOG") == Volume(3));
}

void testRealizedPnlOnClosingALongPosition() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    user.ApplyFill("AAPL", Side::Sell, ToPrice(USD(110)), Volume(10));  // closes flat, +10/share
    assert(user.Position("AAPL") == Volume(0));
    assert(ApproxEqual(user.RealizedPnl("AAPL").value(), 100.0));
}

void testRealizedPnlOnClosingAShortPosition() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Sell, ToPrice(USD(100)), Volume(10));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(90)), Volume(10));  // covers flat, +10/share
    assert(user.Position("AAPL") == Volume(0));
    assert(ApproxEqual(user.RealizedPnl("AAPL").value(), 100.0));
}

void testRealizedPnlOnFlipThroughFlat() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    /* Sells 15 @110: closes the 10 long (+10/share = 100 realized) and opens
       a fresh 5-share short at 110. */
    user.ApplyFill("AAPL", Side::Sell, ToPrice(USD(110)), Volume(15));
    assert(user.Position("AAPL") == Volume(-5));
    assert(ApproxEqual(user.RealizedPnl("AAPL").value(), 100.0));

    const auto it = user.Inventory().find(Ticker("AAPL"));
    assert(it != user.Inventory().end());
    assert(ApproxEqual(it->second.avgCost_.value(), 110.0));
}

void testUnrealizedPnlUsesMidPrice() {
    User user(UserId(1));
    user.ApplyFill("AAPL", Side::Buy, ToPrice(USD(100)), Volume(10));
    user.RefreshUnrealizedPnl("AAPL", ToPrice(USD(107)));
    assert(ApproxEqual(user.UnrealizedPnl("AAPL").value(), 70.0));  // 10 * (107 - 100)

    user.RefreshUnrealizedPnl("AAPL", ToPrice(USD(95)));
    assert(ApproxEqual(user.UnrealizedPnl("AAPL").value(), -50.0));  // 10 * (95 - 100)
}

}  // namespace

int main()
{
    testDefaultPositionIsZero();
    testApplyFillAccumulatesSameDirection();
    testApplyFillCanGoNegative();
    testPositionsAreIndependentPerTicker();
    testRealizedPnlOnClosingALongPosition();
    testRealizedPnlOnClosingAShortPosition();
    testRealizedPnlOnFlipThroughFlat();
    testUnrealizedPnlUsesMidPrice();

    std::cout << "all tests passed\n";
    return 0;
}

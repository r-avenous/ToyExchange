#include <cassert>
#include <iostream>

#include "common/engine/instruments.hpp"
#include "common/net/queues.hpp"


namespace {

void testEquityPnl() {
    Instrument e("AAPL", ToPrice(USD(100)));
    assert(e.CalcPnl(ToPrice(USD(105))) == ToPrice(USD(5)));
}

void testOrderQueueRoundTrip() {
    OrderQueue orders;
    assert(orders.push(Order{.userId_ = UserId(1), .price_ = ToPrice(USD(100)), .volume_ = Volume(10)}));

    Order out;
    assert(orders.pop(out));
    assert(out.userId_ == UserId(1));
    assert(out.price_ == ToPrice(USD(100)));
    assert(out.volume_ == Volume(10));
}

}  // namespace

int main()
{
    testEquityPnl();
    testOrderQueueRoundTrip();

    std::cout << "all tests passed\n";
    return 0;
}

#pragma once

#include <iostream>

#include "common/core/types.hpp"


/* For now the only instrument type in play; options/futures support is
   dropped until it's actually needed again. */
class Instrument {
public:
    explicit Instrument(const Ticker& ticker, Price price)
        : price_(price), ticker_(ticker) {}

    friend std::ostream& operator<<(std::ostream& os, const Instrument& i) {
        return os << i.ticker_ << '\t' << i.price_;
    }

    Price CalcPnl(Price spot) const {
        return spot - price_;
    }

private:
    Price price_;
    Ticker ticker_;
};

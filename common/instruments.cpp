#include "instruments.hpp"


template<>
float Instrument<InstrumentType::Call>::calcPnl(float spot)
{
    return ((spot > mStrike) ? (spot - *mStrike) : 0) - mPrice;
}

template<>
float Instrument<InstrumentType::Put>::calcPnl(float spot)
{
    return ((spot < mStrike) ? (*mStrike - spot) : 0) - mPrice;
}

template<>
float Instrument<InstrumentType::Equity>::calcPnl(float spot)
{
    return spot - mPrice;
}

template<>
float Instrument<InstrumentType::Future>::calcPnl(float spot)
{
    return spot - mPrice;
}
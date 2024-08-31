#include "common/instruments.hpp"

int main()
{
    Instrument<InstrumentType::Call> c("abc", 5.6f, 100.f);
    Instrument<InstrumentType::Equity> e("abc", 100.f);
    std::cout << e.calcPnl(105) << '\n' << e << '\n';
    return 0;
}
#include <iostream>
#include <string>
#include <optional>
#include <string_view>


enum class InstrumentType
{
    Call,
    Put,
    Equity,
    Future,
};

inline std::string_view StringifyInstrumentType(InstrumentType t)
{
    if (t == InstrumentType::Call) return "C";
    else if (t == InstrumentType::Put) return "P";
    else if (t == InstrumentType::Equity) return "E";
    else return "F";
}

template<InstrumentType type>
class Instrument
{
    protected:
        const float mPrice;
        std::optional<float> mStrike {};
        const std::string mTicker;
        InstrumentType mType;
    public:
        explicit Instrument(const std::string& ticker, float price, std::optional<float> strike = {}) : mTicker(ticker), mPrice(price), mStrike(strike), mType(type)
        {
        }
        friend std::ostream& operator<<(std::ostream& os, const Instrument& i)
        {
            os << i.mTicker << '\t' << StringifyInstrumentType(type) << '\t' << i.mStrike.value_or(-1) << '\t' << i.mPrice;
            return os;
        }
        float calcPnl(float spot);
};

template<>
float Instrument<InstrumentType::Call>::calcPnl(float spot);

template<>
float Instrument<InstrumentType::Put>::calcPnl(float spot);

template<>
float Instrument<InstrumentType::Equity>::calcPnl(float spot);

template<>
float Instrument<InstrumentType::Future>::calcPnl(float spot);

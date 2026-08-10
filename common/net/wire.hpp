#pragma once

#include <cstdint>
#include <cstring>

#include "common/net/inbound_command.hpp"
#include "common/core/messages.hpp"
#include "common/core/types.hpp"


/* Fixed-layout, packed structs mirroring exactly what goes on the wire.
   Encoding/decoding is a straight reinterpret_cast over raw bytes - no
   serialization framework, no length prefixes. Every inbound TCP message is
   framed as exactly sizeof(InboundWireMessage) bytes; every outbound
   market-data message is exactly sizeof(WireMarketDataMessage) bytes, sent
   as one UDP datagram. */
#pragma pack(push, 1)

inline constexpr std::size_t WIRE_TICKER_SIZE = MAX_TICKER_LENGTH + 1;  // + NUL

// -- inbound (gateway <- client) -----------------------------------------

struct WireOrderAdd {
    std::int64_t orderId_;
    std::int64_t userId_;
    char ticker_[WIRE_TICKER_SIZE];
    std::uint8_t side_;  // 0 = Buy, 1 = Sell
    std::int64_t price_;
    std::int64_t volume_;
};

struct WireOrderCancel {
    std::int64_t orderId_;
    std::int64_t userId_;
    char ticker_[WIRE_TICKER_SIZE];
};

enum class InboundWireType : std::uint8_t {
    OrderAdd,
    OrderCancel,
};

struct InboundWireMessage {
    InboundWireType type_;
    union {
        WireOrderAdd orderAdd_;
        WireOrderCancel orderCancel_;
    };
};

// -- outbound (matching engine -> market data feed) -----------------------

struct WireOrderAddEvent {
    std::int64_t orderId_;
    std::int64_t userId_;
    char ticker_[WIRE_TICKER_SIZE];
    std::uint8_t side_;
    std::int64_t price_;
    std::int64_t volume_;
};

struct WireOrderCancelEvent {
    std::int64_t orderId_;
    std::int64_t userId_;
    char ticker_[WIRE_TICKER_SIZE];
};

struct WireTradeEvent {
    std::int64_t makerId_;
    std::int64_t takerId_;
    std::int64_t makerOrderId_;
    std::int64_t takerOrderId_;
    char ticker_[WIRE_TICKER_SIZE];
    std::uint8_t takerSide_;  // 0 = Buy, 1 = Sell; the maker's side is the opposite
    std::int64_t price_;
    std::int64_t volume_;
};

enum class MarketEventType : std::uint8_t {
    OrderAdd,
    OrderCancel,
    Trade,
};

struct WireMarketDataMessage {
    MarketEventType type_;
    union {
        WireOrderAddEvent orderAdd_;
        WireOrderCancelEvent orderCancel_;
        WireTradeEvent trade_;
    };
};

#pragma pack(pop)


// -- encode/decode helpers -------------------------------------------------

inline void EncodeTicker(const Ticker& ticker, char (&out)[WIRE_TICKER_SIZE]) {
    std::memset(out, 0, WIRE_TICKER_SIZE);
    std::memcpy(out, ticker.data(), ticker.size());
}

inline Ticker DecodeTicker(const char (&in)[WIRE_TICKER_SIZE]) {
    return Ticker(in, strnlen(in, WIRE_TICKER_SIZE));
}

inline OrderAdd DecodeOrderAdd(const WireOrderAdd& w) {
    return OrderAdd{
        .orderId_ = OrderId(w.orderId_),
        .userId_ = UserId(w.userId_),
        .ticker_ = DecodeTicker(w.ticker_),
        .side_ = (w.side_ == 0) ? Side::Buy : Side::Sell,
        .price_ = Price(w.price_),
        .volume_ = Volume(w.volume_),
    };
}

inline OrderCancel DecodeOrderCancel(const WireOrderCancel& w) {
    return OrderCancel{
        .orderId_ = OrderId(w.orderId_),
        .userId_ = UserId(w.userId_),
        .ticker_ = DecodeTicker(w.ticker_),
    };
}

/* Decodes a full framed inbound message into the in-process command type.
   Anything other than OrderAdd is treated as OrderCancel. */
inline InboundCommand DecodeInboundMessage(const InboundWireMessage& message) {
    if (message.type_ == InboundWireType::OrderAdd) {
        return DecodeOrderAdd(message.orderAdd_);
    }
    return DecodeOrderCancel(message.orderCancel_);
}

inline WireMarketDataMessage EncodeOrderAddEvent(const OrderAdd& order) {
    WireMarketDataMessage msg{};
    msg.type_ = MarketEventType::OrderAdd;
    msg.orderAdd_.orderId_ = order.orderId_.value();
    msg.orderAdd_.userId_ = order.userId_.value();
    EncodeTicker(order.ticker_, msg.orderAdd_.ticker_);
    msg.orderAdd_.side_ = (order.side_ == Side::Buy) ? 0 : 1;
    msg.orderAdd_.price_ = order.price_.value();
    msg.orderAdd_.volume_ = order.volume_.value();
    return msg;
}

inline WireMarketDataMessage EncodeOrderCancelEvent(const OrderCancel& cancel) {
    WireMarketDataMessage msg{};
    msg.type_ = MarketEventType::OrderCancel;
    msg.orderCancel_.orderId_ = cancel.orderId_.value();
    msg.orderCancel_.userId_ = cancel.userId_.value();
    EncodeTicker(cancel.ticker_, msg.orderCancel_.ticker_);
    return msg;
}

inline WireMarketDataMessage EncodeTradeEvent(const Trade& trade) {
    WireMarketDataMessage msg{};
    msg.type_ = MarketEventType::Trade;
    msg.trade_.makerId_ = trade.makerId_.value();
    msg.trade_.takerId_ = trade.takerId_.value();
    msg.trade_.makerOrderId_ = trade.makerOrderId_.value();
    msg.trade_.takerOrderId_ = trade.takerOrderId_.value();
    EncodeTicker(trade.ticker_, msg.trade_.ticker_);
    msg.trade_.takerSide_ = (trade.takerSide_ == Side::Buy) ? 0 : 1;
    msg.trade_.price_ = trade.executionPrice_.value();
    msg.trade_.volume_ = trade.volume_.value();
    return msg;
}

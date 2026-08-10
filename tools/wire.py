"""Wire-format encode/decode helpers mirroring common/net/wire.hpp.

Every payload here is a straight struct.pack/unpack mirror of the
#pragma pack(1) C++ structs defined there - no serialization framework on
either side. Keep this in sync if wire.hpp changes.
"""

import struct


# -- shared constants, mirrors common/core/types.hpp ------------------------

PRICE_MULTIPLIER = 1_000_000
MAX_TICKER_LENGTH = 15
WIRE_TICKER_SIZE = MAX_TICKER_LENGTH + 1  # + NUL
INSTRUMENTS = ["AAPL", "GOOG", "MSFT", "AMZN"]  # mirrors INSTRUMENTS in types.hpp

SIDE_BUY = 0
SIDE_SELL = 1


def format_price(price: int) -> str:
    return f"{price / PRICE_MULTIPLIER:.2f}"


def _encode_ticker(ticker: str) -> bytes:
    raw = ticker.encode("ascii")
    if len(raw) >= WIRE_TICKER_SIZE:
        raise ValueError(f"ticker {ticker!r} too long for the wire format")
    return raw.ljust(WIRE_TICKER_SIZE, b"\0")


def _decode_ticker(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii")


# -- inbound (client -> gateway), mirrors InboundWireMessage ----------------

_INBOUND_TYPE_ADD = 0
_INBOUND_TYPE_CANCEL = 1

# WireOrderAdd: int64 orderId_, userId_; char ticker_[16]; uint8 side_; int64 price_, volume_.
_ORDER_ADD_FMT = "<qq16sBqq"
_ORDER_ADD_SIZE = struct.calcsize(_ORDER_ADD_FMT)  # 49

# WireOrderCancel: int64 orderId_, userId_; char ticker_[16].
_ORDER_CANCEL_FMT = "<qq16s"
_ORDER_CANCEL_SIZE = struct.calcsize(_ORDER_CANCEL_FMT)  # 32

# InboundWireMessage's union is sized to its largest member (WireOrderAdd).
_INBOUND_UNION_SIZE = _ORDER_ADD_SIZE
INBOUND_MESSAGE_SIZE = 1 + _INBOUND_UNION_SIZE  # 50


def encode_order_add(order_id: int, user_id: int, ticker: str, side: int, price: int, volume: int) -> bytes:
    payload = struct.pack(_ORDER_ADD_FMT, order_id, user_id, _encode_ticker(ticker), side, price, volume)
    message = struct.pack("<B", _INBOUND_TYPE_ADD) + payload.ljust(_INBOUND_UNION_SIZE, b"\0")
    assert len(message) == INBOUND_MESSAGE_SIZE
    return message


def encode_order_cancel(order_id: int, user_id: int, ticker: str) -> bytes:
    payload = struct.pack(_ORDER_CANCEL_FMT, order_id, user_id, _encode_ticker(ticker))
    message = struct.pack("<B", _INBOUND_TYPE_CANCEL) + payload.ljust(_INBOUND_UNION_SIZE, b"\0")
    assert len(message) == INBOUND_MESSAGE_SIZE
    return message


# -- outbound (matching engine -> market data feed), mirrors WireMarketDataMessage --

MARKET_EVENT_ORDER_ADD = 0
MARKET_EVENT_ORDER_CANCEL = 1
MARKET_EVENT_TRADE = 2

# WireOrderAddEvent / WireOrderCancelEvent have the same layout as their
# inbound counterparts above.
_ORDER_ADD_EVENT_FMT, _ORDER_ADD_EVENT_SIZE = _ORDER_ADD_FMT, _ORDER_ADD_SIZE
_ORDER_CANCEL_EVENT_FMT, _ORDER_CANCEL_EVENT_SIZE = _ORDER_CANCEL_FMT, _ORDER_CANCEL_SIZE

# WireTradeEvent: int64 makerId_, takerId_, makerOrderId_, takerOrderId_;
# char ticker_[16]; uint8 takerSide_; int64 price_, volume_.
_TRADE_EVENT_FMT = "<qqqq16sBqq"
_TRADE_EVENT_SIZE = struct.calcsize(_TRADE_EVENT_FMT)  # 65

_MARKET_DATA_UNION_SIZE = max(_ORDER_ADD_EVENT_SIZE, _ORDER_CANCEL_EVENT_SIZE, _TRADE_EVENT_SIZE)
MARKET_DATA_MESSAGE_SIZE = 1 + _MARKET_DATA_UNION_SIZE  # 66


def decode_market_data(data: bytes) -> dict:
    if len(data) != MARKET_DATA_MESSAGE_SIZE:
        raise ValueError(f"expected {MARKET_DATA_MESSAGE_SIZE}-byte market data message, got {len(data)}")

    event_type = data[0]
    payload = data[1:]

    if event_type == MARKET_EVENT_ORDER_ADD:
        order_id, user_id, ticker, side, price, volume = struct.unpack(
            _ORDER_ADD_EVENT_FMT, payload[:_ORDER_ADD_EVENT_SIZE])
        return {
            "type": "OrderAdd",
            "orderId": order_id,
            "userId": user_id,
            "ticker": _decode_ticker(ticker),
            "side": "Buy" if side == SIDE_BUY else "Sell",
            "price": price,
            "volume": volume,
        }

    if event_type == MARKET_EVENT_ORDER_CANCEL:
        order_id, user_id, ticker = struct.unpack(_ORDER_CANCEL_EVENT_FMT, payload[:_ORDER_CANCEL_EVENT_SIZE])
        return {
            "type": "OrderCancel",
            "orderId": order_id,
            "userId": user_id,
            "ticker": _decode_ticker(ticker),
        }

    if event_type == MARKET_EVENT_TRADE:
        maker_id, taker_id, maker_order_id, taker_order_id, ticker, taker_side, price, volume = struct.unpack(
            _TRADE_EVENT_FMT, payload[:_TRADE_EVENT_SIZE])
        return {
            "type": "Trade",
            "makerId": maker_id,
            "takerId": taker_id,
            "makerOrderId": maker_order_id,
            "takerOrderId": taker_order_id,
            "ticker": _decode_ticker(ticker),
            "takerSide": "Buy" if taker_side == SIDE_BUY else "Sell",
            "price": price,
            "volume": volume,
        }

    return {"type": "Unknown", "raw": data.hex()}

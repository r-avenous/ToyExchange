#!/usr/bin/env python3
"""Joins the ToyExchange market data multicast feed and reconstructs +
neatly logs a live order book for every listed instrument, purely from the
public event stream (OrderAdd / OrderCancel / Trade) - the exchange doesn't
expose a separate orderbook snapshot/query API, so this is how any real
market data consumer would build one.

Note: a listener that joins mid-session only sees orders/cancels from that
point on, same as any real multicast feed with no snapshot service.

Usage:
    python3 tools/orderbook_listener.py [--group ADDR] [--port PORT] [--snapshot-interval SECONDS]
"""

import argparse
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wire  # noqa: E402


class OrderBook:
    """Local reconstruction of every resting order, built purely from the
    market data feed. Mirrors what the exchange does internally:
      - OrderAdd: the whole incoming order starts out resting locally.
      - Trade: shrinks both the maker's and taker's resting volume.
      - OrderCancel: removes a resting order outright.
    """

    def __init__(self):
        self.open_orders = {}  # orderId -> dict(ticker, side, price, volume)

    def apply(self, event: dict):
        kind = event["type"]
        if kind == "OrderAdd":
            self.open_orders[event["orderId"]] = {
                "ticker": event["ticker"],
                "side": event["side"],
                "price": event["price"],
                "volume": event["volume"],
            }
        elif kind == "OrderCancel":
            self.open_orders.pop(event["orderId"], None)
        elif kind == "Trade":
            for order_id in (event["makerOrderId"], event["takerOrderId"]):
                order = self.open_orders.get(order_id)
                if order is None:
                    continue
                order["volume"] -= event["volume"]
                if order["volume"] <= 0:
                    self.open_orders.pop(order_id, None)

    def levels(self, ticker: str):
        bids, asks = {}, {}
        for order in self.open_orders.values():
            if order["ticker"] != ticker:
                continue
            book = bids if order["side"] == "Buy" else asks
            book[order["price"]] = book.get(order["price"], 0) + order["volume"]
        return bids, asks

    def print_snapshot(self):
        print("=" * 40)
        print(time.strftime("orderbook @ %H:%M:%S"))
        for ticker in wire.INSTRUMENTS:
            bids, asks = self.levels(ticker)
            print(f"-- {ticker} " + "-" * (37 - len(ticker)))
            if not bids and not asks:
                print("   (empty)")
                continue
            for price in sorted(asks, reverse=True):
                print(f"        ask  {wire.format_price(price):>10}  x{asks[price]}")
            if bids and asks:
                print("   " + "-" * 30)
            for price in sorted(bids, reverse=True):
                print(f"        bid  {wire.format_price(price):>10}  x{bids[price]}")
        print("=" * 40)


def log_event(event: dict):
    ts = time.strftime("%H:%M:%S")
    kind = event["type"]
    if kind == "OrderAdd":
        print(f"[{ts}] {event['ticker']:<5} OrderAdd    id={event['orderId']:<6} user={event['userId']:<4} "
              f"{event['side']:<4} {wire.format_price(event['price']):>10} x{event['volume']}")
    elif kind == "OrderCancel":
        print(f"[{ts}] {event['ticker']:<5} OrderCancel id={event['orderId']:<6} user={event['userId']}")
    elif kind == "Trade":
        print(f"[{ts}] {event['ticker']:<5} Trade       maker={event['makerId']:<4} taker={event['takerId']:<4} "
              f"taker{event['takerSide']:<5} {wire.format_price(event['price']):>10} x{event['volume']}")
    else:
        print(f"[{ts}] unrecognized event: {event}")


def join_multicast(group: str, port: int, interface: str = "127.0.0.1") -> socket.socket:
    # Every participant in this toy exchange is local (the gateway only
    # listens on 127.0.0.1) and MarketDataPublisher sends explicitly via
    # loopback, so join on loopback too rather than INADDR_ANY - multicast
    # loopback through a physical interface is unreliable on plenty of real
    # networks/drivers even for host-to-itself delivery.
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))
    mreq = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton(interface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    return sock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", default="239.1.1.1")
    parser.add_argument("--port", type=int, default=30001)
    parser.add_argument("--snapshot-interval", type=float, default=2.0,
                         help="seconds between full orderbook snapshots (0 disables periodic snapshots)")
    args = parser.parse_args()

    sock = join_multicast(args.group, args.port)
    sock.settimeout(0.5)
    print(f"listening for market data on {args.group}:{args.port} (ctrl-c to stop)")

    book = OrderBook()
    last_snapshot = time.monotonic()

    try:
        while True:
            try:
                data, _addr = sock.recvfrom(4096)
            except socket.timeout:
                data = None

            if data is not None:
                try:
                    event = wire.decode_market_data(data)
                except ValueError as exc:
                    print(f"! {exc}")
                    data = None
                else:
                    book.apply(event)
                    log_event(event)

            now = time.monotonic()
            if args.snapshot_interval > 0 and now - last_snapshot >= args.snapshot_interval:
                book.print_snapshot()
                last_snapshot = now
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

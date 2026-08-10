#!/usr/bin/env python3
"""Connects to the ToyExchange gateway and sends a stream of dummy OrderAdd /
OrderCancel messages for all listed instruments, all from a single fake
participant (UserId 0).

Usage:
    python3 tools/dummy_order_generator.py [--host HOST] [--port PORT] [--rate N] [--duration SECONDS]
"""

import argparse
import os
import random
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wire  # noqa: E402


USER_ID = 0

# A rough per-ticker reference price (USD) so generated orders cluster
# somewhere plausible instead of being pure noise.
REFERENCE_PRICES = {
    "AAPL": 190.0,
    "GOOG": 140.0,
    "MSFT": 410.0,
    "AMZN": 175.0,
}


def random_order_add(order_id: int):
    ticker = random.choice(wire.INSTRUMENTS)
    side = random.choice([wire.SIDE_BUY, wire.SIDE_SELL])
    reference = REFERENCE_PRICES[ticker]
    price = round((reference + random.uniform(-2.0, 2.0)) * wire.PRICE_MULTIPLIER)
    volume = random.randint(1, 100)
    message = wire.encode_order_add(order_id, USER_ID, ticker, side, price, volume)
    return message, ticker, side, price, volume


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--rate", type=float, default=2.0, help="messages per second")
    parser.add_argument("--cancel-probability", type=float, default=0.3,
                         help="chance to cancel an open order instead of adding a new one")
    parser.add_argument("--duration", type=float, default=None, help="seconds to run (default: forever)")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port))
    print(f"connected to {args.host}:{args.port} as userId={USER_ID} (ctrl-c to stop)")

    open_orders = {}  # orderId -> ticker, so cancels can supply the right one
    next_order_id = 1
    start = time.monotonic()
    interval = 1.0 / args.rate

    try:
        while args.duration is None or (time.monotonic() - start) < args.duration:
            if open_orders and random.random() < args.cancel_probability:
                order_id = random.choice(list(open_orders))
                ticker = open_orders.pop(order_id)
                sock.sendall(wire.encode_order_cancel(order_id, USER_ID, ticker))
                print(f"sent OrderCancel id={order_id} {ticker}")
            else:
                message, ticker, side, price, volume = random_order_add(next_order_id)
                sock.sendall(message)
                open_orders[next_order_id] = ticker
                side_name = "Buy" if side == wire.SIDE_BUY else "Sell"
                print(f"sent OrderAdd    id={next_order_id} {ticker:<5} {side_name:<4} "
                      f"{wire.format_price(price):>10} x{volume}")
                next_order_id += 1

            time.sleep(interval)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()

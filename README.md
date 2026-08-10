# ToyExchange

A toy exchange in C++23, built with CMake: a TCP gateway, one matching
engine per instrument, and a UDP-multicast market data feed, each on its
own thread, connected by lock-free SPSC queues.

## Layout

- `common/core/` — the basic data model, no dependency on engine or net:
  - `strong_type.hpp` — the `StrongType<T, Tag>` wrapper used for `Price`, `Volume`, `UserId`, `OrderId`, `USD`.
  - `seqlock.hpp` — `Seqlock<T>`, a single-writer/multi-reader lock-free snapshot primitive.
  - `types.hpp` — core types (`Price`, `Volume`, `UserId`, `OrderId`, `USD`, `Ticker`, `INSTRUMENTS`, `Side`, `Order`, `Trade`).
  - `messages.hpp` — inbound request types (`OrderAdd`, `OrderCancel` — both carry a `Ticker`).
- `common/engine/` — the exchange domain logic:
  - `instruments.hpp` — the equity PnL model.
  - `matching_engine.hpp`/`.cpp` — price-time-priority `MatchingEngine` for a single instrument; publishes a live mid price through a `Seqlock`.
  - `matching_engine_router.hpp` — `MatchingEngineRouter`: one `MatchingEngine` per listed ticker, so books never cross between instruments.
  - `user.hpp` — `User`: an identity plus a per-`Ticker` `Holding` (position, average cost, realized/unrealized P&L).
  - `user_registry.hpp` — `UserRegistry`: the fixed set of market participants (`UserId` 0–99), plus `WriteSnapshot()`.
- `common/net/` — the wire format and I/O boundary:
  - `wire.hpp` — packed, `reinterpret_cast`-able structs for inbound orders and outbound market-data events, plus encode/decode helpers.
  - `inbound_command.hpp` — `InboundCommand`, the in-process `OrderAdd`/`OrderCancel` variant.
  - `queues.hpp` — the SPSC queue type aliases connecting the threads below, including a standalone `OrderQueue` demo (not part of the live pipeline).
  - `gateway.hpp`/`.cpp` — `Gateway`: the epoll-based TCP server accepting participant connections.
  - `market_data_publisher.hpp`/`.cpp` — `MarketDataPublisher<Local = true>`: sends wire-encoded events to a UDP multicast group.
- `common/workers/` — thread entry points wiring engine objects to queues:
  - `matching_engine_thread.hpp`/`.cpp` — drains inbound commands, runs the engine, publishes events/trades.
  - `inventory_thread.hpp`/`.cpp` — drains trades and settles them into `UserRegistry`.
- `exchange/` — the `exchange` executable: wires up the queues and spawns the gateway, matching engine, inventory, and market data threads.
- `tests/` — unit tests, run via CTest.
- `tools/` — standalone Python clients speaking the same wire format (see below).

## Dependencies

- CMake >= 3.20
- A C++23 compiler, Linux (uses epoll and POSIX sockets directly)
- Boost >= 1.81 (header-only: `Boost.LockFree`)
- [`ankerl::unordered_dense`](https://github.com/martinus/unordered_dense) (fetched via `FetchContent`, pinned to v4.8.1)
- [`spdlog`](https://github.com/gabime/spdlog) (fetched via `FetchContent`, pinned to v1.15.1)

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

## Run

```sh
./build/exchange/exchange
```

Listens for participant TCP connections on port 9000 and publishes market
data to UDP multicast group `239.1.1.1:30001`. Ctrl-C (or SIGTERM) triggers
an ordered shutdown: the gateway stops accepting and closes every open TCP
connection, the matching engine finishes executing whatever was already
queued, the inventory thread finishes settling every resulting trade, and
finally the resulting per-user positions/P&L are written to
`inventory_snapshot.csv`.

Logs to stdout at `info` by default (connections, trades, startup/shutdown
phases). Set `EXCHANGE_LOG_LEVEL=debug` to additionally see every
order/cancel/dispatch/settlement - the full per-message pipeline trace:

```sh
EXCHANGE_LOG_LEVEL=debug ./build/exchange/exchange
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Python tools

Pure-stdlib clients that speak the exact same binary wire format as the
exchange (`tools/wire.py` mirrors `common/net/wire.hpp` - keep them in sync
if that changes). With `exchange` running:

```sh
# Sends a stream of random OrderAdd/OrderCancel over TCP, all as UserId 0.
python3 tools/dummy_order_generator.py --rate 5

# Joins the market data multicast feed, reconstructs an orderbook per
# instrument purely from the public event stream, and logs it neatly.
python3 tools/orderbook_listener.py
```

## Notable design choices

- **`StrongType<T, Tag>`** (`common/core/strong_type.hpp`) gives `Price`,
  `Volume`, `UserId`, `OrderId`, and `USD` distinct types over their raw
  representation, so they can't be silently swapped for one another at a
  call site.
- **`Ticker`** (`common/core/types.hpp`) is a fixed-capacity
  `boost::static_string`, not a heap-allocated `std::string`; `INSTRUMENTS`
  is the closed, const list of tradable equities.
- **`MatchingEngine`** (`common/engine/matching_engine.hpp`) keeps bids/asks
  in `std::flat_map<Price, ...>` (contiguous, cache-friendly) and uses
  `ankerl::unordered_dense::map` for order-id lookups (cancel-by-id). Every
  resting-order/price-level container (`bids_`, `asks_`, `orderIndex_`) is
  PMR-backed by a per-instrument 4 MiB arena (`monotonic_buffer_resource` +
  `unsynchronized_pool_resource`), preallocated once at construction, so the
  hot matching path is allocation-free in steady state; it transparently
  falls back to the heap if an instrument's arena is ever exhausted. The
  arena is heap-allocated via `unique_ptr`, not an inline buffer, because
  `MatchingEngineRouter` holds every `MatchingEngine` inline in a
  `std::array` - an inline multi-MiB member per instrument would land
  straight on the caller's stack. Only equity instruments are modeled for
  now. `MatchingEngineRouter` holds one
  `MatchingEngine` per entry in `INSTRUMENTS` (a fixed `std::array`, so no
  reallocation ever moves a `MatchingEngine`) and dispatches every
  `OnOrderAdd`/`OnOrderCancel`/`MidPrice` call by `Ticker` - orders for
  different instruments can never cross, and mid price/P&L are tracked
  independently per ticker.
- **Pipeline** (`exchange/main.cpp`): `Gateway` (epoll, one thread) decodes
  binary orders off TCP connections FIFO per-connection and pushes
  `InboundCommand`s onto a `boost::lockfree::spsc_queue`. The matching engine
  thread drains that queue, applies each command, and fans out: every event
  (add/cancel/trade) goes wire-encoded to the market data queue, and every
  trade additionally goes to the inventory queue. The market data thread
  sends each event as one UDP multicast datagram; the inventory thread
  settles trades into `UserRegistry` (positions may go negative - no risk
  checks, by design).
- **Wire encoding** (`common/net/wire.hpp`): all TCP/UDP payloads are fixed-size,
  `#pragma pack(1)` structs, read/written via `reinterpret_cast` directly
  against the socket buffer - no serialization framework. Inbound TCP
  messages are framed as one fixed-size struct per message; outbound
  market-data messages are one UDP datagram per message.
- **Mid price & P&L**: `MatchingEngine` recomputes `(best bid + best ask) / 2`
  after every mutation and publishes it through a `Seqlock<std::optional<Price>>`
  - the inventory thread reads it lock-free while the matching engine thread
  keeps writing concurrently. `User` tracks each `Holding`'s average cost
  (long positive, short negative); every fill realizes P&L on whatever
  portion closes an existing opposite-direction position, and
  `RefreshUnrealizedPnl()` marks the remaining open position to the current
  mid price. Both are refreshed whenever a user trades, not continuously -
  a known toy-scope simplification.
- **Ordered shutdown**: each worker thread only stops once its upstream
  producer thread has been joined *and* its own queue is empty, so a
  shutdown never drops in-flight work (see `exchange/main.cpp`).
- **`MarketDataPublisher<Local>`**: by default (`Local = true`) forces
  multicast sends out via loopback (`IP_MULTICAST_IF` = 127.0.0.1) instead
  of letting the kernel pick the default-route interface. Every participant
  in this toy exchange is local anyway, and multicast loopback through a
  real NIC is unreliable on plenty of networks/drivers even for
  host-to-itself delivery - `lo` doesn't have that problem. Instantiate as
  `MarketDataPublisher<false>` to get the old behavior (OS picks the
  interface), e.g. if the group is meant to actually reach other machines.
- **Logging**: one process-wide async `spdlog` logger (`exchange/main.cpp`'s
  `InitLogging()`), backed by a thread pool sized to exactly one background
  thread - every `spdlog::info`/`spdlog::debug`/... call from any of the
  four worker threads just formats and enqueues, never blocking on I/O, and
  they're all interleaved with consistent timestamps in one output stream.
  Notable events (connections, trades, startup/shutdown phases) are logged
  at `info`; the trivial, high-volume per-order/per-cancel/per-dispatch
  events are `debug`, filtered out by default. Never logged from inside the
  `SIGINT`/`SIGTERM` handler itself - `spdlog` isn't async-signal-safe, so
  the handler only flips an atomic and the first log call happens back on
  the main thread's poll loop. `spdlog::shutdown()` at the end of `main()`
  flushes and joins the logger thread before exit.

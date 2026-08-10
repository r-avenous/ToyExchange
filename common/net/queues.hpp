#pragma once

#include <boost/lockfree/spsc_queue.hpp>

#include "common/net/inbound_command.hpp"
#include "common/core/types.hpp"
#include "common/net/wire.hpp"


inline constexpr std::size_t QUEUE_CAPACITY = 4096;

// Gateway thread (producer) -> MatchingEngine thread (consumer).
using InboundQueue = boost::lockfree::spsc_queue<InboundCommand, boost::lockfree::capacity<QUEUE_CAPACITY>>;

/* MatchingEngine thread (producer) -> MarketData thread (consumer). Already
   wire-encoded, so the market data thread can send it straight to the socket. */
using MarketDataQueue = boost::lockfree::spsc_queue<WireMarketDataMessage, boost::lockfree::capacity<QUEUE_CAPACITY>>;

// MatchingEngine thread (producer) -> Inventory thread (consumer).
using TradeQueue = boost::lockfree::spsc_queue<Trade, boost::lockfree::capacity<QUEUE_CAPACITY>>;

/* Standalone demo queue (not part of the live exchange pipeline, which uses
   InboundQueue instead): a single-producer/single-consumer, lock-free,
   bounded ring buffer for handing Order values between two threads. */
using OrderQueue = boost::lockfree::spsc_queue<Order, boost::lockfree::capacity<QUEUE_CAPACITY>>;

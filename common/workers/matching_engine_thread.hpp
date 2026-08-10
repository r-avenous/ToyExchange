#pragma once

#include <atomic>

#include "common/engine/matching_engine_router.hpp"
#include "common/net/queues.hpp"


/* Drains `inbound`, applies each command to `router` (one book per ticker),
   and publishes the resulting market-data events (already wire-encoded) to
   `marketData`. Every resulting trade is additionally pushed to `trades`
   for the inventory thread to settle.

   Runs until `gatewayFinished` is observed true AND `inbound` is empty, i.e.
   it keeps fully draining whatever the gateway already enqueued even after
   shutdown begins - "remaining orders should be executed". Set
   `gatewayFinished` only after the gateway thread has been joined, so no
   more pushes can race the empty check. */
void RunMatchingEngineThread(MatchingEngineRouter& router, InboundQueue& inbound,
                              MarketDataQueue& marketData, TradeQueue& trades,
                              std::atomic<bool>& gatewayFinished);

#pragma once

#include <atomic>

#include "common/engine/matching_engine_router.hpp"
#include "common/engine/user_registry.hpp"
#include "common/net/queues.hpp"


/* Drains `trades` and applies each one to the maker's and taker's holding in
   `registry` (no risk checks - a position may go negative), refreshing
   unrealized P&L from `router`'s current mid price for that trade's ticker
   along the way.

   Runs until `matchingEngineFinished` is observed true AND `trades` is
   empty, i.e. it keeps fully draining and settling every trade already
   produced even after shutdown begins - "the auxiliary thread ... should
   finish all updates". Set `matchingEngineFinished` only after the matching
   engine thread has been joined, so no more pushes can race the empty
   check. */
void RunInventoryThread(TradeQueue& trades, UserRegistry& registry, const MatchingEngineRouter& router,
                         std::atomic<bool>& matchingEngineFinished);

#pragma once

#include <variant>

#include "common/core/messages.hpp"


/* A single element for the Gateway -> MatchingEngine SPSC queue: either an
   OrderAdd or an OrderCancel. This is an in-process type, not a wire
   format - decoding off the wire produces one of these directly. */
using InboundCommand = std::variant<OrderAdd, OrderCancel>;

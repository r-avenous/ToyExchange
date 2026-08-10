add_library(common STATIC
    core/messages.hpp
    core/seqlock.hpp
    core/strong_type.hpp
    core/types.hpp
    engine/instruments.hpp
    engine/matching_engine.cpp
    engine/matching_engine.hpp
    engine/matching_engine_router.hpp
    engine/user.hpp
    engine/user_registry.hpp
    net/gateway.cpp
    net/gateway.hpp
    net/inbound_command.hpp
    net/market_data_publisher.cpp
    net/market_data_publisher.hpp
    net/queues.hpp
    net/wire.hpp
    workers/inventory_thread.cpp
    workers/inventory_thread.hpp
    workers/matching_engine_thread.cpp
    workers/matching_engine_thread.hpp
)

# Root-relative includes, e.g. #include "common/engine/instruments.hpp", so
# any subdirectory can pull this library's headers unambiguously.
target_include_directories(common PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(common PUBLIC Boost::headers unordered_dense::unordered_dense Threads::Threads spdlog::spdlog)

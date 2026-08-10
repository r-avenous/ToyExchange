#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <memory>
#include <thread>

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "common/net/gateway.hpp"
#include "common/workers/inventory_thread.hpp"
#include "common/net/market_data_publisher.hpp"
#include "common/engine/matching_engine_router.hpp"
#include "common/workers/matching_engine_thread.hpp"
#include "common/net/queues.hpp"
#include "common/engine/user_registry.hpp"


namespace {

// Set by the signal handler; tells the gateway to stop accepting/reading.
std::atomic<bool> gShutdownRequested{false};

void HandleShutdownSignal(int /*signal*/) {
    gShutdownRequested.store(true, std::memory_order_relaxed);
}

constexpr std::uint16_t GATEWAY_PORT = 9000;
constexpr const char* MARKET_DATA_MULTICAST_ADDRESS = "239.1.1.1";
constexpr std::uint16_t MARKET_DATA_PORT = 30001;
constexpr const char* INVENTORY_SNAPSHOT_PATH = "inventory_snapshot.csv";

/* Sets up the process-wide async logger: one background thread (the thread
   pool below is sized to exactly one) drains a queue and writes to stdout,
   so no caller anywhere in the process ever blocks on log I/O. `block`
   overflow policy matches this project's everywhere-else philosophy of
   never dropping work under load, favoring backpressure instead.
   EXCHANGE_LOG_LEVEL (trace/debug/info/warn/err/critical/off) overrides the
   default of info - set it to debug to also see the trivial, high-volume
   per-order/per-trade events. */
void InitLogging() {
    spdlog::init_thread_pool(8192, 1);
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::async_logger>(
        "exchange", sink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    spdlog::level::level_enum level = spdlog::level::info;
    if (const char* envLevel = std::getenv("EXCHANGE_LOG_LEVEL")) {
        level = spdlog::level::from_str(envLevel);
    }
    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
}

}  // namespace

int main()
{
    InitLogging();

    std::signal(SIGINT, HandleShutdownSignal);
    std::signal(SIGTERM, HandleShutdownSignal);

    // Gateway thread -> matching engine thread.
    InboundQueue inboundQueue;
    // Matching engine thread -> market data thread (already wire-encoded).
    MarketDataQueue marketDataQueue;
    // Matching engine thread -> inventory thread.
    TradeQueue tradeQueue;

    MatchingEngineRouter engine;
    UserRegistry registry;

    Gateway gateway(GATEWAY_PORT, inboundQueue);
    MarketDataPublisher<> publisher(MARKET_DATA_MULTICAST_ADDRESS, MARKET_DATA_PORT);  // Local = true (default)

    /* Each stage's worker keeps draining its queue until the stage feeding
       it is confirmed finished (see the *Finished flags below), so a
       shutdown never drops in-flight work: queued orders still get matched,
       resulting trades still get settled, market data for all of it still
       gets sent. */
    std::atomic<bool> gatewayFinished{false};
    std::atomic<bool> matchingEngineFinished{false};

    std::thread gatewayThread([&] { gateway.Run(); });
    std::thread matchingThread([&] {
        RunMatchingEngineThread(engine, inboundQueue, marketDataQueue, tradeQueue, gatewayFinished);
    });
    std::thread inventoryThread([&] {
        RunInventoryThread(tradeQueue, registry, engine, matchingEngineFinished);
    });
    std::thread marketDataThread([&] { publisher.Run(marketDataQueue, matchingEngineFinished); });

    spdlog::info("[main] exchange listening on tcp:{}, publishing market data to {}:{} (ctrl-c to stop)",
                 GATEWAY_PORT, MARKET_DATA_MULTICAST_ADDRESS, MARKET_DATA_PORT);

    while (!gShutdownRequested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Never log from inside the signal handler itself (not async-signal-safe);
    // this is the first safe place to report the shutdown request.
    spdlog::info("[main] shutdown requested, draining in order");

    /* Ordered shutdown: close off each stage's input before letting the
       next stage decide it's allowed to stop. */
    gateway.Stop();
    gatewayThread.join();                                        // TCP connections are now closed
    gatewayFinished.store(true, std::memory_order_release);

    matchingThread.join();                                       // remaining queued orders are now executed
    matchingEngineFinished.store(true, std::memory_order_release);

    inventoryThread.join();                                      // all account updates are now applied
    marketDataThread.join();

    registry.WriteSnapshot(INVENTORY_SNAPSHOT_PATH);
    spdlog::info("[main] wrote final inventory to {}", INVENTORY_SNAPSHOT_PATH);

    spdlog::shutdown();  // flushes and joins the logger's background thread
    return 0;
}

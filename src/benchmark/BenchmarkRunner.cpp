#include "benchmark/BenchmarkRunner.hpp"
#include "utils/MemoryPool.hpp"
#include "utils/SpscRingBuffer.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <numeric>
#include <thread>
#include <sys/resource.h>

namespace astra {

BenchmarkRunner::BenchmarkRunner(BenchmarkConfig cfg)
    : cfg_(std::move(cfg))
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

BenchmarkResult BenchmarkRunner::run() {
    GeneratorConfig genCfg;
    genCfg.count   = cfg_.orderCount;
    genCfg.symbols = cfg_.symbols;
    genCfg.seed    = cfg_.seed;

    OrderGenerator gen(genCfg);
    auto orders = gen.generate();

    BenchmarkResult result;
    switch (cfg_.mode) {
        case BenchmarkMode::Pool:     result = runPool(orders);     break;
        case BenchmarkMode::Pipeline: result = runPipeline(orders); break;
        default:                      result = runBaseline(orders); break;
    }

    result.mode = cfg_.mode;

    if (!cfg_.csvOutput.empty()) {
        saveCsv(result, cfg_, cfg_.csvOutput);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Shared accounting helper
// ---------------------------------------------------------------------------

BenchmarkResult BenchmarkRunner::finalise(BenchmarkResult result,
                                           const std::vector<Trade>& allTrades,
                                           const std::vector<OrderId>& acceptedIds,
                                           MatchingEngine& engine) {
    result.memoryKB = currentRssKB();

    if (cfg_.verifyInvariants) {
        std::vector<InvariantChecker::Report> reports;
        for (const auto& sym : cfg_.symbols) {
            if (auto* book = engine.book(sym))
                reports.push_back(InvariantChecker::checkBook(*book));
        }
        reports.push_back(InvariantChecker::checkTrades(allTrades, {}));
        reports.push_back(InvariantChecker::checkUniqueIds(acceptedIds));
        result.invariants = InvariantChecker::merge(std::move(reports));
    } else {
        result.invariants = {true, {}};
    }

    return result;
}

// ---------------------------------------------------------------------------
// Baseline: single-threaded, std::shared_ptr (original behaviour)
// ---------------------------------------------------------------------------

BenchmarkResult BenchmarkRunner::runBaseline(
        const std::vector<std::shared_ptr<Order>>& orders) {

    MatchingEngine engine;
    for (const auto& sym : cfg_.symbols) engine.registerSymbol(sym);

    LatencyTimer timer;
    timer.reserve(orders.size());

    BenchmarkResult result;
    result.ordersSubmitted = orders.size();

    std::vector<Trade>   allTrades;
    std::vector<OrderId> acceptedIds;
    allTrades.reserve(orders.size() / 4);
    acceptedIds.reserve(orders.size());

    auto wallStart = std::chrono::high_resolution_clock::now();

    for (auto& order : orders) {
        timer.start();
        auto res = engine.submitOrder(order);
        timer.stop();

        if (res.status == OrderStatus::REJECTED) {
            ++result.ordersRejected;
        } else if (res.status == OrderStatus::CANCELLED) {
            ++result.cancelsProcessed;
            ++result.ordersAccepted;
        } else {
            ++result.ordersAccepted;
            acceptedIds.push_back(order->id);
        }

        result.tradesGenerated += res.trades.size();
        for (auto& t : res.trades) allTrades.push_back(t);
    }

    auto wallEnd = std::chrono::high_resolution_clock::now();
    auto wallNs  = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallStart).count());

    result.latency = timer.compute(wallNs);
    return finalise(std::move(result), allTrades, acceptedIds, engine);
}

// ---------------------------------------------------------------------------
// Pool mode: single-threaded, MemoryPool<Order> for resting-order copies.
//
// Architecture note: The engine stores shared_ptr<Order> in PriceLevels.
// We can't trivially replace shared_ptr without invasive surgery on
// PriceLevel/OrderBook. Instead we use the pool for the submitted Order
// objects themselves — the generator already produces shared_ptrs, so we
// pool-allocate the Order and wrap with a custom deleter.
//
// Result: eliminates the tcmalloc/jemalloc round-trip for each Order
// construction on the hot path; also avoids the separate control-block
// allocation that make_shared would create.
// ---------------------------------------------------------------------------

BenchmarkResult BenchmarkRunner::runPool(
        const std::vector<std::shared_ptr<Order>>& orders) {

    MatchingEngine engine;
    for (const auto& sym : cfg_.symbols) engine.registerSymbol(sym);

    // Size pool to hold all submitted orders simultaneously (worst case all rest).
    MemoryPool<Order> pool(orders.size() + 1);

    LatencyTimer timer;
    timer.reserve(orders.size());

    BenchmarkResult result;
    result.ordersSubmitted = orders.size();

    std::vector<Trade>   allTrades;
    std::vector<OrderId> acceptedIds;
    allTrades.reserve(orders.size() / 4);
    acceptedIds.reserve(orders.size());

    auto wallStart = std::chrono::high_resolution_clock::now();

    for (auto& srcOrder : orders) {
        // Copy the generator's order into a pool-allocated slot.
        // Custom deleter returns the slot to the pool on last shared_ptr release.
        Order* raw = pool.alloc(
            srcOrder->id,
            srcOrder->symbol,
            srcOrder->side,
            srcOrder->type,
            srcOrder->price,
            srcOrder->quantity,
            srcOrder->timestamp
        );
        auto poolOrder = std::shared_ptr<Order>(raw, [&pool](Order* p) {
            pool.dealloc(p);
        });

        timer.start();
        auto res = engine.submitOrder(poolOrder);
        timer.stop();

        if (res.status == OrderStatus::REJECTED) {
            ++result.ordersRejected;
        } else if (res.status == OrderStatus::CANCELLED) {
            ++result.cancelsProcessed;
            ++result.ordersAccepted;
        } else {
            ++result.ordersAccepted;
            acceptedIds.push_back(poolOrder->id);
        }

        result.tradesGenerated += res.trades.size();
        for (auto& t : res.trades) allTrades.push_back(t);
    }

    auto wallEnd = std::chrono::high_resolution_clock::now();
    auto wallNs  = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallStart).count());

    result.latency = timer.compute(wallNs);
    return finalise(std::move(result), allTrades, acceptedIds, engine);
}

// ---------------------------------------------------------------------------
// Pipeline mode: SPSC ring buffer between parser and engine threads.
//
// Thread layout:
//   Producer (this thread): pushes shared_ptr<Order> into the ring buffer.
//   Consumer (engine thread): pops and submits to the matching engine.
//
// The engine thread also collects results. The producer signals completion
// by pushing a nullptr sentinel.
//
// Latency: measured on the engine (consumer) thread — from pop to result.
// Throughput: total orders / wall time measured by the consumer thread.
// ---------------------------------------------------------------------------

BenchmarkResult BenchmarkRunner::runPipeline(
        const std::vector<std::shared_ptr<Order>>& orders) {

    const size_t cap = (cfg_.pipelineCapacity > 0)
        ? cfg_.pipelineCapacity
        : 1 << 14; // 16 384 slots default

    SpscRingBuffer<std::shared_ptr<Order>> ring(cap);

    BenchmarkResult result;
    result.ordersSubmitted = orders.size();

    // Consumer state (captured by the engine thread lambda)
    std::vector<Trade>   allTrades;
    std::vector<OrderId> acceptedIds;
    allTrades.reserve(orders.size() / 4);
    acceptedIds.reserve(orders.size());

    MatchingEngine engine;
    for (const auto& sym : cfg_.symbols) engine.registerSymbol(sym);

    LatencyTimer timer;
    timer.reserve(orders.size());

    // Spin-wait helper to avoid thundering-herd busy loops on macOS.
    auto spinPause = []() {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause" ::: "memory");
#else
        // ARM / Apple Silicon: yield hint
        __asm__ volatile("yield" ::: "memory");
#endif
    };

    // --- Consumer thread ---
    uint64_t wallNs = 0;
    std::thread consumer([&]() {
        auto wallStart = std::chrono::high_resolution_clock::now();

        while (true) {
            auto item = ring.pop();
            if (!item) { spinPause(); continue; }

            auto& order = *item;
            if (!order) break; // nullptr sentinel = done

            timer.start();
            auto res = engine.submitOrder(order);
            timer.stop();

            if (res.status == OrderStatus::REJECTED) {
                ++result.ordersRejected;
            } else if (res.status == OrderStatus::CANCELLED) {
                ++result.cancelsProcessed;
                ++result.ordersAccepted;
            } else {
                ++result.ordersAccepted;
                acceptedIds.push_back(order->id);
            }
            result.tradesGenerated += res.trades.size();
            for (auto& t : res.trades) allTrades.push_back(t);
        }

        auto wallEnd = std::chrono::high_resolution_clock::now();
        wallNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                wallEnd - wallStart).count());
    });

    // --- Producer (this thread): push all orders then nullptr sentinel ---
    for (auto& order : orders) {
        while (!ring.push(order)) spinPause(); // backpressure: spin until slot free
    }
    // Push sentinel
    while (!ring.push(std::shared_ptr<Order>{nullptr})) spinPause();

    consumer.join();

    result.latency = timer.compute(wallNs);
    return finalise(std::move(result), allTrades, acceptedIds, engine);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static const char* modeName(BenchmarkMode m) {
    switch (m) {
        case BenchmarkMode::Pool:     return "pool";
        case BenchmarkMode::Pipeline: return "pipeline";
        default:                      return "baseline";
    }
}

void BenchmarkRunner::printTable(const BenchmarkResult& r, const BenchmarkConfig& cfg) {
    auto fmt = [](uint64_t n) -> std::string {
        std::string s = std::to_string(n);
        int pos = static_cast<int>(s.size()) - 3;
        while (pos > 0) { s.insert(pos, ","); pos -= 3; }
        return s;
    };

    const int W = 28;
    std::cout << "\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  AstraExchange Benchmark [" << modeName(r.mode) << "]\n";
    std::cout << std::string(50, '=') << "\n";

    auto row = [&](const std::string& label, const std::string& value) {
        std::cout << "  " << std::left << std::setw(W) << label << value << "\n";
    };

    row("Orders submitted:",  fmt(r.ordersSubmitted));
    row("Orders accepted:",   fmt(r.ordersAccepted));
    row("Orders rejected:",   fmt(r.ordersRejected));
    row("Cancels processed:", fmt(r.cancelsProcessed));
    row("Trades generated:",  fmt(r.tradesGenerated));
    row("Symbols:",           std::to_string(cfg.symbols.size()));

    std::cout << "  " << std::string(46, '-') << "\n";

    std::ostringstream thr;
    thr << std::fixed << std::setprecision(2)
        << r.latency.throughputOpsPerSec / 1e6 << " M orders/sec";
    row("Throughput:", thr.str());

    auto usRow = [&](const std::string& label, uint64_t ns) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << ns / 1e3 << " μs";
        row(label, ss.str());
    };
    usRow("p50 latency:", r.latency.p50);
    usRow("p95 latency:", r.latency.p95);
    usRow("p99 latency:", r.latency.p99);
    usRow("Max latency:", r.latency.max);

    if (r.memoryKB > 0) {
        std::ostringstream mem;
        mem << std::fixed << std::setprecision(1) << r.memoryKB / 1024.0 << " MB (RSS)";
        row("Memory usage:", mem.str());
    }

    std::cout << "  " << std::string(46, '-') << "\n";
    if (r.invariants.passed) {
        std::cout << "  Invariants: PASS (all "
                  << (cfg.verifyInvariants ? "checked" : "skipped") << ")\n";
    } else {
        std::cout << "  Invariants: FAIL\n";
        for (auto& v : r.invariants.violations)
            std::cout << "    [" << v.symbol << "] " << v.rule << ": " << v.detail << "\n";
    }
    std::cout << std::string(50, '=') << "\n\n";
}

void BenchmarkRunner::saveCsv(const BenchmarkResult& r, const BenchmarkConfig& cfg,
                               const std::string& path) {
    bool writeHeader = false;
    { std::ifstream check(path); writeHeader = !check.good(); }

    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Warning: cannot write CSV to " << path << "\n";
        return;
    }

    if (writeHeader) {
        file << "mode,orders_submitted,orders_accepted,orders_rejected,"
             << "cancels_processed,trades_generated,symbols,"
             << "throughput_ops_per_sec,p50_ns,p95_ns,p99_ns,max_ns,"
             << "memory_kb,invariants_passed\n";
    }

    file << modeName(r.mode) << ","
         << r.ordersSubmitted   << ","
         << r.ordersAccepted    << ","
         << r.ordersRejected    << ","
         << r.cancelsProcessed  << ","
         << r.tradesGenerated   << ","
         << cfg.symbols.size()  << ","
         << r.latency.throughputOpsPerSec << ","
         << r.latency.p50 << ","
         << r.latency.p95 << ","
         << r.latency.p99 << ","
         << r.latency.max << ","
         << r.memoryKB    << ","
         << (r.invariants.passed ? "true" : "false") << "\n";
}

size_t BenchmarkRunner::currentRssKB() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss / 1024);
#else
    return static_cast<size_t>(usage.ru_maxrss);
#endif
}

} // namespace astra

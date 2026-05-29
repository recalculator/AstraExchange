#pragma once

#include "benchmark/OrderGenerator.hpp"
#include "benchmark/InvariantChecker.hpp"
#include "benchmark/LatencyTimer.hpp"
#include "core/MatchingEngine.hpp"
#include <string>
#include <vector>

namespace astra {

enum class BenchmarkMode {
    Baseline,  // single-threaded, std::make_shared (original)
    Pool,      // single-threaded, MemoryPool allocator
    Pipeline,  // SPSC ring buffer: parser thread → engine thread
};

struct BenchmarkConfig {
    uint64_t            orderCount  = 1'000'000;
    std::vector<Symbol> symbols     = {"AAPL", "MSFT", "NVDA", "TSLA"};
    uint32_t            seed        = 42;
    std::string         csvOutput;
    bool                verifyInvariants = true;
    BenchmarkMode       mode        = BenchmarkMode::Baseline;

    // Pipeline ring-buffer capacity (must be power-of-two; 0 → auto)
    size_t              pipelineCapacity = 0;
};

struct BenchmarkResult {
    uint64_t ordersSubmitted  = 0;
    uint64_t ordersAccepted   = 0;
    uint64_t ordersRejected   = 0;
    uint64_t cancelsProcessed = 0;
    uint64_t tradesGenerated  = 0;

    LatencyTimer::Stats latency;
    size_t memoryKB = 0;
    InvariantChecker::Report invariants;
    BenchmarkMode mode = BenchmarkMode::Baseline;
};

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(BenchmarkConfig cfg);

    BenchmarkResult run();

    static void printTable(const BenchmarkResult& r, const BenchmarkConfig& cfg);
    static void saveCsv(const BenchmarkResult& r, const BenchmarkConfig& cfg,
                        const std::string& path);

private:
    BenchmarkConfig cfg_;

    // Run modes
    BenchmarkResult runBaseline(const std::vector<std::shared_ptr<Order>>& orders);
    BenchmarkResult runPool(const std::vector<std::shared_ptr<Order>>& orders);
    BenchmarkResult runPipeline(const std::vector<std::shared_ptr<Order>>& orders);

    BenchmarkResult finalise(BenchmarkResult result,
                             const std::vector<Trade>& allTrades,
                             const std::vector<OrderId>& acceptedIds,
                             MatchingEngine& engine);

    static size_t currentRssKB();
};

} // namespace astra

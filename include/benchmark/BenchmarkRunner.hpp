#pragma once

#include "benchmark/OrderGenerator.hpp"
#include "benchmark/InvariantChecker.hpp"
#include "benchmark/LatencyTimer.hpp"
#include "core/MatchingEngine.hpp"
#include <string>
#include <vector>

namespace astra {

struct BenchmarkConfig {
    uint64_t            orderCount  = 1'000'000;
    std::vector<Symbol> symbols     = {"AAPL", "MSFT", "NVDA", "TSLA"};
    uint32_t            seed        = 42;
    std::string         csvOutput;   // empty → no file output
    bool                verifyInvariants = true;
};

struct BenchmarkResult {
    // Order counts
    uint64_t ordersSubmitted  = 0;
    uint64_t ordersAccepted   = 0;
    uint64_t ordersRejected   = 0;
    uint64_t cancelsProcessed = 0;
    uint64_t tradesGenerated  = 0;

    // Latency
    LatencyTimer::Stats latency;

    // Memory (RSS in KB, 0 if unavailable)
    size_t memoryKB = 0;

    // Invariant check
    InvariantChecker::Report invariants;
};

class BenchmarkRunner {
public:
    explicit BenchmarkRunner(BenchmarkConfig cfg);

    BenchmarkResult run();

    // Print a formatted terminal table to stdout.
    static void printTable(const BenchmarkResult& r, const BenchmarkConfig& cfg);

    // Append one row to a CSV file. Creates header if file doesn't exist.
    static void saveCsv(const BenchmarkResult& r, const BenchmarkConfig& cfg,
                        const std::string& path);

private:
    BenchmarkConfig cfg_;
    static size_t currentRssKB();
};

} // namespace astra

#include "benchmark/BenchmarkRunner.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <numeric>
#include <sys/resource.h>

namespace astra {

BenchmarkRunner::BenchmarkRunner(BenchmarkConfig cfg)
    : cfg_(std::move(cfg))
{}

BenchmarkResult BenchmarkRunner::run() {
    // --- Generate orders ---
    GeneratorConfig genCfg;
    genCfg.count   = cfg_.orderCount;
    genCfg.symbols = cfg_.symbols;
    genCfg.seed    = cfg_.seed;

    OrderGenerator gen(genCfg);
    auto orders = gen.generate();

    // --- Engine setup ---
    MatchingEngine engine;
    for (const auto& sym : cfg_.symbols) engine.registerSymbol(sym);

    // --- Run with latency measurement ---
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
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallStart).count()
    );

    result.latency   = timer.compute(wallNs);
    result.memoryKB  = currentRssKB();

    // --- Invariant verification ---
    if (cfg_.verifyInvariants) {
        std::vector<InvariantChecker::Report> reports;

        for (const auto& sym : cfg_.symbols) {
            if (auto* book = engine.book(sym)) {
                reports.push_back(InvariantChecker::checkBook(*book));
            }
        }

        reports.push_back(InvariantChecker::checkTrades(allTrades, {}));
        reports.push_back(InvariantChecker::checkUniqueIds(acceptedIds));

        result.invariants = InvariantChecker::merge(std::move(reports));
    } else {
        result.invariants = {true, {}};
    }

    // --- CSV output ---
    if (!cfg_.csvOutput.empty()) {
        saveCsv(result, cfg_, cfg_.csvOutput);
    }

    return result;
}

void BenchmarkRunner::printTable(const BenchmarkResult& r, const BenchmarkConfig& cfg) {
    auto fmt = [](uint64_t n) -> std::string {
        // Insert thousands separators
        std::string s = std::to_string(n);
        int insertPos = static_cast<int>(s.size()) - 3;
        while (insertPos > 0) { s.insert(insertPos, ","); insertPos -= 3; }
        return s;
    };

    const int W = 28;
    std::cout << "\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "  AstraExchange Benchmark Results\n";
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

    double throughputM = r.latency.throughputOpsPerSec / 1e6;
    std::ostringstream thr;
    thr << std::fixed << std::setprecision(2) << throughputM << " M orders/sec";
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
        std::cout << "  Invariants: PASS (all " << (cfg.verifyInvariants ? "checked" : "skipped") << ")\n";
    } else {
        std::cout << "  Invariants: FAIL\n";
        for (auto& v : r.invariants.violations) {
            std::cout << "    [" << v.symbol << "] " << v.rule << ": " << v.detail << "\n";
        }
    }

    std::cout << std::string(50, '=') << "\n\n";
}

void BenchmarkRunner::saveCsv(const BenchmarkResult& r, const BenchmarkConfig& cfg,
                               const std::string& path) {
    bool writeHeader = false;
    {
        std::ifstream check(path);
        writeHeader = !check.good();
    }

    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Warning: cannot write CSV to " << path << "\n";
        return;
    }

    if (writeHeader) {
        file << "orders_submitted,orders_accepted,orders_rejected,"
             << "cancels_processed,trades_generated,symbols,"
             << "throughput_ops_per_sec,p50_ns,p95_ns,p99_ns,max_ns,"
             << "memory_kb,invariants_passed\n";
    }

    file << r.ordersSubmitted << ","
         << r.ordersAccepted  << ","
         << r.ordersRejected  << ","
         << r.cancelsProcessed << ","
         << r.tradesGenerated  << ","
         << cfg.symbols.size() << ","
         << r.latency.throughputOpsPerSec << ","
         << r.latency.p50  << ","
         << r.latency.p95  << ","
         << r.latency.p99  << ","
         << r.latency.max  << ","
         << r.memoryKB     << ","
         << (r.invariants.passed ? "true" : "false") << "\n";
}

size_t BenchmarkRunner::currentRssKB() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    // On macOS ru_maxrss is in bytes; on Linux it's in KB.
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss / 1024);
#else
    return static_cast<size_t>(usage.ru_maxrss);
#endif
}

} // namespace astra

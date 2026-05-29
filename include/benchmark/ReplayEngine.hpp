#pragma once

#include "core/MatchingEngine.hpp"
#include "benchmark/LatencyTimer.hpp"
#include "feed/EventLog.hpp"
#include <string>
#include <vector>

namespace astra {

struct ReplayConfig {
    std::string csvPath;
    bool        printTrades   = false;
    bool        printBook     = false;
    bool        verbose       = false;
    size_t      bookDepth     = 5;
};

struct ReplayStats {
    uint64_t ordersSubmitted;
    uint64_t ordersAccepted;
    uint64_t ordersRejected;
    uint64_t tradeCount;
    LatencyTimer::Stats latency;
};

class ReplayEngine {
public:
    explicit ReplayEngine(std::vector<Symbol> symbols);

    ReplayStats run(const ReplayConfig& cfg);

private:
    MatchingEngine engine_;
    EventLog       log_;
};

} // namespace astra

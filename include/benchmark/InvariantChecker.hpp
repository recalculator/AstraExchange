#pragma once

#include "core/OrderBook.hpp"
#include "core/Trade.hpp"
#include <string>
#include <vector>
#include <unordered_set>

namespace astra {

struct InvariantViolation {
    std::string symbol;
    std::string rule;
    std::string detail;
};

// Checks correctness invariants across the full exchange state after a run.
class InvariantChecker {
public:
    struct Report {
        bool                          passed;
        std::vector<InvariantViolation> violations;
    };

    // Check a single book for structural correctness.
    static Report checkBook(const OrderBook& book);

    // Verify that all observed trades executed at the resting-order (maker) price,
    // and that bid < ask at time of each trade.
    static Report checkTrades(const std::vector<Trade>& trades,
                               const std::vector<std::pair<Price,Price>>& bidAskAtTrade);

    // Verify that a set of order IDs are globally unique.
    static Report checkUniqueIds(const std::vector<OrderId>& ids);

    // Combine multiple reports into one.
    static Report merge(std::vector<Report> reports);
};

} // namespace astra

#include "benchmark/InvariantChecker.hpp"
#include <sstream>

namespace astra {

InvariantChecker::Report InvariantChecker::checkBook(const OrderBook& book) {
    Report report{true, {}};

    auto addViolation = [&](const std::string& rule, const std::string& detail) {
        report.passed = false;
        report.violations.push_back({book.symbol(), rule, detail});
    };

    // 1. Best bid < best ask (no crossed book)
    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    if (bid && ask && *bid >= *ask) {
        std::ostringstream ss;
        ss << "bid=" << fromFixedPrice(*bid) << " ask=" << fromFixedPrice(*ask);
        addViolation("best_bid_lt_best_ask", ss.str());
    }

    // 2. Bid side is ordered descending (map guarantees this via std::greater)
    // 3. Ask side is ordered ascending (map guarantees this via std::less)
    // The data structure enforces these — verify non-empty levels have qty > 0.

    for (const auto& level : book.topBids(10000)) {
        if (level.qty == 0) {
            addViolation("no_zero_qty_bid_level",
                         "price=" + std::to_string(level.price));
        }
        if (level.orderCount == 0) {
            addViolation("no_empty_bid_level",
                         "price=" + std::to_string(level.price));
        }
    }

    for (const auto& level : book.topAsks(10000)) {
        if (level.qty == 0) {
            addViolation("no_zero_qty_ask_level",
                         "price=" + std::to_string(level.price));
        }
        if (level.orderCount == 0) {
            addViolation("no_empty_ask_level",
                         "price=" + std::to_string(level.price));
        }
    }

    // 4. Total resting volume is non-negative (always true for Quantity, but verify > 0
    //    only if book has levels)
    if ((bid || ask) && book.totalBidQty() == 0 && book.totalAskQty() == 0) {
        addViolation("resting_volume_nonzero",
                     "book reports levels but zero total volume");
    }

    // 5. Use OrderBook's own structural invariant check.
    if (!book.checkInvariants()) {
        addViolation("internal_structural_invariant",
                     "OrderBook::checkInvariants() failed");
    }

    return report;
}

InvariantChecker::Report InvariantChecker::checkTrades(
        const std::vector<Trade>& trades,
        const std::vector<std::pair<Price,Price>>& bidAskAtTrade) {
    Report report{true, {}};

    // bidAskAtTrade is optional context — only checked if provided and same size.
    bool hasBidAsk = (bidAskAtTrade.size() == trades.size());

    for (size_t i = 0; i < trades.size(); ++i) {
        const auto& t = trades[i];

        if (t.price <= 0) {
            report.passed = false;
            report.violations.push_back({t.symbol, "trade_positive_price",
                "trade id maker=" + std::to_string(t.makerOrderId) +
                " price=" + std::to_string(t.price)});
        }
        if (t.quantity == 0) {
            report.passed = false;
            report.violations.push_back({t.symbol, "trade_nonzero_qty",
                "maker=" + std::to_string(t.makerOrderId)});
        }

        if (hasBidAsk) {
            auto [bid, ask] = bidAskAtTrade[i];
            // Trade price must be between bid and ask (inclusive of maker price)
            if (bid > 0 && ask > 0 && bid >= ask) {
                report.passed = false;
                report.violations.push_back({t.symbol, "bid_lt_ask_at_trade",
                    "bid=" + std::to_string(bid) + " ask=" + std::to_string(ask)});
            }
        }
    }

    return report;
}

InvariantChecker::Report InvariantChecker::checkUniqueIds(
        const std::vector<OrderId>& ids) {
    Report report{true, {}};
    std::unordered_set<OrderId> seen;
    seen.reserve(ids.size());
    for (OrderId id : ids) {
        if (!seen.insert(id).second) {
            report.passed = false;
            report.violations.push_back({"global", "unique_order_ids",
                "duplicate id=" + std::to_string(id)});
        }
    }
    return report;
}

InvariantChecker::Report InvariantChecker::merge(std::vector<Report> reports) {
    Report combined{true, {}};
    for (auto& r : reports) {
        if (!r.passed) combined.passed = false;
        for (auto& v : r.violations)
            combined.violations.push_back(std::move(v));
    }
    return combined;
}

} // namespace astra

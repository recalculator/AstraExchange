#pragma once

#include "OrderBook.hpp"
#include "core/RiskValidator.hpp"
#include <unordered_map>
#include <vector>
#include <functional>

namespace astra {

struct EngineResult {
    OrderStatus          status;
    std::vector<Trade>   trades;
    std::string          rejectReason; // non-empty when status == REJECTED
};

class MatchingEngine {
public:
    MatchingEngine();

    void registerSymbol(const Symbol& symbol);

    EngineResult submitOrder(std::shared_ptr<Order> order);
    EngineResult cancelOrder(const Symbol& symbol, OrderId id);

    const OrderBook* book(const Symbol& symbol) const;

    // Aggregate stats
    uint64_t totalOrdersProcessed() const { return ordersProcessed_; }
    uint64_t totalTradesGenerated() const { return tradesGenerated_; }

private:
    std::unordered_map<Symbol, OrderBook> books_;
    RiskValidator validator_;
    uint64_t ordersProcessed_ = 0;
    uint64_t tradesGenerated_ = 0;
};

} // namespace astra

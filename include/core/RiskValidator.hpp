#pragma once

#include "Order.hpp"
#include <unordered_set>
#include <string>

namespace astra {

class RiskValidator {
public:
    struct ValidationResult {
        bool        ok;
        std::string reason;
    };

    void registerSymbol(const Symbol& s) { symbols_.insert(s); }

    ValidationResult validate(const Order& order) const {
        if (order.quantity == 0)
            return {false, "quantity must be > 0"};

        if (order.type == OrderType::LIMIT && order.price <= 0)
            return {false, "limit order price must be > 0"};

        if (!symbols_.count(order.symbol))
            return {false, "unknown symbol: " + order.symbol};

        if (usedIds_.count(order.id))
            return {false, "duplicate order id: " + std::to_string(order.id)};

        return {true, ""};
    }

    void recordId(OrderId id) { usedIds_.insert(id); }

private:
    std::unordered_set<Symbol>  symbols_;
    std::unordered_set<OrderId> usedIds_;
};

} // namespace astra

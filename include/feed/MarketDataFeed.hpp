#pragma once

#include "core/OrderBook.hpp"
#include "core/Trade.hpp"
#include <optional>

namespace astra {

class MarketDataFeed {
public:
    void publish(const OrderBook& book, const std::vector<Trade>& newTrades, size_t depth = 5);

private:
    void printBook(const OrderBook& book, size_t depth);
    void printTrades(const std::vector<Trade>& trades);
};

} // namespace astra

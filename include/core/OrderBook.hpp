#pragma once

#include "PriceLevel.hpp"
#include "Trade.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>

namespace astra {

struct BookLevel {
    Price    price;
    Quantity qty;
    size_t   orderCount;
};

// Callback types emitted by the book.
using TradeCallback     = std::function<void(const Trade&)>;
using BookUpdateCallback = std::function<void(const Symbol&)>;

class OrderBook {
public:
    explicit OrderBook(Symbol symbol);

    // Returns trades generated and sets order status on the incoming order.
    std::vector<Trade> addOrder(std::shared_ptr<Order> order);

    bool cancelOrder(OrderId id);

    // Location lookup for O(1) cancel.
    bool hasOrder(OrderId id) const;

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::optional<Price> spread() const;

    // Top N levels for display / feed.
    std::vector<BookLevel> topBids(size_t n = 5) const;
    std::vector<BookLevel> topAsks(size_t n = 5) const;

    Quantity totalBidQty() const;
    Quantity totalAskQty() const;

    const Symbol& symbol() const { return symbol_; }

    // Correctness invariants — called in debug builds.
    bool checkInvariants() const;

private:
    Symbol symbol_;

    // bids: descending price → highest priority first
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // asks: ascending price → lowest priority first
    std::map<Price, PriceLevel> asks_;

    // order_id → side + price for O(1) lookup
    struct OrderLocation { Side side; Price price; };
    std::unordered_map<OrderId, OrderLocation> index_;

    std::vector<Trade> matchLimit(std::shared_ptr<Order> incoming);
    std::vector<Trade> matchMarket(std::shared_ptr<Order> incoming);

    Trade makeTrade(const Order& maker, Order& taker, Quantity qty) const;

    template<typename Map>
    std::vector<Trade> drainSide(Map& side, std::shared_ptr<Order> incoming,
                                  std::function<bool(Price)> crosses);
    template<typename Map>
    void restOrder(Map& side, std::shared_ptr<Order> incoming);
};

} // namespace astra

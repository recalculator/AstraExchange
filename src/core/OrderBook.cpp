#include "core/OrderBook.hpp"
#include <cassert>
#include <stdexcept>

namespace astra {

OrderBook::OrderBook(Symbol symbol) : symbol_(std::move(symbol)) {}

std::vector<Trade> OrderBook::addOrder(std::shared_ptr<Order> order) {
    order->status = OrderStatus::ACCEPTED;

    switch (order->type) {
        case OrderType::MARKET: return matchMarket(order);
        case OrderType::IOC:    return matchIoc(order);
        case OrderType::FOK:    return matchFok(order);
        default:                return matchLimit(order);
    }
}

bool OrderBook::cancelOrder(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;

    const auto [side, price] = it->second;

    if (side == Side::BUY) {
        auto levelIt = bids_.find(price);
        if (levelIt != bids_.end()) {
            levelIt->second.removeOrder(id);
            if (levelIt->second.empty()) bids_.erase(levelIt);
        }
    } else {
        auto levelIt = asks_.find(price);
        if (levelIt != asks_.end()) {
            levelIt->second.removeOrder(id);
            if (levelIt->second.empty()) asks_.erase(levelIt);
        }
    }

    index_.erase(it);
    return true;
}

bool OrderBook::hasOrder(OrderId id) const {
    return index_.count(id) > 0;
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const {
    auto bid = bestBid();
    auto ask = bestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

std::vector<BookLevel> OrderBook::topBids(size_t n) const {
    std::vector<BookLevel> result;
    result.reserve(n);
    for (auto& [price, level] : bids_) {
        if (result.size() >= n) break;
        result.push_back({price, level.totalQty(), level.orderCount()});
    }
    return result;
}

std::vector<BookLevel> OrderBook::topAsks(size_t n) const {
    std::vector<BookLevel> result;
    result.reserve(n);
    for (auto& [price, level] : asks_) {
        if (result.size() >= n) break;
        result.push_back({price, level.totalQty(), level.orderCount()});
    }
    return result;
}

Quantity OrderBook::totalBidQty() const {
    Quantity total = 0;
    for (auto& [_, level] : bids_) total += level.totalQty();
    return total;
}

Quantity OrderBook::totalAskQty() const {
    Quantity total = 0;
    for (auto& [_, level] : asks_) total += level.totalQty();
    return total;
}

// --- private ---

Trade OrderBook::makeTrade(const Order& maker, Order& taker, Quantity qty) const {
    return Trade{symbol_, maker.price, qty, maker.id, taker.id, nowNs()};
}

// Drain trades from a typed resting-side map until qty exhausted or price condition fails.
template<typename Map>
std::vector<Trade> OrderBook::drainSide(Map& restingSide,
                                         std::shared_ptr<Order> incoming,
                                         std::function<bool(Price)> crosses) {
    std::vector<Trade> trades;
    while (incoming->remainingQty() > 0 && !restingSide.empty()) {
        auto it = restingSide.begin();
        if (crosses && !crosses(it->first)) break;

        PriceLevel& level = it->second;
        auto maker = level.front();

        Quantity fillQty = std::min(incoming->remainingQty(), maker->remainingQty());
        trades.push_back(makeTrade(*maker, *incoming, fillQty));

        maker->filledQty  += fillQty;
        incoming->filledQty += fillQty;
        level.updateTotalQty(fillQty);

        if (maker->isFilled()) {
            maker->status = OrderStatus::FILLED;
            index_.erase(maker->id);
            level.popFront();
        } else {
            maker->status = OrderStatus::PARTIALLY_FILLED;
        }

        if (level.empty()) restingSide.erase(it);
    }
    return trades;
}

template<typename Map>
void OrderBook::restOrder(Map& ownSide, std::shared_ptr<Order> incoming) {
    auto it = ownSide.find(incoming->price);
    if (it == ownSide.end()) {
        auto [ins, ok] = ownSide.emplace(incoming->price, PriceLevel{incoming->price});
        (void)ok;
        ins->second.addOrder(incoming);
    } else {
        it->second.addOrder(incoming);
    }
    index_[incoming->id] = {incoming->side, incoming->price};
}

std::vector<Trade> OrderBook::matchLimit(std::shared_ptr<Order> incoming) {
    std::vector<Trade> trades;

    if (incoming->side == Side::BUY) {
        auto crosses = [&](Price p){ return p <= incoming->price; };
        trades = drainSide(asks_, incoming, crosses);
    } else {
        auto crosses = [&](Price p){ return p >= incoming->price; };
        trades = drainSide(bids_, incoming, crosses);
    }

    if (incoming->filledQty == 0) {
        incoming->status = OrderStatus::ACCEPTED;
    } else if (incoming->isFilled()) {
        incoming->status = OrderStatus::FILLED;
    } else {
        incoming->status = OrderStatus::PARTIALLY_FILLED;
    }

    if (!incoming->isFilled()) {
        if (incoming->side == Side::BUY) restOrder(bids_, incoming);
        else                             restOrder(asks_, incoming);
    }

    return trades;
}

std::vector<Trade> OrderBook::matchMarket(std::shared_ptr<Order> incoming) {
    std::vector<Trade> trades;

    if (incoming->side == Side::BUY) {
        trades = drainSide(asks_, incoming, nullptr);
    } else {
        trades = drainSide(bids_, incoming, nullptr);
    }

    incoming->status = incoming->isFilled() ? OrderStatus::FILLED
                                             : OrderStatus::PARTIALLY_FILLED;
    return trades;
}

// Scan the resting side without touching the book to count fillable qty.
template<typename Map>
Quantity OrderBook::availableFill(const Map& restingSide, const Order& incoming,
                                   std::function<bool(Price)> crosses) const {
    Quantity available = 0;
    for (auto& [price, level] : restingSide) {
        if (crosses && !crosses(price)) break;
        available += level.totalQty();
        if (available >= incoming.quantity) break; // early exit — enough
    }
    return available;
}

// IOC: match greedily, cancel any unfilled remainder — never rests.
std::vector<Trade> OrderBook::matchIoc(std::shared_ptr<Order> incoming) {
    std::vector<Trade> trades;

    if (incoming->side == Side::BUY) {
        auto crosses = [&](Price p){ return p <= incoming->price; };
        trades = drainSide(asks_, incoming, crosses);
    } else {
        auto crosses = [&](Price p){ return p >= incoming->price; };
        trades = drainSide(bids_, incoming, crosses);
    }

    // Any unfilled remainder is cancelled — never rested.
    if (incoming->isFilled()) {
        incoming->status = OrderStatus::FILLED;
    } else if (incoming->filledQty > 0) {
        incoming->status = OrderStatus::PARTIALLY_FILLED;
    } else {
        // No fill at all — treat as cancelled (no liquidity at limit price)
        incoming->status = OrderStatus::CANCELLED;
    }

    return trades;
}

// FOK: fill fully or not at all. No partial fills, never rests.
std::vector<Trade> OrderBook::matchFok(std::shared_ptr<Order> incoming) {
    // Pre-check: can the full qty be filled without executing?
    bool canFill = false;
    if (incoming->side == Side::BUY) {
        auto crosses = [&](Price p){ return p <= incoming->price; };
        canFill = (availableFill(asks_, *incoming, crosses) >= incoming->quantity);
    } else {
        auto crosses = [&](Price p){ return p >= incoming->price; };
        canFill = (availableFill(bids_, *incoming, crosses) >= incoming->quantity);
    }

    if (!canFill) {
        incoming->status = OrderStatus::CANCELLED;
        return {};
    }

    // Full qty available — execute exactly like a limit order drain.
    std::vector<Trade> trades;
    if (incoming->side == Side::BUY) {
        auto crosses = [&](Price p){ return p <= incoming->price; };
        trades = drainSide(asks_, incoming, crosses);
    } else {
        auto crosses = [&](Price p){ return p >= incoming->price; };
        trades = drainSide(bids_, incoming, crosses);
    }

    incoming->status = OrderStatus::FILLED;
    return trades;
}

bool OrderBook::checkInvariants() const {
    // Best bid must be strictly less than best ask.
    auto bid = bestBid();
    auto ask = bestAsk();
    if (bid && ask && *bid >= *ask) return false;

    // No zero-quantity orders in the book.
    for (auto& [p, level] : bids_)
        for (auto& o : level.orders())
            if (o->remainingQty() == 0) return false;
    for (auto& [p, level] : asks_)
        for (auto& o : level.orders())
            if (o->remainingQty() == 0) return false;

    // Index must be consistent with book.
    for (auto& [id, loc] : index_) {
        if (loc.side == Side::BUY) {
            if (!bids_.count(loc.price)) return false;
        } else {
            if (!asks_.count(loc.price)) return false;
        }
    }

    return true;
}

} // namespace astra

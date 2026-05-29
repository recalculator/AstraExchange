#include "core/MatchingEngine.hpp"

namespace astra {

MatchingEngine::MatchingEngine() = default;

void MatchingEngine::registerSymbol(const Symbol& symbol) {
    books_.emplace(symbol, OrderBook{symbol});
    validator_.registerSymbol(symbol);
}

EngineResult MatchingEngine::submitOrder(std::shared_ptr<Order> order) {
    ++ordersProcessed_;

    // Handle CANCEL type routed through submitOrder convenience path.
    if (order->type == OrderType::CANCEL) {
        return cancelOrder(order->symbol, order->id);
    }

    auto vr = validator_.validate(*order);
    if (!vr.ok) {
        order->status = OrderStatus::REJECTED;
        return {OrderStatus::REJECTED, {}, vr.reason};
    }
    validator_.recordId(order->id);

    auto it = books_.find(order->symbol);
    if (it == books_.end()) {
        order->status = OrderStatus::REJECTED;
        return {OrderStatus::REJECTED, {}, "no book for symbol"};
    }

    auto trades = it->second.addOrder(order);
    tradesGenerated_ += trades.size();

    return {order->status, std::move(trades), ""};
}

EngineResult MatchingEngine::cancelOrder(const Symbol& symbol, OrderId id) {
    auto it = books_.find(symbol);
    if (it == books_.end())
        return {OrderStatus::REJECTED, {}, "unknown symbol"};

    bool cancelled = it->second.cancelOrder(id);
    if (!cancelled)
        return {OrderStatus::REJECTED, {}, "order not found: " + std::to_string(id)};

    return {OrderStatus::CANCELLED, {}, ""};
}

const OrderBook* MatchingEngine::book(const Symbol& symbol) const {
    auto it = books_.find(symbol);
    return (it != books_.end()) ? &it->second : nullptr;
}

} // namespace astra

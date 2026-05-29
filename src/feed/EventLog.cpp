#include "feed/EventLog.hpp"
#include <iostream>
#include <sstream>

namespace astra {

EventLog::EventLog(Sink sink)
    : sink_(std::move(sink))
{
    if (!sink_) {
        sink_ = [](const Event& e) {
            std::cout << e.detail << "\n";
        };
    }
}

void EventLog::emit(EventType type, std::string detail) {
    sink_(Event{type, nowNs(), std::move(detail)});
}

void EventLog::logOrderAccepted(const Order& o) {
    std::ostringstream ss;
    ss << "ORDER_ACCEPTED id=" << o.id << " sym=" << o.symbol
       << " side=" << (o.side == Side::BUY ? "BUY" : "SELL")
       << " qty=" << o.quantity << " price=" << fromFixedPrice(o.price);
    emit(EventType::ORDER_ACCEPTED, ss.str());
}

void EventLog::logOrderRejected(const Order& o, const std::string& reason) {
    std::ostringstream ss;
    ss << "ORDER_REJECTED id=" << o.id << " reason=" << reason;
    emit(EventType::ORDER_REJECTED, ss.str());
}

void EventLog::logOrderFilled(const Order& o) {
    std::ostringstream ss;
    ss << "ORDER_FILLED id=" << o.id << " sym=" << o.symbol;
    emit(EventType::ORDER_FILLED, ss.str());
}

void EventLog::logOrderPartialFill(const Order& o) {
    std::ostringstream ss;
    ss << "ORDER_PARTIAL_FILL id=" << o.id
       << " filled=" << o.filledQty << "/" << o.quantity;
    emit(EventType::ORDER_PARTIALLY_FILLED, ss.str());
}

void EventLog::logOrderCancelled(OrderId id) {
    std::ostringstream ss;
    ss << "ORDER_CANCELLED id=" << id;
    emit(EventType::ORDER_CANCELLED, ss.str());
}

void EventLog::logTrade(const Trade& t) {
    std::ostringstream ss;
    ss << "TRADE sym=" << t.symbol
       << " price=" << fromFixedPrice(t.price)
       << " qty=" << t.quantity
       << " maker=" << t.makerOrderId
       << " taker=" << t.takerOrderId;
    emit(EventType::TRADE, ss.str());
}

} // namespace astra

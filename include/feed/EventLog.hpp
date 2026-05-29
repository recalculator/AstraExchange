#pragma once

#include "core/Order.hpp"
#include "core/Trade.hpp"
#include <string>
#include <functional>

namespace astra {

enum class EventType {
    ORDER_ACCEPTED,
    ORDER_REJECTED,
    ORDER_PARTIALLY_FILLED,
    ORDER_FILLED,
    ORDER_CANCELLED,
    TRADE,
    BOOK_UPDATED
};

struct Event {
    EventType   type;
    Timestamp   timestamp;
    std::string detail;
};

// Lightweight event log: calls a sink function for each event.
// Default sink writes to stdout; replace for file output or metrics.
class EventLog {
public:
    using Sink = std::function<void(const Event&)>;

    explicit EventLog(Sink sink = nullptr);

    void logOrderAccepted(const Order& o);
    void logOrderRejected(const Order& o, const std::string& reason);
    void logOrderFilled(const Order& o);
    void logOrderPartialFill(const Order& o);
    void logOrderCancelled(OrderId id);
    void logTrade(const Trade& t);

    void setSink(Sink sink) { sink_ = std::move(sink); }

private:
    Sink sink_;
    void emit(EventType type, std::string detail);
};

} // namespace astra

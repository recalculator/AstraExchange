#pragma once

#include "Order.hpp"
#include <deque>
#include <memory>

namespace astra {

// Owns resting orders at a single price point, maintaining time priority.
class PriceLevel {
public:
    Price price;

    explicit PriceLevel(Price p) : price(p), totalQty_(0) {}

    void addOrder(std::shared_ptr<Order> order) {
        totalQty_ += order->remainingQty();
        orders_.push_back(std::move(order));
    }

    // Remove an order by id. Returns true if found.
    bool removeOrder(OrderId id) {
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if ((*it)->id == id) {
                totalQty_ -= (*it)->remainingQty();
                orders_.erase(it);
                return true;
            }
        }
        return false;
    }

    std::shared_ptr<Order> front() const {
        return orders_.empty() ? nullptr : orders_.front();
    }

    void popFront() {
        if (!orders_.empty()) {
            totalQty_ -= orders_.front()->remainingQty();
            orders_.pop_front();
        }
    }

    // Called when the front order was partially filled externally.
    void updateTotalQty(Quantity filled) {
        totalQty_ = (totalQty_ >= filled) ? totalQty_ - filled : 0;
    }

    bool empty() const { return orders_.empty(); }
    Quantity totalQty() const { return totalQty_; }
    size_t orderCount() const { return orders_.size(); }

    const std::deque<std::shared_ptr<Order>>& orders() const { return orders_; }

private:
    std::deque<std::shared_ptr<Order>> orders_;
    Quantity totalQty_;
};

} // namespace astra

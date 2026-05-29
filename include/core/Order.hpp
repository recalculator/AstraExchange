#pragma once

#include "Types.hpp"

namespace astra {

struct Order {
    OrderId   id;
    Symbol    symbol;
    Side      side;
    OrderType type;
    Price     price;    // 0 for MARKET orders
    Quantity  quantity;
    Quantity  filledQty;
    Timestamp timestamp;
    OrderStatus status;

    Order(OrderId id_, Symbol symbol_, Side side_, OrderType type_,
          Price price_, Quantity quantity_, Timestamp timestamp_)
        : id(id_)
        , symbol(std::move(symbol_))
        , side(side_)
        , type(type_)
        , price(price_)
        , quantity(quantity_)
        , filledQty(0)
        , timestamp(timestamp_)
        , status(OrderStatus::PENDING)
    {}

    Quantity remainingQty() const { return quantity - filledQty; }
    bool isFilled() const { return filledQty >= quantity; }
};

} // namespace astra

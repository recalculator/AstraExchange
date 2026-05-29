#pragma once

#include "Types.hpp"

namespace astra {

struct Trade {
    Symbol    symbol;
    Price     price;
    Quantity  quantity;
    OrderId   makerOrderId;
    OrderId   takerOrderId;
    Timestamp timestamp;
};

} // namespace astra

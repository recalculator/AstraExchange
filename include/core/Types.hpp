#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace astra {

using OrderId  = uint64_t;
using Symbol   = std::string;
using Price    = int64_t;   // fixed-point: actual price * 10000
using Quantity = uint64_t;
using Timestamp = uint64_t; // nanoseconds since epoch

constexpr Price PRICE_SCALE = 10000;

enum class Side : uint8_t { BUY, SELL };
enum class OrderType : uint8_t { LIMIT, MARKET, CANCEL, IOC, FOK };

enum class OrderStatus : uint8_t {
    PENDING,
    ACCEPTED,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED,
    REJECTED
};

inline Price toFixedPrice(double p) {
    return static_cast<Price>(p * PRICE_SCALE + 0.5);
}

inline double fromFixedPrice(Price p) {
    return static_cast<double>(p) / PRICE_SCALE;
}

inline Timestamp nowNs() {
    return static_cast<Timestamp>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
    );
}

} // namespace astra

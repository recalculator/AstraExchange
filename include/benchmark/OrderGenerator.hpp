#pragma once

#include "core/Order.hpp"
#include <vector>
#include <string>
#include <random>
#include <cstdint>

namespace astra {

struct GeneratorConfig {
    uint64_t             count       = 1'000'000;
    std::vector<Symbol>  symbols     = {"AAPL", "MSFT", "NVDA", "TSLA"};
    uint32_t             seed        = 42;

    // Order type mix. Must sum to 1.0.
    // fracIoc and fracFok are carved out of fracLimit's share when non-zero.
    double fracLimit   = 0.55;
    double fracMarket  = 0.20;
    double fracCancel  = 0.15;
    double fracIoc     = 0.03;
    double fracFok     = 0.02;
    double fracInvalid = 0.05;

    // Price distribution per symbol — mid price and half-spread in cents
    struct SymbolParams {
        double midPrice;   // USD
        double halfSpread; // USD — limits cluster around mid ± halfSpread
        double tickSize;   // USD
    };
};

// Default per-symbol price parameters (realistic 2024 levels)
inline GeneratorConfig::SymbolParams defaultSymbolParams(const Symbol& sym) {
    if (sym == "AAPL")  return {185.0, 0.50, 0.01};
    if (sym == "MSFT")  return {415.0, 0.75, 0.01};
    if (sym == "NVDA")  return {875.0, 2.00, 0.01};
    if (sym == "TSLA")  return {180.0, 1.00, 0.01};
    return {100.0, 0.50, 0.01};
}

class OrderGenerator {
public:
    explicit OrderGenerator(GeneratorConfig cfg = {});

    // Generate the full order stream. Returned orders are valid inputs to
    // MatchingEngine::submitOrder. The generator seeds the book first with a
    // configurable number of resting limit orders so that market orders and
    // aggressive limits have something to cross.
    std::vector<std::shared_ptr<Order>> generate();

    uint64_t nextId() { return ++idCounter_; }

private:
    GeneratorConfig cfg_;
    std::mt19937    rng_;
    uint64_t        idCounter_ = 0;

    // Active resting order ids per symbol — used to generate realistic cancels.
    std::vector<std::pair<Symbol, OrderId>> restingOrders_;

    std::shared_ptr<Order> makeLimit(const Symbol& sym, Timestamp ts);
    std::shared_ptr<Order> makeMarket(const Symbol& sym, Timestamp ts);
    std::shared_ptr<Order> makeCancel(Timestamp ts);
    std::shared_ptr<Order> makeIoc(const Symbol& sym, Timestamp ts);
    std::shared_ptr<Order> makeFok(const Symbol& sym, Timestamp ts);
    std::shared_ptr<Order> makeInvalid(const Symbol& sym, Timestamp ts);
};

} // namespace astra

#include "benchmark/OrderGenerator.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace astra {

OrderGenerator::OrderGenerator(GeneratorConfig cfg)
    : cfg_(std::move(cfg))
    , rng_(cfg_.seed)
{}

std::vector<std::shared_ptr<Order>> OrderGenerator::generate() {
    std::vector<std::shared_ptr<Order>> orders;
    orders.reserve(cfg_.count);

    // Seed the book with realistic resting limit orders on both sides before
    // firing the main stream. This ensures market orders have liquidity to hit.
    const size_t seedCount = std::min<size_t>(cfg_.count / 10, 5000);
    for (size_t i = 0; i < seedCount; ++i) {
        std::uniform_int_distribution<size_t> symDist(0, cfg_.symbols.size() - 1);
        const Symbol& sym = cfg_.symbols[symDist(rng_)];
        auto o = makeLimit(sym, static_cast<Timestamp>(i));
        restingOrders_.emplace_back(sym, o->id);
        orders.push_back(std::move(o));
    }

    // Thresholds for order type selection (cumulative)
    const double tLimit  = cfg_.fracLimit;
    const double tMarket = tLimit  + cfg_.fracMarket;
    const double tCancel = tMarket + cfg_.fracCancel;
    const double tIoc    = tCancel + cfg_.fracIoc;
    const double tFok    = tIoc    + cfg_.fracFok;
    // remainder → invalid

    std::uniform_real_distribution<double> typeDist(0.0, 1.0);
    std::uniform_int_distribution<size_t>  symDist(0, cfg_.symbols.size() - 1);

    const size_t mainCount = cfg_.count - seedCount;
    for (size_t i = 0; i < mainCount; ++i) {
        Timestamp ts = static_cast<Timestamp>(seedCount + i);
        const Symbol& sym = cfg_.symbols[symDist(rng_)];
        double r = typeDist(rng_);

        std::shared_ptr<Order> o;
        if (r < tLimit) {
            o = makeLimit(sym, ts);
            // Track for future cancels — keep pool bounded
            if (restingOrders_.size() < 50'000) {
                restingOrders_.emplace_back(sym, o->id);
            }
        } else if (r < tMarket) {
            o = makeMarket(sym, ts);
        } else if (r < tCancel) {
            o = makeCancel(ts);
            if (!o) {
                // No resting orders to cancel yet — emit a limit instead
                o = makeLimit(sym, ts);
                restingOrders_.emplace_back(sym, o->id);
            }
        } else if (r < tIoc) {
            o = makeIoc(sym, ts);
        } else if (r < tFok) {
            o = makeFok(sym, ts);
        } else {
            o = makeInvalid(sym, ts);
        }

        orders.push_back(std::move(o));
    }

    return orders;
}

std::shared_ptr<Order> OrderGenerator::makeLimit(const Symbol& sym, Timestamp ts) {
    auto params = defaultSymbolParams(sym);

    // Decide side
    std::bernoulli_distribution sideDist(0.5);
    Side side = sideDist(rng_) ? Side::BUY : Side::SELL;

    // Price: mid ± uniform(0, halfSpread), rounded to tick
    std::uniform_real_distribution<double> spreadDist(0.0, params.halfSpread);
    double offset = spreadDist(rng_);
    double rawPrice = (side == Side::BUY) ? params.midPrice - offset
                                          : params.midPrice + offset;
    // Round to tick
    rawPrice = std::round(rawPrice / params.tickSize) * params.tickSize;
    if (rawPrice <= 0.0) rawPrice = params.tickSize;

    // Occasionally make an aggressive (crossing) order to generate trades
    std::bernoulli_distribution aggrDist(0.30);
    if (aggrDist(rng_)) {
        // Price past mid to cross the book
        std::uniform_real_distribution<double> aggrDist2(0.0, params.halfSpread * 2);
        double aggrOffset = aggrDist2(rng_);
        rawPrice = (side == Side::BUY) ? params.midPrice + aggrOffset
                                       : params.midPrice - aggrOffset;
        rawPrice = std::round(rawPrice / params.tickSize) * params.tickSize;
        if (rawPrice <= 0.0) rawPrice = params.tickSize;
    }

    std::uniform_int_distribution<Quantity> qtyDist(1, 500);

    return std::make_shared<Order>(nextId(), sym, side, OrderType::LIMIT,
                                   toFixedPrice(rawPrice), qtyDist(rng_), ts);
}

std::shared_ptr<Order> OrderGenerator::makeMarket(const Symbol& sym, Timestamp ts) {
    std::bernoulli_distribution sideDist(0.5);
    Side side = sideDist(rng_) ? Side::BUY : Side::SELL;
    std::uniform_int_distribution<Quantity> qtyDist(1, 200);
    return std::make_shared<Order>(nextId(), sym, side, OrderType::MARKET,
                                   0, qtyDist(rng_), ts);
}

std::shared_ptr<Order> OrderGenerator::makeCancel(Timestamp ts) {
    if (restingOrders_.empty()) return nullptr;

    // Pick a random resting order to cancel
    std::uniform_int_distribution<size_t> idxDist(0, restingOrders_.size() - 1);
    size_t idx = idxDist(rng_);
    auto [sym, cancelId] = restingOrders_[idx];

    // Remove from pool (swap with last for O(1))
    restingOrders_[idx] = restingOrders_.back();
    restingOrders_.pop_back();

    // CANCEL orders use the target order's id, not a new id.
    // Side is BUY as placeholder (engine uses symbol+id for lookup).
    return std::make_shared<Order>(cancelId, sym, Side::BUY, OrderType::CANCEL,
                                   0, 1, ts);
}

std::shared_ptr<Order> OrderGenerator::makeIoc(const Symbol& sym, Timestamp ts) {
    // IOC: aggressive limit price (crosses the spread) with moderate qty.
    auto params = defaultSymbolParams(sym);
    std::bernoulli_distribution sideDist(0.5);
    Side side = sideDist(rng_) ? Side::BUY : Side::SELL;

    // Use an aggressive price so it has a chance to fill.
    std::uniform_real_distribution<double> aggrDist(0.0, params.halfSpread * 3);
    double aggrOffset = aggrDist(rng_);
    double rawPrice = (side == Side::BUY) ? params.midPrice + aggrOffset
                                          : params.midPrice - aggrOffset;
    rawPrice = std::round(rawPrice / params.tickSize) * params.tickSize;
    if (rawPrice <= 0.0) rawPrice = params.tickSize;

    std::uniform_int_distribution<Quantity> qtyDist(1, 150);
    return std::make_shared<Order>(nextId(), sym, side, OrderType::IOC,
                                   toFixedPrice(rawPrice), qtyDist(rng_), ts);
}

std::shared_ptr<Order> OrderGenerator::makeFok(const Symbol& sym, Timestamp ts) {
    // FOK: moderate qty, aggressive price. Will succeed when liquidity is present.
    auto params = defaultSymbolParams(sym);
    std::bernoulli_distribution sideDist(0.5);
    Side side = sideDist(rng_) ? Side::BUY : Side::SELL;

    std::uniform_real_distribution<double> aggrDist(0.0, params.halfSpread * 2);
    double aggrOffset = aggrDist(rng_);
    double rawPrice = (side == Side::BUY) ? params.midPrice + aggrOffset
                                          : params.midPrice - aggrOffset;
    rawPrice = std::round(rawPrice / params.tickSize) * params.tickSize;
    if (rawPrice <= 0.0) rawPrice = params.tickSize;

    // Smaller qty so FOK has a better chance of full fill.
    std::uniform_int_distribution<Quantity> qtyDist(1, 80);
    return std::make_shared<Order>(nextId(), sym, side, OrderType::FOK,
                                   toFixedPrice(rawPrice), qtyDist(rng_), ts);
}

std::shared_ptr<Order> OrderGenerator::makeInvalid(const Symbol& sym, Timestamp ts) {
    std::uniform_int_distribution<int> kindDist(0, 2);
    int kind = kindDist(rng_);

    if (kind == 0) {
        // Zero quantity
        return std::make_shared<Order>(nextId(), sym, Side::BUY, OrderType::LIMIT,
                                       toFixedPrice(100.0), 0, ts);
    } else if (kind == 1) {
        // Negative/zero price on a limit
        return std::make_shared<Order>(nextId(), sym, Side::SELL, OrderType::LIMIT,
                                       0, 100, ts);
    } else {
        // Unknown symbol
        return std::make_shared<Order>(nextId(), "INVALID", Side::BUY, OrderType::LIMIT,
                                       toFixedPrice(100.0), 100, ts);
    }
}

} // namespace astra

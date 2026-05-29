#include "feed/MarketDataFeed.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace astra {

void MarketDataFeed::publish(const OrderBook& book, const std::vector<Trade>& newTrades, size_t depth) {
    printBook(book, depth);
    printTrades(newTrades);
}

void MarketDataFeed::printBook(const OrderBook& book, size_t depth) {
    auto bids = book.topBids(depth);
    auto asks = book.topAsks(depth);

    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    auto spd = book.spread();

    std::cout << "\n=== " << book.symbol() << " Order Book ===\n";
    std::cout << std::left
              << std::setw(22) << "BIDS"
              << "ASKS\n";
    std::cout << std::string(44, '-') << "\n";

    size_t rows = std::max(bids.size(), asks.size());
    for (size_t i = 0; i < rows; ++i) {
        std::ostringstream lhs, rhs;
        if (i < bids.size()) {
            lhs << std::fixed << std::setprecision(2)
                << fromFixedPrice(bids[i].price)
                << " x " << bids[i].qty;
        }
        if (i < asks.size()) {
            rhs << std::fixed << std::setprecision(2)
                << fromFixedPrice(asks[i].price)
                << " x " << asks[i].qty;
        }
        std::cout << std::left << std::setw(22) << lhs.str() << rhs.str() << "\n";
    }

    std::cout << std::string(44, '-') << "\n";
    if (bid) std::cout << "BEST_BID " << std::fixed << std::setprecision(2) << fromFixedPrice(*bid) << "\n";
    if (ask) std::cout << "BEST_ASK " << std::fixed << std::setprecision(2) << fromFixedPrice(*ask) << "\n";
    if (spd) std::cout << "SPREAD   " << std::fixed << std::setprecision(2) << fromFixedPrice(*spd) << "\n";
}

void MarketDataFeed::printTrades(const std::vector<Trade>& trades) {
    for (auto& t : trades) {
        std::cout << "TRADE " << t.symbol
                  << " price=" << std::fixed << std::setprecision(2) << fromFixedPrice(t.price)
                  << " qty=" << t.quantity
                  << " maker=" << t.makerOrderId
                  << " taker=" << t.takerOrderId << "\n";
    }
}

} // namespace astra

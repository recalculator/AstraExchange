#include "benchmark/ReplayEngine.hpp"
#include "parser/CsvParser.hpp"
#include "feed/MarketDataFeed.hpp"
#include <iostream>

namespace astra {

ReplayEngine::ReplayEngine(std::vector<Symbol> symbols)
    : log_([](const Event&){}) // silent by default; caller can override
{
    for (auto& s : symbols) engine_.registerSymbol(s);
}

ReplayStats ReplayEngine::run(const ReplayConfig& cfg) {
    auto parsed = CsvParser::parseFile(cfg.csvPath);
    if (!parsed.errors.empty() && parsed.orders.empty()) {
        std::cerr << "Parse errors:\n";
        for (auto& e : parsed.errors)
            std::cerr << "  line " << e.line << ": " << e.message << "\n";
    }

    LatencyTimer timer;
    timer.reserve(parsed.orders.size());

    ReplayStats stats{};
    stats.ordersSubmitted = parsed.orders.size();

    MarketDataFeed feed;

    auto wallStart = std::chrono::high_resolution_clock::now();

    for (auto& order : parsed.orders) {
        timer.start();
        auto result = engine_.submitOrder(order);
        timer.stop();

        if (result.status == OrderStatus::REJECTED) {
            ++stats.ordersRejected;
            if (cfg.verbose)
                std::cerr << "REJECT id=" << order->id << " " << result.rejectReason << "\n";
        } else {
            ++stats.ordersAccepted;
        }

        stats.tradeCount += result.trades.size();

        if (cfg.printTrades) {
            for (auto& t : result.trades)
                std::cout << "TRADE " << t.symbol
                          << " price=" << fromFixedPrice(t.price)
                          << " qty=" << t.quantity << "\n";
        }

        if (cfg.printBook && !result.trades.empty()) {
            if (auto* book = engine_.book(order->symbol))
                feed.publish(*book, result.trades, cfg.bookDepth);
        }
    }

    auto wallEnd = std::chrono::high_resolution_clock::now();
    auto wallNs  = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallStart).count()
    );

    stats.latency = timer.compute(wallNs);
    return stats;
}

} // namespace astra

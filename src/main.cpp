#include "benchmark/ReplayEngine.hpp"
#include "core/MatchingEngine.hpp"
#include "feed/MarketDataFeed.hpp"
#include "feed/EventLog.hpp"
#include "parser/CsvParser.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

static void printUsage(const char* argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << " --replay <orders.csv> [options]\n"
              << "  " << argv0 << " --interactive\n"
              << "\nOptions:\n"
              << "  --symbols SYM1,SYM2,...   Registered symbols (default: AAPL,MSFT,TSLA,NVDA)\n"
              << "  --trades                  Print each trade\n"
              << "  --book                    Print book after each trade\n"
              << "  --verbose                 Print rejected orders\n";
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0, pos;
    while ((pos = s.find(',', start)) != std::string::npos) {
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(argv[0]); return 1; }

    std::string csvPath;
    std::vector<std::string> symbols = {"AAPL", "MSFT", "TSLA", "NVDA"};
    bool doReplay = false, doInteractive = false;
    bool printTrades = false, printBook = false, verbose = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            doReplay = true;
            csvPath = argv[++i];
        } else if (std::strcmp(argv[i], "--interactive") == 0) {
            doInteractive = true;
        } else if (std::strcmp(argv[i], "--symbols") == 0 && i + 1 < argc) {
            symbols = splitComma(argv[++i]);
        } else if (std::strcmp(argv[i], "--trades") == 0) {
            printTrades = true;
        } else if (std::strcmp(argv[i], "--book") == 0) {
            printBook = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    if (doReplay) {
        astra::ReplayConfig cfg;
        cfg.csvPath     = csvPath;
        cfg.printTrades = printTrades;
        cfg.printBook   = printBook;
        cfg.verbose     = verbose;

        astra::ReplayEngine engine(symbols);
        auto stats = engine.run(cfg);

        std::cout << "\n=== Replay Complete ===\n"
                  << "Orders submitted : " << stats.ordersSubmitted << "\n"
                  << "Orders accepted  : " << stats.ordersAccepted  << "\n"
                  << "Orders rejected  : " << stats.ordersRejected  << "\n"
                  << "Trades generated : " << stats.tradeCount      << "\n";

        std::cout << "\n--- Matching Latency ---\n"
                  << "Mean : " << stats.latency.mean / 1e3 << " μs\n"
                  << "p50  : " << stats.latency.p50  / 1e3 << " μs\n"
                  << "p95  : " << stats.latency.p95  / 1e3 << " μs\n"
                  << "p99  : " << stats.latency.p99  / 1e3 << " μs\n"
                  << "max  : " << stats.latency.max  / 1e3 << " μs\n"
                  << "Throughput : " << stats.latency.throughputOpsPerSec / 1000 << " K orders/sec\n";
        return 0;
    }

    if (doInteractive) {
        astra::MatchingEngine engine;
        for (auto& s : symbols) engine.registerSymbol(s);
        astra::EventLog log;
        astra::MarketDataFeed feed;

        std::cout << "AstraExchange interactive mode. Enter CSV lines:\n"
                  << "  timestamp,order_id,symbol,side,type,price,quantity\n"
                  << "  (type QUIT to exit)\n";

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "QUIT" || line == "quit") break;
            auto order = astra::CsvParser::parseLine(line, 0);
            if (!order) { std::cerr << "parse error\n"; continue; }

            auto result = engine.submitOrder(*order);
            for (auto& t : result.trades) log.logTrade(t);

            if (auto* book = engine.book((*order)->symbol)) {
                feed.publish(*book, result.trades);
            }
        }
        return 0;
    }

    printUsage(argv[0]);
    return 1;
}

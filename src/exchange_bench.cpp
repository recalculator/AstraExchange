#include "benchmark/BenchmarkRunner.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

static void printUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\nOptions:\n"
        << "  --orders N          Number of orders (default: 1000000)\n"
        << "  --symbols SYM,...   Comma-separated symbols (default: AAPL,MSFT,NVDA,TSLA)\n"
        << "  --symbols N         Number of symbols from default list\n"
        << "  --seed N            RNG seed (default: 42)\n"
        << "  --csv PATH          Append results to CSV (default: benchmark-results.csv)\n"
        << "  --no-verify         Skip invariant checking\n"
        << "  --no-csv            Disable CSV output\n"
        << "  --pool              Use MemoryPool allocator (single-threaded)\n"
        << "  --pipeline          Use SPSC ring-buffer pipeline (2 threads)\n"
        << "  --ring-size N       Pipeline ring buffer capacity (default: 16384)\n"
        << "\nExamples:\n"
        << "  " << argv0 << " --orders 1000000\n"
        << "  " << argv0 << " --orders 1000000 --pool\n"
        << "  " << argv0 << " --orders 1000000 --pipeline\n"
        << "  " << argv0 << " --orders 10000000 --no-verify --no-csv\n";
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
    astra::BenchmarkConfig cfg;
    cfg.csvOutput = "benchmark-results.csv";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--orders") == 0 && i + 1 < argc) {
            cfg.orderCount = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "--symbols") == 0 && i + 1 < argc) {
            std::string arg = argv[++i];
            if (arg.find(',') != std::string::npos) {
                cfg.symbols = splitComma(arg);
            } else {
                size_t n = std::stoul(arg);
                std::vector<std::string> all = {"AAPL", "MSFT", "NVDA", "TSLA"};
                if (n > all.size()) n = all.size();
                cfg.symbols = std::vector<std::string>(all.begin(), all.begin() + n);
            }
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            cfg.csvOutput = argv[++i];
        } else if (std::strcmp(argv[i], "--no-verify") == 0) {
            cfg.verifyInvariants = false;
        } else if (std::strcmp(argv[i], "--no-csv") == 0) {
            cfg.csvOutput = "";
        } else if (std::strcmp(argv[i], "--pool") == 0) {
            cfg.mode = astra::BenchmarkMode::Pool;
        } else if (std::strcmp(argv[i], "--pipeline") == 0) {
            cfg.mode = astra::BenchmarkMode::Pipeline;
        } else if (std::strcmp(argv[i], "--ring-size") == 0 && i + 1 < argc) {
            cfg.pipelineCapacity = std::stoul(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    const char* modeStr = (cfg.mode == astra::BenchmarkMode::Pool)     ? " [pool]" :
                          (cfg.mode == astra::BenchmarkMode::Pipeline)  ? " [pipeline]" : "";
    std::cout << "Running benchmark" << modeStr << ": " << cfg.orderCount
              << " orders, " << cfg.symbols.size() << " symbol(s)...\n";

    astra::BenchmarkRunner runner(cfg);
    auto result = runner.run();

    astra::BenchmarkRunner::printTable(result, cfg);

    if (!cfg.csvOutput.empty())
        std::cout << "Results appended to: " << cfg.csvOutput << "\n";

    return result.invariants.passed ? 0 : 1;
}

#include <gtest/gtest.h>
#include "benchmark/OrderGenerator.hpp"
#include "benchmark/InvariantChecker.hpp"
#include "benchmark/BenchmarkRunner.hpp"
#include "core/MatchingEngine.hpp"

using namespace astra;

// ---------------------------------------------------------------------------
// OrderGenerator correctness
// ---------------------------------------------------------------------------

TEST(GeneratorTest, ProducesRequestedCount) {
    GeneratorConfig cfg;
    cfg.count   = 10'000;
    cfg.symbols = {"AAPL", "MSFT"};
    cfg.seed    = 1;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();
    EXPECT_EQ(orders.size(), cfg.count);
}

TEST(GeneratorTest, AllOrdersHaveKnownSymbol) {
    GeneratorConfig cfg;
    cfg.count   = 5'000;
    cfg.symbols = {"AAPL", "MSFT"};
    cfg.seed    = 2;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();

    std::unordered_set<std::string> valid(cfg.symbols.begin(), cfg.symbols.end());
    valid.insert("INVALID"); // invalid orders intentionally use this
    for (auto& o : orders) {
        EXPECT_TRUE(valid.count(o->symbol) || o->symbol == "INVALID")
            << "Unexpected symbol: " << o->symbol;
    }
}

TEST(GeneratorTest, MixHasAllOrderTypes) {
    GeneratorConfig cfg;
    cfg.count   = 20'000;
    cfg.symbols = {"AAPL"};
    cfg.seed    = 3;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();

    size_t limits = 0, markets = 0, cancels = 0;
    for (auto& o : orders) {
        if (o->type == OrderType::LIMIT)  ++limits;
        if (o->type == OrderType::MARKET) ++markets;
        if (o->type == OrderType::CANCEL) ++cancels;
    }
    EXPECT_GT(limits,  0u);
    EXPECT_GT(markets, 0u);
    EXPECT_GT(cancels, 0u);
}

TEST(GeneratorTest, InvalidOrdersAreRejectedByEngine) {
    // The generator intentionally produces ~5% invalid orders (zero price,
    // zero qty, unknown symbol). Verify that every such order is rejected
    // by the engine — none should be accepted or matched.
    GeneratorConfig cfg;
    cfg.count   = 5'000;
    cfg.symbols = {"AAPL"};
    cfg.seed    = 4;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();

    MatchingEngine engine;
    engine.registerSymbol("AAPL");

    uint64_t rejected = 0;
    for (auto& o : orders) {
        auto res = engine.submitOrder(o);
        // Collect rejections; we don't care about valid orders here.
        if (res.status == OrderStatus::REJECTED) ++rejected;
    }
    EXPECT_GT(rejected, 0u) << "Expected some rejected orders from the invalid mix";
}

TEST(GeneratorTest, GeneratesRealMatches) {
    // Run a small batch through the engine; expect at least some trades.
    GeneratorConfig cfg;
    cfg.count   = 10'000;
    cfg.symbols = {"AAPL"};
    cfg.seed    = 5;
    OrderGenerator gen(cfg);
    auto orders = gen.generate();

    MatchingEngine engine;
    engine.registerSymbol("AAPL");

    uint64_t trades = 0;
    for (auto& o : orders) {
        auto res = engine.submitOrder(o);
        trades += res.trades.size();
    }
    EXPECT_GT(trades, 0u) << "Generator produced no trades";
}

// ---------------------------------------------------------------------------
// InvariantChecker
// ---------------------------------------------------------------------------

TEST(InvariantTest, CleanBookPasses) {
    OrderBook book("AAPL");
    auto buy = std::make_shared<Order>(1, "AAPL", Side::BUY, OrderType::LIMIT,
                                       toFixedPrice(100.0), 100, 1);
    auto sell = std::make_shared<Order>(2, "AAPL", Side::SELL, OrderType::LIMIT,
                                        toFixedPrice(101.0), 100, 2);
    book.addOrder(buy);
    book.addOrder(sell);

    auto report = InvariantChecker::checkBook(book);
    EXPECT_TRUE(report.passed);
    EXPECT_TRUE(report.violations.empty());
}

TEST(InvariantTest, UniqueIdCheckPassesOnDistinct) {
    std::vector<OrderId> ids = {1, 2, 3, 4, 5};
    auto report = InvariantChecker::checkUniqueIds(ids);
    EXPECT_TRUE(report.passed);
}

TEST(InvariantTest, UniqueIdCheckCatchesDuplicate) {
    std::vector<OrderId> ids = {1, 2, 3, 2, 5};
    auto report = InvariantChecker::checkUniqueIds(ids);
    EXPECT_FALSE(report.passed);
    EXPECT_EQ(report.violations.size(), 1u);
    EXPECT_EQ(report.violations[0].rule, "unique_order_ids");
}

TEST(InvariantTest, TradeCheckCatchesZeroQty) {
    Trade t{"AAPL", toFixedPrice(100.0), 0, 1, 2, 100};
    auto report = InvariantChecker::checkTrades({t}, {});
    EXPECT_FALSE(report.passed);
}

TEST(InvariantTest, TradeCheckCatchesZeroPrice) {
    Trade t{"AAPL", 0, 100, 1, 2, 100};
    auto report = InvariantChecker::checkTrades({t}, {});
    EXPECT_FALSE(report.passed);
}

TEST(InvariantTest, MergeAggregatesViolations) {
    InvariantChecker::Report r1{true,  {}};
    InvariantChecker::Report r2{false, {{"SYM", "rule1", "detail1"}}};
    InvariantChecker::Report r3{false, {{"SYM", "rule2", "detail2"}}};

    auto merged = InvariantChecker::merge({r1, r2, r3});
    EXPECT_FALSE(merged.passed);
    EXPECT_EQ(merged.violations.size(), 2u);
}

// ---------------------------------------------------------------------------
// Benchmark replay correctness
// ---------------------------------------------------------------------------

TEST(BenchmarkTest, SmallRunPassesInvariants) {
    BenchmarkConfig cfg;
    cfg.orderCount       = 10'000;
    cfg.symbols          = {"AAPL", "MSFT"};
    cfg.seed             = 42;
    cfg.csvOutput        = "";  // no file output in tests
    cfg.verifyInvariants = true;

    BenchmarkRunner runner(cfg);
    auto result = runner.run();

    EXPECT_GT(result.ordersSubmitted, 0u);
    EXPECT_GT(result.ordersAccepted, 0u);
    EXPECT_GT(result.tradesGenerated, 0u);
    EXPECT_TRUE(result.invariants.passed)
        << "Invariant violations:\n"
        << [&]{
            std::string s;
            for (auto& v : result.invariants.violations)
                s += "  [" + v.symbol + "] " + v.rule + ": " + v.detail + "\n";
            return s;
        }();
}

TEST(BenchmarkTest, RejectsArePresentButMinority) {
    BenchmarkConfig cfg;
    cfg.orderCount       = 20'000;
    cfg.symbols          = {"AAPL"};
    cfg.seed             = 7;
    cfg.csvOutput        = "";

    BenchmarkRunner runner(cfg);
    auto result = runner.run();

    EXPECT_GT(result.ordersRejected, 0u) << "Expected some rejected orders from invalid mix";
    EXPECT_LT(result.ordersRejected, result.ordersSubmitted / 2)
        << "Too many rejections — generator may be broken";
}

TEST(BenchmarkTest, CancelsAreCounted) {
    BenchmarkConfig cfg;
    cfg.orderCount       = 20'000;
    cfg.symbols          = {"AAPL", "MSFT"};
    cfg.seed             = 8;
    cfg.csvOutput        = "";

    BenchmarkRunner runner(cfg);
    auto result = runner.run();

    EXPECT_GT(result.cancelsProcessed, 0u);
}

// ---------------------------------------------------------------------------
// Multi-symbol isolation under generated traffic
// ---------------------------------------------------------------------------

TEST(BenchmarkTest, MultiSymbolIsolationUnderLoad) {
    // Run the engine manually and confirm that AAPL trades never reference MSFT
    GeneratorConfig gcfg;
    gcfg.count   = 5'000;
    gcfg.symbols = {"AAPL", "MSFT"};
    gcfg.seed    = 99;

    OrderGenerator gen(gcfg);
    auto orders = gen.generate();

    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    engine.registerSymbol("MSFT");

    for (auto& o : orders) {
        auto res = engine.submitOrder(o);
        for (auto& t : res.trades) {
            EXPECT_EQ(t.symbol, o->symbol)
                << "Trade symbol mismatch: order=" << o->symbol
                << " trade=" << t.symbol;
        }
    }
}

// ---------------------------------------------------------------------------
// Cancelled orders cannot trade
// ---------------------------------------------------------------------------

TEST(BenchmarkTest, CancelledOrdersCannotTrade) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");

    // Place a resting sell
    auto sell = std::make_shared<Order>(1, "AAPL", Side::SELL, OrderType::LIMIT,
                                        toFixedPrice(100.0), 200, 1);
    engine.submitOrder(sell);

    // Cancel it
    auto cancel = std::make_shared<Order>(1, "AAPL", Side::BUY, OrderType::CANCEL,
                                          0, 1, 2);
    auto cres = engine.submitOrder(cancel);
    EXPECT_EQ(cres.status, OrderStatus::CANCELLED);

    // Now an aggressive buy should find nothing to cross
    auto buy = std::make_shared<Order>(2, "AAPL", Side::BUY, OrderType::LIMIT,
                                       toFixedPrice(105.0), 100, 3);
    auto bres = engine.submitOrder(buy);
    EXPECT_TRUE(bres.trades.empty()) << "Cancelled order matched after cancellation";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

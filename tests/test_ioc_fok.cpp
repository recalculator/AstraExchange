#include <gtest/gtest.h>
#include "core/OrderBook.hpp"
#include "core/MatchingEngine.hpp"

using namespace astra;

static std::shared_ptr<Order> makeOrder(OrderId id, const std::string& sym,
                                        Side side, OrderType type,
                                        double price, Quantity qty) {
    return std::make_shared<Order>(id, sym, side, type,
                                   toFixedPrice(price), qty, id);
}

// ============================================================
// IOC Tests
// ============================================================

TEST(IocTest, FullyFills) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 200));

    auto ioc = makeOrder(2, "AAPL", Side::BUY, OrderType::IOC, 100.0, 150);
    auto trades = book.addOrder(ioc);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 150u);
    EXPECT_EQ(ioc->status, OrderStatus::FILLED);
    EXPECT_FALSE(book.hasOrder(2)); // must NOT rest
}

TEST(IocTest, PartiallyFillsAndCancelsRemainder) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50));

    auto ioc = makeOrder(2, "AAPL", Side::BUY, OrderType::IOC, 100.0, 200);
    auto trades = book.addOrder(ioc);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 50u);
    EXPECT_EQ(ioc->status, OrderStatus::PARTIALLY_FILLED);
    EXPECT_FALSE(book.hasOrder(2)); // remainder must NOT rest in book
    EXPECT_FALSE(book.bestBid().has_value()); // no resting bid
}

TEST(IocTest, NoLiquidityAtLimitCancels) {
    OrderBook book("AAPL");
    // Only asks above the IOC limit price
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 110.0, 100));

    auto ioc = makeOrder(2, "AAPL", Side::BUY, OrderType::IOC, 100.0, 50);
    auto trades = book.addOrder(ioc);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(ioc->status, OrderStatus::CANCELLED);
    EXPECT_FALSE(book.hasOrder(2));
}

TEST(IocTest, NeverRestsInBook) {
    OrderBook book("AAPL");
    // No ask side at all
    auto ioc = makeOrder(1, "AAPL", Side::BUY, OrderType::IOC, 100.0, 100);
    book.addOrder(ioc);

    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.hasOrder(1));
}

TEST(IocTest, SellSideIoc) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 300));

    auto ioc = makeOrder(2, "AAPL", Side::SELL, OrderType::IOC, 100.0, 100);
    auto trades = book.addOrder(ioc);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_EQ(ioc->status, OrderStatus::FILLED);
    EXPECT_FALSE(book.hasOrder(2));
}

TEST(IocTest, PriceTimePriorityRespected) {
    OrderBook book("AAPL");
    // Two resting sells at the same price — time priority must hold
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 40));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 40));

    auto ioc = makeOrder(3, "AAPL", Side::BUY, OrderType::IOC, 100.0, 40);
    auto trades = book.addOrder(ioc);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].makerOrderId, 1u); // earliest order matched first
}

// ============================================================
// FOK Tests
// ============================================================

TEST(FokTest, FullyFills) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 500));

    auto fok = makeOrder(2, "AAPL", Side::BUY, OrderType::FOK, 100.0, 200);
    auto trades = book.addOrder(fok);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 200u);
    EXPECT_EQ(fok->status, OrderStatus::FILLED);
    EXPECT_FALSE(book.hasOrder(2));
}

TEST(FokTest, InsufficientLiquidityRejected) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50));

    auto fok = makeOrder(2, "AAPL", Side::BUY, OrderType::FOK, 100.0, 100);
    auto trades = book.addOrder(fok);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(fok->status, OrderStatus::CANCELLED);
    // Existing resting sell must NOT have been touched
    EXPECT_TRUE(book.bestAsk().has_value());
    EXPECT_EQ(book.topAsks(1)[0].qty, 50u);
}

TEST(FokTest, NeverPartiallyFills) {
    OrderBook book("AAPL");
    // 80 available, FOK wants 100
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 80));

    auto fok = makeOrder(2, "AAPL", Side::BUY, OrderType::FOK, 100.0, 100);
    auto trades = book.addOrder(fok);

    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(fok->filledQty, 0u); // not partially filled
    EXPECT_EQ(fok->status, OrderStatus::CANCELLED);
}

TEST(FokTest, NeverRestsInBook) {
    OrderBook book("AAPL");
    auto fok = makeOrder(1, "AAPL", Side::BUY, OrderType::FOK, 100.0, 100);
    book.addOrder(fok);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(FokTest, FillsAcrossMultiplePriceLevels) {
    OrderBook book("AAPL");
    // Two ask levels, FOK qty spans both
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 60));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 101.0, 60));

    auto fok = makeOrder(3, "AAPL", Side::BUY, OrderType::FOK, 101.0, 120);
    auto trades = book.addOrder(fok);

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].quantity + trades[1].quantity, 120u);
    EXPECT_EQ(fok->status, OrderStatus::FILLED);
    EXPECT_FALSE(book.hasOrder(3));
}

TEST(FokTest, SellSideFok) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 300));

    auto fok = makeOrder(2, "AAPL", Side::SELL, OrderType::FOK, 100.0, 300);
    auto trades = book.addOrder(fok);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 300u);
    EXPECT_EQ(fok->status, OrderStatus::FILLED);
}

TEST(FokTest, PriceTimePriorityRespected) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50));

    // FOK fills both — first trade must be against order 1 (time priority)
    auto fok = makeOrder(3, "AAPL", Side::BUY, OrderType::FOK, 100.0, 100);
    auto trades = book.addOrder(fok);

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].makerOrderId, 1u);
    EXPECT_EQ(trades[1].makerOrderId, 2u);
}

TEST(FokTest, InvariantsHoldAfterFokFail) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50));
    book.addOrder(makeOrder(2, "AAPL", Side::BUY, OrderType::LIMIT, 95.0, 50));

    // FOK that fails — book must remain uncrossed and unchanged
    auto fok = makeOrder(3, "AAPL", Side::BUY, OrderType::FOK, 100.0, 200);
    book.addOrder(fok);

    EXPECT_TRUE(book.checkInvariants());
    EXPECT_EQ(book.topAsks(1)[0].qty, 50u); // untouched
    EXPECT_EQ(book.topBids(1)[0].qty, 50u); // untouched
}

// ============================================================
// Engine-level IOC/FOK routing
// ============================================================

TEST(IocFokEngineTest, IocRoutedCorrectly) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    engine.submitOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 100));

    auto ioc = makeOrder(2, "AAPL", Side::BUY, OrderType::IOC, 100.0, 50);
    auto result = engine.submitOrder(ioc);
    EXPECT_EQ(result.trades.size(), 1u);
    EXPECT_NE(result.status, OrderStatus::REJECTED);
}

TEST(IocFokEngineTest, FokRoutedCorrectly) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    engine.submitOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 100));

    auto fok = makeOrder(2, "AAPL", Side::BUY, OrderType::FOK, 100.0, 50);
    auto result = engine.submitOrder(fok);
    EXPECT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.status, OrderStatus::FILLED);
}

TEST(IocFokEngineTest, FokFailReturnsReason) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    // No liquidity
    auto fok = makeOrder(1, "AAPL", Side::BUY, OrderType::FOK, 100.0, 50);
    auto result = engine.submitOrder(fok);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.status, OrderStatus::CANCELLED);
    EXPECT_FALSE(result.rejectReason.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#include <gtest/gtest.h>
#include "core/OrderBook.hpp"
#include "core/MatchingEngine.hpp"

using namespace astra;

static std::shared_ptr<Order> makeOrder(OrderId id, const std::string& sym,
                                        Side side, OrderType type,
                                        double price, Quantity qty) {
    return std::make_shared<Order>(id, sym, side, type,
                                   toFixedPrice(price), qty, id /*ts*/);
}

// --- PriceLevel tests ---

TEST(PriceLevelTest, AddAndFront) {
    PriceLevel level(toFixedPrice(100.0));
    auto o = makeOrder(1, "X", Side::BUY, OrderType::LIMIT, 100.0, 50);
    level.addOrder(o);
    EXPECT_EQ(level.front()->id, 1u);
    EXPECT_EQ(level.totalQty(), 50u);
}

TEST(PriceLevelTest, TimeOrderPreserved) {
    PriceLevel level(toFixedPrice(100.0));
    level.addOrder(makeOrder(1, "X", Side::BUY, OrderType::LIMIT, 100.0, 10));
    level.addOrder(makeOrder(2, "X", Side::BUY, OrderType::LIMIT, 100.0, 20));
    EXPECT_EQ(level.front()->id, 1u); // first in
    level.popFront();
    EXPECT_EQ(level.front()->id, 2u);
}

TEST(PriceLevelTest, RemoveById) {
    PriceLevel level(toFixedPrice(100.0));
    level.addOrder(makeOrder(1, "X", Side::BUY, OrderType::LIMIT, 100.0, 10));
    level.addOrder(makeOrder(2, "X", Side::BUY, OrderType::LIMIT, 100.0, 20));
    EXPECT_TRUE(level.removeOrder(1));
    EXPECT_EQ(level.front()->id, 2u);
    EXPECT_FALSE(level.removeOrder(99));
}

// --- OrderBook basic tests ---

TEST(OrderBookTest, LimitBuyRestsWhenNoAsk) {
    OrderBook book("AAPL");
    auto o = makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 100);
    auto trades = book.addOrder(o);
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.bestBid().has_value());
    EXPECT_EQ(*book.bestBid(), toFixedPrice(100.0));
}

TEST(OrderBookTest, LimitSellRestsWhenNoBid) {
    OrderBook book("AAPL");
    auto o = makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 100);
    auto trades = book.addOrder(o);
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.bestAsk().has_value());
}

TEST(OrderBookTest, BuyCrossesAsk) {
    OrderBook book("AAPL");
    auto sell = makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 100);
    book.addOrder(sell);

    auto buy = makeOrder(2, "AAPL", Side::BUY, OrderType::LIMIT, 51.0, 40);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 40u);
    EXPECT_EQ(trades[0].price, toFixedPrice(50.0)); // executes at maker price
}

TEST(OrderBookTest, SellCrossesBid) {
    OrderBook book("AAPL");
    auto buy = makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 200);
    book.addOrder(buy);

    auto sell = makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 99.0, 50);
    auto trades = book.addOrder(sell);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 50u);
    EXPECT_EQ(trades[0].price, toFixedPrice(100.0));
}

TEST(OrderBookTest, PartialFill) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 30));

    auto buy = makeOrder(2, "AAPL", Side::BUY, OrderType::LIMIT, 50.0, 100);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 30u); // only 30 available

    // Remaining 70 should rest as bid
    EXPECT_TRUE(book.bestBid().has_value());
    EXPECT_EQ(book.topBids(1)[0].qty, 70u);
}

TEST(OrderBookTest, FullFill) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 100));

    auto buy = makeOrder(2, "AAPL", Side::BUY, OrderType::LIMIT, 50.0, 100);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 100u);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST(OrderBookTest, MultipleFills) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 40));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 60));

    auto buy = makeOrder(3, "AAPL", Side::BUY, OrderType::LIMIT, 50.0, 100);
    auto trades = book.addOrder(buy);

    EXPECT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].quantity + trades[1].quantity, 100u);
}

TEST(OrderBookTest, PricePriority) {
    OrderBook book("AAPL");
    // Lower ask should match first
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 52.0, 50));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 50));

    auto buy = makeOrder(3, "AAPL", Side::BUY, OrderType::LIMIT, 53.0, 30);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, toFixedPrice(50.0)); // matched at best ask
    EXPECT_EQ(trades[0].makerOrderId, 2u);
}

TEST(OrderBookTest, TimePriority) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 30));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 30));

    auto buy = makeOrder(3, "AAPL", Side::BUY, OrderType::LIMIT, 50.0, 30);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].makerOrderId, 1u); // order 1 is earlier
}

TEST(OrderBookTest, Cancellation) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 200));
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(OrderBookTest, CancelledOrderCannotTrade) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 100));
    book.cancelOrder(1);

    auto buy = makeOrder(2, "AAPL", Side::BUY, OrderType::LIMIT, 55.0, 100);
    auto trades = book.addOrder(buy);
    EXPECT_TRUE(trades.empty());
}

TEST(OrderBookTest, MarketOrder) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 100));

    auto buy = makeOrder(2, "AAPL", Side::BUY, OrderType::MARKET, 0.0, 60);
    auto trades = book.addOrder(buy);

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 60u);
}

TEST(OrderBookTest, InvalidCancelReturnsfalse) {
    OrderBook book("AAPL");
    EXPECT_FALSE(book.cancelOrder(999));
}

TEST(OrderBookTest, Invariants) {
    OrderBook book("AAPL");
    book.addOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 100));
    book.addOrder(makeOrder(2, "AAPL", Side::SELL, OrderType::LIMIT, 101.0, 50));
    EXPECT_TRUE(book.checkInvariants());

    // After a cross
    book.addOrder(makeOrder(3, "AAPL", Side::BUY, OrderType::LIMIT, 101.0, 50));
    EXPECT_TRUE(book.checkInvariants());
}

// --- MatchingEngine tests ---

TEST(MatchingEngineTest, RejectsUnknownSymbol) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    auto o = makeOrder(1, "GOOG", Side::BUY, OrderType::LIMIT, 100.0, 10);
    auto result = engine.submitOrder(o);
    EXPECT_EQ(result.status, OrderStatus::REJECTED);
}

TEST(MatchingEngineTest, RejectsDuplicateId) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    engine.submitOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 10));
    auto result = engine.submitOrder(makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 10));
    EXPECT_EQ(result.status, OrderStatus::REJECTED);
}

TEST(MatchingEngineTest, RejectsZeroQty) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    auto o = makeOrder(1, "AAPL", Side::BUY, OrderType::LIMIT, 100.0, 0);
    auto result = engine.submitOrder(o);
    EXPECT_EQ(result.status, OrderStatus::REJECTED);
}

TEST(MatchingEngineTest, MultiSymbolIsolation) {
    MatchingEngine engine;
    engine.registerSymbol("AAPL");
    engine.registerSymbol("MSFT");

    engine.submitOrder(makeOrder(1, "AAPL", Side::SELL, OrderType::LIMIT, 50.0, 100));
    engine.submitOrder(makeOrder(2, "MSFT", Side::SELL, OrderType::LIMIT, 300.0, 100));

    // AAPL buy should only match AAPL book
    auto result = engine.submitOrder(makeOrder(3, "AAPL", Side::BUY, OrderType::LIMIT, 50.0, 30));
    EXPECT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].symbol, "AAPL");

    auto* msft = engine.book("MSFT");
    ASSERT_NE(msft, nullptr);
    EXPECT_TRUE(msft->bestAsk().has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

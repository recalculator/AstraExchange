# AstraExchange

A high-performance limit order book and matching engine written in C++17.
Designed as a realistic exchange simulator — not a toy, not a claim to production readiness.

---

## Architecture

```
CSV / CLI / Synthetic Generator
            │
            ▼
      Order Parser
            │
            ▼
     Risk Validator
     (qty, price, symbol, duplicate ID)
            │
            ▼
    Matching Engine
    (multi-symbol dispatch)
            │
     ┌──────┴──────┐
     ▼             ▼
OrderBook        OrderBook
 (AAPL)          (MSFT) ...
     │
  ┌──┴──────────────────┐
  │  Bid side           │  Ask side
  │  map<Price,Level>   │  map<Price,Level>
  │  (descending)       │  (ascending)
  │  deque<Order>       │  deque<Order>
  │  per level          │  per level
  └──────────────────────┘
            │
     ┌──────┴──────┐
     ▼             ▼
 Trade Feed    Market Data
 (EventLog)     (terminal)
            │
     ┌──────┴──────┐
     ▼             ▼
 Benchmark     Invariant
  Runner        Checker
```

### Key data structures

| Structure | Purpose | Complexity |
|-----------|---------|------------|
| `std::map<Price, PriceLevel, std::greater>` | Bid side — best bid at `begin()` | O(log N) insert/erase |
| `std::map<Price, PriceLevel>` | Ask side — best ask at `begin()` | O(log N) insert/erase |
| `std::deque<Order>` per level | Time priority within a price level | O(1) front/pop |
| `std::unordered_map<OrderId, Location>` | O(1) cancel by ID | O(1) avg |

**Price representation:** All prices are stored as 64-bit fixed-point integers (×10,000) to eliminate floating-point comparison errors.

---

## Building

**Requirements:** C++17 compiler, CMake ≥ 3.16, GoogleTest (optional for tests)

```bash
# macOS
brew install cmake googletest

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# Executables
./build/exchange        # CSV replay + interactive mode
./build/exchange_bench  # Synthetic benchmark runner
./build/astra_tests     # Unit tests (core engine)
./build/astra_bench_tests  # Benchmark/generator tests
```

---

## Running

### CSV replay

```bash
./build/exchange --replay data/sample_orders.csv --trades --book
```

CSV format:
```
timestamp,order_id,symbol,side,type,price,quantity
1,1001,AAPL,BUY,LIMIT,150.25,100
2,1002,AAPL,SELL,LIMIT,150.20,50
3,1003,AAPL,BUY,MARKET,0,40
4,1004,AAPL,BUY,LIMIT,1001,CANCEL,0,1
```

Flags:
- `--trades` — print each trade as it executes
- `--book` — print the order book after each trade
- `--verbose` — print rejected orders
- `--symbols AAPL,MSFT,...` — registered symbols (default: AAPL,MSFT,NVDA,TSLA)

### Interactive mode

```bash
./build/exchange --interactive
# Then type CSV lines, QUIT to exit
```

### Benchmark

```bash
# 100K orders
./build/exchange_bench --orders 100000

# 1M orders, 4 symbols
./build/exchange_bench --orders 1000000 --symbols 4

# 10M orders, no invariant check, no CSV
./build/exchange_bench --orders 10000000 --no-verify --no-csv

# Reproducible with custom seed
./build/exchange_bench --orders 1000000 --seed 123
```

Benchmark results are appended to `benchmark-results.csv` by default.

---

## Running tests

```bash
./build/astra_tests         # 21 unit tests
./build/astra_bench_tests   # 16 benchmark/generator tests
```

All 37 tests pass.

### What is tested

**Core engine (21 tests):**
- Limit buy rests when no ask exists
- Limit sell rests when no bid exists
- Buy crosses ask, sell crosses bid
- Partial fill, full fill, multiple fills
- Price priority (best price matches first)
- Time priority (earliest order at same price matches first)
- Cancellation removes orders from the book
- Cancelled orders cannot trade
- Market orders sweep available liquidity
- Invalid cancels return false
- Book invariants hold after every operation
- Engine rejects: unknown symbol, duplicate ID, zero quantity
- Multi-symbol isolation

**Benchmark / generator (16 tests):**
- Generator produces exact requested count
- All order types present in generated stream
- Invalid orders rejected by engine (not silently dropped)
- Trades generated across realistic mixed workload
- Multi-symbol isolation under generated traffic
- Cancelled orders cannot trade after removal
- Invariant checker catches duplicate IDs
- Invariant checker catches zero-qty trades
- Invariant checker catches zero-price trades
- Full 10K benchmark run passes all invariants

---

## Benchmark results

Measured on Apple M3 Pro, macOS 14, clang 15, `-O3`, single-threaded.

### 100,000 orders (4 symbols)

```
Orders submitted:    100,000
Orders accepted:      86,068
Orders rejected:      13,932
Cancels processed:     5,002
Trades generated:     59,811
Throughput:         3.82 M orders/sec
p50 latency:           0.17 μs
p95 latency:           0.50 μs
p99 latency:           1.04 μs
Max latency:         479.67 μs
Memory (RSS):          35.6 MB
Invariants:             PASS
```

### 1,000,000 orders (4 symbols)

```
Orders submitted:  1,000,000
Orders accepted:     850,964
Orders rejected:     149,036
Cancels processed:    49,845
Trades generated:    599,380
Throughput:         2.87 M orders/sec
p50 latency:           0.17 μs
p95 latency:           0.83 μs
p99 latency:           1.83 μs
Max latency:        4567.12 μs
Memory (RSS):         286.8 MB
Invariants:             PASS
```

**Observation:** Throughput decreases slightly at 1M vs 100K due to memory pressure (large deques, index growth). Max latency outliers are driven by occasional deep multi-level sweeps, not typical cases.

---

## Order mix (generated workload)

| Type | Fraction | Notes |
|------|----------|-------|
| Limit orders | 60% | 30% are aggressive (crossing) to generate trades |
| Market orders | 20% | Sweep available liquidity |
| Cancels | 15% | Target randomly sampled resting orders |
| Invalid | 5% | Zero qty, zero price, unknown symbol — all rejected |

---

## Correctness invariants verified after each run

1. **No crossed book** — best bid < best ask for all symbols
2. **No zero-quantity resting orders** in any price level
3. **No empty price levels** remain in the book
4. **Total resting volume is non-negative**
5. **Order IDs are globally unique** across all accepted orders
6. **All trades have positive price and non-zero quantity**
7. **Cancelled orders cannot match** subsequent incoming orders
8. **Book index is consistent** with resting orders (internal check)

---

## Honest limitations

- **Single-threaded.** The matching engine runs on one thread per process. This is intentional — deterministic order processing guarantees correctness. Multi-threaded ingestion (Phase 4) would require lock-free queues between parser and engine.

- **Memory is not pooled.** Orders are heap-allocated via `std::shared_ptr`. A memory pool or arena allocator (Phase 4) would reduce latency variance and improve cache locality.

- **std::map is not the fastest structure.** A real HFT system would use a flat array indexed by price tick offset. `std::map` gives O(log N) per price level lookup; a tick-indexed array gives O(1) but requires bounded price ranges.

- **Max latency outliers are real.** The 4.5 ms max at 1M orders reflects occasional deep multi-level sweeps (a market order consuming many price levels). p99 at 1.83 μs is the more representative tail figure.

- **This is not a network exchange.** There is no TCP stack, session layer, or FIX protocol. Orders are submitted in-process.

- **Benchmark hardware matters.** These numbers are from a laptop. A server with L3 cache tuning, CPU pinning, and NUMA awareness would show different characteristics.

---

## Project structure

```
AstraExchange/
├── include/
│   ├── core/
│   │   ├── Types.hpp          # Fixed-point price, OrderId, Timestamp typedefs
│   │   ├── Order.hpp          # Order struct
│   │   ├── Trade.hpp          # Trade struct
│   │   ├── PriceLevel.hpp     # deque<Order> at one price point
│   │   ├── OrderBook.hpp      # bid/ask maps + index + matching
│   │   ├── MatchingEngine.hpp # multi-symbol dispatcher
│   │   └── RiskValidator.hpp  # pre-trade checks
│   ├── parser/
│   │   └── CsvParser.hpp
│   ├── feed/
│   │   ├── EventLog.hpp       # pluggable event sink
│   │   └── MarketDataFeed.hpp # terminal book display
│   └── benchmark/
│       ├── LatencyTimer.hpp   # high-res timer + p50/p95/p99/max
│       ├── OrderGenerator.hpp # synthetic workload generator
│       ├── InvariantChecker.hpp
│       ├── BenchmarkRunner.hpp
│       └── ReplayEngine.hpp
├── src/                       # implementations mirror include/
├── tests/
│   ├── test_orderbook.cpp     # 21 core engine tests
│   └── test_benchmark.cpp     # 16 generator/benchmark tests
├── data/
│   └── sample_orders.csv
├── benchmark-results.csv      # auto-generated after bench runs
└── CMakeLists.txt
```

---

## Planned (Phase 4)

- Memory pool / slab allocator for `Order` objects
- Lock-free ring buffer between parser thread and engine thread
- Fill-or-kill and immediate-or-cancel order types
- Modify order support
- Tick-indexed price level array (O(1) best bid/ask)

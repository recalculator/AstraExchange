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
      (+ IOC/FOK types)
            │
         [optional]
    SPSC Ring Buffer
    (producer thread)
            │
            ▼  (consumer thread / same thread in baseline)
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
| `MemoryPool<T>` | Slab allocator for Order objects | O(1) alloc/dealloc |
| `SpscRingBuffer<T>` | Lock-free SPSC ingestion queue | O(1) push/pop |

**Price representation:** All prices are stored as 64-bit fixed-point integers (×10,000) to eliminate floating-point comparison errors.

---

## Phase 4 features

### 4A — Memory Pool / Slab Allocator (`include/utils/MemoryPool.hpp`)

A fixed-capacity slab allocator for objects of type `T`:

- Pre-allocates `N` raw aligned slots at construction — no per-object `malloc` during the matching hot path.
- Free-slot tracking via an explicit index-based freelist (`vector<uint32_t>`), avoiding the pointer-overlay approach that requires `sizeof(T) >= sizeof(void*)`.
- O(1) `alloc` and `dealloc`.
- Objects are constructed in-place (placement new) and explicitly destroyed on `dealloc`.
- Not thread-safe — matches the single-threaded engine model.

In benchmark `--pool` mode, each submitted `Order` is pool-allocated and wrapped in a `shared_ptr` with a custom deleter that returns the slot on release. This eliminates the `tcmalloc`/system-allocator round-trip per order.

**Honest result:** At 1M orders, pool mode shows slightly lower throughput than baseline (2.89M vs 3.02M orders/sec). The pool eliminates per-order `malloc`, but the `shared_ptr` with custom deleter still incurs control-block overhead, and the pool's `vector<uint32_t>` freelist is itself heap-managed. The real benefit would come from replacing `shared_ptr<Order>` throughout with raw pool-owned pointers — an invasive change that would require rewriting `PriceLevel` and `OrderBook`. That's a meaningful Phase 5 task, not a one-line swap.

### 4B — Lock-Free SPSC Ring Buffer (`include/utils/SpscRingBuffer.hpp`)

A single-producer/single-consumer ring buffer for the parser-to-engine pipeline:

- Fixed capacity (rounded up to next power-of-two internally).
- Lock-free: no mutexes. Uses `std::atomic<size_t>` head/tail with explicit acquire/release ordering.
- Cache-line padded head and tail (`alignas(64)`) to eliminate false sharing between producer and consumer threads.
- Producer publishes writes with `memory_order_release`; consumer acquires before reading.
- Full and empty detected by the sentinel-gap pattern: capacity usable slots, one gap slot.

In benchmark `--pipeline` mode, the main thread produces parsed orders and the engine runs on a dedicated consumer thread. A `nullptr` sentinel signals end-of-stream.

**Honest result:** Pipeline mode shows modest improvement at 100K orders (3.97M vs 3.49M baseline) because the producer can batch-feed the queue while the consumer is matching. At 1M orders the gap narrows (2.98M vs 3.02M baseline) — the matching engine is the bottleneck, and distributing it across threads doesn't help if the consumer thread is always the slowest stage.

### 4C — IOC and FOK Order Types

**IOC (Immediate-Or-Cancel):**
- Matches aggressively against available liquidity at or better than the limit price.
- Any unfilled remainder is cancelled immediately — never rests in the book.
- Partial fills are allowed.

**FOK (Fill-Or-Kill):**
- Before executing, scans available liquidity to verify the full quantity can be filled.
- If yes: executes in full (may span multiple price levels, respects price-time priority).
- If no: rejects entirely with no trades and no book modification.
- Never partially fills. Never rests in the book.

Both types are fully integrated into the CSV parser (`IOC`/`FOK` tokens), the synthetic generator (3% IOC, 2% FOK of the generated stream), the matching engine, and the event log.

---

## Building

**Requirements:** C++17 compiler, CMake ≥ 3.16, GoogleTest (optional for tests)

```bash
# macOS
brew install cmake googletest

# Configure and build (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# Executables
./build/exchange             # CSV replay + interactive mode
./build/exchange_bench       # Synthetic benchmark runner
./build/astra_tests          # Core engine unit tests (21)
./build/astra_bench_tests    # Benchmark/generator tests (16)
./build/astra_pool_tests     # MemoryPool tests (9)
./build/astra_spsc_tests     # SpscRingBuffer tests (8)
./build/astra_ioc_fok_tests  # IOC/FOK tests (17)
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
2,1002,AAPL,SELL,MARKET,0,50
3,1003,AAPL,BUY,IOC,150.00,40
4,1004,AAPL,SELL,FOK,149.00,30
5,1005,AAPL,BUY,LIMIT,1005,CANCEL,0,1
```

Supported types: `LIMIT`, `MARKET`, `CANCEL`, `IOC`, `FOK`

### Benchmark

```bash
# Baseline single-threaded
./build/exchange_bench --orders 1000000

# Memory pool allocator (single-threaded)
./build/exchange_bench --orders 1000000 --pool

# Lock-free SPSC pipeline (2 threads)
./build/exchange_bench --orders 1000000 --pipeline

# Skip invariant checks for raw speed
./build/exchange_bench --orders 10000000 --no-verify --no-csv

# Custom seed for reproducibility
./build/exchange_bench --orders 1000000 --seed 123

# Custom ring buffer size (pipeline only, must be power of 2)
./build/exchange_bench --orders 1000000 --pipeline --ring-size 32768
```

---

## Running tests

```bash
./build/astra_tests          # 21 core engine tests
./build/astra_bench_tests    # 16 benchmark/generator tests
./build/astra_pool_tests     # 9 MemoryPool tests
./build/astra_spsc_tests     # 8 SPSC ring buffer tests (incl. 1M item MT test)
./build/astra_ioc_fok_tests  # 17 IOC/FOK correctness tests
```

**Total: 71 tests, all passing.**

### What is tested

**Core engine (21 tests):** limit resting, bid/ask crossing, partial/full/multiple fills, price priority, time priority, cancellation, cancelled-cannot-trade, market orders, invalid cancels, book invariants, engine rejections (unknown symbol, duplicate ID, zero qty), multi-symbol isolation.

**Benchmark/generator (16 tests):** generator count, order type mix, invalid rejection, real matches produced, multi-symbol isolation under load, cancelled-cannot-trade, invariant checker (duplicate IDs, zero-qty trades, zero-price trades), full small-run invariant pass.

**MemoryPool (9 tests):** alloc/dealloc, fill to capacity, exhaustion throws `bad_alloc`, slot reuse after dealloc, pointer uniqueness, non-trivial type (string) construction/destruction, alloc/free round-trip stress, available-decreases-on-alloc, null dealloc is no-op.

**SpscRingBuffer (8 tests):** push/pop single item, empty pop returns nullopt, FIFO order, full buffer rejects push, capacity is power-of-two, move-only type, multi-threaded 1M-item producer/consumer with FIFO verification, sentinel-pattern end-of-stream.

**IOC/FOK (17 tests):** IOC full fill, IOC partial fill + cancel remainder, IOC no liquidity cancels, IOC never rests, IOC sell side, IOC price-time priority; FOK full fill, FOK insufficient liquidity rejected with no book modification, FOK never partially fills, FOK never rests, FOK multi-level fill, FOK sell side, FOK price-time priority, FOK fail leaves invariants intact; engine IOC routing, engine FOK routing, FOK fail returns reason string.

---

## Benchmark results

Measured on **Apple M3 Pro, macOS 14, clang 15, `-O3`**, seed=42.
Order mix: 55% LIMIT · 20% MARKET · 15% CANCEL · 3% IOC · 2% FOK · 5% INVALID.

### 100,000 orders (4 symbols)

| Mode | Throughput | p50 | p99 | Max | Memory |
|------|-----------|-----|-----|-----|--------|
| Baseline (single-thread) | 3.49 M/sec | 0.17 μs | 1.00 μs | 428 μs | 34.5 MB |
| Pool (single-thread) | 3.13 M/sec | 0.17 μs | 1.00 μs | 332 μs | 36.4 MB |
| Pipeline (SPSC, 2 threads) | 3.97 M/sec | 0.12 μs | 0.83 μs | 260 μs | 34.0 MB |

### 1,000,000 orders (4 symbols)

| Mode | Throughput | p50 | p99 | Max | Memory |
|------|-----------|-----|-----|-----|--------|
| Baseline (single-thread) | 3.02 M/sec | 0.17 μs | 1.58 μs | 4,116 μs | 284.8 MB |
| Pool (single-thread) | 2.89 M/sec | 0.17 μs | 1.29 μs | 5,355 μs | 307.1 MB |
| Pipeline (SPSC, 2 threads) | 2.98 M/sec | 0.17 μs | 1.46 μs | 3,739 μs | 285.7 MB |

All three modes produce identical trade counts and pass all correctness invariants.

**Honest interpretation of the numbers:**

- **Pool mode** is not faster than baseline. The current architecture uses `shared_ptr<Order>` throughout `PriceLevel` and `OrderBook`. Wrapping a pool-allocated `Order` in a `shared_ptr` with a custom deleter still pays for control-block allocation and atomic ref-counting. The pool removes the per-`Order` `malloc`, but the `shared_ptr` overhead dominates. A meaningful improvement would require replacing `shared_ptr` with raw pool-owned pointers inside the book — an invasive architectural change.

- **Pipeline mode** improves p50/p99 latency at 100K orders by overlapping parsing with matching. At 1M orders the benefit diminishes because the engine (consumer thread) is the throughput bottleneck — adding a producer thread doesn't help when the consumer can't keep up.

- **Max latency** in all modes is dominated by occasional market orders sweeping many price levels, not allocator behavior.

---

## Order mix (generated workload)

| Type | Fraction | Notes |
|------|----------|-------|
| Limit orders | 55% | ~30% are aggressive (crossing) to generate trades |
| Market orders | 20% | Sweep available liquidity |
| Cancels | 15% | Target randomly sampled resting orders |
| IOC | 3% | Aggressive limit price; remainder cancelled if not fully filled |
| FOK | 2% | Moderate qty; rejected entirely if full qty not available |
| Invalid | 5% | Zero qty, zero price, unknown symbol — all rejected by validator |

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
9. **IOC/FOK orders never rest** in the book after processing

---

## Honest limitations

- **Pool mode is not faster than baseline in this architecture.** The root cause is `shared_ptr` ownership throughout the book internals. Fixing this requires replacing `shared_ptr<Order>` in `PriceLevel`/`OrderBook` with raw pointers owned by the pool — that's the next meaningful allocator optimization.

- **Pipeline mode provides limited throughput gain.** The matching engine is single-threaded per design (deterministic ordering requires it). The SPSC queue overlaps parsing with matching but can't parallelize the matching itself. The benefit is real at small workloads (14% improvement at 100K) but marginal at 1M+ orders.

- **No MPMC queue.** Only one producer and one consumer are supported. Multi-symbol parallelism (one engine thread per symbol) would require a more complex routing layer.

- **No MODIFY order type.** Cancelling and re-submitting is the only way to change a resting order.

- **std::map is not the fastest price-level structure.** A tick-indexed flat array would give O(1) best-bid/ask lookup, but requires bounded price ranges. The `std::map` gives O(log N) and works for arbitrary prices.

- **Max latency outliers are real.** The multi-millisecond max values are genuine — they occur when a market or aggressive limit order sweeps many price levels in one call, not allocator variance.

- **This is not a network exchange.** No TCP stack, FIX protocol, session layer, or wire format. Orders are submitted in-process.

- **Not production software.** There is no fault tolerance, persistence, regulatory reporting, or operational tooling. The engine is correct and measurably fast for a C++ portfolio project — nothing more.

---

## Project structure

```
AstraExchange/
├── include/
│   ├── core/
│   │   ├── Types.hpp           # Fixed-point price, OrderId, OrderType (LIMIT/MARKET/CANCEL/IOC/FOK)
│   │   ├── Order.hpp           # Order struct
│   │   ├── Trade.hpp           # Trade struct
│   │   ├── PriceLevel.hpp      # deque<Order> at one price point
│   │   ├── OrderBook.hpp       # bid/ask maps + index + matching (LIMIT/MARKET/IOC/FOK)
│   │   ├── MatchingEngine.hpp  # multi-symbol dispatcher
│   │   └── RiskValidator.hpp   # pre-trade checks
│   ├── parser/
│   │   └── CsvParser.hpp       # parses LIMIT/MARKET/CANCEL/IOC/FOK
│   ├── feed/
│   │   ├── EventLog.hpp
│   │   └── MarketDataFeed.hpp
│   ├── utils/
│   │   ├── MemoryPool.hpp      # slab allocator (Phase 4A)
│   │   └── SpscRingBuffer.hpp  # lock-free SPSC queue (Phase 4B)
│   └── benchmark/
│       ├── LatencyTimer.hpp
│       ├── OrderGenerator.hpp  # generates LIMIT/MARKET/CANCEL/IOC/FOK/INVALID
│       ├── InvariantChecker.hpp
│       ├── BenchmarkRunner.hpp # Baseline / Pool / Pipeline modes
│       └── ReplayEngine.hpp
├── src/
├── tests/
│   ├── test_orderbook.cpp      # 21 core engine tests
│   ├── test_benchmark.cpp      # 16 benchmark/generator tests
│   ├── test_memory_pool.cpp    # 9 MemoryPool tests
│   ├── test_spsc.cpp           # 8 SPSC ring buffer tests
│   └── test_ioc_fok.cpp        # 17 IOC/FOK tests
├── data/
│   └── sample_orders.csv
├── benchmark-results.csv
└── CMakeLists.txt
```

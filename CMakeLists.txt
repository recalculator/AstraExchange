cmake_minimum_required(VERSION 3.16)
project(AstraExchange CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Build type ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -fsanitize=address,undefined")

find_package(Threads REQUIRED)

# --- Include paths ---
include_directories(include)

# --- Core library ---
add_library(astra_core
    src/core/OrderBook.cpp
    src/core/MatchingEngine.cpp
)
target_include_directories(astra_core PUBLIC include)

# --- Parser library ---
add_library(astra_parser
    src/parser/CsvParser.cpp
)
target_include_directories(astra_parser PUBLIC include)
target_link_libraries(astra_parser PUBLIC astra_core)

# --- Feed library ---
add_library(astra_feed
    src/feed/EventLog.cpp
    src/feed/MarketDataFeed.cpp
)
target_include_directories(astra_feed PUBLIC include)
target_link_libraries(astra_feed PUBLIC astra_core)

# --- Benchmark library ---
add_library(astra_bench
    src/benchmark/LatencyTimer.cpp
    src/benchmark/ReplayEngine.cpp
    src/benchmark/OrderGenerator.cpp
    src/benchmark/InvariantChecker.cpp
    src/benchmark/BenchmarkRunner.cpp
)
target_include_directories(astra_bench PUBLIC include)
target_link_libraries(astra_bench PUBLIC astra_core astra_parser astra_feed Threads::Threads)

# --- Main exchange CLI ---
add_executable(exchange src/main.cpp)
target_link_libraries(exchange PRIVATE astra_core astra_parser astra_feed astra_bench)

# --- Benchmark executable ---
add_executable(exchange_bench src/exchange_bench.cpp)
target_link_libraries(exchange_bench PRIVATE astra_bench)

# --- Tests ---
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()

    add_executable(astra_tests
        tests/test_orderbook.cpp
    )
    target_link_libraries(astra_tests PRIVATE astra_core astra_parser GTest::GTest GTest::Main)
    add_test(NAME unit_tests COMMAND astra_tests)

    add_executable(astra_bench_tests
        tests/test_benchmark.cpp
    )
    target_link_libraries(astra_bench_tests PRIVATE astra_bench GTest::GTest GTest::Main)
    add_test(NAME bench_tests COMMAND astra_bench_tests)

    add_executable(astra_pool_tests
        tests/test_memory_pool.cpp
    )
    target_link_libraries(astra_pool_tests PRIVATE astra_core GTest::GTest GTest::Main)
    add_test(NAME pool_tests COMMAND astra_pool_tests)

    add_executable(astra_spsc_tests
        tests/test_spsc.cpp
    )
    target_link_libraries(astra_spsc_tests PRIVATE astra_core Threads::Threads GTest::GTest GTest::Main)
    add_test(NAME spsc_tests COMMAND astra_spsc_tests)

    add_executable(astra_ioc_fok_tests
        tests/test_ioc_fok.cpp
    )
    target_link_libraries(astra_ioc_fok_tests PRIVATE astra_core GTest::GTest GTest::Main)
    add_test(NAME ioc_fok_tests COMMAND astra_ioc_fok_tests)
else()
    message(STATUS "GoogleTest not found — tests skipped. Install with: brew install googletest")
endif()

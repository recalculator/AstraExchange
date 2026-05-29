#pragma once

#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <string>
#include <ostream>

namespace astra {

class LatencyTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using NS    = uint64_t;

    void start() { t0_ = Clock::now(); }

    void stop() {
        auto t1 = Clock::now();
        samples_.push_back(
            static_cast<NS>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0_).count())
        );
    }

    void reserve(size_t n) { samples_.reserve(n); }

    struct Stats {
        NS   p50, p95, p99, max, mean;
        NS   throughputOpsPerSec; // 0 if wallNs == 0
        NS   wallNs;
        size_t count;
    };

    Stats compute(NS wallNs = 0) const {
        if (samples_.empty()) return {};
        auto sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        auto percentile = [&](double pct) -> NS {
            size_t idx = static_cast<size_t>(pct * (sorted.size() - 1));
            return sorted[idx];
        };

        NS total = 0;
        for (auto s : sorted) total += s;

        Stats st;
        st.count   = sorted.size();
        st.p50     = percentile(0.50);
        st.p95     = percentile(0.95);
        st.p99     = percentile(0.99);
        st.max     = sorted.back();
        st.mean    = total / sorted.size();
        st.wallNs  = wallNs;
        st.throughputOpsPerSec = (wallNs > 0)
            ? static_cast<NS>(static_cast<double>(st.count) / wallNs * 1e9)
            : 0;
        return st;
    }

    void printReport(std::ostream& out, const std::string& label, NS wallNs = 0) const;

private:
    Clock::time_point t0_;
    std::vector<NS> samples_;
};

} // namespace astra

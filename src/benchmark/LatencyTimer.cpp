#include "benchmark/LatencyTimer.hpp"
#include <ostream>
#include <iomanip>

namespace astra {

void LatencyTimer::printReport(std::ostream& out, const std::string& label, NS wallNs) const {
    auto st = compute(wallNs);
    out << "\n--- " << label << " ---\n"
        << "Orders processed : " << st.count << "\n"
        << "Mean latency     : " << std::fixed << std::setprecision(2) << st.mean / 1e3 << " μs\n"
        << "p50  latency     : " << st.p50  / 1e3 << " μs\n"
        << "p95  latency     : " << st.p95  / 1e3 << " μs\n"
        << "p99  latency     : " << st.p99  / 1e3 << " μs\n"
        << "Max  latency     : " << st.max  / 1e3 << " μs\n";
    if (st.throughputOpsPerSec > 0) {
        out << "Throughput       : " << st.throughputOpsPerSec / 1'000 << " K orders/sec\n";
    }
}

} // namespace astra

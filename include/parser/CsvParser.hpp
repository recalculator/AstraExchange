#pragma once

#include "core/Order.hpp"
#include <string>
#include <vector>
#include <optional>

namespace astra {

// Parses CSV lines into Order objects.
// Expected header:
//   timestamp,order_id,symbol,side,type,price,quantity
class CsvParser {
public:
    struct ParseError {
        size_t      line;
        std::string message;
    };

    struct ParseResult {
        std::vector<std::shared_ptr<Order>> orders;
        std::vector<ParseError>             errors;
    };

    static ParseResult parseFile(const std::string& path);
    static std::optional<std::shared_ptr<Order>> parseLine(const std::string& line, size_t lineNum);

private:
    static std::vector<std::string> split(const std::string& s, char delim);
};

} // namespace astra

#include "parser/CsvParser.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace astra {

std::vector<std::string> CsvParser::split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) tokens.push_back(tok);
    return tokens;
}

std::optional<std::shared_ptr<Order>> CsvParser::parseLine(const std::string& line, size_t /*lineNum*/) {
    auto fields = split(line, ',');
    if (fields.size() < 7) return std::nullopt;

    try {
        Timestamp ts  = std::stoull(fields[0]);
        OrderId   id  = std::stoull(fields[1]);
        Symbol    sym = fields[2];

        Side side;
        if (fields[3] == "BUY")       side = Side::BUY;
        else if (fields[3] == "SELL") side = Side::SELL;
        else return std::nullopt;

        OrderType type;
        if (fields[4] == "LIMIT")        type = OrderType::LIMIT;
        else if (fields[4] == "MARKET")  type = OrderType::MARKET;
        else if (fields[4] == "CANCEL")  type = OrderType::CANCEL;
        else if (fields[4] == "IOC")     type = OrderType::IOC;
        else if (fields[4] == "FOK")     type = OrderType::FOK;
        else return std::nullopt;

        Price    price = toFixedPrice(std::stod(fields[5]));
        Quantity qty   = std::stoull(fields[6]);

        return std::make_shared<Order>(id, sym, side, type, price, qty, ts);
    } catch (...) {
        return std::nullopt;
    }
}

CsvParser::ParseResult CsvParser::parseFile(const std::string& path) {
    ParseResult result;
    std::ifstream file(path);
    if (!file.is_open()) {
        result.errors.push_back({0, "cannot open file: " + path});
        return result;
    }

    std::string line;
    size_t lineNum = 0;
    bool headerSkipped = false;

    while (std::getline(file, line)) {
        ++lineNum;
        if (line.empty()) continue;
        if (!headerSkipped) { headerSkipped = true; continue; } // skip header

        auto order = parseLine(line, lineNum);
        if (order) {
            result.orders.push_back(*order);
        } else {
            result.errors.push_back({lineNum, "failed to parse: " + line});
        }
    }

    return result;
}

} // namespace astra

#include "exlottery/math.hpp"
#include <stdexcept>

namespace exlottery {
U256 parse_uint256(const std::string& input) {
    const bool hex = input.size() >= 2 && input[0] == '0' &&
                     (input[1] == 'x' || input[1] == 'X');
    const unsigned base = hex ? 16 : 10;
    const std::size_t start = hex ? 2 : 0;
    if (start == input.size()) throw std::invalid_argument("empty unsigned integer");
    U256 result = 0;
    for (auto i = start; i < input.size(); ++i) {
        const char c = input[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (hex && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else throw std::invalid_argument("invalid unsigned integer");
        result *= base;
        result += digit;
    }
    return result;
}
std::string decimal(U256 value) { return value.str(); }
Key to_key(U256 value) {
    Key result{};
    for (auto it = result.rbegin(); it != result.rend() && value != 0; ++it) {
        *it = static_cast<std::uint8_t>(value & 255);
        value >>= 8;
    }
    return result;
}
U256 from_key(const Key& key) {
    U256 result = 0;
    for (const auto byte : key) {
        result <<= 8;
        result |= byte;
    }
    return result;
}
std::string hex_key(const Key& key) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : key) {
        result += alphabet[byte >> 4];
        result += alphabet[byte & 15];
    }
    return result;
}
} // namespace exlottery

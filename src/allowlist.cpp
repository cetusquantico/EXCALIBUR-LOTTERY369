#include "exlottery/math.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace exlottery {
namespace {
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
unsigned nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("expected a two-digit hex byte");
}
void canonicalize(Bytes& bytes) {
    if (bytes.empty()) throw std::invalid_argument("empty allowlist");
    std::sort(bytes.begin(), bytes.end());
    if (std::adjacent_find(bytes.begin(), bytes.end()) != bytes.end())
        throw std::invalid_argument("duplicate allowlist value");
}
} // namespace

Bytes all_bytes(unsigned lo, unsigned hi) {
    if (hi > 255 || lo > hi) throw std::invalid_argument("invalid byte interval");
    Bytes result;
    for (unsigned i = lo; i <= hi; ++i) result.push_back(static_cast<std::uint8_t>(i));
    return result;
}
Bytes parse_allow(const std::string& input) {
    Bytes result;
    std::size_t pos = 0;
    while (pos < input.size() && space(input[pos])) ++pos;
    while (pos < input.size()) {
        if (input.size() - pos < 2) throw std::invalid_argument("incomplete byte");
        result.push_back(static_cast<std::uint8_t>(16 * nibble(input[pos]) + nibble(input[pos+1])));
        if (result.size() > 256) throw std::invalid_argument("too many bytes in allowlist");
        pos += 2;
        const auto end_token = pos;
        while (pos < input.size() && space(input[pos])) ++pos;
        if (pos == input.size()) break;
        if (input[pos] == ',') {
            ++pos;
            while (pos < input.size() && space(input[pos])) ++pos;
            if (pos == input.size()) throw std::invalid_argument("empty allowlist entry");
        } else if (pos == end_token) {
            throw std::invalid_argument("missing byte separator");
        }
    }
    canonicalize(result);
    return result;
}
Bytes read_allow_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("could not open allowlist: " + filename);
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad() || contents.bad()) throw std::runtime_error("could not read allowlist: " + filename);
    return parse_allow(contents.str());
}
std::string format_allow(const Bytes& values) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : values) {
        if (!result.empty()) result += ',';
        result += alphabet[byte >> 4];
        result += alphabet[byte & 15];
    }
    return result;
}
Bytes draw_allow(Bytes pool, unsigned count, std::uint64_t seed, bool first) {
    canonicalize(pool);
    if (!count || count > pool.size()) throw std::invalid_argument("draw size outside pool");
    // Domain separation makes the first and shared draws independently reproducible.
    std::mt19937_64 rng(seed ^ (first ? UINT64_C(0xa0761d6478bd642f) : UINT64_C(0xe7037ed1a0b428db)));
    for (unsigned i = 0; i < count; ++i) {
        const std::uint64_t bound = pool.size() - i;
        // 2^64 mod bound, computed in unsigned arithmetic. The accepted
        // interval has length exactly divisible by bound.
        const std::uint64_t threshold = (std::uint64_t{0} - bound) % bound;
        std::uint64_t word;
        do { word = rng(); } while (word < threshold);
        std::swap(pool[i], pool[i + word % bound]);
    }
    pool.resize(count);
    std::sort(pool.begin(), pool.end());
    return pool;
}
} // namespace exlottery

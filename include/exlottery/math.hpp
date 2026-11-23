#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace exlottery {
using U256 = boost::multiprecision::checked_uint256_t;
using Key = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;

U256 parse_uint256(const std::string& input);
std::string decimal(U256 value);
Key to_key(U256 value);
U256 from_key(const Key& key);
std::string hex_key(const Key& key);

struct Range {
    unsigned n, length, first_min, first_max;
    U256 lo, hi;
};
Range puzzle_range(unsigned n);
Bytes all_bytes(unsigned lo = 0, unsigned hi = 255);
Bytes parse_allow(const std::string& input);
Bytes read_allow_file(const std::string& filename);
std::string format_allow(const Bytes& values);
inline constexpr char draw_algorithm[] = "mt19937_64-fy-v1";
Bytes draw_allow(Bytes pool, unsigned count, std::uint64_t seed, bool first);

class MixedRadix {
public:
    const Range range;
    const Bytes first, shared;
    const std::string id;

    MixedRadix(Range range, Bytes first, Bytes shared);
    U256 total_candidates() const { return total_; }
    Key rank_to_key(U256 rank) const;
    U256 key_to_rank(const Key& key) const;
    // In-place operations expect a key in this ticket. At a boundary return
    // false and preserve the key so forward/reverse traversal never wraps.
    bool next_candidate(Key& key) const;
    bool previous_candidate(Key& key) const;

private:
    const unsigned start_;
    U256 total_;
    std::array<int, 256> first_index_, shared_index_;
    bool step(Key& key, bool forward) const;
};
} // namespace exlottery

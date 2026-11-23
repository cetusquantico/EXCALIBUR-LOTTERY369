#include "exlottery/math.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <stdexcept>

namespace exlottery {
namespace {
Bytes checked(Bytes list, unsigned lo, unsigned hi) {
    std::sort(list.begin(), list.end());
    if (list.empty() || list.front() < lo || list.back() > hi)
        throw std::invalid_argument("allowlist outside legal byte interval");
    if (std::adjacent_find(list.begin(), list.end()) != list.end())
        throw std::invalid_argument("duplicate allowlist byte");
    return list;
}
std::string ticket_id(unsigned n, const Bytes& first, const Bytes& shared) {
    const std::string fields = "exlottery-ticket-v1\n" + std::to_string(n) + "\n" +
                               format_allow(first) + "\n" + format_allow(shared) + "\n";
    Key digest{};
    if (!SHA256(reinterpret_cast<const unsigned char*>(fields.data()), fields.size(), digest.data()))
        throw std::runtime_error("ticket digest failed");
    return hex_key(digest);
}
} // namespace
MixedRadix::MixedRadix(Range input, Bytes a, Bytes b)
    : range(puzzle_range(input.n)), first(checked(std::move(a), range.first_min, range.first_max)),
      shared(checked(std::move(b), 0, 255)), id(ticket_id(range.n, first, shared)),
      start_(32 - range.length), total_(first.size()) {
    if (input.length != range.length || input.first_min != range.first_min ||
        input.first_max != range.first_max || input.lo != range.lo || input.hi != range.hi)
        throw std::invalid_argument("inconsistent puzzle range");
    for (unsigned i = 1; i < range.length; ++i) total_ *= shared.size();
    first_index_.fill(-1);
    shared_index_.fill(-1);
    for (unsigned i = 0; i < first.size(); ++i) first_index_[first[i]] = static_cast<int>(i);
    for (unsigned i = 0; i < shared.size(); ++i) shared_index_[shared[i]] = static_cast<int>(i);
}
Key MixedRadix::rank_to_key(U256 rank) const {
    if (rank >= total_) throw std::out_of_range("ordinal outside ticket");
    Key key{};
    for (unsigned i = 31; i > start_; --i) {
        const auto digit = static_cast<unsigned>(rank % shared.size());
        key[i] = shared[digit];
        rank /= shared.size();
    }
    key[start_] = first[static_cast<unsigned>(rank)];
    return key;
}
U256 MixedRadix::key_to_rank(const Key& key) const {
    if (!std::all_of(key.begin(), key.begin() + start_, [](auto b){ return b == 0; }))
        throw std::out_of_range("nonzero key padding");
    const int leading = first_index_[key[start_]];
    if (leading < 0) throw std::out_of_range("leading byte outside ticket");
    U256 rank = leading;
    for (unsigned i = start_ + 1; i < key.size(); ++i) {
        const int digit = shared_index_[key[i]];
        if (digit < 0) throw std::out_of_range("byte outside ticket");
        rank *= shared.size();
        rank += digit;
    }
    return rank;
}
bool MixedRadix::step(Key& key, bool forward) const {
    for (unsigned end = 32; end > start_;) {
        const unsigned i = --end;
        const auto& list = i == start_ ? first : shared;
        const auto& lookup = i == start_ ? first_index_ : shared_index_;
        const int pos = lookup[key[i]];
        if (pos < 0) throw std::out_of_range("byte outside ticket");
        const int next = forward ? pos + 1 : pos - 1;
        if (next >= 0 && static_cast<unsigned>(next) < list.size()) {
            key[i] = list[static_cast<unsigned>(next)];
            std::fill(key.begin() + i + 1, key.end(), forward ? shared.front() : shared.back());
            return true;
        }
    }
    return false;
}
bool MixedRadix::next_candidate(Key& key) const { return step(key, true); }
bool MixedRadix::previous_candidate(Key& key) const { return step(key, false); }
} // namespace exlottery

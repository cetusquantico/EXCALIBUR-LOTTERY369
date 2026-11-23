#include "exlottery/math.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace exlottery;
namespace {
void check(bool ok, const char* expression, int line) {
    if (!ok) throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
#define CHECK(x) check(bool(x), #x, __LINE__)
template<class Fn> void rejects(Fn fn) {
    try { fn(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("expected an exception");
}
void integers() {
    const std::string max_string = "115792089237316195423570985008687907853269984665640564039457584007913129639935";
    const U256 max = parse_uint256(max_string);
    CHECK(max == std::numeric_limits<U256>::max());
    CHECK(max == parse_uint256("0x" + std::string(64, 'F')));
    CHECK(decimal(max) == max_string);
    CHECK(hex_key(to_key(max)) == std::string(64, 'f'));
    CHECK(from_key(to_key(max)) == max);
    CHECK(hex_key(to_key(0)) == std::string(64, '0'));
    CHECK(parse_uint256("012") == 12);
    CHECK(parse_uint256("0XAbCd") == 0xabcd);
    rejects([&]{ U256 v = max; v += 1; });
    rejects([]{ U256 v = 0; v -= 1; });
    rejects([&]{ U256 v = max; v *= 2; });
    rejects([]{ parse_uint256("0x1" + std::string(64, '0')); });
    rejects([]{ parse_uint256("115792089237316195423570985008687907853269984665640564039457584007913129639936"); });
    for (const char* text : {"", "0x", "-1", "+1", " 2", "gg", "5a", "0x12.3"})
        rejects([&]{ parse_uint256(text); });

    std::mt19937_64 rng(140462);
    for (unsigned i = 0; i < 1024; ++i) {
        Key bytes{};
        for (auto& byte : bytes) byte = static_cast<std::uint8_t>(rng());
        U256 number = from_key(bytes);
        CHECK(to_key(number) == bytes);
        CHECK(parse_uint256("0x" + hex_key(bytes)) == number);
        CHECK(parse_uint256(decimal(number)) == number);
        U256 divisor = U256(rng()) + 1;
        CHECK((number / divisor) * divisor + number % divisor == number);
        CHECK(number % divisor < divisor);
    }
}
void ranges() {
    rejects([]{ puzzle_range(0); });
    rejects([]{ puzzle_range(161); });
    for (unsigned n = 1; n <= 160; ++n) {
        const auto r = puzzle_range(n);
        CHECK(r.lo + r.lo - 1 == r.hi);
        CHECK(boost::multiprecision::msb(r.lo) == n - 1);
        CHECK(boost::multiprecision::lsb(r.lo) == n - 1);
        CHECK(r.length == (n + 7) / 8);
        CHECK(to_key(r.lo)[32-r.length] == r.first_min);
        CHECK(to_key(r.hi)[32-r.length] == r.first_max);
        MixedRadix full(r, all_bytes(r.first_min,r.first_max), all_bytes());
        CHECK(full.total_candidates() == r.hi - r.lo + 1);
        CHECK(from_key(full.rank_to_key(0)) == r.lo);
        CHECK(from_key(full.rank_to_key(r.lo - 1)) == r.hi);
        for (U256 rank : {U256(0), r.lo / 3, r.lo / 2, r.lo - 1}) {
            Key key = full.rank_to_key(rank);
            CHECK(from_key(key) == r.lo + rank);
            CHECK(full.key_to_rank(key) == rank);
        }
        rejects([&]{ full.rank_to_key(r.lo); });
        rejects([&]{ full.key_to_rank(to_key(r.hi+1)); });
        rejects([&]{ full.key_to_rank(to_key(r.lo-1)); });
    }
    const auto r = puzzle_range(160);
    CHECK(hex_key(to_key(r.lo)) == "0000000000000000000000008000000000000000000000000000000000000000");
    CHECK(hex_key(to_key(r.hi)) == "000000000000000000000000ffffffffffffffffffffffffffffffffffffffff");
    for (unsigned m = 1; m <= 256; ++m) {
        MixedRadix t(r, {128, 255}, all_bytes(0, m-1));
        boost::multiprecision::cpp_int total = 2;
        for (unsigned i = 1; i < 20; ++i) total *= m;
        CHECK(decimal(t.total_candidates()) == total.str());
        U256 rank = t.total_candidates() * 2 / 3;
        CHECK(t.key_to_rank(t.rank_to_key(rank)) == rank);
    }
}
void lists() {
    CHECK(parse_allow(" \nFF 0A,\t00") == Bytes({0,10,255}));
    for (const char* bad : {"", " \n", "0", "000", "00,", "00,,ff", ",00", "1x", "01,01", "00;ff"})
        rejects([&]{ parse_allow(bad); });
    rejects([]{ read_allow_file("/dev/null"); });
    CHECK(format_allow({0,10,255}) == "00,0a,ff");
    CHECK(draw_allow(all_bytes(128,255), 1, 369, true) == Bytes({135}));
    CHECK(draw_allow(all_bytes(), 3, 369, false) == Bytes({99,112,170}));
    CHECK(draw_allow(all_bytes(), 256, 44, false) == all_bytes());
    rejects([]{ draw_allow({1,1}, 1, 0, true); });
    rejects([]{ draw_allow({1,2}, 3, 0, true); });
    rejects([]{ draw_allow({1}, 0, 0, false); });
    auto r = puzzle_range(17);
    CHECK(MixedRadix(r,{1},{255,0,49}).id == "8c083070038fa21df29ab64baeadb1adf092023bde3d8c7d0e8a5102c5668979");
    rejects([&]{ MixedRadix(r,{1,1},{0}); });
    rejects([&]{ MixedRadix(r,{0},{1}); });
    rejects([&]{ MixedRadix(r,{1},{5,5}); });
    rejects([&]{ MixedRadix(r,{}, {0}); });
    rejects([&]{ MixedRadix(r,{1}, {}); });
    r.length = 32;
    rejects([&]{ MixedRadix(r,{1}, {0}); });
}
void enumeration() {
    const auto r = puzzle_range(39);
    MixedRadix t(r, {127,65,64}, {255,0,8});
    std::vector<Key> independent;
    // Direct nested Cartesian product in ascending order acts as an oracle.
    for (auto f : t.first)
        for (auto a : t.shared) for (auto b : t.shared)
            for (auto c : t.shared) for (auto d : t.shared) {
                Key key{};
                key[27]=f; key[28]=a; key[29]=b; key[30]=c; key[31]=d;
                independent.push_back(key);
            }
    CHECK(t.total_candidates() == independent.size());
    auto key = t.rank_to_key(0);
    for (unsigned i=0; i<independent.size(); ++i) {
        CHECK(key == independent[i]);
        CHECK(t.rank_to_key(i) == key);
        CHECK(t.key_to_rank(key) == i);
        CHECK(t.next_candidate(key) == (i+1 < independent.size()));
    }
    for (auto i=independent.size(); i>0; --i) {
        CHECK(key == independent[i-1]);
        CHECK(t.previous_candidate(key) == (i>1));
    }
    CHECK(key == independent.front());
    Key invalid = key;
    invalid[0] = 1;
    rejects([&]{ t.key_to_rank(invalid); });
    invalid = key;
    invalid[31] = 9;
    rejects([&]{ t.key_to_rank(invalid); });
    for (unsigned m : {8,16,32,64})
        CHECK(MixedRadix(r,{64},all_bytes(0,m-1)).total_candidates() == U256(m)*m*m*m);
}
} // namespace

int main() {
    try {
        integers();
        ranges();
        lists();
        enumeration();
        std::cout << "160 puzzle ranges, 256 list sizes, arithmetic, draws, Cartesian coverage OK\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

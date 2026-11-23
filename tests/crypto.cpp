#include "exlottery/crypto.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace exlottery;
namespace {
void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

template<class Fn> void invalid(Fn fn) {
    try { fn(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}

CryptoKey key64(std::uint64_t value) {
    CryptoKey out{};
    for (std::size_t i = out.size(); i-- > out.size()-8;) {
        out[i] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
    return out;
}

void addresses() {
    const auto p39 = key64(0x4b5f8303e9);
    require(decode_p2pkh("122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8") == reference_hash160(p39),
            "public puzzle 39 fixture");
    require(digest_hex(reference_hash160(key64(1))) == "751e76e8199196d454941c45d1b3a323f1433bd6",
            "known k=1 HASH160");
    require(parse_hash160("751E76E8199196D454941C45D1B3A323F1433BD6") == reference_hash160(key64(1)),
            "uppercase hex");
    require(decode_p2pkh("1111111111111111111114oLvT2") == Hash160{}, "leading zero payload");
    for (const char* bad : {"", "0", "111", "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT9",
                            "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdTO", "1122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8",
                            "31h1vYVSYuKP6AhS86fbRdMw9XHieotbST", "1zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"})
        invalid([&]{ decode_p2pkh(bad); });
    invalid([]{ parse_hash160(std::string(39,'0')); });
    invalid([]{ parse_hash160(std::string(40,'g')); });
    invalid([]{ reference_hash160(CryptoKey{}); });
    CryptoKey max;
    max.fill(0xff);
    invalid([&]{ reference_hash160(max); });
    invalid([]{ FixtureVerifier bad(key64(1), "other"); });
    invalid([]{ FixtureVerifier bad(key64(1), "reference", "other"); });
    if (!crypto_sse_available())
        invalid([]{ FixtureVerifier bad(key64(1), "jump", "sse"); });
}

std::uint64_t folded(const Hash160& digest) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < digest.size(); ++i)
        value ^= static_cast<std::uint64_t>(digest[i]) << (8 * (i % 8));
    return value;
}

void long_carry() {
    // In a binary 19-position ticket, 2^18-1 -> 2^18 resets 18 suffix bytes.
    std::array<CryptoKey, 8> keys{};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        unsigned ordinal = (1U << 18) - 4 + static_cast<unsigned>(i);
        keys[i][12] = 0x80;
        for (int byte = 31; byte >= 13; --byte) {
            keys[i][byte] = static_cast<std::uint8_t>(ordinal & 1U);
            ordinal >>= 1;
        }
    }
    for (bool reverse : {false, true}) {
        if (reverse) std::reverse(keys.begin(), keys.end());
        for (const auto& known : keys) {
            FixtureVerifier v(known, "jump");
            require(v.verify_batch(keys.data(), keys.size()) == 1, "160-bit carry digest mismatch");
        }
    }
}

void differential() {
    const auto fixture = key64(0x4b5f8303e9);
    std::vector<CryptoKey> candidates{key64(1), key64(2), key64(1), key64(255), key64(256),
                                      fixture, fixture, key64(2), key64(1)};
    std::mt19937_64 rng(1701);
    for (int i = 0; i != 560; ++i) {
        CryptoKey k{};
        for (std::size_t p = 12; p < k.size(); ++p) k[p] = rng() & 255;
        k[12] |= 0x80;
        candidates.push_back(k);
    }
    candidates.push_back(fixture);
    std::vector<std::string> hashes{"scalar", "auto"};
    if (crypto_sse_available()) hashes.push_back("sse");
    for (bool reverse : {false, true}) {
        if (reverse) std::reverse(candidates.begin(), candidates.end());
        for (auto ec : {"reference", "jump"}) {
            for (const auto& hash : hashes) {
                FixtureVerifier v(fixture, ec, hash);
                std::uint64_t expected = 0;
                std::vector<std::size_t> sizes{1, 2, 3, 4, 5, 127, 128, candidates.size()-270};
                std::size_t pos = 0;
                for (const auto size : sizes) {
                    std::size_t hits = 0;
                    for (std::size_t i = pos; i < pos + size; ++i) {
                        expected += folded(reference_hash160(candidates[i]));
                        hits += (candidates[i] == fixture);
                    }
                    require(v.verify_batch(candidates.data()+pos, size) == hits, "fixture count");
                    require(v.checksum64() == expected, "batch digests differ from libsecp256k1/OpenSSL");
                    pos += size;
                }
                require(pos == candidates.size(), "test batch sizes");
                require(v.verify_batch(nullptr, 0) == 0, "empty batch");
                invalid([&]{ v.verify_batch(nullptr, 1); });
                CryptoKey zero{};
                invalid([&]{ v.verify_batch(&zero, 1); });
            }
        }
    }
    // Each expected hash is checked in all 20 bytes and in all four SIMD lanes.
    std::array<CryptoKey, 4> lanes{key64(19), key64(72), key64(65536), fixture};
    for (const auto& k : lanes) {
        for (auto ec : {"reference", "jump"}) {
            FixtureVerifier v(k, ec);
            require(v.verify_batch(lanes.data(), lanes.size()) == 1, "independent lane check");
        }
    }
}
}
int main() {
    try {
        addresses();
        differential();
        long_carry();
        std::cout << "public vectors, P2PKH validation, forward/reverse EC and HASH160 differential OK\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

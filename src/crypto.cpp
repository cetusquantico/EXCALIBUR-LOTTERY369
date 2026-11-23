#include "exlottery/crypto.hpp"

#include "ec/group.h"
#include "hash/hash160.h"
#include "hash/hash_multi.h"

#include <openssl/evp.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace exlottery {
namespace {
using ex369::Fe;
using ex369::PointA;
using ex369::PointJ;
using ex369::U256;
using Pubkey = std::array<std::uint8_t, 33>;

void digest(const EVP_MD* md, const unsigned char* data, std::size_t size,
            unsigned char* output, unsigned int expected) {
    unsigned int written = 0;
    if (!md || EVP_Digest(data, size, output, &written, md, nullptr) != 1 || written != expected)
        throw std::runtime_error("OpenSSL digest unavailable");
}

const secp256k1_context* reference_context() {
    const auto destroy = [](secp256k1_context* c) { secp256k1_context_destroy(c); };
    using Ctx = std::unique_ptr<secp256k1_context, decltype(destroy)>;
    static const Ctx ctx(secp256k1_context_create(SECP256K1_CONTEXT_SIGN), destroy);
    if (!ctx) throw std::runtime_error("libsecp256k1 context unavailable");
    return ctx.get();
}

const U256& curve_order() {
    static const auto n = []{
        U256 result;
        std::copy_n(ex369::CURVE_N, 4, result.d);
        return result;
    }();
    return n;
}

const ex369::MultiHash& choose_hash(const std::string& backend) {
    if (backend == "scalar") return ex369::multihash_for(ex369::SimdBackend::Scalar);
    if (backend != "auto" && backend != "sse") throw std::invalid_argument("unknown HASH160 backend");
    const auto& hash = ex369::multihash_for(ex369::SimdBackend::SSE);
    if (backend == "sse" && hash.lanes != 4) throw std::invalid_argument("SSSE3 unavailable");
    return hash;
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("invalid hexadecimal digit");
}

template<std::size_t N>
std::array<std::uint8_t, N> parse_hex_bytes(const std::string& text, const char* length_error) {
    if (text.size() != 2 * N) throw std::invalid_argument(length_error);
    std::array<std::uint8_t, N> bytes{};
    for (std::size_t i = 0; i < N; ++i)
        bytes[i] = static_cast<std::uint8_t>(nibble(text[2*i]) * 16 + nibble(text[2*i+1]));
    return bytes;
}

constexpr std::size_t block_size = 128;

// One field inversion converts all nonzero Jacobian Z coordinates in a block.
void serialize_batch(const std::array<PointJ, block_size>& points, std::size_t size,
                     std::uint8_t* pubs) {
    std::array<Fe, block_size> zs, inverse;
    for (std::size_t i = 0; i < size; ++i) zs[i] = points[i].Z;
    ex369::batch_inverse(inverse.data(), zs.data(), size);
    for (std::size_t i = 0; i < size; ++i) {
        Fe square, cube;
        PointA affine;
        ex369::fe_sqr(square, inverse[i]);
        ex369::fe_mul(cube, square, inverse[i]);
        ex369::fe_mul(affine.x, points[i].X, square);
        ex369::fe_mul(affine.y, points[i].Y, cube);
        ex369::serialize_compressed(affine, pubs + 33 * i);
    }
}

// Consumes all 20 bytes for every fixture experiment, including negative cases.
// Unsigned wraparound is intentional. This statistic is never an authenticity proof.
std::uint64_t fold_digest(const std::uint8_t* bytes) {
    std::uint64_t a, b;
    std::uint32_t c;
    std::memcpy(&a, bytes, 8);
    std::memcpy(&b, bytes + 8, 8);
    std::memcpy(&c, bytes + 16, 4);
    return a ^ b ^ c;
}

Pubkey reference_pubkey(const CryptoKey& key) {
    secp256k1_pubkey point;
    const auto* ctx = reference_context();
    if (!secp256k1_ec_pubkey_create(ctx, &point, key.data()))
        throw std::invalid_argument("invalid secp256k1 scalar");
    Pubkey pub{};
    std::size_t size = pub.size();
    if (!secp256k1_ec_pubkey_serialize(ctx, pub.data(), &size, &point, SECP256K1_EC_COMPRESSED)
        || size != pub.size())
        throw std::runtime_error("libsecp256k1 compressed pubkey serialization failed");
    return pub;
}

Hash160 digest_pubkey(const Pubkey& pub) {
    std::array<std::uint8_t, 32> sha{};
    Hash160 out{};
    digest(EVP_sha256(), pub.data(), pub.size(), sha.data(), sha.size());
    digest(EVP_ripemd160(), sha.data(), sha.size(), out.data(), out.size());
    return out;
}

constexpr std::string_view base58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::array<std::uint8_t, 32> double_sha256(const std::uint8_t* bytes, std::size_t size) {
    std::array<std::uint8_t, 32> first{}, second{};
    digest(EVP_sha256(), bytes, size, first.data(), first.size());
    digest(EVP_sha256(), first.data(), first.size(), second.data(), second.size());
    return second;
}

std::string base58check(std::vector<std::uint8_t> payload) {
    const auto checksum = double_sha256(payload.data(), payload.size());
    payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);

    auto first = std::find_if(payload.begin(), payload.end(), [](auto byte) { return byte != 0; });
    std::string encoded(static_cast<std::size_t>(first - payload.begin()), '1');
    // Accumulate the integer in little-endian base 58, retaining payload prefix zeros above.
    std::vector<std::uint8_t> digits;
    for (; first != payload.end(); ++first) {
        unsigned carry = *first;
        for (auto& digit : digits) {
            carry += 256U * digit;
            digit = static_cast<std::uint8_t>(carry % 58);
            carry /= 58;
        }
        while (carry) {
            digits.push_back(static_cast<std::uint8_t>(carry % 58));
            carry /= 58;
        }
    }
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) encoded += base58[*it];
    return encoded;
}

template<std::size_t N>
std::string hex_bytes(const std::array<std::uint8_t, N>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 15]);
    }
    return out;
}
} // namespace

bool crypto_sse_available() { return ex369::multihash_for(ex369::SimdBackend::SSE).lanes == 4; }

Hash160 reference_hash160(const CryptoKey& key) { return digest_pubkey(reference_pubkey(key)); }

Hash160 compressed_pubkey_hash160(const std::string& text) {
    const auto pub = parse_hex_bytes<33>(text, "A pubkey comprimida exige 66 dígitos hexadecimais.");
    if (pub.front() != 0x02 && pub.front() != 0x03)
        throw std::invalid_argument("A pubkey comprimida deve começar com 02 ou 03.");
    secp256k1_pubkey point;
    if (!secp256k1_ec_pubkey_parse(reference_context(), &point, pub.data(), pub.size()))
        throw std::invalid_argument("Pubkey inválida para secp256k1.");
    return digest_pubkey(pub);
}

FixtureIdentity reference_identity(const CryptoKey& known_key) {
    const Pubkey pub = reference_pubkey(known_key);
    const Hash160 hash = digest_pubkey(pub);

    std::vector<std::uint8_t> private_payload{0x80};
    private_payload.insert(private_payload.end(), known_key.begin(), known_key.end());
    private_payload.push_back(0x01); // compressed mainnet WIF
    std::vector<std::uint8_t> address_payload{0x00}; // mainnet P2PKH
    address_payload.insert(address_payload.end(), hash.begin(), hash.end());

    return {base58check(private_payload), hex_bytes(pub), hex_bytes(hash), base58check(address_payload)};
}

std::string targets_fingerprint(const TargetSet& targets) {
    std::vector<Hash160> sorted(targets.begin(), targets.end());
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::uint8_t> blob;
    blob.reserve(sorted.size() * 20);
    for (const auto& h : sorted) blob.insert(blob.end(), h.begin(), h.end());
    std::array<std::uint8_t, 32> out{};
    digest(EVP_sha256(), blob.data(), blob.size(), out.data(), out.size());
    return hex_bytes(out);
}

Hash160 parse_hash160(const std::string& text) {
    return parse_hex_bytes<20>(text, "HASH160 requires 40 hex digits");
}

std::string digest_hex(const Hash160& hash) { return hex_bytes(hash); }

Hash160 decode_p2pkh(const std::string& address) {
    if (address.empty() || address.size() > 35 || address.front() != '1')
        throw std::invalid_argument("expected mainnet P2PKH address");
    std::array<std::uint8_t, 25> bytes{};
    const auto leading = address.find_first_not_of('1');
    if (leading == std::string::npos) throw std::invalid_argument("invalid P2PKH address");
    for (std::size_t i = leading; i < address.size(); ++i) {
        const auto pos = base58.find(address[i]);
        if (pos == base58.npos) throw std::invalid_argument("invalid Base58 character");
        unsigned carry = static_cast<unsigned>(pos);
        for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
            carry += *it * 58U;
            *it = static_cast<std::uint8_t>(carry);
            carry >>= 8;
        }
        if (carry) throw std::invalid_argument("oversized Base58 payload");
    }
    auto first_byte = std::find_if(bytes.begin(), bytes.end(), [](auto byte) { return byte != 0; });
    if (static_cast<std::size_t>(first_byte - bytes.begin()) != leading || bytes[0] != 0)
        throw std::invalid_argument("invalid length or version of P2PKH payload");
    const auto checksum = double_sha256(bytes.data(), 21);
    if (!std::equal(bytes.begin()+21, bytes.end(), checksum.begin()))
        throw std::invalid_argument("invalid Base58Check checksum");
    Hash160 out{};
    std::copy_n(bytes.begin()+1, out.size(), out.begin());
    return out;
}

namespace {
// Shared candidate -> compressed-pubkey pipeline. In "jump" mode consecutive
// scalars advance one EC point incrementally and a whole block is serialized
// with a single field inversion; "reference" mode multiplies each scalar
// independently. One instance is private to a single worker.
struct PubkeyEngine {
    bool jump;
    PointJ current = PointJ::infinity();
    U256 previous;
    // Direction is part of the index. This cache is private to one worker.
    std::map<std::pair<bool, U256>, PointA> transitions;

    explicit PubkeyEngine(const std::string& ec) : jump(ec == "jump") {
        if (!jump && ec != "reference") throw std::invalid_argument("unknown EC backend");
    }

    const PointA& transition(const U256& next) {
        const bool backwards = next < previous;
        const auto magnitude = backwards ? previous - next : next - previous;
        const auto id = std::make_pair(backwards, magnitude);
        auto it = transitions.find(id);
        if (it != transitions.end()) return it->second;
        if (transitions.size() == 512) transitions.clear();
        PointA d;
        ex369::ec_mul_gen(d, magnitude);
        if (backwards) ex369::point_a_neg(d, d);
        return transitions.emplace(id, d).first->second;
    }

    void make_pubkeys(const CryptoKey* keys, std::size_t size, std::uint8_t* pubs) {
        std::array<PointJ, block_size> points;
        for (std::size_t i = 0; i < size; ++i) {
            const auto scalar = U256::from_be32(keys[i].data());
            if (scalar.is_zero() || scalar >= curve_order()) throw std::invalid_argument("invalid candidate scalar");
            if (!jump || current.is_inf()) {
                PointA p;
                ex369::ec_mul_gen(p, scalar);
                if (!jump) {
                    ex369::serialize_compressed(p, pubs + 33 * i);
                    continue;
                }
                current = {p.x, p.y, Fe(1)};
            } else if (scalar != previous) {
                const auto& step = transition(scalar);
                ex369::point_j_add_affine(current, current, step);
            }
            if (current.is_inf()) throw std::runtime_error("unexpected point at infinity");
            previous = scalar;
            points[i] = current;
        }
        if (jump) serialize_batch(points, size, pubs);
    }
};

// HASH160 a block of compressed pubkeys, using the SIMD backend for full lanes
// and the scalar fallback for the ragged tail.
void hash_pubkeys(const ex369::MultiHash& hash, const std::uint8_t* pubs,
                  std::uint8_t* hashes, std::size_t size) {
    std::size_t i = 0;
    const auto lanes = static_cast<std::size_t>(hash.lanes);
    for (; i + lanes <= size; i += lanes)
        hash.hash160(pubs + 33 * i, hashes + 20 * i);
    for (; i < size; ++i)
        ex369::hash160_pubkey33(pubs + 33 * i, hashes + 20 * i);
}

// First-stage reject for the search hot loop: a bitmap keyed on the first four
// bytes of each HASH160. Almost every candidate clears the whole target set
// with a single bit test, so the costly full 20-byte lookup runs only on the
// rare prefix collision. The table size adapts to the target count to stay
// sparse (few false positives) while fitting a low cache level for small sets.
// It yields false positives but never false negatives, so the exact TargetSet
// behind it keeps the result correct.
struct PrefixFilter {
    unsigned shift;
    std::vector<std::uint64_t> words;

    explicit PrefixFilter(std::size_t targets) {
        unsigned bits = 16;
        while (bits < 26 && (std::size_t(1) << bits) < targets * 64) ++bits;
        shift = 32 - bits;
        words.assign((std::size_t(1) << bits) / 64, 0);
    }
    static std::uint32_t prefix(const std::uint8_t* h) {
        std::uint32_t p;
        std::memcpy(&p, h, 4);  // native order; add() and test() stay consistent
        return p;
    }
    std::size_t slot(const std::uint8_t* h) const { return prefix(h) >> shift; }
    void add(const std::uint8_t* h) {
        const auto i = slot(h);
        words[i >> 6] |= std::uint64_t(1) << (i & 63);
    }
    bool test(const std::uint8_t* h) const {
        const auto i = slot(h);
        return (words[i >> 6] >> (i & 63)) & 1;
    }
};
} // namespace

struct FixtureVerifier::Impl {
    CryptoKey known;
    Hash160 expected;
    const ex369::MultiHash& hash;
    PubkeyEngine engine;
    std::uint64_t checksum = 0;

    Impl(const CryptoKey& known_key, const std::string& ec, const std::string& hashing)
        : known(known_key), expected(reference_hash160(known)),
          hash(choose_hash(hashing)), engine(ec) {}

    std::size_t verify(const CryptoKey* keys, std::size_t size) {
        std::array<std::uint8_t, 33 * block_size> pubs;
        std::array<std::uint8_t, 20 * block_size> hashes;
        engine.make_pubkeys(keys, size, pubs.data());
        hash_pubkeys(hash, pubs.data(), hashes.data(), size);

        std::size_t hits = 0;
        for (std::size_t i = 0; i < size; ++i) {
            const auto* computed = hashes.data() + 20 * i;
            checksum += fold_digest(computed);
            if (keys[i] == known) {
                if (!std::equal(expected.begin(), expected.end(), computed)
                    || expected != reference_hash160(keys[i]))
                    throw std::runtime_error("independent fixture verification failed");
                ++hits;
            }
        }
        return hits;
    }
};

FixtureVerifier::FixtureVerifier(const CryptoKey& known_key, const std::string& ec,
                                 const std::string& hash)
    : impl_(std::make_unique<Impl>(known_key, ec, hash)) {}
FixtureVerifier::~FixtureVerifier() = default;
std::uint64_t FixtureVerifier::checksum64() const { return impl_->checksum; }

std::size_t FixtureVerifier::verify_batch(const CryptoKey* keys, std::size_t count) {
    if (!keys && count) throw std::invalid_argument("null batch");
    std::size_t hits = 0;
    while (count) {
        const auto n = std::min(count, block_size);
        hits += impl_->verify(keys, n);
        keys += n;
        count -= n;
    }
    return hits;
}

struct TargetSearch::Impl {
    std::shared_ptr<const TargetSet> targets;
    const ex369::MultiHash& hash;
    PubkeyEngine engine;
    PrefixFilter filter;

    Impl(std::shared_ptr<const TargetSet> t, const std::string& ec, const std::string& hashing)
        : targets(std::move(t)), hash(choose_hash(hashing)), engine(ec),
          filter(targets ? targets->size() : 0) {
        if (!targets || targets->empty()) throw std::invalid_argument("empty target set");
        for (const auto& target : *targets) filter.add(target.data());
    }

    std::size_t search(const CryptoKey* keys, std::size_t size, std::vector<SearchMatch>& out) {
        std::array<std::uint8_t, 33 * block_size> pubs;
        std::array<std::uint8_t, 20 * block_size> hashes;
        engine.make_pubkeys(keys, size, pubs.data());
        hash_pubkeys(hash, pubs.data(), hashes.data(), size);

        std::size_t hits = 0;
        for (std::size_t i = 0; i < size; ++i) {
            const std::uint8_t* computed = hashes.data() + 20 * i;
            if (!filter.test(computed)) continue;  // 4-byte prefix reject (hot path)
            Hash160 full;
            std::copy_n(computed, full.size(), full.begin());
            if (targets->find(full) == targets->end()) continue;
            // A reported win is re-derived through the independent reference
            // path, so a bug in the fast engine can never forge a match.
            if (reference_hash160(keys[i]) != full)
                throw std::runtime_error("independent search verification failed");
            out.push_back({keys[i], full});
            ++hits;
        }
        return hits;
    }
};

TargetSearch::TargetSearch(std::shared_ptr<const TargetSet> targets, const std::string& ec,
                           const std::string& hash)
    : impl_(std::make_unique<Impl>(std::move(targets), ec, hash)) {}
TargetSearch::~TargetSearch() = default;

std::size_t TargetSearch::search_batch(const CryptoKey* keys, std::size_t count,
                                       std::vector<SearchMatch>& out) {
    if (!keys && count) throw std::invalid_argument("null batch");
    std::size_t hits = 0;
    while (count) {
        const auto n = std::min(count, block_size);
        hits += impl_->search(keys, n, out);
        keys += n;
        count -= n;
    }
    return hits;
}
} // namespace exlottery

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace exlottery {

using CryptoKey = std::array<std::uint8_t, 32>;
using Hash160 = std::array<std::uint8_t, 20>;

// HASH160 outputs are already uniformly distributed, so the first machine word
// is an ideal bucket index for an open-addressed set of search targets.
struct Hash160Hash {
    std::size_t operator()(const Hash160& h) const noexcept {
        std::size_t word;
        std::memcpy(&word, h.data(), sizeof(word));
        return word;
    }
};
using TargetSet = std::unordered_set<Hash160, Hash160Hash>;

struct SearchMatch {
    CryptoKey key;   // the recovered private scalar, big-endian
    Hash160 hash;    // the target HASH160 it reproduced
};

struct FixtureIdentity {
    std::string wif, pubkey, hash160, address;
};
FixtureIdentity reference_identity(const CryptoKey& known_key);
Hash160 reference_hash160(const CryptoKey& known_key);
Hash160 compressed_pubkey_hash160(const std::string& text);
Hash160 parse_hash160(const std::string& text);
Hash160 decode_p2pkh(const std::string& address);
std::string digest_hex(const Hash160& digest);
// Stable fingerprint of a target set, used to bind a search checkpoint.
std::string targets_fingerprint(const TargetSet& targets);
bool crypto_sse_available();

// One instance per worker. Returns occurrences of its known fixture only.
class FixtureVerifier {
public:
    explicit FixtureVerifier(const CryptoKey& known_key,
                             const std::string& ec_backend = "jump",
                             const std::string& hash_backend = "auto");
    ~FixtureVerifier();
    std::uint64_t checksum64() const;
    std::size_t verify_batch(const CryptoKey* keys, std::size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One instance per worker. Hashes every candidate and reports the ones whose
// HASH160 belongs to the (unknown-key) target set. Each reported match is
// re-derived through libsecp256k1 before it is returned, so a defect in the
// fast engine can never produce a false win.
class TargetSearch {
public:
    explicit TargetSearch(std::shared_ptr<const TargetSet> targets,
                          const std::string& ec_backend = "jump",
                          const std::string& hash_backend = "auto");
    ~TargetSearch();
    // Appends matches found in [keys, keys+count) to `out`; returns their count.
    std::size_t search_batch(const CryptoKey* keys, std::size_t count,
                             std::vector<SearchMatch>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace exlottery

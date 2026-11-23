#pragma once
#include "exlottery/math.hpp"
#include "exlottery/scheduler.hpp"
#include "exlottery/crypto.hpp"
#include <memory>

namespace exlottery {
struct RunOptions {
    unsigned threads = 1;
    std::uint64_t batch = 4096;
    unsigned status = 0, checkpoint_interval = 30;
    bool reverse = false, affinity = false;
    bool machine_output = false, quiet = false, stop_on_hit = false;
    bool show_private = false;
    std::string backend = "jump", simd = "auto", checkpoint;
};

int run_fixture(const MixedRadix&, const Key&, const Metadata&, const RunOptions&,
                const Progress&);

// Real search: sweeps the wheel, hashes every candidate and reports the keys
// whose HASH160 is in `targets`. Writes each hit to `found_path` as it happens.
int run_search(const MixedRadix&, std::shared_ptr<const TargetSet> targets,
               const Metadata&, const RunOptions&, const Progress&,
               const std::string& found_path);
}

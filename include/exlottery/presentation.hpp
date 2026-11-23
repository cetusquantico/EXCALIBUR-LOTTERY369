#pragma once

#include "exlottery/engine.hpp"
#include <iosfwd>

namespace exlottery {
void print_plan(std::ostream& out, const MixedRadix& wheel, std::uint64_t rate);
void print_run(std::ostream& out, const MixedRadix& wheel, const Key& known,
               const RunOptions& options, const Progress& previous,
               const Progress& final, long double seconds);
}

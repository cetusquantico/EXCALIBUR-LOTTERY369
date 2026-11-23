#include "exlottery/math.hpp"
#include <stdexcept>

namespace exlottery {
Range puzzle_range(unsigned n) {
    if (n < 1 || n > 160) throw std::invalid_argument("puzzle must be in [1,160]");
    const unsigned first_min = 1u << ((n - 1) % 8);
    return {n, (n + 7) / 8, first_min, first_min * 2 - 1,
            U256(1) << (n - 1), (U256(1) << n) - 1};
}
} // namespace exlottery

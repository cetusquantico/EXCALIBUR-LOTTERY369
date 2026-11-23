#pragma once
#include "exlottery/math.hpp"
#include <string>
#include <string_view>

namespace exlottery {
struct SavedTicket {
    unsigned puzzle;
    Bytes first;
    Bytes shared;
};

SavedTicket read_saved_ticket(const std::string& name);
int launch_panel();
bool matches_public_puzzle(unsigned puzzle, std::string_view address);
}

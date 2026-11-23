#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace exlottery {

using Count = boost::multiprecision::uint256_t;
using Metadata = std::map<std::string, std::string>;

struct Segment {
    Count begin;
    Count end;  // Exclusive. Always greater than begin.
};

struct Progress {
    Count completed = 0;
    Count hits = 0;
    std::vector<Segment> ranges;  // Sorted, merged, durable work.
};

void validate_progress(const Count& total, const Progress& progress);

class Scheduler {
public:
    Scheduler(Count total, std::uint64_t batch, bool reverse, Progress previous = {});
    std::optional<Segment> reserve(bool stop_on_hit = false);
    void complete(Segment segment, std::uint64_t hits = 0);
    Progress snapshot() const;

private:
    std::uint64_t batch_;
    bool reverse_;
    Count completed_, hits_;
    std::map<Count, Count> remaining_, active_, done_;
    mutable std::mutex mutex_;
};

struct Checkpoint {
    Metadata metadata;
    Count total;
    bool reverse;
    Progress progress;
};

class CheckpointLock {
public:
    explicit CheckpointLock(const std::string& path);
    ~CheckpointLock();
    CheckpointLock(const CheckpointLock&) = delete;
    CheckpointLock& operator=(const CheckpointLock&) = delete;

private:
    int fd_;
};

void save_checkpoint(const std::string& path, const Checkpoint& checkpoint);
Checkpoint load_checkpoint(const std::string& path);

}  // namespace exlottery

#include "exlottery/scheduler.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace exlottery {

void validate_progress(const Count& total, const Progress& progress) {
    if (total == 0 || progress.hits > progress.completed || progress.completed > total)
        throw std::invalid_argument("invalid progress counters");
    Count covered = 0;
    Count previous_end = 0;
    bool first = true;
    for (const Segment& segment : progress.ranges) {
        if (segment.end <= segment.begin || segment.end > total ||
            (!first && segment.begin <= previous_end))
            throw std::invalid_argument("invalid or unmerged completed segments");
        covered += segment.end - segment.begin;
        previous_end = segment.end;
        first = false;
    }
    if (covered != progress.completed)
        throw std::invalid_argument("completed count differs from completed segments");
}

Scheduler::Scheduler(Count total, std::uint64_t batch, bool reverse, Progress previous)
    : batch_(batch), reverse_(reverse), completed_(previous.completed), hits_(previous.hits) {
    if (batch == 0)
        throw std::invalid_argument("batch must be positive");
    validate_progress(total, previous);
    Count cursor = 0;
    for (const Segment& segment : previous.ranges) {
        if (cursor < segment.begin)
            remaining_.emplace(cursor, segment.begin);
        done_.emplace(segment.begin, segment.end);
        cursor = segment.end;
    }
    if (cursor < total)
        remaining_.emplace(cursor, total);
}

std::optional<Segment> Scheduler::reserve(bool stop_on_hit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (remaining_.empty() || (stop_on_hit && hits_ != 0))
        return std::nullopt;
    auto it = reverse_ ? std::prev(remaining_.end()) : remaining_.begin();
    auto gap = remaining_.extract(it);
    const Count length = std::min(Count(batch_), gap.mapped() - gap.key());
    const Count begin = reverse_ ? gap.mapped() - length : gap.key();
    const Segment segment{begin, begin + length};
    if (reverse_)
        gap.mapped() = segment.begin;
    else
        gap.key() = segment.end;
    if (gap.key() < gap.mapped())
        remaining_.insert(std::move(gap));
    active_.emplace(segment.begin, segment.end);
    return segment;
}

void Scheduler::complete(Segment segment, std::uint64_t hits) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto active = active_.find(segment.begin);
    if (active == active_.end() || active->second != segment.end)
        throw std::invalid_argument("segment was not reserved or was already completed");
    const Count size = segment.end - segment.begin;
    if (hits > size)
        throw std::invalid_argument("hits exceed reserved segment length");

    auto entry = active_.extract(active);
    auto right = done_.lower_bound(segment.begin);
    if (right != done_.begin()) {
        auto left = std::prev(right);
        if (left->second == segment.begin) {
            entry.key() = left->first;
            done_.erase(left);
        }
    }
    if (right != done_.end() && right->first == segment.end) {
        entry.mapped() = right->second;
        done_.erase(right);
    }
    done_.insert(std::move(entry));
    completed_ += size;
    hits_ += hits;
}

Progress Scheduler::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Progress result{completed_, hits_, {}};
    result.ranges.reserve(done_.size());
    for (const auto& item : done_)
        result.ranges.push_back({item.first, item.second});
    return result;
}

}  // namespace exlottery

#include "exlottery/scheduler.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace exlottery;

namespace {

void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

template<class F>
void reject(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("expected rejected operation");
}

class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/exlottery-scheduler-test-XXXXXX";
        if (!::mkdtemp(pattern))
            throw std::runtime_error("mkdtemp failed");
        path = pattern;
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void parallel_coverage(unsigned threads, bool reverse) {
    constexpr unsigned size = 1009;
    Scheduler scheduler(size, 7, reverse);
    std::vector<std::atomic<unsigned>> counts(size);
    for (auto& n : counts)
        n.store(0);
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < threads; ++i)
        pool.emplace_back([&] {
            while (auto segment = scheduler.reserve()) {
                unsigned hits = 0;
                for (auto pos = segment->begin; pos < segment->end; ++pos) {
                    auto j = pos.convert_to<unsigned>();
                    ++counts.at(j);
                    if (j == 504)
                        ++hits;
                }
                scheduler.complete(*segment, hits);
            }
        });
    for (auto& worker : pool)
        worker.join();
    for (const auto& n : counts)
        check(n.load() == 1, "missed or repeated ordinal");
    auto progress = scheduler.snapshot();
    check(progress.completed == size && progress.hits == 1, "concurrent counters");
    check(progress.ranges.size() == 1 && progress.ranges[0].begin == 0 &&
              progress.ranges[0].end == size, "merged final range");
}

void large_domain() {
    const Count max = std::numeric_limits<Count>::max();
    const Count mid = Count(1) << 150;
    Progress prior{max - 6, 5, {{0, mid}, {mid + 2, max - 4}}};
    Scheduler reverse(max, UINT64_MAX, true, prior);
    auto last = reverse.reserve().value();
    auto middle = reverse.reserve().value();
    check(last.begin == max - 4 && last.end == max, "uint256 endpoint overflow");
    check(middle.begin == mid && middle.end == mid + 2, "gap above bit 128");
    reverse.complete(middle);
    reverse.complete(last, 4);
    auto finished = reverse.snapshot();
    check(finished.completed == max && finished.hits == 9, "large domain totals");
    check(finished.ranges.size() == 1 && !reverse.reserve(), "large domain coverage");
}

void checkpoint_round_trip() {
    TempDir dir;
    const std::string file = (dir.path / "job.json").string();
    {
        CheckpointLock lock(file);
        reject([&] { CheckpointLock second(file); });
    }
    CheckpointLock reacquired(file);
    const Count total = 21;
    Scheduler first(total, 5, false);
    auto a = first.reserve().value();
    auto lost = first.reserve().value();
    auto c = first.reserve().value();
    check(lost.begin == 5, "reserved abandoned segment");
    first.complete(c);
    first.complete(a, 1);
    Metadata meta{{"allow", "FE,FF"}, {"escapes\\\"", "A\nB\tC"}, {"empty", ""}};
    save_checkpoint(file, {meta, total, false, first.snapshot()});
    auto invalid = first.snapshot();
    ++invalid.completed;
    reject([&] { save_checkpoint(file, {meta, total, false, invalid}); });
    // A rejected replacement must leave the last durable checkpoint readable.
    auto cp = load_checkpoint(file);
    check(cp.metadata == meta && cp.progress.completed == 10 && cp.progress.hits == 1,
          "snapshot metadata or counters changed");
    check(cp.progress.ranges.size() == 2, "pending reservation incorrectly persisted");
    Scheduler resumed(cp.total, 3, cp.reverse, cp.progress);
    unsigned mask = 0;
    while (auto s = resumed.reserve()) {
        for (auto pos = s->begin; pos < s->end; ++pos)
            mask |= 1U << pos.convert_to<unsigned>();
        resumed.complete(*s);
    }
    check(mask == ((31U << 5) | (63U << 15)), "replay coverage differs from complement");
    save_checkpoint(file, {meta, total, false, resumed.snapshot()});
    check(load_checkpoint(file).progress.completed == 21, "atomic overwrite failed");

    std::ifstream in(file);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    std::string damaged = bytes;
    damaged.at(damaged.find("FE,FF")) = '0';
    for (const auto& bad : {std::string(), bytes.substr(0, bytes.size() / 2), damaged, bytes + "X"}) {
        std::ofstream(file, std::ios::binary) << bad;
        reject([&] { load_checkpoint(file); });
    }
    reject([&] { save_checkpoint((dir.path / "missing" / "job").string(), cp); });
    const Count max = std::numeric_limits<Count>::max();
    const Count end = (Count(1) << 159) + 43;
    save_checkpoint(file, {meta, max, true, {3, 0, {{end - 3, end}}}});
    auto wide = load_checkpoint(file);
    check(wide.reverse && wide.total == max && wide.progress.ranges.at(0).end == end,
          "wide checkpoint conversion");
}

}  // namespace

int main() {
    try {
        for (unsigned n : {1, 2, 4})
            for (bool reverse : {false, true})
                parallel_coverage(n, reverse);
        large_domain();
        checkpoint_round_trip();
        reject([] { Scheduler s(0, 1, false); });
        reject([] { Scheduler s(9, 0, false); });
        reject([] { Scheduler s(9, 1, false, {6, 0, {{0, 3}, {2, 5}}}); });
        reject([] { Scheduler s(9, 1, false, {6, 0, {{0, 3}, {3, 6}}}); });
        reject([] { Scheduler s(9, 1, false, {2, 0, {{0, 4}}}); });
        reject([] { Scheduler s(9, 1, false, {1, 2, {{1, 2}}}); });
        reject([] { Scheduler s(9, 1, false, {1, 0, {{9, 10}}}); });
        Scheduler s(9, 1, false);
        auto seg = s.reserve().value();
        reject([&] { s.complete({seg.begin, seg.end + 1}); });
        reject([&] { s.complete(seg, 2); });
        s.complete(seg);
        reject([&] { s.complete(seg); });
        std::cout << "scheduler and checkpoint tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

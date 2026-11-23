#include "exlottery/engine.hpp"
#include "exlottery/crypto.hpp"
#include "exlottery/presentation.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace exlottery {
namespace {
constexpr std::uint64_t block_size = 256;
using Clock = std::chrono::steady_clock;
std::atomic<bool> interrupted{false};
static_assert(decltype(interrupted)::is_always_lock_free);
void stop_signal(int) { interrupted.store(true, std::memory_order_relaxed); }

std::vector<int> available_cpus() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed))
        throw std::system_error(errno, std::generic_category(), "sched_getaffinity");
    std::vector<int> result;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &allowed)) result.push_back(i);
    if (result.empty()) throw std::runtime_error("empty CPU affinity mask");
    return result;
}

void pin_to(int cpu) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    int err = pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
    if (err) throw std::system_error(err, std::generic_category(), "worker affinity");
}

// At most one occurrence of the known key can lie in disjoint confirmed ranges.
void check_fixture_progress(const MixedRadix& wheel, const Key& key, const Progress& p) {
    U256 rank;
    try {
        rank = wheel.key_to_rank(key);
    } catch (const std::out_of_range&) {
        if (p.hits != 0) throw std::runtime_error("unexpected excluded fixture hit");
        return;
    }
    const bool covered = std::any_of(p.ranges.begin(), p.ranges.end(), [&](const Segment& s) {
        return Count(rank) >= s.begin && Count(rank) < s.end;
    });
    if (p.hits != unsigned(covered)) throw std::runtime_error("fixture coverage mismatch");
}
}

int run_fixture(const MixedRadix& wheel, const Key& known, const Metadata& metadata,
                const RunOptions& opt, const Progress& previous) {
    const Count total(wheel.total_candidates());
    // On a hit, only blocks already in flight remain to be committed.
    const auto reservation = opt.stop_on_hit ? std::min(opt.batch, block_size) : opt.batch;
    Scheduler scheduler(total, reservation, opt.reverse, previous);
    if (previous.completed != 0) check_fixture_progress(wheel, known, previous);
    auto persist = [&](const Progress& progress) {
        if (!opt.checkpoint.empty())
            save_checkpoint(opt.checkpoint, {metadata, total, opt.reverse, progress});
    };
    interrupted = false;
    std::signal(SIGINT, stop_signal);
    std::signal(SIGTERM, stop_signal);
    persist(previous);
    const auto cpus = opt.affinity ? available_cpus() : std::vector<int>{};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable changed;
    unsigned finished = 0;
    std::exception_ptr error;
    const auto start = Clock::now();
    auto worker = [&](unsigned id) {
        try {
            if (opt.affinity) pin_to(cpus[id % cpus.size()]);
            FixtureVerifier verifier(known, opt.backend, opt.simd);
            std::array<Key, block_size> keys;
            while (!stop.load() && !interrupted.load(std::memory_order_relaxed)) {
                auto part = scheduler.reserve(opt.stop_on_hit);
                if (!part) break;
                auto left = (part->end - part->begin).convert_to<std::uint64_t>();
                Key cursor = wheel.rank_to_key(U256(opt.reverse ? part->end - 1 : part->begin));
                std::uint64_t hits = 0;
                while (left && !stop.load() && !interrupted.load(std::memory_order_relaxed)) {
                    const auto count = std::min<std::uint64_t>(keys.size(), left);
                    for (std::size_t i = 0; i < count; ++i) {
                        keys[i] = cursor;
                        if (i + 1 == left) break;
                        bool advanced = opt.reverse ? wheel.previous_candidate(cursor)
                                                    : wheel.next_candidate(cursor);
                        if (!advanced) throw std::logic_error("unexpected ticket boundary");
                    }
                    hits += verifier.verify_batch(keys.data(), count);
                    left -= count;
                }
                if (!left) scheduler.complete(*part, hits);
            }
        } catch (...) {
            std::lock_guard<std::mutex> guard(mutex);
            if (!error) error = std::current_exception();
            stop = true;
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            ++finished;
        }
        changed.notify_one();
    };

    std::vector<std::thread> workers;
    try {
        auto last_status = start, last_save = start;
        for (unsigned id = 0; id < opt.threads; ++id) workers.emplace_back(worker, id);
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(150), [&]{ return finished == workers.size(); });
            if (finished == workers.size()) break;
            lock.unlock();
            const auto now = Clock::now();
            if (!opt.quiet && opt.status && now-last_status >= std::chrono::seconds(opt.status)) {
                std::cerr << (opt.machine_output ? "completed=" : "Candidatos confirmados: ")
                          << scheduler.snapshot().completed << '/' << total << '\n';
                last_status = now;
            }
            if (!opt.checkpoint.empty() && now-last_save >= std::chrono::seconds(opt.checkpoint_interval)) {
                persist(scheduler.snapshot());
                last_save = now;
            }
        }
    } catch (...) {
        stop = true;
        for (auto& t : workers) t.join();
        throw;
    }
    for (auto& t : workers) t.join();
    if (error) std::rethrow_exception(error);
    const auto p = scheduler.snapshot();
    const long double seconds = std::chrono::duration<long double>(Clock::now()-start).count();
    check_fixture_progress(wheel, known, p);
    persist(p);
    const bool hit_stop = opt.stop_on_hit && p.hits != 0;
    const bool success = p.completed == total || hit_stop;
    if (!interrupted && !success) throw std::runtime_error("incomplete coverage");
    if (opt.machine_output) {
        if (opt.stop_on_hit)
            std::cout << "stop_reason=" << (hit_stop ? "confirmed_hit" : (success ? "exhausted" : "interrupted")) << '\n';
        std::cout << "completed=" << p.completed << "\nfixture_hits=" << p.hits
                  << "\nsession_completed=" << p.completed - previous.completed
                  << "\nseconds=" << std::fixed << std::setprecision(6) << seconds
                  << "\ncandidates_per_second=" << std::setprecision(2)
                  << (p.completed - previous.completed).convert_to<long double>() / seconds << '\n';
    } else {
        print_run(std::cout, wheel, known, opt, previous, p, seconds);
    }
    return success ? 0 : 130;
}

namespace {
std::string utc_timestamp() {
    char buffer[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}
}

int run_search(const MixedRadix& wheel, std::shared_ptr<const TargetSet> targets,
               const Metadata& metadata, const RunOptions& opt, const Progress& previous,
               const std::string& found_path) {
    if (!targets || targets->empty())
        throw std::invalid_argument("a busca exige pelo menos um alvo (--address/--hash160/--targets)");
    const Count total(wheel.total_candidates());
    // On a hit, only blocks already in flight remain to be committed.
    const auto reservation = opt.stop_on_hit ? std::min(opt.batch, block_size) : opt.batch;
    Scheduler scheduler(total, reservation, opt.reverse, previous);
    auto persist = [&](const Progress& progress) {
        if (!opt.checkpoint.empty())
            save_checkpoint(opt.checkpoint, {metadata, total, opt.reverse, progress});
    };
    interrupted = false;
    std::signal(SIGINT, stop_signal);
    std::signal(SIGTERM, stop_signal);
    persist(previous);
    const auto cpus = opt.affinity ? available_cpus() : std::vector<int>{};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable changed;
    unsigned finished = 0;
    std::exception_ptr error;

    std::mutex hit_mutex;
    std::vector<SearchMatch> all_matches;
    std::ofstream found_file(found_path, std::ios::app);
    if (!found_file) throw std::runtime_error("não foi possível abrir o arquivo de achados: " + found_path);
    const auto start = Clock::now();

    // Persist and announce one confirmed key. Caller holds hit_mutex.
    auto report = [&](const SearchMatch& match) {
        const auto identity = reference_identity(match.key);
        std::ostringstream record;
        record << "=== HIT ===\n"
               << "private  : " << hex_key(match.key) << '\n'
               << "wif      : " << identity.wif << '\n'
               << "pubkey   : " << identity.pubkey << '\n'
               << "hash160  : " << identity.hash160 << '\n'
               << "address  : " << identity.address << '\n'
               << "puzzle   : " << wheel.range.n << '\n'
               << "ticket   : " << wheel.id << '\n'
               << "found_utc: " << utc_timestamp() << '\n';
        found_file << record.str() << std::flush;
        if (opt.machine_output) {
            std::cout << "found_private=" << hex_key(match.key) << "\nfound_wif=" << identity.wif
                      << "\nfound_pubkey=" << identity.pubkey << "\nfound_hash160=" << identity.hash160
                      << "\nfound_address=" << identity.address << '\n';
        } else {
            std::cout << "\n*** CHAVE ENCONTRADA ***\n" << record.str() << '\n';
        }
        std::cout.flush();
    };

    auto worker = [&](unsigned id) {
        try {
            if (opt.affinity) pin_to(cpus[id % cpus.size()]);
            TargetSearch searcher(targets, opt.backend, opt.simd);
            std::array<Key, block_size> keys;
            std::vector<SearchMatch> found;
            while (!stop.load() && !interrupted.load(std::memory_order_relaxed)) {
                auto part = scheduler.reserve(opt.stop_on_hit);
                if (!part) break;
                auto left = (part->end - part->begin).convert_to<std::uint64_t>();
                Key cursor = wheel.rank_to_key(U256(opt.reverse ? part->end - 1 : part->begin));
                std::uint64_t hits = 0;
                while (left && !stop.load() && !interrupted.load(std::memory_order_relaxed)) {
                    const auto count = std::min<std::uint64_t>(keys.size(), left);
                    for (std::size_t i = 0; i < count; ++i) {
                        keys[i] = cursor;
                        if (i + 1 == left) break;
                        bool advanced = opt.reverse ? wheel.previous_candidate(cursor)
                                                    : wheel.next_candidate(cursor);
                        if (!advanced) throw std::logic_error("unexpected ticket boundary");
                    }
                    found.clear();
                    hits += searcher.search_batch(keys.data(), count, found);
                    if (!found.empty()) {
                        std::lock_guard<std::mutex> guard(hit_mutex);
                        for (const auto& match : found) {
                            all_matches.push_back(match);
                            report(match);
                        }
                        if (opt.stop_on_hit) stop = true;
                    }
                    left -= count;
                }
                if (!left) scheduler.complete(*part, hits);
            }
        } catch (...) {
            std::lock_guard<std::mutex> guard(mutex);
            if (!error) error = std::current_exception();
            stop = true;
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            ++finished;
        }
        changed.notify_one();
    };

    std::vector<std::thread> workers;
    try {
        auto last_status = start, last_save = start;
        for (unsigned id = 0; id < opt.threads; ++id) workers.emplace_back(worker, id);
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(150), [&]{ return finished == workers.size(); });
            if (finished == workers.size()) break;
            lock.unlock();
            const auto now = Clock::now();
            if (!opt.quiet && opt.status && now-last_status >= std::chrono::seconds(opt.status)) {
                std::cerr << (opt.machine_output ? "completed=" : "Candidatos varridos: ")
                          << scheduler.snapshot().completed << '/' << total << '\n';
                last_status = now;
            }
            if (!opt.checkpoint.empty() && now-last_save >= std::chrono::seconds(opt.checkpoint_interval)) {
                persist(scheduler.snapshot());
                last_save = now;
            }
        }
    } catch (...) {
        stop = true;
        for (auto& t : workers) t.join();
        throw;
    }
    for (auto& t : workers) t.join();
    if (error) std::rethrow_exception(error);
    const auto p = scheduler.snapshot();
    const long double seconds = std::chrono::duration<long double>(Clock::now()-start).count();
    persist(p);
    const bool found_any = !all_matches.empty();
    const bool exhausted = p.completed == total;
    const bool success = found_any || exhausted;
    if (!interrupted && !success) throw std::runtime_error("incomplete coverage");
    const Count session = p.completed - previous.completed;
    if (opt.machine_output) {
        if (opt.stop_on_hit)
            std::cout << "stop_reason=" << (found_any ? "confirmed_hit" : (exhausted ? "exhausted" : "interrupted")) << '\n';
        std::cout << "completed=" << p.completed << "\nmatches=" << all_matches.size()
                  << "\nsession_completed=" << session
                  << "\nseconds=" << std::fixed << std::setprecision(6) << seconds
                  << "\ncandidates_per_second=" << std::setprecision(2)
                  << (seconds > 0 ? session.convert_to<long double>() / seconds : 0) << '\n';
    } else {
        std::cout << "=== busca ===\n"
                  << "  Puzzle            : " << wheel.range.n << '\n'
                  << "  Alvos             : " << targets->size() << '\n'
                  << "  Motor             : " << opt.backend << " / threads " << opt.threads << '\n'
                  << "  Tempo da sessão   : " << std::fixed << std::setprecision(3) << seconds << " s\n"
                  << "  Candidatos/s      : " << std::setprecision(0)
                  << (seconds > 0 ? session.convert_to<long double>() / seconds : 0) << '\n'
                  << "  Total varrido     : " << p.completed << " de " << total << '\n'
                  << "  Chaves achadas    : " << all_matches.size() << '\n'
                  << "  Estado            : "
                  << (found_any ? "acerto confirmado" : (exhausted ? "concluído" : "interrompido")) << '\n';
        if (found_any) std::cout << "  Arquivo           : " << found_path << '\n';
    }
    return success ? 0 : 130;
}
}

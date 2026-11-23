#include "exlottery/engine.hpp"
#include "exlottery/catalog.hpp"
#include "exlottery/presentation.hpp"
#include "exlottery/crypto.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace exlottery {
namespace {
constexpr char help[] = R"(EXCALIBUR LOTTERY369 1.0.3 — planejamento e validação de fixtures
  exlottery --check-pubkey HEX_COMPRIMIDA --address P2PKH [--output human|kv]
  exlottery --puzzle N [listas] --dry-run [--address P2PKH] [--rate candidatos/s]
  exlottery --puzzle N [listas] --fixture NOME [execução]
  exlottery --puzzle N [listas] --known-key HEX [--hash160 HASH | --address P2PKH]
  exlottery --puzzle N [listas] --search (--address P2PKH | --hash160 HASH | --targets ARQ) [--found ARQ]
  exlottery --resume ARQUIVO [opções de execução]
  exlottery --panel
  exlottery --volante NOME [--dry-run | --known-key HEX]

Listas (escolha uma fonte para cada posição, padrão = todos os bytes legais)
  --first-allow HH,HH   --first-allow-file ARQ   --first-draw-size N
  --allow HH,HH         --allow-file ARQ         --draw-size N
  --draw-seed N         obrigatório para sorteios
Fixtures
  solved-1, solved-39, synthetic-39, synthetic-160
Execução
  --output human|kv     --quiet              --stop-on-hit
  --show-private        inclui HEX, bytes ativos e WIF da fixture na saída redirecionada
  --threads N           --batch N            --affinity on|off
  --mode sequential|reverse                  --status SEGUNDOS
  --backend reference|jump                   --simd auto|scalar|sse|avx2
  --checkpoint ARQ      --resume ARQ         --checkpoint-interval SEGUNDOS
  --dry-run --address valida o endereço público do puzzle e planeja o volante.
  Na execução de fixtures, --hash160 e --address conferem a identidade conhecida.
Busca real (chave desconhecida)
  --search              varre o volante e compara o HASH160 de cada candidato
  --address P2PKH       alvo a procurar (endereço mainnet)
  --hash160 HASH        alvo a procurar (20 bytes em hex)
  --targets ARQ         vários alvos, um por linha (endereço ou HASH160; # = comentário)
  --found ARQ           arquivo onde cada acerto é gravado (padrão found.txt)
)";

std::string get(const Metadata& map, const std::string& key, const std::string& fallback = {}) {
    auto it = map.find(key);
    return it == map.end() ? fallback : it->second;
}
Metadata arguments(int argc, char** argv) {
    const std::set<std::string> names = {"puzzle","first-allow","first-allow-file",
        "first-draw-size","allow","allow-file","draw-size","draw-seed","rate","threads",
        "batch","mode","status","checkpoint","resume","checkpoint-interval","affinity",
        "simd","backend","fixture","known-key","address","hash160","dry-run","help",
        "quiet","panel","volante","output","stop-on-hit","show-private","check-pubkey",
        "search","targets","found"};
    const std::set<std::string> flags = {"dry-run","help","quiet","panel","stop-on-hit",
        "show-private","search"};
    Metadata args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.substr(0,2) != "--" || !names.count(arg.substr(2)))
            throw std::invalid_argument("unknown option: " + arg);
        auto name = arg.substr(2);
        if (args.count(name)) throw std::invalid_argument("repeated option: " + arg);
        std::string value = "on";
        if (!flags.count(name)) {
            ++i;
            value = i < argc ? argv[i] : "";
            if (value.rfind("--", 0) == 0 || value.find_first_not_of(" \t\r\n") == std::string::npos) {
                if (name == "known-key")
                    throw std::invalid_argument("--known-key exige a HEX de uma chave de teste conhecida (1 a 64 dígitos).");
                throw std::invalid_argument("missing value: " + arg);
            }
        }
        args[name] = value;
    }
    return args;
}
std::uint64_t number(const std::string& text, std::uint64_t low, std::uint64_t high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned decimal: " + text);
    auto value = parse_uint256(text);
    if (value < low || value > high) throw std::invalid_argument("number outside limits: " + text);
    return value.convert_to<std::uint64_t>();
}
void choice(const std::string& value, const std::set<std::string>& accepted) {
    if (!accepted.count(value)) throw std::invalid_argument("invalid choice: " + value);
}

struct Selection { Bytes bytes; std::string draw_size; };
Selection select_bytes(const Metadata& args, const Metadata& saved, const Range& r,
                       bool first, const std::string& seed) {
    const std::string name = first ? "first-allow" : "allow";
    const std::string draw = first ? "first-draw-size" : "draw-size";
    if (args.count(name) + args.count(name+"-file") + args.count(draw) > 1)
        throw std::invalid_argument("conflicting sources for " + name);
    if (args.count(name)) return {parse_allow(args.at(name)), ""};
    if (args.count(name+"-file")) return {read_allow_file(args.at(name+"-file")), ""};
    auto pool = first ? all_bytes(r.first_min,r.first_max) : all_bytes();
    auto size = get(args,draw,get(saved,draw));
    if (!size.empty()) {
        if (seed.empty()) throw std::invalid_argument("draw requires --draw-seed");
        unsigned n = number(size,1,pool.size());
        return {draw_allow(pool,n,number(seed,0,UINT64_MAX),first),std::to_string(n)};
    }
    if (saved.count(name)) return {parse_allow(saved.at(name)), ""};
    return {pool, ""};
}

struct Fixture { const char* name; unsigned puzzle; const char* hex; };
const Fixture fixtures[] = {
    {"solved-1",1,"01"}, {"solved-39",39,"4b5f8303e9"},
    {"synthetic-39",39,"4001020304"},
    {"synthetic-160",160,"8000010001000100010001000100010001000100"}
};
Key known_key(const Metadata& args, const Metadata& saved, unsigned puzzle,
              const std::optional<Hash160>& assertion) {
    if (args.count("fixture") && args.count("known-key"))
        throw std::invalid_argument("select a single fixture");
    if (args.count("known-key") || (!args.count("fixture") && saved.count("known-key"))) {
        auto hex = get(args,"known-key",get(saved,"known-key"));
        if (hex.empty() || hex.size() > 64) throw std::invalid_argument("expected 1..64 hexadecimal digits");
        return to_key(parse_uint256("0x"+hex));
    }
    for (const auto& fixture : fixtures) {
        if (puzzle != fixture.puzzle) continue;
        auto key = to_key(parse_uint256(std::string("0x")+fixture.hex));
        if (args.count("fixture")) {
            if (fixture.name == args.at("fixture")) return key;
        } else if (assertion && reference_hash160(key) == *assertion) return key;
    }
    if (args.count("fixture"))
        throw std::invalid_argument("A fixture selecionada não pertence ao catálogo deste puzzle.");
    if (assertion && args.count("address"))
        throw std::invalid_argument("Endereço público validado. Use --dry-run para planejar este volante. "
                                    "A execução criptográfica está disponível apenas para fixtures conhecidas.");
    if (assertion)
        throw std::invalid_argument("O endereço ou HASH160 não corresponde a uma fixture conhecida deste puzzle. "
                                    "A execução criptográfica requer uma chave de teste conhecida.");
    throw std::invalid_argument("Escolha uma fixture do puzzle ou informe a HEX da chave de teste com --known-key.");
}

std::string trim(const std::string& line) {
    const auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = line.find_last_not_of(" \t\r\n");
    return line.substr(b, e - b + 1);
}

// Build the search target set from --address, --hash160 and/or --targets FILE.
// Each entry may be a mainnet P2PKH address or a 40-hex HASH160.
std::shared_ptr<TargetSet> load_targets(const Metadata& args) {
    auto targets = std::make_shared<TargetSet>();
    if (args.count("address")) targets->insert(decode_p2pkh(args.at("address")));
    if (args.count("hash160")) targets->insert(parse_hash160(args.at("hash160")));
    auto it = args.find("targets");
    if (it != args.end()) {
        std::ifstream file(it->second);
        if (!file) throw std::invalid_argument("não foi possível abrir --targets: " + it->second);
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(file, line)) {
            ++lineno;
            const std::string token = trim(line);
            if (token.empty() || token.front() == '#') continue;
            try {
                if (token.size() == 40) targets->insert(parse_hash160(token));
                else targets->insert(decode_p2pkh(token));
            } catch (const std::exception& ex) {
                throw std::invalid_argument("--targets linha " + std::to_string(lineno) + ": " + ex.what());
            }
        }
    }
    if (targets->empty())
        throw std::invalid_argument("a busca exige --address, --hash160 ou --targets com ao menos um alvo");
    return targets;
}

// Canonical (sorted) comma-joined HASH160 list, stored in the checkpoint so a
// resumed search is self-contained and needs no target arguments again.
std::string encode_targets(const TargetSet& targets) {
    std::vector<std::string> hexes;
    hexes.reserve(targets.size());
    for (const auto& h : targets) hexes.push_back(digest_hex(h));
    std::sort(hexes.begin(), hexes.end());
    std::string out;
    for (const auto& h : hexes) { if (!out.empty()) out += ','; out += h; }
    return out;
}
std::shared_ptr<TargetSet> decode_targets(const std::string& csv) {
    auto targets = std::make_shared<TargetSet>();
    for (std::size_t i = 0; i < csv.size();) {
        auto j = csv.find(',', i);
        if (j == std::string::npos) j = csv.size();
        targets->insert(parse_hash160(csv.substr(i, j - i)));
        i = j + 1;
    }
    if (targets->empty()) throw std::invalid_argument("checkpoint de busca sem alvos");
    return targets;
}

int execute(Metadata args) {
    if (args.empty() || args.count("help")) { std::cout << help; return 0; }
    if (args.count("check-pubkey")) {
        if (!args.count("address") || args.size() != 2 + args.count("output"))
            throw std::invalid_argument("Use --check-pubkey HEX --address P2PKH com --output opcional.");
        const auto output = get(args,"output","human");
        choice(output,{"human","kv"});
        const auto& address = args.at("address");
        const auto expected = decode_p2pkh(address);
        const auto computed = compressed_pubkey_hash160(args.at("check-pubkey"));
        if (computed != expected)
            throw std::invalid_argument("A pubkey comprimida e o endereço P2PKH não correspondem.");
        if (output == "kv")
            std::cout << "public_key_match=1\naddress=" << address << "\nhash160=" << digest_hex(computed) << '\n';
        else
            std::cout << "Pubkey comprimida confirmada para " << address << "\nHASH160 " << digest_hex(computed) << '\n';
        return 0;
    }
    if (args.count("panel")) {
        if (args.size() != 1) throw std::invalid_argument("Use --panel sozinho para abrir a interface");
        return launch_panel();
    }
    if (args.count("volante")) {
        for (const auto* option : {"resume","allow","first-allow","allow-file","first-allow-file",
                                   "draw-size","first-draw-size","draw-seed"})
            if (args.count(option))
                throw std::invalid_argument(std::string("--volante é incompatível com --") + option);
        const auto ticket = read_saved_ticket(args.at("volante"));
        if (args.count("puzzle") && number(args.at("puzzle"),1,160) != ticket.puzzle)
            throw std::invalid_argument("O puzzle informado difere do volante salvo");
        args["puzzle"] = std::to_string(ticket.puzzle);
        args["first-allow"] = format_allow(ticket.first);
        args["allow"] = format_allow(ticket.shared);
    }
    const bool dry = args.count("dry-run");
    RunOptions opt;
    const auto output = get(args,"output","human");
    choice(output,{"human","kv"});
    opt.machine_output = output == "kv";
    opt.quiet = args.count("quiet");
    opt.show_private = args.count("show-private") || isatty(STDOUT_FILENO);
    opt.stop_on_hit = args.count("stop-on-hit");
    if (args.count("checkpoint") && args.count("resume"))
        throw std::invalid_argument("select --checkpoint or --resume");
    opt.checkpoint = get(args,"resume",get(args,"checkpoint"));
    std::optional<CheckpointLock> lock;
    if (!dry && args.count("resume")) lock.emplace(opt.checkpoint);
    std::optional<Checkpoint> checkpoint;
    if (args.count("resume")) checkpoint = load_checkpoint(opt.checkpoint);
    const Metadata saved = checkpoint ? checkpoint->metadata : Metadata{};
    const auto range = puzzle_range(number(get(args,"puzzle",get(saved,"puzzle")),1,160));
    auto seed = get(args,"draw-seed",get(saved,"draw-seed"));
    if (!seed.empty()) seed = std::to_string(number(seed,0,UINT64_MAX));
    const auto first = select_bytes(args,saved,range,true,seed);
    const auto shared = select_bytes(args,saved,range,false,seed);
    if (!seed.empty() && first.draw_size.empty() && shared.draw_size.empty())
        throw std::invalid_argument("seed requires a draw size");
    MixedRadix wheel(range,first.bytes,shared.bytes);
    auto mode = get(args,"mode", checkpoint && checkpoint->reverse ? "reverse" : "sequential");
    auto affinity = get(args,"affinity","off");
    opt.backend = get(args,"backend",opt.backend);
    opt.simd = get(args,"simd",opt.simd);
    choice(mode,{"sequential","reverse"}); choice(affinity,{"on","off"});
    choice(opt.backend,{"jump","reference"}); choice(opt.simd,{"auto","scalar","sse","avx2"});
    if (opt.simd == "sse" && !crypto_sse_available()) throw std::invalid_argument("SSSE3 unavailable");
    if (opt.simd == "avx2" && !crypto_avx2_available()) throw std::invalid_argument("AVX2 unavailable");
    opt.reverse = mode == "reverse"; opt.affinity = affinity == "on";
    opt.batch = number(get(args,"batch","4096"),1,UINT64_MAX);
    opt.threads = number(get(args,"threads","1"),1,1024);
    opt.status = number(get(args,"status","0"),0,UINT32_MAX);
    opt.checkpoint_interval = number(get(args,"checkpoint-interval","30"),1,UINT32_MAX);
    auto rate = number(get(args,"rate","1000000"),1,UINT64_MAX);
    const auto total = wheel.total_candidates();
    const auto space = range.hi - range.lo + 1;
    Metadata meta{{"puzzle",std::to_string(range.n)},{"low",hex_key(to_key(range.lo))},
        {"high",hex_key(to_key(range.hi))},{"first-allow",format_allow(wheel.first)},
        {"allow",format_allow(wheel.shared)},{"ticket_id",wheel.id},{"draw-seed",seed},
        {"draw-size",shared.draw_size},{"first-draw-size",first.draw_size},
        {"draw-algorithm",seed.empty() ? "none" : draw_algorithm}};
    const bool search_mode = args.count("search") ||
        (saved.count("mode_kind") && saved.at("mode_kind") == "search");
    if (search_mode) {
        for (const char* bad : {"fixture","known-key"})
            if (args.count(bad))
                throw std::invalid_argument(std::string("--search não usa --") + bad);
        std::shared_ptr<TargetSet> targets;
        if (args.count("address") || args.count("hash160") || args.count("targets"))
            targets = load_targets(args);
        else if (saved.count("targets"))
            targets = decode_targets(saved.at("targets"));
        else
            throw std::invalid_argument("a busca exige --address, --hash160 ou --targets");
        meta["mode_kind"] = "search";
        meta["targets_count"] = std::to_string(targets->size());
        meta["targets_digest"] = targets_fingerprint(*targets);
        meta["targets"] = encode_targets(*targets);
        if (checkpoint && (checkpoint->metadata != meta || checkpoint->reverse != opt.reverse ||
                           checkpoint->total != Count(total)))
            throw std::invalid_argument("checkpoint configuration mismatch");
        if (!dry && args.count("checkpoint")) {
            lock.emplace(opt.checkpoint);
            if (std::filesystem::exists(opt.checkpoint))
                throw std::invalid_argument("checkpoint exists; use --resume");
        }
        if (opt.machine_output) {
            std::cout << "puzzle=" << range.n << "\nlow=" << meta.at("low") << "\nhigh=" << meta.at("high")
                      << "\nactive_bytes=" << range.length << "\nticket_id=" << wheel.id
                      << "\ntotal=" << total << "\ntargets=" << targets->size()
                      << "\nmode_kind=search\nexecution_kind=" << (dry ? "search_plan" : "search") << '\n';
        } else if (dry) {
            print_plan(std::cout, wheel, rate);
            std::cout << "  Alvos de busca    : " << targets->size() << '\n';
        } else if (!opt.quiet) {
            std::cout << "Busca no puzzle " << range.n << "  |  " << total << " candidatos  |  "
                      << targets->size() << " alvo" << (targets->size() == 1 ? "" : "s") << "  |  "
                      << opt.threads << " thread" << (opt.threads == 1 ? "" : "s") << '\n';
        }
        std::cout.flush();
        if (dry) return 0;
        return run_search(wheel, targets, meta, opt, checkpoint ? checkpoint->progress : Progress{},
                          get(args,"found","found.txt"));
    }
    if (args.count("hash160") && args.count("address"))
        throw std::invalid_argument("select --hash160 or --address");
    std::optional<Hash160> assertion;
    if (args.count("address")) assertion = decode_p2pkh(args.at("address"));
    if (args.count("hash160")) assertion = parse_hash160(args.at("hash160"));
    const bool public_address = args.count("address") && !args.count("known-key")
        && !args.count("fixture") && !checkpoint;
    if (public_address && !matches_public_puzzle(range.n, args.at("address")))
        throw std::invalid_argument("O par puzzle/endereço não pertence ao catálogo público.");
    const bool public_plan = dry && public_address;
    Key known{};
    if (!public_plan && (!dry || assertion || checkpoint || args.count("fixture") || args.count("known-key"))) {
        known = known_key(args,saved,range.n,assertion);
        if (from_key(known) < range.lo || from_key(known) > range.hi)
            throw std::invalid_argument("fixture outside puzzle range");
        const auto digest = reference_hash160(known);
        if (assertion && *assertion != digest) throw std::invalid_argument("fixture digest mismatch");
        meta["known-key"] = hex_key(known); meta["hash160"] = digest_hex(digest);
    }
    if (checkpoint && (checkpoint->metadata != meta || checkpoint->reverse != opt.reverse ||
                       checkpoint->total != Count(total)))
        throw std::invalid_argument("checkpoint configuration mismatch");
    if (!dry && args.count("checkpoint")) {
        lock.emplace(opt.checkpoint);
        if (std::filesystem::exists(opt.checkpoint))
            throw std::invalid_argument("checkpoint exists; use --resume");
    }
    if (opt.machine_output) {
        std::cout << "puzzle=" << range.n << "\nlow=" << meta.at("low") << "\nhigh=" << meta.at("high")
                  << "\nactive_bytes=" << range.length << "\nlegal_first="
                  << format_allow({static_cast<uint8_t>(range.first_min)}) << ".."
                  << format_allow({static_cast<uint8_t>(range.first_max)})
                  << "\nfirst_allow=" << meta.at("first-allow") << "\nshared_allow=" << meta.at("allow")
                  << "\nticket_id=" << wheel.id << "\ntotal=" << total << "\noriginal_space=" << space
                  << "\npercent_approx=" << std::setprecision(12)
                  << total.convert_to<long double>() * 100 / space.convert_to<long double>()
                  << "\nrate_assumption=" << rate << "\neta_seconds_ceiling="
                  << total/rate + (total%rate != 0) << '\n';
        if (public_plan) std::cout << "public_address=" << args.at("address") << '\n';
        if (meta.count("hash160")) std::cout << "fixture_hash160=" << meta.at("hash160") << '\n';
        if (!dry) std::cout << "execution_kind=known_fixture\n";
    } else if (dry) {
        print_plan(std::cout, wheel, rate);
        if (public_plan) std::cout << "  Endereço público  : " << args.at("address") << '\n';
    } else if (!opt.quiet) {
        std::cout << "Puzzle " << range.n << "  |  " << total << " candidatos  |  "
                  << opt.threads << " thread" << (opt.threads == 1 ? "" : "s") << '\n';
    }
    std::cout.flush();
    if (dry) return 0;
    return run_fixture(wheel,known,meta,opt,checkpoint ? checkpoint->progress : Progress{});
}
}
}
int main(int argc, char** argv) {
    try { return exlottery::execute(exlottery::arguments(argc,argv)); }
    catch (const std::exception& e) { std::cerr << "exlottery: " << e.what() << '\n'; return 2; }
}

#include "exlottery/presentation.hpp"
#include "exlottery/crypto.hpp"
#include <iomanip>
#include <ostream>
#include <sstream>

namespace exlottery {
namespace {
// The odometer consumes sorted byte sets. Compress long consecutive runs so a
// full 256-byte list still fits in a single terminal line.
std::string byte_list(const Bytes& bytes) {
    std::string result;
    if (bytes.size() <= 32) {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i != 0) result += (i % 16 == 0 ? "\n                      " : " ");
            result += format_allow({bytes[i]});
        }
        return result;
    }
    for (std::size_t i = 0; i < bytes.size();) {
        std::size_t end = i + 1;
        while (end < bytes.size() && bytes[end] == bytes[end - 1] + 1) ++end;
        if (!result.empty()) result += ' ';
        result += format_allow({bytes[i]});
        if (end - i >= 4) {
            result += ".." + format_allow({bytes[end - 1]});
        } else {
            for (std::size_t j = i + 1; j < end; ++j)
                result += ' ' + format_allow({bytes[j]});
        }
        i = end;
    }
    return result;
}

std::string active_bytes(const Key& key, unsigned length) {
    std::string spaced;
    const std::string hex = hex_key(key);
    for (std::size_t i = hex.size() - 2 * length; i < hex.size(); i += 2) {
        if (!spaced.empty()) spaced += ' ';
        spaced.append(hex, i, 2);
    }
    return spaced;
}

std::string grouped(std::string number) {
    for (std::size_t pos = number.size(); pos > 3; pos -= 3)
        number.insert(pos - 3, 1, '.');
    return number;
}

std::string compact_rate(long double rate) {
    const char* suffix = "";
    if (rate >= 1000000) {
        suffix = " M";
        rate /= 1000000;
    } else if (rate >= 1000) {
        suffix = " K";
        rate /= 1000;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(3) << rate << suffix;
    return text.str();
}

void print_lists(std::ostream& out, const MixedRadix& wheel) {
    out << "  Primeiro byte     : " << byte_list(wheel.first) << '\n';
    if (wheel.range.length > 1) {
        out << "  Valores por byte  : " << wheel.shared.size() << " opções em cada uma das "
            << wheel.range.length - 1 << " posições seguintes\n"
            << "                      " << byte_list(wheel.shared) << '\n';
    } else {
        out << "  Lista compartilhada: " << byte_list(wheel.shared) << "  (0 posições)\n";
    }
}
} // namespace

void print_plan(std::ostream& out, const MixedRadix& wheel, std::uint64_t rate) {
    const auto& range = wheel.range;
    const U256 total = wheel.total_candidates();
    const U256 space = range.hi - range.lo + 1;
    const U256 eta = total / rate + (total % rate != 0);
    std::ostringstream percent;
    percent << std::setprecision(10) << total.convert_to<long double>() * 100 / space.convert_to<long double>();
    out << "=== Volante do puzzle " << range.n << " ===\n"
        << "  Início da faixa   : " << hex_key(to_key(range.lo)) << '\n'
        << "  Fim da faixa      : " << hex_key(to_key(range.hi)) << '\n'
        << "  Bytes ativos      : " << range.length << '\n'
        << "  1º byte legal     : " << format_allow({static_cast<std::uint8_t>(range.first_min)})
        << ".." << format_allow({static_cast<std::uint8_t>(range.first_max)}) << '\n';
    print_lists(out, wheel);
    out << "  Primeiro candidato: " << active_bytes(wheel.rank_to_key(0), range.length) << '\n'
        << "  Último candidato  : " << active_bytes(wheel.rank_to_key(total - 1), range.length) << '\n'
        << "  Candidatos        : " << grouped(total.str()) << " de " << grouped(space.str()) << '\n'
        << "  Cobertura aprox.  : " << percent.str() << "%\n"
        << "  Tempo estimado    : " << grouped(eta.str()) << " s a " << grouped(std::to_string(rate))
        << " candidatos/s\n"
        << "  ID SHA-256        : " << wheel.id << '\n';
}

void print_run(std::ostream& out, const MixedRadix& wheel, const Key& known,
               const RunOptions& options, const Progress& previous,
               const Progress& final, long double seconds) {
    if (final.hits > previous.hits) {
        const auto identity = reference_identity(known);
        out << "*** FOUND (chave conhecida confirmada pelo verificador independente) ***\n";
        if (options.show_private) {
            out << "  private : " << hex_key(known) << '\n'
                << "  wif     : " << identity.wif << '\n'
                << "  bytes   : " << active_bytes(known, wheel.range.length) << '\n';
        }
        out << "  pubkey  : " << identity.pubkey << '\n'
            << "  hash160 : " << identity.hash160 << '\n'
            << "  address : " << identity.address << "\n\n";
    } else if (previous.hits != 0) {
        out << "Chave conhecida já confirmada no checkpoint.\n\n";
    }
    const Count session = final.completed - previous.completed;
    const Count total(wheel.total_candidates());
    const bool sse = options.simd == "sse" || (options.simd == "auto" && crypto_sse_available());
    std::ostringstream elapsed;
    elapsed << std::fixed << std::setprecision(3) << seconds;
    out << "=== run ===\n"
        << "  Execução          : validação de fixture conhecida\n"
        << "  Puzzle            : " << wheel.range.n << '\n'
        << "  Tamanho do volante: " << wheel.first.size() << " × " << wheel.shared.size()
        << "^" << (wheel.range.length - 1) << " = " << grouped(total.str()) << '\n'
        << "  Modo              : " << (options.reverse ? "reverso" : "sequencial") << '\n'
        << "  Motor             : " << options.backend << " / " << (sse ? "SSSE3" : "escalar") << '\n'
        << "  Threads           : " << options.threads << '\n'
        << "  Tempo da sessão   : " << elapsed.str() << " s\n"
        << "  Candidatos/s      : " << compact_rate(seconds > 0 ? session.convert_to<long double>() / seconds : 0)
        << '\n'
        << "  Total concluído   : " << grouped(final.completed.str()) << " de " << grouped(total.str()) << '\n';
    if (previous.completed != 0)
        out << "  Nesta sessão      : " << grouped(session.str()) << '\n';
    const char* state = final.completed == total ? "concluído" : "interrompido";
    if (options.stop_on_hit && final.hits != 0) state = "acerto confirmado";
    out << "  Acertos totais    : " << final.hits << '\n'
        << "  Estado            : " << state << '\n';
}
} // namespace exlottery

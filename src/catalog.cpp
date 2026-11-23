#include "exlottery/catalog.hpp"
#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/property_tree/json_parser.hpp>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace exlottery {
namespace {
using boost::property_tree::ptree;
namespace fs = std::filesystem;

fs::path catalog_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && fs::path(xdg).is_absolute())
        return fs::path(xdg) / "exlottery" / "volantes.json";
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        const passwd* user = getpwuid(getuid());
        if (!user) throw std::runtime_error("Diretório pessoal indisponível");
        home = user->pw_dir;
    }
    return fs::path(home) / ".config" / "exlottery" / "volantes.json";
}

// Literal lookup also permits periods in ticket names.
const ptree& field(const ptree& object, const std::string& name) {
    auto it = object.find(name);
    if (it == object.not_found()) throw std::runtime_error("Item ausente no catálogo: " + name);
    if (object.count(name) != 1) throw std::runtime_error("Item duplicado no catálogo: " + name);
    return it->second;
}
}

SavedTicket read_saved_ticket(const std::string& name) {
    const auto path = catalog_path();
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Abra --panel e salve o volante. Catálogo indisponível em " + path.string());
    ptree catalog;
    boost::property_tree::read_json(input, catalog);
    if (field(catalog, "version").data() != "1")
        throw std::runtime_error("Versão incompatível do catálogo de volantes");
    const std::string collection = "volantes";
    const auto& entry = field(field(catalog, collection), name);
    const auto puzzle = parse_uint256(field(entry, "puzzle").data());
    if (puzzle < 1 || puzzle > 160) throw std::invalid_argument("Puzzle inválido no catálogo");
    return {puzzle.convert_to<unsigned>(), parse_allow(field(entry, "first_allow").data()),
            parse_allow(field(entry, "shared_allow").data())};
}

int launch_panel() {
    const auto binary = fs::canonical("/proc/self/exe");
    const auto directory = binary.parent_path();
    auto script = directory / "panel" / "app.py";
    if (!fs::is_regular_file(script) && directory.filename() == "bin")
        script = directory.parent_path() / "panel" / "app.py";
    if (!fs::is_regular_file(script))
        throw std::runtime_error("Painel ausente. Recompile o projeto ou extraia o pacote completo.");
    execlp("python3", "python3", script.c_str(), "--binary", binary.c_str(), static_cast<char*>(nullptr));
    throw std::system_error(errno, std::generic_category(), "Falha ao iniciar Python 3");
}
}

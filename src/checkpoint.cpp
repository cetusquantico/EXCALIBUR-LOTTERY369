#include "exlottery/scheduler.hpp"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/property_tree/json_parser.hpp>
#include <openssl/sha.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <unistd.h>

namespace exlottery {
namespace {

std::system_error io_error(const char* operation) {
    return std::system_error(errno, std::generic_category(), operation);
}

constexpr char hex_digits[] = "0123456789abcdef";

std::string checksum(const std::string& value) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (!SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest))
        throw std::runtime_error("SHA-256 failed");
    std::string result;
    for (auto byte : digest) {
        result += hex_digits[byte >> 4];
        result += hex_digits[byte & 15];
    }
    return result;
}

std::string quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32) {
            out += "\\u00";
            out += hex_digits[c >> 4];
            out += hex_digits[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + '"';
}

std::string encode(const Checkpoint& c) {
    validate_progress(c.total, c.progress);
    std::string out = "{\"schema\":\"exlottery-checkpoint-v1\",\"metadata\":{";
    bool first = true;
    for (const auto& pair : c.metadata) {
        if (!first)
            out += ',';
        out += quote(pair.first) + ':' + quote(pair.second);
        first = false;
    }
    out += "},\"total\":" + quote(c.total.str()) + ",\"mode\":" +
           quote(c.reverse ? "reverse" : "sequential") + ",\"completed\":" +
           quote(c.progress.completed.str()) + ",\"hits\":" + quote(c.progress.hits.str()) +
           ",\"ranges\":[";
    first = true;
    for (const Segment& s : c.progress.ranges) {
        if (!first)
            out += ',';
        out += "[" + quote(s.begin.str()) + "," + quote(s.end.str()) + "]";
        first = false;
    }
    out += "]";
    return out + ",\"sha256\":" + quote(checksum(out + "}")) + "}\n";
}

Count read_count(const std::string& value) {
    if (value.empty() || value.size() > 78 || (value.size() > 1 && value.front() == '0'))
        throw std::invalid_argument("invalid checkpoint decimal");
    Count result = 0;
    for (char c : value) {
        if (c < '0' || c > '9')
            throw std::invalid_argument("invalid checkpoint decimal digit");
        const auto digit = static_cast<unsigned>(c - '0');
        if (result > (std::numeric_limits<Count>::max() - digit) / 10)
            throw std::overflow_error("checkpoint decimal exceeds 256 bits");
        result = result * 10 + digit;
    }
    return result;
}

void write_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        auto n = ::write(fd, data.data() + offset, data.size() - offset);
        if (n == -1 && errno == EINTR)
            continue;
        if (n < 0)
            throw io_error("write checkpoint");
        if (n == 0)
            throw std::runtime_error("zero-length checkpoint write");
        offset += static_cast<std::size_t>(n);
    }
}

}  // namespace

CheckpointLock::CheckpointLock(const std::string& path)
    : fd_(::open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)) {
    if (fd_ < 0)
        throw io_error("open checkpoint lock");
    if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) {
        auto error = io_error("acquire exclusive checkpoint lock");
        ::close(fd_);
        throw error;
    }
}

CheckpointLock::~CheckpointLock() { ::close(fd_); }

void save_checkpoint(const std::string& path, const Checkpoint& c) {
    const std::string data = encode(c);
    auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty())
        parent = ".";
    int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0)
        throw io_error("open checkpoint directory");
    std::string temp = path + ".tmp.XXXXXX";
    int fd = ::mkstemp(temp.data());
    const bool created = fd >= 0;
    bool renamed = false;
    try {
        if (fd < 0)
            throw io_error("create temporary checkpoint");
        write_all(fd, data);
        if (::fsync(fd) < 0)
            throw io_error("sync checkpoint data");
        const int close_result = ::close(fd);
        fd = -1;
        if (close_result < 0)
            throw io_error("close temporary checkpoint");
        if (::rename(temp.c_str(), path.c_str()) < 0)
            throw io_error("replace checkpoint");
        renamed = true;
        if (::fsync(dir_fd) < 0)
            throw io_error("sync checkpoint directory");
    } catch (...) {
        if (fd >= 0)
            ::close(fd);
        if (created && !renamed)
            ::unlink(temp.c_str());
        ::close(dir_fd);
        throw;
    }
    ::close(dir_fd);
}

Checkpoint load_checkpoint(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open checkpoint: " + path);
    const std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (file.bad())
        throw std::runtime_error("checkpoint read failed");
    // Verify the digest before passing any content to the JSON parser.
    const std::string suffix = ",\"sha256\":\"";
    const auto pos = bytes.rfind(suffix);
    if (pos == std::string::npos || bytes.size() - pos != suffix.size() + 67 ||
        bytes.compare(bytes.size() - 3, 3, "\"}\n") != 0 ||
        bytes.compare(pos + suffix.size(), 64, checksum(bytes.substr(0, pos) + "}")) != 0)
        throw std::invalid_argument("invalid checkpoint checksum or framing");

    boost::property_tree::ptree tree;
    std::istringstream input(bytes);
    boost::property_tree::read_json(input, tree);
    Checkpoint c;
    if (tree.get<std::string>("schema") != "exlottery-checkpoint-v1")
        throw std::invalid_argument("unsupported checkpoint schema");
    for (const auto& kv : tree.get_child("metadata"))
        c.metadata.emplace(kv.first, kv.second.data());
    c.total = read_count(tree.get<std::string>("total"));
    const auto mode = tree.get<std::string>("mode");
    c.reverse = mode == "reverse";
    c.progress.completed = read_count(tree.get<std::string>("completed"));
    c.progress.hits = read_count(tree.get<std::string>("hits"));
    for (const auto& range : tree.get_child("ranges")) {
        const auto& pair = range.second;
        if (pair.size() != 2)
            throw std::invalid_argument("invalid checkpoint segment");
        c.progress.ranges.push_back({read_count(pair.front().second.data()),
                                     read_count(pair.back().second.data())});
    }
    // Round-trip rejects duplicate/unknown fields, noncanonical values and
    // non-string leaves that a property tree alone cannot distinguish.
    if (encode(c) != bytes)
        throw std::invalid_argument("checkpoint is not canonical");
    return c;
}

}  // namespace exlottery

#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace hf {
struct Error : std::runtime_error {
    std::string code;
    size_t offset;
    Error(std::string c, std::string message, size_t at = 0)
      : std::runtime_error(std::move(message)), code(std::move(c)), offset(at) {}
};
enum class Kind : uint8_t { Skip, Jump, Left, Right, Restart, RestartFull, Death, TPS, Bugpoint };
struct Action {
    uint64_t frame = 0;
    Kind kind = Kind::Jump;
    bool down = false, p2 = false;
    double tps = 0;
    uint64_t seed = 0;
};
struct Replay {
    int format = 0;
    double tps = 240;
    uint64_t seed = 0;
    uint32_t version = 0, build = 0, randomness = 0;
    uint64_t fingerprint = 0;
    size_t bytes = 0, skippedAtoms = 0;
    std::vector<Action> actions;
};
constexpr size_t MaxBytes = 16 * 1024 * 1024;
constexpr size_t MaxActions = 1000000;
Replay parse(std::vector<uint8_t> const& bytes);
Replay read(std::filesystem::path const& path);
}

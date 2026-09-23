#pragma once
#include <cstdint>
#include <string_view>

namespace hf {
inline uint64_t fingerprint(std::string_view bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : bytes) { hash ^= c; hash *= 1099511628211ULL; }
    return hash;
}
}

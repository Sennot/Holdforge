#include "Slc.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace hf {
namespace {
class Reader {
public:
    std::vector<uint8_t> const& bytes;
    size_t pos = 0, end;
    explicit Reader(std::vector<uint8_t> const& b) : bytes(b), end(b.size()) {}
    [[noreturn]] void fail(std::string code, std::string msg) const { throw Error(code, msg, pos); }
    void require(size_t n) const { if (pos > end || n > end - pos) fail("SLC_TRUNCATED", "Unexpected end of binary data"); }
    uint64_t u(size_t n) {
        require(n);
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= uint64_t(bytes[pos++]) << (8 * i);
        return v;
    }
    double real() { auto bits = u(8); double v; std::memcpy(&v, &bits, 8); return v; }
    void skip(size_t n) { require(n); pos += n; }
    void magic(std::string const& s) {
        require(s.size());
        if (std::memcmp(bytes.data() + pos, s.data(), s.size())) fail("SLC_MAGIC", "Unknown SLC header or invalid footer");
        pos += s.size();
    }
};
void validTps(double tps, Reader const& r) {
    if (!std::isfinite(tps) || tps < 1 || tps > 1000000)
        r.fail("SLC_TPS", "TPS must be finite and between 1 and 1000000");
}
uint64_t advance(uint64_t frame, uint64_t delta, Reader const& r) {
    if (delta > std::numeric_limits<uint64_t>::max() - frame)
        r.fail("SLC_FRAME_OVERFLOW", "Frame counter overflow");
    return frame + delta;
}
void push(Replay& out, Action a, Reader const& r) {
    if (out.actions.size() >= MaxActions) r.fail("SLC_ACTION_LIMIT", "Too many expanded actions");
    out.actions.push_back(a);
}
void v3(Reader& r, Replay& out) {
    out.format = 3;
    r.magic("SLC3RPLY");
    if (r.u(2) != 64) r.fail("SLC_META", "Unsupported v3 metadata size (expected 64)");
    out.tps = r.real(); validTps(out.tps, r);
    out.seed = r.u(8); out.version = static_cast<uint32_t>(r.u(4));
    out.build = static_cast<uint32_t>(r.u(4)); out.randomness = static_cast<uint32_t>(r.u(4));
    r.skip(36);
    if (out.version > 2) r.fail("SLC_VERSION", "Newer v3 metadata version is not supported");
    if (r.pos >= r.end || r.bytes.back() != 0xcc) r.fail("SLC_FOOTER", "Missing v3 footer 0xCC");
    auto fileEnd = r.end - 1;
    r.end = fileEnd;
    bool found = false;
    while (r.pos < fileEnd) {
        auto id = r.u(4), tagged = r.u(8);
        auto flags = tagged >> 56;
        auto length = tagged & 0x00ffffffffffffffULL;
        if (length > fileEnd - r.pos) r.fail("SLC_ATOM_SIZE", "Atom exceeds file bounds");
        if (id != 1) { r.skip(static_cast<size_t>(length)); ++out.skippedAtoms; continue; }
        if (flags) r.fail("SLC_ATOM_FLAGS", "Unsupported action atom flags");
        if (found) r.fail("SLC_MULTI_STREAM", "More than one action atom: ambiguous replay");
        found = true;
        auto atomEnd = length ? r.pos + static_cast<size_t>(length) : fileEnd;
        r.end = atomEnd;
        auto count = r.u(8);
        if (count > MaxActions) r.fail("SLC_ACTION_LIMIT", "Action count exceeds limit");
        out.actions.reserve(static_cast<size_t>(count));
        uint64_t frame = 0;
        auto player = [&](uint64_t state) {
            frame = advance(frame, state >> 4, r);
            auto button = (state >> 2) & 3;
            Action a{frame, static_cast<Kind>(button), bool(state & 1), bool(state & 2)};
            if (button == 0) { // Swift is a press and release on the same frame.
                a.kind = Kind::Jump; a.down = true; push(out, a, r);
                a.down = false; push(out, a, r);
            } else push(out, a, r);
            if (out.actions.size() > count) r.fail("SLC_ACTION_COUNT", "Section expands past declared action count");
        };
        while (out.actions.size() < count) {
            auto header = r.u(2), type = header >> 14;
            if (type <= 1) {
                size_t width = size_t(1) << ((header >> 12) & 3);
                uint64_t cluster = uint64_t(1) << ((header >> 8) & 15);
                uint64_t repeats = type == 1 ? uint64_t(1) << ((header >> 3) & 31) : 1;
                if (cluster > (count - out.actions.size()) / repeats)
                    r.fail("SLC_REPEAT_LIMIT", "Repeat expands past declared action count");
                r.require(static_cast<size_t>(cluster * width));
                std::vector<uint64_t> states;
                states.reserve(static_cast<size_t>(cluster));
                for (uint64_t i = 0; i < cluster; ++i) states.push_back(r.u(width));
                for (uint64_t i = 0; i < repeats; ++i) for (auto s : states) player(s);
            } else if (type == 2) {
                auto special = (header >> 10) & 15;
                frame = advance(frame, r.u(size_t(1) << ((header >> 8) & 3)), r);
                Action a; a.frame = frame;
                if (special <= 2) { a.kind = static_cast<Kind>(special + 4); a.seed = r.u(8); }
                else if (special == 3) { a.kind = Kind::TPS; a.tps = r.real(); validTps(a.tps, r); }
                else if (special == 4) a.kind = Kind::Bugpoint;
                else r.fail("SLC_SPECIAL", "Unknown v3 special action");
                push(out, a, r);
            } else r.fail("SLC_SECTION", "Reserved section identifier");
        }
        // Upstream allows a zero-sized action atom: count determines its boundary.
        if (length) r.pos = atomEnd;
        r.end = fileEnd;
    }
    r.end = r.bytes.size();
    if (r.u(1) != 0xcc) r.fail("SLC_FOOTER", "Invalid v3 footer");
    if (!found) r.fail("SLC_NO_ACTIONS", "Replay has no action atom");
}
void v2(Reader& r, Replay& out) {
    out.format = 2; r.magic("SILL"); out.tps = r.real(); validTps(out.tps, r);
    auto meta = r.u(8);
    // Silicate uses { uint64_t seed; char reserved[56]; }; void and seed-only also exist.
    if (meta != 0 && meta != 8 && meta != 64) r.fail("SLC_META", "Unsupported v2 bot metadata size");
    if (meta) { out.seed = r.u(8); r.skip(static_cast<size_t>(meta - 8)); }
    auto count = r.u(8), blobs = r.u(8);
    if (count > MaxActions || blobs > count) r.fail("SLC_ACTION_LIMIT", "Invalid v2 input/blob count");
    struct Blob { size_t width; uint64_t count; };
    std::vector<Blob> table;
    uint64_t covered = 0;
    for (uint64_t i = 0; i < blobs; ++i) {
        auto width = r.u(8), start = r.u(8), n = r.u(8);
        if ((width != 1 && width != 2 && width != 4 && width != 8) ||
            start != covered || !n || n > count - covered)
            r.fail("SLC_BLOB", "Invalid, overlapping or non-contiguous v2 blob");
        table.push_back({static_cast<size_t>(width), n}); covered += n;
    }
    if (covered != count) r.fail("SLC_BLOB", "v2 blobs do not cover inputs");
    out.actions.reserve(static_cast<size_t>(count));
    uint64_t frame = 0;
    for (auto const& b : table) for (uint64_t i = 0; i < b.count; ++i) {
        auto state = r.u(b.width);
        frame = advance(frame, state >> 5, r);
        Action a{frame, static_cast<Kind>((state >> 2) & 7), bool(state & 1), bool(state & 2)};
        if (a.kind == Kind::TPS) { a.tps = r.real(); validTps(a.tps, r); }
        push(out, a, r);
    }
    r.magic("EOM");
}
}
Replay parse(std::vector<uint8_t> const& bytes) {
    if (bytes.size() > MaxBytes) throw Error("SLC_SIZE", "File exceeds 16 MiB");
    Reader r(bytes); r.require(4);
    Replay out; out.bytes = bytes.size(); out.fingerprint = 14695981039346656037ULL;
    for (auto b : bytes) { out.fingerprint ^= b; out.fingerprint *= 1099511628211ULL; }
    if (!std::memcmp(bytes.data(), "SILL", 4)) v2(r, out);
    else if (!std::memcmp(bytes.data(), "SLC3", 4)) v3(r, out);
    else r.fail("SLC_MAGIC", "Expected SLC3RPLY or SILL. Re-export old/headerless macros in Silicate.");
    if (r.pos != bytes.size()) r.fail("SLC_TRAILING", "Unexpected data after footer");
    return out;
}
Replay read(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw Error("FILE_OPEN", "Cannot open macro file");
    auto size = in.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(MaxBytes)) throw Error("SLC_SIZE", "Invalid file size or more than 16 MiB");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw Error("FILE_READ", "Could not read the complete macro");
    return parse(bytes);
}
}

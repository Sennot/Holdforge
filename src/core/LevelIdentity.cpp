#include "LevelIdentity.hpp"
#include <algorithm>
#include <vector>

namespace hf {
namespace {
using Fields = std::vector<std::pair<std::string, std::string>>;
std::string decimal(std::string_view text) {
    auto original = text;
    bool negative = !text.empty() && text.front() == '-';
    if (!text.empty() && (text.front() == '+' || negative)) text.remove_prefix(1);
    bool point = false, digit = false;
    for (char c : text) {
        if (c == '.' && !point) point = true;
        else if (c >= '0' && c <= '9') digit = true;
        else return std::string(original);
    }
    if (!digit) return std::string(original);
    auto dot = text.find('.');
    auto integer = text.substr(0, dot);
    auto fraction = dot == text.npos ? std::string_view{} : text.substr(dot+1);
    while (!integer.empty() && integer.front() == '0') integer.remove_prefix(1);
    while (!fraction.empty() && fraction.back() == '0') fraction.remove_suffix(1);
    std::string out;
    if (negative && (!integer.empty() || !fraction.empty())) out += '-';
    out += integer.empty() ? "0" : std::string(integer);
    if (!fraction.empty()) { out += '.'; out += fraction; }
    return out;
}
bool fields(std::string_view record, bool header, Fields& out) {
    if (record.empty()) return true;
    size_t at = 0;
    while (at < record.size()) {
        auto comma = record.find(',', at);
        if (comma == record.npos || comma == at) return false;
        auto key = record.substr(at, comma-at);
        auto end = record.find(',', comma+1);
        if (end == record.npos) end = record.size();
        auto value = record.substr(comma+1, end-comma-1);
        out.emplace_back(key, value);
        at = end+1;
        if (at == record.size()) return false; // malformed trailing comma
    }
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    for (size_t i=1; i<out.size(); ++i) if (out[i-1].first == out[i].first) return false;
    std::erase_if(out, [header](auto const& p) {
        return header ? p.first == "kS39" : p.first == "20" || p.first == "61";
    });
    if (!header) for (auto& [key, value] : out)
        if (key == "2" || key == "3" || key == "6") value = decimal(value);
    return true;
}
std::string canonicalRecord(std::string_view record, bool header) {
    Fields props;
    if (!fields(record, header, props)) return "R" + std::string(record);
    std::string out = "N";
    for (auto const& [key, value] : props) { out += key; out += ','; out += value; out += ','; }
    return out;
}
std::string_view next(std::string_view& level) {
    auto end = level.find(';');
    if (end == level.npos) { auto record = level; level = {}; return record; }
    auto record = level.substr(0, end); level.remove_prefix(end+1); return record;
}
void trimTail(std::string_view& level) {
    while (!level.empty() && level.back() == ';') level.remove_suffix(1);
}
}
std::string canonicalLevel(std::string_view level) {
    trimTail(level);
    std::string out = "HFLEVEL1;"; bool header = true;
    while (!level.empty()) { out += canonicalRecord(next(level), header); out += ';'; header = false; }
    return out;
}
uint64_t levelFingerprint(std::string_view level) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : canonicalLevel(level)) { hash ^= c; hash *= 1099511628211ULL; }
    return hash;
}
bool sameLevelData(std::string_view a, std::string_view b) {
    return a == b || canonicalLevel(a) == canonicalLevel(b);
}
EditorSession classifyEditorSession(bool hasMacro, bool sameLevel, bool sameMode,
    std::string_view source, std::string_view generated, std::string_view current) {
    if (!hasMacro) return EditorSession::Unselected;
    if (sameMode && !generated.empty() && sameLevelData(generated, current)) return EditorSession::Generated;
    if (sameMode && sameLevelData(source, current)) return EditorSession::Source;
    return sameLevel ? EditorSession::Changed : EditorSession::Other;
}
LevelDifference firstLevelDifference(std::string_view expected, std::string_view actual) {
    trimTail(expected); trimTail(actual);
    size_t index = 0;
    while (!expected.empty() || !actual.empty()) {
        bool missingExpected = expected.empty(), missingActual = actual.empty();
        auto a = next(expected), b = next(actual);
        if (missingExpected || missingActual)
            return {false, index, "record_count", missingExpected ? "missing" : "present", missingActual ? "missing" : "present"};
        if (canonicalRecord(a, index == 0) != canonicalRecord(b, index == 0)) {
            Fields left, right;
            if (!fields(a, index == 0, left) || !fields(b, index == 0, right))
                return {false, index, "raw_record", "unparsed", "unparsed"};
            size_t i=0, j=0;
            while (i<left.size() || j<right.size()) {
                if (i == left.size() || (j<right.size() && right[j].first < left[i].first))
                    return {false, index, right[j].first, "<missing>", right[j].second.substr(0,80)};
                if (j == right.size() || left[i].first < right[j].first)
                    return {false, index, left[i].first, left[i].second.substr(0,80), "<missing>"};
                if (left[i].second != right[j].second)
                    return {false, index, left[i].first, left[i].second.substr(0,80), right[j].second.substr(0,80)};
                ++i; ++j;
            }
        }
        ++index;
    }
    return {};
}
}

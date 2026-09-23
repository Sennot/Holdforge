#pragma once
#include <Geode/Geode.hpp>
#include <deque>
#include <fstream>

namespace hf {
class Diagnostics {
    matjson::Value m_summary = matjson::Value::object();
    std::deque<matjson::Value> m_events;
    std::ofstream m_journal;
    size_t m_dropped = 0, m_written = 0;
    std::string m_session;
public:
    static Diagnostics& get();
    void begin();
    void refreshContext();
    void set(std::string const& key, matjson::Value value);
    void event(std::string const& type, matjson::Value data = matjson::Value::object());
    std::filesystem::path exportReport();
    static std::filesystem::path folder();
    static std::string stamp();
};
}

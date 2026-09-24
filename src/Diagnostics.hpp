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
    bool m_lifecycleStarted = false;
    matjson::Value m_lastStage = matjson::Value::object();
public:
    static Diagnostics& get();
    void begin();
    void set(std::string const& key, matjson::Value value);
    void event(std::string const& type, matjson::Value data = matjson::Value::object());
    // One small crash checkpoint, independent of the capped event journal.
    void checkpoint(std::string const& stage, matjson::Value data = matjson::Value::object()) noexcept;
    std::filesystem::path exportReport();
    static std::filesystem::path folder();
    static std::string stamp();
};
}

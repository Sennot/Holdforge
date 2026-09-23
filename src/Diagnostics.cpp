#include "Diagnostics.hpp"
#include <chrono>
using namespace geode::prelude;
namespace hf {
Diagnostics& Diagnostics::get() { static Diagnostics value; return value; }
std::string Diagnostics::stamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::filesystem::path Diagnostics::folder() { return Mod::get()->getSaveDir() / "diagnostics"; }
void Diagnostics::begin() {
    m_journal.close(); m_events.clear(); m_dropped = 0; m_written = 0;
    m_summary = matjson::Value::object(); m_session = stamp();
    m_summary["schema"] = 2; m_summary["session"] = m_session;
    m_summary["mod"] = Mod::get()->getVersion().toVString();
    m_summary["sdk"] = "5.10.1"; m_summary["target_gd"] = "2.2081";
    m_summary["loader"] = Loader::get()->getVersion().toVString();
    m_summary["platform"] = "win64";
    auto settings = matjson::Value::object();
    for (auto key : {"strict-240", "unsafe-timeline", "allow-existing-controls", "backup-level", "shared-dual-fix",
                    "debug-enabled", "debug-inputs", "debug-mapping", "debug-runtime", "debug-objects", "debug-mods"})
        settings[key] = Mod::get()->getSettingValue<bool>(key);
    settings["offset-ms"] = Mod::get()->getSettingValue<double>("offset-ms");
    settings["x-offset"] = Mod::get()->getSettingValue<double>("x-offset");
    settings["trigger-y"] = Mod::get()->getSettingValue<double>("trigger-y");
    for (auto key : {"editor-layer", "max-triggers", "debug-max-events"})
        settings[key] = Mod::get()->getSettingValue<int64_t>(key);
    m_summary["settings"] = std::move(settings);
    if (Mod::get()->getSettingValue<bool>("debug-mods")) {
        auto mods = matjson::Value::array();
        for (auto mod : Loader::get()->getAllMods()) {
            auto row = matjson::Value::object(); row["id"] = std::string(mod->getID());
            row["version"] = mod->getVersion().toVString(); row["loaded"] = mod->isLoaded();
            mods.push(std::move(row));
        }
        m_summary["mods"] = std::move(mods);
    }
    std::error_code ec; std::filesystem::create_directories(folder(), ec);
    if (Mod::get()->getSettingValue<bool>("debug-enabled") && !ec) {
        // Two bounded journals; flush after each event so a crash leaves usable evidence.
        auto old = folder() / "previous.jsonl", current = folder() / "latest.jsonl";
        std::filesystem::remove(old, ec); ec.clear();
        if (std::filesystem::exists(current, ec)) { ec.clear(); std::filesystem::rename(current, old, ec); }
        m_journal.open(current, std::ios::trunc);
        if (m_journal) m_journal << m_summary.dump(matjson::NO_INDENTATION) << '\n' << std::flush;
    }
    m_summary["journal_open"] = m_journal.is_open();
}
void Diagnostics::set(std::string const& key, matjson::Value value) {
    m_summary[key] = value;
    auto item = matjson::Value::object(); item["key"] = key; item["value"] = std::move(value);
    event("summary", std::move(item));
}
void Diagnostics::event(std::string const& type, matjson::Value data) {
    if (!Mod::get()->getSettingValue<bool>("debug-enabled")) return;
    size_t cap = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("debug-max-events"));
    cap = std::clamp<size_t>(cap, 100, 100000);
    auto event = matjson::Value::object(); event["type"] = type;
    event["timestamp_us"] = stamp(); event["data"] = std::move(data);
    if (m_events.size() >= cap) { m_events.pop_front(); ++m_dropped; }
    m_events.push_back(event);
    if (m_journal && m_written < cap) { m_journal << event.dump(matjson::NO_INDENTATION) << '\n' << std::flush; ++m_written; }
}
std::filesystem::path Diagnostics::exportReport() {
    if (m_session.empty()) begin();
    auto report = m_summary;
    report["events"] = matjson::Value::array();
    for (auto const& e : m_events) report["events"].push(e);
    report["dropped_ring_events"] = m_dropped;
    report["journal_events_written"] = m_written;
    auto path = folder() / ("holdforge-" + m_session + "-" + stamp() + ".json");
    std::error_code ec; std::filesystem::create_directories(folder(), ec);
    std::ofstream out(path); out << report.dump(2); out.flush();
    if (!out) throw std::runtime_error("Could not write diagnostic report");
    return path;
}
}

#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
#include "TraceRecorder.hpp"
#include <Geode/ui/GeodeUI.hpp>
using namespace geode::prelude;
namespace hf {
namespace {
CCLabelBMFont* label(CCNode* parent, std::string const& text, float x, float y,
                     float scale, ccColor3B color = {220, 230, 245}, float width = 440) {
    auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    l->setPosition({x, y}); l->setColor(color); l->limitLabelWidth(width, scale, 0.15f);
    parent->addChild(l); return l;
}
CCMenuItemSpriteExtra* button(CCMenu* menu, CCObject* target, SEL_MenuHandler cb,
                             char const* text, float x, float y, float width, ccColor3B color) {
    auto bg = NineSlice::create("GJ_square02.png"); bg->setContentSize({width, 30}); bg->setColor(color);
    label(bg, text, width / 2, 15, .34f, {255, 255, 255}, width - 12);
    auto item = CCMenuItemSpriteExtra::create(bg, target, cb); item->setPosition({x, y});
    menu->addChild(item); return item;
}
}

HoldPopup* HoldPopup::create(LevelEditorLayer* editor) {
    auto ret = new HoldPopup;
    if (ret->init(editor)) { ret->autorelease(); return ret; }
    delete ret; return nullptr;
}

bool HoldPopup::init(LevelEditorLayer* editor) {
    if (!Popup::init(500, 310, "GJ_square02.png")) return false;
    m_editor = editor; m_bgSprite->setColor({21, 29, 48});
    setTitle("HOLDFORGE", "bigFont.fnt", .65f, 21.f); m_title->setColor({106, 231, 221});
    label(m_mainLayer, "SILICATE  >  NATIVE OPTIONS TRIGGERS", 250, 264, .29f, {148, 164, 194});
    auto card = NineSlice::create("GJ_square02.png"); card->setContentSize({464, 142});
    card->setColor({34, 45, 67}); card->setPosition({250, 181}); m_mainLayer->addChild(card);
    m_file = label(m_mainLayer, "Choose a .slc macro", 250, 233, .43f);
    m_stats = label(m_mainLayer, "Classic / dual / two-player", 250, 207, .31f, {148, 164, 194});
    label(m_mainLayer, "P1", 42, 174, .28f, {94, 230, 207}, 25);
    label(m_mainLayer, "P2", 42, 149, .28f, {175, 151, 255}, 25);
    m_timeline = CCDrawNode::create(); m_mainLayer->addChild(m_timeline);
    m_status = label(m_mainLayer, "Import  >  Record trace  >  Create", 250, 119, .29f);

    button(m_buttonMenu, this, menu_selector(HoldPopup::onImport), "Import .slc", 67, 78, 106, {49, 79, 103});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onAnalyze), "Analyze", 189, 78, 106, {49, 79, 103});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onRecord), "Record trace", 311, 78, 106, {63, 74, 130});
    m_create = button(m_buttonMenu, this, menu_selector(HoldPopup::onCreate), "Create", 433, 78, 106, {29, 135, 123});
    m_create->setEnabled(false); m_create->setOpacity(110);
    button(m_buttonMenu, this, menu_selector(HoldPopup::onSettings), "Settings", 110, 35, 120, {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onExport), "Export logs", 250, 35, 120, {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onHelp), "Help", 390, 35, 120, {40, 50, 74});
    drawTimeline(); return true;
}

void HoldPopup::status(std::string const& text, bool error) {
    m_status->setString(text.c_str());
    m_status->setColor(error ? ccColor3B{255, 144, 138} : ccColor3B{106, 231, 221});
    m_status->limitLabelWidth(445, .30f, .17f);
}

void HoldPopup::failure(std::exception const& e) {
    auto data = matjson::Value::object(); data["message"] = e.what();
    std::string code = "ERROR";
    if (auto known = dynamic_cast<Error const*>(&e)) { code = known->code; data["byte_offset"] = known->offset; }
    data["code"] = code; Diagnostics::get().set("last_error", data);
    log::error("HoldForge {}: {}", code, e.what());
    status(code + " - export logs", true);
    m_prepared.reset(); m_create->setEnabled(false); m_create->setOpacity(110);
    FLAlertLayer::create("HoldForge", fmt::format("<cr>{}</c>\n{}", code, e.what()), "OK")->show();
}

void HoldPopup::onImport(CCObject*) {
    m_picker.spawn(file::pick(file::PickMode::OpenFile, {{}, {{"Silicate macro", {"*.slc"}}}}),
        [this](file::PickResult result) {
            if (result.isErr()) { failure(std::runtime_error(result.unwrapErr())); return; }
            auto path = result.unwrap(); if (!path) return;
            try {
                Diagnostics::get().refreshContext();
                m_prepared.reset(); m_replay.reset(); m_calibration.reset(); m_macroPath = *path; m_applied = false;
                m_create->setEnabled(false); m_create->setOpacity(110); drawTimeline();
                m_file->setString(utils::string::pathToString(path->filename()).c_str());
                m_file->limitLabelWidth(440, .43f, .15f); m_stats->setString("Reading macro...");
                Diagnostics::get().set("selected_file", utils::string::pathToString(path->filename()));
                m_replay = read(*path);

                auto& replay = *m_replay;
                auto meta = matjson::Value::object();
                meta["filename"] = utils::string::pathToString(path->filename()); meta["format"] = replay.format;
                meta["bytes"] = replay.bytes; meta["fingerprint_fnv1a64"] = fmt::format("{:016x}", replay.fingerprint);
                meta["tps"] = replay.tps; meta["seed"] = replay.seed; meta["version"] = replay.version;
                meta["build"] = replay.build; meta["randomness"] = replay.randomness;
                meta["actions"] = replay.actions.size(); meta["skipped_atoms"] = replay.skippedAtoms;
                Diagnostics::get().set("macro", meta);

                auto macroActions = matjson::Value::array();
                size_t cap = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-triggers"));
                size_t written = 0;
                for (auto const& a : replay.actions) {
                    if (written++ >= cap) break;
                    auto row = matjson::Value::object(); row["frame"] = a.frame; row["kind"] = static_cast<int>(a.kind);
                    row["down"] = a.down; row["p2"] = a.p2; row["tps"] = a.tps;
                    if (Mod::get()->getSettingValue<bool>("debug-inputs")) Diagnostics::get().event("macro_input", row);
                    macroActions.push(std::move(row));
                }
                Diagnostics::get().set("macro_actions", std::move(macroActions));
                Diagnostics::get().set("macro_actions_truncated", replay.actions.size() > cap);

                auto tracePath = *path; tracePath.replace_extension(".hftrace");
                bool traceRejected = false;
                if (std::filesystem::exists(tracePath)) {
                    try { m_calibration = readCalibration(tracePath, replay); }
                    catch (Error const& e) {
                        traceRejected = true;
                        auto row = matjson::Value::object(); row["code"] = e.code; row["message"] = e.what();
                        Diagnostics::get().set("trace_sidecar_rejected", row);
                    }
                }
                Diagnostics::get().set("recorded_positions", m_calibration.has_value());
                m_stats->setString(fmt::format("SLC{} / {} TPS / {} actions", replay.format, replay.tps, replay.actions.size()).c_str());
                m_stats->limitLabelWidth(440, .31f, .17f);
                if (m_calibration) onAnalyze(nullptr);
                else status(traceRejected ? "Old/foreign trace rejected - Record trace" : "Trace required - Record trace");
            } catch (std::exception const& e) { failure(e); }
        });
}

void HoldPopup::onAnalyze(CCObject*) {
    if (!m_replay) { status("Import a macro first", true); return; }
    if (m_applied) { status("Batch already created. Use Undo to retry.", true); return; }
    if (!m_calibration && !Mod::get()->getSettingValue<bool>("allow-static-fallback")) {
        status("No valid trace - use Record trace", true); return;
    }
    try {
        m_prepared = prepare(m_editor, *m_replay, m_calibration ? &*m_calibration : nullptr);
        auto const& p = m_prepared->plan;
        auto stats = fmt::format("SLC{} / {} TPS / {} triggers / {:.2f}s", m_replay->format, m_replay->tps, p.gates.size(), p.duration);
        m_stats->setString(stats.c_str()); m_stats->limitLabelWidth(440, .31f, .17f);
        status(m_calibration ? "Automatic trace verified - Create" : "EXPERIMENTAL static mapping - stock-GD test required", !m_calibration);
        m_create->setEnabled(true); m_create->setOpacity(255); drawTimeline();
    } catch (std::exception const& e) { failure(e); }
}

void HoldPopup::onRecord(CCObject*) {
    if (!m_replay || !m_macroPath) { status("Import a macro first", true); return; }
    if (m_applied) { status("Undo generated controls before recording", true); return; }
    try {
        auto tracePath = *m_macroPath; tracePath.replace_extension(".hftrace");
        TraceRecorder::get().arm(m_editor, *m_replay, tracePath);
        Diagnostics::get().set("trace_target", utils::string::pathToString(tracePath.filename()));
        onClose(nullptr);
        FLAlertLayer::create("HoldForge trace",
            "Recorder armed. Start the <cy>Silicate replay from level start</c> in editor and let the attempt complete. "
            "Stopping early, dying, wrong input order, or changing the level rejects the trace. Reopen HF and import the same .slc after completion.",
            "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}

void HoldPopup::onCreate(CCObject*) {
    if (!m_replay || !m_prepared || m_applied) return;
    try {
        m_prepared = prepare(m_editor, *m_replay, m_calibration ? &*m_calibration : nullptr);
        auto count = apply(m_editor, *m_prepared); m_applied = true;
        m_create->setEnabled(false); m_create->setOpacity(110);
        status(fmt::format("Created {} native triggers - one Undo restores batch", count));
        FLAlertLayer::create("HoldForge", fmt::format(
            "<cg>{} native Options Triggers created.</c>\nFinal acceptance: save a copy and verify by ordinary holding with <cy>HoldForge disabled</c>. "
            "Ordinary dual mirrors one shared stream to P1/P2; 2 Player Mode uses independent P1/P2.", count), "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}

void HoldPopup::onSettings(CCObject*) { openSettingsPopup(Mod::get()); }
void HoldPopup::onExport(CCObject*) {
    try {
        auto path = Diagnostics::get().exportReport(); status("Single diagnostic JSON exported");
        if (!file::openFolder(path.parent_path())) FLAlertLayer::create("Report saved", utils::string::pathToString(path), "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onHelp(CCObject*) {
    std::string message = "1. Import .slc. 2. Record trace and replay the full source macro. 3. Re-import and Create.\n"
        "Press = allow (-1); release = block (1). Ordinary dual mirrors one shared stream to P1/P2; true 2 Player Mode is independent.\n"
        "Generated gameplay uses only stock Options behavior: HoldForge does not push/release player input at runtime.\n"
        "Publication check: test the saved copy by holding with HoldForge disabled. Export logs after any mismatch.";
    FLAlertLayer::create("HoldForge help", message, "OK")->show();
}

void HoldPopup::drawTimeline() {
    m_timeline->clear();
    for (int p = 0; p < 2; ++p) {
        float y = p ? 149.f : 174.f;
        m_timeline->drawSegment({63, y}, {437, y}, 2, {0.22f, .28f, .38f, 1});
        if (!m_prepared || m_prepared->plan.duration <= 0) continue;
        auto const& plan = m_prepared->plan; int state = 1; double start = 0;
        auto color = p ? ccColor4F{.69f, .59f, 1.f, 1} : ccColor4F{.37f, .9f, .81f, 1};
        for (auto const& g : plan.gates) {
            int next = p ? g.p2 : g.p1; if (!next) continue;
            if (state == -1) m_timeline->drawSegment({float(63 + 374 * start / plan.duration), y},
                {float(63 + 374 * g.seconds / plan.duration), y}, 3, color);
            state = next; start = g.seconds;
        }
        if (state == -1) m_timeline->drawSegment({float(63 + 374 * start / plan.duration), y}, {437, y}, 3, color);
    }
}
}

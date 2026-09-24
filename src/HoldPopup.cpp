#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
#include "Workflow.hpp"
#include <Geode/ui/GeodeUI.hpp>
using namespace geode::prelude;
namespace hf {
namespace {
CCLabelBMFont* label(CCNode* parent, std::string const& text, float x, float y,
                     float scale, ccColor3B color = {220, 230, 245}, float width = 390) {
    auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    l->setPosition({x, y}); l->setColor(color); l->limitLabelWidth(width, scale, 0.15f);
    parent->addChild(l); return l;
}
CCMenuItemSpriteExtra* button(CCMenu* menu, CCObject* target, SEL_MenuHandler cb,
                             char const* text, float x, float y, float width, ccColor3B color) {
    auto bg = NineSlice::create("GJ_square02.png"); bg->setContentSize({width, 30});
    bg->setColor(color);
    label(bg, text, width / 2, 15, .36f, {255, 255, 255}, width - 12);
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
    if (!Popup::init(440, 330, "GJ_square02.png")) return false;
    m_editor = editor;
    m_bgSprite->setColor({21, 29, 48});
    setTitle("HOLDFORGE", "bigFont.fnt", .65f, 21.f);
    m_title->setColor({106, 231, 221});
    label(m_mainLayer, "RECORD  >  OPTIONS + AUTO  >  VERIFY", 220, 284, .27f, {148, 164, 194});
    auto card = NineSlice::create("GJ_square02.png"); card->setContentSize({404, 138});
    card->setColor({34, 45, 67}); card->setPosition({220, 201}); m_mainLayer->addChild(card);
    m_file = label(m_mainLayer, "Choose a .slc macro", 220, 251, .43f);
    m_stats = label(m_mainLayer, "Classic / dual / two-player", 220, 226, .31f, {148, 164, 194});
    label(m_mainLayer, "P1", 42, 194, .28f, {94, 230, 207}, 25);
    label(m_mainLayer, "P2", 42, 170, .28f, {175, 151, 255}, 25);
    m_timeline = CCDrawNode::create(); m_mainLayer->addChild(m_timeline);
    m_status = label(m_mainLayer, "Import - Record - Analyze - Create - Verify", 220, 145, .26f);
    button(m_buttonMenu, this, menu_selector(HoldPopup::onImport), "Import .slc", 85, 110, 124, {49, 79, 103});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onRecord), "Record", 220, 110, 124, {49, 79, 103});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onAnalyze), "Analyze", 355, 110, 124, {49, 79, 103});
    m_create = button(m_buttonMenu, this, menu_selector(HoldPopup::onCreate), "Create", 85, 70, 124, {29, 135, 123});
    m_create->setEnabled(false); m_create->setOpacity(110);
    button(m_buttonMenu, this, menu_selector(HoldPopup::onVerify), "Verify", 220, 70, 124, {29, 135, 123});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onExport), "Export logs", 355, 70, 124, {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onSettings), "Settings", 85, 31, 124, {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(HoldPopup::onHelp), "Help", 355, 31, 124, {40, 50, 74});
    auto& workflow = Workflow::get();
    auto session = workflow.editorState(editor);
    if (auto replay = workflow.replay(); replay &&
        (session == Workflow::EditorState::Source || session == Workflow::EditorState::Generated)) {
        m_applied = session == Workflow::EditorState::Generated;
        m_replay = *replay; m_file->setString(workflow.name().c_str());
        m_file->limitLabelWidth(380, .43f, .15f);
        m_stats->setString(workflow.trajectory() ? "Recorded trajectory available" : "Recording required");
        status(workflow.status());
    } else if (session == Workflow::EditorState::Changed) {
        m_stats->setString("Previous session belongs to a different level snapshot");
        m_stats->limitLabelWidth(380, .31f, .17f);
        status("Import a macro for this level");
    }
    drawTimeline(); return true;
}
void HoldPopup::status(std::string const& text, bool error) {
    m_status->setString(text.c_str()); m_status->setColor(error ? ccColor3B{255, 144, 138} : ccColor3B{106, 231, 221});
    m_status->limitLabelWidth(385, .30f, .17f);
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
                Diagnostics::get().begin();
                m_prepared.reset(); m_replay.reset(); m_applied = false;
                m_create->setEnabled(false); m_create->setOpacity(110);
                drawTimeline();
                m_file->setString(utils::string::pathToString(path->filename()).c_str());
                m_file->limitLabelWidth(380, .43f, .15f);
                m_stats->setString("Reading macro...");
                Diagnostics::get().set("selected_file", utils::string::pathToString(path->filename()));
                m_replay = read(*path);
                Workflow::get().select(m_editor, *m_replay, utils::string::pathToString(path->filename()));
                auto& replay = *m_replay;
                auto meta = matjson::Value::object();
                meta["filename"] = utils::string::pathToString(path->filename());
                meta["format"] = replay.format; meta["bytes"] = replay.bytes;
                meta["fingerprint_fnv1a64"] = fmt::format("{:016x}", replay.fingerprint);
                meta["tps"] = replay.tps; meta["seed"] = replay.seed;
                meta["version"] = replay.version; meta["build"] = replay.build;
                meta["randomness"] = replay.randomness; meta["actions"] = replay.actions.size();
                meta["skipped_atoms"] = replay.skippedAtoms; Diagnostics::get().set("macro", meta);
                if (Mod::get()->getSettingValue<bool>("debug-inputs")) for (auto const& a : replay.actions) {
                    auto row = matjson::Value::object(); row["frame"] = a.frame; row["kind"] = static_cast<int>(a.kind);
                    row["down"] = a.down; row["p2"] = a.p2; row["tps"] = a.tps;
                    Diagnostics::get().event("input", row);
                }
                m_file->setString(utils::string::pathToString(path->filename()).c_str());
                m_file->limitLabelWidth(380, .43f, .15f);
                if (Workflow::get().trajectory()) onAnalyze(nullptr);
                else {
                    m_stats->setString(fmt::format("{} inputs / {} TPS", replay.actions.size(), replay.tps).c_str());
                    status("Macro loaded - press Record");
                }
            } catch (std::exception const& e) { failure(e); }
        });
}
void HoldPopup::onAnalyze(CCObject*) {
    if (!m_replay) { status("Import a macro first", true); return; }
    if (m_applied) { status("Batch already created. Close and use Undo to retry.", true); return; }
    try {
        m_prepared = prepare(m_editor, *m_replay, Workflow::get().trajectory());
        auto const& p = m_prepared->plan;
        auto stats = fmt::format("SLC{}  /  {} TPS  /  {} objects  /  {:.2f}s", m_replay->format, m_replay->tps, m_prepared->placements.size() + m_prepared->autoPath.objects.size(), p.duration);
        m_stats->setString(stats.c_str()); m_stats->limitLabelWidth(380, .31f, .17f);
        status(m_prepared->manualDual ? "Ready - Create, then build your dual auto objects" : Workflow::get().trajectory() ? "Recorded positions ready - Create, then Verify" :
            fmt::format("Ready - {} notes. Create, then test holding.", p.warnings.size()));
        m_create->setEnabled(true); m_create->setOpacity(255); drawTimeline();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onCreate(CCObject*) {
    if (!m_replay || !m_prepared || m_applied) return;
    try {
        // Re-evaluate the current settings and level immediately before mutation.
        m_prepared = prepare(m_editor, *m_replay, Workflow::get().trajectory());
        auto count = apply(m_editor, *m_prepared); m_applied = true;
        Workflow::get().generated(m_editor, *m_prepared);
        m_create->setEnabled(false); m_create->setOpacity(110);
        status(fmt::format("Created {} objects - one Undo restores the batch", count));
        std::string notes;
        for (auto const& w : m_prepared->plan.warnings) notes += "\n" + w;
        auto next = m_prepared->manualDual
            ? "Build your invisible blocks/pads in dual sections. Test using normal Play with macro OFF, holding input."
            : "Press Verify, turn macro playback OFF, Save and Exit, then normal Play holding input.";
        FLAlertLayer::create("HoldForge", fmt::format("<cg>{} native objects created.</c>\n{} Test with HoldForge disabled before publishing.{}", count, next, notes), "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onRecord(CCObject*) {
    if (!m_replay) { status("Import a macro for this level first", true); return; }
    try {
        Workflow::get().armRecord(m_editor); status(Workflow::get().status());
        FLAlertLayer::create("Record trajectory", "Close HF. Use <cy>Save and Exit</c>, then the normal <cy>Play</c> button on this original level. Keep GD open. Play the SAME macro in Silicate from the beginning, without practice/StartPos or noclip.\nComplete the level, return to HF, then Analyze. Recording is saved automatically.", "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onVerify(CCObject*) {
    if (!m_replay) { status("Import a macro for this level first", true); return; }
    try {
        Workflow::get().armVerify(m_editor); status(Workflow::get().status());
        FLAlertLayer::create("Verify native hold", "Turn macro playback and noclip OFF. Close HF, Save and Exit, then normal Play from the beginning, hold P1 continuously (both inputs in 2-player).\nHoldForge only observes. Return to HF for the result and Export logs.", "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onSettings(CCObject*) { openSettingsPopup(Mod::get()); }
void HoldPopup::onExport(CCObject*) {
    try {
        auto path = Diagnostics::get().exportReport();
        status("Report exported - send the holdforge JSON file");
        if (!file::openFolder(path.parent_path()))
            FLAlertLayer::create("Report saved", utils::string::pathToString(path), "OK")->show();
    } catch (std::exception const& e) { failure(e); }
}
void HoldPopup::onHelp(CCObject*) {
    std::string message = "Use a <cy>copy of your level</c> and a full-start, clean macro.\n"
        "Press = allow control (-1); release = block (1).\n"
        "Dual shares P1. Two Player Mode uses independent P1/P2 streams.\n"
        "Record saves every physics step; Create writes native level objects.\n"
        "Verify compares native hold to the recording. No runtime fixes.\n"
        "Invisible dual auto uses hidden portals along the recorded P2 path. Beta: test without HoldForge.\n"
        "Manual dual sections leaves dual construction to you; test your edits with normal Play.\n"
        "Settings: timing offset, editor layer, debug traces.\n"
        "For bugs: enable Debug + Runtime trace, reproduce, then Export logs.";
    if (m_prepared) for (auto const& w : m_prepared->plan.warnings) message += "\n" + w;
    FLAlertLayer::create("HoldForge help", message, "OK")->show();
}
void HoldPopup::drawTimeline() {
    m_timeline->clear();
    for (int p = 0; p < 2; ++p) {
        float y = p ? 170.f : 194.f;
        m_timeline->drawSegment({63, y}, {399, y}, 2, {0.22f, .28f, .38f, 1});
        if (!m_prepared || m_prepared->plan.duration <= 0) continue;
        auto const& plan = m_prepared->plan;
        int state = 1; double start = 0;
        auto color = p ? ccColor4F{.69f, .59f, 1.f, 1} : ccColor4F{.37f, .9f, .81f, 1};
        for (auto const& g : plan.gates) {
            int next = p ? g.p2 : g.p1; if (!next) continue;
            if (state == -1) m_timeline->drawSegment({float(63 + 336 * start / plan.duration), y},
                {float(63 + 336 * g.seconds / plan.duration), y}, 3, color);
            state = next; start = g.seconds;
        }
        if (state == -1) m_timeline->drawSegment({float(63 + 336 * start / plan.duration), y}, {399, y}, 3, color);
    }
}
}

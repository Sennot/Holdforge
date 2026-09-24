#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
#include "Workflow.hpp"
using namespace geode::prelude;

namespace {
// A private container, not another CCMenu mixed into a game's menu hierarchy.
// It is attached after the complete editor initialization hook chain returns.
class HFLauncher final : public CCNode {
    WeakRef<LevelEditorLayer> m_editor;
    WeakRef<hf::HoldPopup> m_popup;
    CCMenu* m_menu = nullptr;
    CCMenuItemSpriteExtra* m_button = nullptr;

    bool ready(LevelEditorLayer* editor) const {
        return editor && LevelEditorLayer::get() == editor && editor->m_editorUI &&
            !editor->m_editorUI->m_isPaused && editor->m_playbackMode == PlaybackMode::Not &&
            !editor->m_playbackActive;
    }
    bool init(LevelEditorLayer* editor) {
        if (!CCNode::init()) return false;
        m_editor = editor;
        setID("holdforge.slc_to_hold/launcher");
        m_menu = CCMenu::create(); m_menu->setPosition({0, 0});
        m_menu->setID("holdforge.slc_to_hold/menu");
        auto spr = ButtonSprite::create("HF", "bigFont.fnt", "GJ_button_01.png", .7f);
        if (!spr) return false;
        spr->setScale(.65f); spr->setColor({106, 231, 221});
        m_button = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HFLauncher::onOpen));
        m_button->setID("holdforge.slc_to_hold/open");
        m_menu->addChild(m_button); addChild(m_menu);
        m_menu->setVisible(false);
        scheduleUpdate();
        return true;
    }
    void onOpen(CCObject*) {
        auto editor = m_editor.lock();
        if (!ready(editor.data())) return;
        if (auto popup = m_popup.lock(); popup && popup->getParent()) return;
        hf::Diagnostics::get().event("launcher_open");
        if (auto popup = hf::HoldPopup::create(editor.data())) {
            m_popup = popup;
            popup->show();
        }
    }
public:
    static HFLauncher* create(LevelEditorLayer* editor) {
        auto ret = new HFLauncher;
        if (ret->init(editor)) { ret->autorelease(); return ret; }
        delete ret; return nullptr;
    }
    void update(float) override {
        auto editor = m_editor.lock();
        bool enabled = ready(editor.data());
        m_menu->setVisible(enabled); m_button->setEnabled(enabled);
        if (auto parent = getParent()) {
            auto win = CCDirector::sharedDirector()->getWinSize();
            setPosition(parent->convertToNodeSpace({30.f, win.height * .55f}));
        }
    }
};

bool trace() {
    return Mod::get()->getSettingValue<bool>("debug-enabled") && Mod::get()->getSettingValue<bool>("debug-runtime");
}
matjson::Value state(GJBaseGameLayer* layer) {
    auto row = hf::sampleJson(hf::sample(layer));
    row["progress_tick"] = layer->m_gameState.m_currentProgress;
    row["channel"] = layer->m_gameState.m_currentChannel;
    row["time_warp"] = layer->m_gameState.m_timeWarp;
    row["two_player"] = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    row["native_only"] = true;
    if (layer->m_player1) { row["p1_disabled"] = layer->m_player1->m_controlsDisabled; row["p1_buffered"] = layer->m_player1->m_jumpBuffered; row["p1_dead"] = layer->m_player1->m_isDead; }
    if (layer->m_player2) { row["p2_disabled"] = layer->m_player2->m_controlsDisabled; row["p2_buffered"] = layer->m_player2->m_jumpBuffered; row["p2_dead"] = layer->m_player2->m_isDead; }
    if (layer->m_uiLayer) {
        row["ui_p1"] = layer->m_uiLayer->m_p1Jumping || layer->m_uiLayer->m_p1TouchId != -1;
        row["ui_p2"] = layer->m_uiLayer->m_p2Jumping || layer->m_uiLayer->m_p2TouchId != -1;
    }
    return row;
}
struct Depth { int& value; explicit Depth(int& n) : value(n) { ++value; } ~Depth() { --value; } };
}
// All gameplay hooks below observe GD. No controls, player input, physics or
// trigger properties are modified by HoldForge during play.
class $modify(HFTrace, GJBaseGameLayer) {
    struct Fields { int queueDepth = 0; int optionsDepth = 0; };
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::processQueuedButtons", Priority::First))
            log::warn("HoldForge could not establish the outer input-queue observer");
    }
    void processQueuedButtons(float dt, bool clearInputQueue) {
        bool outer = m_fields->queueDepth == 0;
        Depth depth(m_fields->queueDepth);
        if (outer) hf::Workflow::get().stepBegin(this);
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        if (outer) hf::Workflow::get().stepEnd(this);
    }
    void processOptionsTrigger(GameOptionsTrigger* object) {
        bool logging = (trace() || hf::Workflow::get().active(this)) && object;
        auto before = logging ? state(this) : matjson::Value();
        { Depth depth(m_fields->optionsDepth); GJBaseGameLayer::processOptionsTrigger(object); }
        hf::Workflow::get().options(this, object);
        if (!logging) return;
        auto row = state(this); row["before"] = std::move(before);
        row["trigger_x"] = object->getPositionX();
        row["disable_p1"] = static_cast<int>(object->m_disableP1Controls);
        row["disable_p2"] = static_cast<int>(object->m_disableP2Controls);
        hf::Diagnostics::get().event("options_activated", row);
    }
    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        bool logging = trace() || hf::Workflow::get().active(this);
        auto before = logging ? state(this) : matjson::Value();
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
        if (logging) {
            auto row = state(this); row["before"] = std::move(before); row["requested_dual"] = dual;
            if (object) { row["portal_id"] = object->m_objectID; row["portal_x"] = object->getPositionX(); }
            hf::Diagnostics::get().event("dual_transition", row);
        }
    }
    void handleButton(bool down, int button, bool player1) {
        if (!m_fields->optionsDepth) hf::Workflow::get().button(this, down, button, player1, m_fields->queueDepth > 0);
        GJBaseGameLayer::handleButton(down, button, player1);
        if (!trace() && !hf::Workflow::get().active(this)) return;
        auto row = state(this); row["down"] = down; row["button"] = button; row["player1"] = player1;
        row["queued"] = m_fields->queueDepth > 0; row["from_options"] = m_fields->optionsDepth > 0;
        hf::Diagnostics::get().event("button", row);
    }
};
class $modify(HFPlayTrace, PlayLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PlayLayer::resetLevel", Priority::First))
            log::warn("HoldForge could not establish the outer reset observer");
    }
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        hf::Workflow::get().attempt(this, false); return true;
    }
    void resetLevel() {
        hf::Workflow::get().resetBegin(this);
        PlayLayer::resetLevel();
        hf::Workflow::get().attempt(this, false);
        if (trace()) hf::Diagnostics::get().event("attempt_reset", state(this));
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool anti = object == m_anticheatSpike;
        bool logging = !anti && (trace() || hf::Workflow::get().active(this));
        if (!anti) hf::Workflow::get().damaged(this, player, object);
        if (logging) {
            auto row = state(this); row["player2"] = player == m_player2;
            if (object) { row["object_id"] = object->m_objectID; row["object_x"] = object->getPositionX(); row["object_y"] = object->getPositionY(); }
            hf::Diagnostics::get().event("death_attempt", row);
        }
        PlayLayer::destroyPlayer(player, object);
        if (logging) { auto row = state(this); row["dead_after"] = player && player->m_isDead; hf::Diagnostics::get().event("death_result", row); }
    }
    void levelComplete() {
        hf::Workflow::get().complete(this);
        PlayLayer::levelComplete();
    }
    void onQuit() { hf::Workflow::get().leave(this); PlayLayer::onQuit(); }
    void onExit() { hf::Workflow::get().leave(this); PlayLayer::onExit(); }
};
class $modify(HFEditorTrace, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        hf::Diagnostics::get().event("editor_init_begin");
        if (!LevelEditorLayer::init(level, noUI)) return false;
        hf::Diagnostics::get().event("editor_init_complete");
        if (!noUI) {
            WeakRef<LevelEditorLayer> weak = this;
            queueInMainThread([weak] {
                auto editor = weak.lock();
                if (!editor || LevelEditorLayer::get() != editor.data()) return;
                auto ui = editor->m_editorUI;
                if (!ui || ui->getChildByID("holdforge.slc_to_hold/launcher")) return;
                if (auto launcher = HFLauncher::create(editor.data())) { ui->addChild(launcher, 100); hf::Diagnostics::get().event("launcher_attached"); }
            });
        }
        return true;
    }
    void onPlaytest() {
        LevelEditorLayer::onPlaytest();
        if (trace()) hf::Diagnostics::get().event("playtest_start", state(this));
    }
    void playerTookDamage(PlayerObject* player) {
        if (trace()) { auto row = state(this); row["player2"] = player == m_player2; hf::Diagnostics::get().event("editor_damage", row); }
        LevelEditorLayer::playerTookDamage(player);
    }
};
$on_mod(Loaded) { hf::Diagnostics::get().begin(); }

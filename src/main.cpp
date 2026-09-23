#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
#include "TraceRecorder.hpp"
using namespace geode::prelude;

namespace {
// Separate launcher node: do not inject controls into EditorPauseLayer.
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
        m_menu->setVisible(false); scheduleUpdate();
        return true;
    }
    void onOpen(CCObject*) {
        auto editor = m_editor.lock();
        if (!ready(editor.data())) return;
        if (auto popup = m_popup.lock(); popup && popup->getParent()) return;
        hf::Diagnostics::get().event("launcher_open");
        if (auto popup = hf::HoldPopup::create(editor.data())) { m_popup = popup; popup->show(); }
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
    return Mod::get()->getSettingValue<bool>("debug-enabled") &&
        Mod::get()->getSettingValue<bool>("debug-runtime");
}

matjson::Value state(GJBaseGameLayer* layer) {
    auto row = matjson::Value::object();
    row["progress_tick"] = layer->m_gameState.m_currentProgress;
    row["channel"] = layer->m_gameState.m_currentChannel;
    row["level_time"] = layer->m_gameState.m_levelTime;
    row["dual"] = layer->m_gameState.m_isDualMode;
    row["two_player"] = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    row["editor"] = layer == LevelEditorLayer::get();
    row["time_warp"] = layer->m_gameState.m_timeWarp;
    if (layer->m_uiLayer) {
        row["physical_p1"] = layer->m_uiLayer->m_p1Jumping || layer->m_uiLayer->m_p1TouchId != -1;
        row["physical_p2"] = layer->m_uiLayer->m_p2Jumping || layer->m_uiLayer->m_p2TouchId != -1;
    }
    auto playerState = [&](PlayerObject* p, std::string const& key) {
        if (!p) return;
        row[key + "_x"] = p->getPositionX(); row[key + "_y"] = p->getPositionY();
        row[key + "_disabled"] = p->m_controlsDisabled;
        row[key + "_buffered"] = p->m_jumpBuffered;
        auto held = p->m_holdingButtons.find(1);
        row[key + "_holding"] = held != p->m_holdingButtons.end() && held->second;
        row[key + "_dead"] = p->m_isDead; row[key + "_speed"] = p->m_playerSpeed;
        row[key + "_mode"] = p->m_isShip ? "ship" : p->m_isBird ? "ufo" : p->m_isBall ? "ball" :
            p->m_isDart ? "wave" : p->m_isRobot ? "robot" : p->m_isSpider ? "spider" :
            p->m_isSwing ? "swing" : "cube";
    };
    playerState(layer->m_player1, "p1"); playerState(layer->m_player2, "p2");
    return row;
}
}

class $modify(HFTrace, GJBaseGameLayer) {
    void processOptionsTrigger(GameOptionsTrigger* object) {
        bool logging = trace() && object;
        auto before = logging ? state(this) : matjson::Value();
        // Deliberately native-only. HoldForge must not repair generated levels at runtime.
        GJBaseGameLayer::processOptionsTrigger(object);
        if (!logging) return;
        auto row = state(this); row["before"] = std::move(before);
        row["trigger_x"] = object->getPositionX(); row["trigger_y"] = object->getPositionY();
        row["disable_p1"] = static_cast<int>(object->m_disableP1Controls);
        row["disable_p2"] = static_cast<int>(object->m_disableP2Controls);
        hf::Diagnostics::get().event("options_activated", row);
    }

    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        bool logging = trace();
        auto before = logging ? state(this) : matjson::Value();
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
        if (!logging) return;
        auto row = state(this); row["before"] = std::move(before); row["requested_dual"] = dual;
        if (object) { row["portal_id"] = object->m_objectID; row["portal_x"] = object->getPositionX(); }
        hf::Diagnostics::get().event("dual_transition", row);
    }

    void handleButton(bool down, int button, bool player1) {
        hf::TraceRecorder::get().onButton(this, down, button, player1);
        bool logging = trace();
        auto before = logging ? state(this) : matjson::Value();
        GJBaseGameLayer::handleButton(down, button, player1);
        if (!logging) return;
        auto row = state(this); row["before"] = std::move(before);
        row["down"] = down; row["button"] = button; row["player1"] = player1;
        hf::Diagnostics::get().event("button", row);
    }
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
                if (auto launcher = HFLauncher::create(editor.data())) {
                    ui->addChild(launcher, 100);
                    hf::Diagnostics::get().event("launcher_attached");
                }
            });
        }
        return true;
    }

    void onPlaytest() {
        hf::TraceRecorder::get().onPlaytestStart(this);
        LevelEditorLayer::onPlaytest();
        if (trace()) hf::Diagnostics::get().event("playtest_start", state(this));
    }

    void onStopPlaytest() {
        hf::TraceRecorder::get().onPlaytestStop(this);
        if (trace()) hf::Diagnostics::get().event("playtest_stop", state(this));
        LevelEditorLayer::onStopPlaytest();
    }

    void playerTookDamage(PlayerObject* player) {
        hf::TraceRecorder::get().onDamage(this, player);
        if (trace()) {
            auto row = state(this); row["player2"] = player == m_player2;
            hf::Diagnostics::get().event("editor_damage", row);
        }
        LevelEditorLayer::playerTookDamage(player);
    }
};

$on_mod(Loaded) { hf::Diagnostics::get().begin(); }

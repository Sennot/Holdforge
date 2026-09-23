#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
#include "core/HeldInput.hpp"
using namespace geode::prelude;

namespace {
GJBaseGameLayer* inputOwner = nullptr;
hf::HeldInput capturedInput;
int optionsDepth = 0;
hf::HeldInput& inputs(GJBaseGameLayer* layer) {
    if (inputOwner != layer) { inputOwner = layer; capturedInput = {}; }
    return capturedInput;
}
void resetInputs(GJBaseGameLayer* layer) { inputOwner = layer; capturedInput = {}; }
struct NativeOptionsScope {
    GameOptionsTrigger* object;
    GameOptionsSetting p1, p2;
    NativeOptionsScope(GameOptionsTrigger* value) : object(value), p1(value->m_disableP1Controls), p2(value->m_disableP2Controls) {
        ++optionsDepth;
        object->m_disableP1Controls = object->m_disableP2Controls = GameOptionsSetting::Disabled;
    }
    ~NativeOptionsScope() {
        object->m_disableP1Controls = p1; object->m_disableP2Controls = p2;
        --optionsDepth;
    }
};
void applyHoldGate(PlayerObject* player, bool blocked, bool held) {
    if (!player) return;
    auto it = player->m_holdingButtons.find(1);
    bool holding = it != player->m_holdingButtons.end() && it->second;
    bool desired = !blocked && held;
    // Control the input edge explicitly. Letting the native Options handler
    // infer it from UILayer loses queued/CBF input and single-input P2 holds.
    player->m_controlsDisabled = false;
    if (desired && !holding) player->pushButton(static_cast<PlayerButton>(1));
    else if (!desired && (holding || player->m_jumpBuffered)) player->releaseButton(static_cast<PlayerButton>(1));
    player->m_controlsDisabled = blocked;
}
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
bool sharedFix(GJBaseGameLayer* layer) {
    return Mod::get()->getSettingValue<bool>("shared-dual-fix") && layer->m_levelSettings &&
        !layer->m_levelSettings->m_twoPlayerMode && !layer->m_levelSettings->m_platformerMode;
}
bool holdGate(GameOptionsTrigger* object) {
    if (!object) return false;
    int gate = static_cast<int>(object->m_disableP1Controls);
    return (gate == -1 || gate == 1) && object->m_disableP1Controls == object->m_disableP2Controls &&
        object->m_editorLayer == Mod::get()->getSettingValue<int64_t>("editor-layer");
}
bool primaryHeld(GJBaseGameLayer* layer) {
    bool ui = layer->m_uiLayer && (layer->m_uiLayer->m_p1Jumping || layer->m_uiLayer->m_p1TouchId != -1);
    return inputs(layer).held(ui);
}
matjson::Value state(GJBaseGameLayer* layer) {
    auto row = matjson::Value::object(); row["progress_tick"] = layer->m_gameState.m_currentProgress;
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
    row["captured_input_seen"] = inputs(layer).seen;
    row["effective_hold_p1"] = primaryHeld(layer);
    auto playerState = [&](PlayerObject* p, std::string const& key) {
        if (!p) return;
        row[key + "_x"] = p->getPositionX(); row[key + "_y"] = p->getPositionY();
        row[key + "_disabled"] = p->m_controlsDisabled;
        row[key + "_buffered"] = p->m_jumpBuffered;
        auto held = p->m_holdingButtons.find(1);
        row[key + "_holding"] = held != p->m_holdingButtons.end() && held->second;
        row[key + "_dead"] = p->m_isDead;
        row[key + "_speed"] = p->m_playerSpeed;
        row[key + "_mode"] = p->m_isShip ? "ship" : p->m_isBird ? "ufo" : p->m_isBall ? "ball" :
            p->m_isDart ? "wave" : p->m_isRobot ? "robot" : p->m_isSpider ? "spider" : p->m_isSwing ? "swing" : "cube";
    };
    playerState(layer->m_player1, "p1"); playerState(layer->m_player2, "p2");
    return row;
}
}
class $modify(HFTrace, GJBaseGameLayer) {
    struct Fields { bool sharedGateSeen = false; double lastGateTime = 0; };
    void processOptionsTrigger(GameOptionsTrigger* object) {
        if (m_gameState.m_levelTime < m_fields->lastGateTime) m_fields->sharedGateSeen = false;
        bool marked = sharedFix(this) && holdGate(object);
        if (marked) {
            m_fields->sharedGateSeen = true;
            m_fields->lastGateTime = m_gameState.m_levelTime;
        }
        bool bridge = marked;
        bool logging = trace() && object;
        auto before = logging ? state(this) : matjson::Value();
        if (bridge) {
            bool blocked = object->m_disableP1Controls == GameOptionsSetting::On;
            {
                NativeOptionsScope scope(object);
                GJBaseGameLayer::processOptionsTrigger(object);
            }
            bool held = primaryHeld(this);
            applyHoldGate(m_player1, blocked, held);
            if (m_gameState.m_isDualMode) applyHoldGate(m_player2, blocked, held);
            else if (m_player2) m_player2->m_controlsDisabled = blocked;
        } else GJBaseGameLayer::processOptionsTrigger(object);
        if (!logging) return;
        auto row = state(this); row["trigger_x"] = object->getPositionX();
        row["before"] = std::move(before); row["shared_input_bridge"] = bridge;
        row["disable_p1"] = static_cast<int>(object->m_disableP1Controls);
        row["disable_p2"] = static_cast<int>(object->m_disableP2Controls);
        hf::Diagnostics::get().event("options_activated", row);
    }
    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        if (m_gameState.m_levelTime < m_fields->lastGateTime) m_fields->sharedGateSeen = false;
        bool wasDual = m_gameState.m_isDualMode;
        bool logging = trace();
        auto before = logging ? state(this) : matjson::Value();
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
        bool restored = false;
        // A newly spawned P2 may reset its gate. Restore the active shared hold
        // without replaying the whole Options trigger (which would re-press P1).
        if (!wasDual && m_gameState.m_isDualMode && sharedFix(this) &&
            m_fields->sharedGateSeen && m_player1 && m_player2) {
            applyHoldGate(m_player2, m_player1->m_controlsDisabled, primaryHeld(this));
            restored = true;
        }
        if (logging) {
            auto row = state(this); row["before"] = std::move(before);
            row["requested_dual"] = dual; row["shared_gate_restored"] = restored;
            if (object) { row["portal_id"] = object->m_objectID; row["portal_x"] = object->getPositionX(); }
            hf::Diagnostics::get().event("dual_transition", row);
        }
    }
    void handleButton(bool down, int button, bool player1) {
        if (!optionsDepth && button == 1 && (player1 || (m_levelSettings && !m_levelSettings->m_twoPlayerMode)))
            inputs(this).event(down);
        GJBaseGameLayer::handleButton(down, button, player1);
        if (!optionsDepth && button == 1 && m_fields->sharedGateSeen && sharedFix(this) && m_player1) {
            bool blocked = m_player1->m_controlsDisabled;
            applyHoldGate(m_player1, blocked, primaryHeld(this));
            if (m_gameState.m_isDualMode) applyHoldGate(m_player2, blocked, primaryHeld(this));
        }
        if (!trace()) return;
        auto row = state(this); row["down"] = down; row["button"] = button; row["player1"] = player1;
        hf::Diagnostics::get().event("button", row);
    }
};
class $modify(HFPlayTrace, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        resetInputs(this);
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        if (trace()) hf::Diagnostics::get().event("attempt_reset", state(this));
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool logging = trace();
        auto row = logging ? state(this) : matjson::Value();
        if (logging) {
            row["player2"] = player == m_player2;
            if (object) { row["object_id"] = object->m_objectID; row["object_x"] = object->getPositionX(); row["object_y"] = object->getPositionY(); }
            // Write before the hook chain as well: noclip can suppress the death.
            hf::Diagnostics::get().event("death_attempt", row);
        }
        PlayLayer::destroyPlayer(player, object);
        if (logging) {
            row["after"] = state(this);
            row["dead_after"] = player && player->m_isDead;
            hf::Diagnostics::get().event("death_result", row);
        }
    }
};
class $modify(HFEditorTrace, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        resetInputs(this);
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
        LevelEditorLayer::onPlaytest();
        if (trace()) hf::Diagnostics::get().event("playtest_start", state(this));
    }
    void playerTookDamage(PlayerObject* player) {
        if (trace()) { auto row = state(this); row["player2"] = player == m_player2; hf::Diagnostics::get().event("editor_damage", row); }
        LevelEditorLayer::playerTookDamage(player);
    }
};
$on_mod(Loaded) { hf::Diagnostics::get().begin(); }

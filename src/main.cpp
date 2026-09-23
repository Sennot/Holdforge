#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
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
    auto row = matjson::Value::object(); row["progress_tick"] = layer->m_gameState.m_currentProgress;
    row["channel"] = layer->m_gameState.m_currentChannel;
    if (layer->m_player1) { row["p1_x"] = layer->m_player1->getPositionX(); row["p1_y"] = layer->m_player1->getPositionY(); row["p1_disabled"] = layer->m_player1->m_controlsDisabled; }
    if (layer->m_player2) { row["p2_x"] = layer->m_player2->getPositionX(); row["p2_y"] = layer->m_player2->getPositionY(); row["p2_disabled"] = layer->m_player2->m_controlsDisabled; }
    return row;
}
}
class $modify(HFTrace, GJBaseGameLayer) {
    void processOptionsTrigger(GameOptionsTrigger* object) {
        GJBaseGameLayer::processOptionsTrigger(object);
        if (!trace() || !object) return;
        auto row = state(this); row["trigger_x"] = object->getPositionX();
        row["disable_p1"] = static_cast<int>(object->m_disableP1Controls);
        row["disable_p2"] = static_cast<int>(object->m_disableP2Controls);
        hf::Diagnostics::get().event("options_activated", row);
    }
    void handleButton(bool down, int button, bool player1) {
        GJBaseGameLayer::handleButton(down, button, player1);
        if (!trace()) return;
        auto row = state(this); row["down"] = down; row["button"] = button; row["player1"] = player1;
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
        LevelEditorLayer::onPlaytest();
        if (trace()) hf::Diagnostics::get().event("playtest_start", state(this));
    }
    void playerTookDamage(PlayerObject* player) {
        if (trace()) { auto row = state(this); row["player2"] = player == m_player2; hf::Diagnostics::get().event("editor_damage", row); }
        LevelEditorLayer::playerTookDamage(player);
    }
};
$on_mod(Loaded) { hf::Diagnostics::get().begin(); }

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include "HoldPopup.hpp"
#include "Diagnostics.hpp"
using namespace geode::prelude;

class $modify(HFEditorPause, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create(); menu->setID("holdforge-menu");
        auto spr = ButtonSprite::create("HoldForge", "bigFont.fnt", "GJ_button_01.png", .7f);
        spr->setScale(.65f);
        auto button = CCMenuItemSpriteExtra::create(spr, this, menu_selector(HFEditorPause::onHoldForge));
        button->setID("holdforge-open"); menu->addChild(button);
        menu->setPosition({win.width - 64, win.height - 30}); addChild(menu, 100);
    }
    void onHoldForge(CCObject*) {
        if (auto editor = LevelEditorLayer::get()) {
            if (auto popup = hf::HoldPopup::create(editor)) popup->show();
        }
    }
};

namespace {
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

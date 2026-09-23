#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include "Editor.hpp"

namespace hf {
class HoldPopup : public geode::Popup {
    LevelEditorLayer* m_editor = nullptr;
    geode::async::TaskHolder<geode::utils::file::PickResult> m_picker;
    std::optional<Replay> m_replay;
    std::optional<Calibration> m_calibration;
    std::optional<Prepared> m_prepared;
    std::optional<std::filesystem::path> m_macroPath;
    cocos2d::CCLabelBMFont* m_file = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_stats = nullptr;
    cocos2d::CCDrawNode* m_timeline = nullptr;
    CCMenuItemSpriteExtra* m_create = nullptr;
    bool m_applied = false;
    bool init(LevelEditorLayer* editor);
    void status(std::string const& text, bool error = false);
    void failure(std::exception const& error);
    void onImport(cocos2d::CCObject*);
    void onAnalyze(cocos2d::CCObject*);
    void onRecord(cocos2d::CCObject*);
    void onCreate(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onExport(cocos2d::CCObject*);
    void onHelp(cocos2d::CCObject*);
    void drawTimeline();
public:
    static HoldPopup* create(LevelEditorLayer* editor);
};
}

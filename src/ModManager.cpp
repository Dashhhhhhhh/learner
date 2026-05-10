#include "ModManager.hpp"
#include <Geode/Geode.hpp>

#include <algorithm>

using namespace geode::prelude;

ModManager* ModManager::sharedState() {
    static ModManager instance;
    return &instance;
}

ModManager::ModManager() {
    m_dontFadeOnStart = Mod::get()->getSettingValue<bool>("hide");
    m_hideBtns = Mod::get()->getSettingValue<bool>("hideBtns");
    m_ignoreDisabled = Mod::get()->getSettingValue<bool>("ignoreDisabled");
    m_guidedMode = Mod::get()->getSavedValue<bool>("guided-mode", false);
    m_showStartposSwitcher = Mod::get()->getSavedValue<bool>("show-startpos-switcher", true);
    m_showGuidedPercent = Mod::get()->getSavedValue<bool>("show-guided-percent", true);
    m_guidedLateThreshold = std::clamp(Mod::get()->getSavedValue<int>("guided-late-threshold", 50), 0, 100);
    m_guidedAttemptLimit = std::clamp(Mod::get()->getSavedValue<int>("guided-attempt-limit", 20), 1, 999);
    m_opacity = Mod::get()->getSettingValue<double>("opacity") / 100 * 255;
}

$on_mod(Loaded) {
    auto mm = ModManager::sharedState();
    
    listenForSettingChanges<bool>("hide", [mm](bool val) {
        mm->m_dontFadeOnStart = val;
    });
    
    listenForSettingChanges<bool>("hideBtns", [mm](bool val) {
        mm->m_hideBtns = val;
    });
    
    listenForSettingChanges<bool>("ignoreDisabled", [mm](bool val) {
        mm->m_ignoreDisabled = val;
    });
    
    listenForSettingChanges<double>("opacity", [mm](double val) {
        mm->m_opacity = val / 100 * 255;
    });
}

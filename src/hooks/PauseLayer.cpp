#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include "PlayLayer.hpp"
#include "UILayer.hpp"
#include "../ModManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace {
    class LearnerStatsPopup : public Popup {
    protected:
        geode::Ref<ScrollLayer> m_statsScroll = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_guidedBtn = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_viewBtn = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_clearBtn = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_resetGuidedBtn = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_switcherToggleBtn = nullptr;
        geode::Ref<CCMenuItemSpriteExtra> m_percentToggleBtn = nullptr;
        geode::Ref<CCNode> m_settingsControls = nullptr;
        geode::Ref<TextInput> m_lateThresholdInput = nullptr;
        geode::Ref<TextInput> m_attemptLimitInput = nullptr;
        bool m_showingSettings = false;

    protected:
        bool init() {
            if (!Popup::init(330.f, 205.f)) {
                return false;
            }

            this->setTitle("Learner Stats");
            buildStatsList();

            auto guidedSpr = createGuidedButtonSprite();
            m_guidedBtn = CCMenuItemSpriteExtra::create(guidedSpr, this, menu_selector(LearnerStatsPopup::onToggleGuided));
            m_guidedBtn->setID("guided-mode-button"_spr);
            m_buttonMenu->addChildAtPosition(m_guidedBtn, Anchor::Bottom, ccp(-105.f, 20.f));

            auto viewSpr = createViewButtonSprite();
            m_viewBtn = CCMenuItemSpriteExtra::create(viewSpr, this, menu_selector(LearnerStatsPopup::onToggleView));
            m_viewBtn->setID("guided-settings-button"_spr);
            m_buttonMenu->addChildAtPosition(m_viewBtn, Anchor::Bottom, ccp(0.f, 20.f));

            auto clearSpr = ButtonSprite::create("Clear Stats", 86, 0, 0.38f, true, "goldFont.fnt", "GJ_button_06.png", 24.f);
            m_clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(LearnerStatsPopup::onClearStats));
            m_clearBtn->setID("clear-stats-button"_spr);
            m_buttonMenu->addChildAtPosition(m_clearBtn, Anchor::Bottom, ccp(105.f, 20.f));

            auto resetSpr = ButtonSprite::create("Reset Guide", 86, 0, 0.36f, true, "goldFont.fnt", "GJ_button_06.png", 24.f);
            m_resetGuidedBtn = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(LearnerStatsPopup::onResetGuided));
            m_resetGuidedBtn->setID("reset-guided-button"_spr);
            m_resetGuidedBtn->setVisible(false);
            m_buttonMenu->addChildAtPosition(m_resetGuidedBtn, Anchor::Bottom, ccp(105.f, 20.f));

            return true;
        }

        ButtonSprite* createGuidedButtonSprite() {
            auto enabled = ModManager::sharedState()->m_guidedMode;
            return ButtonSprite::create(
                enabled ? "Guided: ON" : "Guided: OFF",
                100,
                0,
                0.38f,
                true,
                "goldFont.fnt",
                enabled ? "GJ_button_01.png" : "GJ_button_04.png",
                24.f
            );
        }

        ButtonSprite* createViewButtonSprite() {
            return ButtonSprite::create(
                m_showingSettings ? "Stats" : "Settings",
                84,
                0,
                0.38f,
                true,
                "goldFont.fnt",
                "GJ_button_02.png",
                24.f
            );
        }

        ButtonSprite* createToggleSprite(bool enabled) {
            return ButtonSprite::create(
                enabled ? "ON" : "OFF",
                54,
                0,
                0.42f,
                true,
                "goldFont.fnt",
                enabled ? "GJ_button_01.png" : "GJ_button_04.png",
                24.f
            );
        }

        int parseNumber(std::string const& value, int fallback, int minValue, int maxValue) {
            auto digits = std::string();
            for (auto ch : value) {
                if (std::isdigit(static_cast<unsigned char>(ch))) {
                    digits.push_back(ch);
                }
            }
            if (digits.empty()) {
                return fallback;
            }
            return std::clamp(numFromString<int>(digits).unwrapOr(fallback), minValue, maxValue);
        }

        int parsePercent(std::string const& value) {
            return parseNumber(value, 50, 0, 100);
        }

        void removeSettingsControls() {
            if (m_settingsControls) {
                m_settingsControls->removeFromParentAndCleanup(true);
                m_settingsControls = nullptr;
                m_lateThresholdInput = nullptr;
                m_attemptLimitInput = nullptr;
                m_switcherToggleBtn = nullptr;
                m_percentToggleBtn = nullptr;
            }
        }

        void buildRows(std::vector<std::pair<std::string, float>> const& rows) {
            removeSettingsControls();
            if (m_statsScroll) {
                m_statsScroll->removeFromParentAndCleanup(true);
                m_statsScroll = nullptr;
            }

            auto displayRows = rows;
            if (displayRows.empty()) {
                displayRows.emplace_back("No runs yet", 0.42f);
            }

            auto scroll = ScrollLayer::create({290.f, 118.f});
            scroll->setID("stats-scroll"_spr);
            scroll->ignoreAnchorPointForPosition(false);
            scroll->setAnchorPoint({0.5f, 0.5f});

            auto contentHeight = std::max(118.f, displayRows.size() * 16.f + 10.f);
            scroll->m_contentLayer->setContentSize({290.f, contentHeight});

            auto y = contentHeight - 18.f;
            for (auto const& [text, scale] : displayRows) {
                auto row = CCLabelBMFont::create(text.c_str(), scale >= 0.5f ? "goldFont.fnt" : "bigFont.fnt");
                row->setAnchorPoint({0.5f, 0.5f});
                row->setScale(scale);
                row->limitLabelWidth(270.f, scale, 0.1f);
                row->setPosition({145.f, y});
                scroll->m_contentLayer->addChild(row);
                y -= 16.f;
            }

            m_statsScroll = scroll;
            m_mainLayer->addChildAtPosition(scroll, Anchor::Center, ccp(0.f, -17.f));
            scroll->scrollToTop();
        }

        void buildStatsList() {
            auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get());

            auto rows = std::vector<std::pair<std::string, float>>();
            if (playLayer) {
                auto& bestRunEndPercents = playLayer->m_fields->m_bestRunEndPercents;
                rows.emplace_back(fmt::format(
                    "Phase: {}/{}",
                    playLayer->getGuidedPhaseIndex(),
                    playLayer->getGuidedPhaseCount()
                ), 0.42f);
                rows.emplace_back(fmt::format(
                    "Phase {} Runs: {}/{}",
                    playLayer->getGuidedPhaseIndex(),
                    playLayer->getGuidedPhaseStageIndex(),
                    playLayer->getGuidedPhaseStageCount()
                ), 0.42f);
                rows.emplace_back(fmt::format(
                    "Overall Stage: {}/{}",
                    playLayer->getGuidedStageIndex(),
                    playLayer->getGuidedStageCount()
                ), 0.34f);
                rows.emplace_back(fmt::format(
                    "Route: {}%-{}%",
                    playLayer->getLearnerStartPercent(playLayer->m_fields->m_guidedWindowStart),
                    playLayer->getGuidedRunTargetPercent()
                ), 0.36f);
                rows.emplace_back("", 0.18f);

                rows.emplace_back("Best Runs", 0.5f);
                for (auto idx = 0u; idx < bestRunEndPercents.size(); idx++) {
                    rows.emplace_back(fmt::format(
                        "{}%-{}%",
                        playLayer->getLearnerStartPercent(idx),
                        bestRunEndPercents[idx]
                    ), 0.42f);
                }

                rows.emplace_back("", 0.18f);
                rows.emplace_back("Ranking", 0.5f);

                auto ranking = std::vector<size_t>();
                for (auto idx = 0u; idx < playLayer->m_fields->m_sectionAttempts.size(); idx++) {
                    ranking.push_back(idx);
                }

                std::sort(ranking.begin(), ranking.end(), [playLayer](auto a, auto b) {
                    auto attemptsA = playLayer->getLearnerAdjustedAttempts(a);
                    auto attemptsB = playLayer->getLearnerAdjustedAttempts(b);
                    auto clearsA = playLayer->m_fields->m_sectionClears[a];
                    auto clearsB = playLayer->m_fields->m_sectionClears[b];
                    auto totalA = clearsA + attemptsA;
                    auto totalB = clearsB + attemptsB;
                    auto rateA = totalA > 0 ? static_cast<float>(clearsA) / totalA : 2.f;
                    auto rateB = totalB > 0 ? static_cast<float>(clearsB) / totalB : 2.f;

                    if (rateA != rateB) {
                        return rateA < rateB;
                    }
                    return playLayer->getLearnerStartPercent(a) < playLayer->getLearnerStartPercent(b);
                });

                for (auto rank = 0u; rank < ranking.size(); rank++) {
                    auto idx = ranking[rank];
                    auto attempts = playLayer->getLearnerAdjustedAttempts(idx);
                    auto clears = playLayer->m_fields->m_sectionClears[idx];
                    auto total = clears + attempts;
                    auto rate = total > 0 ?
                        fmt::format("{}%", static_cast<int>(std::round(static_cast<float>(clears) / total * 100.f))) :
                        std::string("--%");
                    rows.emplace_back(fmt::format(
                        "{}. {}%-{}%  {}/{}  {}",
                        rank + 1,
                        playLayer->getLearnerStartPercent(idx),
                        playLayer->getLearnerClearTargetPercent(idx),
                        clears,
                        total,
                        rate
                    ), 0.36f);
                }
            }

            buildRows(rows);
        }

        void buildSettingsControls() {
            removeSettingsControls();

            auto mm = ModManager::sharedState();
            auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get());
            auto controls = CCNode::create();
            controls->setID("guided-settings-controls"_spr);

            auto thresholdLabel = CCLabelBMFont::create("Late Cutoff %", "bigFont.fnt");
            thresholdLabel->setAnchorPoint({1.f, 0.5f});
            thresholdLabel->setScale(0.26f);
            thresholdLabel->limitLabelWidth(112.f, 0.26f, 0.1f);
            thresholdLabel->setPosition({144.f, 126.f});
            controls->addChild(thresholdLabel);

            m_lateThresholdInput = TextInput::create(52.f, "50");
            m_lateThresholdInput->setCommonFilter(CommonFilter::Uint);
            m_lateThresholdInput->setMaxCharCount(3);
            m_lateThresholdInput->setString(fmt::format("{}", mm->m_guidedLateThreshold));
            m_lateThresholdInput->setPosition({181.f, 126.f});
            m_lateThresholdInput->setScale(0.5f);
            m_lateThresholdInput->setCallback([this](std::string const& value) {
                auto mm = ModManager::sharedState();
                mm->m_guidedLateThreshold = parsePercent(value);
                Mod::get()->setSavedValue("guided-late-threshold", mm->m_guidedLateThreshold);
                if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                    playLayer->normalizeGuidedRoute();
                    playLayer->saveGuidedProgress();
                }
            });
            controls->addChild(m_lateThresholdInput);

            auto attemptLabel = CCLabelBMFont::create("Attempt Limit", "bigFont.fnt");
            attemptLabel->setAnchorPoint({1.f, 0.5f});
            attemptLabel->setScale(0.26f);
            attemptLabel->limitLabelWidth(112.f, 0.26f, 0.1f);
            attemptLabel->setPosition({144.f, 101.f});
            controls->addChild(attemptLabel);

            m_attemptLimitInput = TextInput::create(64.f, "20");
            m_attemptLimitInput->setCommonFilter(CommonFilter::Uint);
            m_attemptLimitInput->setMaxCharCount(3);
            m_attemptLimitInput->setString(fmt::format("{}", mm->m_guidedAttemptLimit));
            m_attemptLimitInput->setPosition({181.f, 101.f});
            m_attemptLimitInput->setScale(0.5f);
            m_attemptLimitInput->setCallback([this](std::string const& value) {
                auto mm = ModManager::sharedState();
                mm->m_guidedAttemptLimit = parseNumber(value, 20, 1, 999);
                Mod::get()->setSavedValue("guided-attempt-limit", mm->m_guidedAttemptLimit);
            });
            controls->addChild(m_attemptLimitInput);

            auto toggleMenu = CCMenu::create();
            toggleMenu->setPosition({0.f, 0.f});
            controls->addChild(toggleMenu);

            auto switcherLabel = CCLabelBMFont::create("Startpos Switcher", "bigFont.fnt");
            switcherLabel->setAnchorPoint({1.f, 0.5f});
            switcherLabel->setScale(0.22f);
            switcherLabel->limitLabelWidth(132.f, 0.22f, 0.1f);
            switcherLabel->setPosition({144.f, 76.f});
            controls->addChild(switcherLabel);

            m_switcherToggleBtn = CCMenuItemSpriteExtra::create(
                createToggleSprite(mm->m_showStartposSwitcher),
                this,
                menu_selector(LearnerStatsPopup::onToggleSwitcher)
            );
            m_switcherToggleBtn->setPosition({190.f, 76.f});
            toggleMenu->addChild(m_switcherToggleBtn);

            auto percentLabel = CCLabelBMFont::create("Top Right %", "bigFont.fnt");
            percentLabel->setAnchorPoint({1.f, 0.5f});
            percentLabel->setScale(0.24f);
            percentLabel->limitLabelWidth(132.f, 0.24f, 0.1f);
            percentLabel->setPosition({144.f, 51.f});
            controls->addChild(percentLabel);

            m_percentToggleBtn = CCMenuItemSpriteExtra::create(
                createToggleSprite(mm->m_showGuidedPercent),
                this,
                menu_selector(LearnerStatsPopup::onTogglePercent)
            );
            m_percentToggleBtn->setPosition({190.f, 51.f});
            toggleMenu->addChild(m_percentToggleBtn);

            auto mode = CCLabelBMFont::create(
                ModManager::sharedState()->m_guidedMode ? "Mode: Enabled" : "Mode: Disabled",
                "bigFont.fnt"
            );
            mode->setAnchorPoint({0.5f, 0.5f});
            mode->setScale(0.22f);
            mode->limitLabelWidth(210.f, 0.22f, 0.1f);
            mode->setPosition({165.f, 28.f});
            controls->addChild(mode);

            if (playLayer) {
                auto phase = CCLabelBMFont::create(fmt::format(
                    "Phase {}/{}   Runs {}/{}",
                    playLayer->getGuidedPhaseIndex(),
                    playLayer->getGuidedPhaseCount(),
                    playLayer->getGuidedPhaseStageIndex(),
                    playLayer->getGuidedPhaseStageCount()
                ).c_str(), "bigFont.fnt");
                phase->setAnchorPoint({0.5f, 0.5f});
                phase->setScale(0.19f);
                phase->limitLabelWidth(240.f, 0.19f, 0.1f);
                phase->setPosition({165.f, 12.f});
                controls->addChild(phase);
            }

            m_settingsControls = controls;
            m_mainLayer->addChild(controls, 3);
        }

        void buildSettingsList() {
            if (m_statsScroll) {
                m_statsScroll->removeFromParentAndCleanup(true);
                m_statsScroll = nullptr;
            }
            buildSettingsControls();
        }

        void onClearStats(CCObject*) {
            if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                playLayer->clearLearnerStats();
            }
            refreshCurrentView();
        }

        void onToggleGuided(CCObject*) {
            auto mm = ModManager::sharedState();
            mm->m_guidedMode = !mm->m_guidedMode;
            Mod::get()->setSavedValue("guided-mode", mm->m_guidedMode);

            if (m_guidedBtn) {
                m_guidedBtn->setSprite(createGuidedButtonSprite());
            }
            if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                playLayer->applyGuidedStartPos();
            }
            refreshCurrentView();
        }

        void onToggleView(CCObject*) {
            m_showingSettings = !m_showingSettings;
            if (m_viewBtn) {
                m_viewBtn->setSprite(createViewButtonSprite());
            }
            if (m_clearBtn) {
                m_clearBtn->setVisible(!m_showingSettings);
            }
            if (m_resetGuidedBtn) {
                m_resetGuidedBtn->setVisible(m_showingSettings);
            }
            refreshCurrentView();
        }

        void refreshPlayLayerUI() {
            if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                if (auto uiLayer = static_cast<HookUILayer*>(playLayer->m_uiLayer)) {
                    uiLayer->updateUI();
                }
            }
        }

        void onToggleSwitcher(CCObject*) {
            auto mm = ModManager::sharedState();
            mm->m_showStartposSwitcher = !mm->m_showStartposSwitcher;
            Mod::get()->setSavedValue("show-startpos-switcher", mm->m_showStartposSwitcher);
            if (m_switcherToggleBtn) {
                m_switcherToggleBtn->setSprite(createToggleSprite(mm->m_showStartposSwitcher));
            }
            refreshPlayLayerUI();
        }

        void onTogglePercent(CCObject*) {
            auto mm = ModManager::sharedState();
            mm->m_showGuidedPercent = !mm->m_showGuidedPercent;
            Mod::get()->setSavedValue("show-guided-percent", mm->m_showGuidedPercent);
            if (m_percentToggleBtn) {
                m_percentToggleBtn->setSprite(createToggleSprite(mm->m_showGuidedPercent));
            }
            refreshPlayLayerUI();
        }

        void onResetGuided(CCObject*) {
            if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                playLayer->resetGuidedProgress();
                playLayer->applyGuidedStartPos();
            }
            refreshCurrentView();
        }

        void refreshCurrentView() {
            if (m_showingSettings) {
                buildSettingsList();
            } else {
                buildStatsList();
            }
        }

    public:
        static LearnerStatsPopup* create() {
            auto ret = new LearnerStatsPopup();
            if (ret && ret->init()) {
                ret->autorelease();
                return ret;
            }

            CC_SAFE_DELETE(ret);
            return nullptr;
        }
    };
}

class $modify(HookPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto director = CCDirector::sharedDirector();
        auto menu = CCMenu::create();
        menu->setID("learner-stats-menu"_spr);
        menu->setPosition({0.f, 0.f});

        auto sprite = ButtonSprite::create("Stats", 54, 0, 0.55f, true, "goldFont.fnt", "GJ_button_01.png", 28.f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(HookPauseLayer::onLearnerStats));
        button->setID("learner-stats-button"_spr);
        button->setPosition({
            director->getScreenLeft() + 35.f,
            director->getScreenTop() - 24.f
        });

        menu->addChild(button);
        this->addChild(menu, 10);
    }

    void onLearnerStats(CCObject*) {
        LearnerStatsPopup::create()->show();
    }
};

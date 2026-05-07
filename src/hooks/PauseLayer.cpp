#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include "PlayLayer.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    class LearnerStatsPopup : public Popup {
    protected:
        geode::Ref<ScrollLayer> m_statsScroll = nullptr;

    protected:
        bool init() {
            if (!Popup::init(330.f, 205.f)) {
                return false;
            }

            this->setTitle("Learner Stats");
            buildStatsList();

            auto clearSpr = ButtonSprite::create("Clear Stats", 88, 0, 0.42f, true, "goldFont.fnt", "GJ_button_06.png", 24.f);
            auto clearBtn = CCMenuItemSpriteExtra::create(clearSpr, this, menu_selector(LearnerStatsPopup::onClearStats));
            clearBtn->setID("clear-stats-button"_spr);
            m_buttonMenu->addChildAtPosition(clearBtn, Anchor::Bottom, ccp(0.f, 20.f));

            return true;
        }

        void buildStatsList() {
            if (m_statsScroll) {
                m_statsScroll->removeFromParentAndCleanup(true);
                m_statsScroll = nullptr;
            }

            auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get());

            auto rows = std::vector<std::pair<std::string, float>>();
            if (playLayer) {
                auto& bestRunEndPercents = playLayer->m_fields->m_bestRunEndPercents;
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

            if (rows.empty()) {
                rows.emplace_back("No runs yet", 0.42f);
            }

            auto scroll = ScrollLayer::create({290.f, 118.f});
            scroll->setID("stats-scroll"_spr);
            scroll->ignoreAnchorPointForPosition(false);
            scroll->setAnchorPoint({0.5f, 0.5f});

            auto contentHeight = std::max(118.f, rows.size() * 16.f + 10.f);
            scroll->m_contentLayer->setContentSize({290.f, contentHeight});

            auto y = contentHeight - 18.f;
            for (auto const& [text, scale] : rows) {
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

        void onClearStats(CCObject*) {
            if (auto playLayer = static_cast<HookPlayLayer*>(PlayLayer::get())) {
                playLayer->clearLearnerStats();
            }
            buildStatsList();
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

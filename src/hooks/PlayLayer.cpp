#include "PlayLayer.hpp"
#include "Geode/binding/StartPosObject.hpp"
#include "UILayer.hpp"
#include "../ModManager.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    int percentFromX(float x, float levelLength) {
        if (levelLength <= 0.f) {
            return 0;
        }

        auto percent = static_cast<int>(std::round(x / levelLength * 100.f));
        return std::clamp(percent, 0, 100);
    }

    uint64_t stableHash(std::string const& value) {
        auto hash = uint64_t(14695981039346656037ull);
        for (auto ch : value) {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

void HookPlayLayer::addObject(GameObject* obj) {
    if (obj->m_objectID == 31) {
        if(!static_cast<StartPosObject*>(obj)->m_startSettings->m_disableStartPos || !ModManager::sharedState()->m_ignoreDisabled)
            m_fields->m_startPosObjects.push_back(obj);
    }
    PlayLayer::addObject(obj);
}

void HookPlayLayer::updateStartPos(int idx) {
    auto fields = m_fields.self();

    if (fields->m_startPosObjects.size() == 0)
        return;

    if(idx < 0) idx = fields->m_startPosObjects.size();
    if(idx > fields->m_startPosObjects.size()) idx = 0;

    
    if(idx == 0) {
        m_isTestMode = false;
        updateTestModeLabel();
    } else {
        m_isTestMode = true;
        updateTestModeLabel();
    }

    m_currentCheckpoint = nullptr;
    fields->m_startPosIdx = idx;

    GameObject* object = nullptr;

    if(true) {
        object = idx > 0 ? fields->m_startPosObjects[idx - 1] : nullptr;
    } else {
        auto rand = std::rand() % fields->m_startPosObjects.size() + 1;
        object = idx > 0 ? fields->m_startPosObjects[rand] : nullptr;
    }
    setStartPosObject(static_cast<StartPosObject*>(object));

    if(m_isPracticeMode)
        resetLevelFromStart();

    resetLevel();
    startMusic();

    static_cast<HookUILayer*>(m_uiLayer)->updateUI();
}

void HookPlayLayer::createObjectsFromSetupFinished() {
    PlayLayer::createObjectsFromSetupFinished();
    auto fields = m_fields.self();

    std::sort(fields->m_startPosObjects.begin(), fields->m_startPosObjects.end(), [](auto a, auto b) { return a->getPositionX() < b->getPositionX(); });

    if(this->m_startPosObject) {
        auto currentIdx = find(fields->m_startPosObjects.begin(), fields->m_startPosObjects.end(), this->m_startPosObject) - fields->m_startPosObjects.begin();
        fields->m_startPosIdx = currentIdx + 1;
    }

    loadLearnerRuns();
    beginLearnerRun();

    static_cast<HookUILayer*>(m_uiLayer)->updateUI();
}

void HookPlayLayer::resetLevel() {
    PlayLayer::resetLevel();
    beginLearnerRun();
}

void HookPlayLayer::updateProgressbar() {
    PlayLayer::updateProgressbar();
    recordLearnerProgress();
}

void HookPlayLayer::destroyPlayer(PlayerObject* player, GameObject* object) {
    recordLearnerProgress();
    recordLearnerDeathAttempt();
    PlayLayer::destroyPlayer(player, object);
}

void HookPlayLayer::levelComplete() {
    recordLearnerProgress(true);
    PlayLayer::levelComplete();
}

void HookPlayLayer::beginLearnerRun() {
    auto fields = m_fields.self();
    fields->m_activeRunStartIdx = fields->m_startPosIdx;
    fields->m_activeRunCleared = false;
    fields->m_activeRunAttemptCounted = false;

    auto sectionCount = fields->m_startPosObjects.size() + 1;
    if (
        fields->m_bestRunEndPercents.size() != sectionCount ||
        fields->m_sectionAttempts.size() != sectionCount ||
        fields->m_sectionClears.size() != sectionCount ||
        fields->m_sectionAttemptDiscounts.size() != sectionCount
    ) {
        loadLearnerRuns();
    }
    fields->m_activeRunClearedSections.assign(sectionCount, false);
    fields->m_activeRunDeathCountedSections.assign(sectionCount, false);

    recordLearnerProgress();
}

void HookPlayLayer::recordLearnerProgress(bool completed) {
    auto fields = m_fields.self();
    auto idx = fields->m_activeRunStartIdx;
    if (idx < 0 || idx >= fields->m_bestRunEndPercents.size()) {
        return;
    }

    auto percent = completed ? 100 : percentFromX(m_player1 ? m_player1->getPositionX() : 0.f, m_levelLength);
    percent = std::max(percent, getLearnerStartPercent(idx));
    auto changed = false;

    if (percent > fields->m_bestRunEndPercents[idx]) {
        fields->m_bestRunEndPercents[idx] = percent;
        changed = true;
    }

    for (auto section = idx; section < fields->m_bestRunEndPercents.size(); section++) {
        if (percent >= getLearnerClearTargetPercent(section)) {
            recordLearnerSectionClear(section);
            changed = true;
        }
    }

    if (changed) {
        saveLearnerRuns();
    }
}

void HookPlayLayer::recordLearnerSectionClear(int index) {
    auto fields = m_fields.self();
    if (index < 0 || index >= fields->m_sectionClears.size()) {
        return;
    }

    if (fields->m_activeRunClearedSections.size() != fields->m_sectionClears.size()) {
        fields->m_activeRunClearedSections.assign(fields->m_sectionClears.size(), false);
    }
    if (fields->m_activeRunClearedSections[index]) {
        return;
    }

    fields->m_activeRunClearedSections[index] = true;
    fields->m_sectionClears[index]++;

    if (index == fields->m_activeRunStartIdx) {
        fields->m_activeRunCleared = true;
        return;
    }

    fields->m_sectionAttemptDiscounts[index]++;
}

void HookPlayLayer::recordLearnerDeathAttempt() {
    auto fields = m_fields.self();
    if (fields->m_sectionAttempts.empty()) {
        return;
    }

    auto percent = percentFromX(m_player1 ? m_player1->getPositionX() : 0.f, m_levelLength);
    auto section = fields->m_activeRunStartIdx;
    for (auto idx = fields->m_activeRunStartIdx; idx < fields->m_sectionAttempts.size(); idx++) {
        if (percent < getLearnerClearTargetPercent(idx)) {
            section = idx;
            break;
        }
    }

    if (section < 0 || section >= fields->m_sectionAttempts.size()) {
        return;
    }
    if (fields->m_sectionClears[section] <= 0) {
        return;
    }

    if (fields->m_activeRunDeathCountedSections.size() != fields->m_sectionAttempts.size()) {
        fields->m_activeRunDeathCountedSections.assign(fields->m_sectionAttempts.size(), false);
    }
    if (fields->m_activeRunDeathCountedSections[section]) {
        return;
    }

    fields->m_activeRunDeathCountedSections[section] = true;
    fields->m_sectionAttempts[section]++;
    saveLearnerRuns();
}

int HookPlayLayer::getLearnerStartPercent(int index) {
    if (index <= 0) {
        return 0;
    }

    auto fields = m_fields.self();
    auto startPosIdx = static_cast<size_t>(index - 1);
    if (startPosIdx >= fields->m_startPosObjects.size()) {
        return 0;
    }

    return percentFromX(fields->m_startPosObjects[startPosIdx]->getPositionX(), m_levelLength);
}

int HookPlayLayer::getLearnerClearTargetPercent(int index) {
    auto fields = m_fields.self();
    auto nextStartPosIdx = static_cast<size_t>(index);
    if (nextStartPosIdx < fields->m_startPosObjects.size()) {
        return percentFromX(fields->m_startPosObjects[nextStartPosIdx]->getPositionX(), m_levelLength);
    }

    return 100;
}

int HookPlayLayer::getLearnerAdjustedAttempts(int index) {
    auto fields = m_fields.self();
    if (index < 0 || index >= fields->m_sectionAttempts.size()) {
        return 0;
    }

    auto discounts = index < fields->m_sectionAttemptDiscounts.size() ? fields->m_sectionAttemptDiscounts[index] : 0;
    return std::max(0, fields->m_sectionAttempts[index] - discounts);
}

void HookPlayLayer::loadLearnerRuns() {
    auto fields = m_fields.self();
    auto sectionCount = fields->m_startPosObjects.size() + 1;
    auto baseRuns = std::vector<int>(sectionCount, 0);
    for (auto idx = 0u; idx < baseRuns.size(); idx++) {
        baseRuns[idx] = getLearnerStartPercent(idx);
    }

    auto savedRuns = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey(), {});
    for (auto idx = 0u; idx < std::min(baseRuns.size(), savedRuns.size()); idx++) {
        baseRuns[idx] = std::max(baseRuns[idx], std::clamp(savedRuns[idx], 0, 100));
    }

    fields->m_bestRunEndPercents = baseRuns;

    auto savedAttempts = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":attempts", {});
    auto savedClears = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":clears", {});
    auto savedDiscounts = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":discounts", {});
    if (savedClears.empty()) {
        savedClears = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":passes", {});
    }
    fields->m_sectionAttempts.assign(sectionCount, 0);
    fields->m_sectionClears.assign(sectionCount, 0);
    fields->m_sectionAttemptDiscounts.assign(sectionCount, 0);
    fields->m_activeRunClearedSections.assign(sectionCount, false);
    fields->m_activeRunDeathCountedSections.assign(sectionCount, false);

    for (auto idx = 0u; idx < std::min(fields->m_sectionAttempts.size(), savedAttempts.size()); idx++) {
        fields->m_sectionAttempts[idx] = std::max(savedAttempts[idx], 0);
    }
    for (auto idx = 0u; idx < std::min(fields->m_sectionClears.size(), savedClears.size()); idx++) {
        fields->m_sectionClears[idx] = std::clamp(savedClears[idx], 0, fields->m_sectionAttempts[idx]);
    }
    for (auto idx = 0u; idx < std::min(fields->m_sectionAttemptDiscounts.size(), savedDiscounts.size()); idx++) {
        fields->m_sectionAttemptDiscounts[idx] = std::max(savedDiscounts[idx], 0);
    }
}

void HookPlayLayer::saveLearnerRuns() {
    Mod::get()->setSavedValue(getLearnerSaveKey(), m_fields->m_bestRunEndPercents);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":attempts", m_fields->m_sectionAttempts);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":clears", m_fields->m_sectionClears);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":discounts", m_fields->m_sectionAttemptDiscounts);
}

void HookPlayLayer::clearLearnerStats() {
    auto fields = m_fields.self();
    auto sectionCount = fields->m_startPosObjects.size() + 1;

    fields->m_bestRunEndPercents.assign(sectionCount, 0);
    for (auto idx = 0u; idx < fields->m_bestRunEndPercents.size(); idx++) {
        fields->m_bestRunEndPercents[idx] = getLearnerStartPercent(idx);
    }

    fields->m_sectionAttempts.assign(sectionCount, 0);
    fields->m_sectionClears.assign(sectionCount, 0);
    fields->m_sectionAttemptDiscounts.assign(sectionCount, 0);
    fields->m_activeRunClearedSections.assign(sectionCount, false);
    fields->m_activeRunDeathCountedSections.assign(sectionCount, false);
    fields->m_activeRunCleared = false;
    fields->m_activeRunAttemptCounted = false;
    saveLearnerRuns();
}

std::string HookPlayLayer::getLearnerSaveKey() {
    if (!m_level) {
        return "best-runs:unknown";
    }

    auto levelID = static_cast<int>(m_level->m_levelID);
    if (levelID > 0) {
        return fmt::format("best-runs:id:{}", levelID);
    }

    auto levelName = std::string(m_level->m_levelName);
    auto levelString = std::string(m_level->m_levelString);
    return fmt::format("best-runs:local:{}", stableHash(levelName + ":" + levelString));
}

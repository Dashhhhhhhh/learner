#include "PlayLayer.hpp"
#include "Geode/binding/FMODAudioEngine.hpp"
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
    setLearnerStartPos(idx, true);
}

void HookPlayLayer::setLearnerStartPos(int idx, bool shouldReset) {
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

    if (!shouldReset) {
        static_cast<HookUILayer*>(m_uiLayer)->updateUI();
        return;
    }

    if(m_isPracticeMode)
        resetLevelFromStart();

    resetLevel();
    startMusic();
    syncLearnerStartPosMusic();

    static_cast<HookUILayer*>(m_uiLayer)->updateUI();
}

void HookPlayLayer::syncLearnerStartPosMusic() {
    if (!m_startPosObject || !m_startPosObject->m_startSettings) {
        return;
    }

    auto settings = m_startPosObject->m_startSettings;
    auto levelSongOffset = m_levelSettings ? m_levelSettings->m_songOffset : 0.f;
    auto startPosSongOffset = settings->m_songOffset;
    if (std::abs(startPosSongOffset - levelSongOffset) < 0.001f) {
        startPosSongOffset = 0.f;
    }

    auto songTime = timeForPos(
        m_startPosObject->getPosition(),
        settings->m_targetOrder,
        settings->m_targetChannel,
        true,
        0
    ) + levelSongOffset + startPosSongOffset;
    auto songTimeMS = static_cast<unsigned int>(std::max(0.f, songTime) * 1000.f);
    FMODAudioEngine::get()->setMusicTimeMS(songTimeMS, true, 0);
}

void HookPlayLayer::syncLearnerStartPosMusicDelayed(float) {
    syncLearnerStartPosMusic();
}

void HookPlayLayer::queueLearnerStartPosMusicSync() {
    if (!m_startPosObject) {
        return;
    }

    syncLearnerStartPosMusic();
    unschedule(schedule_selector(HookPlayLayer::syncLearnerStartPosMusicDelayed));
    scheduleOnce(schedule_selector(HookPlayLayer::syncLearnerStartPosMusicDelayed), 0.05f);
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
    applyGuidedStartPos(false);
    beginLearnerRun();

    static_cast<HookUILayer*>(m_uiLayer)->updateUI();
}

void HookPlayLayer::resetLevel() {
    if (m_fields->m_guidedStartPosPending) {
        m_fields->m_guidedStartPosPending = false;
        applyGuidedStartPos(false);
    }
    PlayLayer::resetLevel();
    if (m_startPosObject) {
        prepareMusic(false);
        startMusic();
        queueLearnerStartPosMusicSync();
    } else {
        unschedule(schedule_selector(HookPlayLayer::syncLearnerStartPosMusicDelayed));
    }
    beginLearnerRun();
}

void HookPlayLayer::updateProgressbar() {
    PlayLayer::updateProgressbar();
    recordLearnerProgress();
    updateGuidedProgress();
    static_cast<HookUILayer*>(m_uiLayer)->updateGuidedChanceLabel();
}

void HookPlayLayer::destroyPlayer(PlayerObject* player, GameObject* object) {
    recordLearnerProgress();
    auto guidedAlreadyCleared = m_fields->m_guidedRunCleared;
    auto guidedCleared = updateGuidedProgress();
    if (!guidedAlreadyCleared && !guidedCleared) {
        recordLearnerDeathAttempt();
        if (!m_fields->m_activeRunAttemptCounted) {
            m_fields->m_activeRunAttemptCounted = true;
            recordGuidedRouteAttempt();
        }
    }
    PlayLayer::destroyPlayer(player, object);
}

void HookPlayLayer::levelComplete() {
    recordLearnerProgress(true);
    auto guidedCleared = updateGuidedProgress(true);
    (void)guidedCleared;
    PlayLayer::levelComplete();
}

void HookPlayLayer::beginLearnerRun() {
    auto fields = m_fields.self();
    fields->m_activeRunStartIdx = fields->m_startPosIdx;
    fields->m_activeRunCleared = false;
    fields->m_activeRunAttemptCounted = false;
    fields->m_activeRunGuidedQueued = false;
    fields->m_guidedRunCleared = false;
    fields->m_ignoreProgressUntilRunBegin = false;

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
    normalizeGuidedRoute();

    recordLearnerProgress();
}

void HookPlayLayer::recordLearnerProgress(bool completed) {
    auto fields = m_fields.self();
    if (fields->m_ignoreProgressUntilRunBegin) {
        return;
    }

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
    if (fields->m_ignoreProgressUntilRunBegin) {
        return;
    }

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

float HookPlayLayer::getLearnerClearRate(int index) {
    auto fields = m_fields.self();
    if (index < 0 || index >= fields->m_sectionClears.size()) {
        return 0.f;
    }

    auto clears = fields->m_sectionClears[index];
    auto failures = getLearnerAdjustedAttempts(index);
    auto total = clears + failures;
    return total > 0 ? static_cast<float>(clears) / total : 0.f;
}

void HookPlayLayer::normalizeGuidedRoute() {
    auto fields = m_fields.self();
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    if (sectionCount <= 0) {
        fields->m_guidedChainLength = 1;
        fields->m_guidedWindowStart = 0;
        return;
    }

    cleanGuidedRouteData();
    fields->m_guidedChainLength = std::clamp(fields->m_guidedChainLength, 1, getGuidedPhaseCount());
    fields->m_guidedWindowStart = std::clamp(fields->m_guidedWindowStart, 0, sectionCount - 1);

    if (
        isGuidedRoutePlayable(fields->m_guidedWindowStart, fields->m_guidedChainLength) &&
        !isGuidedRouteCompleted(fields->m_guidedWindowStart, fields->m_guidedChainLength)
    ) {
        return;
    }

    auto tryPhase = [&](int phase) -> bool {
        auto start = chooseGuidedStartForPhase(phase, true);
        if (start < 0) {
            return false;
        }
        fields->m_guidedChainLength = phase;
        fields->m_guidedWindowStart = start;
        return true;
    };

    for (auto phase = fields->m_guidedChainLength; phase <= getGuidedPhaseCount(); phase++) {
        if (tryPhase(phase)) {
            return;
        }
    }

    for (auto phase = 1; phase < fields->m_guidedChainLength; phase++) {
        if (tryPhase(phase)) {
            return;
        }
    }

    fields->m_guidedChainLength = getGuidedPhaseCount();
    fields->m_guidedWindowStart = std::clamp(fields->m_guidedWindowStart, 0, sectionCount - 1);
}

int HookPlayLayer::getGuidedRouteLength(int startIndex, int phase) {
    auto fields = m_fields.self();
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    if (startIndex < 0 || startIndex >= sectionCount) {
        return 0;
    }

    auto offset = 0;
    if (startIndex == 0) {
        offset = 2;
    } else if (getLearnerStartPercent(startIndex) < ModManager::sharedState()->m_guidedLateThreshold) {
        offset = 1;
    }

    auto length = phase - offset;
    auto maxLength = sectionCount - startIndex;
    if (length < 1 || length > maxLength) {
        return 0;
    }
    return length;
}

bool HookPlayLayer::isGuidedRoutePlayable(int startIndex, int phase) {
    return getGuidedRouteLength(startIndex, phase) > 0;
}

int HookPlayLayer::getGuidedRouteID(int startIndex, int phase) {
    return phase * 10000 + startIndex;
}

bool HookPlayLayer::isGuidedRouteIDPlayable(int routeID) {
    auto phase = routeID / 10000;
    auto start = routeID % 10000;
    return phase >= 1 && phase <= getGuidedPhaseCount() && isGuidedRoutePlayable(start, phase);
}

void HookPlayLayer::cleanGuidedRouteData() {
    auto fields = m_fields.self();

    auto cleanedRoutes = std::vector<int>();
    for (auto routeID : fields->m_guidedCompletedRoutes) {
        if (!isGuidedRouteIDPlayable(routeID)) {
            continue;
        }
        if (std::find(cleanedRoutes.begin(), cleanedRoutes.end(), routeID) == cleanedRoutes.end()) {
            cleanedRoutes.push_back(routeID);
        }
    }
    fields->m_guidedCompletedRoutes = cleanedRoutes;

    auto cleanedAttemptIDs = std::vector<int>();
    auto cleanedAttemptCounts = std::vector<int>();
    for (auto idx = 0u; idx < fields->m_guidedAttemptRouteIDs.size(); idx++) {
        auto routeID = fields->m_guidedAttemptRouteIDs[idx];
        if (
            !isGuidedRouteIDPlayable(routeID) ||
            std::find(fields->m_guidedCompletedRoutes.begin(), fields->m_guidedCompletedRoutes.end(), routeID) != fields->m_guidedCompletedRoutes.end()
        ) {
            continue;
        }

        auto count = idx < fields->m_guidedAttemptRouteCounts.size() ? fields->m_guidedAttemptRouteCounts[idx] : 0;
        if (count <= 0 || std::find(cleanedAttemptIDs.begin(), cleanedAttemptIDs.end(), routeID) != cleanedAttemptIDs.end()) {
            continue;
        }

        cleanedAttemptIDs.push_back(routeID);
        cleanedAttemptCounts.push_back(count);
    }
    fields->m_guidedAttemptRouteIDs = cleanedAttemptIDs;
    fields->m_guidedAttemptRouteCounts = cleanedAttemptCounts;
}

int HookPlayLayer::getGuidedCompletedStageCount() {
    auto count = 0;
    auto sectionCount = static_cast<int>(m_fields->m_startPosObjects.size() + 1);
    for (auto phase = 1; phase <= getGuidedPhaseCount(); phase++) {
        for (auto start = sectionCount - 1; start >= 0; start--) {
            if (isGuidedRoutePlayable(start, phase) && isGuidedRouteCompleted(start, phase)) {
                count++;
            }
        }
    }
    return count;
}

bool HookPlayLayer::isGuidedRouteCompleted(int startIndex, int phase) {
    auto id = getGuidedRouteID(startIndex, phase);
    auto& routes = m_fields->m_guidedCompletedRoutes;
    return std::find(routes.begin(), routes.end(), id) != routes.end();
}

void HookPlayLayer::markGuidedRouteCompleted(int startIndex, int phase) {
    if (!isGuidedRoutePlayable(startIndex, phase) || isGuidedRouteCompleted(startIndex, phase)) {
        return;
    }
    m_fields->m_guidedCompletedRoutes.push_back(getGuidedRouteID(startIndex, phase));
    clearGuidedRouteAttempts(startIndex, phase);
}

int HookPlayLayer::getGuidedRouteAttempts(int startIndex, int phase) {
    auto id = getGuidedRouteID(startIndex, phase);
    auto fields = m_fields.self();
    for (auto idx = 0u; idx < fields->m_guidedAttemptRouteIDs.size(); idx++) {
        if (fields->m_guidedAttemptRouteIDs[idx] == id && idx < fields->m_guidedAttemptRouteCounts.size()) {
            return fields->m_guidedAttemptRouteCounts[idx];
        }
    }
    return 0;
}

void HookPlayLayer::clearGuidedRouteAttempts(int startIndex, int phase) {
    auto id = getGuidedRouteID(startIndex, phase);
    auto fields = m_fields.self();
    for (auto idx = 0u; idx < fields->m_guidedAttemptRouteIDs.size(); idx++) {
        if (fields->m_guidedAttemptRouteIDs[idx] == id) {
            fields->m_guidedAttemptRouteIDs.erase(fields->m_guidedAttemptRouteIDs.begin() + idx);
            if (idx < fields->m_guidedAttemptRouteCounts.size()) {
                fields->m_guidedAttemptRouteCounts.erase(fields->m_guidedAttemptRouteCounts.begin() + idx);
            }
            return;
        }
    }
}

bool HookPlayLayer::recordGuidedRouteAttempt() {
    auto fields = m_fields.self();
    if (!ModManager::sharedState()->m_guidedMode || fields->m_startPosObjects.empty()) {
        return false;
    }

    normalizeGuidedRoute();
    auto start = fields->m_activeRunStartIdx;
    auto phase = fields->m_guidedChainLength;
    if (start != fields->m_guidedWindowStart || !isGuidedRoutePlayable(start, phase) || isGuidedRouteCompleted(start, phase)) {
        return false;
    }

    auto id = getGuidedRouteID(start, phase);
    auto attempts = 1;
    auto found = false;
    for (auto idx = 0u; idx < fields->m_guidedAttemptRouteIDs.size(); idx++) {
        if (fields->m_guidedAttemptRouteIDs[idx] == id) {
            if (idx >= fields->m_guidedAttemptRouteCounts.size()) {
                fields->m_guidedAttemptRouteCounts.resize(idx + 1, 0);
            }
            fields->m_guidedAttemptRouteCounts[idx]++;
            attempts = fields->m_guidedAttemptRouteCounts[idx];
            found = true;
            break;
        }
    }

    if (!found) {
        fields->m_guidedAttemptRouteIDs.push_back(id);
        fields->m_guidedAttemptRouteCounts.push_back(1);
    }

    if (attempts > ModManager::sharedState()->m_guidedAttemptLimit) {
        if (rotateGuidedRouteInPhase(start, phase) || advanceGuidedRouteToNextIncompletePhase()) {
            fields->m_guidedStartPosPending = true;
            saveGuidedProgress();
            return true;
        }
    }

    saveGuidedProgress();
    return false;
}

bool HookPlayLayer::rotateGuidedRouteInPhase(int startIndex, int phase) {
    auto fields = m_fields.self();
    auto candidates = std::vector<int>();
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    for (auto start = sectionCount - 1; start >= 0; start--) {
        if (start != 0 && start != startIndex && isGuidedRoutePlayable(start, phase) && !isGuidedRouteCompleted(start, phase)) {
            candidates.push_back(start);
        }
    }

    auto zeroAvailable = startIndex != 0 &&
        isGuidedRoutePlayable(0, phase) &&
        !isGuidedRouteCompleted(0, phase);
    if (candidates.empty() && !zeroAvailable) {
        return false;
    }

    clearGuidedRouteAttempts(startIndex, phase);
    if (zeroAvailable && (candidates.empty() || std::rand() % 100 < getGuidedZeroChancePercent())) {
        fields->m_guidedWindowStart = 0;
    } else {
        fields->m_guidedWindowStart = candidates[std::rand() % candidates.size()];
    }
    return true;
}

bool HookPlayLayer::advanceGuidedRouteToNextIncompletePhase() {
    auto fields = m_fields.self();
    for (auto phase = fields->m_guidedChainLength + 1; phase <= getGuidedPhaseCount(); phase++) {
        auto start = chooseGuidedStartForPhase(phase, true);
        if (start >= 0) {
            fields->m_guidedChainLength = phase;
            fields->m_guidedWindowStart = start;
            return true;
        }
    }
    for (auto phase = 1; phase <= fields->m_guidedChainLength; phase++) {
        auto start = chooseGuidedStartForPhase(phase, true);
        if (start >= 0) {
            fields->m_guidedChainLength = phase;
            fields->m_guidedWindowStart = start;
            return true;
        }
    }
    return false;
}

int HookPlayLayer::getGuidedLastStartForPhase(int phase) {
    auto sectionCount = static_cast<int>(m_fields->m_startPosObjects.size() + 1);
    for (auto start = sectionCount - 1; start >= 0; start--) {
        if (isGuidedRoutePlayable(start, phase) && !isGuidedRouteCompleted(start, phase)) {
            return start;
        }
    }
    return -1;
}

int HookPlayLayer::chooseGuidedStartForPhase(int phase, bool allowZeroChance) {
    if (
        allowZeroChance &&
        isGuidedRoutePlayable(0, phase) &&
        !isGuidedRouteCompleted(0, phase) &&
        std::rand() % 100 < getGuidedZeroChancePercent()
    ) {
        return 0;
    }
    return getGuidedLastStartForPhase(phase);
}

int HookPlayLayer::getGuidedZeroChancePercent() {
    auto total = getGuidedStageCount();
    if (total <= 0) {
        return 0;
    }
    auto completed = getGuidedCompletedStageCount();
    auto currentStage = std::clamp(completed + (completed < total ? 1 : 0), 1, total);
    return std::clamp(static_cast<int>(std::round(static_cast<float>(currentStage) / total * 100.f)), 0, 100);
}

int HookPlayLayer::getGuidedPhaseIndex() {
    normalizeGuidedRoute();
    return m_fields->m_guidedChainLength;
}

int HookPlayLayer::getGuidedPhaseCount() {
    auto sectionCount = static_cast<int>(m_fields->m_startPosObjects.size() + 1);
    return sectionCount;
}

int HookPlayLayer::getGuidedPhaseStageIndex() {
    auto fields = m_fields.self();
    normalizeGuidedRoute();

    auto stage = 0;
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    for (auto start = sectionCount - 1; start >= 0; start--) {
        if (isGuidedRoutePlayable(start, fields->m_guidedChainLength) && isGuidedRouteCompleted(start, fields->m_guidedChainLength)) {
            stage++;
        }
    }
    auto total = getGuidedPhaseStageCount();
    return std::clamp(stage + (stage < total ? 1 : 0), 1, total);
}

int HookPlayLayer::getGuidedPhaseStageCount() {
    auto count = 0;
    auto sectionCount = static_cast<int>(m_fields->m_startPosObjects.size() + 1);
    for (auto start = sectionCount - 1; start >= 0; start--) {
        if (isGuidedRoutePlayable(start, m_fields->m_guidedChainLength)) {
            count++;
        }
    }
    return std::max(1, count);
}

int HookPlayLayer::getGuidedStageIndex() {
    normalizeGuidedRoute();

    auto total = getGuidedStageCount();
    auto completed = getGuidedCompletedStageCount();
    return std::clamp(completed + (completed < total ? 1 : 0), 1, total);
}

int HookPlayLayer::getGuidedStageCount() {
    auto count = 0;
    auto sectionCount = static_cast<int>(m_fields->m_startPosObjects.size() + 1);
    for (auto phase = 1; phase <= getGuidedPhaseCount(); phase++) {
        for (auto start = sectionCount - 1; start >= 0; start--) {
            if (isGuidedRoutePlayable(start, phase)) {
                count++;
            }
        }
    }
    return std::max(1, count);
}

int HookPlayLayer::getGuidedRunTargetPercent() {
    auto fields = m_fields.self();
    normalizeGuidedRoute();

    auto routeLength = getGuidedRouteLength(fields->m_guidedWindowStart, fields->m_guidedChainLength);
    if (routeLength <= 0) {
        return 100;
    }

    auto endSection = fields->m_guidedWindowStart + routeLength - 1;
    return getLearnerClearTargetPercent(endSection);
}

bool HookPlayLayer::updateGuidedProgress(bool completed) {
    auto fields = m_fields.self();
    if (!ModManager::sharedState()->m_guidedMode || fields->m_startPosObjects.empty() || fields->m_ignoreProgressUntilRunBegin) {
        return false;
    }

    auto runStart = fields->m_activeRunStartIdx;
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    if (runStart < 0 || runStart >= sectionCount) {
        return false;
    }

    normalizeGuidedRoute();

    auto percent = completed ? 100 : percentFromX(m_player1 ? m_player1->getPositionX() : 0.f, m_levelLength);
    auto changed = false;
    for (auto start = runStart; start < sectionCount; start++) {
        for (auto phase = 1; phase <= getGuidedPhaseCount(); phase++) {
            auto length = getGuidedRouteLength(start, phase);
            if (length <= 0 || isGuidedRouteCompleted(start, phase)) {
                continue;
            }

            auto target = getLearnerClearTargetPercent(start + length - 1);
            if (!completed && target >= 100) {
                continue;
            }
            if (percent >= target) {
                markGuidedRouteCompleted(start, phase);
                changed = true;
            }
        }
    }

    if (!changed) {
        return false;
    }

    fields->m_guidedRunCleared = true;
    fields->m_guidedStartPosPending = true;
    normalizeGuidedRoute();
    saveGuidedProgress();
    return true;
}

void HookPlayLayer::advanceGuidedRoute() {
    auto fields = m_fields.self();
    normalizeGuidedRoute();

    if (getGuidedStageCount() <= 1) {
        fields->m_guidedChainLength = 1;
        fields->m_guidedWindowStart = chooseGuidedStartForPhase(1, true);
        return;
    }

    auto nextStart = fields->m_guidedWindowStart - 1;
    while (
        nextStart >= 0 &&
        (!isGuidedRoutePlayable(nextStart, fields->m_guidedChainLength) || isGuidedRouteCompleted(nextStart, fields->m_guidedChainLength))
    ) {
        nextStart--;
    }
    if (nextStart >= 0) {
        fields->m_guidedWindowStart = nextStart;
        return;
    }

    while (fields->m_guidedChainLength < getGuidedPhaseCount()) {
        fields->m_guidedChainLength++;
        auto start = chooseGuidedStartForPhase(fields->m_guidedChainLength, true);
        if (start >= 0) {
            fields->m_guidedWindowStart = start;
            return;
        }
    }
}

int HookPlayLayer::chooseGuidedStartPos() {
    auto fields = m_fields.self();
    normalizeGuidedRoute();
    if (
        !isGuidedRoutePlayable(fields->m_guidedWindowStart, fields->m_guidedChainLength) ||
        isGuidedRouteCompleted(fields->m_guidedWindowStart, fields->m_guidedChainLength)
    ) {
        return fields->m_startPosIdx;
    }
    return fields->m_guidedWindowStart;
}

bool HookPlayLayer::applyGuidedStartPos(bool shouldReset, bool resetIfSame) {
    auto fields = m_fields.self();
    if (!ModManager::sharedState()->m_guidedMode || fields->m_guidedSwitching || fields->m_startPosObjects.empty()) {
        return false;
    }

    auto target = chooseGuidedStartPos();
    target = std::clamp(target, 0, static_cast<int>(fields->m_startPosObjects.size()));
    if (target == fields->m_startPosIdx && !resetIfSame) {
        return false;
    }

    fields->m_guidedSwitching = true;
    setLearnerStartPos(target, shouldReset);
    if (!shouldReset) {
        fields->m_activeRunStartIdx = target;
        fields->m_activeRunCleared = false;
        fields->m_activeRunAttemptCounted = false;
        fields->m_activeRunGuidedQueued = false;
        fields->m_guidedRunCleared = false;
        fields->m_ignoreProgressUntilRunBegin = true;
    }
    fields->m_guidedSwitching = false;
    return true;
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

    loadGuidedProgress();
}

void HookPlayLayer::saveLearnerRuns() {
    Mod::get()->setSavedValue(getLearnerSaveKey(), m_fields->m_bestRunEndPercents);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":attempts", m_fields->m_sectionAttempts);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":clears", m_fields->m_sectionClears);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":discounts", m_fields->m_sectionAttemptDiscounts);
}

void HookPlayLayer::loadGuidedProgress() {
    auto fields = m_fields.self();
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    auto defaultStart = std::max(0, sectionCount - 1);

    fields->m_guidedChainLength = Mod::get()->getSavedValue<int>(getLearnerSaveKey() + ":guided-chain", 1);
    fields->m_guidedWindowStart = Mod::get()->getSavedValue<int>(getLearnerSaveKey() + ":guided-start", defaultStart);
    fields->m_guidedCompletedRoutes = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":guided-completed", {});
    fields->m_guidedAttemptRouteIDs = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":guided-attempt-ids", {});
    fields->m_guidedAttemptRouteCounts = Mod::get()->getSavedValue<std::vector<int>>(getLearnerSaveKey() + ":guided-attempt-counts", {});
    cleanGuidedRouteData();
    normalizeGuidedRoute();
}

void HookPlayLayer::saveGuidedProgress() {
    cleanGuidedRouteData();
    normalizeGuidedRoute();
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":guided-chain", m_fields->m_guidedChainLength);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":guided-start", m_fields->m_guidedWindowStart);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":guided-completed", m_fields->m_guidedCompletedRoutes);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":guided-attempt-ids", m_fields->m_guidedAttemptRouteIDs);
    Mod::get()->setSavedValue(getLearnerSaveKey() + ":guided-attempt-counts", m_fields->m_guidedAttemptRouteCounts);
}

void HookPlayLayer::resetGuidedProgress() {
    auto fields = m_fields.self();
    auto sectionCount = static_cast<int>(fields->m_startPosObjects.size() + 1);
    fields->m_guidedChainLength = 1;
    fields->m_guidedWindowStart = std::max(0, sectionCount - 1);
    fields->m_guidedCompletedRoutes.clear();
    fields->m_guidedAttemptRouteIDs.clear();
    fields->m_guidedAttemptRouteCounts.clear();
    fields->m_guidedRunCleared = false;
    fields->m_guidedStartPosPending = false;
    saveGuidedProgress();
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
    resetGuidedProgress();
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

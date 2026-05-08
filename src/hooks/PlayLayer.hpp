#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

struct HookPlayLayer : geode::Modify<HookPlayLayer, PlayLayer> {
    struct Fields {
        std::vector<geode::Ref<GameObject>> m_startPosObjects = {};
        std::vector<int> m_bestRunEndPercents = {};
        std::vector<int> m_sectionAttempts = {};
        std::vector<int> m_sectionClears = {};
        std::vector<int> m_sectionAttemptDiscounts = {};
        std::vector<int> m_guidedCompletedRoutes = {};
        std::vector<int> m_guidedAttemptRouteIDs = {};
        std::vector<int> m_guidedAttemptRouteCounts = {};
        std::vector<bool> m_activeRunClearedSections = {};
        std::vector<bool> m_activeRunDeathCountedSections = {};
        int m_startPosIdx = 0;
        int m_activeRunStartIdx = 0;
        int m_guidedChainLength = 1;
        int m_guidedWindowStart = -1;
        bool m_activeRunCleared = false;
        bool m_activeRunAttemptCounted = false;
        bool m_activeRunGuidedQueued = false;
        bool m_guidedRunCleared = false;
        bool m_guidedStartPosPending = false;
        bool m_guidedSwitching = false;
        bool m_ignoreProgressUntilRunBegin = false;
        bool m_canSwitch = true;
    };

    void addObject(GameObject* obj);
    void createObjectsFromSetupFinished();
    void destroyPlayer(PlayerObject* player, GameObject* object);
    void levelComplete();
    void resetLevel();
    void updateProgressbar();

    void updateStartPos(int index);
    void setLearnerStartPos(int index, bool shouldReset);
    void syncLearnerStartPosMusic();
    void beginLearnerRun();
    void recordLearnerProgress(bool completed = false);
    void recordLearnerSectionClear(int index);
    void recordLearnerDeathAttempt();
    int getLearnerStartPercent(int index);
    int getLearnerClearTargetPercent(int index);
    int getLearnerAdjustedAttempts(int index);
    float getLearnerClearRate(int index);
    void normalizeGuidedRoute();
    int getGuidedRouteLength(int startIndex, int phase);
    bool isGuidedRoutePlayable(int startIndex, int phase);
    int getGuidedRouteID(int startIndex, int phase);
    bool isGuidedRouteIDPlayable(int routeID);
    void cleanGuidedRouteData();
    int getGuidedCompletedStageCount();
    bool isGuidedRouteCompleted(int startIndex, int phase);
    void markGuidedRouteCompleted(int startIndex, int phase);
    int getGuidedRouteAttempts(int startIndex, int phase);
    void clearGuidedRouteAttempts(int startIndex, int phase);
    bool recordGuidedRouteAttempt();
    bool rotateGuidedRouteInPhase(int startIndex, int phase);
    bool advanceGuidedRouteToNextIncompletePhase();
    int getGuidedLastStartForPhase(int phase);
    int chooseGuidedStartForPhase(int phase, bool allowZeroChance);
    int getGuidedZeroChancePercent();
    int getGuidedPhaseIndex();
    int getGuidedPhaseCount();
    int getGuidedPhaseStageIndex();
    int getGuidedPhaseStageCount();
    int getGuidedStageIndex();
    int getGuidedStageCount();
    int getGuidedRunTargetPercent();
    bool updateGuidedProgress(bool completed = false);
    void advanceGuidedRoute();
    int chooseGuidedStartPos();
    bool applyGuidedStartPos(bool shouldReset = true, bool resetIfSame = false);
    void loadLearnerRuns();
    void saveLearnerRuns();
    void loadGuidedProgress();
    void saveGuidedProgress();
    void resetGuidedProgress();
    void clearLearnerStats();
    std::string getLearnerSaveKey();
};

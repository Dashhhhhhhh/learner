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
		std::vector<bool> m_activeRunClearedSections = {};
		std::vector<bool> m_activeRunDeathCountedSections = {};
		int m_startPosIdx = 0;
		int m_activeRunStartIdx = 0;
		bool m_activeRunCleared = false;
		bool m_activeRunAttemptCounted = false;
        bool m_canSwitch = true;
	};

	void addObject(GameObject* obj);
    void createObjectsFromSetupFinished();
	void destroyPlayer(PlayerObject* player, GameObject* object);
	void levelComplete();
	void resetLevel();
	void updateProgressbar();

	void updateStartPos(int index);
	void beginLearnerRun();
	void recordLearnerProgress(bool completed = false);
	void recordLearnerSectionClear(int index);
	void recordLearnerDeathAttempt();
	int getLearnerStartPercent(int index);
	int getLearnerClearTargetPercent(int index);
	int getLearnerAdjustedAttempts(int index);
	void loadLearnerRuns();
	void saveLearnerRuns();
	void clearLearnerStats();
	std::string getLearnerSaveKey();
};

#pragma once

class ModManager {
public:
    static ModManager* sharedState();

    ModManager();

    bool m_dontFadeOnStart = false;
    bool m_hideBtns = false;
    bool m_ignoreDisabled = false;
    bool m_guidedMode = false;
    bool m_showStartposSwitcher = true;
    bool m_showGuidedPercent = true;
    int m_guidedLateThreshold = 50;
    int m_guidedAttemptLimit = 20;
    double m_opacity = 0;
};

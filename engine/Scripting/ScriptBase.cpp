#include "ScriptBase.h"
#include "../Scene/Scene.h"

void ScriptBase::SetEnabled(bool enabled) {
    if (m_IsEnabled == enabled) return;

    m_IsEnabled = enabled;

    if (enabled) {
        OnEnable();
    } else {
        OnDisable();
    }
}

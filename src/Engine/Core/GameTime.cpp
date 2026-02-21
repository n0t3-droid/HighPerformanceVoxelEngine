#include "Engine/Core/GameTime.h"

#include <algorithm>

namespace Engine {

    GameTime::GameTime(double fixedDeltaSeconds, double maxFrameSeconds)
        : m_FixedDelta(std::max(0.0001, fixedDeltaSeconds))
        , m_MaxFrame(std::max(m_FixedDelta, maxFrameSeconds)) {
    }

    double GameTime::BeginFrame(double nowSeconds) {
        if (m_LastTime < 0.0) {
            m_LastTime = nowSeconds;
            return 0.0;
        }

        double frameDelta = nowSeconds - m_LastTime;
        m_LastTime = nowSeconds;
        frameDelta = std::clamp(frameDelta, 0.0, m_MaxFrame);
        m_Accumulator += frameDelta;
        return frameDelta;
    }

    bool GameTime::ConsumeStep() {
        if (m_Accumulator < m_FixedDelta) {
            return false;
        }
        m_Accumulator -= m_FixedDelta;
        return true;
    }

    double GameTime::GetAlpha() const {
        return std::clamp(m_Accumulator / m_FixedDelta, 0.0, 1.0);
    }

}

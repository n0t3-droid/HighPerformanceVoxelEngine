#pragma once

namespace Engine {

    class GameTime {
    public:
        explicit GameTime(double fixedDeltaSeconds = 1.0 / 120.0, double maxFrameSeconds = 0.1);

        double BeginFrame(double nowSeconds);
        bool ConsumeStep();

        double GetFixedDelta() const { return m_FixedDelta; }
        double GetAlpha() const;
        double GetAccumulator() const { return m_Accumulator; }

    private:
        double m_FixedDelta = 1.0 / 120.0;
        double m_MaxFrame = 0.1;
        double m_LastTime = -1.0;
        double m_Accumulator = 0.0;
    };

}

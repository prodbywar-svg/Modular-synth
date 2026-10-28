#pragma once

#include "Params.h"

#include <cstdint>

#if ! MS_NO_JUCE
 #include <juce_core/juce_core.h>
#endif

namespace ms
{
// Recorded effect moves for one effects rack: for each pattern and each effect
// control, a value on any of the 16 steps. While a pattern plays the control
// glides from one recorded step to the next, round the bar, so it never jumps.
// Steps with nothing recorded are passed through on the way.
class FxAutomation
{
public:
    FxAutomation() { clear(); }

    // value < 0 removes the point.
    void set (int pattern, int param, int step, float value);
    // -1 = every pattern / every control.
    void clear (int pattern = -1, int param = -1);

    bool has (int pattern, int param) const;
    bool any (int pattern) const;
    bool anyAtAll() const;
    // The recorded value at a step, or -1.
    float point (int pattern, int param, int step) const;

    // The control's value at a position in the bar (0 .. 16 steps).
    float valueAt (int pattern, int param, double position) const;
    // base with every automated control of this pattern replaced.
    FxParams apply (const FxParams& base, int pattern, double position) const;

#if ! MS_NO_JUCE
    // Only the recorded points: [[pattern, control, step, value], ...]
    juce::var toVar() const;
    void fromVar (const juce::var&);
#endif

private:
    static constexpr std::uint16_t kUnset = 0xffff;
    static constexpr float kScale = 65534.0f;
    std::array<std::array<std::array<std::uint16_t, kNumSteps>, kNumFxParams>, kNumPatterns> points {};
};

// Index of a point in SetFxAuto commands, and back.
inline int fxAutoIndex (int pattern, int param, int step) { return (pattern * kNumFxParams + param) * kNumSteps + step; }

// Applies SetFxAuto (index = fxAutoIndex, value < 0 removes) or ClearFxAuto
// (index = pattern or -1 for all, value = control or -1 for all) to one rack.
struct Command;
void applyFxAuto (FxAutomation&, const Command&);
} // namespace ms

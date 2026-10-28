#include "Automation.h"

#include "ProjectState.h"

#include <algorithm>
#include <cmath>

namespace ms
{
namespace
{
bool validPattern (int p) { return p >= 0 && p < kNumPatterns; }
bool validParam (int i) { return i >= 0 && i < kNumFxParams; }
} // namespace

void FxAutomation::set (int pattern, int param, int step, float value)
{
    if (! validPattern (pattern) || ! validParam (param) || step < 0 || step >= kNumSteps)
        return;
    points[(size_t) pattern][(size_t) param][(size_t) step] =
        value < 0.0f ? kUnset : (std::uint16_t) std::lround (std::clamp (value, 0.0f, 1.0f) * kScale);
}

void FxAutomation::clear (int pattern, int param)
{
    for (int p = 0; p < kNumPatterns; ++p)
        for (int i = 0; i < kNumFxParams; ++i)
            if ((pattern < 0 || p == pattern) && (param < 0 || i == param))
                points[(size_t) p][(size_t) i].fill (kUnset);
}

bool FxAutomation::has (int pattern, int param) const
{
    if (! validPattern (pattern) || ! validParam (param))
        return false;
    for (auto v : points[(size_t) pattern][(size_t) param])
        if (v != kUnset)
            return true;
    return false;
}

bool FxAutomation::any (int pattern) const
{
    for (int i = 0; i < kNumFxParams; ++i)
        if (has (pattern, i))
            return true;
    return false;
}

bool FxAutomation::anyAtAll() const
{
    for (int p = 0; p < kNumPatterns; ++p)
        if (any (p))
            return true;
    return false;
}

float FxAutomation::point (int pattern, int param, int step) const
{
    if (! validPattern (pattern) || ! validParam (param) || step < 0 || step >= kNumSteps)
        return -1.0f;
    const auto v = points[(size_t) pattern][(size_t) param][(size_t) step];
    return v == kUnset ? -1.0f : (float) v / kScale;
}

float FxAutomation::valueAt (int pattern, int param, double position) const
{
    const auto& lane = points[(size_t) pattern][(size_t) param];
    position = std::fmod (std::fmod (position, (double) kNumSteps) + kNumSteps, (double) kNumSteps);
    const int here = std::min (kNumSteps - 1, (int) position);

    // the last recorded step at or before here, and the next one after it
    int back = 0, ahead = 1;
    while (back < kNumSteps && lane[(size_t) ((here - back + kNumSteps) % kNumSteps)] == kUnset)
        ++back;
    if (back == kNumSteps)
        return -1.0f;
    while (lane[(size_t) ((here + ahead) % kNumSteps)] == kUnset)
        ++ahead; // stops at the same point a bar later at the latest
    const int before = here - back, after = here + ahead;

    const float a = (float) lane[(size_t) ((before + kNumSteps) % kNumSteps)] / kScale;
    const float b = (float) lane[(size_t) (after % kNumSteps)] / kScale;
    if (after - before >= kNumSteps) // just one point: it holds
        return a;
    const double t = (position - (double) before) / (double) (after - before);
    return a + (b - a) * (float) std::clamp (t, 0.0, 1.0);
}

FxParams FxAutomation::apply (const FxParams& base, int pattern, double position) const
{
    auto out = base;
    if (! validPattern (pattern))
        return out;
    for (int i = 0; i < kNumFxParams; ++i)
        if (has (pattern, i))
            out[(size_t) i] = valueAt (pattern, i, position);
    return out;
}

#if ! MS_NO_JUCE
juce::var FxAutomation::toVar() const
{
    juce::Array<juce::var> a;
    for (int p = 0; p < kNumPatterns; ++p)
        for (int i = 0; i < kNumFxParams; ++i)
            for (int s = 0; s < kNumSteps; ++s)
                if (const float v = point (p, i, s); v >= 0.0f)
                    a.add (juce::Array<juce::var> { p, juce::String (fxParamName (i)), s, v });
    return a;
}

void FxAutomation::fromVar (const juce::var& v)
{
    clear();
    if (auto* a = v.getArray())
        for (const auto& e : *a)
            if (auto* f = e.getArray(); f != nullptr && f->size() == 4)
            {
                // controls are saved by name, so the file survives new ones being added
                int param = -1;
                for (int i = 0; i < kNumFxParams; ++i)
                    if ((*f)[1].toString() == fxParamName (i))
                        param = i;
                set ((int) (*f)[0], param, (int) (*f)[2], (float) (*f)[3]);
            }
}
#endif

void applyFxAuto (FxAutomation& a, const Command& c)
{
    if (c.type == CommandType::SetFxAuto && c.index >= 0)
    {
        const int step = c.index % kNumSteps, rest = c.index / kNumSteps;
        a.set (rest / kNumFxParams, rest % kNumFxParams, step, c.value);
    }
    else if (c.type == CommandType::ClearFxAuto)
    {
        a.clear (c.index, (int) std::lround (c.value));
    }
}
} // namespace ms

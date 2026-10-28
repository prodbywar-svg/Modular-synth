#include "BassState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ms
{
namespace
{
float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

enum class Curve
{
    Lin,
    Exp
};

struct Spec
{
    const char* name;
    Curve curve;
    float lo, hi;
    const char* unit;
};

// Every sound has the same controls except the first, its TONE.
const Spec kToneSpecs[kNumBassSounds] = {
    { "HARMONICS", Curve::Lin, 0.0f, 1.0f, "pct" },
    { "WIDTH",     Curve::Lin, 0.05f, 0.5f, "pct" },
    { "SUB OSC",   Curve::Lin, 0.0f, 1.0f, "pct" },
    { "WAVE",      Curve::Lin, 0.0f, 1.0f, "wave" },
    { "INDEX",     Curve::Lin, 0.0f, 8.0f, "x" },
    { "DETUNE",    Curve::Lin, 0.0f, 50.0f, "ct" },
    { "CRUSH",     Curve::Exp, 1.0f, 32.0f, "x" },
    { "DROP",      Curve::Lin, 0.0f, 4.0f, "oct" },
};

const Spec kCommonSpecs[kNumBassParams] = {
    { nullptr,   Curve::Lin, 0.0f, 1.0f, "" }, // tone, above
    { "CUTOFF",  Curve::Exp, 30.0f, 12000.0f, "Hz" },
    { "RESO",    Curve::Lin, 0.5f, 12.0f, "Q" },
    { "ENV AMT", Curve::Lin, 0.0f, 6.0f, "oct" },
    { "F DECAY", Curve::Exp, 10.0f, 3000.0f, "ms" },
    { "DRIVE",   Curve::Exp, 1.0f, 20.0f, "x" },
    { "GLIDE",   Curve::Lin, 0.0f, 500.0f, "glide" },
    { "ATTACK",  Curve::Exp, 0.5f, 2000.0f, "ms" },
    { "DECAY",   Curve::Exp, 5.0f, 4000.0f, "ms" },
    { "SUSTAIN", Curve::Lin, 0.0f, 1.0f, "pct" },
    { "RELEASE", Curve::Exp, 5.0f, 4000.0f, "ms" },
};

const Spec& spec (BassSound s, int index)
{
    if (index == bpTone)
        return kToneSpecs[std::clamp ((int) s, 0, kNumBassSounds - 1)];
    return kCommonSpecs[std::clamp (index, 0, kNumBassParams - 1)];
}

float toReal (const Spec& s, float v)
{
    v = clamp01 (v);
    return s.curve == Curve::Exp ? s.lo * std::pow (s.hi / s.lo, v) : s.lo + (s.hi - s.lo) * v;
}

float toNorm (const Spec& s, float x)
{
    if (s.curve == Curve::Exp)
        return clamp01 (std::log (x / s.lo) / std::log (s.hi / s.lo));
    return clamp01 ((x - s.lo) / (s.hi - s.lo));
}

// Starting points in real units: TONE, CUTOFF Hz, RESO Q, ENV AMT oct,
// F DECAY ms, DRIVE x, GLIDE ms, ATTACK ms, DECAY ms, SUSTAIN 0..1, RELEASE ms.
const float kDefaults[kNumBassSounds][kNumBassParams] = {
    { 0.15f, 2000.0f, 0.7f, 0.5f, 200.0f, 1.5f, 0.0f,  2.0f, 300.0f, 0.8f, 80.0f },  // SUB
    { 0.25f, 1500.0f, 1.5f, 2.0f, 250.0f, 2.0f, 0.0f,  1.0f, 250.0f, 0.6f, 60.0f },  // PULSE
    { 0.4f,  900.0f,  2.0f, 2.5f, 300.0f, 2.0f, 0.0f,  1.0f, 400.0f, 0.7f, 80.0f },  // SAW
    { 0.2f,  300.0f,  9.0f, 4.0f, 180.0f, 4.0f, 60.0f, 0.5f, 300.0f, 0.5f, 40.0f },  // ACID
    { 3.0f,  4000.0f, 1.0f, 1.0f, 400.0f, 3.0f, 0.0f,  1.0f, 500.0f, 0.6f, 100.0f }, // FM
    { 18.0f, 1200.0f, 1.5f, 1.5f, 600.0f, 2.5f, 30.0f, 5.0f, 800.0f, 0.8f, 150.0f }, // REESE
    { 8.0f,  6000.0f, 0.7f, 1.0f, 100.0f, 6.0f, 0.0f,  0.5f, 150.0f, 0.3f, 30.0f },  // 1-BIT
    { 2.5f,  3000.0f, 0.7f, 0.0f, 100.0f, 3.0f, 0.0f,  0.5f, 900.0f, 0.0f, 200.0f }, // BOOM
};

const char* const kSoundNames[kNumBassSounds] = { "SUB", "PULSE", "SAW", "ACID", "FM", "REESE", "1-BIT", "BOOM" };

struct Scale
{
    const char* name;
    std::uint16_t mask; // bit n = n semitones above the key
};

constexpr std::uint16_t notes (std::initializer_list<int> degrees)
{
    std::uint16_t m = 0;
    for (int d : degrees)
        m = (std::uint16_t) (m | (1u << d));
    return m;
}

const Scale kScales[kNumScales] = {
    { "CHROMATIC", 0x0fff },
    { "MAJOR", notes ({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "MINOR", notes ({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "DORIAN", notes ({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "PHRYGIAN", notes ({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "LYDIAN", notes ({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "MIXOLYDIAN", notes ({ 0, 2, 4, 5, 7, 9, 10 }) },
    { "LOCRIAN", notes ({ 0, 1, 3, 5, 6, 8, 10 }) },
    { "HARM MINOR", notes ({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "MEL MINOR", notes ({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "PENT MAJOR", notes ({ 0, 2, 4, 7, 9 }) },
    { "PENT MINOR", notes ({ 0, 3, 5, 7, 10 }) },
    { "BLUES", notes ({ 0, 3, 5, 6, 7, 10 }) },
};

const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

int clampNote (int n) { return std::clamp (n, kBassLowNote, kBassHighNote); }

void setSound (BassRow& r, BassSound s)
{
    r.sound = s;
    r.params = defaultBassParams (s);
}

// Writes notes into a pattern: { step, note, length }.
struct Hit
{
    int step, note, length;
};

void writeLine (BassRow& r, int pattern, std::initializer_list<Hit> hits, float velocity = 1.0f)
{
    for (const auto& h : hits)
    {
        r.velocity[(size_t) pattern][(size_t) h.step] = velocity;
        r.notes[(size_t) pattern][(size_t) h.step] = (std::int8_t) h.note;
        r.lengths[(size_t) pattern][(size_t) h.step] = (std::uint8_t) h.length;
    }
}

void resnapAll (BassState& b)
{
    for (auto& r : b.rows)
        for (auto& pattern : r.notes)
            for (auto& n : pattern)
                n = (std::int8_t) snapToScale (n, b.key, b.scale);
}

#if ! MS_NO_JUCE
juce::var floats (const float* v, int n)
{
    juce::Array<juce::var> a;
    for (int i = 0; i < n; ++i)
        a.add (v[i]);
    return a;
}

template <typename T>
juce::var ints (const T& values)
{
    juce::Array<juce::var> a;
    for (auto v : values)
        a.add ((int) v);
    return a;
}

void readFloats (const juce::var& v, float* out, int n)
{
    if (auto* a = v.getArray())
        for (int i = 0; i < juce::jmin (n, a->size()); ++i)
            out[i] = clamp01 ((float) (*a)[i]);
}
#endif
} // namespace

BassState makeDefaultBass()
{
    BassState b;
    const BassSound order[kBassRows] = { BassSound::Sub, BassSound::Pulse, BassSound::Saw, BassSound::Acid,
                                         BassSound::Fm, BassSound::Reese, BassSound::OneBit, BassSound::Boom };
    for (int r = 0; r < kBassRows; ++r)
    {
        auto& row = b.rows[(size_t) r];
        setSound (row, order[r]);
        for (auto& pattern : row.notes)
            pattern.fill (36); // C2: where a new note starts
        for (auto& pattern : row.lengths)
            pattern.fill (1);
    }

    // P1 in C minor: a sub line that the acid line cuts through (they share a
    // cable), and an 808 boom on the one.
    writeLine (b.rows[0], 0, { { 0, 36, 3 }, { 8, 36, 2 }, { 11, 31, 1 }, { 14, 34, 2 } });
    writeLine (b.rows[3], 0, { { 2, 48, 1 }, { 3, 36, 1 }, { 6, 39, 1 }, { 7, 48, 1 }, { 10, 43, 1 }, { 13, 46, 1 } }, 0.8f);
    b.rows[0].cable = 3;
    // P2: a long reese with FM stabs
    writeLine (b.rows[5], 1, { { 0, 36, 4 }, { 4, 36, 2 }, { 8, 39, 4 }, { 12, 34, 4 } });
    writeLine (b.rows[4], 1, { { 6, 48, 1 }, { 14, 51, 1 } }, 0.7f);
    // P3: half-time boom and 1-bit
    writeLine (b.rows[7], 2, { { 0, 31, 4 }, { 10, 31, 2 } });
    writeLine (b.rows[6], 2, { { 3, 43, 1 }, { 7, 46, 1 }, { 15, 48, 1 } }, 0.6f);
    return b;
}

// ---- sounds -------------------------------------------------------------------

const char* bassSoundName (BassSound s) { return kSoundNames[std::clamp ((int) s, 0, kNumBassSounds - 1)]; }

BassSound bassSoundFromName (const char* name)
{
    for (int i = 0; i < kNumBassSounds; ++i)
        if (std::strcmp (name, kSoundNames[i]) == 0)
            return (BassSound) i;
    return BassSound::Sub;
}

const char* bassParamName (BassSound s, int index) { return spec (s, index).name; }

BassParams defaultBassParams (BassSound s)
{
    BassParams p {};
    const int si = std::clamp ((int) s, 0, kNumBassSounds - 1);
    for (int i = 0; i < kNumBassParams; ++i)
        p[(size_t) i] = toNorm (spec (s, i), kDefaults[si][i]);
    return p;
}

float bassParamValue (BassSound s, int index, float v) { return toReal (spec (s, index), v); }

void formatBassParam (BassSound s, int index, float v, char* out, int outSize)
{
    const auto& sp = spec (s, index);
    const float x = toReal (sp, v);
    const auto n = (size_t) outSize;
    const char* u = sp.unit;
    if (std::strcmp (u, "Hz") == 0)
        std::snprintf (out, n, x >= 1000.0f ? "%.2f kHz" : "%.0f Hz", x >= 1000.0f ? x / 1000.0f : x);
    else if (std::strcmp (u, "ms") == 0)
        std::snprintf (out, n, x >= 1000.0f ? "%.2f s" : x < 10.0f ? "%.1f ms" : "%.0f ms", x >= 1000.0f ? x / 1000.0f : x);
    else if (std::strcmp (u, "glide") == 0)
        std::snprintf (out, n, x < 1.0f ? "OFF" : "%.0f ms", x);
    else if (std::strcmp (u, "Q") == 0)
        std::snprintf (out, n, "Q %.2f", x);
    else if (std::strcmp (u, "oct") == 0)
        std::snprintf (out, n, "%.2f oct", x);
    else if (std::strcmp (u, "x") == 0)
        std::snprintf (out, n, "%.2fx", x);
    else if (std::strcmp (u, "ct") == 0)
        std::snprintf (out, n, "%.0f ct", x);
    else if (std::strcmp (u, "wave") == 0)
        std::snprintf (out, n, "SAW %d / SQR %d", (int) std::lround ((1.0f - x) * 100.0f), (int) std::lround (x * 100.0f));
    else if (std::strcmp (u, "pct") == 0)
        std::snprintf (out, n, "%.0f %%", x * 100.0f);
    else
        std::snprintf (out, n, "%.3f", x);
}

// ---- notes and scales -----------------------------------------------------------

const char* scaleName (int scale) { return kScales[std::clamp (scale, 0, kNumScales - 1)].name; }

int scaleFromName (const char* name)
{
    for (int i = 0; i < kNumScales; ++i)
        if (std::strcmp (name, kScales[i].name) == 0)
            return i;
    return 2;
}

const char* keyName (int key) { return kKeyNames[((key % 12) + 12) % 12]; }

void noteName (int midiNote, char* out, int outSize)
{
    std::snprintf (out, (size_t) outSize, "%s%d", keyName (midiNote), midiNote / 12 - 1);
}

float noteHz (float midiNote) { return 440.0f * std::pow (2.0f, (midiNote - 69.0f) / 12.0f); }

bool noteInScale (int midiNote, int key, int scale)
{
    const int degree = (((midiNote - key) % 12) + 12) % 12;
    return (kScales[std::clamp (scale, 0, kNumScales - 1)].mask >> degree) & 1u;
}

int snapToScale (int midiNote, int key, int scale)
{
    midiNote = clampNote (midiNote);
    for (int d = 0; d < 12; ++d)
    {
        if (midiNote - d >= kBassLowNote && noteInScale (midiNote - d, key, scale))
            return midiNote - d;
        if (midiNote + d <= kBassHighNote && noteInScale (midiNote + d, key, scale))
            return midiNote + d;
    }
    return midiNote;
}

int stepInScale (int midiNote, int key, int scale, int steps)
{
    int n = snapToScale (midiNote, key, scale);
    const int dir = steps > 0 ? 1 : -1;
    for (int i = 0; i < std::abs (steps); ++i)
    {
        int next = n + dir;
        while (next >= kBassLowNote && next <= kBassHighNote && ! noteInScale (next, key, scale))
            next += dir;
        if (next < kBassLowNote || next > kBassHighNote)
            break;
        n = next;
    }
    return n;
}

std::array<int, kBassRows> cutGroups (const BassState& b)
{
    std::array<int, kBassRows> group {};
    for (int r = 0; r < kBassRows; ++r)
        group[(size_t) r] = r;
    auto find = [&group] (int r) {
        while (group[(size_t) r] != r)
            r = group[(size_t) r];
        return r;
    };
    for (int r = 0; r < kBassRows; ++r)
    {
        const int to = b.rows[(size_t) r].cable;
        if (to >= 0 && to < kBassRows && to != r)
        {
            const int a = find (r), c = find (to);
            group[(size_t) std::max (a, c)] = std::min (a, c);
        }
    }
    for (int r = 0; r < kBassRows; ++r)
        group[(size_t) r] = find (r);
    return group;
}

void applyToBass (BassState& b, const Command& c)
{
    const bool validRow = c.track >= 0 && c.track < kBassRows;
    auto* row = validRow ? &b.rows[(size_t) c.track] : nullptr;
    const int pattern = c.index / kNumSteps, step = c.index % kNumSteps;
    const bool validStep = c.index >= 0 && pattern < kNumPatterns;

    switch (c.type)
    {
        case CommandType::SetVoiceType:
            if (row != nullptr && c.index >= 0 && c.index < kNumBassSounds)
                setSound (*row, (BassSound) c.index);
            break;
        case CommandType::SetVoiceParam:
            if (row != nullptr && c.index >= 0 && c.index < kNumBassParams)
                row->params[(size_t) c.index] = clamp01 (c.value);
            break;
        case CommandType::SetLevel:
            if (row != nullptr) row->level = clamp01 (c.value);
            break;
        case CommandType::SetPan:
            if (row != nullptr) row->pan = clamp01 (c.value);
            break;
        case CommandType::SetMute:
            if (row != nullptr) row->mute = c.value > 0.5f;
            break;
        case CommandType::SetSolo:
            if (row != nullptr) row->solo = c.value > 0.5f;
            break;
        case CommandType::SetFxParam:
            if (c.index >= 0 && c.index < kNumFxParams)
            {
                if (row != nullptr)
                    row->fx[(size_t) c.index] = clamp01 (c.value);
                else if (c.track == -1)
                    b.masterFx[(size_t) c.index] = clamp01 (c.value);
            }
            break;
        case CommandType::SetFxAuto:
        case CommandType::ClearFxAuto:
            if (row != nullptr)
                applyFxAuto (row->fxAuto, c);
            else if (c.track == -1)
                applyFxAuto (b.masterFxAuto, c);
            else if (c.track == kAllRacks)
            {
                applyFxAuto (b.masterFxAuto, c);
                for (auto& r : b.rows)
                    applyFxAuto (r.fxAuto, c);
            }
            break;
        case CommandType::SetStep:
            if (row != nullptr && validStep)
                row->velocity[(size_t) pattern][(size_t) step] = clamp01 (c.value);
            break;
        case CommandType::SetNote:
            if (row != nullptr && validStep)
                row->notes[(size_t) pattern][(size_t) step] = (std::int8_t) clampNote ((int) std::lround (c.value));
            break;
        case CommandType::SetLength:
            if (row != nullptr && validStep)
                row->lengths[(size_t) pattern][(size_t) step] = (std::uint8_t) std::clamp ((int) std::lround (c.value), 1, kMaxStepLength);
            break;
        case CommandType::SetCable:
            if (row != nullptr)
                row->cable = (c.index >= 0 && c.index < kBassRows && c.index != c.track) ? c.index : -1;
            break;
        case CommandType::SetKey:
            b.key = ((c.index % 12) + 12) % 12;
            resnapAll (b);
            break;
        case CommandType::SetScale:
            if (c.index >= 0 && c.index < kNumScales)
            {
                b.scale = c.index;
                resnapAll (b);
            }
            break;
        case CommandType::SelectPattern:
            if (c.index < 0 || c.index >= kNumPatterns)
                break;
            if (c.value > 0.5f)
                b.queuedPattern = c.index == b.pattern ? -1 : c.index;
            else
            {
                b.pattern = c.index;
                b.queuedPattern = -1;
            }
            break;
        case CommandType::SetBpm:
            b.bpm = std::clamp (c.value, kMinBpm, kMaxBpm);
            break;
        case CommandType::SetSwing:
            b.swing = clamp01 (c.value);
            break;
        case CommandType::SetMasterLevel:
            b.masterLevel = clamp01 (c.value);
            break;
        case CommandType::Trigger:
        case CommandType::NoteOff:
        case CommandType::SetCharacter:
        case CommandType::SetSample:
        case CommandType::Play:
        case CommandType::Stop:
        case CommandType::RecordStart:
        case CommandType::RecordStop:
        case CommandType::AddTool:
        case CommandType::RemoveTool:
        case CommandType::SetToolMute:
        case CommandType::SetToolSolo:
        case CommandType::SetLoopInput:
        case CommandType::SetLoopMonitor:
        case CommandType::SetInputGain:
        case CommandType::SetLoopBars:
        case CommandType::SetSlice:
        case CommandType::SetLoopAudio:
        case CommandType::RecordLoop:
        case CommandType::CancelLoopRecord:
        case CommandType::SetCountIn:
        case CommandType::SetMonitorDry:
            break;
    }
}

#if ! MS_NO_JUCE
juce::var bassToVar (const BassState& b)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("pattern", b.pattern);
    root->setProperty ("key", keyName (b.key));
    root->setProperty ("scale", scaleName (b.scale));
    root->setProperty ("masterLevel", b.masterLevel);
    root->setProperty ("masterFx", fxToVar (b.masterFx));
    root->setProperty ("masterFxAuto", b.masterFxAuto.toVar());

    juce::Array<juce::var> rows;
    for (const auto& r : b.rows)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("sound", bassSoundName (r.sound));
        o->setProperty ("params", floats (r.params.data(), kNumBassParams));
        o->setProperty ("level", r.level);
        o->setProperty ("pan", r.pan);
        o->setProperty ("mute", r.mute);
        o->setProperty ("solo", r.solo);
        o->setProperty ("fx", fxToVar (r.fx));
        o->setProperty ("fxAuto", r.fxAuto.toVar());
        o->setProperty ("cable", r.cable);
        juce::Array<juce::var> patterns;
        for (int p = 0; p < kNumPatterns; ++p)
        {
            auto po = std::make_unique<juce::DynamicObject>();
            po->setProperty ("velocity", floats (r.velocity[(size_t) p].data(), kNumSteps));
            po->setProperty ("notes", ints (r.notes[(size_t) p]));
            po->setProperty ("lengths", ints (r.lengths[(size_t) p]));
            patterns.add (juce::var (po.release()));
        }
        o->setProperty ("patterns", patterns);
        rows.add (juce::var (o.release()));
    }
    root->setProperty ("rows", rows);
    return juce::var (root.release());
}

bool bassFromVar (const juce::var& v, BassState& out)
{
    if (! v.isObject())
        return false;

    BassState b = makeDefaultBass();
    b.pattern = juce::jlimit (0, kNumPatterns - 1, (int) v.getProperty ("pattern", 0));
    b.queuedPattern = -1;
    const auto key = v.getProperty ("key", "C").toString();
    for (int k = 0; k < 12; ++k)
        if (key == keyName (k))
            b.key = k;
    b.scale = scaleFromName (v.getProperty ("scale", "MINOR").toString().toRawUTF8());
    b.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", b.masterLevel));
    fxFromVar (v.getProperty ("masterFx", {}), b.masterFx);
    b.masterFxAuto.fromVar (v.getProperty ("masterFxAuto", {}));

    if (auto* rows = v.getProperty ("rows", {}).getArray())
    {
        for (int i = 0; i < juce::jmin (kBassRows, rows->size()); ++i)
        {
            const auto& o = (*rows)[i];
            auto& r = b.rows[(size_t) i];
            setSound (r, bassSoundFromName (o.getProperty ("sound", "SUB").toString().toRawUTF8()));
            readFloats (o.getProperty ("params", {}), r.params.data(), kNumBassParams);
            r.level = clamp01 ((float) o.getProperty ("level", r.level));
            r.pan = clamp01 ((float) o.getProperty ("pan", r.pan));
            r.mute = (bool) o.getProperty ("mute", false);
            r.solo = (bool) o.getProperty ("solo", false);
            r.fx = defaultFxParams();
            fxFromVar (o.getProperty ("fx", {}), r.fx);
            r.fxAuto.fromVar (o.getProperty ("fxAuto", {}));
            const int cable = (int) o.getProperty ("cable", -1);
            r.cable = cable >= 0 && cable < kBassRows && cable != i ? cable : -1;

            for (int p = 0; p < kNumPatterns; ++p)
            {
                r.velocity[(size_t) p].fill (0.0f);
                r.notes[(size_t) p].fill (36);
                r.lengths[(size_t) p].fill (1);
            }
            if (auto* patterns = o.getProperty ("patterns", {}).getArray())
            {
                for (int p = 0; p < juce::jmin (kNumPatterns, patterns->size()); ++p)
                {
                    const auto& po = (*patterns)[p];
                    readFloats (po.getProperty ("velocity", {}), r.velocity[(size_t) p].data(), kNumSteps);
                    if (auto* n = po.getProperty ("notes", {}).getArray())
                        for (int s = 0; s < juce::jmin (kNumSteps, n->size()); ++s)
                            r.notes[(size_t) p][(size_t) s] = (std::int8_t) clampNote ((int) (*n)[s]);
                    if (auto* l = po.getProperty ("lengths", {}).getArray())
                        for (int s = 0; s < juce::jmin (kNumSteps, l->size()); ++s)
                            r.lengths[(size_t) p][(size_t) s] = (std::uint8_t) juce::jlimit (1, kMaxStepLength, (int) (*l)[s]);
                }
            }
        }
    }

    out = b;
    return true;
}
#endif
} // namespace ms

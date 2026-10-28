// Native twin of the browser engine, for web/tests/parity.mjs: runs a script
// of commands through the same C functions and writes the audio out.
//   native_render <script.f32> <hr16-dir-of-.f32> <out.f32>
// script: [sampleRate, totalSamples, then 6 floats per event:
//          time, type, track, index, value, sampleId]

#include <cstdio>
#include <vector>

extern "C" {
void ms_prepare (double);
void ms_command (int, int, int, float, int);
void ms_process (int);
float* ms_left();
float* ms_right();
void ms_set_hr16 (int, const float*, int, double);
}

static std::vector<float> readFloats (const char* path)
{
    std::vector<float> v;
    if (FILE* f = std::fopen (path, "rb"))
    {
        float x;
        while (std::fread (&x, sizeof x, 1, f) == 1) v.push_back (x);
        std::fclose (f);
    }
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 4) return 2;
    const auto script = readFloats (argv[1]);
    const double sr = script[0];
    const long total = (long) script[1];

    for (int n = 1; n <= 56; ++n)
    {
        char path[512];
        std::snprintf (path, sizeof path, "%s/%02d.f32", argv[2], n);
        auto s = readFloats (path);
        if (s.size() > 1) ms_set_hr16 (n, s.data() + 1, (int) s.size() - 1, s[0]);
    }

    ms_prepare (sr);
    FILE* out = std::fopen (argv[3], "wb");
    size_t e = 2;
    for (long pos = 0; pos < total;)
    {
        while (e + 6 <= script.size() && script[e] <= pos)
        {
            ms_command ((int) script[e + 1], (int) script[e + 2], (int) script[e + 3], script[e + 4], (int) script[e + 5]);
            e += 6;
        }
        const int n = (int) std::min<long> (128, total - pos);
        ms_process (n);
        for (int i = 0; i < n; ++i)
        {
            std::fwrite (ms_left() + i, sizeof (float), 1, out);
            std::fwrite (ms_right() + i, sizeof (float), 1, out);
        }
        pos += n;
    }
    std::fclose (out);
    return 0;
}

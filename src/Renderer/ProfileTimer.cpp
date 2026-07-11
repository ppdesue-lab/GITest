#include "stdsfx.h"
#include "ProfileTimer.h"

#ifdef SHOW_PROFILE

#include "Log.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
struct ProfileSample
{
    const char* Name = nullptr;
    double LastMs = 0.0;
    double TotalMs = 0.0;
    double MaxMs = 0.0;
};

std::vector<ProfileSample> s_FrameSamples;
std::vector<ProfileSample> s_WindowSamples;
std::chrono::steady_clock::time_point s_FrameStart;
uint32_t s_FrameCount = 0;
constexpr uint32_t kLogIntervalFrames = 60;

ProfileSample& FindOrAdd(std::vector<ProfileSample>& samples, const char* name)
{
    for (ProfileSample& sample : samples)
    {
        if (sample.Name == name || strcmp(sample.Name, name) == 0)
            return sample;
    }

    samples.push_back({ name });
    return samples.back();
}
}

void ProfileTimer::BeginFrame()
{
    s_FrameSamples.clear();
    s_FrameStart = std::chrono::steady_clock::now();
}

void ProfileTimer::EndFrame()
{
    const auto now = std::chrono::steady_clock::now();
    AddSample("Frame.Total", std::chrono::duration<double, std::milli>(now - s_FrameStart).count());

    for (const ProfileSample& frameSample : s_FrameSamples)
    {
        ProfileSample& windowSample = FindOrAdd(s_WindowSamples, frameSample.Name);
        windowSample.LastMs = frameSample.LastMs;
        windowSample.TotalMs += frameSample.LastMs;
        windowSample.MaxMs = std::max(windowSample.MaxMs, frameSample.LastMs);
    }

    ++s_FrameCount;
    if (s_FrameCount < kLogIntervalFrames)
        return;

    std::vector<ProfileSample> sorted = s_WindowSamples;
    std::sort(sorted.begin(), sorted.end(), [](const ProfileSample& a, const ProfileSample& b)
    {
        return a.TotalMs > b.TotalMs;
    });

    INFO("[PROFILE] {} frame average", kLogIntervalFrames);
    for (const ProfileSample& sample : sorted)
    {
        const double avgMs = sample.TotalMs / (double)kLogIntervalFrames;
        if (avgMs < 0.01 && sample.MaxMs < 0.05)
            continue;
        INFO("[PROFILE] {:28s} avg={:7.3f} ms max={:7.3f} ms last={:7.3f} ms",
            sample.Name, avgMs, sample.MaxMs, sample.LastMs);
    }

    s_WindowSamples.clear();
    s_FrameCount = 0;
}

void ProfileTimer::AddSample(const char* name, double milliseconds)
{
    ProfileSample& sample = FindOrAdd(s_FrameSamples, name);
    sample.LastMs += milliseconds;
}

ProfileTimer::Scope::Scope(const char* name)
    : m_Name(name), m_Start(std::chrono::steady_clock::now())
{
}

ProfileTimer::Scope::~Scope()
{
    const auto now = std::chrono::steady_clock::now();
    ProfileTimer::AddSample(m_Name, std::chrono::duration<double, std::milli>(now - m_Start).count());
}

#endif

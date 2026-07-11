#pragma once

#ifdef SHOW_PROFILE

#include <chrono>

class ProfileTimer
{
public:
    static void BeginFrame();
    static void EndFrame();
    static void AddSample(const char* name, double milliseconds);

    class Scope
    {
    public:
        explicit Scope(const char* name);
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const char* m_Name;
        std::chrono::steady_clock::time_point m_Start;
    };
};

#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
#define PROFILE_BEGIN_FRAME() ::ProfileTimer::BeginFrame()
#define PROFILE_END_FRAME() ::ProfileTimer::EndFrame()
#define PROFILE_SCOPE(name) ::ProfileTimer::Scope PROFILE_CONCAT(profileScope, __LINE__)(name)

#else

#define PROFILE_BEGIN_FRAME() ((void)0)
#define PROFILE_END_FRAME() ((void)0)
#define PROFILE_SCOPE(name) ((void)0)

#endif

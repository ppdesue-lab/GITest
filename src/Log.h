#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

class Log
{
public:
    static void Init()
    {
        spdlog::set_pattern("%^[%T] %n: %v%$");
        s_CoreLogger = spdlog::stdout_color_mt("KENGINE");
        s_CoreLogger->set_level(spdlog::level::trace);
    }

    inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
};

//macro for easy access
#define TRACE(...)    ::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define INFO(...)     ::Log::GetCoreLogger()->info(__VA_ARGS__)
#define WARN(...)     ::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define ERROR(...)    ::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CRITICAL(...) ::Log::GetCoreLogger()->critical(__VA_ARGS__)


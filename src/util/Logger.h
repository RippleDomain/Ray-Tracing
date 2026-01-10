#pragma once

#include <string>
#include <cstdio>
#include <cstdarg>

namespace logger
{
    enum class Level
    {
        Info,
        Warn,
        Error
    };

    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);
}
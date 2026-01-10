#include "Logger.h"

namespace
{
    void logFormatted(const char* tag, const char* fmt, va_list args)
    {
        std::fputs(tag, stdout);
        std::vfprintf(stdout, fmt, args);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
}

namespace logger
{
    void info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        logFormatted("[I] ", fmt, args);
        va_end(args);
    }

    void warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        logFormatted("[W] ", fmt, args);
        va_end(args);
    }

    void error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        logFormatted("[E] ", fmt, args);
        va_end(args);
    }
}
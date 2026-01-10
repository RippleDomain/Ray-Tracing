#pragma once

#include <chrono>

class Timer
{
public:
    using Clock = std::chrono::high_resolution_clock;

    Timer() : mStart(Clock::now()) {}

    void reset()
    {
        mStart = Clock::now();
    }

    double elapsedSeconds() const
    {
        return std::chrono::duration<double>(Clock::now() - mStart).count();
    }

private:
    Clock::time_point mStart;
};
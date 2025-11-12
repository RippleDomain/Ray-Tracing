#pragma once

#include <chrono>

class Timer
{
public:
    using clock = std::chrono::high_resolution_clock;

    Timer() : m_start(clock::now()) {}

    void reset() { m_start = clock::now(); }

    double elapsedSeconds() const 
    {
        return std::chrono::duration<double>(clock::now() - m_start).count();
    }
private:
    clock::time_point m_start;
};
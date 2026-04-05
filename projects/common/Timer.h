#pragma once

#include <chrono>

class Timer
{
public:
    Timer();

    void   Reset();
    double GetElapsedSeconds() const;

private:
    std::chrono::steady_clock::time_point mStartTime;
};

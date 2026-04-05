#include "Timer.h"

Timer::Timer()
{
    Reset();
}

void Timer::Reset()
{
    mStartTime = std::chrono::steady_clock::now();
}

double Timer::GetElapsedSeconds() const
{
    auto elapsed = std::chrono::steady_clock::now() - mStartTime;
    return std::chrono::duration<double>(elapsed).count();
}

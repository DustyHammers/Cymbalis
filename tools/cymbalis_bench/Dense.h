#pragma once

#include <cmath>
#include <string>

// cymbalis_bench --dense (Dense.cpp): FastEngine's block times in dense playing.
int runDense(int argc, char** argv);

// The CPU and its cores (macOS: also the performance and efficiency cores), for the reports.
std::string machine();

// The hi-hat pedal in both benches: a CC every kPedalStepS, the openness going from closed to
// open and back every kPedalPeriodS (a raised cosine, in the CC's 127 steps), so the choke runs
// and FOOT fires at every close.
constexpr double kPedalStepS = 0.01, kPedalPeriodS = 1.5;
inline double pedalOpenness(double t)
{
    const double o = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * t / kPedalPeriodS);
    return std::round(127.0 * o) / 127.0;
}

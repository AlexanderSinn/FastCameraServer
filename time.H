#pragma once

#include <chrono>

inline
auto get_time() {
    return std::chrono::steady_clock::now();
}

using TimeType = decltype(get_time());

inline
double time_diff_us (const TimeType& start, const TimeType& end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

inline
void sleep_us (double us) {
    auto t1 = get_time();
    while (time_diff_us(t1, get_time()) < us) {}
}

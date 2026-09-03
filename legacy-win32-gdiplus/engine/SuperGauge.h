// engine/SuperGauge.h
// Super meter. 0-100, gained on dealing AND receiving hits; supers are
// locked out below their MeterCost. 1:1 port of Character/SuperGauge.ps1.
#pragma once
#include <algorithm>

namespace kakuge {

struct SuperGauge {
    static constexpr double MaxValue = 100.0;
    double Value = 0.0;

    void Add(double amount) {
        double v = Value + amount;
        Value = std::min(std::max(v, 0.0), MaxValue);
    }
    bool CanSpend(double cost) const { return Value >= cost; }
    bool Spend(double cost) {
        if (!CanSpend(cost)) return false;
        Value -= cost;
        return true;
    }
};

} // namespace kakuge

// =====================================================================
// engine/SuperGauge.h - 超必殺技ゲージ
// =====================================================================
// 0 から 100 まで溜まるゲージです。攻撃を当てても、食らっても溜まります
//（多くの格闘ゲームと同じ仕組み。防戦一方でも逆転の目が残ります）。
// 超必殺技はゲージが技の必要量に足りていないと出せません。
// =====================================================================
#pragma once
#include <algorithm>

namespace kakuge {

struct SuperGauge {
    static constexpr double MaxValue = 100.0;
    double Value = 0.0;

    // 増やす（マイナスを渡せば減らせます）。
    // 0 未満・100 超にはならないよう必ず範囲内に収めます。
    void Add(double amount) {
        double v = Value + amount;
        Value = std::min(std::max(v, 0.0), MaxValue);
    }

    bool CanSpend(double cost) const { return Value >= cost; }

    // 消費する。足りなければ何もせず false を返します
    //（「足りないのに減らしてしまう」事故を防ぐため、
    //   確認と消費を 1 つの関数にまとめています）。
    bool Spend(double cost) {
        if (!CanSpend(cost)) return false;
        Value -= cost;
        return true;
    }
};

} // namespace kakuge

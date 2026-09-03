// =====================================================================
// platform/Camera.cpp - カメラの計算
// =====================================================================
#include "platform/Camera.h"

#include <algorithm>
#include <cmath>

namespace kakuge {

namespace {

// 目標へ近づく速さ（1 秒あたりの割合）。
//
// この値の決め方は、次の 2 つの板挟みです。
//   大きすぎる … カメラが中間点にぴったり張り付き、歩くたびに
//                 背景がガタガタ動いて酔いやすくなる
//   小さすぎる … ダッシュや吹き飛びにカメラが付いてこられず、
//                 キャラクターが画面端に押し付けられて見えなくなる
// 8.0 は「少し滑らかだが、入力には十分ついてくる」あたりです。
// （1/60 秒で目標との差の約 12% を詰め、約 0.12 秒でほぼ到達します）
constexpr double kCameraLerpSpeed = 8.0;

GameCamera g_Camera;

// カメラの中心を、ステージの外が映らない範囲に収める。
//
// これが無いと、片方が画面端に追い詰められたとき、2 人の中間点を
// そのまま中心にするせいで、端の向こうの何もない空間が映ります。
// 本物の格闘ゲームのカメラは、そういうとき中間点の追跡をやめて
// 壁に張り付きます。その動きを再現しています。
// これが「画面端に追い詰めた」という状況を作る仕組みでもあります。
double ClampCameraCenter(double centerX) {
    constexpr double halfVisible = VirtualW / 2.0; // 倍率固定なので常に 192
    double minCenter = StageConstants::StageMinX + halfVisible;
    double maxCenter = StageConstants::StageMaxX - halfVisible;
    // ステージが画面より狭い場合は中央固定（今の設定では起きません）。
    if (minCenter > maxCenter) {
        return (StageConstants::StageMinX + StageConstants::StageMaxX) / 2.0;
    }
    return std::clamp(centerX, minCenter, maxCenter);
}

// デッドゾーンを考慮して、カメラが目指すべき位置を求める。
//
//   中間点がデッドゾーンの中  → 今の位置のまま（カメラは動かない）
//   はみ出した               → はみ出したぶんだけ動く
//
// 「はみ出したぶんだけ」なので、帯の縁に中間点が貼り付く形になり、
// カメラの動き出しが急にならず自然につながります。
double CameraTargetX(double midpoint, double currentCenterX) {
    const double half = StageConstants::CameraDeadZoneWidth / 2.0; // 70
    double diff = midpoint - currentCenterX;
    double target = currentCenterX;
    if (diff > half) target = midpoint - half;
    else if (diff < -half) target = midpoint + half;
    return ClampCameraCenter(target);
}

} // namespace

GameCamera& GetCamera() { return g_Camera; }

double CameraDrawX() { return std::round(g_Camera.CenterX); }

void UpdateCamera(double p1x, double p2x, double dt) {
    double midpoint = (p1x + p2x) / 2.0;
    double target = CameraTargetX(midpoint, g_Camera.CenterX);
    // 目標との差の一定割合ずつ近づける（線形補間）。
    // 毎フレーム差が縮むので、なめらかに減速しながら到達します。
    double t = std::clamp(kCameraLerpSpeed * dt, 0.0, 1.0);
    g_Camera.CenterX += (target - g_Camera.CenterX) * t;
}

void ResetCamera(double p1x, double p2x) {
    // 試合開始時はデッドゾーンを考えず、中間点にまっすぐ合わせます。
    g_Camera.CenterX = ClampCameraCenter((p1x + p2x) / 2.0);
}

} // namespace kakuge

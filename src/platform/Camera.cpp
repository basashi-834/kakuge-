// =====================================================================
// platform/Camera.cpp - カメラの計算
// =====================================================================
#include "platform/Camera.h"

#include <algorithm>
#include <cmath>

namespace kakuge {

namespace {

// 2 人の間隔に加えて、左右にどれだけ余白を見せるか。
// 232 という値は「試合開始時の間隔（152）＋この余白 = 384（画面幅）」、
// つまり開始直後がちょうど拡大率 1.0 になるように決めています。
// 開始位置が基準の大きさになるので、寄ったか引いたかが分かりやすい。
constexpr double kCameraPaddingWorld = 232.0;

// もっとも引いたときの拡大率。
// 2 人が両端（640 離れている）まで離れても、
// 384 / 0.49 ≒ 784 > 640 なので必ず両方が画面に入ります。
// これ以上引くと、ステージの外の何もない場所まで映ってしまいます。
constexpr double kCameraMinZoom = 0.49;

// もっとも寄ったときの拡大率。
// キャラクターの身長は拡大率 1.0 で 88 ピクセル。1.05 倍でも 92 ピクセルで、
// 画面上部の体力ゲージ（約 30 ピクセル）に頭がぶつかりません。
constexpr double kCameraMaxZoom = 1.05;

// 目標へ近づく速さ（1 秒あたりの割合）。
// 大きすぎるとカクッと切り替わって酔いやすく、
// 小さすぎるとダッシュにカメラが付いてこられません。
constexpr double kCameraLerpSpeed = 6.0;

GameCamera g_Camera;

// 2 人の間隔から、あるべき拡大率を求める。
double CameraTargetZoom(double distance) {
    double desiredWidth = distance + kCameraPaddingWorld;
    double zoom = (desiredWidth > 1.0) ? (VirtualW / desiredWidth) : kCameraMaxZoom;
    return std::clamp(zoom, kCameraMinZoom, kCameraMaxZoom);
}

// カメラの中心を、ステージの外が映らない範囲に収める。
//
// これが無いと、片方が画面端に追い詰められたとき、2 人の中間点を
// そのまま中心にするせいで、端の向こうの何もない空間が半分映ります。
// 本物の格闘ゲームのカメラは、そういうとき中間点の追跡をやめて
// 壁に張り付きます。その動きを再現しています。
double ClampCameraCenter(double centerX, double zoom) {
    double halfVisible = VirtualW / (2.0 * zoom);
    double minCenter = StageConstants::StageMinX + halfVisible;
    double maxCenter = StageConstants::StageMaxX - halfVisible;
    // 画面のほうがステージより広い場合（最小倍率のとき）は中央固定。
    if (minCenter > maxCenter) {
        return (StageConstants::StageMinX + StageConstants::StageMaxX) / 2.0;
    }
    return std::clamp(centerX, minCenter, maxCenter);
}

} // namespace

GameCamera& GetCamera() { return g_Camera; }

void UpdateCamera(double p1x, double p2x, double dt) {
    double targetZoom = CameraTargetZoom(std::abs(p1x - p2x));
    double targetCenter = ClampCameraCenter((p1x + p2x) / 2.0, targetZoom);
    // 目標との差の一定割合ずつ近づける（線形補間）。
    // 毎フレーム差が縮むので、なめらかに減速しながら到達します。
    double t = std::clamp(kCameraLerpSpeed * dt, 0.0, 1.0);
    g_Camera.CenterX += (targetCenter - g_Camera.CenterX) * t;
    g_Camera.Zoom += (targetZoom - g_Camera.Zoom) * t;
}

void ResetCamera(double p1x, double p2x) {
    g_Camera.Zoom = CameraTargetZoom(std::abs(p1x - p2x));
    g_Camera.CenterX = ClampCameraCenter((p1x + p2x) / 2.0, g_Camera.Zoom);
}

} // namespace kakuge

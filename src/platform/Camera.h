// =====================================================================
// platform/Camera.h - カメラ（自動ズーム）とワールド座標→画面座標の変換
// =====================================================================
// ゲーム内部の座標（ワールド座標）は「画面中央が X=0、地面が Y=0」。
// 一方、描画に使う座標（画面座標）は「左上が (0,0)、右下が (384,224)」。
// この 2 つを変換するのがカメラの役割です。
//
// カメラは 2 人の中間点を追いかけ、2 人が近いときは寄り、
// 離れているときは引きます。実際の格闘ゲームと同じ動きです。
// ただし次の 2 つを守ります。
//   - ステージの外（何もない場所）は映さない
//   - 倍率は急に変えず、なめらかに近づける
// =====================================================================
#pragma once
#include "engine/Constants.h"
#include "platform/Renderer.h"

namespace kakuge {

// ワールド座標の原点が、画面座標のどこに来るか。
constexpr double OriginX = VirtualW / 2.0;      // 画面の横中央
constexpr double OriginY = GameSpec::GroundY;   // 地面の線（上から 189px）

struct GameCamera {
    double CenterX = 0.0; // 今どのワールド座標を画面中央に映しているか
    double Zoom = 1.0;    // 拡大率（1.0 が基準）
};

// カメラは 1 つしか無いので、どこからでも同じものを参照します。
GameCamera& GetCamera();

// 2 人の位置からカメラの目標を計算し、そこへ少しずつ近づける。
// 対戦中、毎フレーム 1 回だけ呼びます。
void UpdateCamera(double p1x, double p2x, double dt);

// カメラを目標位置に「一瞬で」合わせる。
// 試合開始時に呼び、前の試合の寄り具合を引きずらないようにします。
void ResetCamera(double p1x, double p2x);

// ---- 座標変換 ----
// ワールド座標 → 画面座標
inline double ToScreenX(double worldX) {
    return OriginX + (worldX - GetCamera().CenterX) * GetCamera().Zoom;
}
inline double ToScreenY(double worldY) {
    return OriginY + worldY * GetCamera().Zoom;
}
// 長さ（幅・半径・線の太さ）を拡大率に合わせて変換する。
inline double ScreenScale(double worldLength) {
    return worldLength * GetCamera().Zoom;
}

} // namespace kakuge

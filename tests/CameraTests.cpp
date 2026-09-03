// =====================================================================
// tests/CameraTests.cpp - カメラとステージ端の自動テスト
// =====================================================================
// 確かめたいのは次の 3 つです。どれも「対戦中に相手を見失わない」
// ための決めごとで、目で見て確かめるのが難しい部分です。
//
//   1. どんな配置でも、2 人とも画面の中に収まっている
//   2. ステージの端まで寄せたとき、壁の位置が画面の端とぴったり重なる
//   3. 2 人の中間点がデッドゾーンの中にある間、カメラは動かない
//
// 画面は出しません（カメラの計算は数値だけで完結します）。
// SDL2 とリンクしているのは、内部キャンバスの大きさ（VirtualW /
// VirtualH）が platform/Renderer.cpp にあるためです。
//
// 実行方法:  build/bin/CameraTests
// =====================================================================
#include <cmath>
#include <cstdio>

#include "engine/Constants.h"
#include "platform/Camera.h"

using namespace kakuge;

int failures = 0;
void check(const char* what, bool ok) {
    std::printf("%-58s %s\n", what, ok ? "[OK]" : "[NG]");
    if (!ok) failures++;
}

namespace {

// カメラを目標位置まで動かし切る（1 秒ぶん回せば十分に収束します）。
void SettleCamera(double p1x, double p2x) {
    for (int i = 0; i < 60; ++i) UpdateCamera(p1x, p2x, 1.0 / 60.0);
}

// キャラクターが画面の中に完全に収まっているか。
// 「絵 1 体ぶんの幅」を持っているものとして、その左右の端で判定します。
bool OnScreen(double worldX) {
    const double half = StageConstants::PlayerScreenMargin;
    double sx = ToScreenX(worldX);
    // 0.5 ピクセルの余裕は、カメラ位置を描画時に整数へ丸めるぶんです
    //（platform/Camera.cpp の CameraDrawX）。
    return (sx - half) >= -0.5 && (sx + half) <= VirtualW + 0.5;
}

// 押し合い判定の幅（＝壁で止まる位置）まで含めて画面の中にいるか。
// ステージの端では、キャラクターは押し合い判定の半分だけ壁の内側で
// 止まります。壁は画面の端に重なるので、端に追い詰められたときの
// 「画面に映っている」の意味はこちらになります。
bool PushboxOnScreen(double worldX) {
    const double half = GameSpec::PushboxStandWidth / 2.0;
    double sx = ToScreenX(worldX);
    return (sx - half) >= -0.5 && (sx + half) <= VirtualW + 0.5;
}

} // namespace

int main() {
    // ---- 1. どんな配置でも 2 人とも画面に収まる ----
    // 上限の距離まで離れた状態で、ステージの左端から右端まで
    // 2 人をずらしていき、どこでも両方が映ることを確かめます。
    // 1 フレームずつ動かすので、「歩いて離れた結果、デッドゾーンの
    // せいで片方が画面外に取り残される」という実際に起きた不具合も
    // ここで捕まえられます。
    //
    // 端の付近では、カメラはステージの外を映さないほうを優先します。
    // そのぶん、壁際に立ったキャラクターは画面の端に寄りますが、
    // 壁で止まる位置（押し合い判定の端）までは必ず画面の中です。
    {
        const double dist = StageConstants::MaxPlayerDistance;
        bool allVisible = true, allInside = true;
        ResetCamera(-dist / 2.0, dist / 2.0);
        for (double left = StageConstants::StageMinX + 16;
             left <= StageConstants::StageMaxX - dist - 16; left += 1.0) {
            double right = left + dist;
            UpdateCamera(left, right, 1.0 / 60.0);
            if (!PushboxOnScreen(left) || !PushboxOnScreen(right)) allInside = false;
            // カメラがステージ端に張り付いていない範囲では、
            // 絵 1 体ぶんまで含めて完全に画面の中に入ります。
            double midpoint = (left + right) / 2.0;
            bool cameraFree =
                midpoint > StageConstants::StageMinX + VirtualW / 2.0 + 80 &&
                midpoint < StageConstants::StageMaxX - VirtualW / 2.0 - 80;
            if (cameraFree && (!OnScreen(left) || !OnScreen(right))) allVisible = false;
        }
        check("上限まで離れても、2 人の体は必ず画面の中にある", allInside);
        check("端に寄っていなければ、絵 1 体ぶんまで画面に収まる", allVisible);
    }

    // 片方だけがその場から離れていく場合（デッドゾーンに一番効く形）。
    {
        ResetCamera(0.0, 0.0);
        bool allVisible = true;
        for (double d = 0; d <= StageConstants::MaxPlayerDistance; d += 2.0) {
            UpdateCamera(-d / 2.0, d / 2.0, 1.0 / 60.0);
            if (!OnScreen(-d / 2.0) || !OnScreen(d / 2.0)) { allVisible = false; break; }
        }
        check("その場から左右に離れていっても両方が映る", allVisible);
    }

    // ---- 2. ステージの端＝画面の端 ----
    {
        // 左端に追い詰められた状態（1P が左の壁、2P はその右）。
        double p1 = StageConstants::StageMinX + 16;
        SettleCamera(p1, p1 + 120);
        double wallScreenX = ToScreenX(StageConstants::StageMinX);
        check("左端まで寄せると、壁が画面の左端（X=0）に重なる",
              std::abs(wallScreenX - 0.0) < 0.001);

        double p2 = StageConstants::StageMaxX - 16;
        SettleCamera(p2 - 120, p2);
        double rightWall = ToScreenX(StageConstants::StageMaxX);
        check("右端まで寄せると、壁が画面の右端に重なる",
              std::abs(rightWall - VirtualW) < 0.001);
    }

    // ---- 3. デッドゾーン ----
    {
        ResetCamera(-50, 50);
        double before = CameraDrawX();
        // 中間点を 30 だけずらす（デッドゾーンの半分 70 より小さい）。
        SettleCamera(-20, 80);
        check("中間点がデッドゾーンの中ならカメラは動かない",
              std::abs(CameraDrawX() - before) < 0.001);

        // 帯からはみ出せば、はみ出したぶんだけ動く。
        SettleCamera(100, 200);
        check("デッドゾーンからはみ出すとカメラが追いかける",
              std::abs(CameraDrawX() - before) > 1.0);
    }

    // ---- 4. 上限の距離と画面幅の関係 ----
    // 「2 人の距離の上限＋絵 1 体ぶん＝画面幅」。この関係が崩れると、
    // どうカメラを動かしても 2 人を同時に映せなくなります。
    check("上限の距離＋絵 1 体ぶん＝画面の幅",
          StageConstants::MaxPlayerDistance +
              StageConstants::PlayerScreenMargin * 2 == BaseVirtualW);

    std::printf("\n=== RESULT: %s ===\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}

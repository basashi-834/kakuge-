// =====================================================================
// engine/Projectile.h - 飛び道具（波動拳など）
// =====================================================================
// 飛び道具は「キャラクターではない、独立して飛んでいく四角形」です。
// あえて Fighter を継承していません。飛び道具にはジャンプもガードも
// 体力もコマンド入力も要らないので、Fighter の機能をほとんど使わない
// まま引き継ぐと、かえって分かりにくくなるからです。
//
// 飛び道具が持つのは、位置・速度・寿命・「どの技から出たか」の 4 つだけ。
// ダメージやのけぞり時間は、元になった技（Move）を参照して使います。
// =====================================================================
#pragma once
#include <cmath>

#include "engine/Boxes.h"
#include "engine/MoveData.h"

namespace kakuge {

class Fighter; // 前方宣言（相互参照を避けるため、ここでは中身を知らない）

class Projectile {
public:
    const MoveData* Move = nullptr; // 元になった技（ダメージ等をここから読む）
    Fighter* Owner = nullptr;       // 撃った本人（自分には当たらないように）
    double PositionX = 0.0, PositionY = 0.0;
    double Speed = 500.0;
    int LifetimeFrames = 90;        // 残り寿命
    int Facing = 1;                 // 進む向き
    double Width = 30.0, Height = 30.0;
    bool HasHit = false;            // すでに当たったか（二重ヒット防止）
    double StageMinX = -600.0, StageMaxX = 600.0;

    // 技のデータから飛び道具を作る。
    void Setup(const MoveData& move, Fighter* owner, double spawnX, double spawnY, int facing) {
        Move = &move;
        Owner = owner;
        Facing = facing;
        const auto& p = move.Projectile;
        Speed = p.present ? p.speed : 500.0;
        LifetimeFrames = p.present ? p.lifetime : 90;
        Width = p.present ? p.width : 30.0;
        Height = p.present ? p.height : 30.0;
        double offsetX = p.present ? p.spawnOffsetX : 40.0;
        double offsetY = p.present ? p.spawnOffsetY : -40.0;
        // 出現位置は撃った人の原点から相対。向きに応じて左右反転します。
        PositionX = spawnX + (offsetX * facing);
        PositionY = spawnY + offsetY;
    }

    // 1 フレーム進める。false を返したら「もう消していい」という意味です。
    bool FrameStep(double dt) {
        PositionX += Speed * Facing * dt;
        LifetimeFrames -= 1;
        if (LifetimeFrames <= 0) return false;
        // ステージの外に十分出たら消す（画面外で無限に飛び続けないように）。
        if (PositionX < (StageMinX - 100) || PositionX > (StageMaxX + 100)) return false;
        return true;
    }

    RectBox HitboxRect() const { return RectBox(PositionX, PositionY, Width, Height); }
};

} // namespace kakuge

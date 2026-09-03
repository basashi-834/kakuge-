// =====================================================================
// platform/Figure.cpp - キャラクターと演出の描画
// =====================================================================
#include "platform/Figure.h"

#include <algorithm>
#include <cmath>

#include "platform/Camera.h"
#include "platform/Font.h"
#include "platform/Palette.h"
#include "platform/Sprite.h"

namespace kakuge {

namespace {

// 体の各パーツの色をまとめたもの。基準になる道着の色から
// 自動的に「影の色」「肌の色」などを作ります。
struct FigureStyle {
    Color body;    // 道着（基準色）
    Color outline; // 輪郭（基準色を暗くしたもの）
    Color skin;    // 肌
    Color belt;    // 帯・髪・目
    Color band;    // 鉢巻き
    Color dark;    // 奥側の手足（少し暗くして奥行きを出す）
};

FigureStyle MakeFigureStyle(Color body) {
    FigureStyle st;
    st.body = body;
    st.outline = body.Scaled(0.45);
    st.skin = Color(236, 194, 156, body.a);
    st.belt = Color(34, 30, 32, body.a);
    st.band = Color(232, 44, 38, body.a);
    st.dark = body.Scaled(0.7);
    return st;
}

// 手足 1 本ぶん。輪郭色の太いカプセルを下に敷き、その上に
// 本体色の細いカプセルを重ねることで「縁取り」を表現します。
void DrawLimb(Renderer& r, const FigureStyle& st, Color fill,
              double x1, double y1, double x2, double y2, double thickness, double outlineW) {
    r.FillCapsule(static_cast<float>(x1), static_cast<float>(y1),
                  static_cast<float>(x2), static_cast<float>(y2),
                  static_cast<float>(thickness + outlineW * 2), st.outline);
    r.FillCapsule(static_cast<float>(x1), static_cast<float>(y1),
                  static_cast<float>(x2), static_cast<float>(y2),
                  static_cast<float>(thickness), fill);
}

// 丸いパーツ（頭・拳・足先）。こちらも縁取り付き。
void DrawDisc(Renderer& r, const FigureStyle& st, Color fill,
              double cx, double cy, double radius, double outlineW) {
    if (outlineW > 0.0) {
        r.FillCircle(static_cast<float>(cx), static_cast<float>(cy),
                     static_cast<float>(radius + outlineW), st.outline);
    }
    r.FillCircle(static_cast<float>(cx), static_cast<float>(cy),
                 static_cast<float>(radius), fill);
}

} // namespace

// ---------------------------------------------------------------------
// キャラクター 1 体を描く
// ---------------------------------------------------------------------
// 描く順番が重要です。あとから描いたものが上に重なるので、
//   奥の脚 → 胴 → 奥の腕 → 手前の脚 → 頭 → 手前の腕
// の順で描くと、自然な前後関係になります。
// 奥の腕を胴のあとに描いているのは、腕が胴に隠れず
// 「両手で構えている」形がはっきり見えるようにするためです。
void DrawHumanoid(Renderer& r, double sx, double sy, Color color, const HumanoidPose& pose) {
    double s = pose.heightScale * kCharScale; // フィギュア単位 → 画面ピクセル
    int f = pose.facing < 0 ? -1 : 1;         // 向き（座標の左右反転に使う）
    FigureStyle st = MakeFigureStyle(color);

    double ow = std::max(0.6, 1.4 * s); // 輪郭の太さ

    // ---- 倒れている姿（ダウン・KO）----
    // 立ち姿とは骨格の組み立てがまったく違うので、ここで完結させます。
    if (pose.lying) {
        DrawLimb(r, st, st.dark, sx - f * 40 * s, sy - 7 * s,
                 sx - f * 6 * s, sy - 8 * s, 13.0 * s, ow);   // 脚
        DrawLimb(r, st, st.body, sx - f * 8 * s, sy - 9 * s,
                 sx + f * 22 * s, sy - 10 * s, 17.0 * s, ow); // 胴
        DrawLimb(r, st, st.body, sx + f * 4 * s, sy - 12 * s,
                 sx + f * 24 * s, sy - 22 * s, 10.0 * s, ow); // 上げた腕
        DrawDisc(r, st, st.skin, sx + f * 36 * s, sy - 11 * s, 11.0 * s, ow); // 頭
        r.FillRect(static_cast<float>(sx + f * 36 * s - 11 * s),
                   static_cast<float>(sy - 15 * s),
                   static_cast<float>(22 * s), static_cast<float>(4 * s), st.band);
        return;
    }

    double limbT = 13.0 * s;            // 脚の太さ
    double armT = 11.0 * s;             // 腕の太さ
    double headR = 12.0 * s;            // 頭の半径

    // 縦の積み上げ（単位）: 脚 46 ＋ 胴 38 ＋ 首 2 ＋ 頭 24 = 108
    double legH = 46.0 * s, torsoH = 38.0 * s;
    if (pose.crouch) { legH = 24.0 * s; torsoH = 26.0 * s; } // しゃがむと縮む

    // 立ちアニメの上下動（呼吸しているように見せる 1 ピクセルの揺れ）
    double bob = (pose.idleFrame == 1 || pose.idleFrame == 2) ? 1.0 * s : 0.0;

    double hipY = sy - legH + bob;      // 腰の高さ
    double shoulderY = hipY - torsoH;   // 肩の高さ
    double headCY = shoulderY - 2.0 * s - headR; // 頭の中心
    double cx = sx + (pose.leanBack * 0.35);     // のけぞりで体をずらす

    // ---- 脚の関節位置を決める ----
    double hipBackX = cx - f * 8.0 * s, hipFrontX = cx + f * 8.0 * s;
    double kneeBX, kneeBY, footBX, footBY, kneeFX, kneeFY, footFX, footFY;
    if (pose.crouch) {
        // しゃがみ: 膝を大きく開いて腰を落とす
        kneeBX = cx - f * 24.0 * s; kneeBY = hipY + 6.0 * s; footBX = cx - f * 20.0 * s; footBY = sy;
        kneeFX = cx + f * 24.0 * s; kneeFY = hipY + 6.0 * s; footFX = cx + f * 22.0 * s; footFY = sy;
    } else if (pose.jump) {
        // 空中: 脚を抱え込む（足が原点より上に来る）
        kneeBX = cx - f * 14.0 * s; kneeBY = hipY + 16.0 * s; footBX = cx - f * 6.0 * s; footBY = hipY + 30.0 * s;
        kneeFX = cx + f * 18.0 * s; kneeFY = hipY + 14.0 * s; footFX = cx + f * 12.0 * s; footFY = hipY + 30.0 * s;
    } else {
        // 立ち: 前後に軽く開いた構え
        kneeBX = cx - f * 13.0 * s; kneeBY = hipY + legH * 0.52; footBX = cx - f * 20.0 * s; footBY = sy;
        kneeFX = cx + f * 12.0 * s; kneeFY = hipY + legH * 0.52; footFX = cx + f * 20.0 * s; footFY = sy;
    }

    // ---- キック ----
    // 立ちでもしゃがみでも、蹴り脚は「腰の付け根から斜め上へ伸ばした
    // 1 本の直線」にします。膝は曲げません。
    //
    // 膝を曲げないのは見た目の好みではなく、実用上の理由があります。
    // 384x224 の画面ではキャラクターの脚は 40 ピクセルほどしかなく、
    // そこに膝の折れ角を入れると、脚がどこを向いているのか
    // 一瞬では読めなくなります。まっすぐな 1 本の線にすると
    // 「どこへ蹴っているか」が離れて見ても分かります。
    //
    // そのために、膝の点を腰と足先を結ぶ線の途中に置きます。
    // 描画は今までどおり「腰→膝」「膝→足先」の 2 本ですが、
    // 3 点が一直線に並ぶので、見た目は 1 本の脚になります。
    if (pose.legKick > 0) {
        // 角度は姿勢で変えます。
        //   しゃがみ … 浅く（下段攻撃なのに足が上にあると分かりにくい）
        //   立ち     … 斜め上へ
        //   空中     … 斜め下へ（跳び蹴りは下を蹴るもの）
        double angleDeg = 34.0;
        if (pose.crouch) angleDeg = 14.0;
        else if (pose.jump) angleDeg = -30.0;
        const double angle = angleDeg * 3.14159265358979 / 180.0;
        // 脚の長さ。技の強さ（legKick）が大きいほど深く踏み込みます。
        double reach = (pose.crouch ? 40.0 : 48.0) + std::min(pose.legKick, 40.0) * 0.35;
        double len = reach * s;

        footFX = hipFrontX + f * len * std::cos(angle);
        footFY = hipY - len * std::sin(angle);
        // 膝は腰と足先のちょうど中間（＝一直線）。
        kneeFX = (hipFrontX + footFX) / 2.0;
        kneeFY = (hipY + footFY) / 2.0;

        if (!pose.jump) {
            // 軸足（奥の脚）は地面を踏んだままにします。片脚を上げるぶん、
            // 支える側は少し後ろへ開いたほうが安定して見えます。
            kneeBX = cx - f * 16.0 * s; kneeBY = hipY + (sy - hipY) * 0.5;
            footBX = cx - f * 24.0 * s; footBY = sy;
        }
        // 空中では地面が無いので、奥の脚は抱え込んだまま（上の分岐のまま）
        // にします。ここで地面に着けてしまうと、宙に浮いているのに
        // 片脚だけ地面に立っている絵になります。
    }

    // ---- 奥の脚 ----
    DrawLimb(r, st, st.dark, hipBackX, hipY, kneeBX, kneeBY, limbT, ow);
    DrawLimb(r, st, st.dark, kneeBX, kneeBY, footBX, footBY, limbT, ow);
    DrawDisc(r, st, st.outline, footBX - f * 2.0 * s, footBY - 3.0 * s, 4.5 * s, 0.0);

    // ---- 胴（肩が広く腰が狭い台形）----
    {
        Vec2 pts[4] = {
            {static_cast<float>(cx - 22.0 * s), static_cast<float>(shoulderY)},
            {static_cast<float>(cx + 22.0 * s), static_cast<float>(shoulderY)},
            {static_cast<float>(cx + 15.0 * s), static_cast<float>(hipY + 4.0 * s)},
            {static_cast<float>(cx - 15.0 * s), static_cast<float>(hipY + 4.0 * s)},
        };
        // 輪郭は「少し大きい同じ形」を下に敷いて表現します。
        Vec2 outer[4];
        for (int i = 0; i < 4; ++i) {
            // 台形の中心から外側へ ow ぶん押し出す
            float ccx = static_cast<float>(cx);
            float ccy = static_cast<float>((shoulderY + hipY) / 2.0);
            float dx = pts[i].x - ccx, dy = pts[i].y - ccy;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.001f) len = 1.0f;
            outer[i].x = pts[i].x + dx / len * static_cast<float>(ow);
            outer[i].y = pts[i].y + dy / len * static_cast<float>(ow);
        }
        r.FillPolygon(outer, 4, st.outline);
        r.FillPolygon(pts, 4, st.body);

        // 帯
        r.FillRect(static_cast<float>(cx - 15.0 * s), static_cast<float>(hipY - 5.0 * s),
                   static_cast<float>(30.0 * s), static_cast<float>(5.0 * s), st.belt);
        // 道着の合わせ目（斜めの線）。これがあると体に「正面」ができます。
        r.DrawLine(static_cast<float>(cx + f * 12.0 * s), static_cast<float>(shoulderY + 2.0 * s),
                   static_cast<float>(cx - f * 2.0 * s), static_cast<float>(hipY - 6.0 * s),
                   st.outline, static_cast<float>(ow));
    }

    // ---- 奥の腕（胴より前面に出す）----
    // 描く順番の中で、この腕だけは「胴のあと」に来ます。
    //
    // 以前は胴より先に描いていたため、腕の大部分が胴に隠れ、
    // 肩から拳の先だけが胴の脇からのぞく形になっていました。
    // 実際に構えるときは、奥の腕も体の前に回して構えます。
    // 胴のあとに描くと腕が丸ごと見えるようになり、
    // 「両手で構えている」ことが小さな画面でも分かります。
    //
    // 前後関係は「奥の脚 → 胴 → 奥の腕 → 手前の脚 → 頭 → 手前の腕」。
    // 奥の腕は暗い色（st.dark）のままにしてあるので、胴の上に
    // 乗っていても手前の腕と見分けが付き、奥行きは失われません。
    double shBackX = cx - f * 18.0 * s, shBackY = shoulderY + 4.0 * s;
    double elBackX, elBackY, hdBackX, hdBackY;
    if (pose.guardRaise > 0) {
        // ガード: 顔の前に構える
        elBackX = cx - f * 6.0 * s; elBackY = shoulderY + 14.0 * s;
        hdBackX = cx + f * 14.0 * s; hdBackY = shoulderY - 2.0 * s;
    } else {
        // 構え: 肘は体の横に下ろし、前腕だけを前へ出します。
        //
        // 肘を胴の内側に入れて前腕を前へ振り出すと、腕が背中側へ
        // 回り込まず、自然な構えに見えます。
        elBackX = cx - f * 4.0 * s;  elBackY = shoulderY + 24.0 * s;
        hdBackX = cx + f * 24.0 * s; hdBackY = shoulderY + 18.0 * s - bob;
    }
    DrawLimb(r, st, st.dark, shBackX, shBackY, elBackX, elBackY, armT, ow);
    DrawLimb(r, st, st.dark, elBackX, elBackY, hdBackX, hdBackY, armT, ow);
    DrawDisc(r, st, st.skin, hdBackX, hdBackY, 5.5 * s, ow);

    // ---- 手前の脚（胴の上に重なる）----
    DrawLimb(r, st, st.body, hipFrontX, hipY, kneeFX, kneeFY, limbT, ow);
    DrawLimb(r, st, st.body, kneeFX, kneeFY, footFX, footFY, limbT, ow);
    DrawDisc(r, st, st.outline, footFX + f * 2.0 * s, footFY - 3.0 * s, 4.5 * s, 0.0);

    // ---- 頭 ----
    {
        double headCX = cx + f * 2.0 * s;
        DrawDisc(r, st, st.skin, headCX, headCY, headR, ow);
        // 髪（頭の上半分を暗く塗る）
        r.FillTopHalfCircle(static_cast<float>(headCX), static_cast<float>(headCY),
                            static_cast<float>(headR), st.belt);
        // 鉢巻き
        r.FillRect(static_cast<float>(headCX - headR), static_cast<float>(headCY - 5.0 * s),
                   static_cast<float>(headR * 2), static_cast<float>(5.0 * s), st.band);
        // 鉢巻きの垂れ（後ろへ 2 本）
        double tx = headCX - f * headR * 0.9, ty = headCY - 3.0 * s;
        r.DrawLine(static_cast<float>(tx), static_cast<float>(ty),
                   static_cast<float>(tx - f * 9.0 * s), static_cast<float>(ty - 6.0 * s),
                   st.band, static_cast<float>(3.0 * s));
        r.DrawLine(static_cast<float>(tx), static_cast<float>(ty),
                   static_cast<float>(tx - f * 7.0 * s), static_cast<float>(ty + 4.0 * s),
                   st.band, static_cast<float>(3.0 * s));
        // 目（小さな点ひとつ）。これだけで向きが一気に分かりやすくなります。
        r.FillRect(static_cast<float>(headCX + f * 4.0 * s - 1.2 * s),
                   static_cast<float>(headCY + 1.0 * s),
                   static_cast<float>(2.4 * s), static_cast<float>(2.4 * s), st.belt);
    }

    // ---- 手前の腕 ----
    double shFrontX = cx + f * 18.0 * s, shFrontY = shoulderY + 4.0 * s;
    double elX, elY, hdX, hdY;
    if (pose.armReach > 0) {
        // パンチ: 前方に大きく伸ばす
        elX = cx + f * 34.0 * s; elY = shoulderY + 8.0 * s;
        hdX = cx + f * (46.0 + pose.armReach * 0.8) * s;
        hdY = shoulderY + 4.0 * s - std::min(pose.armReach, 20.0) * 0.2 * s;
    } else if (pose.guardRaise > 0) {
        elX = cx + f * 26.0 * s; elY = shoulderY + 12.0 * s;
        hdX = cx + f * 22.0 * s; hdY = shoulderY - 8.0 * s;
    } else {
        elX = cx + f * 30.0 * s; elY = shoulderY + 16.0 * s;
        hdX = cx + f * 27.0 * s; hdY = shoulderY + 2.0 * s + bob;
    }
    DrawLimb(r, st, st.body, shFrontX, shFrontY, elX, elY, armT, ow);
    DrawLimb(r, st, st.body, elX, elY, hdX, hdY, armT, ow);
    DrawDisc(r, st, st.skin, hdX, hdY, 6.0 * s, ow);
}

// ---------------------------------------------------------------------
// 技の種類による体の色
// ---------------------------------------------------------------------
Color MoveTint(const Fighter& fighter) {
    Color base(fighter.Stats.ColorR, fighter.Stats.ColorG, fighter.Stats.ColorB);
    if (!fighter.CurrentMoveData) return base;
    const MoveData& m = *fighter.CurrentMoveData;
    if (m.HasTag(Constants::TagSuper)) return Color(240, 60, 60);    // 超必殺技は明るい赤
    if (m.HasTag(Constants::TagSpecial)) return Color(150, 70, 220); // 必殺技は紫
    return base;
}

// ---------------------------------------------------------------------
// スプライトで描く（絵が用意されているときだけ）
// ---------------------------------------------------------------------
// 描けたら true。絵が無ければ false を返すので、呼び出し側は
// これまでどおりの図形描画に進みます。
bool DrawFighterSprite(Renderer& r, const Fighter& fighter) {
    const CharacterSprites* sprites = GetSprites().Get(fighter.Stats.Id);
    if (!sprites) return false;
    const SpriteCell* cell = PickFighterCell(*sprites, fighter);
    if (!cell) return false;

    // 座標の丸め方は図形描画とまったく同じにします。ここがずれると
    // 当たり判定のデバッグ表示と絵の位置が合わなくなります。
    double sx = std::round(ToScreenX(fighter.PositionX));
    double sy = std::round(ToScreenY(fighter.PositionY));
    DrawSpriteCell(r, sprites->Sheet, *cell, sx, sy, fighter.Facing,
                   FighterSpriteTint(*sprites, fighter));
    return true;
}

// キャラクター選択画面などで、立ち絵を 1 コマ描く。
// 絵が無ければ false（呼び出し側が図形で描きます）。
bool DrawIdleSprite(Renderer& r, const std::string& characterId,
                    double sx, double sy, int facing, int frame, double scale) {
    const CharacterSprites* sprites = GetSprites().Get(characterId);
    if (!sprites) return false;
    const SpriteCell* cell = PickIdleCell(*sprites, frame);
    if (!cell) return false;
    DrawSpriteCell(r, sprites->Sheet, *cell, std::round(sx), std::round(sy), facing,
                   Color(255, 255, 255, 255), scale);
    return true;
}

// ---------------------------------------------------------------------
// 試合中のキャラクターを状態に応じて描く
// ---------------------------------------------------------------------
void DrawFighter(Renderer& r, const Fighter& fighter) {
    // 手描きの絵（スプライト）が用意されていれば、そちらを優先します。
    // 用意されていなければ、これまでどおり図形で組み立てて描きます
    //（絵が 1 枚も無くても遊べる、というこのゲームの性質はそのまま）。
    if (DrawFighterSprite(r, fighter)) return;

    Color bodyColor(fighter.Stats.ColorR, fighter.Stats.ColorG, fighter.Stats.ColorB);

    // 座標を整数に丸めます。当たり判定側も同じように丸めているので、
    // デバッグ表示の枠と絵が必ず一致します。
    double sx = std::round(ToScreenX(fighter.PositionX));
    double sy = std::round(ToScreenY(fighter.PositionY));
    int facing = fighter.Facing;

    // 対戦中の表示倍率は常に 100%（カメラは拡大縮小しません）。
    // 間合いの感覚を狂わせないための、このゲームの基本方針です。
    bool airborne = fighter.PositionY < (Fighter::GroundY - 1.0);

    // ダウン中・KO 後の「倒れている姿」（DrawHumanoid が描き分けます）。
    auto drawLying = [&](Color body) {
        HumanoidPose lying;
        lying.facing = facing;
        lying.lying = true;
        DrawHumanoid(r, sx, sy, body, lying);
    };

    HumanoidPose pose;
    pose.heightScale = 1.0; // 常に等倍
    pose.facing = facing;

    switch (fighter.SM.CurrentState) {
        case CharState::Knockdown:
            drawLying(bodyColor.Scaled(0.8));
            break;
        case CharState::Dead:
            drawLying(Color(205, 202, 200)); // 灰色に
            break;
        case CharState::WakeUp:
        case CharState::Crouch:
            pose.crouch = true;
            DrawHumanoid(r, sx, sy, bodyColor, pose);
            break;
        case CharState::Block:
            pose.guardRaise = 10;
            pose.crouch = fighter.IsCrouchingGuard;
            DrawHumanoid(r, sx, sy, Color(60, 120, 210), pose); // ガードは青
            break;
        case CharState::Hitstun:
            pose.leanBack = -8.0 * facing;
            pose.jump = airborne;
            DrawHumanoid(r, sx, sy, Color(220, 60, 60), pose);  // 食らいは赤
            break;
        case CharState::Throw:
            pose.leanBack = -6.0 * facing;
            DrawHumanoid(r, sx, sy, Color(200, 50, 50), pose);
            break;
        case CharState::Attack: {
            // ボタン名の末尾が K ならキック、それ以外はパンチの姿勢。
            std::string btn = fighter.CurrentMoveData ? fighter.CurrentMoveData->Button : std::string();
            bool isKick = !btn.empty() && btn.back() == 'K';
            bool crouchMove = fighter.CurrentMoveData && fighter.CurrentMoveData->Stance == "crouch";
            pose.crouch = crouchMove;
            pose.jump = airborne;
            if (isKick) pose.legKick = 34; else pose.armReach = 34;
            DrawHumanoid(r, sx, sy, MoveTint(fighter), pose);
            break;
        }
        case CharState::Jump:
            pose.jump = true;
            DrawHumanoid(r, sx, sy, bodyColor, pose);
            break;
        default:
            // 立ち・歩き: 8 フレームごとに 4 コマの呼吸アニメ
            pose.idleFrame = (fighter.FrameCounter / 8) % 4;
            DrawHumanoid(r, sx, sy, bodyColor, pose);
            break;
    }
}

void DrawProjectile(Renderer& r, const Projectile& proj) {
    double sx = ToScreenX(proj.PositionX);
    double sy = ToScreenY(proj.PositionY);
    double rad = 12.0; // 倍率固定なのでそのままのピクセル数
    // 外側の明るい輪 → 内側の濃い玉、の 2 重で「光っている」感じを出す
    r.FillCircle(static_cast<float>(sx), static_cast<float>(sy),
                 static_cast<float>(rad + 2.0), Color(255, 200, 60));
    r.FillCircle(static_cast<float>(sx), static_cast<float>(sy),
                 static_cast<float>(rad), Color(235, 130, 30));
}

EffectStyle GetEffectStyle(const std::string& kind) {
    // {色, 大きさ（画面ピクセル）, 表示時間（秒）}
    //
    // 技の強さで大きさに差を付けます。弱で 10、超必で 34。
    // 表示倍率が 100% 固定になったので、ここの数値がそのまま
    // 画面上のピクセル数です（キャラの身長が 95 なので、
    // 超必のエフェクトでも体の 3 分の 1 ほど）。
    //
    // 表示時間を短めにしているのは、エフェクトが長く残ると
    // キャラクターが隠れて次の行動が読めなくなるためです。
    if (kind == "hit") return {Color(255, 200, 40), 10.0, 0.11};          // 弱
    if (kind == "medium_hit") return {Color(255, 170, 30), 14.0, 0.13};   // 中
    if (kind == "heavy_hit") return {Color(235, 110, 20), 20.0, 0.16};    // 強
    if (kind == "guard") return {Color(60, 130, 220), 13.0, 0.12};        // ガード
    if (kind == "special") return {Color(140, 40, 220), 24.0, 0.20};      // 必殺技
    if (kind == "super") return {Color(230, 30, 30), 34.0, 0.28};         // 超必殺技
    if (kind == "counter") return {Color(255, 210, 30), 18.0, 0.30};
    if (kind == "effective_counter") return {Color(255, 40, 40), 28.0, 0.42};
    return {Color(255, 255, 255), 10.0, 0.11};
}

void DrawEffect(Renderer& r, const LiveEffect& fx) {
    EffectStyle style = GetEffectStyle(fx.kind);
    double t = std::min(1.0, fx.age / style.duration); // 0（出た瞬間）→ 1（消える）
    // 時間とともに大きく、薄くなる
    double rad = style.radius * (0.4 + t * 1.1);
    int alpha = static_cast<int>(255 * (1.0 - t));
    if (alpha <= 0) return;
    double sx = ToScreenX(fx.x), sy = ToScreenY(fx.y);
    r.FillCircle(static_cast<float>(sx), static_cast<float>(sy),
                 static_cast<float>(rad), style.color.WithAlpha(alpha));
}

void DrawCounterEdgeLabel(Renderer& r, const LiveEffect& fx) {
    bool effective = (fx.kind == "effective_counter");
    if (!effective && fx.kind != "counter") return;
    EffectStyle style = GetEffectStyle(fx.kind);
    double t = std::min(1.0, fx.age / style.duration);
    int alpha = static_cast<int>(255 * (1.0 - t));
    if (alpha <= 0) return;

    // 384x224 の画面では、ヒットした場所に文字を出すと窮屈で読めません。
    // そこで「決めたプレイヤー側の画面端」に固定して出します。
    std::string label = effective ? "E.COUNTER" : "COUNTER";
    float dot = effective ? 1.5f : 1.0f;
    float labelW = 72.0f, labelH = 12.0f;
    bool leftEdge = (fx.side == 0);
    float x = leftEdge ? 2.0f : (VirtualW - 2.0f - labelW);
    float y = (VirtualH - labelH) / 2.0f;
    DrawPixelTextCentered(r, label, x, y, labelW, labelH, dot, style.color.WithAlpha(alpha));
}

} // namespace kakuge

// =====================================================================
// platform/Figure.cpp - キャラクターと演出の描画
// =====================================================================
#include "platform/Figure.h"

#include <algorithm>
#include <cmath>

#include "platform/Camera.h"
#include "platform/Font.h"
#include "platform/Palette.h"

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
//   奥の脚 → 奥の腕 → 胴 → 手前の脚 → 頭 → 手前の腕
// の順で描くと、自然な前後関係になります。
void DrawHumanoid(Renderer& r, double sx, double sy, Color color, const HumanoidPose& pose) {
    double s = pose.heightScale * kCharScale; // フィギュア単位 → 画面ピクセル
    int f = pose.facing < 0 ? -1 : 1;         // 向き（座標の左右反転に使う）
    FigureStyle st = MakeFigureStyle(color);

    double ow = std::max(0.6, 1.4 * s); // 輪郭の太さ
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
        if (pose.legKick > 0) {
            // キック: 前脚を前方に伸ばす
            kneeFX = cx + f * 22.0 * s; kneeFY = hipY + 10.0 * s;
            footFX = cx + f * (30.0 + pose.legKick * 0.9) * s;
            footFY = hipY + 6.0 * s - std::min(pose.legKick, 34.0) * 0.55 * s;
        }
    }

    // ---- 奥の脚 ----
    DrawLimb(r, st, st.dark, hipBackX, hipY, kneeBX, kneeBY, limbT, ow);
    DrawLimb(r, st, st.dark, kneeBX, kneeBY, footBX, footBY, limbT, ow);
    DrawDisc(r, st, st.outline, footBX - f * 2.0 * s, footBY - 3.0 * s, 4.5 * s, 0.0);

    // ---- 奥の腕 ----
    double shBackX = cx - f * 18.0 * s, shBackY = shoulderY + 4.0 * s;
    double elBackX, elBackY, hdBackX, hdBackY;
    if (pose.guardRaise > 0) {
        // ガード: 顔の前に構える
        elBackX = cx - f * 6.0 * s; elBackY = shoulderY + 14.0 * s;
        hdBackX = cx + f * 14.0 * s; hdBackY = shoulderY - 2.0 * s;
    } else {
        elBackX = cx - f * 26.0 * s; elBackY = shoulderY + 18.0 * s;
        hdBackX = cx - f * 12.0 * s; hdBackY = shoulderY + 24.0 * s - bob;
    }
    DrawLimb(r, st, st.dark, shBackX, shBackY, elBackX, elBackY, armT, ow);
    DrawLimb(r, st, st.dark, elBackX, elBackY, hdBackX, hdBackY, armT, ow);
    DrawDisc(r, st, st.skin, hdBackX, hdBackY, 5.5 * s, ow);

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
// 試合中のキャラクターを状態に応じて描く
// ---------------------------------------------------------------------
void DrawFighter(Renderer& r, const Fighter& fighter) {
    Color bodyColor(fighter.Stats.ColorR, fighter.Stats.ColorG, fighter.Stats.ColorB);

    // 座標を整数に丸めます。当たり判定側も同じように丸めているので、
    // デバッグ表示の枠と絵が必ず一致します。
    double sx = std::round(ToScreenX(fighter.PositionX));
    double sy = std::round(ToScreenY(fighter.PositionY));
    int facing = fighter.Facing;

    // DrawHumanoid 自体はカメラを知らない（選択画面でも使うため）ので、
    // 拡大率はここで姿勢の heightScale として渡します。
    double zoom = GetCamera().Zoom;
    double cs = kCharScale * zoom;

    bool airborne = fighter.PositionY < (Fighter::GroundY - 1.0);

    // ダウン中・KO 後の「倒れている姿」。立ち姿とは別に描きます。
    auto drawLying = [&](Color body) {
        FigureStyle st = MakeFigureStyle(body);
        double ow = std::max(0.6, 1.4 * cs);
        DrawLimb(r, st, st.dark, sx - facing * 40 * cs, sy - 7 * cs,
                 sx - facing * 6 * cs, sy - 8 * cs, 13.0 * cs, ow);   // 脚
        DrawLimb(r, st, st.body, sx - facing * 8 * cs, sy - 9 * cs,
                 sx + facing * 22 * cs, sy - 10 * cs, 17.0 * cs, ow); // 胴
        DrawLimb(r, st, st.body, sx + facing * 4 * cs, sy - 12 * cs,
                 sx + facing * 24 * cs, sy - 22 * cs, 10.0 * cs, ow); // 上げた腕
        DrawDisc(r, st, st.skin, sx + facing * 36 * cs, sy - 11 * cs, 11.0 * cs, ow); // 頭
        r.FillRect(static_cast<float>(sx + facing * 36 * cs - 11 * cs),
                   static_cast<float>(sy - 15 * cs),
                   static_cast<float>(22 * cs), static_cast<float>(4 * cs), st.band);
    };

    HumanoidPose pose;
    pose.heightScale = zoom;
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
            pose.leanBack = -8.0 * facing * zoom;
            pose.jump = airborne;
            DrawHumanoid(r, sx, sy, Color(220, 60, 60), pose);  // 食らいは赤
            break;
        case CharState::Throw:
            pose.leanBack = -6.0 * facing * zoom;
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
    double rad = ScreenScale(11.0);
    // 外側の明るい輪 → 内側の濃い玉、の 2 重で「光っている」感じを出す
    r.FillCircle(static_cast<float>(sx), static_cast<float>(sy),
                 static_cast<float>(rad + ScreenScale(2.0)), Color(255, 200, 60));
    r.FillCircle(static_cast<float>(sx), static_cast<float>(sy),
                 static_cast<float>(rad), Color(235, 130, 30));
}

EffectStyle GetEffectStyle(const std::string& kind) {
    // {色, 大きさ, 表示時間（秒）}
    if (kind == "hit") return {Color(255, 200, 40), 16.0, 0.14};
    if (kind == "heavy_hit") return {Color(235, 110, 20), 26.0, 0.18};
    if (kind == "guard") return {Color(60, 130, 220), 20.0, 0.14};
    if (kind == "special") return {Color(140, 40, 220), 30.0, 0.22};
    if (kind == "super") return {Color(230, 30, 30), 46.0, 0.32};
    if (kind == "counter") return {Color(255, 210, 30), 24.0, 0.35};
    if (kind == "effective_counter") return {Color(255, 40, 40), 40.0, 0.5};
    return {Color(255, 255, 255), 16.0, 0.14};
}

void DrawEffect(Renderer& r, const LiveEffect& fx) {
    EffectStyle style = GetEffectStyle(fx.kind);
    double t = std::min(1.0, fx.age / style.duration); // 0（出た瞬間）→ 1（消える）
    // 時間とともに大きく、薄くなる
    double rad = ScreenScale(style.radius * (0.4 + t * 1.1));
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

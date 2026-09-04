// =====================================================================
// platform/Hud.cpp - 情報表示の中身
// =====================================================================
#include "platform/Hud.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "platform/Camera.h"
#include "platform/Figure.h"
#include "platform/Font.h"
#include "platform/Palette.h"

namespace kakuge {

namespace {

// 顔アイコン。キャラクターごとの絵は用意していないので、
// 頭・鉢巻き・肩のシルエットだけの共通の記号を描いています。
// 将来キャラクターごとの画像を入れるなら、ここを差し替えます。
void DrawPortraitBust(Renderer& r, float x, float y, float size, Color accent, Color borderColor) {
    r.FillRect(x, y, size, size, Color(34, 32, 40)); // 枠の中の下地

    // はみ出しを防ぐため、枠の内側だけに描くよう制限します。
    r.SetClip(x, y, size, size);
    float headR = size * 0.24f;
    float cx = x + size / 2.0f;
    float headY = y + size * 0.16f;
    r.FillCircle(cx, headY + headR, headR, Color(222, 170, 130));            // 頭
    r.FillRect(cx - headR, headY + headR * 0.85f, headR * 2.0f, headR * 0.5f, accent); // 鉢巻き
    r.FillEllipse(cx, y + size * 0.92f, size * 0.44f, size * 0.30f, Color(60, 62, 74)); // 肩
    r.ClearClip();

    r.DrawRect(x, y, size, size, borderColor, 1.0f);
}

// プレイヤー名。狭いので枠なしの小さな文字だけです。
void DrawNameTag(Renderer& r, float barX, float barBottom, float barW,
                 const std::string& name, bool mirror) {
    const auto& pal = GetPalette();
    if (mirror) DrawPixelTextRight(r, name, barX + barW, barBottom, 1.0f, pal.ArenaLine);
    else DrawPixelText(r, name, barX, barBottom, 1.0f, pal.ArenaLine);
}

// 当たり判定の四角形を、ワールド座標から画面座標に直して枠で描く。
// 左右・上下の端をそれぞれ独立に丸めているのがポイントです。
// 「原点を丸めてから幅を掛ける」と、拡大率によって隣り合う枠の間に
// 1 ピクセルの隙間ができたり重なったりします。
void DrawWorldRect(Renderer& r, const RectBox& box, Color color) {
    float left = static_cast<float>(std::round(ToScreenX(box.Left())));
    float right = static_cast<float>(std::round(ToScreenX(box.Right())));
    float top = static_cast<float>(std::round(ToScreenY(box.Top())));
    float bottom = static_cast<float>(std::round(ToScreenY(box.Bottom())));
    r.DrawRect(left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top), color, 1.0f);
}

} // namespace

// ---------------------------------------------------------------------
// ゲージ 1 本
// ---------------------------------------------------------------------
void DrawBar(Renderer& r, float x, float y, float w, float h, double ratio,
             Color fillColor, Color emptyColor, bool mirror, Color borderColor) {
    ratio = std::max(0.0, std::min(1.0, ratio));
    r.FillRect(x, y, w, h, emptyColor);           // 空の部分
    float fillW = static_cast<float>(w * ratio);
    if (fillW > 0.0f) {
        // mirror なら右端を起点に伸びます（2P のゲージは中央から
        // 外側に向かって減るのが格闘ゲームの慣習）。
        float fx = mirror ? (x + w - fillW) : x;
        r.FillRect(fx, y, fillW, h, fillColor);
        // 上端を少し明るくして立体感を出す
        r.FillRect(fx, y, fillW, std::max(1.0f, h * 0.28f), fillColor.Scaled(1.35));
    }
    r.DrawRect(x, y, w, h, borderColor, 1.0f);    // 枠
}

// ---------------------------------------------------------------------
// 情報表示ひとそろい
// ---------------------------------------------------------------------
void DrawHUD(Renderer& r, const BattleSystem& bs,
             int p1ComboDisplay, int p2ComboDisplay, double comboFade) {
    const auto& pal = GetPalette();

    // ---- 体力ゲージと顔アイコン ----
    // 画面の外側から順に「顔アイコン → 体力ゲージ」と並べ、
    // ゲージは画面中央に向かって伸びる配置です。
    const float barY = 9.0f, barH = 10.0f;
    const float portraitSize = 22.0f, portraitGap = 3.0f;
    const float portraitY = barY - 6.0f;
    const float p1PortraitX = 2.0f;
    const float barW = 115.0f;
    const float p1BarX = p1PortraitX + portraitSize + portraitGap;
    const float p2BarX = VirtualW - 2.0f - portraitSize - portraitGap - barW;
    const float p2PortraitX = VirtualW - 2.0f - portraitSize;

    double p1Ratio = bs.Player1.CurrentHP / static_cast<double>(bs.Player1.Stats.MaxHP);
    double p2Ratio = bs.Player2.CurrentHP / static_cast<double>(bs.Player2.Stats.MaxHP);
    DrawBar(r, p1BarX, barY, barW, barH, p1Ratio, pal.Accent, pal.EmptyBar, false, pal.ArenaLine);
    DrawBar(r, p2BarX, barY, barW, barH, p2Ratio, pal.Accent, pal.EmptyBar, true, pal.ArenaLine);

    DrawPortraitBust(r, p1PortraitX, portraitY, portraitSize, pal.Accent, pal.ArenaLine);
    DrawPortraitBust(r, p2PortraitX, portraitY, portraitSize, pal.Accent, pal.ArenaLine);

    DrawNameTag(r, p1BarX, barY + barH + 1.0f, barW, bs.Player1.Stats.Name, false);
    DrawNameTag(r, p2BarX, barY + barH + 1.0f, barW, bs.Player2.Stats.Name, true);

    // ---- 残り時間 ----
    // トレーニングモードは時間が減らないので "--" と表示します
    //（∞ の記号は、このドットフォントに無いうえ、この大きさでは
    //   何の記号か分からないため使いません）。
    std::string timerText = bs.TrainingMode
        ? std::string("--")
        : std::to_string(static_cast<int>(std::ceil(bs.FramesLeft / 60.0)));

    const float boxW = 28, boxH = 18;
    const float boxX = (VirtualW - boxW) / 2.0f;
    r.FillRect(boxX, 5, boxW, boxH, pal.Accent);
    r.DrawRect(boxX, 5, boxW, boxH, pal.ArenaLine, 1.0f);
    DrawPixelTextCentered(r, timerText, boxX, 5, boxW, boxH, 2.0f, pal.White);
    DrawPixelTextCentered(r, bs.TrainingMode ? "TRAINING" : "ROUND 1",
                          VirtualW / 2.0f - 30, 24, 60, 8, 1.0f, pal.ArenaTextDim);

    // ---- 「FIGHT」表示（試合開始の合図）----
    if (bs.RoundStartFlashFrames > 0) {
        double t = bs.RoundStartFlashFrames / static_cast<double>(BattleSystem::RoundStartFlashDuration);
        // 表示時間の最後の 3 割で消えていく
        int alpha = t > 0.3 ? 255 : static_cast<int>(255 * (t / 0.3));
        r.FillRect(VirtualW / 2.0f - 60, 62, 120, 16, Color(20, 19, 18, alpha));
        DrawPixelTextCentered(r, "FIGHT", VirtualW / 2.0f - 60, 62, 120, 16, 2.0f,
                              Color(255, 255, 255, alpha));
    }

    // ---- コンボ数 ----
    if (comboFade > 0.01) {
        int alpha = static_cast<int>(255 * comboFade);
        int shownCombo = p1ComboDisplay > 0 ? p1ComboDisplay : p2ComboDisplay;
        bool onRight = p2ComboDisplay > 0;
        if (shownCombo >= 2) { // 1 ヒットはコンボと呼ばない
            std::string comboText = std::to_string(shownCombo) + " HIT COMBO";
            float cx = onRight ? VirtualW - 82.0f : 12.0f;
            r.FillRect(cx, 34, 70, 10, pal.Accent.WithAlpha(std::min(220, alpha)));
            DrawPixelTextCentered(r, comboText, cx, 34, 70, 10, 1.0f,
                                  Color(255, 255, 255, alpha));
        }
    }

    // ---- 超必殺技ゲージ（画面下）----
    const float gaugeW = 90.0f, gaugeH = 8.0f;
    const float gaugeY = static_cast<float>(VirtualH) - gaugeH - 3.0f;
    double sp1 = bs.Player1.Gauge.Value / SuperGauge::MaxValue;
    double sp2 = bs.Player2.Gauge.Value / SuperGauge::MaxValue;
    DrawBar(r, 3, gaugeY, gaugeW, gaugeH, sp1, pal.Accent, pal.EmptyBar, false, pal.ArenaLine);
    DrawBar(r, VirtualW - 3.0f - gaugeW, gaugeY, gaugeW, gaugeH, sp2,
            pal.Accent, pal.EmptyBar, true, pal.ArenaLine);
    DrawPixelText(r, "SP", 3, gaugeY - 7, 1.0f, pal.ArenaTextDim);
    DrawPixelTextRight(r, "SP", VirtualW - 3.0f, gaugeY - 7, 1.0f, pal.ArenaTextDim);
}

// ---------------------------------------------------------------------
// デバッグ表示
// ---------------------------------------------------------------------
// 技を作るときの必需品です。目に見えない当たり判定を可視化します。
//
// 色はキャラクターエディタの判定プレビューと必ず同じにします。
// 以前はここだけ「緑＝押し合い / 青＝食らい」で、エディタは
// 「緑＝食らい / 青＝押し合い」と逆になっていました。エディタで
// 緑を伸ばして、ゲーム内で緑を見ながら確かめる…という一番よくある
// 使い方で、まったく別の四角形を見比べてしまう状態でした。
//
//   赤  … ヒットボックス（攻撃）
//   緑  … ハートボックス（食らい）
//   青  … プッシュボックス（押し合い）
//   黄  … 投げの間合い
//   橙  … 飛び道具
void DrawDebugOverlay(Renderer& r, const BattleSystem& bs) {
    const Color pushColor(60, 110, 220, 200);
    const Color hurtColor(40, 190, 90, 220);
    const Color hitColor(255, 60, 60, 240);
    const Color throwColor(255, 220, 60, 220);
    const Color projColor(255, 150, 40, 220);

    const Fighter* players[2] = {&bs.Player1, &bs.Player2};
    for (const Fighter* p : players) {
        DrawWorldRect(r, p->PushboxRect(), pushColor);
        for (const auto& hb : p->HurtboxRects()) DrawWorldRect(r, hb, hurtColor);
        for (const auto& hb : p->ActiveHitboxRects) DrawWorldRect(r, hb, hitColor);

        // 投げは四角形ではなく距離で判定するので、間合いを
        // 「その距離ぶんの横棒」として描きます。
        if (p->SM.CurrentState == CharState::Attack && p->CurrentMoveData &&
            p->CurrentMoveData->GuardType == Constants::GuardThrow) {
            double reach = p->CurrentMoveData->ThrowRange;
            RectBox range(p->PositionX + reach / 2.0 * p->Facing, p->PositionY - 20, reach, 40);
            DrawWorldRect(r, range, throwColor);
        }
    }
    for (const auto& proj : bs.Projectiles) DrawWorldRect(r, proj.HitboxRect(), projColor);

    // ---- 数値の表示 ----
    // 体力ゲージのすぐ下、左右にそれぞれのプレイヤーぶんを出します。
    auto drawInfo = [&](const Fighter& f, float x, bool rightAlign) {
        auto d = f.DebugInfo();
        // フレーム数は 99 で頭打ちにします（技が終わったあとも
        // 増え続ける数字が画面に出ていると読みにくいため）。
        int frame = std::min(99, d.frame);
        auto sign = [](int v) {
            return (v >= 0 ? std::string("+") : std::string("")) + std::to_string(v);
        };
        std::string line1 = d.state + (d.phase.empty() ? std::string() : " " + d.phase);
        std::string line2 = d.move.empty() ? std::string("-") : d.move;
        std::string line3 = "F:" + std::to_string(frame) +
                            " HS:" + std::to_string(d.hitstun) +
                            " BS:" + std::to_string(d.blockstun);
        // 出している技のフレームデータと硬直差。
        //   4/3/7 は 発生/持続/硬直、+4/-1 は ヒット/ガードの硬直差。
        std::string line4;
        if (!d.move.empty()) {
            line4 = std::to_string(d.startup) + "/" + std::to_string(d.active) + "/" +
                    std::to_string(d.recovery) + " " + sign(d.hitAdvantage) + "/" +
                    sign(d.blockAdvantage);
        }
        const auto& pal = GetPalette();
        float y = 44.0f;
        const std::string* lines[4] = {&line1, &line2, &line3, &line4};
        for (int i = 0; i < 4; ++i) {
            if (lines[i]->empty()) continue;
            if (rightAlign) DrawPixelTextRight(r, *lines[i], x, y + i * 8.0f, 1.0f, pal.ArenaLine);
            else DrawPixelText(r, *lines[i], x, y + i * 8.0f, 1.0f, pal.ArenaLine);
        }
    };
    drawInfo(bs.Player1, 3.0f, false);
    drawInfo(bs.Player2, VirtualW - 3.0f, true);
}

} // namespace kakuge

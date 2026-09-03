// =====================================================================
// platform/Screens.cpp - 各画面の見た目
// =====================================================================
// Game.cpp が「いつどの画面を出すか」を決めるのに対して、
// このファイルは「その画面がどう見えるか」だけを担当します。
// 分けておくと、見た目を調整したいときにここだけ見れば済みます。
//
// 座標は内部キャンバス上の数値です（基準 384x224。ウィンドウの形に
// 合わせて少し広がることがあるので、VirtualW / VirtualH を使います）。
// =====================================================================
#include <algorithm>
#include <cmath>
#include <string>

#include "platform/Camera.h"
#include "platform/Figure.h"
#include "platform/Font.h"
#include "platform/Game.h"
#include "platform/Hud.h"
#include "platform/Palette.h"

namespace kakuge {

// ---------------------------------------------------------------------
// 共通の部品
// ---------------------------------------------------------------------
void Game::DrawButtons() {
    const auto& pal = GetPalette();
    for (size_t i = 0; i < buttons_.size(); ++i) {
        const auto& b = buttons_[i];
        bool focused = (static_cast<int>(i) == focusIndex_);

        if (b.primary) {
            // 塗りつぶしボタン。選択中はさらに明るくします。
            Color fill = focused ? pal.Accent.Scaled(1.15) : pal.Accent;
            r_.FillRect(b.x, b.y, b.w, b.h, fill);
            // 上部にうっすら白を重ねて「つや」を出す（立体感の演出）
            r_.FillGradientRect(b.x, b.y, b.w, b.h * 0.45f,
                                Color(255, 255, 255, 70), Color(255, 255, 255, 0));
            r_.DrawRect(b.x, b.y, b.w, b.h, pal.Ink, 1.0f);
            DrawPixelTextCentered(r_, b.text, b.x, b.y, b.w, b.h, 1.0f, pal.OnAccent);
        } else {
            // 枠だけのボタン。選択中は下地を薄く塗ります。
            r_.FillRect(b.x, b.y, b.w, b.h, focused ? pal.PanelBg2 : pal.PanelBg);
            r_.DrawRect(b.x, b.y, b.w, b.h, focused ? pal.Accent : pal.Ink, 1.0f);
            DrawPixelTextCentered(r_, b.text, b.x, b.y, b.w, b.h, 1.0f, pal.Ink);
        }

        // 選択中の目印（左側の三角）。キーボードで操作している人にも
        // どこが選ばれているか一目で分かるようにするためです。
        if (focused) {
            Vec2 tri[3] = {
                {b.x - 8.0f, b.y + b.h / 2.0f - 4.0f},
                {b.x - 3.0f, b.y + b.h / 2.0f},
                {b.x - 8.0f, b.y + b.h / 2.0f + 4.0f},
            };
            r_.FillPolygon(tri, 3, pal.Accent);
        }
    }
}

void Game::DrawScreenHeader(const std::string& title, const std::string& subtitle) {
    const auto& pal = GetPalette();
    r_.FillRect(0, 0, VirtualW, 30, pal.Accent);
    r_.FillRect(0, 30, VirtualW, 2, pal.Ink);
    DrawPixelText(r_, title, 8, 8, 2.0f, pal.OnAccent);
    if (!subtitle.empty()) DrawPixelTextRight(r_, subtitle, VirtualW - 8, 12, 1.0f, pal.OnAccent);
}

// ---------------------------------------------------------------------
// タイトル画面
// ---------------------------------------------------------------------
void Game::DrawTitle() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);

    // 上部の赤い帯と、そこに載るタイトル文字
    r_.FillRect(0, 0, VirtualW, 74, pal.Accent);
    // 斜めの光沢。大きな赤い面にだけ使う演出です。
    Vec2 shine[4] = {{40, 0}, {96, 0}, {56, 74}, {0, 74}};
    r_.FillPolygon(shine, 4, Color(255, 255, 255, 28));
    r_.FillRect(0, 74, VirtualW, 2, pal.Ink);

    DrawPixelTextCentered(r_, "KAKUGE", 0, 16, VirtualW, 24, 3.0f, pal.OnAccent);
    DrawPixelTextCentered(r_, "2D FIGHTING GAME", 0, 48, VirtualW, 10, 1.0f, pal.OnAccent);

    DrawButtons();

    // 画面下の帯（操作の要約）
    r_.FillRect(0, VirtualH - 14, VirtualW, 14, pal.Ink);
    DrawPixelTextCentered(r_, "ARROW / WASD - MOVE   ENTER - SELECT",
                          0, VirtualH - 14, VirtualW, 14, 1.0f, pal.White);

    // キャラクターがいない場合の注意書き。
    // data フォルダのコピーに失敗したときに、何が起きているのか
    // 分からないまま「対戦を選んでも何も起きない」となるのを防ぎます。
    if (dm_->GetCharacterIds().empty()) {
        r_.FillRect(20, 80, VirtualW - 40, 20, Color(30, 28, 27));
        DrawPixelTextCentered(r_, "NO CHARACTER DATA - CHECK DATA FOLDER",
                              20, 80, VirtualW - 40, 20, 1.0f, Color(255, 120, 100));
    }
}

// ---------------------------------------------------------------------
// キャラクター選択画面
// ---------------------------------------------------------------------
void Game::DrawCharacterSelect() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);
    DrawScreenHeader("SELECT", isTrainingMode_ ? "TRAINING" : "VERSUS");

    const auto& ids = dm_->GetCharacterIds();
    if (ids.empty()) {
        DrawPixelTextCentered(r_, "NO CHARACTERS FOUND", 0, 100, VirtualW, 10, 1.0f, pal.Ink);
        DrawButtons();
        return;
    }
    selectIndex_ = std::clamp(selectIndex_, 0, static_cast<int>(ids.size()) - 1);

    // ---- キャラクターの一覧（タイル）----
    // 6 個ずつ横に並べ、あふれたら次の行へ折り返します。
    const float tileW = 46, tileH = 46, gap = 6;
    const int perRow = 6;
    float startX = (VirtualW - (perRow * tileW + (perRow - 1) * gap)) / 2.0f;
    float startY = 44;
    for (size_t i = 0; i < ids.size(); ++i) {
        int row = static_cast<int>(i) / perRow;
        int col = static_cast<int>(i) % perRow;
        float x = startX + col * (tileW + gap);
        float y = startY + row * (tileH + gap);
        bool selected = (static_cast<int>(i) == selectIndex_);

        r_.FillRect(x, y, tileW, tileH, selected ? pal.PanelBg2 : pal.PanelBg);
        r_.DrawRect(x, y, tileW, tileH, selected ? pal.Accent : pal.Ink, selected ? 2.0f : 1.0f);

        // タイルの中に小さなキャラクターを描く
        const CharacterStats* cs = dm_->GetCharacter(ids[i]);
        if (cs) {
            HumanoidPose pose;
            pose.heightScale = 0.36; // タイルに収まる大きさ
            pose.facing = 1;
            DrawHumanoid(r_, x + tileW / 2.0f, y + tileH - 5,
                         Color(cs->ColorR, cs->ColorG, cs->ColorB), pose);
        }
    }

    // ---- 選んでいるキャラクターの情報 ----
    const CharacterStats* cur = dm_->GetCharacter(ids[static_cast<size_t>(selectIndex_)]);
    if (cur) {
        DrawPixelTextCentered(r_, cur->Name, 0, 150, VirtualW, 10, 2.0f, pal.Ink);
        std::string stats = "HP " + std::to_string(cur->MaxHP) +
                            "   MOVES " + std::to_string(dm_->GetMoveset(cur->Id)
                                                             ? dm_->GetMoveset(cur->Id)->size() : 0);
        DrawPixelTextCentered(r_, stats, 0, 168, VirtualW, 8, 1.0f, pal.Ink55);
    }

    // ---- どちらを選んでいるかの表示 ----
    std::string step = (selectStep_ == 0) ? "PLAYER 1" : "PLAYER 2 (CPU)";
    r_.FillRect(0, 34, VirtualW, 8, pal.Ink);
    DrawPixelTextCentered(r_, step, 0, 34, VirtualW, 8, 1.0f, pal.White);

    // 1P が決まっていれば左下に表示
    if (!p1CharId_.empty()) {
        if (const CharacterStats* p1 = dm_->GetCharacter(p1CharId_)) {
            DrawPixelText(r_, "1P: " + p1->Name, 6, 178, 1.0f, pal.AccentDeep);
        }
    }

    DrawButtons();
    DrawPixelTextCentered(r_, "LEFT / RIGHT - CHOOSE     ENTER - DECIDE",
                          0, VirtualH - 12, VirtualW, 10, 1.0f, pal.Ink45);
}

// ---------------------------------------------------------------------
// 対戦前の演出
// ---------------------------------------------------------------------
void Game::DrawVS() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);

    const CharacterStats* p1 = dm_->GetCharacter(p1CharId_);
    const CharacterStats* p2 = dm_->GetCharacter(p2CharId_);

    // 2 人を左右対称に並べます。
    const float boxW = 80, boxH = 95, gapW = 80;
    float totalW = boxW * 2 + gapW;
    float leftX = (VirtualW - totalW) / 2.0f;
    float rightX = leftX + boxW + gapW;
    float boxY = VirtualH - 8 - boxH;

    auto drawSide = [&](const CharacterStats* cs, float x, int facing) {
        r_.FillRect(x, boxY, boxW, boxH, pal.PanelBg);
        r_.DrawRect(x, boxY, boxW, boxH, pal.Ink, 1.0f);
        if (!cs) return;
        HumanoidPose pose;
        pose.heightScale = 0.85;
        pose.facing = facing;
        DrawHumanoid(r_, x + boxW / 2.0f, boxY + boxH - 8,
                     Color(cs->ColorR, cs->ColorG, cs->ColorB), pose);
        DrawPixelTextCentered(r_, cs->Name, x, boxY - 12, boxW, 10, 1.0f, pal.Ink);
    };
    drawSide(p1, leftX, 1);
    drawSide(p2, rightX, -1);

    // 中央の「VS」。演出時間に合わせて少し大きくなります。
    float t = static_cast<float>(std::min(1.0, vsTimer_ / 0.4));
    float badgeSize = 40 + t * 8;
    float bx = (VirtualW - badgeSize) / 2.0f;
    float by = 100 - badgeSize / 2.0f;
    r_.FillRect(bx, by, badgeSize, badgeSize, pal.Accent);
    r_.FillGradientRect(bx, by, badgeSize, badgeSize * 0.45f,
                        Color(255, 255, 255, 80), Color(255, 255, 255, 0));
    r_.DrawRect(bx, by, badgeSize, badgeSize, pal.Ink, 2.0f);
    DrawPixelTextCentered(r_, "VS", bx, by, badgeSize, badgeSize, 3.0f, pal.OnAccent);
}

// ---------------------------------------------------------------------
// 対戦画面
// ---------------------------------------------------------------------
void Game::DrawGame() {
    const auto& pal = GetPalette();
    if (!battle_) { r_.Clear(pal.ArenaBg); return; }

    // ---- 画面揺れ ----
    // ランダムに全方向へブルブル震わせるのではなく、
    //   「まず攻撃された向きへ押される → 中央へ戻る」
    // という動きにします。そのほうが「その方向に殴られた」という
    // 手応えが出ますし、短時間でもはっきり伝わります。
    //
    // cos の 1.5 周期ぶんを使って、
    //   最初 = 攻撃方向へ最大 → 反対側へ小さく揺り戻し → 0
    // という減衰した往復を作っています。
    // 最後に整数へ丸めるのは、ドット絵をにじませないためです。
    if (battle_->ShakeFrames > 0 && battle_->ShakeTotalFrames > 0) {
        double decay = static_cast<double>(battle_->ShakeFrames) / battle_->ShakeTotalFrames;
        double progress = 1.0 - decay;             // 0（発生直後）→ 1（終わり）
        double phase = progress * 3.14159265 * 1.5; // 0 → 1.5π
        double amount = battle_->ShakeMagnitude * decay * std::cos(phase);
        double ox = amount * battle_->ShakeDirX;
        double oy = amount * battle_->ShakeVertical;
        r_.SetShake(static_cast<float>(std::round(ox)), static_cast<float>(std::round(oy)));
    } else {
        r_.SetShake(0, 0);
    }

    // ---- 背景 ----
    r_.Clear(pal.ArenaBg);
    // 空（上のほうを少し明るく）
    r_.FillGradientRect(0, 0, VirtualW, static_cast<float>(OriginY()),
                        Color(74, 72, 78), Color(48, 46, 48));

    // 奥の建物のシルエット。
    //
    // 2 段重ねにして、奥の層はカメラの動きに対してゆっくり、
    // 手前の層は速く流します（多重スクロール）。速さが違うと
    // 人の目は「奥行きがある」と感じます。
    // また、地面（Y=200）に対してキャラクターは 95px しかないため、
    // 画面上部が空いてしまいます。奥の層を高くして、その空白を
    // 埋めています（暗い色なので、手前のキャラクターの邪魔はしません）。
    struct Layer { double speed; int spacing; float minH; float stepH; int width; Color body; Color top; };
    const Layer layers[2] = {
        // 奥: ゆっくり流れる高いビル群
        {0.15, 78, 88.0f, 14.0f, 56, Color(45, 44, 48), Color(55, 53, 58)},
        // 手前: 速く流れる低いビル群
        {0.35, 62, 46.0f, 10.0f, 44, Color(34, 33, 36), Color(48, 46, 50)},
    };
    for (const auto& L : layers) {
        double parallax = -CameraDrawX() * L.speed;
        for (int i = -5; i <= 5; ++i) {
            float bx = static_cast<float>(VirtualW / 2 + i * L.spacing + parallax);
            // ビルの高さは i から決めます。乱数だと毎フレーム形が
            // 変わってちらついてしまうためです。
            float bh = L.minH + ((i * 37) % 5 + 5) % 5 * L.stepH;
            float bw = static_cast<float>(L.width);
            r_.FillRect(bx - bw / 2, static_cast<float>(OriginY()) - bh, bw, bh, L.body);
            r_.FillRect(bx - bw / 2, static_cast<float>(OriginY()) - bh, bw, 2, L.top);
        }
    }

    // 地面
    r_.FillRect(0, static_cast<float>(OriginY()), VirtualW,
                static_cast<float>(VirtualH - OriginY()), Color(70, 62, 58));
    r_.FillRect(0, static_cast<float>(OriginY()), VirtualW, 2, Color(120, 108, 100));
    // 床のタイル線。カメラと一緒に流れるので、動きが分かりやすくなります。
    for (int i = -12; i <= 12; ++i) {
        double worldX = std::floor(CameraDrawX() / 40.0) * 40.0 + i * 40.0;
        float sx = static_cast<float>(ToScreenX(worldX));
        if (sx < -4 || sx > VirtualW + 4) continue;
        r_.DrawLine(sx, static_cast<float>(OriginY()) + 3, sx, static_cast<float>(VirtualH),
                    Color(90, 80, 74), 1.0f);
    }

    // ステージの端（壁）。ここから先には進めないことを示します。
    for (double wall : {StageConstants::StageMinX, StageConstants::StageMaxX}) {
        float sx = static_cast<float>(ToScreenX(wall));
        if (sx < -2 || sx > VirtualW + 2) continue;
        r_.FillRect(sx - 1, 0, 2, static_cast<float>(VirtualH), Color(30, 28, 30, 160));
    }

    // ---- キャラクターと飛び道具 ----
    // 影を先に描いてからキャラクターを描きます。
    for (const Fighter* p : {&battle_->Player1, &battle_->Player2}) {
        float sx = static_cast<float>(ToScreenX(p->PositionX));
        float shadowW = 22.0f; // 倍率固定なのでそのままのピクセル数
        // 空中にいるほど影が小さくなると、高さが分かりやすくなります。
        double height = -p->PositionY;
        float shrink = static_cast<float>(std::max(0.4, 1.0 - height / 160.0));
        r_.FillEllipse(sx, static_cast<float>(OriginY()) + 1,
                       shadowW * shrink, 3.0f * shrink, Color(20, 18, 20, 110));
    }
    DrawFighter(r_, battle_->Player1);
    DrawFighter(r_, battle_->Player2);
    for (const auto& proj : battle_->Projectiles) DrawProjectile(r_, proj);

    // ---- 演出 ----
    for (const auto& fx : effects_) DrawEffect(r_, fx);

    // ---- 情報表示 ----
    // HUD は画面揺れの影響を受けないようにします
    //（ゲージが揺れると、かえって見づらくなるため）。
    r_.SetShake(0, 0);
    DrawHUD(r_, *battle_, lastP1Combo_, lastP2Combo_, std::max(p1ComboFade_, p2ComboFade_));
    for (const auto& fx : effects_) DrawCounterEdgeLabel(r_, fx);
    if (debugVisible_) DrawDebugOverlay(r_, *battle_);

    // ---- KO 表示 ----
    if (matchFinished_) {
        r_.FillRect(0, 88, VirtualW, 28, Color(20, 19, 18, 200));
        std::string text = battle_->IsDraw ? "DRAW"
                         : (battle_->Winner == &battle_->Player1 ? "K.O." : "YOU LOSE");
        DrawPixelTextCentered(r_, text, 0, 88, VirtualW, 28, 3.0f, pal.Accent);
    }

    if (paused_) DrawPauseMenu();
}

void Game::DrawPauseMenu() {
    const auto& pal = GetPalette();
    // 画面全体を薄暗くして、メニューを目立たせます。
    r_.FillRect(0, 0, VirtualW, VirtualH, Color(15, 14, 14, 190));
    r_.FillRect(VirtualW / 2.0f - 84, 56, 168, 18, pal.Accent);
    DrawPixelTextCentered(r_, "PAUSE", VirtualW / 2.0f - 84, 56, 168, 18, 2.0f, pal.OnAccent);
    DrawButtons();
    if (isTrainingMode_) {
        DrawPixelTextCentered(r_, "F1 - HITBOX DISPLAY", 0, VirtualH - 20, VirtualW, 10,
                              1.0f, pal.ArenaTextDim);
    }
}

// ---------------------------------------------------------------------
// 結果画面
// ---------------------------------------------------------------------
void Game::DrawResult() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);

    std::string title = lastResult_.isDraw ? "DRAW"
                      : (lastResult_.winnerIsPlayer ? "YOU WIN" : "YOU LOSE");
    DrawScreenHeader(title, isTrainingMode_ ? "TRAINING" : "VERSUS");

    // 成績を 3 つの枠に分けて表示します。
    struct Tile { const char* label; std::string value; };
    Tile tiles[3] = {
        {"MAX COMBO", std::to_string(lastResult_.maxCombo)},
        {"DAMAGE", std::to_string(lastResult_.damageDealt)},
        {"TIME LEFT", std::to_string(lastResult_.timeLeftSeconds)},
    };
    const float tw = 100, th = 46;
    float totalW = tw * 3 + 12 * 2;
    float x0 = (VirtualW - totalW) / 2.0f;
    for (int i = 0; i < 3; ++i) {
        float x = x0 + i * (tw + 12);
        r_.FillRect(x, 62, tw, th, pal.PanelBg);
        r_.DrawRect(x, 62, tw, th, pal.Ink, 1.0f);
        DrawPixelTextCentered(r_, tiles[i].label, x, 68, tw, 8, 1.0f, pal.Ink55);
        DrawPixelTextCentered(r_, tiles[i].value, x, 82, tw, 18, 2.0f, pal.AccentDeep);
    }

    DrawButtons();
}

// ---------------------------------------------------------------------
// 設定画面
// ---------------------------------------------------------------------
void Game::DrawSettings() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);
    DrawScreenHeader("SETTINGS", "");

    DrawPixelTextCentered(r_, "WINDOW SIZE", 0, 76, VirtualW, 10, 1.0f, pal.Ink55);

    const auto& presets = ResolutionPresets();
    int idx = std::clamp(pendingResIndex_, 0, static_cast<int>(presets.size()) - 1);
    const auto& p = presets[static_cast<size_t>(idx)];

    const float panelX = VirtualW / 2.0f - 96;
    r_.FillRect(panelX, 104, 192, 18, pal.PanelBg);
    r_.DrawRect(panelX, 104, 192, 18, pal.Ink, 1.0f);
    DrawPixelTextCentered(r_, p.label, panelX, 104, 192, 18, 1.0f, pal.Ink);
    DrawPixelTextCentered(r_, p.aspect, 0, 126, VirtualW, 8, 1.0f, pal.Ink45);

    DrawButtons();

    // 実際に使っている内部キャンバスの大きさを出します。ウィンドウの
    // 形に合わせて変わるので、固定の文字列にはできません。
    std::string canvas = "CANVAS " + std::to_string(VirtualW) + "X" +
                         std::to_string(VirtualH) + "  (NO BLACK BARS)";
    DrawPixelTextCentered(r_, canvas, 0, VirtualH - 24, VirtualW, 10, 1.0f, pal.Ink45);

    // 日本語をどのフォントで出しているか。エディタの字が読めないとき、
    // 「フォントを見つけられていない」のか「別の問題」なのかが分かります。
    std::string fontPath = JapaneseFontPath();
    std::string fontLine;
    if (fontPath.empty()) {
        fontLine = "JP FONT: BUILT-IN KANA (NO SYSTEM FONT FOUND)";
    } else {
        // 長いパスは末尾（ファイル名）だけ出します。
        size_t slash = fontPath.find_last_of("/\\");
        fontLine = "JP FONT: " + (slash == std::string::npos ? fontPath
                                                             : fontPath.substr(slash + 1));
    }
    DrawPixelTextCentered(r_, fontLine, 0, VirtualH - 13, VirtualW, 10, 1.0f, pal.Ink45);
}

// ---------------------------------------------------------------------
// 操作説明画面
// ---------------------------------------------------------------------
void Game::DrawControls() {
    const auto& pal = GetPalette();
    r_.Clear(pal.Bg);
    DrawScreenHeader("CONTROLS", "");

    // 表形式で「キー」と「操作」を並べます。
    // 左半分がキーボード、右半分がコントローラです。
    struct Row { const char* key; const char* action; };
    const Row keyboardRows[] = {
        {"A / D",      "WALK BACK / FWD"},
        {"S",          "CROUCH"},
        {"SPACE",      "JUMP"},
        {"S + BACK",   "CROUCH GUARD"},
        {"U / I / O",  "PUNCH  L/M/H"},
        {"J / K / L",  "KICK   L/M/H"},
        {"U + J",      "THROW"},
        {"D,D",        "DASH / STEP FWD"},
        {"236 + P",    "FIREBALL"},
        {"623 + P",    "ANTI-AIR SPECIAL"},
        {"214 + K",    "HURRICANE KICK"},
        {"236236 + P", "SUPER (NEEDS METER)"},
        {"ESC",        "PAUSE"},
        {"F1",         "HITBOX (TRAINING)"},
    };

    const float leftX = 8, leftActionX = 88;
    const float rightX = VirtualW / 2.0f + 8;
    float y = 43;

    DrawPixelText(r_, "KEYBOARD", leftX, 32, 1.0f, pal.Ink);
    for (const auto& row : keyboardRows) {
        DrawPixelText(r_, row.key, leftX, y, 1.0f, pal.AccentDeep);
        DrawPixelText(r_, row.action, leftActionX, y, 1.0f, pal.Ink70);
        y += 10;
    }

    // ---- コントローラ ----
    // 市販のパッドは役割どおり、自作の基板は 0 番から順に読みます
    //（詳しくは platform/Gamepad.h）。
    const Row padRows[] = {
        {"STICK/DPAD", "MOVE"},
        {"X / Y / RB", "PUNCH  L/M/H"},
        {"A / B / RT", "KICK   L/M/H"},
        {"LB",         "THROW"},
        {"START",      "CONFIRM (MENU)"},
        {"",           ""},
        {"BTN 0/1/2",  "PUNCH  L/M/H"},
        {"BTN 3/4/5",  "KICK   L/M/H"},
        {"BTN 6",      "THROW"},
    };
    DrawPixelText(r_, "GAMEPAD", rightX, 32, 1.0f, pal.Ink);
    y = 43;
    for (const auto& row : padRows) {
        if (row.key[0] != '\0') {
            DrawPixelText(r_, row.key, rightX, y, 1.0f, pal.AccentDeep);
            DrawPixelText(r_, row.action, rightX + 66, y, 1.0f, pal.Ink70);
        }
        y += 10;
    }
    // 番号読みの説明（自作コントローラ向け）。
    DrawPixelText(r_, "BTN N = UNMAPPED PAD (DIY BOARDS)", rightX, y, 1.0f, pal.Ink45);

    // 今つながっている機器を出します。コントローラが効かないとき、
    // 「そもそも認識されていないのか」がここで分かります。
    std::string padLine = pad_.IsConnected()
        ? std::string("PAD: ") + pad_.Name()
        : std::string("PAD: NOT CONNECTED");
    DrawPixelText(r_, padLine, rightX, y + 10, 1.0f, pal.Ink55);

    // コマンドの数字（テンキー表記）の意味を補足します。
    DrawPixelText(r_, "236 = DOWN, DOWN-FORWARD, FORWARD", leftX, 185, 1.0f, pal.Ink45);

    DrawButtons();
}

// ---------------------------------------------------------------------
// キャラクターエディタ
// ---------------------------------------------------------------------
// 中身は platform/Editor.cpp が全部持っているので、ここは橋渡しだけです。
void Game::DrawEditor() {
    if (editor_) editor_->Draw(r_);
}

} // namespace kakuge

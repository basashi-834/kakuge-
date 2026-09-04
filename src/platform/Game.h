// =====================================================================
// platform/Game.h - ゲーム全体の進行役（画面の切り替えとメインループ）
// =====================================================================
// このクラスがゲーム全体を回します。やることは 3 つ。
//
//   1. 今どの画面にいるかを覚える（タイトル / キャラ選択 / 対戦 / 結果）
//   2. キーボードの状態を読んで、対戦中なら BattleSystem に渡す
//   3. 今の画面に応じた絵を描く
//
// 固定タイムステップ
// ----------------
// このゲームの戦闘計算は必ず 1/60 秒きざみで進みます。パソコンの
// 性能や画面のリフレッシュレート（60Hz / 144Hz など）が違っても、
// 技の発生フレームやコンボの繋がりが変わってはいけないからです。
//
// そのために「たまった時間」を Accumulator に足していき、
// 1/60 秒たまるごとに 1 回ずつ計算する、という方式にしています。
//
//   経過時間を足す → 1/60 秒以上たまっていたら 1 回計算して 1/60 引く
//                    → まだたまっていたらもう 1 回…（の繰り返し）
//
// 画面の更新が遅れた場合は 1 フレームで複数回計算されるので、
// ゲームの速さは常に一定に保たれます。
// =====================================================================
#pragma once
#include <SDL.h>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/BattleSystem.h"
#include "engine/DataManager.h"
#include "engine/Settings.h"
#include "platform/Editor.h"
#include "platform/Figure.h"
#include "platform/Gamepad.h"
#include "platform/Renderer.h"

namespace kakuge {

// 画面の種類。
enum class Screen {
    Title,           // タイトル
    CharacterSelect, // キャラクター選択
    VS,              // 対戦前の演出
    Game,            // 対戦中
    Result,          // 結果
    Settings,        // 設定（解像度）
    Controls,        // 操作説明
    Editor           // キャラクターエディタ
};

// 画面上の押せるボタン 1 個。
// 「四角形の中でクリックされたか」を調べるだけの単純なものです。
// キーボードの上下でも選べるようにしてあります。
struct UiButton {
    float x = 0, y = 0, w = 0, h = 0;
    std::string text;
    bool primary = true;  // true なら赤い塗りつぶし、false なら枠だけ
    bool enabled = true;
    int action = 0;       // 押されたときに何をするかの番号

    bool HitTest(double px, double py) const {
        return enabled && px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

class Game {
public:
    // 起動と終了。Run() の中でメインループが回ります。
    bool Init();
    int Run();
    void Shutdown();

private:
    // ---- SDL まわり ----
    SDL_Window* window_ = nullptr;
    SDL_Renderer* sdlRenderer_ = nullptr;
    Renderer r_;
    bool running_ = false;

    // ---- データ ----
    std::unique_ptr<DataManager> dm_;
    Settings settings_;

    // ---- 画面の状態 ----
    Screen current_ = Screen::Title;
    std::vector<UiButton> buttons_;
    int focusIndex_ = 0;   // キーボードで選んでいるボタン
    double mouseVx_ = -1, mouseVy_ = -1; // マウス位置（内部キャンバス座標）
    bool pendingRelayout_ = false; // ウィンドウの大きさが変わった
    int laidOutW_ = 0, laidOutH_ = 0; // ボタンを並べたときのキャンバスの大きさ

    // ---- モード ----
    bool isTrainingMode_ = false;
    bool trainingAutoHeal_ = true;

    // ---- キャラクター選択 ----
    int selectStep_ = 0; // 0 = 1P を選ぶ、1 = 2P を選ぶ
    std::string p1CharId_, p2CharId_;
    int selectIndex_ = 0;

    // ---- 対戦前演出 ----
    double vsTimer_ = 0.0;

    // ---- 対戦中 ----
    std::unique_ptr<BattleSystem> battle_;
    std::unordered_set<SDL_Keycode> heldKeys_;
    Gamepad pad_; // USB コントローラ（つながっていなければ何もしない）
    bool debugVisible_ = false;
    bool paused_ = false;
    int pauseIndex_ = 0;
    std::vector<LiveEffect> effects_;
    bool matchFinished_ = false;
    // 試合が終わってから結果画面へ移るまでの余韻（秒）。
    // 試合ごとの値なので、必ず試合開始（StartMatch）で 0 に戻します。
    double matchEndDelay_ = 0.0;
    DummyMode p2DummyMode_ = DummyMode::CPU;
    double p1ComboFade_ = 0.0, p2ComboFade_ = 0.0;
    int lastP1Combo_ = 0, lastP2Combo_ = 0;

    // ---- 結果画面に出す情報 ----
    // 試合が終わると BattleSystem は作り直されるので、
    // 表示に必要な数値はここに写しておきます。
    struct ResultData {
        bool winnerIsPlayer = false;
        bool isDraw = false;
        int maxCombo = 0;
        int timeLeftSeconds = 0;
        int damageDealt = 0;
    } lastResult_;

    // ---- 設定画面 ----
    int pendingResIndex_ = 0;

    // ---- キャラクターエディタ ----
    // 画面を開いたときだけ作ります（使わない間はメモリを使わない）。
    std::unique_ptr<Editor> editor_;

    // ---- 時間の管理 ----
    std::chrono::steady_clock::time_point lastTick_;
    double accumulator_ = 0.0;
    static constexpr double FixedDt = 1.0 / 60.0; // 1 フレームの長さ

    // ---- メインループの中身 ----
    void HandleEvents();
    void Update(double dt);
    void Render();

    // ---- 入力 ----
    RawInput BuildPlayerInput() const;
    void OnKeyDown(SDL_Keycode key);
    void OnMouseMove(int windowX, int windowY);
    void OnMouseClick(int windowX, int windowY);
    void ActivateFocused();
    void DoAction(int action);

    // ---- 画面の切り替え ----
    void GoTitle();
    void GoCharacterSelect(bool training);
    void GoVS();
    void StartMatch();
    void GoResult();
    void GoSettings();
    void GoControls();
    void GoEditor();
    void ApplySettings();
    // 今の画面のボタンを並べ直す（画面切り替え時とリサイズ時）。
    void LayoutButtons();

    // ---- 各画面の描画（Screens.cpp）----
    void DrawTitle();
    void DrawCharacterSelect();
    void DrawVS();
    void DrawGame();
    void DrawPauseMenu();
    void DrawResult();
    void DrawSettings();
    void DrawControls();
    void DrawEditor();

    // ---- 共通の部品 ----
    void DrawButtons();
    void DrawScreenHeader(const std::string& title, const std::string& subtitle);
    void AddButton(float x, float y, float w, float h, const std::string& text,
                   int action, bool primary = true);

    // ウィンドウ座標 → 内部キャンバス座標の変換（マウス判定用）。
    void WindowToVirtual(int wx, int wy, double& vx, double& vy) const;
};

} // namespace kakuge

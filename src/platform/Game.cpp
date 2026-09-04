// =====================================================================
// platform/Game.cpp - 起動・メインループ・入力・画面の切り替え
// =====================================================================
#include "platform/Game.h"

#include <algorithm>
#include <cmath>

#include "platform/Camera.h"
#include "platform/Font.h"
#include "platform/Hud.h"
#include "platform/Palette.h"
#include "platform/Paths.h"
#include "platform/Sprite.h"

namespace kakuge {

// ボタンが押されたときの動作の番号。
// 数字のままだと何のことか分からないので、名前を付けています。
enum Action {
    ActNone = 0,
    ActVersus,        // 対戦モードへ
    ActTraining,      // トレーニングモードへ
    ActControls,      // 操作説明へ
    ActSettings,      // 設定へ
    ActQuit,          // ゲーム終了
    ActBackToTitle,   // タイトルへ戻る
    ActSelectConfirm, // キャラクター決定
    ActSelectBack,    // ひとつ前へ戻る
    ActRematch,       // もう一度戦う
    ActResumeGame,    // ポーズ解除
    ActQuitMatch,     // 試合をやめてタイトルへ
    ActToggleHeal,    // 自動回復の切り替え
    ActDummyMode,     // 練習相手の行動の切り替え
    ActResPrev,       // 解像度をひとつ前へ
    ActResNext,       // 解像度をひとつ次へ
    ActResApply,      // 解像度を適用
    ActEditor         // キャラクターエディタへ
};

// ---------------------------------------------------------------------
// 起動
// ---------------------------------------------------------------------
bool Game::Init() {
    // SDL の初期化。使うのは映像とタイマーだけです。
    // 音を出す機能（SDL_INIT_AUDIO）は今回使っていないので初期化しません。
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kakuge",
                                 "SDL の初期化に失敗しました。", nullptr);
        return false;
    }

    // データを読み込みます。ウィンドウを作る前に読むのは、
    // 保存されている解像度の設定を使いたいからです。
    dm_ = std::make_unique<DataManager>(GetBaseDataDir(), GetUserDataDir());
    dm_->ReloadAll();
    settings_ = dm_->LoadSettings();

    // パソコンに入っている日本語フォントを探します。
    // 見つかれば漢字もひらがなもそのまま出せます。見つからなくても
    // 内蔵のカナ表示に切り替わるだけなので、失敗しても先へ進みます。
    InitJapaneseFont(GetBaseDataDir().string());

    window_ = SDL_CreateWindow("Kakuge - 2D Fighting Game",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               settings_.Width, settings_.Height,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kakuge",
                                 "ウィンドウを作成できませんでした。", nullptr);
        return false;
    }

    // SDL_RENDERER_PRESENTVSYNC は「画面の更新に合わせて表示する」設定。
    // これを付けると映像のちらつき（ティアリング）が消え、
    // ついでに CPU の無駄な回転も抑えられます。
    sdlRenderer_ = SDL_CreateRenderer(window_, -1,
                                      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlRenderer_) {
        // GPU が使えない環境（古い PC、リモートデスクトップなど）では
        // ソフトウェア描画に切り替えて、とにかく起動できるようにします。
        sdlRenderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!sdlRenderer_ || !r_.Init(sdlRenderer_)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kakuge",
                                 "描画の準備に失敗しました。", nullptr);
        return false;
    }

    // 保存されている解像度に合う項目を設定画面の初期値にします。
    const auto& presets = ResolutionPresets();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].width == settings_.Width && presets[i].height == settings_.Height) {
            pendingResIndex_ = static_cast<int>(i);
            break;
        }
    }

    // 手描きの絵（スプライト）を読み込みます。
    // data/sprites/<キャラクター ID>/sprites.json が無いキャラクターは、
    // これまでどおり図形で描かれます（platform/Sprite.h）。
    GetSprites().LoadAll(sdlRenderer_, dm_->BaseDir, dm_->UserDir, dm_->GetCharacterIds());

    // USB コントローラ（つながっていなくても失敗しません）。
    pad_.Init();

    GoTitle();
    lastTick_ = std::chrono::steady_clock::now();
    running_ = true;
    return true;
}

void Game::Shutdown() {
    pad_.Shutdown();
    // 画像は SDL_Renderer より先に片付けます（逆にすると、
    // すでに壊した描画機に向かって解放を頼むことになります）。
    GetSprites().Unload();
    r_.Shutdown();
    if (sdlRenderer_) { SDL_DestroyRenderer(sdlRenderer_); sdlRenderer_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    SDL_Quit();
}

// ---------------------------------------------------------------------
// メインループ
// ---------------------------------------------------------------------
int Game::Run() {
    while (running_) {
        // 前回からの経過時間を測る
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;
        // 一時的に処理が止まったとき（ウィンドウを動かした等）に
        // 大量のフレームをまとめて処理して早送りにならないよう、
        // 1 回に進める時間の上限を決めておきます。
        if (dt > 0.25) dt = 0.25;

        HandleEvents();
        Update(dt);
        Render();
    }
    return 0;
}

// ---------------------------------------------------------------------
// 入力イベントの処理
// ---------------------------------------------------------------------
void Game::HandleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: // ウィンドウの × ボタン
                running_ = false;
                break;
            case SDL_KEYDOWN:
                // e.key.repeat は「キーを押しっぱなしにしたときの
                // 自動連打」。ゲームでは邪魔なので無視します。
                if (e.key.repeat == 0) OnKeyDown(e.key.keysym.sym);
                heldKeys_.insert(e.key.keysym.sym);
                break;
            case SDL_KEYUP:
                heldKeys_.erase(e.key.keysym.sym);
                break;
            case SDL_TEXTINPUT:
                // 名前などの文字入力（エディタ画面でのみ使います）。
                // SDL は日本語入力なども含めて UTF-8 で渡してくれます。
                if (current_ == Screen::Editor && editor_) editor_->HandleText(e.text.text);
                break;
            case SDL_MOUSEMOTION:
                OnMouseMove(e.motion.x, e.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) OnMouseClick(e.button.x, e.button.y);
                break;
            case SDL_MOUSEBUTTONUP:
                // エディタで判定をつかんでいたら、ここで離します。
                if (e.button.button == SDL_BUTTON_LEFT && current_ == Screen::Editor && editor_) {
                    editor_->HandleMouseUp();
                }
                break;
            case SDL_WINDOWEVENT:
                // ウィンドウからフォーカスが外れたら、押しているキーを
                // 全部離した扱いにします。そうしないと、別の画面を
                // 触っている間ずっと前進し続けてしまいます。
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) heldKeys_.clear();
                // 大きさが変わると内部キャンバスの大きさも変わるので、
                // ボタンの位置を計算し直します。
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    pendingRelayout_ = true;
                }
                break;
            default:
                break;
        }
        // コントローラの抜き差しを見張ります。
        pad_.HandleEvent(e);
    }

    // コントローラの十字キー・ボタンを、メニュー操作のキーとして扱います。
    // キーボードとまったく同じ処理に流し込むので、どちらでも
    // 同じようにメニューを操作できます。
    for (SDL_Keycode key : pad_.PollMenuKeys()) {
        // 対戦中はメニュー操作を送りません。しゃがみガードのつもりで
        // 下を入れたときに、ポーズメニューが動いてしまうためです
        //（ポーズ中は送ります）。
        if (current_ == Screen::Game && !paused_) continue;
        OnKeyDown(key);
    }
}

// キーボードとコントローラの今の状態を、エンジンが理解できる形にまとめる。
//
// 操作キー（6 ボタン式）
//   移動   A（左） D（右） S（下） Space（ジャンプ）
//   パンチ U（弱） I（中） O（強）
//   キック J（弱） K（中） L（強）
//   投げ   U ＋ J の同時押し
RawInput Game::BuildPlayerInput() const {
    RawInput input;
    auto held = [this](SDL_Keycode k) { return heldKeys_.count(k) > 0; };
    input.Left = held(SDLK_a);
    input.Right = held(SDLK_d);
    input.Down = held(SDLK_s);
    input.Up = held(SDLK_SPACE);
    input.Buttons.LP = held(SDLK_u);
    input.Buttons.MP = held(SDLK_i);
    input.Buttons.HP = held(SDLK_o);
    input.Buttons.LK = held(SDLK_j);
    input.Buttons.MK = held(SDLK_k);
    input.Buttons.HK = held(SDLK_l);

    // コントローラの入力を重ねます。「どちらかが押していれば押している」
    // という足し方なので、キーボードとコントローラを併用できます
    //（右手はパッド、左手はキーボード、といった使い方もできます）。
    RawInput padInput = pad_.Poll();
    input.Left = input.Left || padInput.Left;
    input.Right = input.Right || padInput.Right;
    input.Down = input.Down || padInput.Down;
    input.Up = input.Up || padInput.Up;
    for (const char* name : {"LP", "MP", "HP", "LK", "MK", "HK"}) {
        if (padInput.Buttons.Get(name)) input.Buttons.Set(name, true);
    }
    return input;
}

void Game::OnKeyDown(SDL_Keycode key) {
    // キャラクターエディタは独自のキー操作を持つので、先に渡します。
    if (current_ == Screen::Editor && editor_) {
        // Shift を押しているかは、SDL に今の状態を聞きます。
        bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
        if (editor_->HandleKey(key, shift)) {
            if (editor_->WantsExit()) {
                editor_->ClearExitRequest();
                // 編集結果をゲーム側にも反映させるため読み直します。
                dm_->ReloadAll();
                // 絵も読み直します（新しく作ったキャラクターの絵を
                // 置いた直後でも、ゲームを起動し直さずに反映されます）。
                GetSprites().LoadAll(sdlRenderer_, dm_->BaseDir, dm_->UserDir,
                                     dm_->GetCharacterIds());
                GoTitle();
            }
            return;
        }
        return; // エディタ画面では、ほかのメニュー操作は行わない
    }

    // 対戦画面だけは専用のキーがあります。
    if (current_ == Screen::Game) {
        if (key == SDLK_ESCAPE) {
            paused_ = !paused_;
            pauseIndex_ = 0;
            if (paused_) heldKeys_.clear(); // ポーズ中に前進し続けないように
            return;
        }
        if (key == SDLK_F1 && isTrainingMode_) {
            debugVisible_ = !debugVisible_;
            return;
        }
        if (!paused_) return; // 対戦中は以下のメニュー操作を無効に
    }

    // キャラクター選択画面では、左右キーは「選ぶキャラクターの移動」に
    // 使います（ボタン間の移動ではなく）。上下キーとタブでボタンを選びます。
    if (current_ == Screen::CharacterSelect) {
        const auto& ids = dm_->GetCharacterIds();
        int count = static_cast<int>(ids.size());
        if (count > 0 && (key == SDLK_LEFT || key == SDLK_a)) {
            selectIndex_ = (selectIndex_ - 1 + count) % count;
            return;
        }
        if (count > 0 && (key == SDLK_RIGHT || key == SDLK_d)) {
            selectIndex_ = (selectIndex_ + 1) % count;
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_z || key == SDLK_SPACE) {
            DoAction(ActSelectConfirm);
            return;
        }
        if (key == SDLK_ESCAPE || key == SDLK_x) {
            DoAction(ActSelectBack);
            return;
        }
    }

    // メニューのキーボード操作
    if (buttons_.empty()) return;
    if (key == SDLK_UP || key == SDLK_LEFT || key == SDLK_w) {
        focusIndex_ = (focusIndex_ - 1 + static_cast<int>(buttons_.size())) %
                      static_cast<int>(buttons_.size());
    } else if (key == SDLK_DOWN || key == SDLK_RIGHT || key == SDLK_s) {
        focusIndex_ = (focusIndex_ + 1) % static_cast<int>(buttons_.size());
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE ||
               key == SDLK_z) {
        ActivateFocused();
    } else if (key == SDLK_ESCAPE) {
        if (current_ != Screen::Title) GoTitle();
    }
}

void Game::WindowToVirtual(int wx, int wy, double& vx, double& vy) const {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    if (ww <= 0 || wh <= 0) { vx = vy = -1; return; }
    // Renderer::EndFrame とまったく同じ計算を使います。
    // ここがずれると、見えているボタンと押せる場所がずれてしまいます。
    CanvasLayout layout = ComputeCanvasLayout(ww, wh);
    if (layout.scale <= 0) { vx = vy = -1; return; }
    double offX = (ww - layout.width * layout.scale) / 2.0;
    double offY = (wh - layout.height * layout.scale) / 2.0;
    vx = (wx - offX) / layout.scale;
    vy = (wy - offY) / layout.scale;
}

void Game::OnMouseMove(int windowX, int windowY) {
    WindowToVirtual(windowX, windowY, mouseVx_, mouseVy_);
    // エディタ画面では、判定の四角形をマウスで動かせます。
    if (current_ == Screen::Editor && editor_) {
        editor_->HandleMouseMove(mouseVx_, mouseVy_);
        return;
    }
    // マウスを乗せたボタンを選択状態にします（キーボードと連動）。
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i].HitTest(mouseVx_, mouseVy_)) {
            focusIndex_ = static_cast<int>(i);
            break;
        }
    }
}

void Game::OnMouseClick(int windowX, int windowY) {
    double vx, vy;
    WindowToVirtual(windowX, windowY, vx, vy);
    if (current_ == Screen::Editor && editor_) {
        editor_->HandleMouseDown(vx, vy);
        return;
    }
    for (const auto& b : buttons_) {
        if (b.HitTest(vx, vy)) { DoAction(b.action); return; }
    }
}

void Game::ActivateFocused() {
    if (focusIndex_ < 0 || focusIndex_ >= static_cast<int>(buttons_.size())) return;
    DoAction(buttons_[focusIndex_].action);
}

// ボタンが押されたときの処理をまとめた場所。
void Game::DoAction(int action) {
    switch (action) {
        case ActVersus: GoCharacterSelect(false); break;
        case ActTraining: GoCharacterSelect(true); break;
        case ActControls: GoControls(); break;
        case ActSettings: GoSettings(); break;
        case ActEditor: GoEditor(); break;
        case ActQuit: running_ = false; break;
        case ActBackToTitle: GoTitle(); break;

        case ActSelectConfirm: {
            const auto& ids = dm_->GetCharacterIds();
            if (ids.empty()) return;
            int idx = std::clamp(selectIndex_, 0, static_cast<int>(ids.size()) - 1);
            if (selectStep_ == 0) {
                p1CharId_ = ids[static_cast<size_t>(idx)];
                selectStep_ = 1;
            } else {
                p2CharId_ = ids[static_cast<size_t>(idx)];
                GoVS();
            }
            break;
        }
        case ActSelectBack:
            if (selectStep_ == 1) selectStep_ = 0;
            else GoTitle();
            break;

        case ActRematch: StartMatch(); break;
        case ActResumeGame: paused_ = false; break;
        case ActQuitMatch: GoTitle(); break;

        case ActToggleHeal:
            trainingAutoHeal_ = !trainingAutoHeal_;
            if (battle_) battle_->TrainingAutoHeal = trainingAutoHeal_;
            break;

        case ActDummyMode: {
            // CPU → 立ち → しゃがみ → ジャンプ → ガード → CPU ... と順に切り替え
            int m = (static_cast<int>(p2DummyMode_) + 1) % 5;
            p2DummyMode_ = static_cast<DummyMode>(m);
            if (battle_ && battle_->CpuAI) battle_->CpuAI->Mode = p2DummyMode_;
            break;
        }

        case ActResPrev: {
            int n = static_cast<int>(ResolutionPresets().size());
            pendingResIndex_ = (pendingResIndex_ - 1 + n) % n;
            break;
        }
        case ActResNext: {
            int n = static_cast<int>(ResolutionPresets().size());
            pendingResIndex_ = (pendingResIndex_ + 1) % n;
            break;
        }
        case ActResApply: ApplySettings(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------
// 画面の切り替え
// ---------------------------------------------------------------------
void Game::AddButton(float x, float y, float w, float h, const std::string& text,
                     int action, bool primary) {
    UiButton b;
    b.x = x; b.y = y; b.w = w; b.h = h;
    b.text = text;
    b.action = action;
    b.primary = primary;
    buttons_.push_back(b);
}

// 今の画面に合わせてボタンを並べ直す。
//
// 画面を切り替えたときだけでなく、ウィンドウの大きさが変わったときにも
// 呼びます。内部キャンバスの大きさはウィンドウの形に合わせて変わるので
//（黒い余白を出さないため）、そのつど位置を計算し直さないと、
// ボタンが中央からずれたり画面外に出たりしてしまいます。
//
// 画面の状態（選んでいるキャラクターなど）はここでは触りません。
// リサイズのたびに選択がリセットされては困るからです。
void Game::LayoutButtons() {
    buttons_.clear();
    const float halfW = VirtualW / 2.0f;
    switch (current_) {
        case Screen::Title: {
            // 6 つのボタンを縦に並べ、上の赤い帯（高さ 74）と
            // 画面下の帯（高さ 14）の間で縦中央に置きます。
            const float bw = 150, bh = 15, step = 18;
            const float blockH = step * 5 + bh;
            const float bx = halfW - bw / 2.0f;
            const float by = 74 + (VirtualH - 14 - 74 - blockH) / 2.0f;
            AddButton(bx, by, bw, bh, "VERSUS (VS CPU)", ActVersus, true);
            AddButton(bx, by + step, bw, bh, "TRAINING", ActTraining, false);
            AddButton(bx, by + step * 2, bw, bh, "CHARACTER EDITOR", ActEditor, false);
            AddButton(bx, by + step * 3, bw, bh, "CONTROLS", ActControls, false);
            AddButton(bx, by + step * 4, bw, bh, "SETTINGS", ActSettings, false);
            AddButton(bx, by + step * 5, bw, bh, "EXIT", ActQuit, false);
            break;
        }
        case Screen::CharacterSelect:
            AddButton(halfW - 76, VirtualH - 34, 72, 16, "SELECT", ActSelectConfirm, true);
            AddButton(halfW + 4, VirtualH - 34, 72, 16, "BACK", ActSelectBack, false);
            break;
        case Screen::Result: {
            const float bw = 100, bh = 18, bx = halfW - bw / 2.0f;
            AddButton(bx, VirtualH - 64, bw, bh, "REMATCH", ActRematch, true);
            AddButton(bx, VirtualH - 42, bw, bh, "TITLE", ActBackToTitle, false);
            break;
        }
        case Screen::Settings:
            AddButton(halfW - 122, 104, 20, 18, "<", ActResPrev, false);
            AddButton(halfW + 102, 104, 20, 18, ">", ActResNext, false);
            AddButton(halfW - 50, 150, 100, 18, "APPLY", ActResApply, true);
            AddButton(halfW - 50, 174, 100, 18, "BACK", ActBackToTitle, false);
            break;
        case Screen::Controls:
            AddButton(halfW - 50, VirtualH - 28, 100, 18, "BACK", ActBackToTitle, false);
            break;
        default:
            break; // 対戦中とエディタはボタンを使わない（ポーズ中は毎フレーム作る）
    }
    if (focusIndex_ >= static_cast<int>(buttons_.size())) focusIndex_ = 0;
}

void Game::GoTitle() {
    current_ = Screen::Title;
    battle_.reset();
    focusIndex_ = 0;
    LayoutButtons();
}

void Game::GoCharacterSelect(bool training) {
    current_ = Screen::CharacterSelect;
    isTrainingMode_ = training;
    selectStep_ = 0;
    selectIndex_ = 0;
    p1CharId_.clear();
    p2CharId_.clear();
    focusIndex_ = 0;
    LayoutButtons();
}

void Game::GoVS() {
    current_ = Screen::VS;
    vsTimer_ = 0.0;
    buttons_.clear();
}

void Game::StartMatch() {
    const CharacterStats* p1 = dm_->GetCharacter(p1CharId_);
    const CharacterStats* p2 = dm_->GetCharacter(p2CharId_);
    if (!p1 || !p2) { GoTitle(); return; }

    battle_ = std::make_unique<BattleSystem>();
    battle_->StartMatch(*p1, dm_->GetMoveset(p1CharId_),
                        *p2, dm_->GetMoveset(p2CharId_), 99);
    battle_->TrainingMode = isTrainingMode_;
    battle_->TrainingAutoHeal = trainingAutoHeal_;
    if (battle_->CpuAI) battle_->CpuAI->Mode = p2DummyMode_;

    ResetCamera(battle_->Player1.PositionX, battle_->Player2.PositionX);
    effects_.clear();
    matchFinished_ = false;
    paused_ = false;
    p1ComboFade_ = p2ComboFade_ = 0.0;
    lastP1Combo_ = lastP2Combo_ = 0;
    accumulator_ = 0.0;
    heldKeys_.clear();
    current_ = Screen::Game;
    buttons_.clear();
}

void Game::GoResult() {
    // 試合中の数値を結果画面用に写しておきます。
    if (battle_) {
        lastResult_.isDraw = battle_->IsDraw;
        lastResult_.winnerIsPlayer = (battle_->Winner == &battle_->Player1);
        lastResult_.maxCombo = battle_->P1MaxCombo;
        lastResult_.timeLeftSeconds = static_cast<int>(std::ceil(battle_->FramesLeft / 60.0));
        lastResult_.damageDealt = battle_->Player2.Stats.MaxHP - battle_->Player2.CurrentHP;
    }
    current_ = Screen::Result;
    focusIndex_ = 0;
    LayoutButtons();
}

void Game::GoSettings() {
    current_ = Screen::Settings;
    focusIndex_ = 2;
    LayoutButtons();
}

void Game::GoControls() {
    current_ = Screen::Controls;
    focusIndex_ = 0;
    LayoutButtons();
}

// キャラクターエディタを開く。
// 開くたびに作り直すのは、ほかの画面で編集内容が変わっている
// 可能性があるためです（新規作成したキャラクターなど）。
void Game::GoEditor() {
    current_ = Screen::Editor;
    buttons_.clear();
    battle_.reset();
    editor_ = std::make_unique<Editor>();
    editor_->Open(dm_.get());
}

void Game::ApplySettings() {
    const auto& presets = ResolutionPresets();
    int idx = std::clamp(pendingResIndex_, 0, static_cast<int>(presets.size()) - 1);
    settings_.Width = presets[static_cast<size_t>(idx)].width;
    settings_.Height = presets[static_cast<size_t>(idx)].height;
    SDL_SetWindowSize(window_, settings_.Width, settings_.Height);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    dm_->SaveSettings(settings_); // 次回の起動でも同じ大きさになるよう保存
}

// ---------------------------------------------------------------------
// 更新（1 フレームぶんの計算）
// ---------------------------------------------------------------------
void Game::Update(double dt) {
    // SDL の文字入力は、エディタで名前を編集しているときだけ有効に
    // します。常に有効だと、対戦中のキーまで文字として届いてしまい、
    // 無駄な処理が増えます（IME が勝手に開くこともあります）。
    bool wantText = (current_ == Screen::Editor && editor_ && editor_->IsTextEditing());
    if (wantText && !SDL_IsTextInputActive()) SDL_StartTextInput();
    else if (!wantText && SDL_IsTextInputActive()) SDL_StopTextInput();

    if (current_ == Screen::VS) {
        // 対戦前の演出。1.2 秒たったら試合開始。
        vsTimer_ += dt;
        if (vsTimer_ > 1.2) StartMatch();
        return;
    }

    if (current_ != Screen::Game || !battle_) return;

    if (paused_) {
        // ポーズ中はメニューだけ組み立てて、戦闘計算は止めます。
        buttons_.clear();
        const float bw = 140, bh = 16, bx = (VirtualW - bw) / 2.0f;
        float by = 84;
        AddButton(bx, by, bw, bh, "RESUME", ActResumeGame, true);
        if (isTrainingMode_) {
            AddButton(bx, by + 20, bw, bh,
                      std::string("AUTO HEAL: ") + (trainingAutoHeal_ ? "ON" : "OFF"),
                      ActToggleHeal, false);
            const char* modeNames[5] = {"CPU", "STAND", "CROUCH", "JUMP", "GUARD"};
            AddButton(bx, by + 40, bw, bh,
                      std::string("DUMMY: ") + modeNames[static_cast<int>(p2DummyMode_)],
                      ActDummyMode, false);
            AddButton(bx, by + 60, bw, bh, "QUIT MATCH", ActQuitMatch, false);
        } else {
            AddButton(bx, by + 20, bw, bh, "QUIT MATCH", ActQuitMatch, false);
        }
        if (focusIndex_ >= static_cast<int>(buttons_.size())) focusIndex_ = 0;
        return;
    }

    buttons_.clear();

    // ---- 固定タイムステップ ----
    // ここが「どんな PC でもゲームの速さが変わらない」仕組みです。
    accumulator_ += dt;
    RawInput input = BuildPlayerInput();
    int steps = 0;
    while (accumulator_ >= FixedDt) {
        accumulator_ -= FixedDt;
        // 極端に処理が遅れたときに、追いつこうとして何十回も計算し
        // さらに遅くなる「死のスパイラル」を防ぐための上限です。
        if (++steps > 5) { accumulator_ = 0.0; break; }

        battle_->Update(FixedDt, input);
        UpdateCamera(battle_->Player1.PositionX, battle_->Player2.PositionX, FixedDt);

        // BattleSystem が出した演出の注文を、表示用の一覧に移します。
        for (const auto& e : battle_->AllEffects) {
            effects_.push_back({e.kind, e.x, e.y, 0.0, e.side});
        }

        // コンボ表示。数が増えた瞬間に濃さを 1.0 に戻し、
        // あとは時間とともに薄くしていきます。
        if (battle_->P1ComboCount > lastP1Combo_) p1ComboFade_ = 1.0;
        if (battle_->P2ComboCount > lastP2Combo_) p2ComboFade_ = 1.0;
        lastP1Combo_ = battle_->P1ComboCount;
        lastP2Combo_ = battle_->P2ComboCount;

        if (!battle_->MatchActive && !matchFinished_) {
            matchFinished_ = true;
        }
    }

    // ---- 演出の寿命管理 ----
    for (auto& fx : effects_) fx.age += dt;
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const LiveEffect& fx) {
                                      return fx.age > GetEffectStyle(fx.kind).duration;
                                  }),
                   effects_.end());

    p1ComboFade_ = std::max(0.0, p1ComboFade_ - dt * 0.7);
    p2ComboFade_ = std::max(0.0, p2ComboFade_ - dt * 0.7);

    // 試合が終わったら、少し余韻を置いてから結果画面へ。
    // すぐ切り替えると、KO の瞬間が見えません。
    if (matchFinished_) {
        static double endDelay = 0.0;
        endDelay += dt;
        if (endDelay > 1.5) { endDelay = 0.0; GoResult(); }
    }
}

// ---------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------
void Game::Render() {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    // 内部キャンバスの大きさはウィンドウの形で決まるので、
    // 描き始める前に BeginFrame へ渡します。
    r_.BeginFrame(ww, wh);

    // ここで VirtualW / VirtualH が確定します。前のフレームと違って
    // いたら、この時点でボタンを並べ直します。
    //
    // ウィンドウのリサイズイベントを見るだけでは足りません。起動直後の
    // 1 回目もキャンバスの大きさが決まるのはここなので、それまでは
    // 基準値（384x224）で並べたボタンが中央からずれたままになります。
    // 「キャンバスの大きさが変わったら並べ直す」という条件にすれば、
    // 起動時もリサイズ時も 1 つの仕組みで正しく揃います。
    if (pendingRelayout_ || VirtualW != laidOutW_ || VirtualH != laidOutH_) {
        pendingRelayout_ = false;
        laidOutW_ = VirtualW;
        laidOutH_ = VirtualH;
        LayoutButtons();
    }
    switch (current_) {
        case Screen::Title: DrawTitle(); break;
        case Screen::CharacterSelect: DrawCharacterSelect(); break;
        case Screen::VS: DrawVS(); break;
        case Screen::Game: DrawGame(); break;
        case Screen::Result: DrawResult(); break;
        case Screen::Settings: DrawSettings(); break;
        case Screen::Controls: DrawControls(); break;
        case Screen::Editor: DrawEditor(); break;
    }
    r_.EndFrame(ww, wh);
}

} // namespace kakuge

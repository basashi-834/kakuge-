// =====================================================================
// platform/Gamepad.h - USB コントローラの入力
// =====================================================================
// キーボードだけでなく、USB につないだコントローラでも遊べるように
// するための部分です。アーケードスティックや、自作のレバーレス
// コントローラ（PKB 32u4 など、ATmega32U4 を使った基板）を想定して
// います。
//
// 2 種類の道を用意してあります
// -------------------------
// SDL には、コントローラを扱う仕組みが 2 つあります。
//
//   (1) SDL_GameController … 「A ボタン」「十字キー上」のように
//       役割の名前で読める、便利なほう。ただし SDL が持っている
//       対応表にその機種が載っている必要があります。市販の
//       Xbox / PlayStation のパッドはほぼ載っています。
//
//   (2) SDL_Joystick … 「0 番のボタン」「1 番の軸」のように、
//       番号で読む素朴なほう。対応表が要らないので、どんな機器でも
//       とりあえず読めます。
//
// 自作の基板は当然 SDL の対応表には載っていないので、(1) では
// まったく反応しません。そこで「(1) で開けるなら (1)、無理なら (2)」
// という順で試します。これで、市販パッドは正しい配置で、自作基板も
// 番号順の配置で動きます。
//
// ボタンの割り当て
// -------------
//   市販パッド … X=弱P  Y=中P  RB=強P  A=弱K  B=中K  RT=強K
//                 LB=投げ（弱P＋弱K の同時押しと同じ）
//   番号読み   … 0=弱P 1=中P 2=強P 3=弱K 4=中K 5=強K 6=投げ
//                 （多くの自作基板は、配線した順に 0 番から並びます）
//
// 抜き差しについて
// -------------
// ゲームの起動中につないでも、抜いても大丈夫なようにしてあります。
// SDL がその都度イベントを送ってくるので、それを受けて開き直します。
// =====================================================================
#pragma once
#include <SDL.h>

#include <vector>

#include "engine/InputSystem.h"

namespace kakuge {

class Gamepad {
public:
    // SDL_Init のあとに 1 回呼びます。つながっている機器を開きます。
    void Init();
    void Shutdown();

    // SDL のイベントを渡す。抜き差しの検出に使います。
    void HandleEvent(const SDL_Event& e);

    // 今の状態を読み取る。つながっていなければ何も押していない状態を返します。
    RawInput Poll() const;

    // 今フレーム「新しく押された」ものを、メニュー操作用のキーとして
    // 取り出す。Update の中で毎フレーム 1 回だけ呼んでください
    //（呼ぶたびに「前回の状態」を更新するため）。
    //
    // 返すのは SDL のキーコードです。こうしておくと、メニュー側は
    // キーボードと同じ処理をそのまま使えます。
    //   十字キー → 矢印キー / 決定 → Enter / 戻る → Esc
    std::vector<SDL_Keycode> PollMenuKeys();

    bool IsConnected() const { return controller_ != nullptr || joystick_ != nullptr; }
    // 画面に出す機器の名前（つながっていなければ空）。
    const char* Name() const;

private:
    void OpenFirstAvailable();
    void Close();

    SDL_GameController* controller_ = nullptr; // (1) 役割で読めるとき
    SDL_Joystick* joystick_ = nullptr;         // (2) 番号で読むとき
    SDL_JoystickID instanceId_ = -1;

    // メニュー操作の「押した瞬間」を見るために、前フレームの
    // 押し状態を覚えておきます。
    struct MenuState {
        bool up = false, down = false, left = false, right = false;
        bool confirm = false, cancel = false;
    };
    MenuState prevMenu_;
};

} // namespace kakuge

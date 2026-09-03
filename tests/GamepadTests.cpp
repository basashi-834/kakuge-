// =====================================================================
// tests/GamepadTests.cpp - コントローラ入力の自動テスト
// =====================================================================
// 実物のコントローラを挿さずに、Gamepad の割り当てを検証します。
//
// どうやって実物なしで試すのか
// -------------------------
// SDL2 には「仮想ジョイスティック」という仕組みがあります
//（SDL_JoystickAttachVirtual、SDL 2.0.14 以降）。プログラムから
// 「ボタン 3 を押した」「ハットを左に倒した」と指示できる、
// 実体のない機器を作れます。SDL から見ると本物と区別がつかないので、
// Gamepad 側は何も知らずにそのまま動きます。
//
// ここで検証しているのは、自作基板（PKB 32u4 など）が通る
// 「番号読み」の経路です。市販パッドが通る SDL_GameController の
// 経路は、SDL 内部の対応表を使うため、実物でしか確かめられません。
//
// 実行方法:  build/bin/GamepadTests
// 画面は出しませんが、SDL の初期化のため映像機能を使います。
// 画面の無い環境では SDL_VIDEODRIVER=dummy を付けてください。
// =====================================================================
#include <SDL.h>
#include <cstdio>
#include <string>
#include "platform/Gamepad.h"
using namespace kakuge;

int failures = 0;
void check(const char* what, bool ok) {
    printf("%-46s %s\n", what, ok ? "[OK]" : "[NG]");
    if (!ok) failures++;
}

int main() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError()); return 1;
    }
    // 軸 2 本・ボタン 10 個・ハット 1 個。自作のレバーレス基板を想定。
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_ARCADE_STICK, 2, 10, 1);
    if (index < 0) { printf("attach failed: %s\n", SDL_GetError()); return 1; }

    Gamepad pad;
    pad.Init();
    SDL_JoystickUpdate();
    check("コントローラを認識する", pad.IsConnected());

    SDL_Joystick* js = SDL_JoystickOpen(index);

    auto press = [&](int button, bool down) {
        SDL_JoystickSetVirtualButton(js, button, down ? SDL_PRESSED : SDL_RELEASED);
        SDL_JoystickUpdate();
    };
    auto hat = [&](Uint8 value) {
        SDL_JoystickSetVirtualHat(js, 0, value);
        SDL_JoystickUpdate();
    };
    auto axis = [&](int a, int v) {
        SDL_JoystickSetVirtualAxis(js, a, static_cast<Sint16>(v));
        SDL_JoystickUpdate();
    };

    press(0, true);
    check("ボタン 0 が弱パンチになる", pad.Poll().Buttons.LP);
    press(0, false);
    press(5, true);
    check("ボタン 5 が強キックになる", pad.Poll().Buttons.HK);
    press(5, false);
    press(6, true);
    {
        RawInput in = pad.Poll();
        check("ボタン 6 が投げ（弱P＋弱K）になる", in.Buttons.LP && in.Buttons.LK);
    }
    press(6, false);

    hat(SDL_HAT_LEFT);
    check("ハット左が Left になる", pad.Poll().Left);
    hat(SDL_HAT_DOWN);
    check("ハット下が Down になる", pad.Poll().Down);
    hat(SDL_HAT_CENTERED);
    check("ハット中央では何も入らない", !pad.Poll().Left && !pad.Poll().Down);

    axis(0, 30000);
    check("軸を大きく倒すと Right になる", pad.Poll().Right);
    axis(0, 8000);
    check("軸の遊びの範囲では反応しない", !pad.Poll().Right);
    axis(0, 0);

    // メニュー操作のキーが「押した瞬間」だけ出るか
    pad.PollMenuKeys(); // 初期状態をそろえる
    hat(SDL_HAT_DOWN);
    {
        auto keys = pad.PollMenuKeys();
        bool found = false;
        for (auto k : keys) if (k == SDLK_DOWN) found = true;
        check("メニュー: 下を入れると DOWN が 1 回出る", found);
        keys = pad.PollMenuKeys();
        check("メニュー: 押しっぱなしでは連射しない", keys.empty());
    }
    hat(SDL_HAT_CENTERED);
    press(0, true);
    {
        auto keys = pad.PollMenuKeys();
        bool found = false;
        for (auto k : keys) if (k == SDLK_RETURN) found = true;
        check("メニュー: 弱パンチで決定できる", found);
    }
    press(0, false);
    pad.PollMenuKeys();

    // 抜いたときの挙動
    SDL_JoystickClose(js);
    SDL_JoystickDetachVirtual(index);
    SDL_Event e;
    while (SDL_PollEvent(&e)) pad.HandleEvent(e);
    check("抜いたら「つながっていない」に戻る", !pad.IsConnected());
    {
        RawInput in = pad.Poll();
        check("抜けている間は何も押されていない", !in.Left && !in.Buttons.LP);
    }

    pad.Shutdown();
    SDL_Quit();
    printf("\n=== %s ===\n", failures == 0 ? "ALL PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}

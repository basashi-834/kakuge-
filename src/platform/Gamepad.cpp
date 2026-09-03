// =====================================================================
// platform/Gamepad.cpp - Gamepad.h の中身
// =====================================================================
#include "platform/Gamepad.h"

#include <cmath>

namespace kakuge {

namespace {

// アナログスティックの「遊び」。
//
// アナログスティックは、手を離していても完全に 0 には戻りません。
// そのまま読むと、触っていないのにキャラクターがじりじり歩き出します。
// そこで、中央から一定の範囲は「倒していない」とみなします。
//
// SDL の軸の値は -32768〜32767。その約 半分（16000）を境にすると、
// 「はっきり倒したときだけ反応する」という、格闘ゲーム向きの
// きびきびした操作感になります。小さくすると反応は早くなりますが、
// 斜めに入りやすくなり、しゃがみガードのつもりで
// しゃがみ歩きになる、といった誤爆が増えます。
constexpr int kAxisDeadZone = 16000;

// 番号で読むときの、ボタン番号と役割の対応。
// 自作の基板は、たいてい配線した順に 0 番から並びます。
constexpr int kRawLP = 0, kRawMP = 1, kRawHP = 2;
constexpr int kRawLK = 3, kRawMK = 4, kRawHK = 5;
constexpr int kRawThrow = 6;   // 投げ（弱P＋弱K の同時押しと同じ扱い）
constexpr int kRawConfirm = 7; // メニューの決定
constexpr int kRawCancel = 8;  // メニューの取り消し

} // namespace

void Gamepad::Init() {
    // ここで初めてジョイスティックの仕組みを起動します。
    // SDL_Init で一緒に初期化しないのは、コントローラの検出に
    // 少し時間がかかることがあり、ウィンドウが出るのを
    // 遅らせたくないためです。
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    }
    // ウィンドウが後ろに回っていてもコントローラを読めるようにします。
    // 対戦中に別のウィンドウをうっかり触っても操作が切れません。
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    OpenFirstAvailable();
}

void Gamepad::Shutdown() {
    Close();
}

void Gamepad::Close() {
    if (controller_) { SDL_GameControllerClose(controller_); controller_ = nullptr; }
    if (joystick_) { SDL_JoystickClose(joystick_); joystick_ = nullptr; }
    instanceId_ = -1;
    prevMenu_ = MenuState();
}

void Gamepad::OpenFirstAvailable() {
    if (IsConnected()) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        // まずは「役割で読めるほう」を試します。
        if (SDL_IsGameController(i)) {
            controller_ = SDL_GameControllerOpen(i);
            if (controller_) {
                instanceId_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_));
                return;
            }
        }
        // だめなら番号で読みます。自作の基板はこちらになります。
        joystick_ = SDL_JoystickOpen(i);
        if (joystick_) {
            instanceId_ = SDL_JoystickInstanceID(joystick_);
            return;
        }
    }
}

void Gamepad::HandleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_JOYDEVICEADDED:
        case SDL_CONTROLLERDEVICEADDED:
            // まだ何も開いていなければ、つながった機器を開きます。
            OpenFirstAvailable();
            break;
        case SDL_JOYDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEREMOVED:
            // 抜かれたのが「今使っている機器」なら閉じて、
            // ほかにつながっているものがあればそちらへ移ります。
            if (e.jdevice.which == instanceId_) {
                Close();
                OpenFirstAvailable();
            }
            break;
        default:
            break;
    }
}

const char* Gamepad::Name() const {
    if (controller_) return SDL_GameControllerName(controller_);
    if (joystick_) return SDL_JoystickName(joystick_);
    return "";
}

RawInput Gamepad::Poll() const {
    RawInput in;
    if (controller_) {
        auto btn = [this](SDL_GameControllerButton b) {
            return SDL_GameControllerGetButton(controller_, b) != 0;
        };
        auto axis = [this](SDL_GameControllerAxis a) {
            return SDL_GameControllerGetAxis(controller_, a);
        };

        // 方向は十字キーとスティックの両方を受け付けます
        //（どちらで操作しても同じように動くほうが親切です）。
        in.Left = btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                  axis(SDL_CONTROLLER_AXIS_LEFTX) < -kAxisDeadZone;
        in.Right = btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                   axis(SDL_CONTROLLER_AXIS_LEFTX) > kAxisDeadZone;
        in.Down = btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                  axis(SDL_CONTROLLER_AXIS_LEFTY) > kAxisDeadZone;
        in.Up = btn(SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                axis(SDL_CONTROLLER_AXIS_LEFTY) < -kAxisDeadZone;

        in.Buttons.LP = btn(SDL_CONTROLLER_BUTTON_X);
        in.Buttons.MP = btn(SDL_CONTROLLER_BUTTON_Y);
        in.Buttons.HP = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        in.Buttons.LK = btn(SDL_CONTROLLER_BUTTON_A);
        in.Buttons.MK = btn(SDL_CONTROLLER_BUTTON_B);
        // 強キックは右トリガー。アナログなので、半分以上引いたら
        // 押したことにします。
        in.Buttons.HK = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;

        // 投げ（LB）は、弱P と弱K を同時に押したことにします。
        // Fighter 側が「LP と LK の同時押し」を投げとして扱うので、
        // ここで専用の扱いをする必要はありません。
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
            in.Buttons.LP = true;
            in.Buttons.LK = true;
        }
        return in;
    }

    if (joystick_) {
        auto btn = [this](int index) {
            return index < SDL_JoystickNumButtons(joystick_) &&
                   SDL_JoystickGetButton(joystick_, index) != 0;
        };

        // 十字キー（ハットスイッチ）。レバーレスの基板では、
        // 方向がハットとして出てくることが多いです。
        if (SDL_JoystickNumHats(joystick_) > 0) {
            Uint8 hat = SDL_JoystickGetHat(joystick_, 0);
            in.Left = (hat & SDL_HAT_LEFT) != 0;
            in.Right = (hat & SDL_HAT_RIGHT) != 0;
            in.Up = (hat & SDL_HAT_UP) != 0;
            in.Down = (hat & SDL_HAT_DOWN) != 0;
        }
        // 軸でも方向が出る機器があるので、両方を受け付けて
        // どちらかが入っていれば押しているものとします。
        if (SDL_JoystickNumAxes(joystick_) >= 2) {
            int x = SDL_JoystickGetAxis(joystick_, 0);
            int y = SDL_JoystickGetAxis(joystick_, 1);
            if (x < -kAxisDeadZone) in.Left = true;
            if (x > kAxisDeadZone) in.Right = true;
            if (y < -kAxisDeadZone) in.Up = true;
            if (y > kAxisDeadZone) in.Down = true;
        }

        in.Buttons.LP = btn(kRawLP);
        in.Buttons.MP = btn(kRawMP);
        in.Buttons.HP = btn(kRawHP);
        in.Buttons.LK = btn(kRawLK);
        in.Buttons.MK = btn(kRawMK);
        in.Buttons.HK = btn(kRawHK);
        if (btn(kRawThrow)) { in.Buttons.LP = true; in.Buttons.LK = true; }
    }
    return in;
}

std::vector<SDL_Keycode> Gamepad::PollMenuKeys() {
    std::vector<SDL_Keycode> keys;
    if (!IsConnected()) { prevMenu_ = MenuState(); return keys; }

    RawInput now = Poll();
    MenuState state;
    state.up = now.Up;
    state.down = now.Down;
    state.left = now.Left;
    state.right = now.Right;

    if (controller_) {
        state.confirm = SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_A) ||
                        SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_START);
        state.cancel = SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_B) ||
                       SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_BACK);
    } else if (joystick_) {
        auto btn = [this](int index) {
            return index < SDL_JoystickNumButtons(joystick_) &&
                   SDL_JoystickGetButton(joystick_, index) != 0;
        };
        // 番号読みのときは、弱パンチでも決定できるようにします。
        // 7 番・8 番が無い基板でもメニューを進められるようにするためです。
        state.confirm = btn(kRawConfirm) || btn(kRawLP);
        state.cancel = btn(kRawCancel) || btn(kRawMP);
    }

    // 「押した瞬間」だけを取り出します。押しっぱなしで送り続けると、
    // メニューの選択が一瞬で一番下まで飛んでしまいます。
    auto pressed = [](bool nowHeld, bool wasHeld) { return nowHeld && !wasHeld; };
    if (pressed(state.up, prevMenu_.up)) keys.push_back(SDLK_UP);
    if (pressed(state.down, prevMenu_.down)) keys.push_back(SDLK_DOWN);
    if (pressed(state.left, prevMenu_.left)) keys.push_back(SDLK_LEFT);
    if (pressed(state.right, prevMenu_.right)) keys.push_back(SDLK_RIGHT);
    if (pressed(state.confirm, prevMenu_.confirm)) keys.push_back(SDLK_RETURN);
    if (pressed(state.cancel, prevMenu_.cancel)) keys.push_back(SDLK_ESCAPE);

    prevMenu_ = state;
    return keys;
}

} // namespace kakuge

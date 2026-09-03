// =====================================================================
// engine/InputSystem.h - 入力の受け取りと、コマンド技の判定
// =====================================================================
// このファイルには 3 つの部品が入っています。
//
//   RawInput      … 「今このフレームで押されているもの」の一覧。
//                    キーボードから作られたものでも、CPU が考えて
//                    作ったものでも、まったく同じ形をしています。
//                    だから Fighter は「人間かCPUか」を知らずに済みます。
//
//   InputBuffer   … 直近 20 フレームぶんの入力の履歴。
//                    波動拳（↓ ↘ → + パンチ）のように、複数フレームに
//                    またがる入力を判定するために必要です。
//
//   CommandParser … その履歴を見て「波動拳コマンドが成立したか」を
//                    判定する部分。
//
// レバー方向は「テンキー表記」という格闘ゲーム界の慣習で数字にします。
//   7 8 9      7=左上  8=上    9=右上
//   4 5 6      4=後ろ  5=中立  6=前
//   1 2 3      1=左下  2=下    3=右下
// 大事なのは 4 と 6 が「左右」ではなく「後ろ・前」だという点です。
// キャラクターが左を向いていれば、左キーが 6（前）になります。
// この変換のおかげで、技のデータは向きを気にせず 1 通り書けば済みます。
// =====================================================================
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "engine/Constants.h"

namespace kakuge {

// ---------------------------------------------------------------------
// 攻撃ボタン（6 ボタン式）
// ---------------------------------------------------------------------
//   LP / MP / HP … 弱・中・強パンチ (Light/Medium/Heavy Punch)
//   LK / MK / HK … 弱・中・強キック (Kick)
//   Throw        … 投げ。専用のキーは無く、LP と LK の同時押しで
//                  Fighter が内部的に作り出す「仮想ボタン」です。
struct ButtonsHeld {
    bool LP = false, MP = false, HP = false, LK = false, MK = false, HK = false, Throw = false;

    // 名前（文字列）でアクセスできるようにしておくと、
    // 技データの "button": "HP" という指定をそのまま使えます。
    bool Get(const std::string& name) const {
        if (name == "LP") return LP;
        if (name == "MP") return MP;
        if (name == "HP") return HP;
        if (name == "LK") return LK;
        if (name == "MK") return MK;
        if (name == "HK") return HK;
        if (name == "Throw") return Throw;
        return false;
    }
    void Set(const std::string& name, bool v) {
        if (name == "LP") LP = v;
        else if (name == "MP") MP = v;
        else if (name == "HP") HP = v;
        else if (name == "LK") LK = v;
        else if (name == "MK") MK = v;
        else if (name == "HK") HK = v;
        else if (name == "Throw") Throw = v;
    }
};

// このフレームの入力そのもの。
struct RawInput {
    bool Left = false, Right = false, Down = false, Up = false;
    ButtonsHeld Buttons;
};

// 入力履歴 1 行ぶん。
//   frame   … 何フレーム目の入力か
//   digit   … レバー方向（テンキー表記の 1-9）
//   buttons … そのフレームに「新しく押された」ボタンの名前
//             （押しっぱなしは含めません。押しっぱなしを含めると
//               ボタンを押している間ずっと技が出続けてしまいます）
struct InputHistoryEntry {
    int frame = 0;
    int digit = 5;
    std::vector<std::string> buttons;
};

class InputBuffer {
public:
    std::deque<InputHistoryEntry> History;
    static constexpr int Length = 20; // 覚えておくフレーム数（約 0.33 秒）

    // 上下左右の押し状態と向きから、テンキー表記の数字を作る。
    static int ComputeDigit(bool left, bool right, bool down, bool up, int facing) {
        bool forward = right, back = left;
        if (facing == Constants::FacingLeft) { forward = left; back = right; }
        if (down && forward) return 3;
        if (down && back) return 1;
        if (up && forward) return 9;
        if (up && back) return 7;
        if (down) return 2;
        if (up) return 8;
        if (forward) return 6;
        if (back) return 4;
        return 5; // 何も入れていない（ニュートラル）
    }

    // 1 フレームぶん記録する。20 件を超えたら古いほうから捨てます。
    void RecordFrame(int frameNumber, int digit, const std::vector<std::string>& buttonsPressed) {
        History.push_back({frameNumber, digit, buttonsPressed});
        while (static_cast<int>(History.size()) > Length) History.pop_front();
    }

    void Clear() { History.clear(); }
};

// ---------------------------------------------------------------------
// コマンド技の判定
// ---------------------------------------------------------------------
struct CommandParser {
    // 必殺技のボタン指定には "AnyP"（どのパンチでも）/ "AnyK"（どのキック
    // でも）という特別な値が使えます。本物の格闘ゲームでも、波動拳は
    // 弱中強どのパンチでも出せますよね。それを表現するためのものです。
    static bool ButtonSatisfies(const std::string& required, const std::string& pressed) {
        if (required == "AnyP") return pressed == "LP" || pressed == "MP" || pressed == "HP";
        if (required == "AnyK") return pressed == "LK" || pressed == "MK" || pressed == "HK";
        return required == pressed;
    }

    // コマンド文字列（例 "236"）を数字の並びに変換する。
    // 数字以外の文字（空白や区切り記号）は無視するので、
    // "2 3 6" のように読みやすく書いても同じ意味になります。
    static std::vector<int> ParseDigits(const std::string& inputCommand) {
        std::vector<int> digits;
        for (char c : inputCommand) {
            if (c >= '1' && c <= '9') digits.push_back(c - '0');
        }
        return digits;
    }

    // コマンドが成立しているか判定する。
    //
    // 判定の考え方（ここが格闘ゲームの心臓部です）:
    //   1. 直近 window フレーム（既定 16）の履歴だけを見る。
    //   2. その履歴を古いほうから順に見て、コマンドの数字（例 2→3→6）が
    //      「この順番で現れるか」を調べる。連続している必要はありません。
    //      2,2,2,3,3,6 のように同じ方向が続いても、間に 5（ニュートラル）が
    //      挟まっても成立します。人間の入力は必ずブレるので、
    //      厳密に連続を要求すると誰も技を出せなくなります。
    //   3. 最後の数字（6）が成立したフレームから 8 フレーム以内に
    //      対象のボタンが押されていれば成立。この「猶予」があるおかげで、
    //      レバーとボタンを完全に同時に押さなくても技が出ます。
    static bool Matches(const InputBuffer& buffer, const std::string& inputCommand,
                        const std::string& button, int window) {
        if (inputCommand.empty()) return false;
        std::vector<int> digits = ParseDigits(inputCommand);
        if (digits.empty()) return false;
        if (buffer.History.empty()) return false;

        // 1. 直近 window フレームぶんだけ抜き出す
        int lastFrame = buffer.History.back().frame;
        std::vector<const InputHistoryEntry*> relevant;
        for (const auto& entry : buffer.History) {
            if ((lastFrame - entry.frame) <= window) relevant.push_back(&entry);
        }

        // 2. 数字が順番どおりに現れるかを調べる
        size_t ptr = 0;       // 今どこまで一致したか
        int matchFrame = -1;  // 最後に一致したフレーム番号
        for (const auto* entry : relevant) {
            if (ptr < digits.size() && entry->digit == digits[ptr]) {
                ptr += 1;
                matchFrame = entry->frame;
                if (ptr >= digits.size()) break; // 全部そろった
            }
        }
        if (ptr < digits.size()) return false; // 途中までしか入っていない

        // 3. そのあとボタンが押されたか
        const int buttonGrace = 8; // 猶予フレーム
        for (const auto* entry : relevant) {
            if (entry->frame >= matchFrame && (entry->frame - matchFrame) <= buttonGrace) {
                for (const auto& b : entry->buttons) {
                    if (ButtonSatisfies(button, b)) return true;
                }
            }
        }
        return false;
    }
};

} // namespace kakuge

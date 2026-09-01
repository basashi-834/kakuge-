// engine/InputSystem.h
// Raw input snapshot + rolling history + motion-command recognition. Raw
// per-frame input is passed around as a small struct with Left/Right/
// Down/Up plus a ButtonsHeld sub-struct - intentionally the same shape
// whether it comes from real keyboard state (GameScreen) or CPUAI's
// synthesized input, so Fighter never has to know which.
// 1:1 port of InputSystem/InputSystem.ps1.
#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include "Constants.h"

namespace kakuge {

// Classic 6-button scheme (LP/MP/HP/LK/MK/HK), plus a synthetic Throw
// (pressed by holding LP+LK together - see Fighter::FrameStep) rather than
// a dedicated key. Move data's "button" field names one of these six
// directly for normals, or the sentinel "AnyP"/"AnyK" for specials/supers
// that should fire off any punch or kick button (see
// CommandParser::ButtonSatisfies).
struct ButtonsHeld {
    bool LP = false, MP = false, HP = false, LK = false, MK = false, HK = false, Throw = false;

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

struct RawInput {
    bool Left = false, Right = false, Down = false, Up = false;
    ButtonsHeld Buttons;
};

struct InputHistoryEntry {
    int frame = 0;
    int digit = 5;
    std::vector<std::string> buttons;
};

class InputBuffer {
public:
    std::deque<InputHistoryEntry> History;
    static constexpr int Length = 20;

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
        return 5;
    }

    void RecordFrame(int frameNumber, int digit, const std::vector<std::string>& buttonsPressed) {
        History.push_back({frameNumber, digit, buttonsPressed});
        while (static_cast<int>(History.size()) > Length) History.pop_front();
    }

    void Clear() { History.clear(); }
};

struct CommandParser {
    // "AnyP"/"AnyK" let a special/super move's motion+button requirement
    // be satisfied by any of the three punch or kick buttons, matching how
    // real fighting games let you throw a fireball with whichever punch
    // strength you like rather than binding it to one exact button.
    static bool ButtonSatisfies(const std::string& required, const std::string& pressed) {
        if (required == "AnyP") return pressed == "LP" || pressed == "MP" || pressed == "HP";
        if (required == "AnyK") return pressed == "LK" || pressed == "MK" || pressed == "HK";
        return required == pressed;
    }

    static const std::unordered_map<std::string, std::vector<int>>& Motions() {
        static const std::unordered_map<std::string, std::vector<int>> m = {
            {"236", {2, 3, 6}},
            {"214", {2, 1, 4}},
            {"623", {6, 2, 3}},
            {"236236", {2, 3, 6, 2, 3, 6}},
        };
        return m;
    }

    static bool Matches(const InputBuffer& buffer, const std::string& inputCommand, const std::string& button, int window) {
        if (inputCommand.empty()) return false;
        const auto& motions = Motions();
        auto it = motions.find(inputCommand);
        if (it == motions.end()) return false;
        const auto& digits = it->second;
        if (buffer.History.empty()) return false;

        int lastFrame = buffer.History.back().frame;
        std::vector<const InputHistoryEntry*> relevant;
        for (const auto& entry : buffer.History) {
            if ((lastFrame - entry.frame) <= window) relevant.push_back(&entry);
        }

        size_t ptr = 0;
        int matchFrame = -1;
        for (const auto* entry : relevant) {
            if (ptr < digits.size() && entry->digit == digits[ptr]) {
                ptr += 1;
                matchFrame = entry->frame;
                if (ptr >= digits.size()) break;
            }
        }
        if (ptr < digits.size()) return false;

        const int buttonGrace = 8;
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

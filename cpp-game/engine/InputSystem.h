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

struct ButtonsHeld {
    bool Light = false, Medium = false, Heavy = false, Special = false, Super = false, Throw = false;

    bool Get(const std::string& name) const {
        if (name == "Light") return Light;
        if (name == "Medium") return Medium;
        if (name == "Heavy") return Heavy;
        if (name == "Special") return Special;
        if (name == "Super") return Super;
        if (name == "Throw") return Throw;
        return false;
    }
    void Set(const std::string& name, bool v) {
        if (name == "Light") Light = v;
        else if (name == "Medium") Medium = v;
        else if (name == "Heavy") Heavy = v;
        else if (name == "Special") Special = v;
        else if (name == "Super") Super = v;
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
                    if (b == button) return true;
                }
            }
        }
        return false;
    }
};

} // namespace kakuge

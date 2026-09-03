// engine/Settings.h
// New: display settings (resolution + aspect ratio) persisted alongside
// character/move data, so the Settings screen (added per user request:
// "画面比率を4:3、16:9にできる設定画面" / "解像度は320x200から1920x1080まで")
// survives a restart.
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace kakuge {

struct ResolutionPreset {
    std::string label;
    int width;
    int height;
    std::string aspect; // "4:3" or "16:9" (informational, shown in the UI)
};

// Presets spanning the requested 320x200 - 1920x1080 range, each tagged
// with its exact aspect ratio so the Settings screen can group them.
inline const std::vector<ResolutionPreset>& ResolutionPresets() {
    static const std::vector<ResolutionPreset> presets = {
        {"320 x 200",   320,  200,  "16:10 (CGA)"},
        {"320 x 240",   320,  240,  "4:3"},
        {"640 x 480",   640,  480,  "4:3"},
        {"800 x 600",   800,  600,  "4:3"},
        {"1024 x 768",  1024, 768,  "4:3"},
        {"1280 x 720",  1280, 720,  "16:9"},
        {"1366 x 768",  1366, 768,  "16:9"},
        {"1600 x 900",  1600, 900,  "16:9"},
        {"1920 x 1080", 1920, 1080, "16:9"},
    };
    return presets;
}

struct Settings {
    int Width = 1280;
    int Height = 720;

    static Settings FromJson(const nlohmann::json& obj) {
        Settings s;
        s.Width = obj.value("width", 1280);
        s.Height = obj.value("height", 720);
        if (s.Width < 320) s.Width = 320;
        if (s.Width > 1920) s.Width = 1920;
        if (s.Height < 200) s.Height = 200;
        if (s.Height > 1080) s.Height = 1080;
        return s;
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["width"] = Width;
        j["height"] = Height;
        return j;
    }
};

} // namespace kakuge

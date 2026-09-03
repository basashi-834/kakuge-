// =====================================================================
// engine/Settings.h - 画面設定（ウィンドウの大きさ）の保存
// =====================================================================
// 設定画面で選んだ解像度を、次回の起動時にも覚えておくためのものです。
// 保存先はユーザーフォルダ（Windows なら %APPDATA%\Kakuge\settings.json）で、
// ゲーム本体のフォルダには一切書き込みません。
// Program Files などの書き込み権限が無い場所にインストールされても
// 設定を保存できるようにするためです。
//
// 内部の描画は常に 384x224 で行い、それをここで選んだ大きさまで
// 整数倍で拡大して表示します。どの解像度を選んでも、絵の見た目
// （ドットの並び）は変わりません。大きく映るだけです。
// =====================================================================
#pragma once
#include <string>
#include <vector>

#include "core/Json.h"

namespace kakuge {

struct ResolutionPreset {
    std::string label;
    int width;
    int height;
    std::string aspect; // 画面比率（表示用）
};

// 選べる解像度の一覧。320x200 から 1920x1080 まで、
// 4:3 と 16:9 の両方を用意しています。
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

    static Settings FromJson(const Json& obj) {
        Settings s;
        s.Width = obj.GetInt("width", 1280);
        s.Height = obj.GetInt("height", 720);
        // 設定ファイルが壊れていても、とんでもない大きさの
        // ウィンドウが開かないように範囲を制限します。
        if (s.Width < 320) s.Width = 320;
        if (s.Width > 1920) s.Width = 1920;
        if (s.Height < 200) s.Height = 200;
        if (s.Height > 1080) s.Height = 1080;
        return s;
    }

    Json ToJson() const {
        Json j = Json::MakeObject();
        j.Set("width", Json(Width));
        j.Set("height", Json(Height));
        return j;
    }
};

} // namespace kakuge

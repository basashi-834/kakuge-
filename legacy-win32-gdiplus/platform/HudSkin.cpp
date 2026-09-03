// platform/HudSkin.cpp
#include "HudSkin.h"
#include <vector>

using namespace Gdiplus;

namespace kakuge {

namespace {
std::unique_ptr<HudSkin> g_Skin;
std::string g_SkinKey;

std::unique_ptr<Image> TryLoad(const fs::path& baseDir, const fs::path& userDir, const std::string& name) {
    for (const fs::path& dir : {userDir, baseDir}) {
        fs::path p = dir / "hud" / name;
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;
        auto img = std::make_unique<Image>(p.wstring().c_str());
        if (img->GetLastStatus() == Ok && img->GetWidth() > 0 && img->GetHeight() > 0) return img;
    }
    return nullptr;
}
} // namespace

const HudSkin& GetHudSkin(const fs::path& baseDir, const fs::path& userDir) {
    std::string key = baseDir.string() + "|" + userDir.string();
    if (g_Skin && g_SkinKey == key) return *g_Skin;
    g_Skin = std::make_unique<HudSkin>();
    g_SkinKey = key;
    HudSkin& s = *g_Skin;
    s.HpFrame = TryLoad(baseDir, userDir, "hp_frame.png");
    s.HpFill = TryLoad(baseDir, userDir, "hp_fill.png");
    s.HpUnit = TryLoad(baseDir, userDir, "hp_unit.png");
    s.HpEmpty = TryLoad(baseDir, userDir, "hp_empty.png");
    s.SpFrame = TryLoad(baseDir, userDir, "sp_frame.png");
    s.SpFill = TryLoad(baseDir, userDir, "sp_fill.png");
    s.SpUnit = TryLoad(baseDir, userDir, "sp_unit.png");
    s.SpEmpty = TryLoad(baseDir, userDir, "sp_empty.png");
    s.TimerFrame = TryLoad(baseDir, userDir, "timer_frame.png");
    s.PortraitFrame = TryLoad(baseDir, userDir, "portrait_frame.png");
    // Portraits: any portrait_<id>.png present in either dir.
    for (const fs::path& dir : {baseDir, userDir}) {
        std::error_code ec;
        fs::path hudDir = dir / "hud";
        if (!fs::is_directory(hudDir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(hudDir, ec)) {
            std::string fname = entry.path().filename().string();
            const std::string prefix = "portrait_", suffix = ".png";
            if (fname.size() <= prefix.size() + suffix.size()) continue;
            if (fname.compare(0, prefix.size(), prefix) != 0) continue;
            if (fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            std::string id = fname.substr(prefix.size(), fname.size() - prefix.size() - suffix.size());
            if (id == "frame") continue;
            auto img = std::make_unique<Image>(entry.path().wstring().c_str());
            if (img->GetLastStatus() == Ok && img->GetWidth() > 0) s.Portraits[id] = std::move(img); // user dir (iterated last) overrides base
        }
    }
    return s;
}

void ClearHudSkinCache() {
    g_Skin.reset();
    g_SkinKey.clear();
}

void DrawSkinImage(Graphics& g, Image* img, const RectF& dest, bool mirror) {
    if (!img) return;
    PointF pts[3];
    REAL left = dest.X, right = dest.X + dest.Width, top = dest.Y, bottom = dest.Y + dest.Height;
    if (!mirror) {
        pts[0] = PointF(left, top); pts[1] = PointF(right, top); pts[2] = PointF(left, bottom);
    } else {
        pts[0] = PointF(right, top); pts[1] = PointF(left, top); pts[2] = PointF(right, bottom);
    }
    g.DrawImage(img, pts, 3);
}

} // namespace kakuge

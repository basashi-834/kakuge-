// platform/HudSkin.h
// Optional image "skin" for the in-match HUD, so the user can design the
// HP bars, portraits, timer box and super gauges as image files instead
// of the procedural drawing in DrawHUD - every element falls back to the
// procedural version when its file is absent, so the game works with no
// images present and each element can be replaced one at a time.
//
// Files live under <dataDir>/hud/ (base Data/ first, then the per-user
// override dir, same split DataManager uses). Sizes below are the on-
// canvas slot each image is stretched into, in 384x224 canvas pixels
// (author at that size, or an integer multiple of it - the renderer
// scales with nearest-neighbor); every P2 element is the P1 image drawn
// mirrored, so only one of each is needed:
//
//   hp_frame.png      121 x 16  frame around the HP bar (slot: bar rect + 3px)
//   hp_fill.png       115 x 10  full-length bar fill, clipped to the HP ratio
//   hp_unit.png         1 x 10  ONE PERCENT of HP - tiled 100x across the
//                               bar; ceil(hp%) copies are drawn, the rest
//                               are simply not drawn (the user's proposed
//                               "hide N unit sprites" approach; any width
//                               works, it's scaled to 1.15px per unit)
//   hp_empty.png      115 x 10  optional backing drawn under the fill
//   sp_frame.png       96 x 14  frame around the super gauge (bar + 3px)
//   sp_fill.png        90 x 8   full-length gauge fill, clipped to ratio
//   sp_unit.png         1 x 8   one percent of gauge, tiled like hp_unit
//   sp_empty.png       90 x 8   optional backing under the gauge fill
//   timer_frame.png    28 x 18  round-timer box (digits drawn on top)
//   portrait_frame.png 22 x 22  frame drawn over the portrait slot
//   portrait_<charId>.png  22 x 22  per-character portrait (e.g.
//                               portrait_ryu.png); falls back to the
//                               procedural bust when missing
//
// Precedence when both a fill and a unit image exist: hp_unit/sp_unit
// wins (it's the more specific, per-percent design).
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <memory>
#include <unordered_map>
#include <filesystem>

namespace kakuge {
namespace fs = std::filesystem;

struct HudSkin {
    std::unique_ptr<Gdiplus::Image> HpFrame, HpFill, HpUnit, HpEmpty;
    std::unique_ptr<Gdiplus::Image> SpFrame, SpFill, SpUnit, SpEmpty;
    std::unique_ptr<Gdiplus::Image> TimerFrame, PortraitFrame;
    std::unordered_map<std::string, std::unique_ptr<Gdiplus::Image>> Portraits;

    bool HasHpArt() const { return HpFrame || HpFill || HpUnit || HpEmpty; }
    bool HasSpArt() const { return SpFrame || SpFill || SpUnit || SpEmpty; }
    Gdiplus::Image* Portrait(const std::string& charId) const {
        auto it = Portraits.find(charId);
        return it == Portraits.end() ? nullptr : it->second.get();
    }
};

// Loads (once) and caches the skin; safe to call every frame.
const HudSkin& GetHudSkin(const fs::path& baseDir, const fs::path& userDir);

// Drops the cache so the next GetHudSkin() re-checks disk.
void ClearHudSkinCache();

// Draws `img` stretched into `dest`, mirrored horizontally when
// `mirror` (used for every Player-2 element).
void DrawSkinImage(Gdiplus::Graphics& g, Gdiplus::Image* img, const Gdiplus::RectF& dest, bool mirror);

} // namespace kakuge

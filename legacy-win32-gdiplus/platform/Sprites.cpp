// platform/Sprites.cpp
#include "Sprites.h"
#include <unordered_map>

namespace kakuge {
namespace {

std::unordered_map<std::string, CharacterSpriteSet> g_Cache;

// User-dir override first, then the shipped default - same resolution
// order DataManager uses for character/move JSON.
std::unique_ptr<Gdiplus::Image> TryLoad(const fs::path& userDir, const fs::path& baseDir,
                                         const std::string& charId, const std::string& name) {
    for (const fs::path& dir : {userDir, baseDir}) {
        fs::path path = dir / "sprites" / charId / (name + ".png");
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) continue;
        auto img = std::make_unique<Gdiplus::Image>(path.wstring().c_str());
        if (img->GetLastStatus() == Gdiplus::Ok && img->GetWidth() > 0 && img->GetHeight() > 0) {
            return img;
        }
    }
    return nullptr;
}

} // namespace

const CharacterSpriteSet& GetCharacterSprites(const std::string& charId, const fs::path& baseDir, const fs::path& userDir) {
    auto existing = g_Cache.find(charId);
    if (existing != g_Cache.end()) return existing->second;

    CharacterSpriteSet set;
    for (int i = 0; i < 4; i++) {
        set.Stand[i] = TryLoad(userDir, baseDir, charId, "stand_" + std::to_string(i));
    }
    set.Crouch = TryLoad(userDir, baseDir, charId, "crouch");
    set.Punch = TryLoad(userDir, baseDir, charId, "punch");
    set.Kick = TryLoad(userDir, baseDir, charId, "kick");
    set.Jump = TryLoad(userDir, baseDir, charId, "jump");
    set.JumpPunch = TryLoad(userDir, baseDir, charId, "jumppunch");
    set.JumpKick = TryLoad(userDir, baseDir, charId, "jumpkick");
    set.Hitstun = TryLoad(userDir, baseDir, charId, "hitstun");
    set.Knockdown = TryLoad(userDir, baseDir, charId, "knockdown");

    auto result = g_Cache.emplace(charId, std::move(set));
    return result.first->second;
}

void ClearSpriteCache() { g_Cache.clear(); }

} // namespace kakuge

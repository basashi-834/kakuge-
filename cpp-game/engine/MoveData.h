// engine/MoveData.h
// Pure data description of a single move (normal/special/super). Mirrors
// the JSON schema used by the PowerShell/WinForms edition 1:1 so the same
// Data/moves/<character>/*.json files work unchanged.
// 1:1 port of winforms-game/MoveData/MoveData.ps1.
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include "Constants.h"
#include <nlohmann/json.hpp>

namespace kakuge {

struct HitboxDef {
    double offsetX = 0, offsetY = 0, width = 40, height = 40;
};

struct Invincibility {
    std::string type = "None";
    int start_frame = 0;
    int end_frame = 0;
};

struct ProjectileDef {
    bool present = false;
    double speed = 500.0;
    int lifetime = 90;
    double width = 30.0;
    double height = 30.0;
    double spawnOffsetX = 40.0;
    double spawnOffsetY = -40.0;
};

class MoveData {
public:
    std::string Id;
    std::string Name;

    int Startup = 1, Active = 1, Recovery = 1, TotalFrame = 0;

    int Damage = 0, Hitstun = 0, Blockstun = 0, Hitstop = 0;
    std::string GuardType = "High";
    double ChipDamagePercent = 0.0;

    std::vector<HitboxDef> Hitboxes;

    double KnockbackX = 0.0, KnockbackY = 0.0;
    std::string HitOutcome = "Normal";

    int MeterGain = 0, MeterCost = 0;

    std::vector<std::string> CancelRoutes;
    int CancelStartFrame = 0, CancelEndFrame = 0;

    std::vector<std::string> Tags;

    Invincibility Inv;

    std::string InputCommand;
    std::string Button;
    std::string Stance = "stand";
    bool RequiresAir = false;

    ProjectileDef Projectile;
    double EffectiveRange = 0.0;

    // Reserved (not yet wired to gameplay): a hitbox that moves under its
    // own trajectory relative to the character during the move, rather
    // than just riding along with the character's own position (which
    // already happens automatically every frame - see Fighter::
    // ApplyPhysics recomputing ActiveHitboxRects each tick). Flagged as a
    // spec placeholder per the user's request - authoring a full per-frame
    // hitbox keyframe timeline in the Character Editor is out of scope for
    // now, but the data has a place to live once it's built.
    bool HasDynamicHitbox = false;

    // Optional reference image for this move's motion, attached in the
    // Character Editor (browse/preview only - see platform/Editor.cpp).
    // Stored as a path relative to the Data directory when the chosen file
    // lives under it, or an absolute path otherwise; empty means no image
    // is attached. Not yet wired into in-game rendering (the game still
    // draws every move with the procedural pixel-art humanoid) - this is
    // purely a move-design reference asset for now.
    std::string MotionImagePath;

    bool HasTag(const std::string& tag) const {
        return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
    }
    bool CanCancelInto(const std::string& moveId) const {
        return std::find(CancelRoutes.begin(), CancelRoutes.end(), moveId) != CancelRoutes.end();
    }
    bool IsCancelWindowOpen(int frame) const {
        if (CancelStartFrame <= 0 && CancelEndFrame <= 0) return false;
        return frame >= CancelStartFrame && frame <= CancelEndFrame;
    }

    // Frame advantage helpers, used by the Character Editor.
    int OnHitAdvantage() const { return Hitstun - Recovery; }
    int OnBlockAdvantage() const { return Blockstun - Recovery; }

    static MoveData FromJson(const nlohmann::json& obj) {
        MoveData m;
        m.Id = obj.value("id", std::string());
        m.Name = obj.value("name", m.Id);
        m.Startup = obj.value("startup", 1);
        m.Active = obj.value("active", 1);
        m.Recovery = obj.value("recovery", 1);
        int tf = obj.value("totalFrame", 0);
        m.TotalFrame = tf > 0 ? tf : (m.Startup + m.Active + m.Recovery);
        m.Damage = obj.value("damage", 0);
        m.Hitstun = obj.value("hitstun", 0);
        m.Blockstun = obj.value("blockstun", 0);
        m.Hitstop = obj.value("hitstop", 0);
        m.GuardType = obj.value("guardType", std::string("High"));
        m.ChipDamagePercent = obj.value("chipDamagePercent", 0.0);

        if (obj.contains("hitbox") && obj["hitbox"].is_array()) {
            for (const auto& hb : obj["hitbox"]) {
                HitboxDef d;
                d.offsetX = hb.value("offsetX", 0.0);
                d.offsetY = hb.value("offsetY", 0.0);
                d.width = hb.value("width", 40.0);
                d.height = hb.value("height", 40.0);
                m.Hitboxes.push_back(d);
            }
        }

        m.KnockbackX = obj.value("knockbackX", 0.0);
        m.KnockbackY = obj.value("knockbackY", 0.0);
        m.HitOutcome = obj.value("hitOutcome", std::string("Normal"));
        m.MeterGain = obj.value("meterGain", 0);
        m.MeterCost = obj.value("meterCost", 0);

        if (obj.contains("cancelRoutes") && obj["cancelRoutes"].is_array()) {
            for (const auto& r : obj["cancelRoutes"]) m.CancelRoutes.push_back(r.get<std::string>());
        }
        m.CancelStartFrame = obj.value("cancelStartFrame", 0);
        m.CancelEndFrame = obj.value("cancelEndFrame", 0);

        if (obj.contains("tags") && obj["tags"].is_array()) {
            for (const auto& t : obj["tags"]) m.Tags.push_back(t.get<std::string>());
        }

        if (obj.contains("invincibility") && obj["invincibility"].is_object()) {
            const auto& iv = obj["invincibility"];
            m.Inv.type = iv.value("type", std::string("None"));
            m.Inv.start_frame = iv.value("start_frame", 0);
            m.Inv.end_frame = iv.value("end_frame", 0);
        }

        m.InputCommand = obj.value("input", std::string());
        m.Button = obj.value("button", std::string());
        m.Stance = obj.value("stance", std::string("stand"));
        m.RequiresAir = obj.value("requiresAir", false);

        if (obj.contains("projectile") && obj["projectile"].is_object()) {
            const auto& p = obj["projectile"];
            m.Projectile.present = true;
            m.Projectile.speed = p.value("speed", 500.0);
            m.Projectile.lifetime = p.value("lifetime", 90);
            m.Projectile.width = p.value("width", 30.0);
            m.Projectile.height = p.value("height", 30.0);
            m.Projectile.spawnOffsetX = p.value("spawnOffsetX", 40.0);
            m.Projectile.spawnOffsetY = p.value("spawnOffsetY", -40.0);
        }

        m.HasDynamicHitbox = obj.value("hasDynamicHitbox", false);
        m.MotionImagePath = obj.value("motionImage", std::string());
        m.EffectiveRange = obj.value("effectiveRange", 0.0);
        if (m.EffectiveRange <= 0.0) {
            if (m.HasTag(Constants::TagProjectile)) m.EffectiveRange = 900.0;
            else if (m.HasTag(Constants::TagThrow)) m.EffectiveRange = 55.0;
            else if (m.HasTag(Constants::TagHeavy)) m.EffectiveRange = 100.0;
            else if (m.HasTag(Constants::TagMedium)) m.EffectiveRange = 85.0;
            else m.EffectiveRange = 70.0;
        }
        return m;
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["id"] = Id; j["name"] = Name;
        j["startup"] = Startup; j["active"] = Active; j["recovery"] = Recovery; j["totalFrame"] = TotalFrame;
        j["damage"] = Damage; j["hitstun"] = Hitstun; j["blockstun"] = Blockstun; j["hitstop"] = Hitstop;
        j["guardType"] = GuardType; j["chipDamagePercent"] = ChipDamagePercent;
        j["hitbox"] = nlohmann::json::array();
        for (const auto& hb : Hitboxes) {
            j["hitbox"].push_back({{"offsetX", hb.offsetX}, {"offsetY", hb.offsetY}, {"width", hb.width}, {"height", hb.height}});
        }
        j["knockbackX"] = KnockbackX; j["knockbackY"] = KnockbackY; j["hitOutcome"] = HitOutcome;
        j["meterGain"] = MeterGain; j["meterCost"] = MeterCost;
        j["cancelRoutes"] = CancelRoutes;
        j["cancelStartFrame"] = CancelStartFrame; j["cancelEndFrame"] = CancelEndFrame;
        j["tags"] = Tags;
        j["invincibility"] = {{"type", Inv.type}, {"start_frame", Inv.start_frame}, {"end_frame", Inv.end_frame}};
        j["input"] = InputCommand; j["button"] = Button; j["stance"] = Stance; j["requiresAir"] = RequiresAir;
        if (Projectile.present) {
            j["projectile"] = {
                {"speed", Projectile.speed}, {"lifetime", Projectile.lifetime},
                {"width", Projectile.width}, {"height", Projectile.height},
                {"spawnOffsetX", Projectile.spawnOffsetX}, {"spawnOffsetY", Projectile.spawnOffsetY}
            };
        } else {
            j["projectile"] = nlohmann::json::object();
        }
        j["effectiveRange"] = EffectiveRange;
        j["hasDynamicHitbox"] = HasDynamicHitbox;
        j["motionImage"] = MotionImagePath;
        return j;
    }
};

} // namespace kakuge

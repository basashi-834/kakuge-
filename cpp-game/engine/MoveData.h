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
#include "Boxes.h"
#include <nlohmann/json.hpp>

namespace kakuge {

// offsetX/offsetY are the box CENTER relative to the character's origin
// (center X, feet Y), in the character's facing direction - the engine
// mirrors offsetX for a left-facing fighter (see MoveExecutor::
// GetActiveHitboxRects), so one set of data serves both facings. A
// spec-style top-left box (x, y, w, h) is entered as
// offsetX = x + w/2, offsetY = y + h/2.
struct HitboxDef {
    double offsetX = 0, offsetY = 0, width = 40, height = 40;
};

// Per-animation-frame collision override for a span of a move's frames
// (0-based, inclusive on both ends, in the move's own frame count). Any
// of the three box kinds can be overridden independently; a kind that
// isn't set falls back to the normal source for that frame (stance
// hurtboxes, stance pushbox, the move's Active-window hitboxes). This is
// what lets a move shrink its arm-side hurtbox during an anti-air, swap
// in a tucked pushbox mid-jump, or run a hitbox on a different timing
// than the shared startup/active/recovery window. JSON key "frameBoxes":
//   [{"startFrame":4,"endFrame":6,
//     "hurtboxes":[{"part":"head","offsetX":0,"offsetY":-80,"width":18,"height":16}, ...],
//     "pushbox":{"offsetX":0,"offsetY":-36,"width":30,"height":72},
//     "hitboxes":[{"offsetX":26,"offsetY":-61,"width":16,"height":10}]}]
// Authored in JSON for now (not yet exposed in the Character Editor).
struct FrameBoxSet {
    int startFrame = 0, endFrame = 0;
    bool hasHurtboxes = false;
    std::vector<HurtboxPart> hurtboxes;
    bool hasPushbox = false;
    RectBox pushbox;
    bool hasHitboxes = false;
    std::vector<HitboxDef> hitboxes;

    bool Covers(int frame) const { return frame >= startFrame && frame <= endFrame; }
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
    std::vector<FrameBoxSet> FrameBoxes;

    // Throws (GuardType "Throw") don't use Hitboxes for their connect
    // check - they resolve on center-to-center distance during the Active
    // window instead (see BattleSystem::ResolveCombat). Defaults to the
    // spec's NORMAL_THROW_RANGE; per-move override via JSON "throwRange".
    double ThrowRange = GameSpec::NormalThrowRange;

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

    // First FrameBoxSet covering `frame`, or nullptr when the frame has no
    // per-frame override (the common case for every move authored before
    // frameBoxes existed).
    const FrameBoxSet* FrameBoxesAt(int frame) const {
        for (const auto& fb : FrameBoxes) {
            if (fb.Covers(frame)) return &fb;
        }
        return nullptr;
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

        auto readHitbox = [](const nlohmann::json& hb) {
            HitboxDef d;
            d.offsetX = hb.value("offsetX", 0.0);
            d.offsetY = hb.value("offsetY", 0.0);
            d.width = hb.value("width", 40.0);
            d.height = hb.value("height", 40.0);
            return d;
        };
        if (obj.contains("hitbox") && obj["hitbox"].is_array()) {
            for (const auto& hb : obj["hitbox"]) m.Hitboxes.push_back(readHitbox(hb));
        }

        if (obj.contains("frameBoxes") && obj["frameBoxes"].is_array()) {
            for (const auto& fj : obj["frameBoxes"]) {
                FrameBoxSet fb;
                fb.startFrame = fj.value("startFrame", 0);
                fb.endFrame = fj.value("endFrame", fb.startFrame);
                if (fj.contains("hurtboxes") && fj["hurtboxes"].is_array()) {
                    fb.hasHurtboxes = true;
                    for (const auto& pj : fj["hurtboxes"]) {
                        HurtboxPart part;
                        part.Name = pj.value("part", std::string("body"));
                        part.Box = RectBox(pj.value("offsetX", 0.0), pj.value("offsetY", 0.0),
                                           pj.value("width", 40.0), pj.value("height", 40.0));
                        fb.hurtboxes.push_back(part);
                    }
                }
                if (fj.contains("pushbox") && fj["pushbox"].is_object()) {
                    const auto& pb = fj["pushbox"];
                    fb.hasPushbox = true;
                    fb.pushbox = RectBox(pb.value("offsetX", 0.0), pb.value("offsetY", 0.0),
                                         pb.value("width", 30.0), pb.value("height", 72.0));
                }
                if (fj.contains("hitboxes") && fj["hitboxes"].is_array()) {
                    fb.hasHitboxes = true;
                    for (const auto& hb : fj["hitboxes"]) fb.hitboxes.push_back(readHitbox(hb));
                }
                m.FrameBoxes.push_back(fb);
            }
        }
        m.ThrowRange = obj.value("throwRange", static_cast<double>(GameSpec::NormalThrowRange));

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
            // CPU-AI engagement distances (center-to-center), sized against
            // the GameSpec-scale normals: a light reaches ~34px from center,
            // a heavy ~48px, a throw NormalThrowRange (28).
            if (m.HasTag(Constants::TagProjectile)) m.EffectiveRange = 900.0;
            else if (m.HasTag(Constants::TagThrow)) m.EffectiveRange = m.ThrowRange;
            else if (m.HasTag(Constants::TagHeavy)) m.EffectiveRange = 50.0;
            else if (m.HasTag(Constants::TagMedium)) m.EffectiveRange = 42.0;
            else m.EffectiveRange = 36.0;
        }
        return m;
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["id"] = Id; j["name"] = Name;
        j["startup"] = Startup; j["active"] = Active; j["recovery"] = Recovery; j["totalFrame"] = TotalFrame;
        j["damage"] = Damage; j["hitstun"] = Hitstun; j["blockstun"] = Blockstun; j["hitstop"] = Hitstop;
        j["guardType"] = GuardType; j["chipDamagePercent"] = ChipDamagePercent;
        auto hitboxJson = [](const HitboxDef& hb) {
            return nlohmann::json{{"offsetX", hb.offsetX}, {"offsetY", hb.offsetY}, {"width", hb.width}, {"height", hb.height}};
        };
        j["hitbox"] = nlohmann::json::array();
        for (const auto& hb : Hitboxes) j["hitbox"].push_back(hitboxJson(hb));
        if (!FrameBoxes.empty()) {
            j["frameBoxes"] = nlohmann::json::array();
            for (const auto& fb : FrameBoxes) {
                nlohmann::json fj{{"startFrame", fb.startFrame}, {"endFrame", fb.endFrame}};
                if (fb.hasHurtboxes) {
                    fj["hurtboxes"] = nlohmann::json::array();
                    for (const auto& part : fb.hurtboxes) {
                        fj["hurtboxes"].push_back({{"part", part.Name}, {"offsetX", part.Box.CenterX}, {"offsetY", part.Box.CenterY},
                                                   {"width", part.Box.Width}, {"height", part.Box.Height}});
                    }
                }
                if (fb.hasPushbox) {
                    fj["pushbox"] = {{"offsetX", fb.pushbox.CenterX}, {"offsetY", fb.pushbox.CenterY},
                                     {"width", fb.pushbox.Width}, {"height", fb.pushbox.Height}};
                }
                if (fb.hasHitboxes) {
                    fj["hitboxes"] = nlohmann::json::array();
                    for (const auto& hb : fb.hitboxes) fj["hitboxes"].push_back(hitboxJson(hb));
                }
                j["frameBoxes"].push_back(fj);
            }
        }
        if (GuardType == Constants::GuardThrow) j["throwRange"] = ThrowRange;
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

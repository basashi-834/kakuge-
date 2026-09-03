// engine/CharacterStats.h
// Base performance data for a character. Pure data - runtime fields (current
// HP, position, velocity, facing) live on Fighter and are re-initialized
// from this every round. 1:1 port of Character/CharacterStats.ps1.
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "Boxes.h"

namespace kakuge {

class CharacterStats {
public:
    std::string Id;
    std::string Name = "Fighter";
    int MaxHP = 1000;
    double WalkForwardSpeed = 220.0;
    double WalkBackwardSpeed = 170.0;
    double DashSpeed = 420.0;
    double JumpVelocity = -900.0;
    double Gravity = 2400.0;
    int ColorR = 200, ColorG = 50, ColorB = 45;
    std::vector<std::string> MoveIds;
    // Per-stance hurtbox, editable in the Character Editor. Defaults to
    // HurtboxSet's own built-in defaults when absent from JSON, so existing
    // character files stay valid without this field.
    HurtboxSet Hurtboxes;

    static CharacterStats FromJson(const nlohmann::json& obj) {
        CharacterStats s;
        s.Id = obj.value("id", std::string());
        s.Name = obj.value("name", s.Id);
        s.MaxHP = obj.value("maxHP", 1000);
        s.WalkForwardSpeed = obj.value("walkForwardSpeed", 220.0);
        s.WalkBackwardSpeed = obj.value("walkBackwardSpeed", 170.0);
        s.DashSpeed = obj.value("dashSpeed", 420.0);
        s.JumpVelocity = obj.value("jumpVelocity", -900.0);
        s.Gravity = obj.value("gravity", 2400.0);
        if (obj.contains("color") && obj["color"].is_array() && obj["color"].size() >= 3) {
            s.ColorR = static_cast<int>(obj["color"][0].get<double>() * 255);
            s.ColorG = static_cast<int>(obj["color"][1].get<double>() * 255);
            s.ColorB = static_cast<int>(obj["color"][2].get<double>() * 255);
        }
        if (obj.contains("moves") && obj["moves"].is_array()) {
            for (const auto& mid : obj["moves"]) s.MoveIds.push_back(mid.get<std::string>());
        }
        if (obj.contains("hurtboxes") && obj["hurtboxes"].is_object()) {
            const auto& hb = obj["hurtboxes"];
            // A stance value is either the OLD schema - a single object
            // {offsetX,offsetY,width,height} - or the new one, an array of
            // named parts [{part,offsetX,offsetY,width,height}, ...]. Old
            // files load as one part named "body" so nothing on disk needs
            // migrating by hand.
            auto loadStance = [](const nlohmann::json& j, std::vector<HurtboxPart>& parts) {
                if (j.is_array()) {
                    parts.clear();
                    for (const auto& pj : j) {
                        HurtboxPart part;
                        part.Name = pj.value("part", std::string("body"));
                        part.Box.CenterX = pj.value("offsetX", 0.0);
                        part.Box.CenterY = pj.value("offsetY", 0.0);
                        part.Box.Width = pj.value("width", 40.0);
                        part.Box.Height = pj.value("height", 40.0);
                        parts.push_back(part);
                    }
                } else if (j.is_object()) {
                    HurtboxPart part;
                    part.Name = "body";
                    part.Box.CenterX = j.value("offsetX", 0.0);
                    part.Box.CenterY = j.value("offsetY", 0.0);
                    part.Box.Width = j.value("width", 40.0);
                    part.Box.Height = j.value("height", 40.0);
                    parts = {part};
                }
            };
            if (hb.contains("stand")) loadStance(hb["stand"], s.Hurtboxes.Stand);
            if (hb.contains("crouch")) loadStance(hb["crouch"], s.Hurtboxes.Crouch);
            if (hb.contains("air")) loadStance(hb["air"], s.Hurtboxes.Air);
        }
        return s;
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["id"] = Id; j["name"] = Name; j["maxHP"] = MaxHP;
        j["walkForwardSpeed"] = WalkForwardSpeed; j["walkBackwardSpeed"] = WalkBackwardSpeed;
        j["dashSpeed"] = DashSpeed; j["jumpVelocity"] = JumpVelocity; j["gravity"] = Gravity;
        j["color"] = {ColorR / 255.0, ColorG / 255.0, ColorB / 255.0};
        auto saveStance = [](const std::vector<HurtboxPart>& parts) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& part : parts) {
                arr.push_back({{"part", part.Name}, {"offsetX", part.Box.CenterX}, {"offsetY", part.Box.CenterY},
                                {"width", part.Box.Width}, {"height", part.Box.Height}});
            }
            return arr;
        };
        j["hurtboxes"] = {{"stand", saveStance(Hurtboxes.Stand)}, {"crouch", saveStance(Hurtboxes.Crouch)}, {"air", saveStance(Hurtboxes.Air)}};
        j["moves"] = MoveIds;
        return j;
    }
};

} // namespace kakuge

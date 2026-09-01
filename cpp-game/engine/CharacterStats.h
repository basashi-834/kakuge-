// engine/CharacterStats.h
// Base performance data for a character. Pure data - runtime fields (current
// HP, position, velocity, facing) live on Fighter and are re-initialized
// from this every round. 1:1 port of Character/CharacterStats.ps1.
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

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
        return s;
    }

    nlohmann::json ToJson() const {
        nlohmann::json j;
        j["id"] = Id; j["name"] = Name; j["maxHP"] = MaxHP;
        j["walkForwardSpeed"] = WalkForwardSpeed; j["walkBackwardSpeed"] = WalkBackwardSpeed;
        j["dashSpeed"] = DashSpeed; j["jumpVelocity"] = JumpVelocity; j["gravity"] = Gravity;
        j["color"] = {ColorR / 255.0, ColorG / 255.0, ColorB / 255.0};
        j["moves"] = MoveIds;
        return j;
    }
};

} // namespace kakuge

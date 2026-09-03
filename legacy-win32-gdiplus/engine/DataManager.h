// engine/DataManager.h
// Owns ALL external data I/O: character base stats, move frame data, and
// (new) display settings.
//
// Default data ships read-only next to the launcher under Data/characters
// and Data/moves/<id>/. The Character Editor writes edits (and brand-new
// characters, see CreateCharacter) to a per-user folder (%APPDATA%/Kakuge
// on Windows) so changes survive a restart without ever touching the
// shipped files. Resolution order per file: user override > shipped
// default. 1:1 port of winforms-game/Data/DataManager.ps1, extended with
// settings persistence and in-game character creation.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "CharacterStats.h"
#include "MoveData.h"
#include "Settings.h"
#include <nlohmann/json.hpp>

namespace kakuge {
namespace fs = std::filesystem;

class DataManager {
public:
    fs::path BaseDir;
    fs::path UserDir;
    std::unordered_map<std::string, CharacterStats> Characters;
    std::unordered_map<std::string, std::unordered_map<std::string, MoveData>> Movesets;
    std::vector<std::string> CharacterOrder; // preserves discovery order for UI lists

    DataManager(const fs::path& baseDir, const fs::path& userDir) : BaseDir(baseDir), UserDir(userDir) {
        std::error_code ec;
        fs::create_directories(UserDir / "characters", ec);
        fs::create_directories(UserDir / "moves", ec);
    }

    static bool ReadJsonFile(const fs::path& path, nlohmann::json& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) return false;
        try {
            f >> out;
        } catch (...) {
            return false;
        }
        return true;
    }

    static bool WriteJsonFile(const fs::path& path, const nlohmann::json& data) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.good()) return false;
        f << data.dump(4);
        return true;
    }

    void ReloadAll() {
        Characters.clear();
        Movesets.clear();
        CharacterOrder.clear();
        LoadCharactersFrom(BaseDir / "characters");
        LoadCharactersFrom(UserDir / "characters");
        for (const auto& id : CharacterOrder) {
            Movesets[id] = {};
            LoadMovesFrom(id, BaseDir / "moves" / id);
            LoadMovesFrom(id, UserDir / "moves" / id);
        }
    }

    void LoadCharactersFrom(const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            nlohmann::json data;
            if (!ReadJsonFile(entry.path(), data)) continue;
            if (!data.contains("id") || data["id"].get<std::string>().empty()) continue;
            CharacterStats stats = CharacterStats::FromJson(data);
            if (Characters.find(stats.Id) == Characters.end()) CharacterOrder.push_back(stats.Id);
            Characters[stats.Id] = stats;
        }
    }

    void LoadMovesFrom(const std::string& charId, const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            nlohmann::json data;
            if (!ReadJsonFile(entry.path(), data)) continue;
            if (!data.contains("id") || data["id"].get<std::string>().empty()) continue;
            MoveData move = MoveData::FromJson(data);
            Movesets[charId][move.Id] = move;
        }
    }

    const std::vector<std::string>& GetCharacterIds() const { return CharacterOrder; }

    const CharacterStats* GetCharacter(const std::string& id) const {
        auto it = Characters.find(id);
        return it == Characters.end() ? nullptr : &it->second;
    }

    const std::unordered_map<std::string, MoveData>* GetMoveset(const std::string& charId) const {
        auto it = Movesets.find(charId);
        return it == Movesets.end() ? nullptr : &it->second;
    }

    const MoveData* GetMove(const std::string& charId, const std::string& moveId) const {
        auto* ms = GetMoveset(charId);
        if (!ms) return nullptr;
        auto it = ms->find(moveId);
        return it == ms->end() ? nullptr : &it->second;
    }

    void SaveCharacter(const CharacterStats& stats) {
        if (Characters.find(stats.Id) == Characters.end()) CharacterOrder.push_back(stats.Id);
        Characters[stats.Id] = stats;
        fs::path path = UserDir / "characters" / (stats.Id + ".json");
        WriteJsonFile(path, stats.ToJson());
    }

    void SaveMove(const std::string& charId, const MoveData& move) {
        Movesets[charId][move.Id] = move;
        fs::path path = UserDir / "moves" / charId / (move.Id + ".json");
        WriteJsonFile(path, move.ToJson());
    }

    // New: create a brand-new character in-game (not just edit an existing
    // one), by cloning an existing character's stats+moveset as a starting
    // template under a new id/name, then persisting it to the user dir so
    // it's immediately selectable and playable. Returns false if id is
    // empty, already taken, or the template character doesn't exist.
    bool CreateCharacter(const std::string& newId, const std::string& newName, const std::string& templateCharId) {
        if (newId.empty()) return false;
        if (Characters.find(newId) != Characters.end()) return false;
        const CharacterStats* tmpl = GetCharacter(templateCharId);
        if (!tmpl) return false;
        const auto* tmplMoves = GetMoveset(templateCharId);

        CharacterStats stats = *tmpl;
        stats.Id = newId;
        stats.Name = newName.empty() ? newId : newName;
        SaveCharacter(stats);

        if (tmplMoves) {
            for (const auto& kv : *tmplMoves) {
                SaveMove(newId, kv.second);
            }
        }
        return true;
    }

    // ---- Settings (resolution / aspect ratio) ----
    Settings LoadSettings() const {
        nlohmann::json data;
        if (ReadJsonFile(UserDir / "settings.json", data)) {
            return Settings::FromJson(data);
        }
        return Settings();
    }

    void SaveSettings(const Settings& s) const {
        WriteJsonFile(UserDir / "settings.json", s.ToJson());
    }
};

} // namespace kakuge

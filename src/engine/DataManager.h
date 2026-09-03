// =====================================================================
// engine/DataManager.h - データファイルの読み書きを一手に引き受ける
// =====================================================================
// キャラクター性能・技データ・画面設定の読み書きは、すべてここを通ります。
// ファイルを触る場所を 1 か所に集めておくと、「どこでファイルが
// 書き換わっているのか分からない」という事態を防げます。
//
// 2 つのフォルダを使い分けます
// -------------------------
//   BaseDir （実行ファイルの隣の data/）
//       最初から入っているデータ。読み込むだけで、書き換えません。
//       ゲームを再インストールしても、ここは常に元の状態です。
//
//   UserDir （Windows なら %APPDATA%\Kakuge\）
//       プレイヤーが編集・追加したデータと設定の保存先。
//
// 読み込みは BaseDir → UserDir の順に行い、同じ ID があれば
// UserDir のほうで上書きします（＝「ユーザーの編集が勝つ」）。
// 元データを壊さずに調整を試せる仕組みです。
// =====================================================================
#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Json.h"
#include "engine/CharacterStats.h"
#include "engine/MoveData.h"
#include "engine/Settings.h"

namespace kakuge {
namespace fs = std::filesystem;

class DataManager {
public:
    fs::path BaseDir;
    fs::path UserDir;
    std::unordered_map<std::string, CharacterStats> Characters;
    // キャラクター ID → （技 ID → 技データ）の二段の表。
    std::unordered_map<std::string, std::unordered_map<std::string, MoveData>> Movesets;
    // 読み込んだ順番を保つ一覧（キャラクター選択画面の並び順に使う）。
    // unordered_map は順番を保証しないので、別に持っておく必要があります。
    std::vector<std::string> CharacterOrder;

    DataManager(const fs::path& baseDir, const fs::path& userDir)
        : BaseDir(baseDir), UserDir(userDir) {
        // 保存先フォルダが無ければ作っておきます。
        // error_code 版を使うのは、作れなくても例外で落ちないようにするため
        //（読み込み専用の環境でもゲーム自体は遊べるべきなので）。
        std::error_code ec;
        fs::create_directories(UserDir / "characters", ec);
        fs::create_directories(UserDir / "moves", ec);
    }

    static bool ReadJsonFile(const fs::path& path, Json& out) {
        return Json::LoadFile(path.string(), out);
    }

    static bool WriteJsonFile(const fs::path& path, const Json& data) {
        return Json::SaveFile(path.string(), data, 4);
    }

    // すべて読み直す（起動時と、エディタで保存したあとに呼ぶ）。
    void ReloadAll() {
        Characters.clear();
        Movesets.clear();
        CharacterOrder.clear();
        LoadCharactersFrom(BaseDir / "characters");
        LoadCharactersFrom(UserDir / "characters"); // 後勝ち＝ユーザー版が優先
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
            Json data;
            if (!ReadJsonFile(entry.path(), data)) continue; // 壊れたファイルは黙って飛ばす
            if (data.GetString("id", "").empty()) continue;  // id が無いものは無効
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
            Json data;
            if (!ReadJsonFile(entry.path(), data)) continue;
            if (data.GetString("id", "").empty()) continue;
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
        WriteJsonFile(UserDir / "characters" / (stats.Id + ".json"), stats.ToJson());
    }

    void SaveMove(const std::string& charId, const MoveData& move) {
        Movesets[charId][move.Id] = move;
        WriteJsonFile(UserDir / "moves" / charId / (move.Id + ".json"), move.ToJson());
    }

    // 既存のキャラクターを雛形として、新しいキャラクターを作る。
    // 性能も技も丸ごとコピーしてから ID と名前だけ変えるので、
    // 作った直後からそのまま遊べます。
    // ID が空・すでに使われている・雛形が見つからない場合は false。
    bool CreateCharacter(const std::string& newId, const std::string& newName,
                         const std::string& templateCharId) {
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
            for (const auto& kv : *tmplMoves) SaveMove(newId, kv.second);
        }
        return true;
    }

    // ---- 画面設定 ----
    Settings LoadSettings() const {
        Json data;
        if (ReadJsonFile(UserDir / "settings.json", data)) return Settings::FromJson(data);
        return Settings(); // 初回起動時などは既定値
    }

    void SaveSettings(const Settings& s) const {
        WriteJsonFile(UserDir / "settings.json", s.ToJson());
    }
};

} // namespace kakuge

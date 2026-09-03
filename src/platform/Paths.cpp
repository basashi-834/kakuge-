// =====================================================================
// platform/Paths.cpp - フォルダの場所を決める
// =====================================================================
#include "platform/Paths.h"

#include <SDL.h>

namespace kakuge {

namespace fs = std::filesystem;

fs::path GetBaseDataDir() {
    fs::path base;
    // SDL_GetBasePath は自分でメモリを確保して返してくるので、
    // 使い終わったら SDL_free で返さなければなりません（お約束）。
    if (char* raw = SDL_GetBasePath()) {
        base = fs::path(raw);
        SDL_free(raw);
    } else {
        // まれに取得できない環境があります。その場合は
        // 「今いるフォルダ」を基準にして、少なくとも起動はできるようにします。
        base = fs::current_path();
    }

    fs::path dataDir = base / "data";
    std::error_code ec;
    if (fs::is_directory(dataDir, ec)) return dataDir;

    // 見つからないとき用の予備。開発中に Visual Studio から
    // 直接実行した場合など、実行ファイルがソースツリーの奥深くに
    // できることがあるので、いくつか上の階層も探します。
    for (int up = 1; up <= 3; ++up) {
        fs::path candidate = base;
        for (int i = 0; i < up; ++i) candidate = candidate.parent_path();
        candidate /= "data";
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    return dataDir; // どこにも無ければ既定の場所を返す（中身が空なら
                    // キャラクターが 0 人になり、その旨が画面に出ます）
}

fs::path GetUserDataDir() {
    fs::path dir;
    // 第 1 引数は会社・団体名、第 2 引数はアプリ名。
    // Windows では AppData\Roaming\<会社名>\<アプリ名>\ になります。
    // ここでは会社名を空にして AppData\Roaming\Kakuge\ にしています。
    if (char* raw = SDL_GetPrefPath("", "Kakuge")) {
        dir = fs::path(raw);
        SDL_free(raw);
    } else {
        dir = GetBaseDataDir().parent_path() / "userdata";
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace kakuge

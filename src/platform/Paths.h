// =====================================================================
// platform/Paths.h - データフォルダと保存先フォルダの場所を決める
// =====================================================================
// ゲームは 2 つのフォルダを使います（詳しくは engine/DataManager.h）。
//   data フォルダ … 最初から入っているデータ。読むだけ。
//   保存フォルダ  … プレイヤーの編集や設定の保存先。
//
// 「実行ファイルの隣」を自力で探すのが意外と面倒なので、
// SDL に用意されている関数を使います。
//   SDL_GetBasePath() … 実行ファイルのあるフォルダ
//   SDL_GetPrefPath() … 設定の保存に適したフォルダ
//                       Windows: C:/Users/<名前>/AppData/Roaming/Kakuge/
//                       Linux  : ~/.local/share/Kakuge/
// OS ごとの違いを SDL が吸収してくれるので、こちらは意識しません。
// =====================================================================
#pragma once
#include <filesystem>

namespace kakuge {

// 最初から入っているデータの場所（実行ファイルの隣の data/）。
std::filesystem::path GetBaseDataDir();

// プレイヤーの編集内容・設定の保存先。
std::filesystem::path GetUserDataDir();

} // namespace kakuge

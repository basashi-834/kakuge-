// =====================================================================
// main.cpp - プログラムの出発点
// =====================================================================
// C++ のプログラムは必ず main 関数から始まります。
// このファイルは、Game クラスを作って動かすだけの短いものです。
// 中身を細かく分けておくと、あとで「起動処理だけ見たい」というときに
// 迷わずここを開けば済みます。
//
// SDL_main について
// ---------------
// Windows のウィンドウアプリは、本来 main ではなく WinMain という
// 特別な関数から始まります。しかし SDL2 が SDL2main というライブラリで
// その違いを吸収してくれるので、Windows でも Linux でも
// この普通の main を書くだけで動きます。
// （そのため main の引数は使わなくても int argc, char** argv の形に
//   しておく必要があります。省略すると SDL2main とかみ合いません）
// =====================================================================
#include <SDL.h>

#include <cstring>
#include <string>

#include "platform/Game.h"
#include "platform/SpriteExport.h"

int main(int argc, char** argv) {
    // ---- スプライトの型紙を書き出すだけのモード ----
    // 通常の起動では通りません（ゲーム画面は開かずに終了します）。
    //   Kakuge --export-sprites          … 全キャラクターぶん
    //   Kakuge --export-sprites ryu      … ryu だけ
    // 詳しくは platform/SpriteExport.h を見てください。
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--export-sprites") != 0) continue;
        std::string charId = (i + 1 < argc) ? argv[i + 1] : std::string();
        // 次の引数が別のオプションなら、キャラクター指定ではありません。
        if (!charId.empty() && charId.rfind("--", 0) == 0) charId.clear();
        return kakuge::ExportSpriteSheets(charId);
    }

    kakuge::Game game;
    if (!game.Init()) {
        // Init の中でエラーの内容をダイアログ表示しているので、
        // ここでは終了コードを返すだけです（0 以外 = 異常終了）。
        game.Shutdown();
        return 1;
    }
    int result = game.Run();
    game.Shutdown();
    return result;
}

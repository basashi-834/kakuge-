// =====================================================================
// platform/SpriteExport.cpp - SpriteExport.h の中身
// =====================================================================
#include "platform/SpriteExport.h"

#include <SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/Json.h"
#include "engine/DataManager.h"
#include "platform/Figure.h"
#include "platform/Paths.h"
#include "platform/Renderer.h"

namespace kakuge {

namespace fs = std::filesystem;

namespace {

// ---- シートの寸法 ----
// 1 コマの大きさは「一番大きく広がる姿勢」に合わせて決めます。
// 立ち強パンチで拳の先が中心から約 71px、立ちキックで足先が
// 約 67px。左右対称に取って 160px あれば、どの技も切れません。
// 高さは、身長 95px ＋ 上の余白 9px ＋ 足元より下の余白 32px。
//
// 仕様の推奨サイズ（GameSpec::CharacterSpriteWidth = 80 x 100）より
// 大きいのは、そちらが「立ち姿が収まる目安」の値で、技のコマは
// その枠に収まらなくてよいと決めているためです。
constexpr int kCellW = 160;
constexpr int kCellH = 136;
constexpr int kOriginX = kCellW / 2; // 足元は左右の中央
constexpr int kOriginY = 104;        // コマの上から数えた足元の高さ
constexpr int kColumns = 8;
constexpr int kRows = 4;

// 透明にする色（マゼンタ）。この色で背景を塗っておき、
// sprites.json 側で「この色は透明」と指定します。
constexpr int kKeyR = 255, kKeyG = 0, kKeyB = 255;

// ---- どのコマに何を描くか ----
// 並び順がそのままシートの並び（左上から右へ、8 個で次の段）です。
struct CellPose {
    const char* comment; // 何の姿勢か（読む人むけ）
    HumanoidPose pose;
};

// 技の 3 コマ（発生 / 持続 / 硬直）は、腕・脚の伸ばし具合を
// 変えて作ります。持続がいちばん伸びた形です。
HumanoidPose MakePose(bool crouch, bool jump, double armReach, double legKick,
                      double guardRaise = 0.0, double leanBack = 0.0,
                      bool lying = false, int idleFrame = 0) {
    HumanoidPose p;
    p.facing = 1; // 絵は必ず右向きで描く決まり
    p.crouch = crouch;
    p.jump = jump;
    p.armReach = armReach;
    p.legKick = legKick;
    p.guardRaise = guardRaise;
    p.leanBack = leanBack;
    p.lying = lying;
    p.idleFrame = idleFrame;
    return p;
}

std::vector<CellPose> BuildCellList() {
    std::vector<CellPose> cells;
    // 0-3: 立ち（呼吸の 4 コマ）
    for (int i = 0; i < 4; ++i) {
        cells.push_back({"idle", MakePose(false, false, 0, 0, 0, 0, false, i)});
    }
    cells.push_back({"crouch", MakePose(true, false, 0, 0)});               // 4
    cells.push_back({"jump", MakePose(false, true, 0, 0)});                 // 5
    cells.push_back({"block", MakePose(false, false, 0, 0, 10)});           // 6
    cells.push_back({"block_crouch", MakePose(true, false, 0, 0, 10)});     // 7
    cells.push_back({"hitstun", MakePose(false, false, 0, 0, 0, -8)});      // 8
    cells.push_back({"hitstun_air", MakePose(false, true, 0, 0, 0, -8)});   // 9
    cells.push_back({"knockdown", MakePose(false, false, 0, 0, 0, 0, true)});// 10
    cells.push_back({"dead", MakePose(false, false, 0, 0, 0, 0, true)});    // 11
    // 12-14: 立ちパンチ（発生 / 持続 / 硬直）
    cells.push_back({"punch startup", MakePose(false, false, 12, 0)});
    cells.push_back({"punch active", MakePose(false, false, 34, 0)});
    cells.push_back({"punch recovery", MakePose(false, false, 20, 0)});
    // 15-17: 立ちキック
    cells.push_back({"kick startup", MakePose(false, false, 0, 12)});
    cells.push_back({"kick active", MakePose(false, false, 0, 34)});
    cells.push_back({"kick recovery", MakePose(false, false, 0, 20)});
    // 18-20: しゃがみパンチ
    cells.push_back({"crouch_punch startup", MakePose(true, false, 12, 0)});
    cells.push_back({"crouch_punch active", MakePose(true, false, 34, 0)});
    cells.push_back({"crouch_punch recovery", MakePose(true, false, 20, 0)});
    // 21-23: しゃがみキック
    cells.push_back({"crouch_kick startup", MakePose(true, false, 0, 12)});
    cells.push_back({"crouch_kick active", MakePose(true, false, 0, 34)});
    cells.push_back({"crouch_kick recovery", MakePose(true, false, 0, 20)});
    // 24-26: 空中パンチ
    cells.push_back({"jump_punch startup", MakePose(false, true, 12, 0)});
    cells.push_back({"jump_punch active", MakePose(false, true, 34, 0)});
    cells.push_back({"jump_punch recovery", MakePose(false, true, 20, 0)});
    // 27-29: 空中キック
    cells.push_back({"jump_kick startup", MakePose(false, true, 0, 12)});
    cells.push_back({"jump_kick active", MakePose(false, true, 0, 34)});
    cells.push_back({"jump_kick recovery", MakePose(false, true, 0, 20)});
    return cells;
}

// アニメーション 1 本ぶんの JSON を作る補助。
Json MakeAnim(std::initializer_list<int> cells, int hold, bool loop, const char* mode) {
    Json anim = Json::MakeObject();
    Json list = Json::MakeArray();
    for (int c : cells) list.Push(Json(c));
    anim.Set("cells", list);
    if (mode) anim.Set("mode", Json(mode));
    if (hold > 0) anim.Set("hold", Json(hold));
    if (!loop) anim.Set("loop", Json(false));
    return anim;
}

// 書き出す sprites.json を組み立てる。
Json BuildSpritesJson(const std::string& sheetFileName) {
    Json root = Json::MakeObject();
    root.Set("sheet", Json(sheetFileName));
    Json key = Json::MakeArray();
    key.Push(Json(kKeyR)); key.Push(Json(kKeyG)); key.Push(Json(kKeyB));
    root.Set("transparent", key);
    root.Set("cellWidth", Json(kCellW));
    root.Set("cellHeight", Json(kCellH));
    root.Set("columns", Json(kColumns));
    root.Set("originX", Json(kOriginX));
    root.Set("originY", Json(kOriginY));
    root.Set("stateTint", Json(true));

    Json anims = Json::MakeObject();
    // 立ち・歩き。歩きの絵はまだ無いので立ちで代用します
    //（"walk_forward" を足せば、そちらが使われます）。
    anims.Set("idle", MakeAnim({0, 1, 2, 3}, 8, true, nullptr));
    anims.Set("crouch", MakeAnim({4}, 0, false, nullptr));
    anims.Set("jump", MakeAnim({5}, 0, false, nullptr));
    anims.Set("block", MakeAnim({6}, 0, false, nullptr));
    anims.Set("block_crouch", MakeAnim({7}, 0, false, nullptr));
    anims.Set("hitstun", MakeAnim({8}, 0, false, nullptr));
    anims.Set("hitstun_air", MakeAnim({9}, 0, false, nullptr));
    anims.Set("knockdown", MakeAnim({10}, 0, false, nullptr));
    anims.Set("dead", MakeAnim({11}, 0, false, nullptr));
    anims.Set("wakeup", MakeAnim({4}, 0, false, nullptr));
    anims.Set("throw", MakeAnim({8}, 0, false, nullptr));
    // 技は「段階」で 3 コマ。技のフレーム数をあとから変えても、
    // 発生 → 持続 → 硬直の絵が必ず合います。
    anims.Set("punch", MakeAnim({12, 13, 14}, 0, false, "phase"));
    anims.Set("kick", MakeAnim({15, 16, 17}, 0, false, "phase"));
    anims.Set("crouch_punch", MakeAnim({18, 19, 20}, 0, false, "phase"));
    anims.Set("crouch_kick", MakeAnim({21, 22, 23}, 0, false, "phase"));
    anims.Set("jump_punch", MakeAnim({24, 25, 26}, 0, false, "phase"));
    anims.Set("jump_kick", MakeAnim({27, 28, 29}, 0, false, "phase"));
    root.Set("animations", anims);
    return root;
}

// 1 キャラクターぶんのシートを描いて BMP に保存する。
bool WriteSheet(SDL_Renderer* sdl, Renderer& r, const CharacterStats& stats,
                const fs::path& bmpPath) {
    const int sheetW = kCellW * kColumns;
    const int sheetH = kCellH * kRows;

    SDL_Texture* sheet = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, sheetW, sheetH);
    if (!sheet) {
        SDL_Log("シート用の画像を作れませんでした: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderTarget(sdl, sheet);

    // 背景を透明色（マゼンタ）で塗りつぶす。
    r.Clear(Color(kKeyR, kKeyG, kKeyB, 255));

    Color body(stats.ColorR, stats.ColorG, stats.ColorB);
    const std::vector<CellPose> cells = BuildCellList();
    for (size_t i = 0; i < cells.size(); ++i) {
        int col = static_cast<int>(i) % kColumns;
        int row = static_cast<int>(i) / kColumns;
        double footX = col * kCellW + kOriginX;
        double footY = row * kCellH + kOriginY;
        DrawHumanoid(r, footX, footY, body, cells[i].pose);
    }

    // 画面に出す代わりに、描いた結果を読み出して BMP に保存します。
    // 24bit（RGB）で保存するので、どのお絵かきソフトでも開けます。
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, sheetW, sheetH, 24,
                                                      SDL_PIXELFORMAT_RGB24);
    bool ok = false;
    if (out) {
        if (SDL_RenderReadPixels(sdl, nullptr, SDL_PIXELFORMAT_RGB24,
                                 out->pixels, out->pitch) == 0) {
            std::error_code ec;
            fs::create_directories(bmpPath.parent_path(), ec);
            ok = (SDL_SaveBMP(out, bmpPath.string().c_str()) == 0);
            if (!ok) SDL_Log("BMP を保存できませんでした: %s", SDL_GetError());
        } else {
            SDL_Log("描いた絵を読み出せませんでした: %s", SDL_GetError());
        }
        SDL_FreeSurface(out);
    }

    SDL_SetRenderTarget(sdl, nullptr);
    SDL_DestroyTexture(sheet);
    return ok;
}

} // namespace

int ExportSpriteSheets(const std::string& characterId) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL を初期化できませんでした: %s", SDL_GetError());
        return 1;
    }
    // 画面は出さないので、隠したウィンドウを 1 枚だけ用意します
    //（SDL では描画機を作るのにウィンドウが要るため）。
    SDL_Window* window = SDL_CreateWindow("Kakuge sprite export",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          64, 64, SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Log("ウィンドウを作れませんでした: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* sdl = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE);
    if (!sdl) {
        SDL_Log("描画機を作れませんでした: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);

    Renderer r;
    r.Init(sdl);

    DataManager dm(GetBaseDataDir(), GetUserDataDir());
    dm.ReloadAll();

    // Windows ではコンソールが出ない作り（WIN32 アプリ）なので、
    // 結果はダイアログでも知らせます。
    std::string report;
    int written = 0, failed = 0;
    for (const std::string& id : dm.GetCharacterIds()) {
        if (!characterId.empty() && id != characterId) continue;
        const CharacterStats* stats = dm.GetCharacter(id);
        if (!stats) continue;

        fs::path dir = GetBaseDataDir() / "sprites" / id;
        fs::path bmp = dir / (id + ".bmp");
        if (!WriteSheet(sdl, r, *stats, bmp)) { ++failed; continue; }

        if (!Json::SaveFile((dir / "sprites.json").string(), BuildSpritesJson(id + ".bmp"), 4)) {
            SDL_Log("sprites.json を保存できませんでした: %s", dir.string().c_str());
            ++failed;
            continue;
        }
        SDL_Log("書き出しました: %s", dir.string().c_str());
        report += dir.string() + "\n";
        ++written;
    }

    if (written == 0 && failed == 0) {
        SDL_Log("該当するキャラクターが見つかりませんでした（%s）",
                    characterId.empty() ? "全キャラクター" : characterId.c_str());
        failed = 1;
    }
    std::string message;
    if (written > 0) {
        message =
            "スプライトの型紙を書き出しました。\n\n" + report +
            "\n次にやること\n"
            "  1. 書き出した .bmp をお絵かきソフトで開く\n"
            "  2. マゼンタ（255,0,255）の背景はそのままに、コマの上から絵を描き直す\n"
            "     （足元の位置＝各コマの左右中央・上から " + std::to_string(kOriginY) +
            " ピクセル目を動かさないこと）\n"
            "  3. 上書き保存してゲームを起動する（自動で読み込まれます）\n\n"
            "※ ソースツリーの data/sprites/ にも同じものを置くと、\n"
            "   ビルドし直しても残ります。";
    } else {
        message = "スプライトの型紙を書き出せませんでした。";
    }
    SDL_Log("%s", message.c_str());
    SDL_ShowSimpleMessageBox(failed == 0 ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
                             "Kakuge", message.c_str(), nullptr);

    r.Shutdown();
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failed == 0 ? 0 : 1;
}

} // namespace kakuge

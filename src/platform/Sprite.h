// =====================================================================
// platform/Sprite.h - 手描きの絵（スプライト）を差し込む仕組み
// =====================================================================
// このゲームは元々、キャラクターを図形の組み合わせでその場で
// 描いています（platform/Figure.cpp）。絵が 1 枚も無くても動く
// かわりに、絵柄を自由に作り込むことはできません。
//
// ここは「1 コマ丸ごとの絵」を差し込むための仕組みです。
// 絵を用意した状態（姿勢・技）だけがその絵に置き換わり、
// 用意していない状態は今までどおり図形で描かれます。
//
// 使い方（3 ステップ）
// -----------------
//   1. 絵を 1 枚のシート（BMP 画像）にまとめて並べる
//   2. data/sprites/<キャラクター ID>/sprites.json に、
//      「どのコマがどの姿勢か」を書く
//   3. ゲームを起動する（自動で読み込まれます）
//
// 絵を描かなくても、まずは
//     Kakuge --export-sprites
// を実行してください。今の図形描画をそのままシート画像と
// sprites.json に書き出します（platform/SpriteExport.h）。
// できたシートを塗り替えていけば、確実に位置の合った
// スプライトが作れます。
//
// なぜ BMP なのか
// -------------
// このプロジェクトは SDL2 だけで作る決まりです。PNG を読むには
// SDL_image という別のライブラリが要りますが、BMP は SDL2 本体の
// SDL_LoadBMP() だけで読めます。Windows のペイントでも
// GIMP でも保存できる形式なので、絵を用意する側も困りません。
//
// 透明はどう表すのか
// ---------------
// 「この色は透明」と決めた 1 色（既定はマゼンタ = 255,0,255）で
// 背景を塗ります。BMP に透明の情報が入っていなくても、これなら
// 確実に抜けます。32bit の BMP を使う場合は sprites.json に
// "transparent": null と書けば、画像側の透明度をそのまま使います。
// =====================================================================
#pragma once
#include <SDL.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "engine/Fighter.h"
#include "platform/Renderer.h"

namespace kakuge {

// ---------------------------------------------------------------------
// 1 コマぶんの絵
// ---------------------------------------------------------------------
// シート画像のどこを切り出すか（x, y, w, h）と、
// そのコマの中で「足の裏（接地点）」がどこかを持ちます。
//
// 原点（originX, originY）が要です。ゲーム側がキャラクターの
// 位置として持っているのは足元の 1 点だけなので、コマごとに
// 「この絵のどこが足元か」を教えてやらないと、技を出すたびに
// 絵がずれます。しゃがみでも跳び蹴りでも、原点さえ正しければ
// 絵は必ず正しい場所に出ます。
struct SpriteCell {
    int x = 0, y = 0, w = 0, h = 0; // シート内の切り出し位置と大きさ
    int originX = 0, originY = 0;   // コマ内での足元の位置
};

// コマの進め方。
//   Time  … 一定フレームごとに次のコマへ（立ち・歩きなど）
//   Phase … 技の段階（発生 / 持続 / 硬直）に合わせて 3 コマを使う
//
// 技に Phase を使うのがこのゲームの標準です。技の発生や硬直の
// フレーム数は data/moves/*.json でいつでも変えられるので、
// コマ数を固定で決め打ちすると、数値を変えたとたんに絵と
// 動きがずれます。段階に紐づけておけば、何フレームに変えても
// 「出始めの絵 → 当たっている絵 → 戻りの絵」が必ず合います。
enum class SpriteTiming { Time, Phase };

struct SpriteAnimation {
    std::vector<SpriteCell> Cells;
    int Hold = 4;                            // 1 コマを何フレーム見せるか（Time のとき）
    bool Loop = true;                        // 最後まで行ったら先頭へ戻るか
    SpriteTiming Timing = SpriteTiming::Time;

    // 今のフレーム数（と技の段階）から、表示するコマを選ぶ。
    const SpriteCell* CellAt(int frame, MovePhase phase) const;
};

// ---------------------------------------------------------------------
// キャラクター 1 人ぶんのスプライト
// ---------------------------------------------------------------------
struct CharacterSprites {
    SDL_Texture* Sheet = nullptr;                    // シート画像
    std::map<std::string, SpriteAnimation> Animations; // 名前 → アニメ
    bool StateTint = true; // ガード中を青く、食らい中を赤く…と色を付けるか

    bool Ready() const { return Sheet != nullptr && !Animations.empty(); }
    // 名前でアニメを探す（無ければ nullptr）。
    const SpriteAnimation* Find(const std::string& key) const;
};

// ---------------------------------------------------------------------
// 全キャラクターぶんのスプライト置き場
// ---------------------------------------------------------------------
// 画像は 1 度読んだら使い回します（毎フレーム読み直すと重いので）。
// ゲーム中に 1 つだけ存在すればよいので GetSprites() で共有します。
class SpriteLibrary {
public:
    // data/sprites/<id>/sprites.json と、保存フォルダ側の同じ場所を
    // 読み込みます。両方にあれば保存フォルダ側が勝ちます
    //（元データを壊さずに自分の絵を試せるように）。
    void LoadAll(SDL_Renderer* renderer,
                 const std::filesystem::path& baseDir,
                 const std::filesystem::path& userDir,
                 const std::vector<std::string>& characterIds);
    void Unload(); // 画像を解放する（終了時に呼ぶ）

    const CharacterSprites* Get(const std::string& characterId) const;

private:
    std::map<std::string, CharacterSprites> chars_;
};

SpriteLibrary& GetSprites();

// ---------------------------------------------------------------------
// 描画に使う関数
// ---------------------------------------------------------------------
// キャラクターの今の状態から、表示すべきコマを選ぶ。
// 絵が用意されていなければ nullptr を返します（＝図形で描く）。
const SpriteCell* PickFighterCell(const CharacterSprites& sprites, const Fighter& fighter);

// 状態に応じて絵に掛ける色（ガード＝青、食らい＝赤 など）。
// StateTint が false のときは常に白（＝色を変えない）。
Color FighterSpriteTint(const CharacterSprites& sprites, const Fighter& fighter);

// 立ちアニメの 1 コマ（キャラクター選択画面などで使う）。
const SpriteCell* PickIdleCell(const CharacterSprites& sprites, int frame);

// コマを 1 つ描く。(footX, footY) はキャラクターの足元の画面座標。
// facing が -1 なら左右反転して描きます（絵は右向きで描く決まり）。
//
// scale は表示倍率です。対戦中は必ず 1.0（等倍）で描きます。
// 小さくできるのは、キャラクター選択画面のように「絵を並べて見せる」
// 用途のためだけで、間合いに関わる対戦画面では使いません。
void DrawSpriteCell(Renderer& r, SDL_Texture* sheet, const SpriteCell& cell,
                    double footX, double footY, int facing,
                    Color tint = Color(255, 255, 255, 255), double scale = 1.0);

} // namespace kakuge

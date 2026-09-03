// =====================================================================
// platform/Figure.h - キャラクターの絵を描く
// =====================================================================
// このゲームには画像ファイル（スプライト）が入っていません。
// キャラクターは毎フレーム、円と多角形とカプセルを組み合わせて
// その場で描いています。
//
// なぜ画像を使わないのか
// -------------------
//  - 画像を用意しなくてもすぐ動かせる（絵が描けなくても作れる）
//  - 姿勢を数値で指定できるので、腕の伸ばし具合や体の傾きを
//    自由に変えられる。ヒットの強さに応じて仰け反り具合を
//    変える、といった調整がコード側だけでできます。
//  - 拡大率が変わっても、線の太さまで含めてきれいに追従します。
//
// 体の作り
// -------
// 「フィギュア単位」という内部の単位で組み立てています。
// 立ち姿の全高が 108 単位（頭 24 ＋ 首 2 ＋ 胴 38 ＋ 脚 46）。
// これに拡大率を掛けて画面ピクセルにします。
// 95 ピクセル（仕様の身長）÷ 108 単位 = 約 0.88 が基準倍率です。
// =====================================================================
#pragma once
#include "engine/BattleSystem.h"
#include "engine/Fighter.h"
#include "platform/Renderer.h"

namespace kakuge {

// 108 フィギュア単位を 95 ピクセル（仕様の身長）にする倍率。
constexpr double kCharScale = static_cast<double>(GameSpec::CharacterVisualHeight) / 108.0;

// 姿勢の指定。数値を変えるだけで見た目が変わります。
struct HumanoidPose {
    double heightScale = 1.0; // 全体の大きさ（対戦中は常に 1.0）
    int facing = 1;           // 向き（+1 右 / -1 左）
    double armReach = 0.0;    // 前腕を伸ばす量（パンチ）
    double legKick = 0.0;     // 脚を伸ばす量（キック）
    double leanBack = 0.0;    // 体の傾き（のけぞり）
    double guardRaise = 0.0;  // 腕を上げる量（ガード）
    bool crouch = false;      // しゃがみ姿勢
    bool jump = false;        // 空中姿勢（脚を抱える）
    bool lying = false;       // 倒れている姿（ダウン・KO）
    int idleFrame = 0;        // 立ちの呼吸アニメ（0-3）
};

// 足元の座標 (sx, sy) を基準にキャラクターを 1 体描く。
// キャラクター選択画面などでも使うので、カメラとは切り離してあります。
void DrawHumanoid(Renderer& r, double sx, double sy, Color color, const HumanoidPose& pose);

// 試合中のキャラクターを、状態に応じた姿勢で描く。
// カメラの拡大率・位置は内部で適用します。
//
// data/sprites/<キャラクター ID>/ に絵が置いてあれば、
// 図形ではなくその絵で描きます（platform/Sprite.h）。
void DrawFighter(Renderer& r, const Fighter& fighter);

// スプライトだけで描く（絵が無ければ false）。DrawFighter が
// 内部で呼びます。外から使うことはまずありません。
bool DrawFighterSprite(Renderer& r, const Fighter& fighter);

// 立ち絵を 1 コマ描く（キャラクター選択画面むけ）。絵が無ければ false。
// scale は表示倍率（図形描画の HumanoidPose::heightScale と同じ意味）。
bool DrawIdleSprite(Renderer& r, const std::string& characterId,
                    double sx, double sy, int facing, int frame, double scale = 1.0);

// 飛び道具を描く。
void DrawProjectile(Renderer& r, const Projectile& proj);

// ヒット時などの光る演出。
struct EffectStyle { Color color; double radius; double duration; };
EffectStyle GetEffectStyle(const std::string& kind);

// 画面に出ている演出 1 個ぶん（age は出てからの経過秒数）。
struct LiveEffect { std::string kind; double x, y, age; int side = 0; };
void DrawEffect(Renderer& r, const LiveEffect& fx);

// カウンターヒットの文字を、決めた側の画面端に出す。
void DrawCounterEdgeLabel(Renderer& r, const LiveEffect& fx);

// 技の種類に応じた体の色（必殺技なら紫がかる、など）。
Color MoveTint(const Fighter& fighter);

} // namespace kakuge

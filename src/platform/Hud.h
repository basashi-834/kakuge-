// =====================================================================
// platform/Hud.h - 対戦画面の情報表示（体力ゲージ・制限時間など）
// =====================================================================
// HUD は Head-Up Display の略で、ゲーム画面に重ねて出す情報表示のことです。
// このゲームでは次のものを出します。
//
//   画面上部（高さ 30px）… 顔アイコン、体力ゲージ、名前、残り時間
//   画面下部（高さ 18px）… 超必殺技ゲージ
//   場面に応じて         … 「FIGHT」表示、コンボ数、デバッグ情報
//
// 384x224 という狭い画面なので、余白の取り方が非常にシビアです。
// 数値はすべて実際に表示して確認しながら決めています。
// =====================================================================
#pragma once
#include "engine/BattleSystem.h"
#include "platform/Renderer.h"

namespace kakuge {

// 横向きのゲージを 1 本描く。
//   ratio  … 0.0〜1.0 の割合
//   mirror … true にすると右端から左へ減っていく（2P 用）
void DrawBar(Renderer& r, float x, float y, float w, float h, double ratio,
             Color fillColor, Color emptyColor, bool mirror, Color borderColor);

// 対戦画面の情報表示ひとそろい。
//   p1ComboDisplay / p2ComboDisplay … 表示中のコンボ数
//   comboFade … コンボ表示の残り濃さ（1.0 → 0.0 で消える）
void DrawHUD(Renderer& r, const BattleSystem& bs,
             int p1ComboDisplay, int p2ComboDisplay, double comboFade);

// デバッグ表示（トレーニングモードで F1）。
// すべての当たり判定の四角形と、フレーム数などの数値を表示します。
// 技を作るときはこれを見ながら調整します。
void DrawDebugOverlay(Renderer& r, const BattleSystem& bs);

} // namespace kakuge

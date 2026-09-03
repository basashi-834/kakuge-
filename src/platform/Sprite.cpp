// =====================================================================
// platform/Sprite.cpp - Sprite.h の中身（読み込みと、コマの選び方）
// =====================================================================
#include "platform/Sprite.h"

#include <algorithm>
#include <cmath>

#include "core/Json.h"

namespace kakuge {

namespace fs = std::filesystem;

namespace {

// シート画像（BMP）を読んでテクスチャにする。
//
// 手順は 3 つです。
//   1. SDL_LoadBMP でファイルを読む（SDL2 本体の機能。追加ライブラリ不要）
//   2. 透明にしたい色を指定する（カラーキー）
//   3. テクスチャに変換し、拡大方法を「にじませない」に設定する
SDL_Texture* LoadSheet(SDL_Renderer* renderer, const fs::path& path,
                       bool useColorKey, int keyR, int keyG, int keyB) {
    SDL_Surface* surface = SDL_LoadBMP(path.string().c_str());
    if (!surface) {
        SDL_Log("スプライトのシートを読めませんでした: %s (%s)",
                path.string().c_str(), SDL_GetError());
        return nullptr;
    }
    if (useColorKey) {
        // MapRGB は「この画像の形式でこの色を表す値」を返します。
        // 画像が 8bit でも 24bit でも、同じ書き方で指定できます。
        SDL_SetColorKey(surface, SDL_TRUE,
                        SDL_MapRGB(surface->format,
                                   static_cast<Uint8>(keyR),
                                   static_cast<Uint8>(keyG),
                                   static_cast<Uint8>(keyB)));
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!tex) {
        SDL_Log("スプライトのテクスチャを作れませんでした: %s (%s)",
                path.string().c_str(), SDL_GetError());
        return nullptr;
    }
    // 内部キャンバスごと整数倍に拡大するので、ここでにじませないことが
    // ドット絵をくっきり保つ条件です（Renderer.cpp と同じ理由）。
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

// アニメーション 1 本ぶんの JSON を読む。
//
// コマの指定には 2 つの書き方があり、混ぜても使えます。
//   "cells":  [0, 1, 2]   … 格子（cellWidth x cellHeight）の何番目か
//   "frames": [{...}]     … 切り出し位置を直接書く（不揃いな絵むけ）
SpriteAnimation ParseAnimation(const Json& node, int cellW, int cellH, int columns,
                               int defOriginX, int defOriginY) {
    SpriteAnimation anim;
    anim.Hold = std::max(1, node.GetInt("hold", 4));
    anim.Loop = node.GetBool("loop", true);
    anim.Timing = (node.GetString("mode", "time") == "phase") ? SpriteTiming::Phase
                                                              : SpriteTiming::Time;
    int originX = node.GetInt("originX", defOriginX);
    int originY = node.GetInt("originY", defOriginY);

    if (const Json* cells = node.Find("cells"); cells && cells->IsArray() && columns > 0) {
        for (const Json& c : cells->Items()) {
            int index = c.AsInt(0);
            SpriteCell cell;
            cell.x = (index % columns) * cellW;
            cell.y = (index / columns) * cellH;
            cell.w = cellW;
            cell.h = cellH;
            cell.originX = originX;
            cell.originY = originY;
            anim.Cells.push_back(cell);
        }
    }
    if (const Json* frames = node.Find("frames"); frames && frames->IsArray()) {
        for (const Json& f : frames->Items()) {
            SpriteCell cell;
            cell.x = f.GetInt("x", 0);
            cell.y = f.GetInt("y", 0);
            cell.w = f.GetInt("w", cellW);
            cell.h = f.GetInt("h", cellH);
            cell.originX = f.GetInt("originX", originX);
            cell.originY = f.GetInt("originY", originY);
            anim.Cells.push_back(cell);
        }
    }
    return anim;
}

// sprites.json を 1 つ読む。読めなければ false（＝図形描画のまま）。
bool LoadCharacterSprites(SDL_Renderer* renderer, const fs::path& dir, CharacterSprites& out) {
    fs::path jsonPath = dir / "sprites.json";
    std::error_code ec;
    if (!fs::is_regular_file(jsonPath, ec)) return false;

    Json data;
    std::string error;
    if (!Json::LoadFile(jsonPath.string(), data, &error)) {
        SDL_Log("sprites.json を読めませんでした: %s (%s)",
                jsonPath.string().c_str(), error.c_str());
        return false;
    }

    // ---- シート画像 ----
    std::string sheetName = data.GetString("sheet", "");
    if (sheetName.empty()) return false;

    // 透明色。"transparent": null と書けば画像側の透明度を使います。
    bool useColorKey = true;
    int keyR = 255, keyG = 0, keyB = 255; // 既定はマゼンタ
    if (const Json* t = data.Find("transparent")) {
        if (t->IsNull()) {
            useColorKey = false;
        } else if (t->IsArray() && t->Size() >= 3) {
            keyR = t->At(0).AsInt(255);
            keyG = t->At(1).AsInt(0);
            keyB = t->At(2).AsInt(255);
        }
    }
    SDL_Texture* sheet = LoadSheet(renderer, dir / sheetName, useColorKey, keyR, keyG, keyB);
    if (!sheet) return false;

    // ---- 格子の大きさ（"cells" 指定で使う）----
    int cellW = data.GetInt("cellWidth", GameSpec::CharacterSpriteWidth);
    int cellH = data.GetInt("cellHeight", GameSpec::CharacterSpriteHeight);
    int columns = std::max(1, data.GetInt("columns", 1));
    int originX = data.GetInt("originX", cellW / 2);
    int originY = data.GetInt("originY", cellH);

    CharacterSprites sprites;
    sprites.Sheet = sheet;
    sprites.StateTint = data.GetBool("stateTint", true);

    if (const Json* anims = data.Find("animations"); anims && anims->IsObject()) {
        for (const auto& kv : anims->Members()) {
            SpriteAnimation anim = ParseAnimation(kv.second, cellW, cellH, columns,
                                                  originX, originY);
            if (!anim.Cells.empty()) sprites.Animations[kv.first] = anim;
        }
    }

    if (sprites.Animations.empty()) {
        SDL_Log("sprites.json にコマが 1 つもありません: %s", jsonPath.string().c_str());
        SDL_DestroyTexture(sheet);
        return false;
    }
    out = sprites;
    return true;
}

// 候補の名前を順に試して、最初に見つかったアニメを返す。
// 「専用の絵が無ければ、近い絵で代用する」ための仕組みです。
const SpriteAnimation* FindFirst(const CharacterSprites& sprites,
                                 std::initializer_list<std::string> keys) {
    for (const std::string& key : keys) {
        if (const SpriteAnimation* a = sprites.Find(key)) return a;
    }
    return nullptr;
}

SpriteLibrary g_Sprites;

} // namespace

// ---------------------------------------------------------------------
// コマの選択
// ---------------------------------------------------------------------
const SpriteCell* SpriteAnimation::CellAt(int frame, MovePhase phase) const {
    if (Cells.empty()) return nullptr;
    const int count = static_cast<int>(Cells.size());

    if (Timing == SpriteTiming::Phase) {
        // 発生 → 0 コマ目、持続 → 1 コマ目、硬直（と終了）→ 2 コマ目。
        // コマが足りなければ、あるところまでで止めます。
        int index = 0;
        switch (phase) {
            case MovePhase::Startup: index = 0; break;
            case MovePhase::Active: index = 1; break;
            case MovePhase::Recovery:
            case MovePhase::Done: index = 2; break;
        }
        return &Cells[std::min(index, count - 1)];
    }

    if (frame < 0) frame = 0;
    int index = frame / Hold;
    if (Loop) index %= count;
    else index = std::min(index, count - 1);
    return &Cells[index];
}

const SpriteAnimation* CharacterSprites::Find(const std::string& key) const {
    auto it = Animations.find(key);
    return it == Animations.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------
// 読み込み
// ---------------------------------------------------------------------
void SpriteLibrary::LoadAll(SDL_Renderer* renderer, const fs::path& baseDir,
                            const fs::path& userDir,
                            const std::vector<std::string>& characterIds) {
    Unload();
    if (!renderer) return;
    for (const std::string& id : characterIds) {
        CharacterSprites sprites;
        // 後に読んだほうが勝ちます（保存フォルダ側 ＞ data フォルダ側）。
        bool found = LoadCharacterSprites(renderer, baseDir / "sprites" / id, sprites);
        CharacterSprites userSprites;
        if (LoadCharacterSprites(renderer, userDir / "sprites" / id, userSprites)) {
            if (found && sprites.Sheet) SDL_DestroyTexture(sprites.Sheet);
            sprites = userSprites;
            found = true;
        }
        if (found) {
            chars_[id] = sprites;
            SDL_Log("スプライトを読み込みました: %s（%zu 種類）",
                    id.c_str(), sprites.Animations.size());
        }
    }
}

void SpriteLibrary::Unload() {
    for (auto& kv : chars_) {
        if (kv.second.Sheet) SDL_DestroyTexture(kv.second.Sheet);
    }
    chars_.clear();
}

const CharacterSprites* SpriteLibrary::Get(const std::string& characterId) const {
    auto it = chars_.find(characterId);
    if (it == chars_.end() || !it->second.Ready()) return nullptr;
    return &it->second;
}

SpriteLibrary& GetSprites() { return g_Sprites; }

// ---------------------------------------------------------------------
// 状態 → アニメ名
// ---------------------------------------------------------------------
// 用意されていない名前は、近いものへ順に降りていきます。
// いちばん最後は必ず "idle" なので、立ち絵 1 枚だけ用意した状態でも
// 破綻せずに動きます（全部が立ち絵で表示されます）。
const SpriteCell* PickFighterCell(const CharacterSprites& sprites, const Fighter& fighter) {
    const StateMachine& sm = fighter.SM;
    int frame = sm.CurrentFrame;
    MovePhase phase = MovePhase::Startup;
    const SpriteAnimation* anim = nullptr;

    bool airborne = fighter.PositionY < (Fighter::GroundY - 1.0);

    switch (sm.CurrentState) {
        case CharState::Attack: {
            if (fighter.CurrentMoveData) {
                const MoveData& move = *fighter.CurrentMoveData;
                phase = MoveExecutor::GetPhase(move, sm.CurrentFrame);
                // 技ごとの絵（"move:立ち強パンチの ID"）が最優先。
                anim = sprites.Find("move:" + move.Id);
                if (!anim) {
                    // 無ければ「姿勢＋パンチ / キック」で代用します。
                    // ボタン名の末尾が K ならキックです（LK / MK / HK）。
                    bool kick = !move.Button.empty() && move.Button.back() == 'K';
                    std::string kind = kick ? "kick" : "punch";
                    std::string stance = airborne ? "jump_" :
                                         (move.Stance == "crouch" ? "crouch_" : "");
                    anim = FindFirst(sprites, {stance + kind, kind, "idle"});
                }
            } else {
                anim = FindFirst(sprites, {"punch", "idle"});
            }
            break;
        }
        case CharState::WalkForward:
            anim = FindFirst(sprites, {"walk_forward", "walk", "idle"});
            break;
        case CharState::WalkBackward:
            anim = FindFirst(sprites, {"walk_backward", "walk", "idle"});
            break;
        case CharState::Crouch:
            anim = FindFirst(sprites, {"crouch", "idle"});
            break;
        case CharState::WakeUp:
            anim = FindFirst(sprites, {"wakeup", "crouch", "idle"});
            break;
        case CharState::Jump:
            anim = FindFirst(sprites, {"jump", "idle"});
            break;
        case CharState::Block:
            anim = fighter.IsCrouchingGuard
                       ? FindFirst(sprites, {"block_crouch", "crouch_block", "block", "crouch", "idle"})
                       : FindFirst(sprites, {"block", "idle"});
            break;
        case CharState::Hitstun:
            anim = airborne ? FindFirst(sprites, {"hitstun_air", "hitstun", "jump", "idle"})
                            : FindFirst(sprites, {"hitstun", "idle"});
            break;
        case CharState::Throw:
            anim = FindFirst(sprites, {"throw", "hitstun", "idle"});
            break;
        case CharState::Knockdown:
            anim = FindFirst(sprites, {"knockdown", "dead", "hitstun", "idle"});
            break;
        case CharState::Dead:
            anim = FindFirst(sprites, {"dead", "knockdown", "hitstun", "idle"});
            break;
        case CharState::Idle:
        default:
            anim = sprites.Find("idle");
            // 立ちだけは試合開始からの通し数を使います。状態が
            // 切り替わるたびに呼吸アニメが 1 コマ目へ戻ると、
            // 歩いて止まるたびにカクついて見えるためです。
            frame = fighter.FrameCounter;
            break;
    }
    if (!anim) return nullptr;
    return anim->CellAt(frame, phase);
}

const SpriteCell* PickIdleCell(const CharacterSprites& sprites, int frame) {
    const SpriteAnimation* anim = sprites.Find("idle");
    if (!anim) return nullptr;
    return anim->CellAt(frame, MovePhase::Startup);
}

// ---------------------------------------------------------------------
// 状態に応じた色
// ---------------------------------------------------------------------
// 図形描画では、ガード中は体ごと青、食らい中は赤に塗り替えて
// 「今どうなっているか」を一目で分かるようにしています。
// スプライトでも同じ手掛かりを残したいので、絵に色を薄く掛けます
//（掛けすぎると絵柄が潰れるので、白と混ぜて 45% だけ効かせます）。
//
// 絵の色をそのまま出したい場合は sprites.json に
//   "stateTint": false
// と書いてください。
Color FighterSpriteTint(const CharacterSprites& sprites, const Fighter& fighter) {
    const Color white(255, 255, 255, 255);
    if (!sprites.StateTint) return white;

    auto blend = [&](Color c) {
        const double k = 0.45; // 効かせ具合
        return Color(static_cast<int>(255 + (c.r - 255) * k),
                     static_cast<int>(255 + (c.g - 255) * k),
                     static_cast<int>(255 + (c.b - 255) * k), 255);
    };

    // 色を掛けるのは「守り・食らい」の 3 つだけにしています。
    // 図形描画では必殺技も紫に染めていましたが、絵を描いた場合は
    // 技のたびに全身の色が変わってしまい、せっかくの絵柄が
    // 台無しになります。必殺技はヒット時の演出（紫の光）で
    // 十分に分かるので、絵はそのままの色で出します。
    switch (fighter.SM.CurrentState) {
        case CharState::Block: return blend(Color(60, 120, 210));   // 青
        case CharState::Hitstun:
        case CharState::Throw: return blend(Color(220, 60, 60));    // 赤
        case CharState::Dead: return blend(Color(150, 150, 150));   // 灰
        default: return white;
    }
}

// ---------------------------------------------------------------------
// 描く
// ---------------------------------------------------------------------
void DrawSpriteCell(Renderer& r, SDL_Texture* sheet, const SpriteCell& cell,
                    double footX, double footY, int facing, Color tint, double scale) {
    if (!sheet || cell.w <= 0 || cell.h <= 0 || scale <= 0.0) return;

    double w = cell.w * scale, h = cell.h * scale;
    double ox = cell.originX * scale, oy = cell.originY * scale;

    // 絵は「右向き」で描く決まりです。左を向いているときは左右反転
    // して描きますが、そのとき原点も反対側から数えることになります。
    //   右向き … コマの左端から originX だけ右が足元
    //   左向き … コマの右端から originX だけ左が足元
    //             ＝ 左端からは (w - originX)
    double left = std::round(footX) - (facing < 0 ? (w - ox) : ox);
    double top = std::round(footY) - oy;

    SDL_Rect src{cell.x, cell.y, cell.w, cell.h};
    r.DrawTexture(sheet, src, static_cast<float>(left), static_cast<float>(top),
                  static_cast<float>(w), static_cast<float>(h), facing < 0, tint);
}

} // namespace kakuge

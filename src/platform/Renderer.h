// =====================================================================
// platform/Renderer.h - SDL2 を使った描画の土台
// =====================================================================
// このゲームの絵は、次の 2 段階で画面に出ます。
//
//   (1) 384x224 ピクセルの「内部キャンバス」に全部描く
//   (2) それをウィンドウの大きさまで、拡大して 1 枚のっぺり貼り付ける
//
// なぜこんな面倒なことをするのか
// ---------------------------
// 1990 年代のアーケード格闘ゲームは 384x224 という小さな画面でした。
// 今の大きなディスプレイにその雰囲気を出すには、小さく描いてから
// 拡大するのが一番確実です。しかもこのとき「ニアレストネイバー」
// （最も近い点をそのままコピーする）方式で拡大すると、
// ドットがくっきり四角いまま大きくなり、本物のドット絵に見えます。
// 逆に、最初から大きな画面に直接描くと、線の太さやフォントの大きさを
// 解像度ごとに調整しなければならず、大変な手間になります。
//
// SDL2 でどうやって図形を塗るのか
// ---------------------------
// SDL2 が最初から持っている描画機能は、点・線・四角形の 3 つだけです。
// 円や多角形を塗る関数はありません。
// そこで SDL_RenderGeometry という関数を使います。これは
// 「三角形をたくさん並べて描く」ための関数で、SDL 2.0.18 から
// 使えるようになりました（このプロジェクトが SDL 2.0.18 以降を
// 必要とする理由がこれです）。
//
// どんな形も三角形の集まりで表せます。
//   円      → 中心から扇状に三角形を並べる（32 分割ほどで十分きれい）
//   多角形  → 1 つの頂点を軸に扇状に分割する（凸多角形なら必ず成功）
//   カプセル→ 長方形（三角形 2 個）＋ 両端の円
// このファイルはその変換をまとめて引き受けます。
// =====================================================================
#pragma once
#include <SDL.h>

#include <cstdint>
#include <vector>

namespace kakuge {

// 内部キャンバスの大きさ。すべての座標はこの中の数値で書きます。
//
// 「基準」と「実際」の 2 つがあります
// -------------------------------
// 設計の基準は 384x224（1990 年代のアーケード基板 CPS2 と同じ）です。
// ただし実際のウィンドウは 16:9 だったり、ユーザーが自由な形に
// 引き伸ばしたりします。そこで基準の比率のまま拡大すると、
// 上下または左右に黒い余白（レターボックス）が残ります。
//
// そこで、内部キャンバスの大きさをウィンドウの形に合わせて
// 毎フレーム決め直します。高さ 224・幅 384 を「最低限これだけは
// 映す」下限として、余ったぶんだけキャンバスを広げるので、
// 黒い余白が出ません。広がったぶんには背景がそのまま続きます。
//
// 拡大率は必ず整数倍のままなので、ドットの大きさは一定です
//（ここを崩すと、ドット絵は途端に汚くなります）。
//
// VirtualW / VirtualH は実行時に変わる値です。画面の配置は
// これらを基準に書いてください（数値を直接書かない）。
constexpr int BaseVirtualW = 384;
constexpr int BaseVirtualH = 224;
// キャンバスを広げる上限。極端に細長いウィンドウで、際限なく
// 広い世界が見えてしまわないようにするための歯止めです。
//
// 640x480 にしてあるのは、よくあるウィンドウの形をひととおり
// 余白無しで収められる大きさだからです。
//   1920x1080 → 4 倍 → 480x270
//   1280x720  → 3 倍 → 427x240
//   1024x768  → 2 倍 → 512x384   ← 4:3。ここが一番縦に大きい
//   3440x1440 → 6 倍 → 574x240   ← 横長。ここが一番横に大きい
// 横 640 はステージ幅 850 より狭いので、カメラの端の処理も
// これまでどおり成り立ちます。
constexpr int MaxVirtualW = 640;
constexpr int MaxVirtualH = 480;

extern int VirtualW;
extern int VirtualH;

// ウィンドウの大きさから「拡大率」と「内部キャンバスの大きさ」を求める。
// Renderer と、マウス座標の変換（Game::WindowToVirtual）の両方が
// まったく同じ答えを使う必要があるので、計算はここ 1 か所だけです。
struct CanvasLayout {
    int scale = 1;    // 拡大率（整数）
    int width = BaseVirtualW;   // 内部キャンバスの幅
    int height = BaseVirtualH;  // 内部キャンバスの高さ
    int offsetX = 0;  // ウィンドウ内での表示開始位置
    int offsetY = 0;
};
CanvasLayout ComputeCanvasLayout(int windowW, int windowH);

// 色（赤・緑・青・不透明度、それぞれ 0-255）。
// a が 0 だと完全に透明、255 だと完全に不透明です。
struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    constexpr Color() = default;
    constexpr Color(int rr, int gg, int bb, int aa = 255)
        : r(static_cast<std::uint8_t>(rr)), g(static_cast<std::uint8_t>(gg)),
          b(static_cast<std::uint8_t>(bb)), a(static_cast<std::uint8_t>(aa)) {}

    // 不透明度だけ変えた色を作る（フェードアウトの演出などに使う）。
    Color WithAlpha(int newAlpha) const { return Color(r, g, b, newAlpha); }
    // 明るさを変えた色を作る（f が 1 より小さいと暗く、大きいと明るく）。
    Color Scaled(double f) const;
};

struct Vec2 {
    float x = 0, y = 0;
};

class Renderer {
public:
    // SDL の準備ができたあとに 1 回だけ呼びます。
    // 内部キャンバス用のテクスチャ（描き込み先の画像）を作ります。
    bool Init(SDL_Renderer* sdlRenderer);
    void Shutdown();

    // 1 フレームの描画の始めと終わり。
    //   BeginFrame … ウィンドウの形に合った内部キャンバスを用意し、
    //                描き込み先をそこへ切り替える
    //   EndFrame   … キャンバスをウィンドウに拡大表示して、画面に出す
    //
    // BeginFrame にウィンドウの大きさを渡すのは、キャンバスの大きさを
    // 「描き始める前」に決めないといけないからです。描いたあとで
    // 大きさを変えると、その 1 フレームだけ配置がずれます。
    void BeginFrame(int windowW, int windowH);
    void EndFrame(int windowW, int windowH);

    SDL_Renderer* Raw() const { return sdl_; }

    // ---- 基本の描画 ----
    void Clear(Color color);
    void FillRect(float x, float y, float w, float h, Color color);
    // 枠だけ描く（thickness は線の太さ。内側に向かって太くなります）
    void DrawRect(float x, float y, float w, float h, Color color, float thickness = 1.0f);
    void DrawLine(float x1, float y1, float x2, float y2, Color color, float thickness = 1.0f);

    // ---- SDL_RenderGeometry を使う描画 ----
    // 凸多角形を塗る（へこみのある形は正しく塗れません）。
    void FillPolygon(const Vec2* points, int count, Color color);
    // 円・楕円
    void FillCircle(float cx, float cy, float r, Color color);
    void FillEllipse(float cx, float cy, float rx, float ry, Color color);
    // 半円（上半分だけ塗る。キャラクターの髪を描くのに使います）
    void FillTopHalfCircle(float cx, float cy, float r, Color color);
    // カプセル（両端が丸い太い線）。腕や脚を描くのに使います。
    void FillCapsule(float x1, float y1, float x2, float y2, float thickness, Color color);
    // 上下方向のグラデーション。SDL_RenderGeometry は頂点ごとに色を
    // 指定できるので、四角形の上辺と下辺に違う色を置くだけで作れます。
    void FillGradientRect(float x, float y, float w, float h, Color top, Color bottom);

    // ---- 画像（テクスチャ）を貼る ----
    // スプライト（platform/Sprite.h）を描くために使います。
    //   src   … 画像のどこを切り出すか
    //   x,y,w,h … 内部キャンバスのどこへ、どの大きさで貼るか
    //   flipX … true なら左右反転（絵は右向きだけ用意すれば済みます）
    //   mod   … 掛ける色。白（255,255,255,255）なら元の絵のまま。
    //           ガード中を青くする、といった色付けに使います。
    void DrawTexture(SDL_Texture* texture, const SDL_Rect& src,
                     float x, float y, float w, float h,
                     bool flipX, Color mod = Color(255, 255, 255, 255));

    // ---- 描画範囲の制限（クリップ）----
    // 指定した四角形の外にはみ出した部分を描かないようにします。
    // ゲージを「割合ぶんだけ」表示するときなどに使います。
    void SetClip(float x, float y, float w, float h);
    void ClearClip();

    // 画面全体を揺らすためのずらし量（カウンターヒットの演出）。
    // ここに値を入れると、以降のすべての描画がその分ずれます。
    void SetShake(float dx, float dy) { shakeX_ = dx; shakeY_ = dy; }

private:
    // 内部キャンバスを指定の大きさで作り直す（必要なときだけ）。
    bool EnsureCanvas(int w, int h);

    SDL_Renderer* sdl_ = nullptr;
    SDL_Texture* canvas_ = nullptr;
    int canvasW_ = 0, canvasH_ = 0;
    float shakeX_ = 0.0f, shakeY_ = 0.0f;

    // 頂点を組み立てるときの一時置き場。毎回 new し直さないように
    // メンバとして持ち回しています（毎フレーム何百回も呼ばれるため）。
    std::vector<SDL_Vertex> verts_;
    std::vector<int> indices_;

    // points を「扇状の三角形」に分解して SDL に渡す。
    void SubmitFan(const Vec2* points, int count, Color color);
};

} // namespace kakuge

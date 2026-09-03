// =====================================================================
// platform/Renderer.cpp - Renderer.h の中身
// =====================================================================
#include "platform/Renderer.h"

#include <algorithm>
#include <cmath>

namespace kakuge {

namespace {
// 円をいくつの三角形で近似するか。
// 内部キャンバスは 384x224 と小さく、円の直径もせいぜい 30 ピクセル
// 程度なので、24 分割もあれば人の目には完全な円に見えます。
// 増やすときれいになりますが、そのぶん描画の手間が増えます。
constexpr int kCircleSegments = 24;

inline SDL_Color ToSdl(Color c) {
    SDL_Color out;
    out.r = c.r; out.g = c.g; out.b = c.b; out.a = c.a;
    return out;
}
} // namespace

Color Color::Scaled(double f) const {
    auto clamp255 = [](double v) {
        return static_cast<int>(std::max(0.0, std::min(255.0, v)));
    };
    return Color(clamp255(r * f), clamp255(g * f), clamp255(b * f), a);
}

// ---------------------------------------------------------------------
// 準備と後片付け
// ---------------------------------------------------------------------
bool Renderer::Init(SDL_Renderer* sdlRenderer) {
    sdl_ = sdlRenderer;
    if (!sdl_) return false;

    // 内部キャンバス。TEXTUREACCESS_TARGET は「このテクスチャに
    // 描き込める」という指定です（普通のテクスチャは表示専用）。
    canvas_ = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_TARGET, VirtualW, VirtualH);
    if (!canvas_) return false;

    // ここが「くっきりドット絵」の要。SDL_ScaleModeNearest は
    // 拡大するときに色を混ぜず、1 ピクセルをそのまま四角く引き伸ばします。
    // 既定の Linear だとぼやけた絵になってしまいます。
    SDL_SetTextureScaleMode(canvas_, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(canvas_, SDL_BLENDMODE_BLEND);
    // 半透明を正しく重ねるための設定。
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    return true;
}

void Renderer::Shutdown() {
    if (canvas_) {
        SDL_DestroyTexture(canvas_);
        canvas_ = nullptr;
    }
    sdl_ = nullptr;
}

// ---------------------------------------------------------------------
// フレームの開始と終了
// ---------------------------------------------------------------------
void Renderer::BeginFrame() {
    // 以降の描画命令はすべて内部キャンバスに向かいます。
    SDL_SetRenderTarget(sdl_, canvas_);
    shakeX_ = shakeY_ = 0.0f;
}

void Renderer::EndFrame(int windowW, int windowH) {
    // 描き込み先をウィンドウに戻す
    SDL_SetRenderTarget(sdl_, nullptr);
    SDL_SetRenderDrawColor(sdl_, 0, 0, 0, 255);
    SDL_RenderClear(sdl_); // 余白（レターボックス）は黒で塗る

    if (windowW <= 0 || windowH <= 0) { SDL_RenderPresent(sdl_); return; }

    // 縦横比を保ったまま、はみ出さない最大の倍率を求めます。
    double scale = std::min(static_cast<double>(windowW) / VirtualW,
                            static_cast<double>(windowH) / VirtualH);
    // 2 倍以上にできるなら整数倍にそろえます。
    // 2.7 倍のような半端な倍率だと、ドットの大きさが場所によって
    // 2 ピクセルだったり 3 ピクセルだったりしてムラが出るためです。
    if (scale >= 2.0) scale = std::floor(scale);

    SDL_Rect dest;
    dest.w = static_cast<int>(VirtualW * scale);
    dest.h = static_cast<int>(VirtualH * scale);
    dest.x = (windowW - dest.w) / 2;  // 中央に配置
    dest.y = (windowH - dest.h) / 2;
    SDL_RenderCopy(sdl_, canvas_, nullptr, &dest);
    SDL_RenderPresent(sdl_); // ここで実際に画面が切り替わる
}

// ---------------------------------------------------------------------
// 基本の描画
// ---------------------------------------------------------------------
void Renderer::Clear(Color color) {
    SDL_SetRenderDrawColor(sdl_, color.r, color.g, color.b, color.a);
    SDL_RenderClear(sdl_);
}

void Renderer::FillRect(float x, float y, float w, float h, Color color) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(sdl_, color.r, color.g, color.b, color.a);
    SDL_FRect rect{x + shakeX_, y + shakeY_, w, h};
    SDL_RenderFillRectF(sdl_, &rect);
}

void Renderer::DrawRect(float x, float y, float w, float h, Color color, float thickness) {
    if (w <= 0 || h <= 0) return;
    float t = std::max(1.0f, thickness);
    // 4 本の細長い四角形として描きます。上下は角まで伸ばし、
    // 左右はその内側だけにすることで、角が二重に塗られません
    //（半透明の色でも角だけ濃くならない）。
    FillRect(x, y, w, t, color);                          // 上
    FillRect(x, y + h - t, w, t, color);                  // 下
    FillRect(x, y + t, t, h - t * 2, color);              // 左
    FillRect(x + w - t, y + t, t, h - t * 2, color);      // 右
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, Color color, float thickness) {
    if (thickness <= 1.01f) {
        // 細い線は SDL の線描画で十分（速い）
        SDL_SetRenderDrawColor(sdl_, color.r, color.g, color.b, color.a);
        SDL_RenderDrawLineF(sdl_, x1 + shakeX_, y1 + shakeY_, x2 + shakeX_, y2 + shakeY_);
        return;
    }
    // 太い線は「細長い四角形」として多角形で描きます。
    // 線に対して垂直な方向へ、太さの半分だけずらした 4 点を作ります。
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * thickness * 0.5f; // 垂直な向きの単位ベクトル×半分の太さ
    float ny = dx / len * thickness * 0.5f;
    Vec2 pts[4] = {
        {x1 + nx, y1 + ny}, {x2 + nx, y2 + ny},
        {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny},
    };
    FillPolygon(pts, 4, color);
}

// ---------------------------------------------------------------------
// SDL_RenderGeometry を使う描画
// ---------------------------------------------------------------------
// 多角形を「1 番目の頂点を中心にした扇」として三角形に分解します。
//   頂点 0-1-2、0-2-3、0-3-4 ... という組み合わせ。
// 凸多角形（へこみが無い形）ならこれで必ず正しく塗りつぶせます。
void Renderer::SubmitFan(const Vec2* points, int count, Color color) {
    if (count < 3) return;
    verts_.clear();
    indices_.clear();
    verts_.reserve(static_cast<size_t>(count));
    SDL_Color sc = ToSdl(color);
    for (int i = 0; i < count; ++i) {
        SDL_Vertex v{};
        v.position.x = points[i].x + shakeX_;
        v.position.y = points[i].y + shakeY_;
        v.color = sc;
        v.tex_coord.x = 0.0f; // テクスチャは使わないので 0 のまま
        v.tex_coord.y = 0.0f;
        verts_.push_back(v);
    }
    for (int i = 1; i + 1 < count; ++i) {
        indices_.push_back(0);
        indices_.push_back(i);
        indices_.push_back(i + 1);
    }
    // 第 2 引数の nullptr は「テクスチャを貼らず、頂点の色だけで塗る」
    // という意味です。
    SDL_RenderGeometry(sdl_, nullptr, verts_.data(), static_cast<int>(verts_.size()),
                       indices_.data(), static_cast<int>(indices_.size()));
}

void Renderer::FillPolygon(const Vec2* points, int count, Color color) {
    SubmitFan(points, count, color);
}

void Renderer::FillEllipse(float cx, float cy, float rx, float ry, Color color) {
    if (rx <= 0.0f || ry <= 0.0f) return;
    // 円周上の点を等間隔に並べた多角形として描きます。
    Vec2 pts[kCircleSegments];
    for (int i = 0; i < kCircleSegments; ++i) {
        float angle = static_cast<float>(i) / kCircleSegments * 6.28318530718f; // 2π
        pts[i].x = cx + std::cos(angle) * rx;
        pts[i].y = cy + std::sin(angle) * ry;
    }
    SubmitFan(pts, kCircleSegments, color);
}

void Renderer::FillCircle(float cx, float cy, float r, Color color) {
    FillEllipse(cx, cy, r, r, color);
}

void Renderer::FillTopHalfCircle(float cx, float cy, float r, Color color) {
    if (r <= 0.0f) return;
    // 上半分だけの扇。角度 π（左）から 2π（右）までが上半分です
    //（画面の Y 軸は下向きなので、sin がマイナスの側が上になります）。
    constexpr int kHalfSegments = kCircleSegments / 2;
    Vec2 pts[kHalfSegments + 2];
    int n = 0;
    for (int i = 0; i <= kHalfSegments; ++i) {
        float angle = 3.14159265359f + static_cast<float>(i) / kHalfSegments * 3.14159265359f;
        pts[n].x = cx + std::cos(angle) * r;
        pts[n].y = cy + std::sin(angle) * r;
        n++;
    }
    pts[n++] = {cx + r, cy}; // 底辺を閉じる
    SubmitFan(pts, n, color);
}

void Renderer::FillCapsule(float x1, float y1, float x2, float y2, float thickness, Color color) {
    float r = thickness * 0.5f;
    // 胴体部分（太い線）＋ 両端の丸
    DrawLine(x1, y1, x2, y2, color, thickness);
    FillCircle(x1, y1, r, color);
    FillCircle(x2, y2, r, color);
}

void Renderer::FillGradientRect(float x, float y, float w, float h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    verts_.clear();
    indices_.clear();
    SDL_Color ct = ToSdl(top), cb = ToSdl(bottom);
    // 四隅に頂点を置き、上 2 つと下 2 つで色を変えます。
    // SDL が間の色を自動的に混ぜてくれるので、これだけで
    // なめらかなグラデーションになります。
    auto push = [&](float px, float py, SDL_Color c) {
        SDL_Vertex v{};
        v.position.x = px + shakeX_;
        v.position.y = py + shakeY_;
        v.color = c;
        verts_.push_back(v);
    };
    push(x, y, ct);
    push(x + w, y, ct);
    push(x + w, y + h, cb);
    push(x, y + h, cb);
    indices_ = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(sdl_, nullptr, verts_.data(), 4, indices_.data(), 6);
}

// ---------------------------------------------------------------------
// クリップ（描画範囲の制限）
// ---------------------------------------------------------------------
void Renderer::SetClip(float x, float y, float w, float h) {
    SDL_Rect r;
    r.x = static_cast<int>(std::floor(x));
    r.y = static_cast<int>(std::floor(y));
    r.w = static_cast<int>(std::ceil(w));
    r.h = static_cast<int>(std::ceil(h));
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    SDL_RenderSetClipRect(sdl_, &r);
}

void Renderer::ClearClip() {
    SDL_RenderSetClipRect(sdl_, nullptr);
}

} // namespace kakuge

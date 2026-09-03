// =====================================================================
// core/TrueType.h - TrueType フォントを読んで、点の並びに変換する
// =====================================================================
// パソコンに入っている日本語フォント（メイリオ、MS ゴシックなど）を
// 直接読み込んで、文字を「点を打つか打たないか」の白黒ビットマップに
// 変換します。これで漢字もひらがなも表示できます。
//
// なぜ自前で書くのか
// ---------------
// 普通は SDL_ttf や FreeType といったライブラリを使います。でもこの
// プロジェクトは「使うライブラリは SDL2 だけ」と決めています。
// JSON を自前で書いたのと同じ理由です。外から持ってきたものが
// 増えるほど、ビルドの手順が複雑になり、動かない環境が増えます。
//
// TrueType のフォントファイルは、思ったより素直な作りをしています。
//   1. ファイルの先頭に「表の目次」がある
//   2. 目次から必要な表（cmap / glyf / loca ...）の場所を引く
//   3. cmap で「文字コード → 何番目の字か」を引く
//   4. loca で「何番目の字が、glyf のどこにあるか」を引く
//   5. glyf にはその字の輪郭が、点の並びとして入っている
// ここではその 5 段階だけを実装します。
//
// 対応している範囲（ゲームの文字表示に必要なぶんだけ）
// ------------------------------------------------
//   ○ TrueType 形式の輪郭（glyf 表）。Windows の日本語フォントや
//     IPA フォントはこれです。
//   ○ .ttc（複数のフォントが 1 つのファイルに入っている形式）。
//     先頭のフォントを使います。メイリオや MS ゴシックは .ttc です。
//   ○ cmap の形式 4（基本の文字）と形式 12（絵文字などを含む広い範囲）
//   ○ 複合グリフ（ほかの字を組み合わせて作る字）
//   × CFF 形式の輪郭（.otf に多い）。Noto Sans CJK などはこちら。
//     読めないので、そのときは内蔵のカナ表示に戻ります。
//
// 輪郭から点の並びへ（ラスタライズ）
// ------------------------------
// 輪郭は「直線と 2 次ベジェ曲線」でできています。これを細かい直線に
// 分割してから、横方向に走査して塗ります。1 ピクセルを 4x4 の細かい
// 点で調べ、半分以上が内側なら「点を打つ」と決めます。中間色を
// 使わないので、ドット絵の画面になじむくっきりした字になります。
// =====================================================================
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kakuge {

class TrueTypeFont {
public:
    // 1 文字ぶんの白黒ビットマップ。
    struct Glyph {
        int width = 0;      // ビットマップの横のドット数
        int height = 0;     // 縦のドット数
        int bearingX = 0;   // 文字の左端から、ビットマップ左端までの距離
        int bearingY = 0;   // ベースラインから、ビットマップ上端までの高さ
                            //（上向きが正。ベースラインは文字が並ぶ下の線）
        int advance = 0;    // 次の文字までの送り幅
        std::vector<std::uint8_t> bits; // width*height 個。1 なら点を打つ
        bool Get(int x, int y) const {
            return bits[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] != 0;
        }
    };

    // フォントファイルを読み込む。読めたら true。
    bool LoadFromFile(const std::string& path);
    bool IsLoaded() const { return loaded_; }
    const std::string& Path() const { return path_; }

    // その文字がこのフォントに入っているか。
    bool HasGlyph(std::uint32_t codepoint) const;

    // 文字を pixelSize の大きさで描く。入っていない文字なら false。
    // 同じ文字・同じ大きさは覚えておくので、2 回目以降は速いです。
    const Glyph* GetGlyph(std::uint32_t codepoint, int pixelSize) const;

    // ベースラインより上・下にどれだけ使うか（pixelSize のときの値）。
    int Ascent(int pixelSize) const;
    int Descent(int pixelSize) const;

private:
    // ---- ファイルの中身と、必要な表の場所 ----
    std::vector<std::uint8_t> data_;
    std::string path_;
    bool loaded_ = false;

    std::uint32_t offGlyf_ = 0, offLoca_ = 0, offCmap_ = 0, offHmtx_ = 0;
    std::uint32_t lenLoca_ = 0;
    int unitsPerEm_ = 1000;
    int indexToLocFormat_ = 0; // 0 = 短い形式、1 = 長い形式
    int numGlyphs_ = 0;
    int numHMetrics_ = 0;
    int ascender_ = 0, descender_ = 0;
    std::uint32_t cmapSubtable_ = 0; // 実際に使う cmap の小表の位置
    int cmapFormat_ = 0;

    // 描いた字の置き場（文字コードと大きさをまとめた鍵で引く）
    mutable std::unordered_map<std::uint64_t, Glyph> cache_;

    // ---- ファイルからの読み取り（TrueType はすべてビッグエンディアン）----
    bool InRange(std::uint32_t offset, std::uint32_t size) const {
        return static_cast<std::uint64_t>(offset) + size <= data_.size();
    }
    std::uint8_t U8(std::uint32_t o) const { return InRange(o, 1) ? data_[o] : 0; }
    std::uint16_t U16(std::uint32_t o) const {
        return InRange(o, 2) ? static_cast<std::uint16_t>((data_[o] << 8) | data_[o + 1]) : 0;
    }
    std::int16_t S16(std::uint32_t o) const { return static_cast<std::int16_t>(U16(o)); }
    std::uint32_t U32(std::uint32_t o) const {
        if (!InRange(o, 4)) return 0;
        return (static_cast<std::uint32_t>(data_[o]) << 24) |
               (static_cast<std::uint32_t>(data_[o + 1]) << 16) |
               (static_cast<std::uint32_t>(data_[o + 2]) << 8) |
               static_cast<std::uint32_t>(data_[o + 3]);
    }

    bool ParseTables();
    bool ChooseCmap(std::uint32_t cmapOffset);
    int GlyphIndex(std::uint32_t codepoint) const;
    bool GlyphRange(int glyphIndex, std::uint32_t& start, std::uint32_t& end) const;
    int AdvanceWidth(int glyphIndex) const;

    // 輪郭の 1 点（フォント内部の座標）。
    struct OutlinePoint {
        double x = 0, y = 0;
        bool onCurve = false;
    };
    struct Contour {
        std::vector<OutlinePoint> points;
    };
    // 字の輪郭を取り出す。depth は複合グリフの入れ子の深さ（暴走よけ）。
    bool BuildOutline(int glyphIndex, double offsetX, double offsetY,
                      double scaleX, double scaleY,
                      std::vector<Contour>& out, int depth) const;
    // 輪郭を白黒のビットマップに塗る。
    Glyph Rasterize(const std::vector<Contour>& contours, int pixelSize, int advanceUnits) const;
};

// パソコンに入っている日本語フォントを探して読み込む。
// 探す順番は実装（TrueType.cpp）のコメントに書いてあります。
// 見つからなければ false（呼び出し側は内蔵のカナ表示に戻ります）。
bool LoadSystemJapaneseFont(TrueTypeFont& font, const std::string& preferredPath);

} // namespace kakuge

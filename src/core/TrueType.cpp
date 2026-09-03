// =====================================================================
// core/TrueType.cpp - TrueType.h の中身
// =====================================================================
#include "core/TrueType.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace kakuge {

namespace {

// 4 文字のタグを 1 つの数値にする（'g','l','y','f' → 0x676C7966）。
// 表を探すときの比較を、文字列比較ではなく数値比較で済ませるためです。
constexpr std::uint32_t Tag(const char* s) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[3]));
}

// 1 ピクセルを縦横いくつに分けて調べるか。
// 4x4 = 16 点を調べ、8 点以上が字の内側なら「点を打つ」とします。
// 増やすと輪郭の判定が正確になりますが、小さい字では見た目が
// ほとんど変わらないわりに時間だけ増えます。
constexpr int kSubSamples = 4;

// 2 次ベジェ曲線を何本の直線に分けるか。
// 小さい字なら 6 分割で、目には完全な曲線に見えます。
constexpr int kCurveSegments = 6;

} // namespace

// =====================================================================
// 読み込み
// =====================================================================
bool TrueTypeFont::LoadFromFile(const std::string& path) {
    loaded_ = false;
    cache_.clear();
    data_.clear();

    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) return false;
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    // 極端に大きいファイルは読みません（フォントは普通 数十 MB 以下）。
    if (size <= 0 || size > 64 * 1024 * 1024) { std::fclose(fp); return false; }
    data_.resize(static_cast<size_t>(size));
    size_t got = std::fread(data_.data(), 1, data_.size(), fp);
    std::fclose(fp);
    if (got != data_.size()) { data_.clear(); return false; }

    path_ = path;
    loaded_ = ParseTables();
    if (!loaded_) { data_.clear(); }
    return loaded_;
}

bool TrueTypeFont::ParseTables() {
    // ---- ファイルの先頭 ----
    // .ttc（複数のフォントが 1 つのファイルに入っている形式）なら、
    // 目次のさらに前に「何番目のフォントがどこから始まるか」の表が
    // あります。ここでは先頭のフォントだけを使います。
    std::uint32_t base = 0;
    if (U32(0) == Tag("ttcf")) {
        std::uint32_t numFonts = U32(8);
        if (numFonts == 0) return false;
        base = U32(12);
    }

    std::uint32_t sfnt = U32(base);
    // 0x00010000 が TrueType、'true' は古い Mac のもの。
    // 'OTTO' は輪郭が CFF 形式なので、ここでは読めません。
    if (sfnt != 0x00010000 && sfnt != Tag("true")) return false;

    int numTables = U16(base + 4);
    if (numTables <= 0 || numTables > 512) return false;

    std::uint32_t offHead = 0, offMaxp = 0, offHhea = 0;
    std::uint32_t lenHead = 0;
    for (int i = 0; i < numTables; ++i) {
        std::uint32_t rec = base + 12 + static_cast<std::uint32_t>(i) * 16;
        std::uint32_t tag = U32(rec);
        std::uint32_t off = U32(rec + 8);
        std::uint32_t len = U32(rec + 12);
        if (off >= data_.size()) continue;
        if (tag == Tag("head")) { offHead = off; lenHead = len; }
        else if (tag == Tag("maxp")) { offMaxp = off; }
        else if (tag == Tag("hhea")) { offHhea = off; }
        else if (tag == Tag("hmtx")) { offHmtx_ = off; }
        else if (tag == Tag("loca")) { offLoca_ = off; lenLoca_ = len; }
        else if (tag == Tag("glyf")) { offGlyf_ = off; }
        else if (tag == Tag("cmap")) { offCmap_ = off; }
    }
    // glyf と loca が無いフォント（CFF 形式）はここで諦めます。
    if (offHead == 0 || offMaxp == 0 || offLoca_ == 0 || offGlyf_ == 0 || offCmap_ == 0) {
        return false;
    }
    if (lenHead < 54) return false;

    unitsPerEm_ = U16(offHead + 18);
    if (unitsPerEm_ <= 0) return false;
    indexToLocFormat_ = S16(offHead + 50);
    numGlyphs_ = U16(offMaxp + 4);
    if (numGlyphs_ <= 0) return false;

    if (offHhea != 0) {
        ascender_ = S16(offHhea + 4);
        descender_ = S16(offHhea + 6);
        numHMetrics_ = U16(offHhea + 34);
    }
    if (ascender_ == 0) ascender_ = unitsPerEm_ * 4 / 5; // 情報が無いときの目安
    if (descender_ == 0) descender_ = -unitsPerEm_ / 5;

    return ChooseCmap(offCmap_);
}

// 「文字コード → 何番目の字か」の表を選ぶ。
//
// フォントには用途の違う表がいくつも入っています。ここでは
//   (3,10) Windows の広い範囲（形式 12）… いちばん望ましい
//   (3,1)  Windows の基本範囲（形式 4） … 日本語はこれで足りる
//   (0,*)  Unicode 共通
// の順に探します。日本語の文字は基本範囲に収まっているので、
// (3,1) が見つかれば実用上は十分です。
bool TrueTypeFont::ChooseCmap(std::uint32_t cmapOffset) {
    int numSubtables = U16(cmapOffset + 2);
    std::uint32_t best = 0;
    int bestScore = -1;
    int bestFormat = 0;
    for (int i = 0; i < numSubtables; ++i) {
        std::uint32_t rec = cmapOffset + 4 + static_cast<std::uint32_t>(i) * 8;
        int platform = U16(rec);
        int encoding = U16(rec + 2);
        std::uint32_t sub = cmapOffset + U32(rec + 4);
        int format = U16(sub);
        if (format != 4 && format != 12) continue;

        int score = -1;
        if (platform == 3 && encoding == 10) score = 3;
        else if (platform == 3 && encoding == 1) score = 2;
        else if (platform == 0) score = 1;
        if (score > bestScore) { bestScore = score; best = sub; bestFormat = format; }
    }
    if (bestScore < 0) return false;
    cmapSubtable_ = best;
    cmapFormat_ = bestFormat;
    return true;
}

// =====================================================================
// 文字コード → 字の番号
// =====================================================================
int TrueTypeFont::GlyphIndex(std::uint32_t cp) const {
    if (cmapSubtable_ == 0) return 0;

    if (cmapFormat_ == 12) {
        // 形式 12: 「この範囲の文字コードは、この番号から連番」という
        // 表が並んでいるだけ。二分探索で該当する範囲を探します。
        std::uint32_t nGroups = U32(cmapSubtable_ + 12);
        std::uint32_t lo = 0, hi = nGroups;
        while (lo < hi) {
            std::uint32_t mid = (lo + hi) / 2;
            std::uint32_t g = cmapSubtable_ + 16 + mid * 12;
            std::uint32_t startChar = U32(g);
            std::uint32_t endChar = U32(g + 4);
            if (cp < startChar) hi = mid;
            else if (cp > endChar) lo = mid + 1;
            else return static_cast<int>(U32(g + 8) + (cp - startChar));
        }
        return 0;
    }

    // 形式 4: 基本範囲（U+0000〜U+FFFF）用。
    // 「終わりの文字コード」の配列を探して、その区間の規則で番号を出します。
    if (cp > 0xFFFF) return 0;
    std::uint32_t t = cmapSubtable_;
    std::uint32_t segX2 = U16(t + 6);
    if (segX2 == 0) return 0;
    std::uint32_t segCount = segX2 / 2;
    std::uint32_t endCodes = t + 14;
    std::uint32_t startCodes = endCodes + segX2 + 2; // +2 は予約領域
    std::uint32_t idDeltas = startCodes + segX2;
    std::uint32_t idRangeOffsets = idDeltas + segX2;

    // 該当する区間を探す（終わりの文字コードが cp 以上の最初の区間）
    std::uint32_t lo = 0, hi = segCount;
    while (lo < hi) {
        std::uint32_t mid = (lo + hi) / 2;
        if (U16(endCodes + mid * 2) < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= segCount) return 0;
    std::uint32_t seg = lo;
    std::uint32_t startCode = U16(startCodes + seg * 2);
    if (cp < startCode) return 0;

    std::uint32_t rangeOffset = U16(idRangeOffsets + seg * 2);
    std::int16_t delta = S16(idDeltas + seg * 2);
    if (rangeOffset == 0) {
        return static_cast<std::uint16_t>(cp + static_cast<std::uint16_t>(delta));
    }
    // rangeOffset は「この位置から何バイト先に番号の配列があるか」。
    // 仕様上、その配列は idRangeOffsets の自分の位置を基準に測ります。
    std::uint32_t addr = idRangeOffsets + seg * 2 + rangeOffset + (cp - startCode) * 2;
    std::uint16_t g = U16(addr);
    if (g == 0) return 0;
    return static_cast<std::uint16_t>(g + static_cast<std::uint16_t>(delta));
}

bool TrueTypeFont::HasGlyph(std::uint32_t cp) const {
    return loaded_ && GlyphIndex(cp) != 0;
}

// 字の輪郭データが glyf 表のどこからどこまでかを、loca 表から引く。
bool TrueTypeFont::GlyphRange(int glyphIndex, std::uint32_t& start, std::uint32_t& end) const {
    if (glyphIndex < 0 || glyphIndex >= numGlyphs_) return false;
    if (indexToLocFormat_ == 0) {
        // 短い形式は「実際の値の半分」が入っています（2 で割ってある）。
        std::uint32_t need = static_cast<std::uint32_t>(glyphIndex) * 2 + 4;
        if (need > lenLoca_) return false;
        start = static_cast<std::uint32_t>(U16(offLoca_ + glyphIndex * 2)) * 2;
        end = static_cast<std::uint32_t>(U16(offLoca_ + glyphIndex * 2 + 2)) * 2;
    } else {
        std::uint32_t need = static_cast<std::uint32_t>(glyphIndex) * 4 + 8;
        if (need > lenLoca_) return false;
        start = U32(offLoca_ + glyphIndex * 4);
        end = U32(offLoca_ + glyphIndex * 4 + 4);
    }
    return end >= start;
}

int TrueTypeFont::AdvanceWidth(int glyphIndex) const {
    if (offHmtx_ == 0 || numHMetrics_ <= 0) return unitsPerEm_ / 2;
    int i = std::min(glyphIndex, numHMetrics_ - 1);
    return U16(offHmtx_ + static_cast<std::uint32_t>(i) * 4);
}

// =====================================================================
// 輪郭の取り出し
// =====================================================================
bool TrueTypeFont::BuildOutline(int glyphIndex, double offsetX, double offsetY,
                                double scaleX, double scaleY,
                                std::vector<Contour>& out, int depth) const {
    // 複合グリフが自分自身を参照していると無限に潜るので、深さを制限します。
    if (depth > 5) return false;
    std::uint32_t start = 0, end = 0;
    if (!GlyphRange(glyphIndex, start, end)) return false;
    if (end == start) return true; // 空白など、輪郭を持たない字

    std::uint32_t g = offGlyf_ + start;
    std::int16_t numContours = S16(g);

    // ---- 複合グリフ（ほかの字を組み合わせて作る字）----
    if (numContours < 0) {
        std::uint32_t p = g + 10;
        for (;;) {
            std::uint16_t flags = U16(p);
            std::uint16_t compIndex = U16(p + 2);
            p += 4;
            double dx = 0, dy = 0;
            if (flags & 0x0001) { // 引数が 2 バイト
                dx = S16(p); dy = S16(p + 2); p += 4;
            } else {
                dx = static_cast<std::int8_t>(U8(p));
                dy = static_cast<std::int8_t>(U8(p + 1));
                p += 2;
            }
            if (!(flags & 0x0002)) { dx = 0; dy = 0; } // 位置指定でなければずらさない

            double a = 1.0, d = 1.0;
            if (flags & 0x0008) {            // 縦横同じ倍率
                a = d = S16(p) / 16384.0; p += 2;
            } else if (flags & 0x0040) {     // 縦横別の倍率
                a = S16(p) / 16384.0; d = S16(p + 2) / 16384.0; p += 4;
            } else if (flags & 0x0080) {     // 2x2 の変形（回転など）
                a = S16(p) / 16384.0; d = S16(p + 6) / 16384.0; p += 8;
            }
            BuildOutline(compIndex, offsetX + dx * scaleX, offsetY + dy * scaleY,
                         scaleX * a, scaleY * d, out, depth + 1);
            if (!(flags & 0x0020)) break; // これ以上の部品は無い
        }
        return true;
    }

    // ---- 単純グリフ ----
    if (numContours == 0) return true;
    std::uint32_t p = g + 10;
    std::vector<int> contourEnds(static_cast<size_t>(numContours));
    for (int i = 0; i < numContours; ++i) {
        contourEnds[static_cast<size_t>(i)] = U16(p);
        p += 2;
    }
    int numPoints = contourEnds.back() + 1;
    if (numPoints <= 0 || numPoints > 10000) return false;

    std::uint16_t instrLen = U16(p);
    p += 2 + instrLen; // 命令（ヒント）は使わないので読み飛ばす

    // フラグ（点ごとの情報）。同じフラグが続くときは「繰り返し回数」で
    // 圧縮されているので、そこを展開しながら読みます。
    std::vector<std::uint8_t> flags(static_cast<size_t>(numPoints));
    for (int i = 0; i < numPoints;) {
        std::uint8_t f = U8(p++);
        flags[static_cast<size_t>(i++)] = f;
        if (f & 0x08) { // 次の 1 バイトが繰り返し回数
            int repeat = U8(p++);
            for (int r = 0; r < repeat && i < numPoints; ++r) {
                flags[static_cast<size_t>(i++)] = f;
            }
        }
    }

    // 座標は「前の点からの差分」で入っています。X をすべて読んでから
    // Y をすべて読む、という並びです。
    std::vector<double> xs(static_cast<size_t>(numPoints));
    std::vector<double> ys(static_cast<size_t>(numPoints));
    int v = 0;
    for (int i = 0; i < numPoints; ++i) {
        std::uint8_t f = flags[static_cast<size_t>(i)];
        if (f & 0x02) {                       // 1 バイトの差分
            int d = U8(p++);
            v += (f & 0x10) ? d : -d;
        } else if (!(f & 0x10)) {             // 2 バイトの差分
            v += S16(p); p += 2;
        }                                     // それ以外は前の点と同じ
        xs[static_cast<size_t>(i)] = v;
    }
    v = 0;
    for (int i = 0; i < numPoints; ++i) {
        std::uint8_t f = flags[static_cast<size_t>(i)];
        if (f & 0x04) {
            int d = U8(p++);
            v += (f & 0x20) ? d : -d;
        } else if (!(f & 0x20)) {
            v += S16(p); p += 2;
        }
        ys[static_cast<size_t>(i)] = v;
    }

    int first = 0;
    for (int c = 0; c < numContours; ++c) {
        int last = contourEnds[static_cast<size_t>(c)];
        if (last < first) { first = last + 1; continue; }
        Contour contour;
        for (int i = first; i <= last; ++i) {
            OutlinePoint pt;
            pt.x = offsetX + xs[static_cast<size_t>(i)] * scaleX;
            pt.y = offsetY + ys[static_cast<size_t>(i)] * scaleY;
            pt.onCurve = (flags[static_cast<size_t>(i)] & 0x01) != 0;
            contour.points.push_back(pt);
        }
        out.push_back(std::move(contour));
        first = last + 1;
    }
    return true;
}

// =====================================================================
// 輪郭 → 白黒のビットマップ
// =====================================================================
TrueTypeFont::Glyph TrueTypeFont::Rasterize(const std::vector<Contour>& contours,
                                            int pixelSize, int advanceUnits) const {
    Glyph out;
    double scale = static_cast<double>(pixelSize) / unitsPerEm_;
    out.advance = static_cast<int>(std::lround(advanceUnits * scale));

    // ---- 輪郭を直線の集まりに変える ----
    // TrueType の曲線は 2 次ベジェです。制御点（曲線上にない点）が
    // 2 つ続く場合は、その中間に「曲線上の点」があるものとして扱う、
    // という決まりがあります（データを小さくするための省略）。
    struct Edge { double x0, y0, x1, y1; };
    std::vector<Edge> edges;
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;

    auto addLine = [&](double x0, double y0, double x1, double y1) {
        if (y0 == y1) return; // 水平な線は走査線と交わらないので不要
        edges.push_back({x0, y0, x1, y1});
    };
    auto track = [&](double x, double y) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    };

    for (const auto& contour : contours) {
        const auto& pts = contour.points;
        if (pts.size() < 2) continue;

        // 曲線上の点から始まるように並べ替える（無ければ中間点を作る）。
        std::vector<OutlinePoint> seq;
        size_t startIndex = pts.size();
        for (size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].onCurve) { startIndex = i; break; }
        }
        if (startIndex == pts.size()) {
            // すべて制御点という珍しい形。最初の 2 点の中間から始めます。
            OutlinePoint mid;
            mid.x = (pts[0].x + pts.back().x) / 2.0;
            mid.y = (pts[0].y + pts.back().y) / 2.0;
            mid.onCurve = true;
            seq.push_back(mid);
            for (size_t i = 0; i < pts.size(); ++i) seq.push_back(pts[i]);
        } else {
            for (size_t i = 0; i < pts.size(); ++i) {
                seq.push_back(pts[(startIndex + i) % pts.size()]);
            }
        }
        seq.push_back(seq.front()); // 輪郭を閉じる

        double curX = seq[0].x, curY = seq[0].y;
        track(curX, curY);
        for (size_t i = 1; i < seq.size();) {
            if (seq[i].onCurve) {
                addLine(curX, curY, seq[i].x, seq[i].y);
                curX = seq[i].x; curY = seq[i].y;
                track(curX, curY);
                i++;
                continue;
            }
            // 制御点。次が曲線上の点ならそこまで、制御点なら中間点まで。
            double cx = seq[i].x, cy = seq[i].y;
            double nx, ny;
            if (i + 1 < seq.size() && seq[i + 1].onCurve) {
                nx = seq[i + 1].x; ny = seq[i + 1].y;
                i += 2;
            } else if (i + 1 < seq.size()) {
                nx = (cx + seq[i + 1].x) / 2.0;
                ny = (cy + seq[i + 1].y) / 2.0;
                i += 1;
            } else {
                nx = seq[0].x; ny = seq[0].y;
                i += 1;
            }
            track(cx, cy); track(nx, ny);
            // 2 次ベジェを直線に分割する
            double px = curX, py = curY;
            for (int s = 1; s <= kCurveSegments; ++s) {
                double t = static_cast<double>(s) / kCurveSegments;
                double u = 1.0 - t;
                double qx = u * u * curX + 2 * u * t * cx + t * t * nx;
                double qy = u * u * curY + 2 * u * t * cy + t * t * ny;
                addLine(px, py, qx, qy);
                px = qx; py = qy;
            }
            curX = nx; curY = ny;
        }
    }

    if (edges.empty()) {
        out.width = out.height = 0;
        return out;
    }

    // ---- ビットマップの大きさと位置を決める ----
    int left = static_cast<int>(std::floor(minX * scale));
    int right = static_cast<int>(std::ceil(maxX * scale));
    int bottom = static_cast<int>(std::floor(minY * scale));
    int top = static_cast<int>(std::ceil(maxY * scale));
    int w = right - left;
    int h = top - bottom;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) { out.width = out.height = 0; return out; }

    out.width = w;
    out.height = h;
    out.bearingX = left;
    out.bearingY = top;   // ベースラインからの高さ（上向きが正）
    out.bits.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);

    // ---- 走査線で塗る ----
    // 1 ピクセルを縦横 kSubSamples 個に分け、その細かい点が字の内側に
    // あるかを数えます。内側かどうかは「その点から右に進んだとき、
    // 輪郭を何回またぐか」で決めます（時計回り・反時計回りを
    // ＋1／－1 と数える「非ゼロ規則」。穴のあいた字も正しく塗れます）。
    struct Crossing { double x; int dir; };
    std::vector<Crossing> crossings;
    std::vector<int> coverage(static_cast<size_t>(w), 0);

    for (int py = 0; py < h; ++py) {
        std::fill(coverage.begin(), coverage.end(), 0);
        for (int sy = 0; sy < kSubSamples; ++sy) {
            // このサブ走査線の高さ（フォント内部の座標に戻す）
            double pixelY = static_cast<double>(top) - py - (sy + 0.5) / kSubSamples;
            double fy = pixelY / scale;

            crossings.clear();
            for (const auto& e : edges) {
                double y0 = e.y0, y1 = e.y1;
                int dir = 1;
                double x0 = e.x0, x1 = e.x1;
                if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); dir = -1; }
                if (fy < y0 || fy >= y1) continue;
                double t = (fy - y0) / (y1 - y0);
                crossings.push_back({x0 + (x1 - x0) * t, dir});
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            // 非ゼロ規則で「内側の区間」を作り、その区間を塗ります。
            int winding = 0;
            for (size_t i = 0; i + 1 < crossings.size(); ++i) {
                winding += crossings[i].dir;
                if (winding == 0) continue;
                double spanL = crossings[i].x * scale - left;
                double spanR = crossings[i + 1].x * scale - left;
                if (spanR <= 0 || spanL >= w) continue;
                // 区間に入っているサブ点を数える
                int firstPx = std::max(0, static_cast<int>(std::floor(spanL)));
                int lastPx = std::min(w - 1, static_cast<int>(std::ceil(spanR)));
                for (int px = firstPx; px <= lastPx; ++px) {
                    for (int sx = 0; sx < kSubSamples; ++sx) {
                        double sampleX = px + (sx + 0.5) / kSubSamples;
                        if (sampleX >= spanL && sampleX < spanR) {
                            coverage[static_cast<size_t>(px)]++;
                        }
                    }
                }
            }
        }
        // 半分以上が内側なら点を打つ。中間色を作らないので、
        // 拡大してもにじまないドット絵の字になります。
        const int half = kSubSamples * kSubSamples / 2;
        for (int px = 0; px < w; ++px) {
            if (coverage[static_cast<size_t>(px)] >= half) {
                out.bits[static_cast<size_t>(py) * static_cast<size_t>(w) +
                         static_cast<size_t>(px)] = 1;
            }
        }
    }
    return out;
}

const TrueTypeFont::Glyph* TrueTypeFont::GetGlyph(std::uint32_t cp, int pixelSize) const {
    if (!loaded_ || pixelSize <= 0 || pixelSize > 256) return nullptr;
    std::uint64_t key = (static_cast<std::uint64_t>(pixelSize) << 32) | cp;
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;

    int gi = GlyphIndex(cp);
    if (gi == 0) return nullptr; // このフォントに無い文字

    std::vector<Contour> contours;
    if (!BuildOutline(gi, 0.0, 0.0, 1.0, 1.0, contours, 0)) return nullptr;
    Glyph g = Rasterize(contours, pixelSize, AdvanceWidth(gi));
    auto inserted = cache_.emplace(key, std::move(g));
    return &inserted.first->second;
}

int TrueTypeFont::Ascent(int pixelSize) const {
    return static_cast<int>(std::lround(ascender_ * static_cast<double>(pixelSize) / unitsPerEm_));
}
int TrueTypeFont::Descent(int pixelSize) const {
    return static_cast<int>(std::lround(descender_ * static_cast<double>(pixelSize) / unitsPerEm_));
}

// =====================================================================
// パソコンに入っている日本語フォントを探す
// =====================================================================
bool LoadSystemJapaneseFont(TrueTypeFont& font, const std::string& preferredPath) {
    namespace fs = std::filesystem;

    // 「日本語が本当に出せるか」の確認用。
    // ファイルが読めただけでは足りません。英字だけのフォントでも
    // 読み込みには成功してしまうので、実際に「あ」と「水」が
    // 入っているかを見ます。
    auto usable = [](TrueTypeFont& f) {
        return f.IsLoaded() && f.HasGlyph(0x3042) /* あ */ && f.HasGlyph(0x6C34) /* 水 */;
    };

    // 1) ユーザーが置いたフォントを最優先。
    //    data/font.ttf を置けば、好きなフォントに差し替えられます。
    if (!preferredPath.empty() && fs::exists(preferredPath)) {
        if (font.LoadFromFile(preferredPath) && usable(font)) return true;
    }

    // 2) OS が持っている日本語フォント。
    //    ゴシック体を先に探します。明朝体は線が細く、10 ピクセル台では
    //    かすれて読めなくなるためです。
    static const char* kCandidates[] = {
        // ---- Windows ----
        "C:/Windows/Fonts/meiryo.ttc",     // メイリオ（Vista 以降）
        "C:/Windows/Fonts/YuGothM.ttc",    // 游ゴシック Medium（Win8.1 以降）
        "C:/Windows/Fonts/YuGothR.ttc",    // 游ゴシック Regular
        "C:/Windows/Fonts/msgothic.ttc",   // MS ゴシック（どの版にもある）
        "C:/Windows/Fonts/msmincho.ttc",   // MS 明朝（最後の手段）
        // ---- macOS ----
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        // ---- Linux ----
        "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
        "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
        "/etc/alternatives/fonts-japanese-gothic.ttf",
        "/usr/share/fonts/truetype/vlgothic/VL-Gothic-Regular.ttf",
        "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf",
    };
    for (const char* candidate : kCandidates) {
        std::error_code ec;
        if (!fs::exists(candidate, ec)) continue;
        if (font.LoadFromFile(candidate) && usable(font)) return true;
    }
    return false;
}

} // namespace kakuge

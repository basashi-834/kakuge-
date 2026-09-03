// =====================================================================
// engine/Boxes.h - 当たり判定に使う「四角形」
// =====================================================================
// 格闘ゲームの当たり判定は、実は絵の形とは関係ありません。
// 目に見えない四角形どうしが重なったかどうかで決まります。
// 使う四角形は 3 種類あります。
//
//   ヒットボックス (Hitbox)   … 攻撃が当たる範囲。技を出している間だけ現れる。
//                               → MoveData.h / MoveExecutor.h が持つ
//   ハートボックス (Hurtbox)  … 自分の「食らう」範囲。体そのもの。
//                               → このファイルの HurtboxSet が持つ
//   プッシュボックス (Pushbox) … 体と体が重ならないよう押し合う範囲。
//                               攻撃には一切関係しない。
//                               → Fighter.h が持つ
//
// 判定のルールはただ 1 つ:
//   「攻撃側のヒットボックス」と「防御側のハートボックス」が重なったらヒット。
// ヒットボックスどうしや、プッシュボックスとは絶対に判定しません。
//
// このファイルは C++ の標準機能だけで書かれていて、SDL2 にも
// ゲーム画面にも依存しません。だから画面を出さずにテスト
// （tests/EngineTests.cpp）できます。
// =====================================================================
#pragma once
#include <cmath>
#include <string>
#include <vector>

namespace kakuge {

// ---------------------------------------------------------------------
// RectBox - 中心と大きさで表した四角形
// ---------------------------------------------------------------------
// 「左上の座標＋幅高さ」ではなく「中心の座標＋幅高さ」で持っています。
// キャラクターを左右反転させるとき、中心基準のほうが計算が単純
// （X 座標に -1 を掛けるだけ）だからです。
//
// Y 軸は下向きがプラスです（画面と同じ向き）。キャラクターの原点は
// 「足元」なので、頭は原点より上、つまり Y がマイナスの位置にあります。
struct RectBox {
    double CenterX = 0.0;
    double CenterY = 0.0;
    double Width = 0.0;
    double Height = 0.0;

    RectBox() = default;
    RectBox(double cx, double cy, double w, double h)
        : CenterX(cx), CenterY(cy), Width(w), Height(h) {}

    double Left() const { return CenterX - Width / 2.0; }
    double Right() const { return CenterX + Width / 2.0; }
    double Top() const { return CenterY - Height / 2.0; }
    double Bottom() const { return CenterY + Height / 2.0; }

    // 2 つの四角形が重なっているか。
    // 「重なっていない条件」を 4 つ調べて、どれにも当てはまらなければ
    // 重なっている、という書き方をしています（そのほうが単純で速い）。
    // 辺がぴったり接しているだけ（Right == other.Left）は重なりとしません。
    bool Intersects(const RectBox& other) const {
        if (Right() <= other.Left()) return false;   // 自分が完全に左
        if (Left() >= other.Right()) return false;   // 自分が完全に右
        if (Bottom() <= other.Top()) return false;   // 自分が完全に上
        if (Top() >= other.Bottom()) return false;   // 自分が完全に下
        return true;
    }
};

// ---------------------------------------------------------------------
// HurtboxPart - ハートボックスの「部位」1 つぶん
// ---------------------------------------------------------------------
// 体全体を 1 個の四角形にしてしまうと、しゃがみパンチが相手の頭に
// 当たってしまうような不自然さが出ます。そこで頭・胴・脚のように
// 部位ごとの四角形に分け、そのどれか 1 つにでも当たればヒット、
// という作りにしています。
// Name は "head" / "torso" / "leg" のような自由な名前で、
// 判定そのものには影響しません（データを整理するための目印）。
struct HurtboxPart {
    std::string Name = "body";
    RectBox Box;
};

// ---------------------------------------------------------------------
// HurtboxSet - 立ち / しゃがみ / 空中 それぞれのハートボックス一式
// ---------------------------------------------------------------------
// 座標は「キャラクター中心からの相対位置」です。
//   X = 0 が体の中心（プラスが向いている方向。左向きなら呼び出し側で反転）
//   Y = 0 が足元、マイナスが上（頭のほう）
//
// 下の既定値は、標準体型のキャラクターの寸法です。
// 立ち姿の外形は 幅 30 x 高さ 88。見た目の幅（約 55）よりわざと細く
// してあります。道着の袖や帯、構えた拳まで「体」として当たってしまうと
// 理不尽に感じられるからです。
// キャラクターごとの上書きは data/characters/*.json の "hurtboxes" で
// 指定できます。
struct HurtboxSet {
    // 立ち: 頭（16 高）・胴（30 高）・脚（42 高）で合計 88。
    std::vector<HurtboxPart> Stand{
        {"head",  RectBox{0, -80, 18, 16}},
        {"torso", RectBox{0, -57, 30, 30}},
        {"leg",   RectBox{0, -21, 28, 42}},
    };
    // しゃがみ: 全体で 57 の高さ。立ちより低いので、
    // 上を通る攻撃（中段）をかわせます。
    std::vector<HurtboxPart> Crouch{
        {"head",  RectBox{0, -50, 18, 14}},
        {"torso", RectBox{0, -36, 30, 24}},
        {"leg",   RectBox{0, -12, 34, 24}},
    };
    // 空中: 脚を抱え込む姿勢なので、立ちより一回り小さめ（72 の高さ）。
    std::vector<HurtboxPart> Air{
        {"head",  RectBox{0, -64, 18, 16}},
        {"torso", RectBox{0, -42, 28, 28}},
        {"leg",   RectBox{0, -14, 24, 28}},
    };

    // 姿勢を表す文字列から、対応する部位一覧を取り出す。
    // （const 版と非 const 版の 2 つあるのは C++ の作法です。
    //   読むだけなら上、書き換えるなら下が呼ばれます）
    std::vector<HurtboxPart>& PartsForStance(const std::string& stance) {
        if (stance == "crouch") return Crouch;
        if (stance == "air") return Air;
        return Stand;
    }
    const std::vector<HurtboxPart>& PartsForStance(const std::string& stance) const {
        if (stance == "crouch") return Crouch;
        if (stance == "air") return Air;
        return Stand;
    }

    // 相対座標の部位一覧を、実際のワールド座標の四角形に変換する。
    //   facing が -1（左向き）なら X を左右反転
    //   原点は std::round で整数に丸める
    //
    // なぜ丸めるのか: キャラクターの移動は小数（例: X=12.37）で計算して
    // いますが、当たり判定を小数のまま行うと、見た目のドット絵の位置
    // （整数）と 1 ピクセルずれることがあります。描画側も同じように
    // 丸めているので、両方を丸めておけばデバッグ表示の枠と実際の判定が
    // 必ず一致します。
    static std::vector<RectBox> PlaceParts(const std::vector<HurtboxPart>& parts, int facing,
                                           double originX, double originY) {
        std::vector<RectBox> out;
        double ox = std::round(originX), oy = std::round(originY);
        int f = facing < 0 ? -1 : 1;
        for (const auto& part : parts) {
            out.emplace_back(ox + part.Box.CenterX * f, oy + part.Box.CenterY,
                             part.Box.Width, part.Box.Height);
        }
        return out;
    }

    // 現在の姿勢のハートボックスを、ワールド座標で全部返す。
    std::vector<RectBox> RectsForStance(const std::string& stance, int facing,
                                        double originX, double originY) const {
        return PlaceParts(PartsForStance(stance), facing, originX, originY);
    }
};

} // namespace kakuge

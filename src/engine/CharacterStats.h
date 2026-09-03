// =====================================================================
// engine/CharacterStats.h - キャラクターの基本性能（変わらないデータ）
// =====================================================================
// 「リュウは体力 1000、前進速度 220」といった、そのキャラクターが
// 生まれつき持っている数値です。data/characters/<id>.json から読み込みます。
//
// 「今の体力」「今の座標」のような、試合中に変わる値はここには入れません。
// それらは Fighter.h（実際に動いているキャラクター）が持ちます。
// 設計図（CharacterStats）と、その設計図から作られた実体（Fighter）を
// 分けておくと、ラウンドをやり直すとき「設計図から作り直す」だけで
// 済むので簡単です。
// =====================================================================
#pragma once
#include <string>
#include <vector>

#include "core/Json.h"
#include "engine/Boxes.h"

namespace kakuge {

class CharacterStats {
public:
    std::string Id;             // ファイル名にもなる識別子（例: "ryu"）
    std::string Name = "Fighter"; // 画面に出す名前
    int MaxHP = 1000;           // 最大体力

    // 移動速度は「1 秒あたり何ワールド単位進むか」です。
    // 1 フレーム（1/60 秒）あたりでは 220/60 ≒ 3.7 単位進みます。
    double WalkForwardSpeed = 220.0;  // 前進の速さ
    double WalkBackwardSpeed = 170.0; // 後退の速さ（前進より遅いのが定番）
    double DashSpeed = 420.0;         // 前ダッシュの速さ

    // ジャンプの初速度。上方向がマイナスなので負の値です。
    // 重力 Gravity で毎フレーム下向きに加速され、やがて落ちてきます。
    // 滞空時間 ≒ 2 * 900 / 2400 = 0.75 秒（約 45 フレーム）。
    double JumpVelocity = -900.0;
    double Gravity = 2400.0;

    // キャラクターの色（0-255）。描画時の道着の色になります。
    int ColorR = 200, ColorG = 50, ColorB = 45;

    // このキャラクターが使える技の ID 一覧。
    std::vector<std::string> MoveIds;

    // 姿勢ごとの食らい判定。JSON に書かれていなければ
    // HurtboxSet の既定値（標準体型）がそのまま使われます。
    HurtboxSet Hurtboxes;

    // -----------------------------------------------------------------
    // JSON からの読み込み
    // -----------------------------------------------------------------
    // 書かれていない項目は既定値のままにします。つまり JSON に
    // {"id": "ryu"} とだけ書いても、体力 1000 の普通のキャラクターとして
    // ちゃんと動きます。データが不完全でもゲームが落ちないようにする、
    // という方針です。
    static CharacterStats FromJson(const Json& obj) {
        CharacterStats s;
        s.Id = obj.GetString("id", std::string());
        s.Name = obj.GetString("name", s.Id);
        s.MaxHP = obj.GetInt("maxHP", 1000);
        s.WalkForwardSpeed = obj.GetNumber("walkForwardSpeed", 220.0);
        s.WalkBackwardSpeed = obj.GetNumber("walkBackwardSpeed", 170.0);
        s.DashSpeed = obj.GetNumber("dashSpeed", 420.0);
        s.JumpVelocity = obj.GetNumber("jumpVelocity", -900.0);
        s.Gravity = obj.GetNumber("gravity", 2400.0);

        // 色は [0.78, 0.2, 0.18] のように 0.0-1.0 の 3 要素の配列で
        // 書かれているので、255 倍して 0-255 に直します。
        if (const Json* color = obj.Find("color"); color && color->Size() >= 3) {
            s.ColorR = static_cast<int>(color->At(0).AsNumber() * 255);
            s.ColorG = static_cast<int>(color->At(1).AsNumber() * 255);
            s.ColorB = static_cast<int>(color->At(2).AsNumber() * 255);
        }

        if (const Json* moves = obj.Find("moves"); moves && moves->IsArray()) {
            for (const Json& mid : moves->Items()) s.MoveIds.push_back(mid.AsString());
        }

        if (const Json* hb = obj.Find("hurtboxes"); hb && hb->IsObject()) {
            // 1 つの姿勢のデータは 2 通りの書き方を許しています。
            //   新しい形式: 部位の配列
            //     [{"part":"head","offsetX":0,"offsetY":-80,"width":18,"height":16}, ...]
            //   古い形式: 体全体を表す 1 個のオブジェクト
            //     {"offsetX":0,"offsetY":-44,"width":30,"height":88}
            // 古い形式は "body" という名前の部位 1 個として読み込むので、
            // 昔作ったデータファイルもそのまま動きます。
            auto loadStance = [](const Json& j, std::vector<HurtboxPart>& parts) {
                if (j.IsArray()) {
                    parts.clear();
                    for (const Json& pj : j.Items()) {
                        HurtboxPart part;
                        part.Name = pj.GetString("part", "body");
                        part.Box.CenterX = pj.GetNumber("offsetX", 0.0);
                        part.Box.CenterY = pj.GetNumber("offsetY", 0.0);
                        part.Box.Width = pj.GetNumber("width", 40.0);
                        part.Box.Height = pj.GetNumber("height", 40.0);
                        parts.push_back(part);
                    }
                } else if (j.IsObject()) {
                    HurtboxPart part;
                    part.Name = "body";
                    part.Box.CenterX = j.GetNumber("offsetX", 0.0);
                    part.Box.CenterY = j.GetNumber("offsetY", 0.0);
                    part.Box.Width = j.GetNumber("width", 40.0);
                    part.Box.Height = j.GetNumber("height", 40.0);
                    parts = {part};
                }
            };
            if (const Json* v = hb->Find("stand")) loadStance(*v, s.Hurtboxes.Stand);
            if (const Json* v = hb->Find("crouch")) loadStance(*v, s.Hurtboxes.Crouch);
            if (const Json* v = hb->Find("air")) loadStance(*v, s.Hurtboxes.Air);
        }
        return s;
    }

    // -----------------------------------------------------------------
    // JSON への書き出し（キャラクターを保存するとき）
    // -----------------------------------------------------------------
    Json ToJson() const {
        Json j = Json::MakeObject();
        j.Set("id", Json(Id));
        j.Set("name", Json(Name));
        j.Set("maxHP", Json(MaxHP));
        j.Set("walkForwardSpeed", Json(WalkForwardSpeed));
        j.Set("walkBackwardSpeed", Json(WalkBackwardSpeed));
        j.Set("dashSpeed", Json(DashSpeed));
        j.Set("jumpVelocity", Json(JumpVelocity));
        j.Set("gravity", Json(Gravity));

        Json color = Json::MakeArray();
        color.Push(Json(ColorR / 255.0));
        color.Push(Json(ColorG / 255.0));
        color.Push(Json(ColorB / 255.0));
        j.Set("color", std::move(color));

        auto saveStance = [](const std::vector<HurtboxPart>& parts) {
            Json arr = Json::MakeArray();
            for (const auto& part : parts) {
                Json p = Json::MakeObject();
                p.Set("part", Json(part.Name));
                p.Set("offsetX", Json(part.Box.CenterX));
                p.Set("offsetY", Json(part.Box.CenterY));
                p.Set("width", Json(part.Box.Width));
                p.Set("height", Json(part.Box.Height));
                arr.Push(std::move(p));
            }
            return arr;
        };
        Json hb = Json::MakeObject();
        hb.Set("stand", saveStance(Hurtboxes.Stand));
        hb.Set("crouch", saveStance(Hurtboxes.Crouch));
        hb.Set("air", saveStance(Hurtboxes.Air));
        j.Set("hurtboxes", std::move(hb));

        Json moves = Json::MakeArray();
        for (const auto& id : MoveIds) moves.Push(Json(id));
        j.Set("moves", std::move(moves));
        return j;
    }
};

} // namespace kakuge

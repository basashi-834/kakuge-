// =====================================================================
// engine/MoveData.h - 1 つの技のデータ（フレームデータ）
// =====================================================================
// 「立ち強パンチは 発生 8F、持続 3F、硬直 18F、ダメージ 120」といった、
// 技の性能をすべて表すデータです。data/moves/<キャラ>/<技>.json から
// 読み込みます。プログラムを書き換えずに JSON を編集するだけで
// 技のバランスを調整できる、というのがこの作りの狙いです。
//
// 格闘ゲームのフレームデータの基礎
// -------------------------------
// 技はボタンを押してから終わるまで、必ず 3 つの段階を通ります。
//
//   発生 (Startup)   … 拳を引いて構えている間。まだ当たらない。
//   持続 (Active)    … 攻撃判定が出ている間。ここで当たる。
//   硬直 (Recovery)  … 拳を戻している間。もう当たらないし、動けない。
//
//   例: 発生 8F・持続 3F・硬直 18F の技は、全部で 29 フレーム（約 0.48 秒）。
//       ボタンを押してから 8/60 秒後に攻撃判定が出て、3/60 秒だけ続き、
//       そのあと 18/60 秒はまったく動けません。
//
// 「発生が速い技は弱く、遅い技は強い」というバランスの根っこがこれです。
// =====================================================================
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "core/Json.h"
#include "engine/Boxes.h"
#include "engine/Constants.h"

namespace kakuge {

// ---------------------------------------------------------------------
// HitboxDef - 攻撃判定の四角形 1 個ぶん（技データの中での書き方）
// ---------------------------------------------------------------------
// offsetX / offsetY は「キャラクターの原点（中心X・足元Y）から見た、
// 四角形の中心の位置」です。向いている方向がプラスなので、
// 左向きのときはエンジンが自動で左右反転します
//（MoveExecutor::GetActiveHitboxRects）。だからデータは 1 通りで済みます。
struct HitboxDef {
    double offsetX = 0, offsetY = 0, width = 40, height = 40;
};

// ---------------------------------------------------------------------
// FrameBoxSet - 特定のフレームだけ判定を差し替える指定
// ---------------------------------------------------------------------
// 上級者向けの機能です。通常はハートボックスは姿勢で決まり、
// ヒットボックスは持続フレーム中に出ますが、
//   「昇龍拳の 4-6 フレーム目だけ、腕側の食らい判定を小さくしたい」
//   「同じ技でも 10 フレーム目からは攻撃判定の位置を変えたい」
// といった、フレーム単位の細かい調整をしたいことがあります。
// そのときにこの指定を使います（JSON のキーは "frameBoxes"）。
//
//   [{"startFrame":4, "endFrame":6,
//     "hurtboxes":[{"part":"head","offsetX":0,"offsetY":-80,"width":18,"height":16}],
//     "pushbox"  :{"offsetX":0,"offsetY":-36,"width":30,"height":72},
//     "hitboxes" :[{"offsetX":26,"offsetY":-61,"width":16,"height":10}]}]
//
// 3 種類の判定は独立して指定でき、書かなかったものは通常どおりです。
struct FrameBoxSet {
    int startFrame = 0, endFrame = 0; // 何フレーム目から何フレーム目まで（両端を含む）
    bool hasHurtboxes = false;
    std::vector<HurtboxPart> hurtboxes;
    bool hasPushbox = false;
    RectBox pushbox;
    bool hasHitboxes = false;
    std::vector<HitboxDef> hitboxes;

    bool Covers(int frame) const { return frame >= startFrame && frame <= endFrame; }
};

// 無敵時間の指定。
//   type … 何に対して無敵か（Constants::Invincible* を参照）
//   start_frame / end_frame … 何フレーム目から何フレーム目まで
struct Invincibility {
    std::string type = "None";
    int start_frame = 0;
    int end_frame = 0;
};

// 飛び道具（波動拳など）の設定。
struct ProjectileDef {
    bool present = false;        // この技は飛び道具を出すか
    double speed = 500.0;        // 進む速さ（1 秒あたり）
    int lifetime = 90;           // 何フレームで消えるか
    double width = 30.0;
    double height = 30.0;
    double spawnOffsetX = 40.0;  // どこから出るか（キャラ原点からの相対位置）
    double spawnOffsetY = -40.0;
};

class MoveData {
public:
    std::string Id;    // 技の識別子（ファイル名と同じ。例 "fireball"）
    std::string Name;  // 画面に出す名前（例 "波動拳"）

    // ---- フレームデータ ----
    int Startup = 1;    // 発生
    int Active = 1;     // 持続
    int Recovery = 1;   // 硬直
    int TotalFrame = 0; // 全体（0 なら上の 3 つの合計を使う）

    // ---- 当たったときの効果 ----
    int Damage = 0;     // ダメージ
    int Hitstun = 0;    // 当てたとき相手が動けないフレーム数
    int Blockstun = 0;  // ガードされたとき相手が動けないフレーム数
    int Hitstop = 0;    // ヒット時に両者の時間が止まるフレーム数（打撃感の演出）
    std::string GuardType = "High";  // どうガードできるか
    double ChipDamagePercent = 0.0;  // ガードされたときの削りダメージの割合

    // ---- 判定 ----
    std::vector<HitboxDef> Hitboxes;    // 持続フレーム中に出る攻撃判定（複数可）
    std::vector<FrameBoxSet> FrameBoxes;// フレーム単位の上書き指定

    // 投げ（GuardType が "Throw"）は攻撃判定ではなく、
    // 中心どうしの距離で成立します。その距離。
    double ThrowRange = GameSpec::NormalThrowRange;

    // ---- のけぞり・吹き飛び ----
    double KnockbackX = 0.0, KnockbackY = 0.0;
    std::string HitOutcome = "Normal";  // ヒット後の状態（のけぞり / ダウン等）

    // ---- ゲージ ----
    int MeterGain = 0;  // この技で溜まるゲージ量
    int MeterCost = 0;  // この技を出すのに必要なゲージ量（超必殺技用）

    // ---- キャンセル（コンボ）----
    // 技の途中から次の技に移れる時間帯。ここが開いている間だけ、
    // 硬直を飛ばして次の技を出せます。これがコンボの仕組みです。
    // CancelRoutes は昔の「この技からこの技へ」という許可リストの
    // 名残で、今は時間帯（下の 2 つ）だけで判定しています。
    std::vector<std::string> CancelRoutes;
    int CancelStartFrame = 0, CancelEndFrame = 0;

    std::vector<std::string> Tags;  // 技の性質ラベル（Constants::Tag* 参照）
    Invincibility Inv;              // 無敵時間

    // ---- 出し方 ----
    std::string InputCommand;   // レバーコマンド（例 "236"）。空なら通常技
    std::string Button;         // 使うボタン（"HP" や "AnyP"）
    std::string Stance = "stand"; // どの姿勢から出せるか（stand/crouch/air）
    bool RequiresAir = false;

    ProjectileDef Projectile;   // 飛び道具の設定
    double EffectiveRange = 0.0;// CPU が「この距離なら当たる」と判断する目安

    // 予約項目（まだゲームには影響しません）。技の最中に判定そのものが
    // 独自の軌道で動くタイプの技を将来入れるための場所です。
    bool HasDynamicHitbox = false;
    // 技のモーション参考画像のパス（資料用。描画には使っていません）。
    std::string MotionImagePath;

    // -----------------------------------------------------------------
    // 便利関数
    // -----------------------------------------------------------------
    bool HasTag(const std::string& tag) const {
        return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
    }
    bool CanCancelInto(const std::string& moveId) const {
        return std::find(CancelRoutes.begin(), CancelRoutes.end(), moveId) != CancelRoutes.end();
    }
    // 今のフレームがキャンセル可能時間帯の中か。
    // どちらも 0 のままなら「キャンセル不可の技」という意味です。
    bool IsCancelWindowOpen(int frame) const {
        if (CancelStartFrame <= 0 && CancelEndFrame <= 0) return false;
        return frame >= CancelStartFrame && frame <= CancelEndFrame;
    }

    // このフレームに対する上書き指定があれば返す（無ければ nullptr）。
    const FrameBoxSet* FrameBoxesAt(int frame) const {
        for (const auto& fb : FrameBoxes) {
            if (fb.Covers(frame)) return &fb;
        }
        return nullptr;
    }

    // 有利フレーム / 不利フレーム。
    // 「+2」なら当てたあと自分が 2 フレーム早く動ける（有利）、
    // 「-5」なら 5 フレーム遅い（不利＝反撃される）という意味です。
    int OnHitAdvantage() const { return Hitstun - Recovery; }
    int OnBlockAdvantage() const { return Blockstun - Recovery; }

    // -----------------------------------------------------------------
    // JSON からの読み込み
    // -----------------------------------------------------------------
    static MoveData FromJson(const Json& obj) {
        MoveData m;
        m.Id = obj.GetString("id", std::string());
        m.Name = obj.GetString("name", m.Id);
        m.Startup = obj.GetInt("startup", 1);
        m.Active = obj.GetInt("active", 1);
        m.Recovery = obj.GetInt("recovery", 1);
        int tf = obj.GetInt("totalFrame", 0);
        m.TotalFrame = tf > 0 ? tf : (m.Startup + m.Active + m.Recovery);
        m.Damage = obj.GetInt("damage", 0);
        m.Hitstun = obj.GetInt("hitstun", 0);
        m.Blockstun = obj.GetInt("blockstun", 0);
        m.Hitstop = obj.GetInt("hitstop", 0);
        m.GuardType = obj.GetString("guardType", "High");
        m.ChipDamagePercent = obj.GetNumber("chipDamagePercent", 0.0);

        auto readHitbox = [](const Json& hb) {
            HitboxDef d;
            d.offsetX = hb.GetNumber("offsetX", 0.0);
            d.offsetY = hb.GetNumber("offsetY", 0.0);
            d.width = hb.GetNumber("width", 40.0);
            d.height = hb.GetNumber("height", 40.0);
            return d;
        };
        if (const Json* hitboxes = obj.Find("hitbox"); hitboxes && hitboxes->IsArray()) {
            for (const Json& hb : hitboxes->Items()) m.Hitboxes.push_back(readHitbox(hb));
        }

        if (const Json* frameBoxes = obj.Find("frameBoxes"); frameBoxes && frameBoxes->IsArray()) {
            for (const Json& fj : frameBoxes->Items()) {
                FrameBoxSet fb;
                fb.startFrame = fj.GetInt("startFrame", 0);
                fb.endFrame = fj.GetInt("endFrame", fb.startFrame);
                if (const Json* hurt = fj.Find("hurtboxes"); hurt && hurt->IsArray()) {
                    fb.hasHurtboxes = true;
                    for (const Json& pj : hurt->Items()) {
                        HurtboxPart part;
                        part.Name = pj.GetString("part", "body");
                        part.Box = RectBox(pj.GetNumber("offsetX", 0.0), pj.GetNumber("offsetY", 0.0),
                                           pj.GetNumber("width", 40.0), pj.GetNumber("height", 40.0));
                        fb.hurtboxes.push_back(part);
                    }
                }
                if (const Json* pb = fj.Find("pushbox"); pb && pb->IsObject()) {
                    fb.hasPushbox = true;
                    fb.pushbox = RectBox(pb->GetNumber("offsetX", 0.0), pb->GetNumber("offsetY", 0.0),
                                         pb->GetNumber("width", 30.0), pb->GetNumber("height", 72.0));
                }
                if (const Json* hits = fj.Find("hitboxes"); hits && hits->IsArray()) {
                    fb.hasHitboxes = true;
                    for (const Json& hb : hits->Items()) fb.hitboxes.push_back(readHitbox(hb));
                }
                m.FrameBoxes.push_back(fb);
            }
        }

        m.ThrowRange = obj.GetNumber("throwRange", static_cast<double>(GameSpec::NormalThrowRange));
        m.KnockbackX = obj.GetNumber("knockbackX", 0.0);
        m.KnockbackY = obj.GetNumber("knockbackY", 0.0);
        m.HitOutcome = obj.GetString("hitOutcome", "Normal");
        m.MeterGain = obj.GetInt("meterGain", 0);
        m.MeterCost = obj.GetInt("meterCost", 0);

        if (const Json* routes = obj.Find("cancelRoutes"); routes && routes->IsArray()) {
            for (const Json& r : routes->Items()) m.CancelRoutes.push_back(r.AsString());
        }
        m.CancelStartFrame = obj.GetInt("cancelStartFrame", 0);
        m.CancelEndFrame = obj.GetInt("cancelEndFrame", 0);

        if (const Json* tags = obj.Find("tags"); tags && tags->IsArray()) {
            for (const Json& t : tags->Items()) m.Tags.push_back(t.AsString());
        }

        if (const Json* iv = obj.Find("invincibility"); iv && iv->IsObject()) {
            m.Inv.type = iv->GetString("type", "None");
            m.Inv.start_frame = iv->GetInt("start_frame", 0);
            m.Inv.end_frame = iv->GetInt("end_frame", 0);
        }

        m.InputCommand = obj.GetString("input", std::string());
        m.Button = obj.GetString("button", std::string());
        m.Stance = obj.GetString("stance", "stand");
        m.RequiresAir = obj.GetBool("requiresAir", false);

        if (const Json* p = obj.Find("projectile"); p && p->IsObject() && !p->Members().empty()) {
            m.Projectile.present = true;
            m.Projectile.speed = p->GetNumber("speed", 500.0);
            m.Projectile.lifetime = p->GetInt("lifetime", 90);
            m.Projectile.width = p->GetNumber("width", 30.0);
            m.Projectile.height = p->GetNumber("height", 30.0);
            m.Projectile.spawnOffsetX = p->GetNumber("spawnOffsetX", 40.0);
            m.Projectile.spawnOffsetY = p->GetNumber("spawnOffsetY", -40.0);
        }

        m.HasDynamicHitbox = obj.GetBool("hasDynamicHitbox", false);
        m.MotionImagePath = obj.GetString("motionImage", std::string());

        // CPU 用の間合い。JSON に書いていなければ、技の性質から推定します。
        // 弱攻撃は中心から約 34、強攻撃は約 48 まで届くので、その辺りの値。
        m.EffectiveRange = obj.GetNumber("effectiveRange", 0.0);
        if (m.EffectiveRange <= 0.0) {
            if (m.HasTag(Constants::TagProjectile)) m.EffectiveRange = 900.0; // 飛び道具は画面端まで
            else if (m.HasTag(Constants::TagThrow)) m.EffectiveRange = m.ThrowRange;
            else if (m.HasTag(Constants::TagHeavy)) m.EffectiveRange = 50.0;
            else if (m.HasTag(Constants::TagMedium)) m.EffectiveRange = 42.0;
            else m.EffectiveRange = 36.0;
        }
        return m;
    }

    // -----------------------------------------------------------------
    // JSON への書き出し
    // -----------------------------------------------------------------
    Json ToJson() const {
        Json j = Json::MakeObject();
        j.Set("id", Json(Id));
        j.Set("name", Json(Name));
        j.Set("startup", Json(Startup));
        j.Set("active", Json(Active));
        j.Set("recovery", Json(Recovery));
        j.Set("totalFrame", Json(TotalFrame));
        j.Set("damage", Json(Damage));
        j.Set("hitstun", Json(Hitstun));
        j.Set("blockstun", Json(Blockstun));
        j.Set("hitstop", Json(Hitstop));
        j.Set("guardType", Json(GuardType));
        j.Set("chipDamagePercent", Json(ChipDamagePercent));

        auto hitboxJson = [](const HitboxDef& hb) {
            Json o = Json::MakeObject();
            o.Set("offsetX", Json(hb.offsetX));
            o.Set("offsetY", Json(hb.offsetY));
            o.Set("width", Json(hb.width));
            o.Set("height", Json(hb.height));
            return o;
        };
        Json hitboxArr = Json::MakeArray();
        for (const auto& hb : Hitboxes) hitboxArr.Push(hitboxJson(hb));
        j.Set("hitbox", std::move(hitboxArr));

        if (!FrameBoxes.empty()) {
            Json arr = Json::MakeArray();
            for (const auto& fb : FrameBoxes) {
                Json fj = Json::MakeObject();
                fj.Set("startFrame", Json(fb.startFrame));
                fj.Set("endFrame", Json(fb.endFrame));
                if (fb.hasHurtboxes) {
                    Json parts = Json::MakeArray();
                    for (const auto& part : fb.hurtboxes) {
                        Json p = Json::MakeObject();
                        p.Set("part", Json(part.Name));
                        p.Set("offsetX", Json(part.Box.CenterX));
                        p.Set("offsetY", Json(part.Box.CenterY));
                        p.Set("width", Json(part.Box.Width));
                        p.Set("height", Json(part.Box.Height));
                        parts.Push(std::move(p));
                    }
                    fj.Set("hurtboxes", std::move(parts));
                }
                if (fb.hasPushbox) {
                    Json p = Json::MakeObject();
                    p.Set("offsetX", Json(fb.pushbox.CenterX));
                    p.Set("offsetY", Json(fb.pushbox.CenterY));
                    p.Set("width", Json(fb.pushbox.Width));
                    p.Set("height", Json(fb.pushbox.Height));
                    fj.Set("pushbox", std::move(p));
                }
                if (fb.hasHitboxes) {
                    Json hits = Json::MakeArray();
                    for (const auto& hb : fb.hitboxes) hits.Push(hitboxJson(hb));
                    fj.Set("hitboxes", std::move(hits));
                }
                arr.Push(std::move(fj));
            }
            j.Set("frameBoxes", std::move(arr));
        }

        if (GuardType == Constants::GuardThrow) j.Set("throwRange", Json(ThrowRange));
        j.Set("knockbackX", Json(KnockbackX));
        j.Set("knockbackY", Json(KnockbackY));
        j.Set("hitOutcome", Json(HitOutcome));
        j.Set("meterGain", Json(MeterGain));
        j.Set("meterCost", Json(MeterCost));

        Json routes = Json::MakeArray();
        for (const auto& r : CancelRoutes) routes.Push(Json(r));
        j.Set("cancelRoutes", std::move(routes));
        j.Set("cancelStartFrame", Json(CancelStartFrame));
        j.Set("cancelEndFrame", Json(CancelEndFrame));

        Json tags = Json::MakeArray();
        for (const auto& t : Tags) tags.Push(Json(t));
        j.Set("tags", std::move(tags));

        Json inv = Json::MakeObject();
        inv.Set("type", Json(Inv.type));
        inv.Set("start_frame", Json(Inv.start_frame));
        inv.Set("end_frame", Json(Inv.end_frame));
        j.Set("invincibility", std::move(inv));

        j.Set("input", Json(InputCommand));
        j.Set("button", Json(Button));
        j.Set("stance", Json(Stance));
        j.Set("requiresAir", Json(RequiresAir));

        Json proj = Json::MakeObject();
        if (Projectile.present) {
            proj.Set("speed", Json(Projectile.speed));
            proj.Set("lifetime", Json(Projectile.lifetime));
            proj.Set("width", Json(Projectile.width));
            proj.Set("height", Json(Projectile.height));
            proj.Set("spawnOffsetX", Json(Projectile.spawnOffsetX));
            proj.Set("spawnOffsetY", Json(Projectile.spawnOffsetY));
        }
        j.Set("projectile", std::move(proj));

        j.Set("effectiveRange", Json(EffectiveRange));
        j.Set("hasDynamicHitbox", Json(HasDynamicHitbox));
        j.Set("motionImage", Json(MotionImagePath));
        return j;
    }
};

} // namespace kakuge

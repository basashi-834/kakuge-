// =====================================================================
// platform/Editor.cpp - キャラクターエディタの中身
// =====================================================================
// 大きく 3 つの部分でできています。
//
//   1. 下書きの管理    … 編集中の値を持つ（保存するまでファイルは触らない）
//   2. 項目一覧の組み立て … タブごとに「編集できる行」を並べる
//   3. 描画と入力      … 画面に出して、キーで動かす
//
// 2 の「項目一覧」が、このファイルの中心的な考え方です。
// 各行は「ラベル」と「値を読む関数・書く関数」の組でできていて、
// 画面はその一覧を上から順に描くだけ。項目を増やしたいときは、
// BuildXxxFields() に 1 行足すだけで済みます。
// =====================================================================
#include "platform/Editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "platform/Figure.h"
#include "platform/Font.h"
#include "platform/Palette.h"

namespace kakuge {

namespace {

// 画面のどこに何を置くか。
//
// すべて「日本語 1 行ぶんの高さ」から計算します。パソコンのフォントが
// 使えるときは 1 行 12 ドット、使えず内蔵のカナに戻ったときは 8 ドット
// になるので、固定値で組むと字が帯からはみ出したり、逆にすかすかに
// なったりします。高さ 1 つを基準にすれば、どちらでも収まります。
inline float LineH() { return static_cast<float>(JapaneseLineHeight()); }

inline float HeaderH() { return LineH() + 3.0f; }              // 上のタイトル帯
inline float TabY() { return HeaderH() + 2.0f; }               // タブ行
inline float TabH() { return LineH() + 3.0f; }
inline float BodyY() { return TabY() + TabH() + 4.0f; }        // 本体の開始
inline float FooterH() { return LineH() * 2.0f + 5.0f; }       // 下の操作説明の高さ

// 下の操作説明を置く Y 座標。内部キャンバスの高さはウィンドウの形で
// 変わるので、固定値ではなく「画面の下から数えた位置」にします。
inline float FooterY() { return static_cast<float>(VirtualH) - FooterH(); }

// 1 行の高さ。日本語 1 文字ぶんの高さ（Font.h の JapaneseLineHeight）に
// 上下 1 ドットずつの余裕を足して決めます。パソコンのフォントを使うと
// 12 ドット、内蔵のカナだと 8 ドットなので、行の高さもそれに追随します。
// 固定値にすると、フォントが変わったときに字が重なったり、
// 逆に間延びしたりします。
float RowHeight() { return static_cast<float>(JapaneseLineHeight()) + 2.0f; }

// 数値を文字列にする。小数桁数を指定できます。
std::string FormatNumber(double v, int decimals) {
    char buf[48];
    if (decimals <= 0) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(v)));
    } else {
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    }
    return buf;
}

// 選択肢の中から、今の値の位置を探す（見つからなければ 0）。
int IndexOfOption(const std::vector<std::string>& options, const std::string& value) {
    for (size_t i = 0; i < options.size(); ++i) {
        if (options[i] == value) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

// =====================================================================
// 開く / 読み込む
// =====================================================================
void Editor::Open(DataManager* dm) {
    dm_ = dm;
    wantsExit_ = false;
    dirty_ = false;
    editingText_ = false;
    tab_ = Tab::Character;
    selected_ = 0;
    scroll_ = 0;
    boxTarget_ = BoxTarget::Hitbox;
    boxIndex_ = 0;
    message_.clear();
    messageTimer_ = 0.0;

    charIds_ = dm_->GetCharacterIds();
    charIndex_ = 0;
    LoadCharacter(0);
}

void Editor::LoadCharacter(int index) {
    if (charIds_.empty()) return;
    charIndex_ = std::clamp(index, 0, static_cast<int>(charIds_.size()) - 1);
    const CharacterStats* stats = dm_->GetCharacter(charIds_[static_cast<size_t>(charIndex_)]);
    if (stats) statsDraft_ = *stats;

    // 技の一覧を作る。unordered_map は順番がばらばらなので、
    // 毎回同じ並びになるよう ID で並べ替えます
    //（順番が変わると、選んでいた技が勝手に入れ替わってしまいます）。
    moveIds_.clear();
    if (const auto* moveset = dm_->GetMoveset(statsDraft_.Id)) {
        for (const auto& kv : *moveset) moveIds_.push_back(kv.first);
        std::sort(moveIds_.begin(), moveIds_.end());
    }
    moveIndex_ = 0;
    LoadMove(0);
    RebuildFields();
}

void Editor::LoadMove(int index) {
    if (moveIds_.empty()) { moveDraft_ = MoveData(); return; }
    moveIndex_ = std::clamp(index, 0, static_cast<int>(moveIds_.size()) - 1);
    const MoveData* move = dm_->GetMove(statsDraft_.Id, moveIds_[static_cast<size_t>(moveIndex_)]);
    if (move) moveDraft_ = *move;
    boxIndex_ = 0;
}

// =====================================================================
// 項目一覧の組み立て
// =====================================================================
void Editor::RebuildFields() {
    fields_.clear();
    switch (tab_) {
        case Tab::Character: BuildCharacterFields(); break;
        case Tab::Move: BuildMoveFields(); break;
        case Tab::Boxes: BuildBoxFields(); break;
    }
    // 選択位置が範囲外にならないように整えます。
    if (fields_.empty()) { selected_ = 0; return; }
    selected_ = std::clamp(selected_, 0, static_cast<int>(fields_.size()) - 1);
    // 表示だけの行は選べないので、選べる行までずらします。
    int guard = 0;
    while (fields_[static_cast<size_t>(selected_)].kind == Field::Kind::Info &&
           guard++ < static_cast<int>(fields_.size())) {
        selected_ = (selected_ + 1) % static_cast<int>(fields_.size());
    }
}

void Editor::BuildCharacterFields() {
    // 数値項目を 1 行足すための小さな助け舟。
    auto addNum = [&](const std::string& label, std::function<double()> get,
                      std::function<void(double)> set, double step,
                      double lo, double hi, int decimals,
                      const std::string& unit = std::string()) {
        Field f;
        f.kind = Field::Kind::Number;
        f.label = label;
        f.getNum = std::move(get);
        f.setNum = std::move(set);
        f.step = step;
        f.minV = lo; f.maxV = hi; f.hasRange = true;
        f.decimals = decimals;
        f.unit = unit;
        fields_.push_back(std::move(f));
    };

    auto addChoice = [&](const std::string& label, std::vector<std::string> options,
                         std::function<int()> get, std::function<void(int)> set,
                         std::vector<std::string> labels = {}) {
        Field f;
        f.kind = Field::Kind::Choice;
        f.label = label;
        f.options = std::move(options);
        f.optionLabels = std::move(labels);
        f.getIndex = std::move(get);
        f.setIndex = std::move(set);
        fields_.push_back(std::move(f));
    };

    {   // 名前（文字）
        Field f;
        f.kind = Field::Kind::Text;
        f.label = Loc("名前", "NAME");
        f.getText = [this] { return statsDraft_.Name; };
        f.setText = [this](const std::string& v) { statsDraft_.Name = v; };
        fields_.push_back(std::move(f));
    }
    {   // ID は読み取り専用（変えるとファイル名が変わり、別キャラになるため）
        Field f;
        f.kind = Field::Kind::Info;
        f.label = "ID";
        f.getInfo = [this] { return statsDraft_.Id; };
        fields_.push_back(std::move(f));
    }

    addNum(Loc("最大HP", "MAX HP"), [this] { return statsDraft_.MaxHP; },
           [this](double v) { statsDraft_.MaxHP = static_cast<int>(v); }, 10, 100, 9999, 0);
    // 速度はすべて「1 秒あたり何ピクセル進むか」です。
    // 画面は 384px 幅なので、67 なら画面を横切るのに約 5.7 秒かかります。
    addNum(Loc("前歩き速度", "WALK FWD"), [this] { return statsDraft_.WalkForwardSpeed; },
           [this](double v) { statsDraft_.WalkForwardSpeed = v; }, 1, 0, 400, 1, "PX/S");
    addNum(Loc("後ろ歩き速度", "WALK BACK"), [this] { return statsDraft_.WalkBackwardSpeed; },
           [this](double v) { statsDraft_.WalkBackwardSpeed = v; }, 1, 0, 400, 1, "PX/S");
    // 前・前 と入れたときの踏み込み方。
    //   ダッシュ … 一定時間ずっと速く走る（距離を自分で調節できる）
    //   ステップ … 決まった距離だけ前へ跳ぶ（毎回同じなので覚えやすい）
    addChoice(Loc("前進の方式", "FORWARD MOVE"), {"dash", "step"},
              [this] { return statsDraft_.ForwardMoveType == "step" ? 1 : 0; },
              [this](int i) {
                  statsDraft_.ForwardMoveType = (i == 1) ? "step" : "dash";
                  RebuildFields(); // 方式で表示する項目が変わるので作り直す
              },
              japanese_ ? std::vector<std::string>{"ダッシュ", "ステップ"}
                        : std::vector<std::string>{});

    if (statsDraft_.ForwardMoveType == "step") {
        addNum(Loc("ステップ距離", "STEP DISTANCE"), [this] { return statsDraft_.StepDistance; },
               [this](double v) { statsDraft_.StepDistance = v; }, 2, 4, 240, 0, "PX");
        addNum(Loc("ステップ時間", "STEP FRAMES"), [this] { return statsDraft_.StepFrames; },
               [this](double v) { statsDraft_.StepFrames = static_cast<int>(v); }, 1, 1, 40, 0, "F");
        {   // 何体ぶん進むのかを、身長 95px のキャラを基準に出します
            Field f;
            f.kind = Field::Kind::Info;
            f.label = Loc("  ↑ キャラ何体分", " ^ IN CHARACTER WIDTHS");
            f.getInfo = [this] {
                double bodies = statsDraft_.StepDistance / GameSpec::CharacterVisualWidth;
                return FormatNumber(bodies, 2) + Loc(" 体分", " BODIES");
            };
            fields_.push_back(std::move(f));
        }
    } else {
        addNum(Loc("ダッシュ速度", "DASH SPEED"), [this] { return statsDraft_.DashSpeed; },
               [this](double v) { statsDraft_.DashSpeed = v; }, 1, 0, 800, 1, "PX/S");
    }
    // ジャンプ初速は上向きがマイナス。数値が小さいほど高く跳びます。
    addNum(Loc("ジャンプ初速", "JUMP VEL"), [this] { return statsDraft_.JumpVelocity; },
           [this](double v) { statsDraft_.JumpVelocity = v; }, 1, -600, -1, 1, "PX/S");
    // 重力は「1 秒で下向きの速度がどれだけ増えるか」。
    // 単位だけでは実感が湧かないので、下に換算値を並べます。
    addNum(Loc("重力", "GRAVITY"), [this] { return statsDraft_.Gravity; },
           [this](double v) { statsDraft_.Gravity = v; }, 5, 50, 3000, 1, "PX/S2");

    {   // 重力が何に効いているのかを、実際の数字で見せる
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("  ↑ 1Fで速くなる量", " ^ SPEED GAIN PER FRAME");
        f.getInfo = [this] {
            return FormatNumber(statsDraft_.Gravity / Constants::Fps, 2) +
                   Loc(" PX/S", " PX/S");
        };
        fields_.push_back(std::move(f));
    }
    addNum(Loc("色 R", "COLOR R"), [this] { return statsDraft_.ColorR; },
           [this](double v) { statsDraft_.ColorR = static_cast<int>(v); }, 5, 0, 255, 0, "0-255");
    addNum(Loc("色 G", "COLOR G"), [this] { return statsDraft_.ColorG; },
           [this](double v) { statsDraft_.ColorG = static_cast<int>(v); }, 5, 0, 255, 0, "0-255");
    addNum(Loc("色 B", "COLOR B"), [this] { return statsDraft_.ColorB; },
           [this](double v) { statsDraft_.ColorB = static_cast<int>(v); }, 5, 0, 255, 0, "0-255");

    {   // 跳べる高さを計算して見せる（数値だけでは想像しづらいので）
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("ジャンプ高さ / 滞空", "JUMP HEIGHT / AIRTIME");
        f.getInfo = [this] {
            double v = std::abs(statsDraft_.JumpVelocity);
            double g = std::max(1.0, statsDraft_.Gravity);
            double h = (v * v) / (2.0 * g);         // 物理の公式そのまま
            double airFrames = (2.0 * v / g) * Constants::Fps; // 滞空フレーム数
            // 身長 95px の何倍まで跳べるかも出します。
            double bodies = h / GameSpec::CharacterVisualHeight;
            return FormatNumber(h, 0) + "PX (" + FormatNumber(bodies, 1) + Loc("体分", "BODY") +
                   ") / " + FormatNumber(airFrames, 0) + "F";
        };
        fields_.push_back(std::move(f));
    }

    {   // 新規作成
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> このキャラを複製して新規作成",
                       "> NEW CHARACTER (COPY THIS)");
        f.onActivate = [this] { CreateNewCharacter(); };
        fields_.push_back(std::move(f));
    }
}

void Editor::BuildMoveFields() {
    if (moveIds_.empty()) {
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("技がありません", "NO MOVES");
        f.getInfo = [] { return std::string("-"); };
        fields_.push_back(std::move(f));
        return;
    }

    auto addNum = [&](const std::string& label, std::function<double()> get,
                      std::function<void(double)> set, double step,
                      double lo, double hi, int decimals,
                      const std::string& unit = std::string()) {
        Field f;
        f.kind = Field::Kind::Number;
        f.label = label;
        f.getNum = std::move(get);
        f.setNum = std::move(set);
        f.step = step;
        f.minV = lo; f.maxV = hi; f.hasRange = true;
        f.decimals = decimals;
        f.unit = unit;
        fields_.push_back(std::move(f));
    };
    // labels を渡すと、保存される値ではなくそちらを画面に出します。
    auto addChoice = [&](const std::string& label, std::vector<std::string> options,
                         std::function<int()> get, std::function<void(int)> set,
                         std::vector<std::string> labels = {}) {
        Field f;
        f.kind = Field::Kind::Choice;
        f.label = label;
        f.options = std::move(options);
        f.optionLabels = std::move(labels);
        f.getIndex = std::move(get);
        f.setIndex = std::move(set);
        fields_.push_back(std::move(f));
    };

    // どの技を編集するか（← → で切り替え）
    addChoice(Loc("技", "MOVE"), moveIds_, [this] { return moveIndex_; },
              [this](int i) { LoadMove(i); RebuildFields(); });

    {   // 技の表示名
        Field f;
        f.kind = Field::Kind::Text;
        f.label = Loc("技の名前", "NAME");
        f.getText = [this] { return moveDraft_.Name; };
        f.setText = [this](const std::string& v) { moveDraft_.Name = v; };
        fields_.push_back(std::move(f));
    }

    // 出し方
    addChoice(Loc("ボタン", "BUTTON"),
              {"LP", "MP", "HP", "LK", "MK", "HK", "AnyP", "AnyK", "Throw"},
              [this] {
                  static const std::vector<std::string> opts =
                      {"LP", "MP", "HP", "LK", "MK", "HK", "AnyP", "AnyK", "Throw"};
                  return IndexOfOption(opts, moveDraft_.Button);
              },
              [this](int i) {
                  static const std::vector<std::string> opts =
                      {"LP", "MP", "HP", "LK", "MK", "HK", "AnyP", "AnyK", "Throw"};
                  moveDraft_.Button = opts[static_cast<size_t>(i)];
              });
    addChoice(Loc("姿勢", "STANCE"), {"stand", "crouch", "air"},
              [this] {
                  static const std::vector<std::string> opts = {"stand", "crouch", "air"};
                  return IndexOfOption(opts, moveDraft_.Stance);
              },
              [this](int i) {
                  static const std::vector<std::string> opts = {"stand", "crouch", "air"};
                  moveDraft_.Stance = opts[static_cast<size_t>(i)];
              },
              japanese_ ? std::vector<std::string>{"立ち", "しゃがみ", "空中"}
                        : std::vector<std::string>{});
    {   // コマンド（"236" など。空なら通常技）
        Field f;
        f.kind = Field::Kind::Text;
        f.label = Loc("コマンド", "COMMAND");
        f.getText = [this] { return moveDraft_.InputCommand.empty()
                                        ? Loc("(なし)", "(NONE)") : moveDraft_.InputCommand; };
        f.setText = [this](const std::string& v) { moveDraft_.InputCommand = v; };
        fields_.push_back(std::move(f));
    }

    // フレームデータ
    addNum(Loc("発生", "STARTUP"), [this] { return moveDraft_.Startup; },
           [this](double v) { moveDraft_.Startup = static_cast<int>(v);
                              moveDraft_.TotalFrame = moveDraft_.Startup + moveDraft_.Active +
                                                      moveDraft_.Recovery; }, 1, 1, 60, 0, "F");
    addNum(Loc("持続", "ACTIVE"), [this] { return moveDraft_.Active; },
           [this](double v) { moveDraft_.Active = static_cast<int>(v);
                              moveDraft_.TotalFrame = moveDraft_.Startup + moveDraft_.Active +
                                                      moveDraft_.Recovery; }, 1, 1, 60, 0, "F");
    addNum(Loc("硬直", "RECOVERY"), [this] { return moveDraft_.Recovery; },
           [this](double v) { moveDraft_.Recovery = static_cast<int>(v);
                              moveDraft_.TotalFrame = moveDraft_.Startup + moveDraft_.Active +
                                                      moveDraft_.Recovery; }, 1, 1, 90, 0, "F");

    {   // 全体フレーム（発生＋持続＋硬直）。硬直差の計算に使う数です。
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("全体フレーム", "TOTAL FRAMES");
        f.getInfo = [this] {
            return std::to_string(moveDraft_.TotalFrames()) + "F  (" +
                   Loc("当たり時の残り ", "REMAIN ") +
                   std::to_string(moveDraft_.RemainingFramesOnEarliestHit()) + "F)";
        };
        fields_.push_back(std::move(f));
    }

    {   // 有利・不利フレームは、調整で一番よく見る数字なので必ず出します
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("硬直差 ヒット/ガード", "ADV HIT/BLOCK");
        f.getInfo = [this] {
            auto sign = [](int v) {
                return (v >= 0 ? std::string("+") : std::string("")) + std::to_string(v);
            };
            return sign(moveDraft_.OnHitAdvantage()) + " / " + sign(moveDraft_.OnBlockAdvantage());
        };
        fields_.push_back(std::move(f));
    }

    // 当たったときの効果
    addNum(Loc("ダメージ", "DAMAGE"), [this] { return moveDraft_.Damage; },
           [this](double v) { moveDraft_.Damage = static_cast<int>(v); }, 5, 0, 999, 0);
    addNum(Loc("ヒットのけぞり", "HITSTUN"), [this] { return moveDraft_.Hitstun; },
           [this](double v) { moveDraft_.Hitstun = static_cast<int>(v); }, 1, 0, 120, 0, "F");
    addNum(Loc("ガードのけぞり", "BLOCKSTUN"), [this] { return moveDraft_.Blockstun; },
           [this](double v) { moveDraft_.Blockstun = static_cast<int>(v); }, 1, 0, 120, 0, "F");
    // ストップは打撃感の要。仕様の目安は 弱2-4 / 中4-6 / 強6-9。
    // ヒットとガードで別の値を持ちます（ガードのほうを短くすると
    // 「ガードすると手応えが軽い」という差が出ます）。
    // どちらも攻撃側・防御側に同時に掛かって同時に解けるので、
    // 有利不利（硬直差）には影響しません。
    addNum(Loc("ヒットストップ", "HITSTOP"), [this] { return moveDraft_.Hitstop; },
           [this](double v) { moveDraft_.Hitstop = static_cast<int>(v); }, 1, 0, 30, 0, "F");
    addNum(Loc("ガードストップ", "GUARDSTOP"), [this] { return moveDraft_.Guardstop; },
           [this](double v) { moveDraft_.Guardstop = static_cast<int>(v); }, 1, 0, 30, 0, "F");
    addChoice(Loc("ガード種別", "GUARD"), {"High", "Low", "Overhead", "Throw"},
              [this] {
                  static const std::vector<std::string> opts = {"High", "Low", "Overhead", "Throw"};
                  return IndexOfOption(opts, moveDraft_.GuardType);
              },
              [this](int i) {
                  static const std::vector<std::string> opts = {"High", "Low", "Overhead", "Throw"};
                  moveDraft_.GuardType = opts[static_cast<size_t>(i)];
              },
              // High = 立ちでもしゃがみでも防げる / Overhead = 立ちガードのみ
              japanese_ ? std::vector<std::string>{"どちらでも", "下段", "中段", "投げ"}
                        : std::vector<std::string>{});
    addNum(Loc("ノックバック X", "KNOCKBACK X"), [this] { return moveDraft_.KnockbackX; },
           [this](double v) { moveDraft_.KnockbackX = v; }, 5, 0, 400, 1, "PX/S");
    addNum(Loc("ノックバック Y", "KNOCKBACK Y"), [this] { return moveDraft_.KnockbackY; },
           [this](double v) { moveDraft_.KnockbackY = v; }, 5, 0, 400, 1, "PX/S");
    addNum(Loc("ゲージ増加", "METER GAIN"), [this] { return moveDraft_.MeterGain; },
           [this](double v) { moveDraft_.MeterGain = static_cast<int>(v); }, 1, 0, 100, 0);
    addNum(Loc("ゲージ消費", "METER COST"), [this] { return moveDraft_.MeterCost; },
           [this](double v) { moveDraft_.MeterCost = static_cast<int>(v); }, 5, 0, 100, 0);
    // キャンセル可能時間帯。ここが開いている間だけ次の技につなげます
    //（＝コンボの成立条件）。
    addNum(Loc("キャンセル開始F", "CANCEL FROM"), [this] { return moveDraft_.CancelStartFrame; },
           [this](double v) { moveDraft_.CancelStartFrame = static_cast<int>(v); }, 1, 0, 90, 0, "F");
    addNum(Loc("キャンセル終了F", "CANCEL TO"), [this] { return moveDraft_.CancelEndFrame; },
           [this](double v) { moveDraft_.CancelEndFrame = static_cast<int>(v); }, 1, 0, 90, 0, "F");
    addNum(Loc("投げ距離", "THROW RANGE"), [this] { return moveDraft_.ThrowRange; },
           [this](double v) { moveDraft_.ThrowRange = v; }, 1, 0, 120, 0, "PX");
}

void Editor::BuildBoxFields() {
    // labels を渡すと、保存される値ではなくそちらを画面に出します。
    auto addChoice = [&](const std::string& label, std::vector<std::string> options,
                         std::function<int()> get, std::function<void(int)> set,
                         std::vector<std::string> labels = {}) {
        Field f;
        f.kind = Field::Kind::Choice;
        f.label = label;
        f.options = std::move(options);
        f.optionLabels = std::move(labels);
        f.getIndex = std::move(get);
        f.setIndex = std::move(set);
        fields_.push_back(std::move(f));
    };

    // 何の判定を編集するか
    addChoice(Loc("対象", "TARGET"),
              {"HITBOX", "HURT STAND", "HURT CROUCH", "HURT AIR"},
              [this] { return static_cast<int>(boxTarget_); },
              [this](int i) {
                  boxTarget_ = static_cast<BoxTarget>(i);
                  boxIndex_ = 0;
                  RebuildFields();
              },
              japanese_ ? std::vector<std::string>{"攻撃判定", "食らい 立ち",
                                                   "食らい しゃがみ", "食らい 空中"}
                        : std::vector<std::string>{});

    if (boxTarget_ == BoxTarget::Hitbox) {
        // どの技の判定かが分かるように、技名も出します
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("技", "MOVE");
        f.getInfo = [this] { return moveIds_.empty() ? std::string("-")
                                                     : moveIds_[static_cast<size_t>(moveIndex_)]; };
        fields_.push_back(std::move(f));
    }

    int count = CurrentBoxCount();
    {
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("判定", "BOX");
        f.getInfo = [this, count] {
            if (count == 0) return Loc("なし", "NONE");
            return std::to_string(boxIndex_ + 1) + " / " + std::to_string(count);
        };
        fields_.push_back(std::move(f));
    }

    if (count > 1) {
        std::vector<std::string> nums;
        for (int i = 0; i < count; ++i) nums.push_back(std::to_string(i + 1));
        addChoice(Loc("判定を選ぶ", "SELECT BOX"), nums, [this] { return boxIndex_; },
                  [this](int i) { boxIndex_ = i; RebuildFields(); });
    }

    if (count > 0) {
        // 値の場所をポインタで覚えず、読み書きのたびに引き直します
        //（箱を足したり技を変えたりしても壊れないようにするため）。
        auto addBoxNum = [&](const std::string& label, int which, double lo, double hi) {
            Field f;
            f.kind = Field::Kind::Number;
            f.label = label;
            f.getNum = [this, which] { return GetBoxValue(which); };
            f.setNum = [this, which](double v) { SetBoxValue(which, v); };
            f.step = 1;
            f.minV = lo; f.maxV = hi; f.hasRange = true;
            f.decimals = 1;
            f.unit = "PX";
            fields_.push_back(std::move(f));
        };
        // 座標はキャラクターの中心（X）と足元（Y）からの相対。
        // Y は上がマイナスなので、頭のあたりは -95 くらいになります。
        addBoxNum(Loc("位置 X", "OFFSET X"), 0, -120, 120);
        addBoxNum(Loc("位置 Y", "OFFSET Y"), 1, -140, 20);
        addBoxNum(Loc("幅", "WIDTH"), 2, 1, 160);
        addBoxNum(Loc("高さ", "HEIGHT"), 3, 1, 160);
        // 判定の座標は「キャラクターの中心（X）と足元（Y）から何ピクセル」。
        // Y は上がマイナスなので、頭のあたりは -95 くらいになります。
    }

    {
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> 判定を追加", "> ADD BOX");
        f.onActivate = [this] { AddBox(); };
        fields_.push_back(std::move(f));
    }
    if (count > 0) {
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> 判定を削除", "> DELETE BOX");
        f.onActivate = [this] { DeleteBox(); };
        fields_.push_back(std::move(f));
    }
}

// =====================================================================
// 判定の取り出し
// =====================================================================
// HitboxDef（技の攻撃判定）と RectBox（食らい判定）は別の型ですが、
// どちらも「中心 X・中心 Y・幅・高さ」の 4 つの数値です。
// 編集画面では同じ 4 項目として扱いたいので、
//   0=OffsetX 1=OffsetY 2=Width 3=Height
// という番号で読み書きし、型の違いはここで吸収します。
namespace {
RectBox ToRect(const HitboxDef& d) { return RectBox(d.offsetX, d.offsetY, d.width, d.height); }
} // namespace

int Editor::CurrentBoxCount() const {
    switch (boxTarget_) {
        case BoxTarget::Hitbox: return static_cast<int>(moveDraft_.Hitboxes.size());
        case BoxTarget::HurtStand: return static_cast<int>(statsDraft_.Hurtboxes.Stand.size());
        case BoxTarget::HurtCrouch: return static_cast<int>(statsDraft_.Hurtboxes.Crouch.size());
        case BoxTarget::HurtAir: return static_cast<int>(statsDraft_.Hurtboxes.Air.size());
    }
    return 0;
}

std::string Editor::CurrentStanceName() const {
    switch (boxTarget_) {
        case BoxTarget::HurtCrouch: return "crouch";
        case BoxTarget::HurtAir: return "air";
        default: return "stand";
    }
}

// 今の対象の「部位一覧」を取り出す（攻撃判定のときは nullptr）。
namespace {
const std::vector<HurtboxPart>* HurtPartsConst(const CharacterStats& stats, int target) {
    switch (target) {
        case 1: return &stats.Hurtboxes.Stand;
        case 2: return &stats.Hurtboxes.Crouch;
        case 3: return &stats.Hurtboxes.Air;
        default: return nullptr;
    }
}
std::vector<HurtboxPart>* HurtParts(CharacterStats& stats, int target) {
    switch (target) {
        case 1: return &stats.Hurtboxes.Stand;
        case 2: return &stats.Hurtboxes.Crouch;
        case 3: return &stats.Hurtboxes.Air;
        default: return nullptr;
    }
}
} // namespace

bool Editor::GetBoxRect(int index, RectBox& out) const {
    if (index < 0 || index >= CurrentBoxCount()) return false;
    size_t i = static_cast<size_t>(index);
    if (boxTarget_ == BoxTarget::Hitbox) {
        out = ToRect(moveDraft_.Hitboxes[i]);
        return true;
    }
    const auto* parts = HurtPartsConst(statsDraft_, static_cast<int>(boxTarget_));
    if (!parts) return false;
    out = (*parts)[i].Box;
    return true;
}

double Editor::GetBoxValue(int which) const {
    RectBox b;
    if (!GetBoxRect(boxIndex_, b)) return 0.0;
    switch (which) {
        case 0: return b.CenterX;
        case 1: return b.CenterY;
        case 2: return b.Width;
        default: return b.Height;
    }
}

void Editor::SetBoxValue(int which, double value) {
    int count = CurrentBoxCount();
    if (count == 0) return;
    size_t i = static_cast<size_t>(std::clamp(boxIndex_, 0, count - 1));

    if (boxTarget_ == BoxTarget::Hitbox) {
        HitboxDef& d = moveDraft_.Hitboxes[i];
        switch (which) {
            case 0: d.offsetX = value; break;
            case 1: d.offsetY = value; break;
            case 2: d.width = value; break;
            default: d.height = value; break;
        }
        return;
    }
    auto* parts = HurtParts(statsDraft_, static_cast<int>(boxTarget_));
    if (!parts) return;
    RectBox& b = (*parts)[i].Box;
    switch (which) {
        case 0: b.CenterX = value; break;
        case 1: b.CenterY = value; break;
        case 2: b.Width = value; break;
        default: b.Height = value; break;
    }
}

void Editor::AddBox() {
    if (boxTarget_ == BoxTarget::Hitbox) {
        // 新しい判定は、前方の胸の高さに小さめで作ります
        //（そこから調整するのが一番やりやすい位置）。
        HitboxDef d;
        d.offsetX = 28; d.offsetY = -60; d.width = 18; d.height = 12;
        moveDraft_.Hitboxes.push_back(d);
        boxIndex_ = static_cast<int>(moveDraft_.Hitboxes.size()) - 1;
    } else {
        auto* parts = HurtParts(statsDraft_, static_cast<int>(boxTarget_));
        if (!parts) return;
        HurtboxPart part;
        part.Name = "part" + std::to_string(parts->size() + 1);
        part.Box = RectBox(0, -30, 24, 24);
        parts->push_back(part);
        boxIndex_ = static_cast<int>(parts->size()) - 1;
    }
    dirty_ = true;
    SetMessage("判定を追加しました", "BOX ADDED");
    RebuildFields();
}

void Editor::DeleteBox() {
    int count = CurrentBoxCount();
    if (count == 0) return;
    int idx = std::clamp(boxIndex_, 0, count - 1);

    if (boxTarget_ == BoxTarget::Hitbox) {
        moveDraft_.Hitboxes.erase(moveDraft_.Hitboxes.begin() + idx);
    } else {
        auto* parts = HurtParts(statsDraft_, static_cast<int>(boxTarget_));
        if (!parts) return;
        parts->erase(parts->begin() + idx);
    }
    boxIndex_ = std::max(0, boxIndex_ - 1);
    dirty_ = true;
    SetMessage("判定を削除しました", "BOX DELETED");
    RebuildFields();
}

// =====================================================================
// 保存・新規作成
// =====================================================================
void Editor::Save() {
    if (!dm_) return;
    dm_->SaveCharacter(statsDraft_);
    if (!moveIds_.empty()) dm_->SaveMove(statsDraft_.Id, moveDraft_);
    dirty_ = false;
    SetMessage("ユーザーフォルダに保存しました", "SAVED TO USER FOLDER");
}

void Editor::CreateNewCharacter() {
    if (!dm_) return;
    // 「ryu2」「ryu3」… のように、空いている ID を探します。
    std::string base = statsDraft_.Id.empty() ? std::string("fighter") : statsDraft_.Id;
    std::string newId;
    for (int n = 2; n < 100; ++n) {
        std::string candidate = base + std::to_string(n);
        if (dm_->GetCharacter(candidate) == nullptr) { newId = candidate; break; }
    }
    if (newId.empty()) { SetMessage("空いている ID がありません", "NO FREE ID"); return; }

    if (!dm_->CreateCharacter(newId, statsDraft_.Name + " " + newId, statsDraft_.Id)) {
        SetMessage("作成できませんでした", "CREATE FAILED");
        return;
    }
    dm_->ReloadAll();
    charIds_ = dm_->GetCharacterIds();
    // 作ったキャラクターに切り替える
    for (size_t i = 0; i < charIds_.size(); ++i) {
        if (charIds_[i] == newId) { LoadCharacter(static_cast<int>(i)); break; }
    }
    dirty_ = false;
    SetMessage(newId + " を作成しました", "CREATED " + newId);
}

void Editor::SetMessage(const std::string& jp, const std::string& en) {
    message_ = Loc(jp, en);
    messageTimer_ = 2.5;
}

// =====================================================================
// 入力
// =====================================================================
void Editor::MoveSelection(int delta) {
    if (fields_.empty()) return;
    int n = static_cast<int>(fields_.size());
    int next = selected_;
    // 表示だけの行（Info）は飛ばします。
    for (int i = 0; i < n; ++i) {
        next = (next + delta + n) % n;
        if (fields_[static_cast<size_t>(next)].kind != Field::Kind::Info) break;
    }
    selected_ = next;
}

void Editor::AdjustSelected(int direction, bool shift) {
    if (fields_.empty()) return;
    Field& f = fields_[static_cast<size_t>(selected_)];

    if (f.kind == Field::Kind::Number && f.getNum && f.setNum) {
        // Shift を押していると 10 倍動きます。細かい微調整と、
        // 大きく変えたいときを 1 つのキーで両立させるためです。
        double step = f.step * (shift ? 10.0 : 1.0);
        double v = f.getNum() + step * direction;
        if (f.hasRange) v = std::clamp(v, f.minV, f.maxV);
        f.setNum(v);
        dirty_ = true;
    } else if (f.kind == Field::Kind::Choice && f.getIndex && f.setIndex && !f.options.empty()) {
        int n = static_cast<int>(f.options.size());
        int i = (f.getIndex() + direction + n) % n;
        f.setIndex(i);
        dirty_ = true;
    }
}

void Editor::ActivateSelected() {
    if (fields_.empty()) return;
    Field& f = fields_[static_cast<size_t>(selected_)];
    if (f.kind == Field::Kind::Action && f.onActivate) {
        f.onActivate();
    } else if (f.kind == Field::Kind::Text && f.getText) {
        // 文字入力の開始。今の値を編集用の文字列に写します。
        editingText_ = true;
        editingField_ = selected_;
        textBuffer_ = f.getText();
        if (textBuffer_ == "(NONE)" || textBuffer_ == "(なし)") textBuffer_.clear();
    }
}

bool Editor::HandleKey(SDL_Keycode key, bool shift) {
    // ---- 文字入力中の処理を先に ----
    if (editingText_) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (editingField_ >= 0 && editingField_ < static_cast<int>(fields_.size())) {
                Field& f = fields_[static_cast<size_t>(editingField_)];
                if (f.setText) { f.setText(textBuffer_); dirty_ = true; }
            }
            editingText_ = false;
            RebuildFields();
            return true;
        }
        if (key == SDLK_ESCAPE) { editingText_ = false; return true; } // 取り消し
        if (key == SDLK_BACKSPACE) {
            // UTF-8 の途中で切らないよう、続きのバイト（10xxxxxx）を
            // まとめて取り除きます。日本語 1 文字が 3 バイトあるためです。
            while (!textBuffer_.empty()) {
                unsigned char c = static_cast<unsigned char>(textBuffer_.back());
                textBuffer_.pop_back();
                if ((c & 0xC0) != 0x80) break;
            }
            return true;
        }
        return true; // 入力中は他のキーを通さない
    }

    switch (key) {
        case SDLK_UP:    MoveSelection(-1); return true;
        case SDLK_DOWN:  MoveSelection(1); return true;
        case SDLK_LEFT:  AdjustSelected(-1, shift); return true;
        case SDLK_RIGHT: AdjustSelected(1, shift); return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: ActivateSelected(); return true;

        case SDLK_TAB: {
            int t = (static_cast<int>(tab_) + (shift ? 2 : 1)) % 3;
            tab_ = static_cast<Tab>(t);
            selected_ = 0;
            scroll_ = 0;
            RebuildFields();
            return true;
        }
        case SDLK_s: Save(); return true;
        case SDLK_l:
            // 表示言語の切り替え。ラベルは項目を組み立てるときに決まるので、
            // 切り替えたら一覧を作り直す必要があります。
            japanese_ = !japanese_;
            RebuildFields();
            SetMessage("表示: 日本語", "LANGUAGE: ENGLISH");
            return true;
        case SDLK_PAGEUP:
            if (!charIds_.empty()) {
                LoadCharacter((charIndex_ - 1 + static_cast<int>(charIds_.size())) %
                              static_cast<int>(charIds_.size()));
                SetMessage("キャラ: " + statsDraft_.Id, "CHARACTER: " + statsDraft_.Id);
            }
            return true;
        case SDLK_PAGEDOWN:
            if (!charIds_.empty()) {
                LoadCharacter((charIndex_ + 1) % static_cast<int>(charIds_.size()));
                SetMessage("キャラ: " + statsDraft_.Id, "CHARACTER: " + statsDraft_.Id);
            }
            return true;
        case SDLK_ESCAPE:
            wantsExit_ = true;
            return true;
        default:
            return false;
    }
}

void Editor::HandleText(const char* utf8Text) {
    if (!editingText_ || !utf8Text) return;
    // 長すぎる名前は表示が崩れるので、ほどほどで止めます。
    if (textBuffer_.size() < 40) textBuffer_ += utf8Text;
}

// =====================================================================
// 描画
// =====================================================================
void Editor::Draw(Renderer& r) {
    const auto& pal = GetPalette();
    r.Clear(pal.Bg);

    DrawHeader(r);
    DrawTabs(r);

    if (tab_ == Tab::Boxes) {
        // 左に項目、右にキャラクターと判定のプレビュー。
        // 幅の割り当ては画面の広さに応じて決めます（項目側は
        // ラベルと数値が並ぶので、最低 176 は確保します）。
        float listW = std::max(176.0f, VirtualW * 0.46f);
        DrawFieldList(r, 4, BodyY(), listW, FooterY() - BodyY() - 4);
        DrawBoxPreview(r, listW + 12, BodyY(), VirtualW - listW - 16,
                       FooterY() - BodyY() - 4);
    } else {
        DrawFieldList(r, 4, BodyY(), VirtualW - 8, FooterY() - BodyY() - 4);
    }

    DrawFooter(r);

    if (messageTimer_ > 0.0) messageTimer_ -= 1.0 / 60.0;
}

void Editor::DrawHeader(Renderer& r) {
    const auto& pal = GetPalette();
    r.FillRect(0, 0, VirtualW, HeaderH(), pal.Accent);
    DrawPixelText(r, Loc("キャラクターエディタ", "CHARACTER EDITOR"),
                  4, 2, 1.0f, pal.OnAccent);

    // 今どのキャラクターを編集しているか（PgUp/PgDn で切り替え）
    std::string right = statsDraft_.Id;
    if (charIds_.size() > 1) {
        right = "< " + right + " > (" + std::to_string(charIndex_ + 1) + "/" +
                std::to_string(charIds_.size()) + ")";
    }
    if (dirty_) right += " *"; // 未保存の印
    DrawPixelTextRight(r, right, VirtualW - 4, 3, 1.0f, pal.OnAccent);
}

void Editor::DrawTabs(Renderer& r) {
    const auto& pal = GetPalette();
    const std::string names[3] = {Loc("キャラクター", "CHARACTER"),
                                 Loc("技", "MOVE"),
                                 Loc("判定", "BOXES")};
    float x = 4;
    for (int i = 0; i < 3; ++i) {
        float w = PixelTextWidth(names[i], 1.0f) + 10;
        bool active = (static_cast<int>(tab_) == i);
        if (active) {
            r.FillRect(x, TabY(), w, TabH(), pal.Ink);
            DrawPixelText(r, names[i], x + 5, TabY() + 1, 1.0f, pal.White);
        } else {
            r.DrawRect(x, TabY(), w, TabH(), pal.RuleSoft, 1.0f);
            DrawPixelText(r, names[i], x + 5, TabY() + 1, 1.0f, pal.Ink55);
        }
        x += w + 3;
    }
    // タブの下の区切り線
    r.FillRect(0, TabY() + TabH() + 1, VirtualW, 1, pal.RuleSoft);
}

void Editor::DrawFieldList(Renderer& r, float x, float y, float w, float h) {
    const auto& pal = GetPalette();
    if (fields_.empty()) {
        DrawPixelText(r, Loc("データがありません", "NO DATA"), x + 4, y + 4, 1.0f, pal.Ink55);
        return;
    }

    int visibleRows = static_cast<int>(h / RowHeight());
    // 選んでいる行が必ず見えるように、表示の先頭をずらします。
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visibleRows) scroll_ = selected_ - visibleRows + 1;
    scroll_ = std::max(0, std::min(scroll_,
                                   std::max(0, static_cast<int>(fields_.size()) - visibleRows)));

    for (int i = 0; i < visibleRows; ++i) {
        int index = scroll_ + i;
        if (index >= static_cast<int>(fields_.size())) break;
        const Field& f = fields_[static_cast<size_t>(index)];
        float ry = y + i * RowHeight();
        bool isSelected = (index == selected_);

        if (isSelected) r.FillRect(x, ry - 1, w, RowHeight(), pal.PanelBg2);

        // ラベル
        Color labelColor = (f.kind == Field::Kind::Info) ? pal.Ink45
                         : (isSelected ? pal.Ink : pal.Ink70);
        DrawPixelText(r, f.label, x + 3, ry, 1.0f, labelColor);

        // 値
        std::string value;
        Color valueColor = isSelected ? pal.AccentDeep : pal.Ink;
        switch (f.kind) {
            case Field::Kind::Number:
                if (f.getNum) {
                    value = FormatNumber(f.getNum(), f.decimals);
                    if (!f.unit.empty()) value += " " + f.unit;
                }
                break;
            case Field::Kind::Text:
                if (editingText_ && index == editingField_) {
                    value = textBuffer_ + "_"; // 入力中はカーソルを出す
                    valueColor = pal.Accent;
                } else if (f.getText) {
                    value = f.getText();
                }
                break;
            case Field::Kind::Choice:
                if (f.getIndex && !f.options.empty()) {
                    int idx = std::clamp(f.getIndex(), 0, static_cast<int>(f.options.size()) - 1);
                    // 表示用のラベルがあればそちらを出す（無ければ値そのもの）
                    const std::vector<std::string>& shown =
                        f.optionLabels.size() == f.options.size() ? f.optionLabels : f.options;
                    value = shown[static_cast<size_t>(idx)];
                    if (isSelected) value = "< " + value + " >";
                }
                break;
            case Field::Kind::Info:
                if (f.getInfo) value = f.getInfo();
                valueColor = pal.Ink55;
                break;
            case Field::Kind::Action:
                value = isSelected ? "ENTER" : "";
                break;
        }
        if (!value.empty()) DrawPixelTextRight(r, value, x + w - 3, ry, 1.0f, valueColor);
    }

    // 続きがあることを示す印
    if (scroll_ > 0) DrawPixelTextRight(r, "^", x + w - 3, y - 8, 1.0f, pal.Ink45);
    if (scroll_ + visibleRows < static_cast<int>(fields_.size())) {
        DrawPixelTextRight(r, "V", x + w - 3, y + visibleRows * RowHeight(), 1.0f, pal.Ink45);
    }
}

void Editor::DrawBoxPreview(Renderer& r, float x, float y, float w, float h) {
    const auto& pal = GetPalette();
    r.FillRect(x, y, w, h, Color(228, 226, 224));
    r.DrawRect(x, y, w, h, pal.RuleSoft, 1.0f);
    DrawPixelText(r, Loc("プレビュー", "PREVIEW"), x + 3, y + 3, 1.0f, pal.Ink45);

    // キャラクターの足元をどこに置くか。
    // 身長 95 が枠に収まるように、下寄りに配置します。
    float footX = x + w * 0.45f;
    float footY = y + h - 14;

    // 地面の線
    r.FillRect(x + 2, footY, w - 4, 1, pal.Ink45);

    HumanoidPose pose;
    pose.facing = 1;
    std::string stance = CurrentStanceName();
    if (boxTarget_ == BoxTarget::Hitbox) {
        // 技の判定を見るときは、その技の姿勢で立たせます。
        stance = moveDraft_.Stance;
        if (moveDraft_.Button.size() >= 2 && moveDraft_.Button.back() == 'K') pose.legKick = 34;
        else pose.armReach = 34;
    }
    if (stance == "crouch") pose.crouch = true;
    else if (stance == "air") pose.jump = true;

    DrawHumanoid(r, footX, footY,
                 Color(statsDraft_.ColorR, statsDraft_.ColorG, statsDraft_.ColorB), pose);

    // 判定の枠を重ねて描く。
    //   緑 … 食らい判定（ハートボックス）
    //   赤 … 攻撃判定（ヒットボックス）
    // 今選んでいる箱だけ濃く描いて、どれを編集中か分かるようにします。
    auto drawBox = [&](const RectBox& b, Color color, bool highlight) {
        float bx = footX + static_cast<float>(b.Left());
        float by = footY + static_cast<float>(b.Top());
        r.FillRect(bx, by, static_cast<float>(b.Width), static_cast<float>(b.Height),
                   color.WithAlpha(highlight ? 70 : 30));
        r.DrawRect(bx, by, static_cast<float>(b.Width), static_cast<float>(b.Height),
                   color.WithAlpha(highlight ? 255 : 120), 1.0f);
    };

    if (boxTarget_ == BoxTarget::Hitbox) {
        // 参考として、その姿勢の食らい判定も薄く出します
        //（攻撃判定が体のどこから出ているかが分かるように）。
        for (const auto& p : statsDraft_.Hurtboxes.PartsForStance(moveDraft_.Stance)) {
            drawBox(p.Box, Color(40, 170, 80), false);
        }
    }
    // 編集中の一式（攻撃判定なら赤、食らい判定なら緑）。
    // 選んでいる箱だけ濃く描いて、どれを編集中か分かるようにします。
    Color boxColor = (boxTarget_ == BoxTarget::Hitbox) ? Color(220, 50, 40) : Color(40, 170, 80);
    for (int i = 0; i < CurrentBoxCount(); ++i) {
        RectBox b;
        if (GetBoxRect(i, b)) drawBox(b, boxColor, i == boxIndex_);
    }

    // 外形の大きさを数値で出す（仕様の目安と見比べるため）
    int count = CurrentBoxCount();
    if (count > 0) {
        double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
        auto extend = [&](const RectBox& b) {
            left = std::min(left, b.Left()); right = std::max(right, b.Right());
            top = std::min(top, b.Top()); bottom = std::max(bottom, b.Bottom());
        };
        for (int i = 0; i < count; ++i) {
            RectBox b;
            if (GetBoxRect(i, b)) extend(b);
        }
        std::string size = FormatNumber(right - left, 0) + "x" + FormatNumber(bottom - top, 0);
        DrawPixelTextRight(r, size, x + w - 3, y + 3, 1.0f, pal.Ink55);
    }
}

void Editor::DrawFooter(Renderer& r) {
    const auto& pal = GetPalette();
    r.FillRect(0, FooterY(), VirtualW, VirtualH - FooterY(), pal.PanelBg);
    r.FillRect(0, FooterY(), VirtualW, 1, pal.RuleSoft);

    if (messageTimer_ > 0.0 && !message_.empty()) {
        // 保存しました等のお知らせ。数秒で消えます。
        DrawPixelText(r, message_, 4, FooterY() + 3, 1.0f, pal.AccentDeep);
        DrawPixelTextRight(r, Loc("ESC:戻る", "ESC: BACK"),
                           VirtualW - 4, FooterY() + 3, 1.0f, pal.Ink55);
        return;
    }

    if (editingText_) {
        DrawPixelText(r, Loc("文字入力  ENTER:決定  ESC:取消",
                             "TYPE TEXT   ENTER: OK   ESC: CANCEL"),
                      4, FooterY() + 3, 1.0f, pal.Ink70);
        return;
    }

    DrawPixelText(r, Loc("矢印:変更  SHIFT:10倍  ENTER:実行  TAB:ページ",
                         "ARROWS:EDIT  SHIFT:X10  ENTER:GO  TAB:PAGE"),
                  4, FooterY() + 2, 1.0f, pal.Ink70);
    DrawPixelText(r, Loc("S:保存  PGUP/PGDN:キャラ  L:日本語/EN  ESC:戻る",
                         "S:SAVE  PGUP/PGDN:CHARACTER  L:JP/EN  ESC:BACK"),
                  4, FooterY() + 3 + LineH(), 1.0f, pal.Ink55);
}

} // namespace kakuge

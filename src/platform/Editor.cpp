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

// 姿勢の値（データに入っている英語）を、日本語の呼び名にする。
// データそのものは英語のままにしておき、画面に出すときだけ変えます
//（日本語をデータに書き込むと、ファイルを直接編集する人が困るため）。
std::string StanceLabelJp(const std::string& stance) {
    if (stance == "crouch") return "しゃがみ";
    if (stance == "air") return "空中";
    return "立ち";
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
    pendingChars_.clear();
    pendingMoves_.clear();

    charIds_ = dm_->GetCharacterIds();
    charIndex_ = 0;
    LoadCharacter(0);
}

// -----------------------------------------------------------------
// 保存していない編集の預かり・取り出し
// -----------------------------------------------------------------
// 下書きは 1 つぶんしかないので、切り替える前にここへ預けます。
// 鍵は「キャラクターID/技ID」。キャラクターをまたいでも、
// 同じ名前の技どうしが混ざらないようにするためです。
namespace {
std::string MoveKey(const std::string& charId, const std::string& moveId) {
    return charId + "/" + moveId;
}
} // namespace

void Editor::RememberDrafts(bool onlyIfDirty) {
    if (onlyIfDirty && !dirty_) return;
    if (!statsDraft_.Id.empty()) pendingChars_[statsDraft_.Id] = statsDraft_;
    if (!moveDraft_.Id.empty() && !statsDraft_.Id.empty()) {
        pendingMoves_[MoveKey(statsDraft_.Id, moveDraft_.Id)] = moveDraft_;
    }
}

void Editor::LoadCharacter(int index) {
    if (charIds_.empty()) return;
    // 切り替える前に、今の下書きを預かり所へ（変更があるときだけ）。
    RememberDrafts(true);
    charIndex_ = std::clamp(index, 0, static_cast<int>(charIds_.size()) - 1);
    const std::string& id = charIds_[static_cast<size_t>(charIndex_)];
    // 預かり所にあれば、そちら（編集の続き）を優先します。
    if (auto it = pendingChars_.find(id); it != pendingChars_.end()) {
        statsDraft_ = it->second;
    } else if (const CharacterStats* stats = dm_->GetCharacter(id)) {
        statsDraft_ = *stats;
    }

    // 技の一覧を作る。unordered_map は順番がばらばらなので、
    // 毎回同じ並びになるよう ID で並べ替えます
    //（順番が変わると、選んでいた技が勝手に入れ替わってしまいます）。
    moveIds_.clear();
    if (const auto* moveset = dm_->GetMoveset(statsDraft_.Id)) {
        for (const auto& kv : *moveset) moveIds_.push_back(kv.first);
        std::sort(moveIds_.begin(), moveIds_.end());
    }
    moveIndex_ = 0;
    // ここでは預かりません（すぐ上で預けたばかりなので）。
    LoadMoveInternal(0);
    RebuildFields();
}

// 技を切り替える（外から呼ばれる入口）。
// 切り替える前に、今の下書きを預かり所へ入れます。
void Editor::LoadMove(int index) {
    RememberDrafts(true);
    LoadMoveInternal(index);
}

// 実際の読み込み。預かり所にあればそれを、無ければファイルの内容を使います。
void Editor::LoadMoveInternal(int index) {
    if (moveIds_.empty()) { moveDraft_ = MoveData(); return; }
    moveIndex_ = std::clamp(index, 0, static_cast<int>(moveIds_.size()) - 1);
    const std::string& moveId = moveIds_[static_cast<size_t>(moveIndex_)];
    if (auto it = pendingMoves_.find(MoveKey(statsDraft_.Id, moveId));
        it != pendingMoves_.end()) {
        moveDraft_ = it->second;  // 保存していない編集の続きから
    } else if (const MoveData* move = dm_->GetMove(statsDraft_.Id, moveId)) {
        moveDraft_ = *move;
    }
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
           [this](double v) { statsDraft_.MaxHP = static_cast<int>(v); }, 100, 1000, 99999, 0);
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

    // ---- 後ろ・後ろ で下がる（バックステップ）----
    // 前へ踏み込む設定とは別の数値です。前を変えても後ろは変わりません。
    addChoice(Loc("後退の方式", "BACK MOVE"), {"step", "dash"},
              [this] { return statsDraft_.BackMoveType == "dash" ? 1 : 0; },
              [this](int i) {
                  statsDraft_.BackMoveType = (i == 1) ? "dash" : "step";
                  RebuildFields();
              },
              japanese_ ? std::vector<std::string>{"バックステップ", "バックダッシュ"}
                        : std::vector<std::string>{"BACK STEP", "BACK DASH"});
    if (statsDraft_.BackMoveType == "step") {
        addNum(Loc("バックステップ距離", "BACK DISTANCE"),
               [this] { return statsDraft_.BackStepDistance; },
               [this](double v) { statsDraft_.BackStepDistance = v; }, 2, 4, 240, 0, "PX");
        addNum(Loc("バックステップ時間", "BACK FRAMES"),
               [this] { return statsDraft_.BackStepFrames; },
               [this](double v) { statsDraft_.BackStepFrames = static_cast<int>(v); },
               1, 1, 60, 0, "F");
    } else {
        addNum(Loc("バックダッシュ速度", "BACK DASH SPEED"),
               [this] { return statsDraft_.BackDashSpeed; },
               [this](double v) { statsDraft_.BackDashSpeed = v; }, 1, 0, 800, 1, "PX/S");
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

    // ---- フレームデータ ----
    // 数え方は「発生 4F ＝ 4F 目に最初の攻撃判定が出る」です。
    // 攻撃判定が出る前のフレーム数は 発生 - 1 なので、
    // 全体フレームは (発生 - 1) + 持続 + 硬直 になります。
    auto syncTotal = [this] { moveDraft_.TotalFrame = moveDraft_.TotalFrames(); };
    addNum(Loc("発生", "STARTUP"), [this] { return moveDraft_.Startup; },
           [this, syncTotal](double v) { moveDraft_.Startup = static_cast<int>(v); syncTotal(); },
           1, 1, 60, 0, "F");
    addNum(Loc("持続", "ACTIVE"), [this] { return moveDraft_.Active; },
           [this, syncTotal](double v) { moveDraft_.Active = static_cast<int>(v); syncTotal(); },
           1, 1, 60, 0, "F");
    addNum(Loc("硬直", "RECOVERY"), [this] { return moveDraft_.Recovery; },
           [this, syncTotal](double v) { moveDraft_.Recovery = static_cast<int>(v); syncTotal(); },
           1, 1, 90, 0, "F");

    {   // フレーム表の読み方をそのまま出します（何F目に何が起きるか）。
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("全体フレーム", "TOTAL FRAMES");
        f.getInfo = [this] {
            const MoveData& m = moveDraft_;
            std::string active = std::to_string(m.Startup) + "-" +
                                 std::to_string(m.Startup + m.Active - 1);
            std::string recov = std::to_string(m.Startup + m.Active) + "-" +
                                std::to_string(m.ActionableFrame() - 1);
            return std::to_string(m.TotalFrames()) + "F  " +
                   Loc("判定", "ACT") + active + " " + Loc("硬直", "REC") + recov + " " +
                   Loc("行動可", "FREE") + std::to_string(m.ActionableFrame()) + "F";
        };
        fields_.push_back(std::move(f));
    }

    // ---- 硬直差（ここが入力欄。のけぞり時間は自動で決まります）----
    //
    // ヒット時の欄には数値のほかに「D」と入れられます。
    // D はダウン（Down / Knockdown）の意味で、のけぞらせるかわりに
    // 相手を転ばせます。内部では数値とは別の項目（HitOutcome）として
    // 持っているので、数値欄に文字が混ざることはありません。
    {
        Field f;
        f.kind = Field::Kind::Number;
        f.label = Loc("ヒット時硬直差(D=ダウン)", "HIT ADV (D=DOWN)");
        f.getNum = [this] { return moveDraft_.HitAdvantage; };
        f.setNum = [this](double v) { moveDraft_.HitAdvantage = static_cast<int>(v); };
        f.step = 1; f.minV = -40; f.maxV = 40; f.hasRange = true;
        f.decimals = 0; f.unit = "F";
        f.getDown = [this] { return moveDraft_.HitOutcome != Constants::HitNormal; };
        f.setDown = [this](bool down) {
            if (down) {
                if (moveDraft_.HitOutcome == Constants::HitNormal) {
                    moveDraft_.HitOutcome = Constants::HitKnockdown;
                }
                // ダウン時間が未設定なら、標準のダウン時間を入れておきます。
                if (moveDraft_.KnockdownFrames <= 0) moveDraft_.KnockdownFrames = 30;
            } else {
                moveDraft_.HitOutcome = Constants::HitNormal;
            }
            RebuildFields();
        };
        fields_.push_back(std::move(f));
    }
    addNum(Loc("ガード時硬直差", "BLOCK ADV"), [this] { return moveDraft_.BlockAdvantage; },
           [this](double v) { moveDraft_.BlockAdvantage = static_cast<int>(v); }, 1, -40, 40, 0, "F");

    {   // 硬直差から決まるのけぞり時間。手で入力する値ではありません。
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("のけぞり ヒット/ガード", "STUN HIT/BLOCK");
        f.getInfo = [this] {
            return std::to_string(moveDraft_.HitstunFrames()) + "F / " +
                   std::to_string(moveDraft_.BlockstunFrames()) + "F";
        };
        fields_.push_back(std::move(f));
    }

    {   // 硬直差から分かること（コンボ・確定反撃の目安）。
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("つながる発生 / 反撃される発生", "COMBO / PUNISH");
        f.getInfo = [this] {
            std::string combo = moveDraft_.HitAdvantage >= 1
                                    ? (Loc("発生", "") + std::to_string(moveDraft_.HitAdvantage) +
                                       Loc("F以下", "F OR FASTER"))
                                    : Loc("なし", "NONE");
            std::string punish = moveDraft_.BlockAdvantage < 0
                                     ? (Loc("発生", "") + std::to_string(-moveDraft_.BlockAdvantage) +
                                        Loc("F以下", "F OR FASTER"))
                                     : Loc("なし", "NONE");
            return combo + " / " + punish;
        };
        fields_.push_back(std::move(f));
    }

    // 当たったときの効果
    addNum(Loc("ダメージ", "DAMAGE"), [this] { return moveDraft_.Damage; },
           [this](double v) { moveDraft_.Damage = static_cast<int>(v); }, 10, 0, 9990, 0);
    // ヒット後にどうなるか。上の欄に D と入れるとダウン（Knockdown）に
    // なりますが、強制ダウン（長いダウン）などはここで選びます。
    addChoice(Loc("ヒット後", "ON HIT RESULT"),
              {"Normal", "Knockdown", "HardKnockdown"},
              [this] {
                  static const std::vector<std::string> opts =
                      {"Normal", "Knockdown", "HardKnockdown"};
                  return IndexOfOption(opts, moveDraft_.HitOutcome);
              },
              [this](int i) {
                  static const std::vector<std::string> opts =
                      {"Normal", "Knockdown", "HardKnockdown"};
                  moveDraft_.HitOutcome = opts[static_cast<size_t>(i)];
                  if (moveDraft_.HitOutcome != Constants::HitNormal &&
                      moveDraft_.KnockdownFrames <= 0) {
                      moveDraft_.KnockdownFrames = 30;
                  }
                  RebuildFields();
              },
              japanese_ ? std::vector<std::string>{"のけぞり", "ダウン", "強制ダウン"}
                        : std::vector<std::string>{});
    // ダウンさせる技だけ意味のある値（倒れているフレーム数）。
    addNum(Loc("ダウン時間", "KNOCKDOWN"), [this] { return moveDraft_.KnockdownFrames; },
           [this](double v) { moveDraft_.KnockdownFrames = static_cast<int>(v); }, 1, 0, 180, 0, "F");
    // 空中技だけ意味のある値。空中技の硬直は着地してから消化します。
    addNum(Loc("着地硬直", "LANDING REC"), [this] { return moveDraft_.LandingRecovery; },
           [this](double v) { moveDraft_.LandingRecovery = static_cast<int>(v); }, 1, 0, 60, 0, "F");
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
    addNum(Loc("投げ距離", "THROW RANGE"), [this] { return moveDraft_.ThrowRange; },
           [this](double v) { moveDraft_.ThrowRange = v; }, 1, 0, 120, 0, "PX");

    // ---- 技中の空中判定 ----
    // 見た目の高さ（Y 座標）とは別に、「このフレームからは空中扱い」と
    // 決められます。地上投げを避けたり、空中喰らいにしたりできます。
    addChoice(Loc("空中判定", "AIRBORNE"), {"off", "on"},
              [this] { return moveDraft_.AirborneEnabled ? 1 : 0; },
              [this](int i) { moveDraft_.AirborneEnabled = (i == 1); RebuildFields(); },
              japanese_ ? std::vector<std::string>{"なし", "あり"}
                        : std::vector<std::string>{"OFF", "ON"});
    if (moveDraft_.AirborneEnabled) {
        addNum(Loc("  空中開始F", "  AIR FROM"), [this] { return moveDraft_.AirborneStart; },
               [this](double v) { moveDraft_.AirborneStart = static_cast<int>(v); },
               1, 1, 90, 0, "F");
        addChoice(Loc("  空中の続き方", "  AIR MODE"), {"FixedDuration", "UntilLanding"},
                  [this] {
                      return moveDraft_.AirborneKind == AirborneMode::UntilLanding ? 1 : 0;
                  },
                  [this](int i) {
                      moveDraft_.AirborneKind = (i == 1) ? AirborneMode::UntilLanding
                                                         : AirborneMode::FixedDuration;
                      RebuildFields();
                  },
                  japanese_ ? std::vector<std::string>{"指定フレーム", "着地まで"}
                            : std::vector<std::string>{});
        if (moveDraft_.AirborneKind == AirborneMode::FixedDuration) {
            addNum(Loc("  空中の長さ", "  AIR FRAMES"),
                   [this] { return moveDraft_.AirborneDuration; },
                   [this](double v) { moveDraft_.AirborneDuration = static_cast<int>(v); },
                   1, 1, 90, 0, "F");
            Field f;
            f.kind = Field::Kind::Info;
            f.label = Loc("  空中の区間", "  AIRBORNE RANGE");
            f.getInfo = [this] {
                int a = std::max(1, moveDraft_.AirborneStart);
                int b = a + std::max(1, moveDraft_.AirborneDuration) - 1;
                return std::to_string(a) + "-" + std::to_string(b) + "F";
            };
            fields_.push_back(std::move(f));
        }
    }

    // ---- キャンセル（種類ごとに別々の設定）----
    BuildCancelFields();

    // ---- 技の管理（追加 / 複製 / 削除）----
    {
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> ＋ 技を追加", "> + ADD MOVE");
        f.onActivate = [this] { AddMove(); };
        fields_.push_back(std::move(f));
    }
    {
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> 技を複製", "> DUPLICATE MOVE");
        f.onActivate = [this] { DuplicateMove(); };
        fields_.push_back(std::move(f));
    }
    {
        Field f;
        f.kind = Field::Kind::Action;
        f.label = Loc("> 技を削除", "> DELETE MOVE");
        f.onActivate = [this] { RequestDeleteMove(); };
        fields_.push_back(std::move(f));
    }
}

// ---------------------------------------------------------------------
// キャンセル設定（4 種類ぶん）
// ---------------------------------------------------------------------
// 種類ごとに同じ 6 項目（＋派生先）を並べます。種類を増やしたときも
// ここは書き換え不要で、MoveData 側に 1 つ足すだけで並びます。
void Editor::BuildCancelFields() {
    auto addNum = [&](const std::string& label, std::function<double()> get,
                      std::function<void(double)> set, double lo, double hi) {
        Field f;
        f.kind = Field::Kind::Number;
        f.label = label;
        f.getNum = std::move(get);
        f.setNum = std::move(set);
        f.step = 1;
        f.minV = lo; f.maxV = hi; f.hasRange = true;
        f.decimals = 0;
        f.unit = "F";
        fields_.push_back(std::move(f));
    };
    auto addFlag = [&](const std::string& label, std::function<bool()> get,
                       std::function<void(bool)> set) {
        Field f;
        f.kind = Field::Kind::Choice;
        f.label = label;
        f.options = {"off", "on"};
        f.optionLabels = japanese_ ? std::vector<std::string>{"×", "○"}
                                   : std::vector<std::string>{"NO", "YES"};
        f.getIndex = [get] { return get() ? 1 : 0; };
        f.setIndex = [set](int i) { set(i == 1); };
        fields_.push_back(std::move(f));
    };

    const struct { CancelKind kind; const char* jp; const char* en; } kinds[] = {
        {CancelKind::Special, "必殺技キャンセル", "SPECIAL CANCEL"},
        {CancelKind::Super, "超必キャンセル", "SUPER CANCEL"},
        {CancelKind::DriveRush, "ドライブラッシュ", "DRIVE RUSH"},
        {CancelKind::TargetCombo, "ターゲットコンボ", "TARGET COMBO"},
    };

    for (const auto& entry : kinds) {
        CancelKind kind = entry.kind;
        CancelRule& rule = moveDraft_.Cancel(kind);
        {
            Field f;
            f.kind = Field::Kind::Choice;
            f.label = Loc(entry.jp, entry.en);
            f.options = {"off", "on"};
            f.optionLabels = japanese_ ? std::vector<std::string>{"なし", "あり"}
                                       : std::vector<std::string>{"OFF", "ON"};
            f.getIndex = [this, kind] { return moveDraft_.Cancel(kind).Enabled ? 1 : 0; };
            f.setIndex = [this, kind](int i) {
                moveDraft_.Cancel(kind).Enabled = (i == 1);
                RebuildFields(); // 有効にしたら中身の項目を出す
            };
            fields_.push_back(std::move(f));
        }
        if (!rule.Enabled) continue;

        addNum(Loc("  開始F", "  FROM"),
               [this, kind] { return moveDraft_.Cancel(kind).StartFrame; },
               [this, kind](double v) {
                   moveDraft_.Cancel(kind).StartFrame = static_cast<int>(v);
               }, 0, 90);
        addNum(Loc("  終了F", "  TO"),
               [this, kind] { return moveDraft_.Cancel(kind).EndFrame; },
               [this, kind](double v) {
                   moveDraft_.Cancel(kind).EndFrame = static_cast<int>(v);
               }, 0, 90);
        addFlag(Loc("  ヒット時", "  ON HIT"),
                [this, kind] { return moveDraft_.Cancel(kind).OnHit; },
                [this, kind](bool v) { moveDraft_.Cancel(kind).OnHit = v; });
        addFlag(Loc("  ガード時", "  ON BLOCK"),
                [this, kind] { return moveDraft_.Cancel(kind).OnBlock; },
                [this, kind](bool v) { moveDraft_.Cancel(kind).OnBlock = v; });
        addFlag(Loc("  空振り時", "  ON WHIFF"),
                [this, kind] { return moveDraft_.Cancel(kind).OnWhiff; },
                [this, kind](bool v) { moveDraft_.Cancel(kind).OnWhiff = v; });

        // 派生先（この技からつなげてよい技の ID）。
        // 空なら制限なし。ドライブラッシュは技へ移らないので出しません。
        if (kind != CancelKind::DriveRush) {
            Field f;
            f.kind = Field::Kind::Text;
            f.label = Loc("  派生先(空=全部)", "  ALLOWED MOVES");
            f.getText = [this, kind] {
                const auto& allowed = moveDraft_.Cancel(kind).AllowedMoves;
                if (allowed.empty()) return Loc("(すべての技)", "(ANY MOVE)");
                std::string out;
                for (const auto& id : allowed) {
                    if (!out.empty()) out += ",";
                    out += id;
                }
                return out;
            };
            f.setText = [this, kind](const std::string& v) {
                // 「,」区切りで技 ID を並べます。空にすれば制限なし。
                std::vector<std::string> ids;
                std::string cur;
                for (char c : v) {
                    if (c == ',' || c == ' ') {
                        if (!cur.empty()) { ids.push_back(cur); cur.clear(); }
                    } else {
                        cur += c;
                    }
                }
                if (!cur.empty()) ids.push_back(cur);
                moveDraft_.Cancel(kind).AllowedMoves = ids;
            };
            fields_.push_back(std::move(f));
        }
    }
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
              {"HITBOX", "HURT MOVE", "HURT STAND", "HURT CROUCH", "HURT AIR",
               "PUSH STAND", "PUSH CROUCH", "PUSH AIR"},
              [this] { return static_cast<int>(boxTarget_); },
              [this](int i) {
                  boxTarget_ = static_cast<BoxTarget>(i);
                  boxIndex_ = 0;
                  RebuildFields();
              },
              japanese_ ? std::vector<std::string>{"攻撃判定", "食らい この技",
                                                   "食らい 立ち",
                                                   "食らい しゃがみ", "食らい 空中",
                                                   "押し合い 立ち", "押し合い しゃがみ",
                                                   "押し合い 空中"}
                        : std::vector<std::string>{});

    if (boxTarget_ == BoxTarget::Hitbox || IsMoveHurtTarget()) {
        // どの技の判定かが分かるように、技名も出します
        Field f;
        f.kind = Field::Kind::Info;
        f.label = Loc("技", "MOVE");
        f.getInfo = [this] { return moveIds_.empty() ? std::string("-")
                                                     : moveIds_[static_cast<size_t>(moveIndex_)]; };
        fields_.push_back(std::move(f));
    }

    // ---- 技ごとの食らい判定の「使う / 使わない」----
    // 技を出している間だけ、体の形が変わります（脚を伸ばす等）。
    // 「使わない」にすると、判定の中身を残したまま姿勢どおりの
    // 標準の形に戻ります。試しに切って比べる、という使い方ができます。
    if (IsMoveHurtTarget()) {
        // ラベルと値はどちらも 1 行に収まる短さにします（ラベルは左寄せ、
        // 値は右寄せで描くので、長いと画面の真ん中で重なって読めません）。
        addChoice(Loc("この技の判定", "MOVE HURTBOX"), {"off", "on"},
                  [this] { return moveDraft_.HurtboxOverrideEnabled ? 1 : 0; },
                  [this](int i) {
                      moveDraft_.HurtboxOverrideEnabled = (i == 1);
                      RebuildFields();
                  },
                  japanese_ ? std::vector<std::string>{"使わない", "使う"}
                            : std::vector<std::string>{"OFF", "ON"});
        {   // 「今このフレームで実際に使われるのはどちらか」を出します。
            // 使わない設定のときに何が起きているのかが、ここで分かります。
            Field f;
            f.kind = Field::Kind::Info;
            f.label = Loc("使用中", "IN USE");
            f.getInfo = [this] {
                if (moveDraft_.HasHurtboxOverride()) return Loc("この技", "MOVE");
                return Loc("姿勢:" + StanceLabelJp(moveDraft_.Stance),
                           "STANCE " + moveDraft_.Stance);
            };
            fields_.push_back(std::move(f));
        }
        {   // 姿勢の判定を写してくる（一から作るより早い）。
            Field f;
            f.kind = Field::Kind::Action;
            f.label = Loc("> 姿勢の判定をコピー", "> COPY FROM STANCE");
            f.onActivate = [this] { CopyStanceHurtboxesToMove(); };
            fields_.push_back(std::move(f));
        }
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
    if (boxTarget_ == BoxTarget::Hitbox) return static_cast<int>(moveDraft_.Hitboxes.size());
    // 押し合い判定は姿勢ごとに 1 個だけです（増やす意味がないので）。
    if (IsPushboxTarget()) return 1;
    // 残りは食らい判定（技ごと / 姿勢ごと）。どちらも部位の一覧です。
    const auto* parts = CurrentHurtParts();
    return parts ? static_cast<int>(parts->size()) : 0;
}

// 押し合い判定を編集しているか（追加・削除ができない対象）。
bool Editor::IsPushboxTarget() const {
    return boxTarget_ == BoxTarget::PushStand || boxTarget_ == BoxTarget::PushCrouch ||
           boxTarget_ == BoxTarget::PushAir;
}

// 技ごとの食らい判定を編集しているか（保存先が技のファイルになる対象）。
bool Editor::IsMoveHurtTarget() const { return boxTarget_ == BoxTarget::HurtMove; }

// プレビューで、どの姿勢のキャラクターを立たせるか。
// 技の判定（攻撃・技ごとの食らい）を見ているときは、その技の姿勢です。
std::string Editor::CurrentStanceName() const {
    switch (boxTarget_) {
        case BoxTarget::Hitbox:
        case BoxTarget::HurtMove: return moveDraft_.Stance;
        case BoxTarget::HurtCrouch:
        case BoxTarget::PushCrouch: return "crouch";
        case BoxTarget::HurtAir:
        case BoxTarget::PushAir: return "air";
        default: return "stand";
    }
}

// 今編集している押し合い判定（対象でなければ nullptr）。
RectBox* Editor::CurrentPushbox() {
    if (!IsPushboxTarget()) return nullptr;
    return &statsDraft_.Pushboxes.ForStance(CurrentStanceName());
}
const RectBox* Editor::CurrentPushbox() const {
    if (!IsPushboxTarget()) return nullptr;
    return &statsDraft_.Pushboxes.ForStance(CurrentStanceName());
}

// 今の対象の「部位一覧」を取り出す（攻撃判定・押し合い判定なら nullptr）。
//
// 技ごとの食らい判定だけは、書き換える先が技の下書き（moveDraft_）です。
// 姿勢ごとの食らい判定はキャラクターの下書き（statsDraft_）になります。
// 保存されるファイルが違うので、ここで行き先をはっきり分けています。
//   技ごと → data/moves/<キャラ>/<技>.json（の "hurtboxes"）
//   姿勢   → data/characters/<キャラ>.json（の "hurtboxes"）
//
// const 版と非 const 版の 2 つあるのは C++ の作法です（読むだけなら上、
// 書き換えるなら下）。中身が同じなので、片方はもう片方を呼んでいます。
const std::vector<HurtboxPart>* Editor::CurrentHurtParts() const {
    switch (boxTarget_) {
        case BoxTarget::HurtMove: return &moveDraft_.Hurtboxes;
        case BoxTarget::HurtStand: return &statsDraft_.Hurtboxes.Stand;
        case BoxTarget::HurtCrouch: return &statsDraft_.Hurtboxes.Crouch;
        case BoxTarget::HurtAir: return &statsDraft_.Hurtboxes.Air;
        default: return nullptr;
    }
}
std::vector<HurtboxPart>* Editor::CurrentHurtParts() {
    // const 版の結果から const を外します。元は非 const のメンバなので安全です。
    const Editor* self = this;
    return const_cast<std::vector<HurtboxPart>*>(self->CurrentHurtParts());
}

// 姿勢ごとの食らい判定を、技ごとの食らい判定へ丸ごと写す。
void Editor::CopyStanceHurtboxesToMove() {
    if (!IsMoveHurtTarget()) return;
    // 写す元は、その技の姿勢（立ち技なら立ちの判定）です。
    moveDraft_.Hurtboxes = statsDraft_.Hurtboxes.PartsForStance(moveDraft_.Stance);
    moveDraft_.HurtboxOverrideEnabled = true; // 写したらそのまま使う
    boxIndex_ = 0;
    dirty_ = true;
    SetMessage("姿勢の判定をコピーしました", "COPIED FROM STANCE");
    RebuildFields();
}

bool Editor::GetBoxRect(int index, RectBox& out) const {
    if (index < 0 || index >= CurrentBoxCount()) return false;
    size_t i = static_cast<size_t>(index);
    if (const RectBox* push = CurrentPushbox(); push != nullptr) {
        out = *push;
        return true;
    }
    if (boxTarget_ == BoxTarget::Hitbox) {
        out = ToRect(moveDraft_.Hitboxes[i]);
        return true;
    }
    const auto* parts = CurrentHurtParts();
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

    if (RectBox* push = CurrentPushbox(); push != nullptr) {
        switch (which) {
            case 0: push->CenterX = value; break;
            case 1: push->CenterY = value; break;
            case 2: push->Width = value; break;
            default: push->Height = value; break;
        }
        return;
    }
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
    auto* parts = CurrentHurtParts();
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
    if (IsPushboxTarget()) {
        SetMessage("押し合い判定は姿勢ごとに 1 個です", "ONE PUSHBOX PER STANCE");
        return;
    }
    if (boxTarget_ == BoxTarget::Hitbox) {
        // 新しい判定は、前方の胸の高さに小さめで作ります
        //（そこから調整するのが一番やりやすい位置）。
        HitboxDef d;
        d.offsetX = 28; d.offsetY = -60; d.width = 18; d.height = 12;
        moveDraft_.Hitboxes.push_back(d);
        boxIndex_ = static_cast<int>(moveDraft_.Hitboxes.size()) - 1;
    } else {
        auto* parts = CurrentHurtParts();
        if (!parts) return;
        HurtboxPart part;
        part.Name = "part" + std::to_string(parts->size() + 1);
        part.Box = RectBox(0, -30, 24, 24);
        parts->push_back(part);
        boxIndex_ = static_cast<int>(parts->size()) - 1;
        // 技ごとの食らい判定に足したときは、自動で「使う」にします。
        // 足したのに何も変わらない（＝使う設定を別に入れる必要がある）
        // のは、まず間違いなく意図と違うためです。
        if (IsMoveHurtTarget()) moveDraft_.HurtboxOverrideEnabled = true;
    }
    dirty_ = true;
    SetMessage("判定を追加しました", "BOX ADDED");
    RebuildFields();
}

void Editor::DeleteBox() {
    if (IsPushboxTarget()) {
        SetMessage("押し合い判定は消せません", "PUSHBOX CANNOT BE DELETED");
        return;
    }
    int count = CurrentBoxCount();
    if (count == 0) return;
    int idx = std::clamp(boxIndex_, 0, count - 1);

    if (boxTarget_ == BoxTarget::Hitbox) {
        moveDraft_.Hitboxes.erase(moveDraft_.Hitboxes.begin() + idx);
    } else {
        auto* parts = CurrentHurtParts();
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
    // 今の下書きも、預かってあるぶんも、まとめて書き出します。
    // これが無いと「技を切り替えながら 3 つ直して、最後に S」で
    // 最後の 1 つしか保存されません。
    RememberDrafts(false);

    for (const auto& kv : pendingChars_) dm_->SaveCharacter(kv.second);
    int moveCount = 0;
    for (const auto& kv : pendingMoves_) {
        // 鍵は "キャラクターID/技ID"。前半を取り出して保存先を決めます。
        std::string::size_type slash = kv.first.find('/');
        if (slash == std::string::npos) continue;
        dm_->SaveMove(kv.first.substr(0, slash), kv.second);
        ++moveCount;
    }
    int charCount = static_cast<int>(pendingChars_.size());
    pendingChars_.clear();
    pendingMoves_.clear();
    dirty_ = false;
    SetMessage("保存しました（キャラ " + std::to_string(charCount) + " / 技 " +
                   std::to_string(moveCount) + "）",
               "SAVED " + std::to_string(charCount) + " CHAR / " +
                   std::to_string(moveCount) + " MOVES");
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

// =====================================================================
// 技の追加・複製・削除
// =====================================================================
// 保存の仕組みは今までどおりです（DataManager がユーザーフォルダの
// JSON に書きます）。ここでは「下書きを作って保存を呼ぶ」だけなので、
// 追加した技もゲームを閉じて開き直せばそのまま残ります。

// 使われていない技 ID を作る。
// 見た目の名前（Name）と内部の ID は別物です。名前は日本語でも
// かまいませんが、ID はファイル名になるので英数字にします。
std::string Editor::MakeUniqueMoveId(const std::string& base, bool alwaysNumber) const {
    auto taken = [this](const std::string& id) {
        return std::find(moveIds_.begin(), moveIds_.end(), id) != moveIds_.end();
    };
    if (!alwaysNumber && !base.empty() && !taken(base)) return base;
    for (int n = 1; n < 1000; ++n) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s_%03d", base.empty() ? "move" : base.c_str(), n);
        if (!taken(buf)) return buf;
    }
    return base + "_x";
}

void Editor::SelectMoveById(const std::string& moveId) {
    for (size_t i = 0; i < moveIds_.size(); ++i) {
        if (moveIds_[i] == moveId) { LoadMove(static_cast<int>(i)); break; }
    }
    RebuildFields();
}

void Editor::AddMove() {
    if (!dm_) return;
    // 安全な初期値の技を作ります。フレームは 1/1/1、硬直差 0、
    // キャンセルも空中判定も無し。ここから調整していきます。
    MoveData m;
    m.Id = MakeUniqueMoveId("move", true); // move_001, move_002, ...
    m.Name = Loc("新しい技", "New Move");
    m.Startup = 1;
    m.Active = 1;
    m.Recovery = 1;
    m.TotalFrame = m.TotalFrames();
    m.Button = "LP";
    m.Stance = "stand";
    m.Tags = {Constants::TagNormal};
    m.Hitboxes.push_back({28, -60, 18, 12});

    dm_->SaveMove(statsDraft_.Id, m);
    // キャラクター側の技一覧にも足しておきます（表示用の一覧）。
    if (std::find(statsDraft_.MoveIds.begin(), statsDraft_.MoveIds.end(), m.Id) ==
        statsDraft_.MoveIds.end()) {
        statsDraft_.MoveIds.push_back(m.Id);
        dm_->SaveCharacter(statsDraft_);
    }

    moveIds_.push_back(m.Id);
    std::sort(moveIds_.begin(), moveIds_.end());
    SelectMoveById(m.Id);
    dirty_ = false;
    SetMessage(m.Id + " を追加しました", "ADDED " + m.Id);

    // 名前をすぐ変えられるように、名前の行へ移して入力を始めます。
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].kind == Field::Kind::Text && fields_[i].setText) {
            selected_ = static_cast<int>(i);
            ActivateSelected();
            break;
        }
    }
}

void Editor::DuplicateMove() {
    if (!dm_ || moveIds_.empty()) return;
    // 中身はそのまま、ID と名前だけ新しくします。
    MoveData copy = moveDraft_;
    copy.Id = MakeUniqueMoveId(moveDraft_.Id + "_copy");
    copy.Name = moveDraft_.Name + Loc(" のコピー", " Copy");

    dm_->SaveMove(statsDraft_.Id, copy);
    if (std::find(statsDraft_.MoveIds.begin(), statsDraft_.MoveIds.end(), copy.Id) ==
        statsDraft_.MoveIds.end()) {
        statsDraft_.MoveIds.push_back(copy.Id);
        dm_->SaveCharacter(statsDraft_);
    }
    moveIds_.push_back(copy.Id);
    std::sort(moveIds_.begin(), moveIds_.end());
    SelectMoveById(copy.Id);
    dirty_ = false;
    SetMessage(copy.Id + " に複製しました", "DUPLICATED TO " + copy.Id);
}

// この技を「派生先」に指定している技を探す。
// 消したあとに、どこからも行けない技 ID が残らないようにするためです。
std::vector<std::string> Editor::MovesReferencing(const std::string& moveId) const {
    std::vector<std::string> found;
    if (!dm_) return found;
    const auto* moveset = dm_->GetMoveset(statsDraft_.Id);
    if (!moveset) return found;
    for (const auto& kv : *moveset) {
        if (kv.first == moveId) continue;
        for (int i = 0; i < CancelKindCount; ++i) {
            const auto& allowed = kv.second.Cancels[i].AllowedMoves;
            if (std::find(allowed.begin(), allowed.end(), moveId) != allowed.end()) {
                found.push_back(kv.first);
                break;
            }
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

void Editor::RequestDeleteMove() {
    if (moveIds_.empty()) return;
    std::string id = moveIds_[static_cast<size_t>(moveIndex_)];
    std::vector<std::string> refs = MovesReferencing(id);

    std::string detail;
    if (!refs.empty()) {
        detail = Loc("この技は他の技から参照されています: ",
                     "REFERENCED BY: ");
        for (size_t i = 0; i < refs.size() && i < 4; ++i) {
            if (i > 0) detail += ",";
            detail += refs[i];
        }
        if (refs.size() > 4) detail += "...";
    }
    OpenConfirm(moveDraft_.Name + Loc(" を削除しますか?", " - DELETE?"), detail,
                [this] { DeleteCurrentMove(); });
}

void Editor::DeleteCurrentMove() {
    if (!dm_ || moveIds_.empty()) return;
    std::string id = moveIds_[static_cast<size_t>(moveIndex_)];

    // 参照している技から、この ID を取り除きます
    //（消えた技を指したままの派生先が残らないように）。
    if (const auto* moveset = dm_->GetMoveset(statsDraft_.Id)) {
        std::vector<MoveData> fixed;
        for (const auto& kv : *moveset) {
            if (kv.first == id) continue;
            MoveData m = kv.second;
            bool changed = false;
            for (int i = 0; i < CancelKindCount; ++i) {
                auto& allowed = m.Cancels[i].AllowedMoves;
                auto it = std::find(allowed.begin(), allowed.end(), id);
                if (it != allowed.end()) { allowed.erase(it); changed = true; }
            }
            if (changed) fixed.push_back(m);
        }
        for (const auto& m : fixed) dm_->SaveMove(statsDraft_.Id, m);
    }

    dm_->DeleteMove(statsDraft_.Id, id);
    // 預かり所にも残っていたら消します。残したままだと、次に保存した
    // ときに「消したはずの技」が書き戻されてしまいます。
    pendingMoves_.erase(MoveKey(statsDraft_.Id, id));
    auto mit = std::find(statsDraft_.MoveIds.begin(), statsDraft_.MoveIds.end(), id);
    if (mit != statsDraft_.MoveIds.end()) {
        statsDraft_.MoveIds.erase(mit);
        dm_->SaveCharacter(statsDraft_);
    }

    auto it = std::find(moveIds_.begin(), moveIds_.end(), id);
    if (it != moveIds_.end()) moveIds_.erase(it);
    moveIndex_ = std::max(0, moveIndex_ - 1);
    LoadMove(moveIndex_);
    dirty_ = false;
    RebuildFields();
    SetMessage(id + " を削除しました", "DELETED " + id);
}

// =====================================================================
// 確認ダイアログ
// =====================================================================
// 取り返しのつかない操作（技の削除）の前に必ず出します。
void Editor::OpenConfirm(const std::string& title, const std::string& detail,
                         std::function<void()> action) {
    confirmActive_ = true;
    confirmTitle_ = title;
    confirmDetail_ = detail;
    confirmChoice_ = 0; // 既定は「キャンセル」。誤って消さないように。
    confirmAction_ = std::move(action);
}

bool Editor::HandleConfirmKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_TAB:
            confirmChoice_ = confirmChoice_ == 0 ? 1 : 0;
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            bool run = (confirmChoice_ == 1);
            auto action = confirmAction_;
            confirmActive_ = false;
            confirmAction_ = nullptr;
            if (run && action) action();
            return true;
        }
        case SDLK_ESCAPE:
            confirmActive_ = false;
            confirmAction_ = nullptr;
            return true;
        default:
            return true; // ダイアログ中は他のキーを通さない
    }
}

void Editor::SetMessage(const std::string& jp, const std::string& en) {
    message_ = Loc(jp, en);
    messageTimer_ = 2.5;
}

// =====================================================================
// マウスで判定を編集する
// =====================================================================
// 数値を ← → で 1 ずつ動かすのは正確ですが、「だいたいこの辺」を
// 決めるには向きません。逆にマウスは大まかに置くのが得意です。
// 両方から同じデータ（moveDraft_ / statsDraft_）を書き換えるので、
// どちらで動かしても数値欄と画面の四角形は必ず一致します。

// マウス位置（内部キャンバスの座標）を、判定の座標へ直す。
// 判定の座標はキャラクターの中心（X）と足元（Y）からの相対で、
// プレビューは等倍で描いているので、原点を引くだけで戻せます。
bool Editor::PreviewToBox(double vx, double vy, double& bx, double& by) const {
    if (previewW_ <= 0 || previewH_ <= 0) return false;
    if (vx < previewX_ || vx > previewX_ + previewW_) return false;
    if (vy < previewY_ || vy > previewY_ + previewH_) return false;
    bx = vx - previewFootX_;
    by = vy - previewFootY_;
    return true;
}

// 目盛りに合わせる。OFF なら 1px 単位（整数）に丸めるだけです。
// 判定の座標を小数のままにすると、描画とゲーム内の判定で
// 丸め方が変わってずれるので、必ず整数にします。
double Editor::SnapValue(double v) const {
    if (gridSnap_ && gridSize_ > 1) {
        return std::round(v / gridSize_) * gridSize_;
    }
    return std::round(v);
}

void Editor::SetBoxRect(int index, const RectBox& box) {
    int count = CurrentBoxCount();
    if (index < 0 || index >= count) return;
    size_t i = static_cast<size_t>(index);
    if (RectBox* push = CurrentPushbox(); push != nullptr) {
        push->CenterX = std::round(box.CenterX);
        push->CenterY = std::round(box.CenterY);
        push->Width = std::max(1.0, std::round(box.Width));
        push->Height = std::max(1.0, std::round(box.Height));
        return;
    }
    // 幅・高さは 1px 未満にしない（潰れた判定は当たらないので）。
    double w = std::max(1.0, std::round(box.Width));
    double h = std::max(1.0, std::round(box.Height));
    double cx = std::round(box.CenterX);
    double cy = std::round(box.CenterY);
    if (boxTarget_ == BoxTarget::Hitbox) {
        HitboxDef& d = moveDraft_.Hitboxes[i];
        d.offsetX = cx; d.offsetY = cy; d.width = w; d.height = h;
        return;
    }
    auto* parts = CurrentHurtParts();
    if (!parts) return;
    (*parts)[i].Box = RectBox(cx, cy, w, h);
}

// その位置にある判定を探す。重なっているときは、
// 手前（あとから描かれる＝番号の大きいほう）を優先します。
int Editor::BoxAtPoint(double vx, double vy) const {
    double bx = 0, by = 0;
    if (!PreviewToBox(vx, vy, bx, by)) return -1;
    for (int i = CurrentBoxCount() - 1; i >= 0; --i) {
        RectBox b;
        if (!GetBoxRect(i, b)) continue;
        if (bx >= b.Left() && bx <= b.Right() && by >= b.Top() && by <= b.Bottom()) return i;
    }
    return -1;
}

// 四角形のどこをつかんだか（角・辺・内側）。
Editor::DragMode Editor::HandleAtPoint(const RectBox& box, double bx, double by) const {
    const double grip = 3.0; // つかめる幅（px）
    bool nearLeft = std::abs(bx - box.Left()) <= grip;
    bool nearRight = std::abs(bx - box.Right()) <= grip;
    bool nearTop = std::abs(by - box.Top()) <= grip;
    bool nearBottom = std::abs(by - box.Bottom()) <= grip;
    bool insideX = bx >= box.Left() - grip && bx <= box.Right() + grip;
    bool insideY = by >= box.Top() - grip && by <= box.Bottom() + grip;
    if (!insideX || !insideY) return DragMode::None;

    if (nearLeft && nearTop) return DragMode::TopLeft;
    if (nearRight && nearTop) return DragMode::TopRight;
    if (nearLeft && nearBottom) return DragMode::BottomLeft;
    if (nearRight && nearBottom) return DragMode::BottomRight;
    if (nearLeft) return DragMode::Left;
    if (nearRight) return DragMode::Right;
    if (nearTop) return DragMode::Top;
    if (nearBottom) return DragMode::Bottom;
    // 内側なら丸ごと移動
    if (bx >= box.Left() && bx <= box.Right() && by >= box.Top() && by <= box.Bottom()) {
        return DragMode::Move;
    }
    return DragMode::None;
}

void Editor::HandleMouseDown(double vx, double vy) {
    mouseVx_ = vx; mouseVy_ = vy;
    if (confirmActive_ || tab_ != Tab::Boxes) return;
    double bx = 0, by = 0;
    if (!PreviewToBox(vx, vy, bx, by)) return;

    // まず、今選んでいる判定の「つかめる場所」を調べます。
    // 選択中を優先するので、重なっていても狙ったものを掴めます。
    RectBox current;
    DragMode mode = DragMode::None;
    if (GetBoxRect(boxIndex_, current)) mode = HandleAtPoint(current, bx, by);

    if (mode == DragMode::None) {
        // 掴めなければ、その場所にある判定を選び直します。
        int hit = BoxAtPoint(vx, vy);
        if (hit < 0) return;
        boxIndex_ = hit;
        RebuildFields();
        if (!GetBoxRect(boxIndex_, current)) return;
        mode = DragMode::Move;
    }

    dragMode_ = mode;
    dragStartX_ = bx;
    dragStartY_ = by;
    dragStartBox_ = current;
}

void Editor::HandleMouseMove(double vx, double vy) {
    mouseVx_ = vx; mouseVy_ = vy;
    if (dragMode_ == DragMode::None) return;
    double bx = 0, by = 0;
    // 枠の外へ出ても掴んだままにしたいので、範囲の判定はしません。
    bx = vx - previewFootX_;
    by = vy - previewFootY_;

    double dx = bx - dragStartX_;
    double dy = by - dragStartY_;

    // つかんだ瞬間の四角形を基準に、辺の位置を動かします。
    double left = dragStartBox_.Left();
    double right = dragStartBox_.Right();
    double top = dragStartBox_.Top();
    double bottom = dragStartBox_.Bottom();

    switch (dragMode_) {
        case DragMode::Move: {
            double cx = SnapValue(dragStartBox_.CenterX + dx);
            double cy = SnapValue(dragStartBox_.CenterY + dy);
            SetBoxRect(boxIndex_, RectBox(cx, cy, dragStartBox_.Width, dragStartBox_.Height));
            dirty_ = true;
            return;
        }
        case DragMode::Left: left = SnapValue(left + dx); break;
        case DragMode::Right: right = SnapValue(right + dx); break;
        case DragMode::Top: top = SnapValue(top + dy); break;
        case DragMode::Bottom: bottom = SnapValue(bottom + dy); break;
        case DragMode::TopLeft: left = SnapValue(left + dx); top = SnapValue(top + dy); break;
        case DragMode::TopRight: right = SnapValue(right + dx); top = SnapValue(top + dy); break;
        case DragMode::BottomLeft:
            left = SnapValue(left + dx); bottom = SnapValue(bottom + dy); break;
        case DragMode::BottomRight:
            right = SnapValue(right + dx); bottom = SnapValue(bottom + dy); break;
        default: return;
    }
    // 左右・上下が入れ替わってしまったら、つぶれないように直します。
    if (right < left + 1) right = left + 1;
    if (bottom < top + 1) bottom = top + 1;
    SetBoxRect(boxIndex_, RectBox((left + right) / 2.0, (top + bottom) / 2.0,
                                  right - left, bottom - top));
    dirty_ = true;
}

void Editor::HandleMouseUp() { dragMode_ = DragMode::None; }

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
        // 「D（ダウン）」を入れられる欄は、数値の上限のさらに右側に
        // D があるものとして扱います。→ を押していくと最大値の次が D、
        // D から ← を押すと最大値に戻ります。
        if (f.getDown && f.setDown) {
            if (f.getDown()) {
                if (direction < 0) { f.setDown(false); f.setNum(f.maxV); dirty_ = true; }
                return; // D のまま右へは動かさない
            }
            if (direction > 0 && f.hasRange && f.getNum() >= f.maxV) {
                f.setDown(true);
                dirty_ = true;
                return;
            }
        }
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
    } else if (f.kind == Field::Kind::Number && f.getDown && f.setDown) {
        // 数値でも D でも入力できる欄。キーボードから直接打てます。
        editingText_ = true;
        editingField_ = selected_;
        textBuffer_ = f.getDown() ? std::string("D")
                                  : FormatNumber(f.getNum(), f.decimals);
    } else if (f.kind == Field::Kind::Text && f.getText) {
        // 文字入力の開始。今の値を編集用の文字列に写します。
        editingText_ = true;
        editingField_ = selected_;
        textBuffer_ = f.getText();
        if (textBuffer_ == "(NONE)" || textBuffer_ == "(なし)") textBuffer_.clear();
    }
}

bool Editor::HandleKey(SDL_Keycode key, bool shift) {
    // ---- 確認ダイアログが出ている間は、そちらが入力を全部受け取る ----
    if (confirmActive_) return HandleConfirmKey(key);

    // ---- 文字入力中の処理を先に ----
    if (editingText_) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (editingField_ >= 0 && editingField_ < static_cast<int>(fields_.size())) {
                Field& f = fields_[static_cast<size_t>(editingField_)];
                if (f.kind == Field::Kind::Number && f.setDown && f.setNum) {
                    // "D" または "d" ならダウン技。それ以外は数値として読みます。
                    std::string t;
                    for (char c : textBuffer_) { if (c != ' ') t += c; }
                    if (t == "D" || t == "d") {
                        f.setDown(true);
                    } else if (!t.empty()) {
                        f.setDown(false);
                        double v = std::atof(t.c_str());
                        if (f.hasRange) v = std::clamp(v, f.minV, f.maxV);
                        f.setNum(v);
                    }
                    dirty_ = true;
                } else if (f.setText) {
                    f.setText(textBuffer_);
                    dirty_ = true;
                }
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
        case SDLK_g:
            // 判定タブでの目盛り吸着（Snap to Grid）の ON / OFF。
            gridSnap_ = !gridSnap_;
            SetMessage(gridSnap_ ? "目盛りに吸着: ON" : "目盛りに吸着: OFF",
                       gridSnap_ ? "SNAP TO GRID: ON" : "SNAP TO GRID: OFF");
            return true;
        case SDLK_LEFTBRACKET:
            if (gridSize_ > 1) gridSize_ /= 2;
            SetMessage("目盛り " + std::to_string(gridSize_) + "PX",
                       "GRID " + std::to_string(gridSize_) + "PX");
            return true;
        case SDLK_RIGHTBRACKET:
            if (gridSize_ < 16) gridSize_ *= 2;
            SetMessage("目盛り " + std::to_string(gridSize_) + "PX",
                       "GRID " + std::to_string(gridSize_) + "PX");
            return true;
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
    DrawConfirm(r); // 出ていれば、いちばん手前に重ねる

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
                if (editingText_ && index == editingField_) {
                    value = textBuffer_ + "_"; // 入力中はカーソルを出す
                    valueColor = pal.Accent;
                } else if (f.getDown && f.getDown()) {
                    // ダウン技の欄は、数値のかわりに D と出します。
                    value = "D";
                } else if (f.getNum) {
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

    // マウス操作のために、原点と表示範囲を覚えておきます。
    // 描くときと同じ値を使うので、見えている四角形とつかめる場所が
    // ずれることがありません。
    previewFootX_ = footX;
    previewFootY_ = footY;
    previewX_ = x; previewY_ = y; previewW_ = w; previewH_ = h;

    // 地面の線
    r.FillRect(x + 2, footY, w - 4, 1, pal.Ink45);

    HumanoidPose pose;
    pose.facing = 1;
    // どの姿勢で立たせるか。技の判定（攻撃・技ごとの食らい）を見るときは、
    // その技の姿勢です（CurrentStanceName がそう返します）。
    std::string stance = CurrentStanceName();
    if (boxTarget_ == BoxTarget::Hitbox || IsMoveHurtTarget()) {
        // 技を出している最中の見た目にします。腕（または脚）を伸ばした
        // 絵になるので、伸ばした先に判定が付いているか確かめられます。
        if (moveDraft_.Button.size() >= 2 && moveDraft_.Button.back() == 'K') pose.legKick = 34;
        else pose.armReach = 34;
    }
    if (stance == "crouch") pose.crouch = true;
    else if (stance == "air") pose.jump = true;

    DrawHumanoid(r, footX, footY,
                 Color(statsDraft_.ColorR, statsDraft_.ColorG, statsDraft_.ColorB), pose);

    // 判定の枠を重ねて描く。色はゲーム内のデバッグ表示（F1）と同じです。
    //   赤 … 攻撃判定（ヒットボックス）
    //   緑 … 食らい判定（ハートボックス）
    //   青 … 押し合い判定（プッシュボックス）
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
        // 参考として、その技のときに実際に使われる食らい判定を薄く出します
        //（攻撃判定が体のどこから出ているかが分かるように）。
        const std::vector<HurtboxPart>& ref =
            moveDraft_.HasHurtboxOverride() ? moveDraft_.Hurtboxes
                                            : statsDraft_.Hurtboxes.PartsForStance(moveDraft_.Stance);
        for (const auto& p : ref) drawBox(p.Box, Color(40, 170, 80), false);
    } else if (IsMoveHurtTarget()) {
        // 技ごとの食らい判定を編集するときは、比べる相手として
        //   ・姿勢ごとの標準の食らい判定（薄い緑）
        //   ・その技の攻撃判定（薄い赤）
        // を薄く出します。「どれだけ伸ばしたか」「攻撃判定より
        // 食らい判定が前に出ていないか」がその場で分かります。
        for (const auto& p : statsDraft_.Hurtboxes.PartsForStance(moveDraft_.Stance)) {
            drawBox(p.Box, Color(40, 170, 80), false);
        }
        for (const auto& hb : moveDraft_.Hitboxes) {
            drawBox(RectBox(hb.offsetX, hb.offsetY, hb.width, hb.height),
                    Color(220, 50, 40), false);
        }
    } else if (IsPushboxTarget()) {
        // 押し合い判定を編集するときは、同じ姿勢の食らい判定を
        // 薄く出します（体の幅と見比べられるように）。
        for (const auto& p : statsDraft_.Hurtboxes.PartsForStance(CurrentStanceName())) {
            drawBox(p.Box, Color(40, 170, 80), false);
        }
    }
    // 編集中の一式。攻撃判定は赤、食らい判定は緑、押し合いは青。
    // 選んでいる箱だけ濃く描いて、どれを編集中か分かるようにします。
    Color boxColor = (boxTarget_ == BoxTarget::Hitbox) ? Color(220, 50, 40)
                   : IsPushboxTarget() ? Color(60, 110, 220)
                                       : Color(40, 170, 80);
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

    // 選んでいる判定に、つまめる印（ハンドル）を出します。
    // 四隅は幅と高さの両方、辺はどちらか一方が変わります。
    RectBox sel;
    if (GetBoxRect(boxIndex_, sel)) {
        float l = footX + static_cast<float>(sel.Left());
        float rr = footX + static_cast<float>(sel.Right());
        float t = footY + static_cast<float>(sel.Top());
        float b = footY + static_cast<float>(sel.Bottom());
        float mx = (l + rr) / 2.0f, my = (t + b) / 2.0f;
        const float hs = 3.0f; // 印の大きさ
        const float pts[8][2] = {{l, t}, {mx, t}, {rr, t}, {rr, my},
                                 {rr, b}, {mx, b}, {l, b}, {l, my}};
        for (const auto& p : pts) {
            r.FillRect(p[0] - hs / 2, p[1] - hs / 2, hs, hs, pal.Ink);
            r.DrawRect(p[0] - hs / 2, p[1] - hs / 2, hs, hs, pal.White, 1.0f);
        }
    }

    // 目盛り吸着の状態を隅に出します（今どちらで動くのか分かるように）。
    std::string grid = gridSnap_ ? (Loc("吸着 ", "SNAP ") + std::to_string(gridSize_) + "PX")
                                 : Loc("吸着なし(1PX)", "FREE (1PX)");
    DrawPixelText(r, grid, x + 3, y + h - 9, 1.0f, pal.Ink45);
}

// 確認ダイアログ（技の削除など）。画面の中央に出します。
void Editor::DrawConfirm(Renderer& r) {
    if (!confirmActive_) return;
    const auto& pal = GetPalette();
    float w = std::min(static_cast<float>(VirtualW) - 20.0f, 300.0f);
    float lh = LineH();
    float h = lh * 4.0f + 16.0f;
    float x = (VirtualW - w) / 2.0f;
    float y = (VirtualH - h) / 2.0f;

    // 後ろを暗くして、ダイアログ以外を触れないことを伝えます。
    r.FillRect(0, 0, static_cast<float>(VirtualW), static_cast<float>(VirtualH),
               Color(0, 0, 0, 120));
    r.FillRect(x, y, w, h, pal.PanelBg);
    r.DrawRect(x, y, w, h, pal.Ink, 1.0f);

    DrawPixelText(r, confirmTitle_, x + 6, y + 5, 1.0f, pal.Ink);
    if (!confirmDetail_.empty()) {
        DrawPixelText(r, confirmDetail_, x + 6, y + 5 + lh, 1.0f, pal.AccentDeep);
    }

    const std::string labels[2] = {Loc("キャンセル", "CANCEL"), Loc("削除", "DELETE")};
    float by = y + h - lh - 5;
    float bx = x + 6;
    for (int i = 0; i < 2; ++i) {
        float bw = PixelTextWidth(labels[i], 1.0f) + 10;
        bool active = (confirmChoice_ == i);
        if (active) {
            r.FillRect(bx, by - 1, bw, lh + 2, pal.Ink);
            DrawPixelText(r, labels[i], bx + 5, by, 1.0f, pal.White);
        } else {
            r.DrawRect(bx, by - 1, bw, lh + 2, pal.RuleSoft, 1.0f);
            DrawPixelText(r, labels[i], bx + 5, by, 1.0f, pal.Ink55);
        }
        bx += bw + 6;
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

    if (confirmActive_) {
        DrawPixelText(r, Loc("← →:選ぶ  ENTER:決定  ESC:やめる",
                             "LEFT/RIGHT:CHOOSE  ENTER:OK  ESC:CANCEL"),
                      4, FooterY() + 3, 1.0f, pal.Ink70);
        return;
    }

    if (tab_ == Tab::Boxes) {
        // 判定タブはマウスでも編集できるので、その説明を出します。
        DrawPixelText(r, Loc("マウス:判定をドラッグ / 角で大きさ変更  G:目盛り吸着  [ ]:目盛り幅",
                             "MOUSE:DRAG BOX / CORNERS RESIZE  G:SNAP  [ ]:GRID SIZE"),
                      4, FooterY() + 2, 1.0f, pal.Ink70);
    } else {
        DrawPixelText(r, Loc("矢印:変更  SHIFT:10倍  ENTER:実行  TAB:ページ",
                             "ARROWS:EDIT  SHIFT:X10  ENTER:GO  TAB:PAGE"),
                      4, FooterY() + 2, 1.0f, pal.Ink70);
    }
    DrawPixelText(r, Loc("S:保存  PGUP/PGDN:キャラ  L:日本語/EN  ESC:戻る",
                         "S:SAVE  PGUP/PGDN:CHARACTER  L:JP/EN  ESC:BACK"),
                  4, FooterY() + 3 + LineH(), 1.0f, pal.Ink55);
}

} // namespace kakuge

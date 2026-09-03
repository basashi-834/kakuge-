// =====================================================================
// platform/Editor.h - キャラクターエディタ（ゲーム内でデータを編集する画面）
// =====================================================================
// キャラクターの性能や技のフレームデータを、ゲームを起動したまま
// その場で編集して保存できる画面です。
//
// なぜ必要なのか
// ------------
// 格闘ゲームの面白さは、ほとんどが数値の調整で決まります。
//   「この技、発生 7 フレームだと強すぎる。8 にしてみよう」
//   「弱パンチのリーチをあと 2 ピクセルだけ伸ばしたい」
// といった試行錯誤を、何十回、何百回と繰り返すことになります。
//
// そのたびに JSON をテキストエディタで開いて、保存して、ゲームを
// 再起動して…では、1 回の確認に 30 秒かかります。ここで直せれば
// 数秒です。この差が、最終的な作品の完成度を決めます。
//
// 保存先
// -----
// 編集結果はユーザーフォルダ（Windows なら %APPDATA%\Kakuge）に
// 保存され、data/ の元データは書き換えません。
// 読み込み順は「元データ → ユーザーフォルダ」なので、編集したものが
// 優先されます。おかしくしてしまっても、ユーザーフォルダの
// ファイルを消せば元に戻せます。
//
// 操作方法（すべてキーボード）
// ------------------------
//   ↑ ↓          … 編集する項目を選ぶ
//   ← →          … 数値を増減 / 選択肢を切り替え
//   Shift + ← →  … 10 倍の幅で増減（大きく動かしたいとき）
//   Enter        … 文字の項目なら入力開始、動作の項目なら実行
//   Tab          … タブ（CHARACTER / MOVE / BOXES）を切り替え
//   S            … 保存
//   L            … 表示を日本語 / 英語で切り替える
//   Esc          … 戻る（文字入力中なら入力の取り消し）
//
// 表示言語
// -------
// 既定は日本語です。項目名は 8x8 のカナで表示します（漢字はフォントに
// 入っていないので、カナ書きになります。詳しくは Font.h）。
// L キーで英語に切り替えられます。データそのもの（技の名前や ID、
// 姿勢の "crouch" といった値）は言語を変えても変わりません。
// 画面に出すラベルだけが切り替わります。
// =====================================================================
#pragma once
#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "engine/CharacterStats.h"
#include "engine/DataManager.h"
#include "engine/MoveData.h"
#include "platform/Renderer.h"

namespace kakuge {

class Editor {
public:
    // 画面を開く。今あるデータを下書きとして読み込みます。
    void Open(DataManager* dm);

    // キー入力。処理したら true を返します（Game 側はそこで処理を止める）。
    bool HandleKey(SDL_Keycode key, bool shift);
    // 文字入力（名前などのテキスト項目用）。
    void HandleText(const char* utf8Text);

    void Draw(Renderer& r);

    // BACK が押されたか（Game 側がタイトルへ戻すのに使う）。
    bool WantsExit() const { return wantsExit_; }
    void ClearExitRequest() { wantsExit_ = false; }
    // 文字入力中かどうか（SDL のテキスト入力の開始/終了に使う）。
    bool IsTextEditing() const { return editingText_; }

private:
    // -----------------------------------------------------------------
    // 編集する項目 1 行ぶん
    // -----------------------------------------------------------------
    // 「ラベル」と「値の読み書き方法」をまとめたものです。
    // 値そのものは下書き（statsDraft_ / moveDraft_）が持っていて、
    // ここでは読み書きする関数だけを覚えています。こうしておくと、
    // 項目を 1 行足すのに 1 行書くだけで済みます。
    struct Field {
        enum class Kind {
            Number,  // 数値（← → で増減）
            Text,    // 文字（Enter で入力開始）
            Choice,  // 選択肢（← → で切り替え）
            Action,  // 動作（Enter で実行）
            Info     // 表示だけ（選べない）
        };
        Kind kind = Kind::Number;
        std::string label;

        // 数値項目
        std::function<double()> getNum;
        std::function<void(double)> setNum;
        double step = 1.0;
        double minV = 0.0, maxV = 0.0;
        bool hasRange = false;
        int decimals = 0; // 表示する小数桁数

        // 文字項目
        std::function<std::string()> getText;
        std::function<void(const std::string&)> setText;

        // 選択肢項目
        //   options      … 保存される値（"crouch" など。言語で変わらない）
        //   optionLabels … 画面に出す文字（"シャガミ" など。空なら options）
        // 2 つに分けているのは、表示を日本語にしたときにデータへ
        // 日本語が書き込まれてしまわないようにするためです。
        std::vector<std::string> options;
        std::vector<std::string> optionLabels;
        std::function<int()> getIndex;
        std::function<void(int)> setIndex;

        // 動作項目 / 表示だけの項目
        std::function<void()> onActivate;
        std::function<std::string()> getInfo;
    };

    // どのタブを開いているか。
    enum class Tab { Character, Move, Boxes };

    // BOXES タブで、どの判定を編集しているか。
    enum class BoxTarget { Hitbox, HurtStand, HurtCrouch, HurtAir };

    // ---- 状態 ----
    DataManager* dm_ = nullptr;
    bool japanese_ = true; // 表示言語（L キーで切り替え）
    Tab tab_ = Tab::Character;
    int selected_ = 0;   // 選んでいる行
    int scroll_ = 0;     // 表示の先頭行（項目が多いとき用）
    bool wantsExit_ = false;
    bool dirty_ = false; // 保存していない変更があるか
    std::string message_; // 画面下に出す一言（保存しました等）
    double messageTimer_ = 0.0;

    // ---- 編集対象 ----
    std::vector<std::string> charIds_;
    int charIndex_ = 0;
    CharacterStats statsDraft_;

    std::vector<std::string> moveIds_;
    int moveIndex_ = 0;
    MoveData moveDraft_;

    BoxTarget boxTarget_ = BoxTarget::Hitbox;
    int boxIndex_ = 0;

    // ---- 文字入力中の状態 ----
    bool editingText_ = false;
    std::string textBuffer_;
    int editingField_ = -1;

    std::vector<Field> fields_;

    // ---- 内部処理 ----
    void LoadCharacter(int index);  // キャラクターを読み込んで下書きにする
    void LoadMove(int index);       // 技を読み込んで下書きにする
    void RebuildFields();           // 今のタブに合わせて項目一覧を作り直す
    void BuildCharacterFields();
    void BuildMoveFields();
    void BuildBoxFields();

    void Save();                    // 下書きをファイルへ書き出す
    void CreateNewCharacter();      // 今のキャラを雛形に新規作成
    void AddBox();
    void DeleteBox();

    // BOXES タブの値の読み書き。
    //
    // ポインタで箱を覚えず、読み書きのたびに「今の対象・今の番号」から
    // 引き直します。箱を追加・削除したり技を切り替えたりすると、
    // 覚えていたポインタは壊れた場所を指してしまうためです
    //（std::vector は要素を足すと中身ごと引っ越すことがあります）。
    //   which: 0=OffsetX 1=OffsetY 2=Width 3=Height
    double GetBoxValue(int which) const;
    void SetBoxValue(int which, double value);
    // index 番目の箱を四角形として取り出す（描画用）。無ければ false。
    bool GetBoxRect(int index, RectBox& out) const;
    int CurrentBoxCount() const;
    std::string CurrentStanceName() const;

    // 表示言語に応じて日本語か英語を返す。ラベルはすべてこれを通します。
    std::string Loc(const std::string& jp, const std::string& en) const {
        return japanese_ ? jp : en;
    }

    void SetMessage(const std::string& jp, const std::string& en);
    void MoveSelection(int delta);
    void AdjustSelected(int direction, bool shift);
    void ActivateSelected();

    // ---- 描画 ----
    void DrawHeader(Renderer& r);
    void DrawTabs(Renderer& r);
    void DrawFieldList(Renderer& r, float x, float y, float w, float h);
    void DrawBoxPreview(Renderer& r, float x, float y, float w, float h);
    void DrawFooter(Renderer& r);
};

} // namespace kakuge

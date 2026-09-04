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
// 保存していない編集は消えません
// --------------------------
// 技やキャラクターを切り替えても、まだ保存していない編集は覚えています
// （下の pendingMoves_ / pendingChars_）。3 つの技を続けて調整してから
// 最後に S を 1 回押す、という使い方ができます。保存すると、覚えている
// ぶんも含めてまとめてファイルへ書き出します。
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
#include <unordered_map>
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

    // ---- マウス操作（判定タブで、判定の四角形を直接つかむ）----
    // 座標は内部キャンバス（VirtualW x VirtualH）のものを渡します。
    void HandleMouseMove(double vx, double vy);
    void HandleMouseDown(double vx, double vy);
    void HandleMouseUp();

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

        // 単位（"PX/S" など）。数値のうしろに小さく出します。
        // 「67.1」だけを見ても何の 67.1 なのか分からないので、
        // 単位が無いと調整のしようがありません。
        std::string unit;

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

        // 「D（ダウン）」を入力できる数値項目のための読み書き。
        // 設定されていれば、数値のかわりに D と表示・入力できます
        //（ヒット時硬直差の欄で使います）。
        std::function<bool()> getDown;
        std::function<void(bool)> setDown;
    };

    // どのタブを開いているか。
    enum class Tab { Character, Move, Boxes };

    // BOXES タブで、どの判定を編集しているか。
    //
    //   Hitbox     … 今選んでいる技の攻撃判定
    //   HurtMove   … 今選んでいる技だけの食らい判定（技ごとの設定）
    //   HurtStand / HurtCrouch / HurtAir … 姿勢ごとの食らい判定（技を出して
    //                 いないときの標準の形。キャラクター全体の設定）
    //   Push*      … 姿勢ごとの押し合い判定
    //
    // Hitbox と HurtMove は「技」のデータ（moveDraft_）を、
    // それ以外は「キャラクター」のデータ（statsDraft_）を書き換えます。
    // 保存先が違うので、混同しないよう名前を分けています。
    enum class BoxTarget { Hitbox, HurtMove, HurtStand, HurtCrouch, HurtAir,
                           PushStand, PushCrouch, PushAir };

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

    // ---- 保存していない編集の預かり所 ----
    //
    // 下書き（statsDraft_ / moveDraft_）は 1 つぶんしかありません。
    // 技を切り替えると、その 1 つが新しい技で上書きされます。
    // 以前はここで、保存していない編集が黙って消えていました
    //（弱パンチを調整 → 強パンチを見に行った → 弱パンチの調整が消える）。
    //
    // そこで、切り替える直前に「まだ保存していない下書き」をここへ
    // 預けておき、戻ってきたらそれを読み直します。S キーで保存する
    // ときは、預かっているものも全部まとめてファイルへ書きます。
    //   pendingMoves_ の鍵は "キャラクターID/技ID"
    //   pendingChars_ の鍵は "キャラクターID"
    std::unordered_map<std::string, MoveData> pendingMoves_;
    std::unordered_map<std::string, CharacterStats> pendingChars_;

    // 今の下書きを預かり所に入れる。
    //   onlyIfDirty = true  … 変更があるときだけ（切り替えのとき）
    //   onlyIfDirty = false … 変更が無くても必ず（保存のとき）
    void RememberDrafts(bool onlyIfDirty);
    // 預かり所にあればそれを、無ければファイルの内容を下書きにする。
    void LoadMoveInternal(int index);

    BoxTarget boxTarget_ = BoxTarget::Hitbox;
    int boxIndex_ = 0;

    // ---- 確認ダイアログ（技の削除など、取り返しのつかない操作用）----
    bool confirmActive_ = false;
    std::string confirmTitle_;
    std::string confirmDetail_;
    int confirmChoice_ = 0; // 0 = キャンセル / 1 = 実行
    std::function<void()> confirmAction_;

    // ---- マウスで判定を編集するための状態 ----
    // どこをつかんでいるか。四隅・辺をつかむと大きさが、
    // 内側をつかむと位置が変わります。
    enum class DragMode { None, Move, Left, Right, Top, Bottom,
                          TopLeft, TopRight, BottomLeft, BottomRight };
    DragMode dragMode_ = DragMode::None;
    double dragStartX_ = 0, dragStartY_ = 0;  // つかんだ位置（判定の座標系）
    RectBox dragStartBox_;                     // つかんだ瞬間の四角形
    double mouseVx_ = 0, mouseVy_ = 0;         // 今のマウス位置（内部キャンバス）
    bool gridSnap_ = false;                    // 目盛りに吸着させるか
    int gridSize_ = 4;                         // 吸着の間隔（px）
    // プレビューの原点（キャラクターの足元）と表示範囲。
    // マウス座標を判定の座標へ戻すのに使うので、描画のたびに覚えます。
    float previewFootX_ = 0, previewFootY_ = 0;
    float previewX_ = 0, previewY_ = 0, previewW_ = 0, previewH_ = 0;

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

    // ---- 技の管理 ----
    void BuildCancelFields();       // キャンセル設定の行を並べる
    void AddMove();                 // 新しい技を追加する
    void DuplicateMove();           // 今の技を複製する
    void RequestDeleteMove();       // 削除の確認を出す
    void DeleteCurrentMove();       // 実際に削除する
    // 引数の技を「派生先」に指定している技の一覧（削除時の警告に使う）。
    std::vector<std::string> MovesReferencing(const std::string& moveId) const;
    // 使われていない技 ID を作る（move_001 のような通し番号）。
    // alwaysNumber が true なら、base 自体が空いていても番号を付けます。
    std::string MakeUniqueMoveId(const std::string& base, bool alwaysNumber = false) const;
    void SelectMoveById(const std::string& moveId);

    // ---- 確認ダイアログ ----
    void OpenConfirm(const std::string& title, const std::string& detail,
                     std::function<void()> action);
    bool HandleConfirmKey(SDL_Keycode key);
    void DrawConfirm(Renderer& r);

    // ---- マウスで判定を編集する ----
    // マウス位置（内部キャンバス）を判定の座標へ直す。
    bool PreviewToBox(double vx, double vy, double& bx, double& by) const;
    // 位置を目盛りに合わせる（Snap to Grid が ON のときだけ）。
    double SnapValue(double v) const;
    // 今の対象の判定を書き換える（マウス操作の結果を反映）。
    void SetBoxRect(int index, const RectBox& box);
    // (vx, vy) の位置にある判定を探す。見つからなければ -1。
    int BoxAtPoint(double vx, double vy) const;
    // 選んでいる判定の、つかめる場所を調べる。
    DragMode HandleAtPoint(const RectBox& box, double bx, double by) const;

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
    bool IsPushboxTarget() const;       // 押し合い判定を編集中か
    bool IsMoveHurtTarget() const;      // 技ごとの食らい判定を編集中か
    // 今編集している食らい判定の「部位一覧」。
    // 技ごとの設定なら技の下書きを、姿勢ごとの設定ならキャラクターの
    // 下書きを指します。攻撃判定・押し合い判定のときは nullptr。
    std::vector<HurtboxPart>* CurrentHurtParts();
    const std::vector<HurtboxPart>* CurrentHurtParts() const;
    // 今の姿勢の食らい判定を、技ごとの食らい判定へ丸ごと写す。
    // 一から置くより、標準の形を写してから伸ばすほうが早いためです。
    void CopyStanceHurtboxesToMove();
    RectBox* CurrentPushbox();          // 編集中の押し合い判定（無ければ nullptr）
    const RectBox* CurrentPushbox() const;

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

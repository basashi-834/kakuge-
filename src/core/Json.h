// =====================================================================
// core/Json.h - このプロジェクト専用の、小さな JSON 読み書きライブラリ
// =====================================================================
// なぜ自前で書いているのか
// -----------------------
// このゲームのキャラクター性能や技のデータは、すべて data/ フォルダの
// 中の .json というテキストファイルに入っています。ゲームはそれを起動時に
// 読み込みます。つまり「JSON を C++ のデータに変換する部品」が必要です。
//
// 世の中には nlohmann/json のような高機能な JSON ライブラリがありますが、
// このプロジェクトの決まりごとは「使う外部ライブラリは SDL2 だけ」です。
// 入れるライブラリが増えるほど、ビルド手順が長くなり、初めての人が
// つまずく場所も増えるからです。JSON の読み書きはそれほど難しくないので、
// ここに必要な分だけを自分で用意しています（約 400 行）。
//
// 対応している範囲
// ---------------
//  - オブジェクト {"key": 値, ...}、配列 [値, ...]
//  - 文字列 "..."（\n \t \" \\ \uXXXX のエスケープに対応）
//  - 数値（12, -3.5, 1e3 など）、true / false / null
//  - 日本語などの UTF-8 文字はそのまま素通し（技名に日本語が使えます）
// JSON5 のようなコメント付き JSON や、末尾カンマには対応していません
// （標準の JSON にない書き方なので、間違いとして弾きます）。
//
// 使い方の例
// ---------
//   kakuge::Json j;
//   if (kakuge::Json::LoadFile("data/characters/ryu.json", j)) {
//       std::string name = j.GetString("name", "名無し");  // 無ければ "名無し"
//       int hp           = j.GetInt("maxHP", 1000);
//       if (const kakuge::Json* moves = j.Find("moves")) { // 配列を回す
//           for (const kakuge::Json& m : moves->Items()) {
//               std::string moveId = m.AsString();
//           }
//       }
//   }
//
//   kakuge::Json out = kakuge::Json::MakeObject();
//   out.Set("id", kakuge::Json("ryu"));
//   out.Set("maxHP", kakuge::Json(1000));
//   kakuge::Json::SaveFile("out.json", out);
// =====================================================================
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace kakuge {

class Json {
public:
    // JSON の値は必ずこの 6 種類のどれかです。
    enum class Type { Null, Bool, Number, String, Array, Object };

    // ---- 作る ----
    Json() = default;                                    // null
    explicit Json(bool v) : type_(Type::Bool), bool_(v) {}
    explicit Json(int v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    explicit Json(double v) : type_(Type::Number), num_(v) {}
    explicit Json(const char* v) : type_(Type::String), str_(v ? v : "") {}
    explicit Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

    // 空の配列 / 空のオブジェクトを作るときはこの 2 つを使います。
    // （Json() は「null」であって「空のオブジェクト」ではありません）
    static Json MakeArray();
    static Json MakeObject();

    // ---- 種類を調べる ----
    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    // ---- この値そのものを取り出す ----
    // 種類が違うときは既定値（def）を返します。例外は投げません。
    // データファイルが多少壊れていてもゲームが落ちないように、
    // 「読めなければ既定値で続行」という方針で統一しています。
    bool AsBool(bool def = false) const;
    double AsNumber(double def = 0.0) const;
    int AsInt(int def = 0) const;
    std::string AsString(const std::string& def = std::string()) const;

    // ---- オブジェクトとして読む ----
    // Has  : そのキーがあるか
    // Find : あればその値へのポインタ、無ければ nullptr
    //        （配列やオブジェクトを取り出したいときに使います）
    bool Has(const std::string& key) const;
    const Json* Find(const std::string& key) const;

    // キーがあればその値を、無ければ def を返す便利関数。
    // データ読み込みのほとんどはこの 4 つで書けます。
    bool GetBool(const std::string& key, bool def) const;
    double GetNumber(const std::string& key, double def) const;
    int GetInt(const std::string& key, int def) const;
    std::string GetString(const std::string& key, const std::string& def) const;

    // ---- オブジェクトとして書く ----
    // 同じキーを 2 回 Set すると上書きします。キーの並び順は
    // 「最初に Set した順」を覚えているので、保存し直しても
    // 行の順番が入れ替わらず、差分が読みやすくなります。
    void Set(const std::string& key, Json value);
    const std::vector<std::pair<std::string, Json>>& Members() const { return members_; }

    // ---- 配列として読む / 書く ----
    size_t Size() const;                       // 配列の要素数（配列以外は 0）
    const Json& At(size_t index) const;        // 範囲外なら null を返す
    const std::vector<Json>& Items() const;    // for (auto& v : j.Items())
    void Push(Json value);                     // 配列の末尾に追加

    // ---- 文字列にする ----
    // indent = 0 なら 1 行にぎゅっと詰めます。既定の 4 は人が読みやすい
    // 形（data/ のファイルと同じ見た目）です。
    std::string Dump(int indent = 4) const;

    // ---- 解析する ----
    // 成功したら true を返して out に結果を入れます。失敗したら false を
    // 返し、error に「何行目のどこが変か」を日本語で入れます
    //（error に nullptr を渡せばメッセージは作りません）。
    static bool Parse(const std::string& text, Json& out, std::string* error = nullptr);

    // ---- ファイルとして読み書きする ----
    // LoadFile はファイルが無い・壊れている場合に false を返します。
    // SaveFile は途中のフォルダが無ければ作ってから書き込みます。
    static bool LoadFile(const std::string& path, Json& out, std::string* error = nullptr);
    static bool SaveFile(const std::string& path, const Json& value, int indent = 4);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> elements_;                          // 配列の中身
    std::vector<std::pair<std::string, Json>> members_;    // オブジェクトの中身（順序つき）

    Json* FindMutable(const std::string& key);
    void DumpTo(std::string& out, int indent, int depth) const;
};

} // namespace kakuge

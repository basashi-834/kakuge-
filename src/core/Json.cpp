// =====================================================================
// core/Json.cpp - Json.h の中身（実装）
// =====================================================================
// 前半が「JSON 文字列 → C++ のデータ」（解析 / パース）、
// 後半が「C++ のデータ → JSON 文字列」（出力 / ダンプ）です。
//
// 解析は「再帰下降パーサ」という、JSON のような入れ子構造を読むときの
// 定番の書き方をしています。難しそうな名前ですが、やっていることは
//   「値をひとつ読む関数」を作り、
//   　その中で配列やオブジェクトに出会ったら、
//   　自分自身をもう一度呼んで中身を読む
// というだけです。
// =====================================================================
#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>

namespace kakuge {

namespace {

// 「値が無い」ときに参照を返すための、共有の null。
// At() が範囲外で呼ばれても安全に null を返せるようにするためのものです。
const Json& NullJson() {
    static const Json kNull;
    return kNull;
}

const std::vector<Json>& EmptyItems() {
    static const std::vector<Json> kEmpty;
    return kEmpty;
}

// ---------------------------------------------------------------
// パーサ本体
// ---------------------------------------------------------------
// text_ の先頭から 1 文字ずつ見ていき、pos_ を進めながら読み進めます。
class Parser {
public:
    Parser(const std::string& text) : text_(text) {}

    bool Run(Json& out) {
        SkipSpace();
        if (!ParseValue(out)) return false;
        SkipSpace();
        // JSON ファイルは値が「ひとつ」だけ。読み終わったあとに
        // まだ何か文字が残っていたら、それは書き間違いです。
        if (pos_ != text_.size()) {
            Fail("ファイルの終わりに余分な文字があります");
            return false;
        }
        return true;
    }

    const std::string& Error() const { return error_; }

private:
    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;

    // 現在位置が何行目・何文字目かを添えてエラーメッセージを作ります。
    // （「JSON が壊れています」だけだと直しようがないため）
    void Fail(const std::string& message) {
        if (!error_.empty()) return; // 最初のエラーだけ残す
        int line = 1, column = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') { line++; column = 1; } else { column++; }
        }
        error_ = std::to_string(line) + " 行 " + std::to_string(column) + " 文字目: " + message;
    }

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    void SkipSpace() {
        while (!AtEnd()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { pos_++; continue; }
            // UTF-8 の BOM（ファイル先頭に付くことがある見えない 3 バイト）。
            // メモ帳で保存した JSON にはこれが付くことがあるので、
            // 先頭にあれば読み飛ばします。
            if (pos_ == 0 && text_.size() >= 3 &&
                static_cast<unsigned char>(text_[0]) == 0xEF &&
                static_cast<unsigned char>(text_[1]) == 0xBB &&
                static_cast<unsigned char>(text_[2]) == 0xBF) {
                pos_ += 3;
                continue;
            }
            break;
        }
    }

    // 期待した文字が来ているか確認して 1 つ進める。
    bool Expect(char c) {
        if (Peek() != c) {
            Fail(std::string("'") + c + "' があるはずですが違いました");
            return false;
        }
        pos_++;
        return true;
    }

    // 値をひとつ読む。JSON の値は先頭の 1 文字で種類が分かります。
    bool ParseValue(Json& out) {
        SkipSpace();
        if (AtEnd()) { Fail("値がありません"); return false; }
        char c = Peek();
        switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Json(std::move(s));
                return true;
            }
            case 't':
                if (!Literal("true")) return false;
                out = Json(true);
                return true;
            case 'f':
                if (!Literal("false")) return false;
                out = Json(false);
                return true;
            case 'n':
                if (!Literal("null")) return false;
                out = Json();
                return true;
            default:
                return ParseNumber(out);
        }
    }

    bool Literal(const char* word) {
        size_t len = std::string(word).size();
        if (text_.compare(pos_, len, word) != 0) {
            Fail(std::string("'") + word + "' と書かれているはずです");
            return false;
        }
        pos_ += len;
        return true;
    }

    bool ParseObject(Json& out) {
        if (!Expect('{')) return false;
        out = Json::MakeObject();
        SkipSpace();
        if (Peek() == '}') { pos_++; return true; }   // 空のオブジェクト {}
        while (true) {
            SkipSpace();
            std::string key;
            if (!ParseString(key)) return false;      // キーは必ず文字列
            SkipSpace();
            if (!Expect(':')) return false;
            Json value;
            if (!ParseValue(value)) return false;
            out.Set(key, std::move(value));
            SkipSpace();
            if (Peek() == ',') { pos_++; continue; }  // まだ続く
            if (Peek() == '}') { pos_++; return true; } // 終わり
            Fail("',' か '}' があるはずです");
            return false;
        }
    }

    bool ParseArray(Json& out) {
        if (!Expect('[')) return false;
        out = Json::MakeArray();
        SkipSpace();
        if (Peek() == ']') { pos_++; return true; }   // 空の配列 []
        while (true) {
            Json value;
            if (!ParseValue(value)) return false;
            out.Push(std::move(value));
            SkipSpace();
            if (Peek() == ',') { pos_++; continue; }
            if (Peek() == ']') { pos_++; return true; }
            Fail("',' か ']' があるはずです");
            return false;
        }
    }

    // 1 つの Unicode 文字（コードポイント）を UTF-8 のバイト列にして足す。
    // \uXXXX エスケープを元の文字に戻すときに使います。
    static void AppendUtf8(std::string& out, unsigned int codepoint) {
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    bool ParseHex4(unsigned int& value) {
        if (pos_ + 4 > text_.size()) { Fail("\\u のあとの 16 進数が足りません"); return false; }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else { Fail("\\u のあとが 16 進数ではありません"); return false; }
        }
        return true;
    }

    bool ParseString(std::string& out) {
        if (!Expect('"')) return false;
        out.clear();
        while (true) {
            if (AtEnd()) { Fail("文字列が閉じられていません"); return false; }
            char c = text_[pos_++];
            if (c == '"') return true;               // 閉じ引用符 → 終わり
            if (c != '\\') { out += c; continue; }   // 普通の文字（UTF-8 もここを通る）

            // ここからエスケープ（\ で始まる特別な書き方）の処理
            if (AtEnd()) { Fail("\\ のあとに文字がありません"); return false; }
            char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!ParseHex4(cp)) return false;
                    // サロゲートペア: 絵文字など、\uXXXX 2 個で 1 文字を
                    // 表す書き方。前半（D800-DBFF）が来たら後半も読みます。
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        size_t save = pos_;
                        pos_ += 2;
                        unsigned int low = 0;
                        if (ParseHex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save; // 後半が来ていないので単独の文字として扱う
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    Fail("知らないエスケープ \\ が使われています");
                    return false;
            }
        }
    }

    bool ParseNumber(Json& out) {
        size_t start = pos_;
        if (Peek() == '-' || Peek() == '+') pos_++;
        bool anyDigit = false;
        while (!AtEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') { pos_++; anyDigit = true; }
        if (!AtEnd() && text_[pos_] == '.') {
            pos_++;
            while (!AtEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') { pos_++; anyDigit = true; }
        }
        if (anyDigit && !AtEnd() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            pos_++;
            if (!AtEnd() && (text_[pos_] == '+' || text_[pos_] == '-')) pos_++;
            while (!AtEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') pos_++;
        }
        if (!anyDigit) { Fail("数値として読めません"); return false; }
        // std::stod はロケール（地域設定）の影響を受けて "1.5" を
        // 読み違えることがあるため、影響を受けない istringstream +
        // classic ロケールで変換します。
        std::istringstream iss(text_.substr(start, pos_ - start));
        iss.imbue(std::locale::classic());
        double value = 0.0;
        iss >> value;
        if (iss.fail()) { pos_ = start; Fail("数値として読めません"); return false; }
        out = Json(value);
        return true;
    }
};

// 文字列を JSON の書き方（"..."）に直す。
// 制御文字とダブルクォート、バックスラッシュだけをエスケープし、
// 日本語などの UTF-8 はそのまま出します（\uXXXX に変換しません）。
// そのほうがファイルを直接開いたときに読めるからです。
void EscapeString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

// 数値を文字列にする。整数として表せる値は "1000" のように小数点なしで
// 書きます（"1000.0" だと元のデータファイルと見た目が変わってしまうため）。
std::string NumberToString(double v) {
    if (std::isnan(v) || std::isinf(v)) return "0"; // JSON に無い値なので 0 に落とす
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        long long asInt = static_cast<long long>(v);
        return std::to_string(asInt);
    }
    char buf[64];
    // %.10g : 有効数字 10 桁。ゲームのデータ（座標・速度）には十分で、
    // 0.30000000000000004 のような見苦しい誤差表示を防げます。
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

} // namespace

// ---------------------------------------------------------------
// 作る
// ---------------------------------------------------------------
Json Json::MakeArray() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::MakeObject() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

// ---------------------------------------------------------------
// 値そのものを取り出す
// ---------------------------------------------------------------
bool Json::AsBool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    // JSON では 0 / 1 を真偽値の代わりに書いてあることがあるので拾います。
    if (type_ == Type::Number) return num_ != 0.0;
    return def;
}

double Json::AsNumber(double def) const {
    if (type_ == Type::Number) return num_;
    if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
    return def;
}

int Json::AsInt(int def) const {
    if (type_ == Type::Number) return static_cast<int>(std::llround(num_));
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return def;
}

std::string Json::AsString(const std::string& def) const {
    if (type_ == Type::String) return str_;
    return def;
}

// ---------------------------------------------------------------
// オブジェクトとして読む / 書く
// ---------------------------------------------------------------
const Json* Json::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : members_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

Json* Json::FindMutable(const std::string& key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& kv : members_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

bool Json::Has(const std::string& key) const { return Find(key) != nullptr; }

bool Json::GetBool(const std::string& key, bool def) const {
    const Json* v = Find(key);
    return v ? v->AsBool(def) : def;
}

double Json::GetNumber(const std::string& key, double def) const {
    const Json* v = Find(key);
    return v ? v->AsNumber(def) : def;
}

int Json::GetInt(const std::string& key, int def) const {
    const Json* v = Find(key);
    return v ? v->AsInt(def) : def;
}

std::string Json::GetString(const std::string& key, const std::string& def) const {
    const Json* v = Find(key);
    return v ? v->AsString(def) : def;
}

void Json::Set(const std::string& key, Json value) {
    // null に対して Set したら、暗黙にオブジェクトとして扱います
    // （Json j; j.Set(...) と書けるようにするための親切設計）。
    if (type_ != Type::Object) {
        type_ = Type::Object;
        elements_.clear();
    }
    if (Json* existing = FindMutable(key)) {
        *existing = std::move(value);
        return;
    }
    members_.emplace_back(key, std::move(value));
}

// ---------------------------------------------------------------
// 配列として読む / 書く
// ---------------------------------------------------------------
size_t Json::Size() const { return type_ == Type::Array ? elements_.size() : 0; }

const Json& Json::At(size_t index) const {
    if (type_ != Type::Array || index >= elements_.size()) return NullJson();
    return elements_[index];
}

const std::vector<Json>& Json::Items() const {
    if (type_ != Type::Array) return EmptyItems();
    return elements_;
}

void Json::Push(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        members_.clear();
    }
    elements_.push_back(std::move(value));
}

// ---------------------------------------------------------------
// 文字列にする
// ---------------------------------------------------------------
std::string Json::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    return out;
}

void Json::DumpTo(std::string& out, int indent, int depth) const {
    // indent が 0 なら改行もインデントも入れず 1 行にまとめます。
    const bool pretty = indent > 0;
    auto newlineAndPad = [&](int level) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
    };

    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += (bool_ ? "true" : "false"); break;
        case Type::Number: out += NumberToString(num_); break;
        case Type::String: EscapeString(str_, out); break;
        case Type::Array: {
            if (elements_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < elements_.size(); ++i) {
                if (i > 0) out += ',';
                newlineAndPad(depth + 1);
                elements_[i].DumpTo(out, indent, depth + 1);
            }
            newlineAndPad(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            if (members_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < members_.size(); ++i) {
                if (i > 0) out += ',';
                newlineAndPad(depth + 1);
                EscapeString(members_[i].first, out);
                out += pretty ? ": " : ":";
                members_[i].second.DumpTo(out, indent, depth + 1);
            }
            newlineAndPad(depth);
            out += '}';
            break;
        }
    }
}

// ---------------------------------------------------------------
// 解析する / ファイルを読み書きする
// ---------------------------------------------------------------
bool Json::Parse(const std::string& text, Json& out, std::string* error) {
    Parser parser(text);
    Json parsed;
    if (!parser.Run(parsed)) {
        if (error) *error = parser.Error();
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool Json::LoadFile(const std::string& path, Json& out, std::string* error) {
    // std::ios::binary で開くのは、Windows で改行が勝手に変換されるのを
    // 避けるためです（JSON の中身は自分で改行を扱うので、素のままが安全）。
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        if (error) *error = "ファイルを開けません: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    std::string parseError;
    if (!Parse(text, out, &parseError)) {
        if (error) *error = path + " : " + parseError;
        return false;
    }
    return true;
}

bool Json::SaveFile(const std::string& path, const Json& value, int indent) {
    std::error_code ec;
    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) return false;
    std::string text = value.Dump(indent);
    text += '\n'; // 行末改行（テキストファイルの慣習）
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

} // namespace kakuge

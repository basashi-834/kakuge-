// platform/Editor.cpp
// Character Editor screen - a fully custom-drawn GDI+ form (no native
// Win32 child controls). The previous version built ~70 real EDIT/
// COMBOBOX/BUTTON HWNDs, each owner-drawn through WM_DRAWITEM and all
// repainted independently; the user reported it as sluggish. This
// version keeps ONE widget list, paints the whole form in a single pass
// into the app's back buffer, and repaints only on interaction (the
// 15ms game timer doesn't touch this screen), so it stays snappy.
//
// Widget model: `EdWidget`s are built once per EnterEditor() and bind
// to the App's draft data through small getter/setter closures, so a
// text field shows whatever the draft holds and writes back on every
// keystroke - nothing to keep in sync. Types:
//   Label      static text (EStr, so JP/EN switches at draw time)
//   Field      single-line text input (click to focus; typing, Backspace,
//              Delete, Left/Right/Home/End, Tab to next field, Enter/Esc
//              to leave); numeric fields parse on each edit and keep the
//              last valid value if the text is mid-edit ("-", "")
//   Dropdown   click opens a list overlay drawn last (scrollable with
//              the wheel); click an item to select, click elsewhere to
//              close
//   Button     click -> onClick; `primary` = red fill (the form's main
//              action), else outlined
//   Check      toggle box
// Mode: widgets carry a mode mask so the "+ NEW CHARACTER" prompt can
// replace column 1's normal contents without rebuilding anything.
//
// Text: the hand-authored pixel font for plain-ASCII labels/buttons (same
// look as every other screen), the system font for anything containing
// non-ASCII (Japanese labels, typed names) since the pixel font only has
// Latin glyphs - see DrawLabelText.
#include "App.h"
#include <commdlg.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cwctype>

namespace kakuge {

namespace {

// ---------------------------------------------------------------------
// Language table.
// ---------------------------------------------------------------------
enum class EStr {
    Character, Name, MaxHp, WalkFwd, WalkBack, DashSpeed, JumpVel, Gravity,
    SaveCharacter, NewCharacter, BackToTitle,
    NewCharId, DisplayName, CloneFrom, Create, CancelBtn,
    Move, MoveDisplayName, Startup, Active, Recovery, Damage, Hitstun, Blockstun, Hitstop,
    ButtonLabel, CancelStart, CancelEnd, CommandLabel, ClearBtn, MovingHitbox, SaveMove,
    PreviewTitle, DragHitbox, DragHurtbox,
    HitboxSection, AddBtn, RemoveBtn,
    HurtboxSection, Stance, Part,
    Legend, LanguageBtn,
    MotionImageLabel, BrowseBtn,
    SavedCharacter, SavedMove, CreatedCharacter, NeedIdAndTemplate, IdTaken,
    Count
};

const wchar_t* const kStringsEN[static_cast<size_t>(EStr::Count)] = {
    L"CHARACTER", L"NAME", L"MAX HP", L"WALK FORWARD SPEED", L"WALK BACKWARD SPEED", L"DASH SPEED",
    L"JUMP VELOCITY (negative = up)", L"GRAVITY",
    L"SAVE CHARACTER", L"+ NEW CHARACTER", L"BACK TO TITLE",
    L"NEW CHARACTER ID (e.g. ken)", L"DISPLAY NAME", L"CLONE MOVESET FROM", L"CREATE", L"CANCEL",
    L"MOVE", L"DISPLAY NAME", L"STARTUP", L"ACTIVE", L"RECOVERY", L"DAMAGE", L"HITSTUN", L"BLOCKSTUN", L"HITSTOP",
    L"BUTTON (NORMAL MOVES ONLY)", L"C-START", L"C-END",
    L"SPECIAL MOVE COMMAND", L"CLEAR",
    L"HITBOX MOVES INDEPENDENTLY (SPEC ONLY)", L"SAVE MOVE",
    L"HITBOX / HURTBOX PREVIEW (drag = move, drag corner = resize)", L"DRAG: HITBOX", L"DRAG: HURTBOX",
    L"MOVE HITBOX  X / Y / W / H", L"+ ADD", L"- REMOVE",
    L"CHARACTER HURTBOX  X / Y / W / H", L"HURTBOX STANCE", L"HURTBOX PART",
    L"green = hurtbox   red = hitbox", L"JP",
    L"MOTION IMAGE (reference only)", L"BROWSE",
    L"Character saved.", L"Move saved.", L"New character created.", L"Enter an id and pick a template.", L"That id is already taken (or invalid).",
};
const wchar_t* const kStringsJP[static_cast<size_t>(EStr::Count)] = {
    L"キャラクター", L"名前", L"最大HP", L"前歩き速度", L"後ろ歩き速度", L"ダッシュ速度",
    L"ジャンプ初速(負=上方向)", L"重力",
    L"キャラクターを保存", L"+ 新規キャラクター", L"タイトルへ戻る",
    L"新規キャラID (例: ken)", L"表示名", L"技構成のコピー元", L"作成", L"キャンセル",
    L"技", L"表示名", L"発生", L"持続", L"硬直", L"ダメージ", L"ヒット硬直", L"ガード硬直", L"ヒットストップ",
    L"ボタン(通常技のみ)", L"キャンセル開始F", L"キャンセル終了F",
    L"必殺技コマンド(必殺技/超必殺技のみ)", L"クリア",
    L"判定が独自に移動する技(仕様のみ・未実装)", L"技を保存",
    L"当たり判定プレビュー(ドラッグで移動、角をドラッグでリサイズ)", L"操作対象:攻撃判定", L"操作対象:喰らい判定",
    L"攻撃判定 X/Y/W/H", L"+ 追加", L"- 削除",
    L"喰らい判定 X/Y/W/H", L"姿勢", L"部位",
    L"緑=喰らい判定  赤=攻撃判定", L"EN",
    L"モーション画像(参考用)", L"参照",
    L"キャラクターを保存しました。", L"技を保存しました。", L"新しいキャラクターを作成しました。", L"IDを入力し、コピー元を選んでください。", L"そのIDは既に使われています(または無効です)。",
};

const wchar_t* Str(EStr id, int lang) {
    return lang == 1 ? kStringsJP[static_cast<size_t>(id)] : kStringsEN[static_cast<size_t>(id)];
}

std::wstring PartDisplayName(const std::string& name, int lang) {
    static const std::pair<const char*, std::pair<const wchar_t*, const wchar_t*>> kPresets[] = {
        {"head", {L"HEAD", L"頭"}}, {"torso", {L"TORSO", L"胴体"}}, {"arm", {L"ARM", L"腕"}},
        {"hand", {L"HAND", L"手"}}, {"waist", {L"WAIST", L"腰"}}, {"leg", {L"LEG", L"足"}},
        {"foot", {L"FOOT", L"足先"}}, {"body", {L"BODY", L"全身"}},
    };
    for (const auto& p : kPresets) {
        if (name == p.first) return lang == 1 ? p.second.second : p.second.first;
    }
    return Utf8ToWide(name);
}

std::wstring StanceDisplayName(int stance, int lang) {
    if (lang == 1) return stance == 1 ? L"しゃがみ" : stance == 2 ? L"空中" : L"立ち";
    return stance == 1 ? L"CROUCH" : stance == 2 ? L"AIR" : L"STAND";
}

const char* StanceKey(int stance) { return stance == 1 ? "crouch" : stance == 2 ? "air" : "stand"; }

const char* const kPartPresets[] = {"head", "torso", "arm", "hand", "waist", "leg", "foot"};
const wchar_t* const kNormalButtons[] = {L"LP", L"MP", L"HP", L"LK", L"MK", L"HK"};
const wchar_t* const kAllButtons[] = {L"LP", L"MP", L"HP", L"LK", L"MK", L"HK", L"AnyP", L"AnyK"};

// ---------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------
enum class WType { Label, Field, Dropdown, Button, Check };
enum Mode { ModeAlways = 0, ModeNormal = 1, ModeCreate = 2 };

struct EdWidget {
    int id = 0;
    WType type = WType::Label;
    Gdiplus::RectF rect;
    EStr label = EStr::Count;       // translated caption (labels/buttons)
    std::wstring rawText;           // untranslated caption (digits, LP/MP...)
    bool primary = false;
    int mode = ModeAlways;
    bool readOnly = false;
    bool numeric = false;
    float dot = 1.5f;               // pixel-font size for labels/buttons
    std::function<bool()> enabled;  // optional; default true
    std::function<std::wstring()> getText;
    std::function<void(const std::wstring&)> setText;
    std::function<std::vector<std::wstring>()> items;
    std::function<int()> getSel;
    std::function<void(int)> setSel;
    std::function<void()> onClick;
    std::function<bool()> getChecked;
    std::function<void(bool)> setChecked;

    bool IsEnabled() const { return !enabled || enabled(); }
};

std::vector<EdWidget> g_Widgets;
int g_NextId = 1;

EdWidget& Add(EdWidget w) {
    w.id = g_NextId++;
    g_Widgets.push_back(std::move(w));
    return g_Widgets.back();
}

EdWidget* Find(int id) {
    for (auto& w : g_Widgets) if (w.id == id) return &w;
    return nullptr;
}

bool ModeVisible(const EdWidget& w, bool creating) {
    if (w.mode == ModeAlways) return true;
    return creating ? (w.mode == ModeCreate) : (w.mode == ModeNormal);
}

bool HasNonAscii(const std::wstring& text) {
    for (wchar_t c : text) if (c > 127) return true;
    return false;
}

// Pixel font for plain ASCII, system font otherwise (Japanese).
void DrawLabelText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::RectF& rect, float dot, Gdiplus::Color color, bool centered) {
    if (HasNonAscii(text)) {
        Gdiplus::Font font(UiFontFamily(), dot * 6.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (centered) DrawTextCentered(g, text, font, rect, color);
        else DrawTextLeft(g, text, font, rect.X, rect.Y + (rect.Height - dot * 7.0f) / 2.0f - 2.0f, color);
    } else {
        if (centered) DrawPixelTextCentered(g, text, rect, dot, color);
        else DrawPixelText(g, text, rect.X, rect.Y + (rect.Height - dot * 7.0f) / 2.0f, dot, color);
    }
}

std::wstring FormatDouble(double v) {
    std::wstringstream ss; ss << v; return ss.str();
}
bool ParseDouble(const std::wstring& s, double& out) {
    try { size_t idx = 0; double v = std::stod(s, &idx); if (idx == 0) return false; out = v; return true; } catch (...) { return false; }
}
bool ParseInt(const std::wstring& s, int& out) {
    try { size_t idx = 0; int v = std::stoi(s, &idx); if (idx == 0) return false; out = v; return true; } catch (...) { return false; }
}

// Binding helpers: a numeric field over a double/int in a draft. `clampHi`
// caps frame-count fields at 99 (two digits, per the user).
EdWidget FieldDouble(float x, float y, float w, double* ref) {
    EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(x, y, w, 26); f.numeric = true; f.mode = ModeNormal;
    f.getText = [ref]() { return FormatDouble(*ref); };
    f.setText = [ref](const std::wstring& s) { double v; if (ParseDouble(s, v)) *ref = v; };
    return f;
}
EdWidget FieldInt(float x, float y, float w, int* ref, int lo = -9999, int hi = 99999) {
    EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(x, y, w, 26); f.numeric = true; f.mode = ModeNormal;
    f.getText = [ref]() { return std::to_wstring(*ref); };
    f.setText = [ref, lo, hi](const std::wstring& s) { int v; if (ParseInt(s, v)) *ref = std::clamp(v, lo, hi); };
    return f;
}
EdWidget FieldString(float x, float y, float w, std::string* ref, int mode = ModeNormal) {
    EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(x, y, w, 26); f.mode = mode;
    f.getText = [ref]() { return Utf8ToWide(*ref); };
    f.setText = [ref](const std::wstring& s) { *ref = WideToUtf8(s); };
    return f;
}
EdWidget Label(float x, float y, float w, EStr s, int mode = ModeNormal) {
    EdWidget l; l.type = WType::Label; l.rect = Gdiplus::RectF(x, y, w, 20); l.label = s; l.mode = mode;
    return l;
}
EdWidget Button(float x, float y, float w, float h, EStr s, bool primary, std::function<void()> onClick, int mode = ModeNormal) {
    EdWidget b; b.type = WType::Button; b.rect = Gdiplus::RectF(x, y, w, h); b.label = s; b.primary = primary; b.onClick = std::move(onClick); b.mode = mode;
    b.dot = h >= 34 ? 2.0f : 1.5f;
    return b;
}
EdWidget ButtonRaw(float x, float y, float w, float h, const std::wstring& text, std::function<void()> onClick) {
    EdWidget b; b.type = WType::Button; b.rect = Gdiplus::RectF(x, y, w, h); b.rawText = text; b.onClick = std::move(onClick); b.mode = ModeNormal;
    b.dot = h >= 34 ? 2.0f : 1.5f;
    return b;
}
EdWidget Dropdown(float x, float y, float w, std::function<std::vector<std::wstring>()> items, std::function<int()> getSel, std::function<void(int)> setSel, int mode = ModeNormal) {
    EdWidget d; d.type = WType::Dropdown; d.rect = Gdiplus::RectF(x, y, w, 28); d.items = std::move(items); d.getSel = std::move(getSel); d.setSel = std::move(setSel); d.mode = mode;
    return d;
}

constexpr float kDropRowH = 26.0f;
constexpr int kDropMaxRows = 20;

Gdiplus::RectF DropdownListRect(const EdWidget& d) {
    int n = d.items ? static_cast<int>(d.items().size()) : 0;
    int rows = std::min(n, kDropMaxRows);
    return Gdiplus::RectF(d.rect.X, d.rect.Y + d.rect.Height, d.rect.Width, rows * kDropRowH + 4.0f);
}

bool Inside(const Gdiplus::RectF& r, int x, int y) {
    return x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height;
}

// Current selection accessors for the two draft lists.
RectBox& HurtboxForStance(HurtboxSet& set, int stance, int partIndex) {
    auto& parts = set.PartsForStance(StanceKey(stance));
    if (parts.empty()) parts.push_back({"body", RectBox{0, -40, 30, 80}});
    if (partIndex < 0 || partIndex >= static_cast<int>(parts.size())) partIndex = 0;
    return parts[partIndex].Box;
}

} // namespace

// ---------------------------------------------------------------------
// Form build
// ---------------------------------------------------------------------
static void BuildEditorWidgets(App& app) {
    g_Widgets.clear();
    g_NextId = 1;
    const float col1 = 40, col2 = 420, col3 = 800, fieldW = 320;
    const float thirdW = 96, thirdGap = 16;

    // Language toggle (top-right, always visible).
    {
        EdWidget b = Button(1600 - 40 - 70, 16, 70, 30, EStr::LanguageBtn, false, [&app]() { app.EditorLanguage = app.EditorLanguage == 1 ? 0 : 1; }, ModeAlways);
        Add(b);
    }

    // ---- Column 1: character stats ----
    Add(Label(col1, 90, fieldW, EStr::Character));
    Add(Dropdown(col1, 112, fieldW,
        [&app]() {
            std::vector<std::wstring> out;
            for (const auto& id : app.Dm->GetCharacterIds()) {
                const auto* st = app.Dm->GetCharacter(id);
                out.push_back(Utf8ToWide((st ? st->Name : id) + " (" + id + ")"));
            }
            return out;
        },
        [&app]() {
            const auto& ids = app.Dm->GetCharacterIds();
            for (size_t i = 0; i < ids.size(); i++) if (ids[i] == app.EditorCharId) return static_cast<int>(i);
            return 0;
        },
        [&app](int i) { const auto& ids = app.Dm->GetCharacterIds(); if (i >= 0 && i < static_cast<int>(ids.size())) app.LoadCharacterIntoForm(ids[i]); }));
    Add(Label(col1, 150, fieldW, EStr::Name));
    Add(FieldString(col1, 172, fieldW, &app.EditorStatsDraft.Name));
    Add(Label(col1, 210, fieldW, EStr::MaxHp));
    Add(FieldInt(col1, 232, fieldW, &app.EditorStatsDraft.MaxHP, 1, 99999));
    Add(Label(col1, 270, fieldW, EStr::WalkFwd));
    Add(FieldDouble(col1, 292, fieldW, &app.EditorStatsDraft.WalkForwardSpeed));
    Add(Label(col1, 330, fieldW, EStr::WalkBack));
    Add(FieldDouble(col1, 352, fieldW, &app.EditorStatsDraft.WalkBackwardSpeed));
    Add(Label(col1, 390, fieldW, EStr::DashSpeed));
    Add(FieldDouble(col1, 412, fieldW, &app.EditorStatsDraft.DashSpeed));
    Add(Label(col1, 450, fieldW, EStr::JumpVel));
    Add(FieldDouble(col1, 472, fieldW, &app.EditorStatsDraft.JumpVelocity));
    Add(Label(col1, 510, fieldW, EStr::Gravity));
    Add(FieldDouble(col1, 532, fieldW, &app.EditorStatsDraft.Gravity));
    Add(Button(col1, 580, fieldW, 44, EStr::SaveCharacter, true, [&app]() { app.ApplyStatsForm(); app.EditorSetStatus(Str(EStr::SavedCharacter, app.EditorLanguage)); }));
    Add(Button(col1, 634, fieldW, 44, EStr::NewCharacter, false, [&app]() { app.EditorBlur(); app.EditorCreatingNew = true; app.EditorNewId.clear(); app.EditorNewName.clear(); app.EditorTemplateIndex = 0; }));

    // ---- Create-new-character prompt (replaces column 1's contents) ----
    Add(Label(col1, 90, fieldW, EStr::NewCharId, ModeCreate));
    Add(FieldString(col1, 112, fieldW, &app.EditorNewId, ModeCreate));
    Add(Label(col1, 150, fieldW, EStr::DisplayName, ModeCreate));
    Add(FieldString(col1, 172, fieldW, &app.EditorNewName, ModeCreate));
    Add(Label(col1, 210, fieldW, EStr::CloneFrom, ModeCreate));
    Add(Dropdown(col1, 232, fieldW,
        [&app]() {
            std::vector<std::wstring> out;
            for (const auto& id : app.Dm->GetCharacterIds()) {
                const auto* st = app.Dm->GetCharacter(id);
                out.push_back(Utf8ToWide((st ? st->Name : id) + " (" + id + ")"));
            }
            return out;
        },
        [&app]() { return app.EditorTemplateIndex; },
        [&app](int i) { app.EditorTemplateIndex = i; }, ModeCreate));
    Add(Button(col1, 280, fieldW, 44, EStr::Create, true, [&app]() {
        app.EditorBlur();
        const auto& ids = app.Dm->GetCharacterIds();
        if (app.EditorNewId.empty() || app.EditorTemplateIndex < 0 || app.EditorTemplateIndex >= static_cast<int>(ids.size())) {
            app.EditorSetStatus(Str(EStr::NeedIdAndTemplate, app.EditorLanguage));
            return;
        }
        std::string templateId = ids[app.EditorTemplateIndex];
        if (!app.Dm->CreateCharacter(app.EditorNewId, app.EditorNewName, templateId)) {
            app.EditorSetStatus(Str(EStr::IdTaken, app.EditorLanguage));
            return;
        }
        app.EditorCreatingNew = false;
        app.LoadCharacterIntoForm(app.EditorNewId);
        app.EditorSetStatus(Str(EStr::CreatedCharacter, app.EditorLanguage));
    }, ModeCreate));
    Add(Button(col1, 334, fieldW, 44, EStr::CancelBtn, false, [&app]() { app.EditorBlur(); app.EditorCreatingNew = false; }, ModeCreate));

    // Motion reference image (column 1, below + NEW CHARACTER).
    Add(Label(col1, 700, fieldW, EStr::MotionImageLabel));
    {
        EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(col1, 722, 170, 26); f.readOnly = true; f.mode = ModeNormal;
        f.getText = [&app]() { return app.EditorMotionImagePath.empty() ? std::wstring() : Utf8ToWide(fs::path(app.EditorMotionImagePath).filename().string()); };
        Add(f);
    }
    Add(Button(col1 + 178, 722, 68, 26, EStr::BrowseBtn, false, [&app]() { app.BrowseMotionImage(); }));
    Add(Button(col1 + 252, 722, 68, 26, EStr::ClearBtn, false, [&app]() { app.EditorMotionImagePath.clear(); }));
    app.EditorMotionImageRect = Gdiplus::RectF(col1, 756.0f, fieldW, 136.0f);

    Add(Button(col1, 900, fieldW, 44, EStr::BackToTitle, false, [&app]() { app.GoTo(Screen::Title); }, ModeAlways));

    // ---- Column 2: move editor ----
    const float rx = col2;
    Add(Label(rx, 90, fieldW, EStr::Move));
    Add(Dropdown(rx, 112, fieldW,
        [&app]() {
            std::vector<std::wstring> out;
            for (const auto& id : app.EditorMoveIds) {
                const auto* m = app.Dm->GetMove(app.EditorCharId, id);
                out.push_back(m ? Utf8ToWide(m->Name) : Utf8ToWide(id));
            }
            return out;
        },
        [&app]() {
            for (size_t i = 0; i < app.EditorMoveIds.size(); i++) if (app.EditorMoveIds[i] == app.EditorMoveId) return static_cast<int>(i);
            return 0;
        },
        [&app](int i) { if (i >= 0 && i < static_cast<int>(app.EditorMoveIds.size())) app.LoadMoveIntoForm(app.EditorMoveIds[i]); }));
    Add(Label(rx, 150, fieldW, EStr::MoveDisplayName));
    Add(FieldString(rx, 172, fieldW, &app.EditorMoveDraft.Name));
    Add(Label(rx, 210, thirdW, EStr::Startup));
    Add(Label(rx + thirdW + thirdGap, 210, thirdW, EStr::Active));
    Add(Label(rx + (thirdW + thirdGap) * 2, 210, thirdW, EStr::Recovery));
    Add(FieldInt(rx, 232, thirdW, &app.EditorMoveDraft.Startup, 0, 99));
    Add(FieldInt(rx + thirdW + thirdGap, 232, thirdW, &app.EditorMoveDraft.Active, 0, 99));
    Add(FieldInt(rx + (thirdW + thirdGap) * 2, 232, thirdW, &app.EditorMoveDraft.Recovery, 0, 99));
    Add(Label(rx, 270, fieldW, EStr::Damage));
    Add(FieldInt(rx, 292, fieldW, &app.EditorMoveDraft.Damage, 0, 99999));
    Add(Label(rx, 330, thirdW, EStr::Hitstun));
    Add(Label(rx + thirdW + thirdGap, 330, thirdW, EStr::Blockstun));
    Add(Label(rx + (thirdW + thirdGap) * 2, 330, thirdW, EStr::Hitstop));
    Add(FieldInt(rx, 352, thirdW, &app.EditorMoveDraft.Hitstun, 0, 99));
    Add(FieldInt(rx + thirdW + thirdGap, 352, thirdW, &app.EditorMoveDraft.Blockstun, 0, 99));
    Add(FieldInt(rx + (thirdW + thirdGap) * 2, 352, thirdW, &app.EditorMoveDraft.Hitstop, 0, 99));
    // Frame advantage readout (computed live from the draft).
    {
        EdWidget l; l.type = WType::Label; l.rect = Gdiplus::RectF(rx, 400, fieldW, 20); l.mode = ModeNormal;
        l.getText = [&app]() {
            int onHit = app.EditorMoveDraft.Hitstun - app.EditorMoveDraft.Recovery;
            int onBlock = app.EditorMoveDraft.Blockstun - app.EditorMoveDraft.Recovery;
            return app.EditorLanguage == 1
                ? (L"ヒット時: " + std::to_wstring(onHit) + L"  ガード時: " + std::to_wstring(onBlock))
                : (L"ON-HIT: " + std::to_wstring(onHit) + L"  ON-BLOCK: " + std::to_wstring(onBlock));
        };
        Add(l);
    }

    Add(Label(rx, 450, fieldW, EStr::ButtonLabel));
    {
        EdWidget d = Dropdown(rx, 472, thirdW * 2 + thirdGap,
            []() { return std::vector<std::wstring>(std::begin(kNormalButtons), std::end(kNormalButtons)); },
            [&app]() { for (int i = 0; i < 6; i++) if (app.EditorMoveDraft.Button == WideToUtf8(kNormalButtons[i])) return i; return -1; },
            [&app](int i) { if (i >= 0 && i < 6) app.EditorMoveDraft.Button = WideToUtf8(kNormalButtons[i]); });
        d.enabled = [&app]() { return app.EditorMoveDraft.HasTag("Normal"); };
        Add(d);
    }
    Add(Label(rx, 510, thirdW, EStr::CancelStart));
    Add(Label(rx + thirdW + thirdGap, 510, thirdW, EStr::CancelEnd));
    Add(FieldInt(rx, 532, thirdW, &app.EditorMoveDraft.CancelStartFrame, 0, 99));
    Add(FieldInt(rx + thirdW + thirdGap, 532, thirdW, &app.EditorMoveDraft.CancelEndFrame, 0, 99));

    // Special-move command builder (numpad grid + preview + button).
    Add(Label(rx, 578, fieldW, EStr::CommandLabel));
    static const int kPadLayout[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
    const float padCell = 34, padGap = 4;
    auto specialEnabled = [&app]() { return app.EditorMoveDraft.HasTag("Special") || app.EditorMoveDraft.HasTag("Super"); };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int digit = kPadLayout[row][col];
            EdWidget b = ButtonRaw(rx + col * (padCell + padGap), 602 + row * (padCell + padGap), padCell, padCell, std::to_wstring(digit),
                                   [&app, digit]() { app.EditorCommandDigits += std::to_string(digit); });
            b.enabled = specialEnabled;
            Add(b);
        }
    }
    const float padRightX = rx + 3 * (padCell + padGap) + 16;
    {
        EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(padRightX, 602, fieldW - (padRightX - rx), 26); f.readOnly = true; f.mode = ModeNormal;
        f.getText = [&app]() { return Utf8ToWide(app.EditorCommandDigits); };
        Add(f);
    }
    {
        EdWidget b = Button(padRightX, 636, 100, 30, EStr::ClearBtn, false, [&app]() { app.EditorCommandDigits.clear(); });
        b.enabled = specialEnabled;
        Add(b);
    }
    {
        EdWidget d = Dropdown(padRightX, 672, fieldW - (padRightX - rx),
            []() { return std::vector<std::wstring>(std::begin(kAllButtons), std::end(kAllButtons)); },
            [&app]() { for (int i = 0; i < 8; i++) if (app.EditorMoveDraft.Button == WideToUtf8(kAllButtons[i])) return i; return -1; },
            [&app](int i) { if (i >= 0 && i < 8) app.EditorMoveDraft.Button = WideToUtf8(kAllButtons[i]); });
        d.enabled = specialEnabled;
        Add(d);
    }
    {
        EdWidget c; c.type = WType::Check; c.rect = Gdiplus::RectF(rx, 714, fieldW, 24); c.label = EStr::MovingHitbox; c.mode = ModeNormal;
        c.getChecked = [&app]() { return app.EditorMoveDraft.HasDynamicHitbox; };
        c.setChecked = [&app](bool v) { app.EditorMoveDraft.HasDynamicHitbox = v; };
        Add(c);
    }
    Add(Button(rx, 764, fieldW, 44, EStr::SaveMove, true, [&app]() { app.ApplyMoveForm(); app.EditorSetStatus(Str(EStr::SavedMove, app.EditorLanguage)); }));

    // ---- Column 3: hitbox/hurtbox editor ----
    const float px = col3;
    const float previewSize = 520;
    Add(Label(px, 90, 700, EStr::PreviewTitle));
    app.EditorPreviewRect = Gdiplus::RectF(px, 116.0f, previewSize, previewSize);
    const float belowPreview = 116 + previewSize + 20;
    const float quarterW = 130, quarterGap = 16;
    {
        EdWidget b = Button(px, belowPreview, 250, 36, EStr::DragHitbox, false, [&app]() { app.EditorDragTargetIsHurtbox = false; });
        Add(b);
        EdWidget b2 = Button(px + 266, belowPreview, 250, 36, EStr::DragHurtbox, false, [&app]() { app.EditorDragTargetIsHurtbox = true; });
        Add(b2);
    }
    const float hbListY = belowPreview + 48;
    Add(Dropdown(px, hbListY, 180,
        [&app]() {
            std::vector<std::wstring> out;
            for (size_t i = 0; i < app.EditorHitboxDraftList.size(); i++) out.push_back((app.EditorLanguage == 1 ? L"判定 " : L"HITBOX ") + std::to_wstring(i + 1));
            return out;
        },
        [&app]() { return app.EditorHitboxIndex; },
        [&app](int i) { app.EditorHitboxIndex = i; }));
    Add(Button(px + 194, hbListY, 90, 28, EStr::AddBtn, false, [&app]() {
        app.EditorHitboxDraftList.push_back(HitboxDef{});
        app.EditorHitboxIndex = static_cast<int>(app.EditorHitboxDraftList.size()) - 1;
    }));
    Add(Button(px + 292, hbListY, 90, 28, EStr::RemoveBtn, false, [&app]() {
        if (app.EditorHitboxIndex >= 0 && app.EditorHitboxIndex < static_cast<int>(app.EditorHitboxDraftList.size())) {
            app.EditorHitboxDraftList.erase(app.EditorHitboxDraftList.begin() + app.EditorHitboxIndex);
            if (app.EditorHitboxIndex >= static_cast<int>(app.EditorHitboxDraftList.size())) app.EditorHitboxIndex = static_cast<int>(app.EditorHitboxDraftList.size()) - 1;
        }
    }));
    const float hitRowY = hbListY + 36;
    Add(Label(px, hitRowY, 400, EStr::HitboxSection));
    {
        // X/Y/W/H fields over the currently selected hitbox - resolved at
        // access time, so switching the selection needs no re-binding.
        auto hb = [&app]() -> HitboxDef* {
            if (app.EditorHitboxIndex < 0 || app.EditorHitboxIndex >= static_cast<int>(app.EditorHitboxDraftList.size())) return nullptr;
            return &app.EditorHitboxDraftList[app.EditorHitboxIndex];
        };
        auto makeField = [&](float x, int which) {
            EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(x, hitRowY + 22, quarterW, 26); f.numeric = true; f.mode = ModeNormal;
            f.enabled = [hb]() { return hb() != nullptr; };
            f.getText = [hb, which]() {
                HitboxDef* b = hb(); if (!b) return std::wstring();
                double v = which == 0 ? b->offsetX : which == 1 ? b->offsetY : which == 2 ? b->width : b->height;
                return FormatDouble(v);
            };
            f.setText = [hb, which, &app](const std::wstring& s) {
                HitboxDef* b = hb(); double v; if (!b || !ParseDouble(s, v)) return;
                double x = b->offsetX, y = b->offsetY, w = b->width, h = b->height;
                (which == 0 ? x : which == 1 ? y : which == 2 ? w : h) = v;
                app.ClampBoxToPreview(x, y, w, h);
                b->offsetX = x; b->offsetY = y; b->width = w; b->height = h;
            };
            Add(f);
        };
        makeField(px, 0); makeField(px + (quarterW + quarterGap), 1); makeField(px + (quarterW + quarterGap) * 2, 2); makeField(px + (quarterW + quarterGap) * 3, 3);
    }

    const float stanceRowY = hitRowY + 66;
    Add(Dropdown(px, stanceRowY, 120,
        [&app]() { return std::vector<std::wstring>{StanceDisplayName(0, app.EditorLanguage), StanceDisplayName(1, app.EditorLanguage), StanceDisplayName(2, app.EditorLanguage)}; },
        [&app]() { return app.EditorHurtStance; },
        [&app](int i) { app.EditorHurtStance = i; app.EditorHurtPartIndex = 0; }));
    Add(Dropdown(px + 134, stanceRowY, 180,
        [&app]() {
            std::vector<std::wstring> out;
            for (const auto& part : app.EditorHurtboxDraft.PartsForStance(StanceKey(app.EditorHurtStance))) out.push_back(PartDisplayName(part.Name, app.EditorLanguage));
            return out;
        },
        [&app]() { return app.EditorHurtPartIndex; },
        [&app](int i) { app.EditorHurtPartIndex = i; }));
    Add(Button(px + 328, stanceRowY, 90, 28, EStr::AddBtn, false, [&app]() {
        auto& parts = app.EditorHurtboxDraft.PartsForStance(StanceKey(app.EditorHurtStance));
        std::string newName;
        for (const char* preset : kPartPresets) {
            bool taken = false;
            for (const auto& p : parts) if (p.Name == preset) { taken = true; break; }
            if (!taken) { newName = preset; break; }
        }
        if (newName.empty()) newName = "part" + std::to_string(parts.size() + 1);
        parts.push_back({newName, RectBox{0, -40, 20, 30}});
        app.EditorHurtPartIndex = static_cast<int>(parts.size()) - 1;
    }));
    Add(Button(px + 426, stanceRowY, 90, 28, EStr::RemoveBtn, false, [&app]() {
        auto& parts = app.EditorHurtboxDraft.PartsForStance(StanceKey(app.EditorHurtStance));
        if (parts.size() > 1 && app.EditorHurtPartIndex >= 0 && app.EditorHurtPartIndex < static_cast<int>(parts.size())) {
            parts.erase(parts.begin() + app.EditorHurtPartIndex);
            if (app.EditorHurtPartIndex >= static_cast<int>(parts.size())) app.EditorHurtPartIndex = static_cast<int>(parts.size()) - 1;
        }
    }));
    const float hurtRowY = stanceRowY + 36;
    Add(Label(px, hurtRowY, 400, EStr::HurtboxSection));
    {
        auto makeField = [&](float x, int which) {
            EdWidget f; f.type = WType::Field; f.rect = Gdiplus::RectF(x, hurtRowY + 22, quarterW, 26); f.numeric = true; f.mode = ModeNormal;
            f.getText = [&app, which]() {
                RectBox& b = HurtboxForStance(app.EditorHurtboxDraft, app.EditorHurtStance, app.EditorHurtPartIndex);
                double v = which == 0 ? b.CenterX : which == 1 ? b.CenterY : which == 2 ? b.Width : b.Height;
                return FormatDouble(v);
            };
            f.setText = [&app, which](const std::wstring& s) {
                double v; if (!ParseDouble(s, v)) return;
                RectBox& b = HurtboxForStance(app.EditorHurtboxDraft, app.EditorHurtStance, app.EditorHurtPartIndex);
                double x = b.CenterX, y = b.CenterY, w = b.Width, h = b.Height;
                (which == 0 ? x : which == 1 ? y : which == 2 ? w : h) = v;
                app.ClampBoxToPreview(x, y, w, h);
                b.CenterX = x; b.CenterY = y; b.Width = w; b.Height = h;
            };
            Add(f);
        };
        makeField(px, 0); makeField(px + (quarterW + quarterGap), 1); makeField(px + (quarterW + quarterGap) * 2, 2); makeField(px + (quarterW + quarterGap) * 3, 3);
    }
}

// ---------------------------------------------------------------------
// Enter / leave
// ---------------------------------------------------------------------
void App::EnterEditor() {
    GetWindowRect(Hwnd, &PreEditorWindowRect);
    EditorSizeSaved = true;
    RECT rc{0, 0, 1600, 980};
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtr(Hwnd, GWL_STYLE)), FALSE);
    SetWindowPos(Hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    BuildEditorWidgets(*this);
    EditorFocusId = -1;
    EditorOpenDropdown = -1;
    EditorHoverId = -1;
    EditorStatus.clear();
    // EditorCreatingNew may already be true if Character Select routed us
    // here via the "+ ADD CHARACTER" tile - respect that starting mode.
    if (EditorCreatingNew) { EditorNewId.clear(); EditorNewName.clear(); EditorTemplateIndex = 0; }
    if (!Dm->GetCharacterIds().empty()) {
        std::string id = EditorCharId;
        bool known = false;
        for (const auto& cid : Dm->GetCharacterIds()) if (cid == id) known = true;
        LoadCharacterIntoForm(known ? id : Dm->GetCharacterIds()[0]);
    }
    InvalidateRect(Hwnd, nullptr, TRUE);
}

void App::LeaveEditor() {
    EditorBlur();
    g_Widgets.clear();
    if (EditorSizeSaved) {
        SetWindowPos(Hwnd, nullptr, 0, 0, PreEditorWindowRect.right - PreEditorWindowRect.left,
                     PreEditorWindowRect.bottom - PreEditorWindowRect.top, SWP_NOMOVE | SWP_NOZORDER);
        EditorSizeSaved = false;
    }
    EditorCreatingNew = false;
}

void App::EditorSetStatus(const std::wstring& text) {
    EditorStatus = text;
    EditorStatusUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    InvalidateRect(Hwnd, nullptr, FALSE);
}

// Drops keyboard focus (the field's value has already been written on
// every keystroke, so nothing to commit) and closes any open dropdown.
void App::EditorBlur() {
    EditorFocusId = -1;
    EditorEditBuffer.clear();
    EditorCaret = 0;
    EditorOpenDropdown = -1;
}

// ---------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------
void App::LoadCharacterIntoForm(const std::string& charId) {
    EditorBlur();
    EditorCharId = charId;
    const auto* stats = Dm->GetCharacter(charId);
    if (!stats) return;
    EditorStatsDraft = *stats;
    EditorHurtboxDraft = stats->Hurtboxes;
    EditorHurtStance = 0;
    EditorHurtPartIndex = 0;

    EditorMoveIds.clear();
    if (const auto* moves = Dm->GetMoveset(charId)) {
        for (const auto& kv : *moves) EditorMoveIds.push_back(kv.first);
        std::sort(EditorMoveIds.begin(), EditorMoveIds.end());
    }
    if (!EditorMoveIds.empty()) LoadMoveIntoForm(EditorMoveIds[0]);
    else { EditorMoveId.clear(); EditorMoveDraft = MoveData(); EditorHitboxDraftList.clear(); }
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::LoadMoveIntoForm(const std::string& moveId) {
    EditorBlur();
    EditorMoveId = moveId;
    const auto* move = Dm->GetMove(EditorCharId, moveId);
    if (!move) return;
    EditorMoveDraft = *move;
    EditorCommandDigits = move->InputCommand;
    EditorMotionImagePath = move->MotionImagePath;
    EditorHitboxDraftList = move->Hitboxes;
    EditorHitboxIndex = 0;
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::ApplyStatsForm() {
    EditorBlur();
    if (EditorCharId.empty()) return;
    const auto* existing = Dm->GetCharacter(EditorCharId);
    if (!existing) return;
    CharacterStats stats = *existing;
    stats.Name = EditorStatsDraft.Name;
    stats.MaxHP = EditorStatsDraft.MaxHP;
    stats.WalkForwardSpeed = EditorStatsDraft.WalkForwardSpeed;
    stats.WalkBackwardSpeed = EditorStatsDraft.WalkBackwardSpeed;
    stats.DashSpeed = EditorStatsDraft.DashSpeed;
    stats.JumpVelocity = EditorStatsDraft.JumpVelocity;
    stats.Gravity = EditorStatsDraft.Gravity;
    stats.Hurtboxes = EditorHurtboxDraft;
    Dm->SaveCharacter(stats);
}

void App::ApplyMoveForm() {
    EditorBlur();
    if (EditorCharId.empty() || EditorMoveId.empty()) return;
    const auto* existing = Dm->GetMove(EditorCharId, EditorMoveId);
    if (!existing) return;
    MoveData move = *existing;
    move.Name = EditorMoveDraft.Name;
    move.Startup = std::clamp(EditorMoveDraft.Startup, 0, 99);
    move.Active = std::clamp(EditorMoveDraft.Active, 0, 99);
    move.Recovery = std::clamp(EditorMoveDraft.Recovery, 0, 99);
    move.TotalFrame = move.Startup + move.Active + move.Recovery;
    move.Damage = EditorMoveDraft.Damage;
    move.Hitstun = EditorMoveDraft.Hitstun;
    move.Blockstun = EditorMoveDraft.Blockstun;
    move.Hitstop = EditorMoveDraft.Hitstop;
    move.CancelStartFrame = EditorMoveDraft.CancelStartFrame;
    move.CancelEndFrame = EditorMoveDraft.CancelEndFrame;
    bool isNormal = move.HasTag("Normal");
    bool isSpecialOrSuper = move.HasTag("Special") || move.HasTag("Super");
    if (isNormal || isSpecialOrSuper) move.Button = EditorMoveDraft.Button;
    if (isSpecialOrSuper) move.InputCommand = EditorCommandDigits;
    move.HasDynamicHitbox = EditorMoveDraft.HasDynamicHitbox;
    move.MotionImagePath = EditorMotionImagePath;
    move.Hitboxes = EditorHitboxDraftList;
    Dm->SaveMove(EditorCharId, move);
    EditorMoveDraft = move;
}

// ---------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------
static void FocusField(App& app, EdWidget& w) {
    app.EditorFocusId = w.id;
    app.EditorEditBuffer = w.getText ? w.getText() : std::wstring();
    app.EditorCaret = static_cast<int>(app.EditorEditBuffer.size());
}

static void CommitBuffer(App& app) {
    EdWidget* w = Find(app.EditorFocusId);
    if (w && w->setText && !w->readOnly) w->setText(app.EditorEditBuffer);
}

bool App::EditorMouseDown(int x, int y) {
    EditorMouseX = x; EditorMouseY = y;
    // An open dropdown list eats the click first: pick a row or close.
    if (EditorOpenDropdown >= 0) {
        EdWidget* d = Find(EditorOpenDropdown);
        if (d) {
            Gdiplus::RectF list = DropdownListRect(*d);
            if (Inside(list, x, y)) {
                int row = static_cast<int>((y - list.Y - 2) / kDropRowH) + EditorDropdownScroll;
                auto items = d->items ? d->items() : std::vector<std::wstring>();
                if (row >= 0 && row < static_cast<int>(items.size()) && d->setSel) d->setSel(row);
                EditorOpenDropdown = -1;
                InvalidateRect(Hwnd, nullptr, FALSE);
                return true;
            }
        }
        EditorOpenDropdown = -1;
        InvalidateRect(Hwnd, nullptr, FALSE);
        return true;
    }

    for (auto& w : g_Widgets) {
        if (!ModeVisible(w, EditorCreatingNew) || w.type == WType::Label) continue;
        if (!Inside(w.rect, x, y)) continue;
        if (!w.IsEnabled()) return true;
        if (EditorFocusId != w.id) { CommitBuffer(*this); EditorFocusId = -1; }
        switch (w.type) {
            case WType::Field:
                if (!w.readOnly) FocusField(*this, w);
                break;
            case WType::Dropdown:
                EditorOpenDropdown = w.id;
                EditorDropdownScroll = 0;
                if (w.getSel) {
                    int sel = w.getSel();
                    if (sel >= kDropMaxRows) EditorDropdownScroll = sel - kDropMaxRows + 1;
                }
                break;
            case WType::Button:
                if (w.onClick) w.onClick();
                break;
            case WType::Check:
                if (w.setChecked && w.getChecked) w.setChecked(!w.getChecked());
                break;
            default: break;
        }
        InvalidateRect(Hwnd, nullptr, FALSE);
        return true;
    }
    // Click on empty space: drop focus.
    if (EditorFocusId >= 0) { CommitBuffer(*this); EditorFocusId = -1; InvalidateRect(Hwnd, nullptr, FALSE); }
    return false;
}

void App::EditorMouseMove(int x, int y) {
    EditorMouseX = x; EditorMouseY = y;
    int hover = -1;
    for (auto& w : g_Widgets) {
        if (!ModeVisible(w, EditorCreatingNew) || w.type == WType::Label) continue;
        if (Inside(w.rect, x, y)) { hover = w.id; break; }
    }
    bool listHover = false;
    if (EditorOpenDropdown >= 0) {
        if (EdWidget* d = Find(EditorOpenDropdown)) listHover = Inside(DropdownListRect(*d), x, y);
    }
    if (hover != EditorHoverId || listHover) {
        EditorHoverId = hover;
        InvalidateRect(Hwnd, nullptr, FALSE);
    }
}

void App::EditorWheel(int delta) {
    if (EditorOpenDropdown < 0) return;
    EdWidget* d = Find(EditorOpenDropdown);
    if (!d || !d->items) return;
    int n = static_cast<int>(d->items().size());
    int maxScroll = std::max(0, n - kDropMaxRows);
    EditorDropdownScroll = std::clamp(EditorDropdownScroll - (delta > 0 ? 3 : -3), 0, maxScroll);
    InvalidateRect(Hwnd, nullptr, FALSE);
}

static void FocusNextField(App& app, int dir) {
    std::vector<EdWidget*> fields;
    for (auto& w : g_Widgets) {
        if (w.type == WType::Field && !w.readOnly && ModeVisible(w, app.EditorCreatingNew) && w.IsEnabled()) fields.push_back(&w);
    }
    if (fields.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < fields.size(); i++) if (fields[i]->id == app.EditorFocusId) idx = static_cast<int>(i);
    int next = (idx + dir + static_cast<int>(fields.size())) % static_cast<int>(fields.size());
    if (idx < 0) next = dir > 0 ? 0 : static_cast<int>(fields.size()) - 1;
    CommitBuffer(app);
    FocusField(app, *fields[next]);
}

bool App::EditorKeyDown(int vk) {
    if (vk == VK_ESCAPE) {
        if (EditorOpenDropdown >= 0 || EditorFocusId >= 0) { CommitBuffer(*this); EditorBlur(); InvalidateRect(Hwnd, nullptr, FALSE); }
        return true;
    }
    if (vk == VK_TAB) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        FocusNextField(*this, shift ? -1 : 1);
        InvalidateRect(Hwnd, nullptr, FALSE);
        return true;
    }
    if (EditorFocusId < 0) return false;
    EdWidget* w = Find(EditorFocusId);
    if (!w) return false;
    std::wstring& buf = EditorEditBuffer;
    int& caret = EditorCaret;
    caret = std::clamp(caret, 0, static_cast<int>(buf.size()));
    switch (vk) {
        case VK_RETURN: CommitBuffer(*this); EditorFocusId = -1; break;
        case VK_BACK: if (caret > 0) { buf.erase(caret - 1, 1); caret--; CommitBuffer(*this); } break;
        case VK_DELETE: if (caret < static_cast<int>(buf.size())) { buf.erase(caret, 1); CommitBuffer(*this); } break;
        case VK_LEFT: if (caret > 0) caret--; break;
        case VK_RIGHT: if (caret < static_cast<int>(buf.size())) caret++; break;
        case VK_HOME: caret = 0; break;
        case VK_END: caret = static_cast<int>(buf.size()); break;
        default: return false;
    }
    InvalidateRect(Hwnd, nullptr, FALSE);
    return true;
}

void App::EditorChar(wchar_t c) {
    if (EditorFocusId < 0 || c < 32) return;
    EdWidget* w = Find(EditorFocusId);
    if (!w || w->readOnly) return;
    if (w->numeric) {
        bool ok = (c >= L'0' && c <= L'9') || c == L'-' || c == L'.';
        if (!ok) return;
    }
    if (EditorEditBuffer.size() >= 64) return;
    EditorCaret = std::clamp(EditorCaret, 0, static_cast<int>(EditorEditBuffer.size()));
    EditorEditBuffer.insert(EditorEditBuffer.begin() + EditorCaret, c);
    EditorCaret++;
    CommitBuffer(*this);
    InvalidateRect(Hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------
static void DrawWidget(Gdiplus::Graphics& g, App& app, const EdWidget& w) {
    const auto& pal = GetPalette();
    bool enabled = w.IsEnabled();
    bool hover = enabled && app.EditorHoverId == w.id;
    std::wstring caption = w.rawText.empty() && w.label != EStr::Count ? Str(w.label, app.EditorLanguage) : w.rawText;

    switch (w.type) {
        case WType::Label: {
            std::wstring text = w.getText ? w.getText() : caption;
            DrawLabelText(g, text, w.rect, 1.5f, pal.Ink, false);
            break;
        }
        case WType::Field: {
            bool focused = app.EditorFocusId == w.id;
            Gdiplus::SolidBrush bg(w.readOnly ? pal.PanelBg2 : pal.PanelBg);
            g.FillRectangle(&bg, w.rect);
            Gdiplus::Pen border(focused ? pal.Accent : (enabled ? pal.Ink : pal.Ink45), focused ? 2.0f : 1.0f);
            g.DrawRectangle(&border, w.rect);
            std::wstring text = focused ? app.EditorEditBuffer : (w.getText ? w.getText() : std::wstring());
            Gdiplus::Font font(UiFontFamily(), 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::Color textColor = enabled ? pal.Ink : pal.Ink45;
            float tx = w.rect.X + 6.0f, ty = w.rect.Y + 4.0f;
            DrawTextLeft(g, text, font, tx, ty, textColor);
            if (focused) {
                int caret = std::clamp(app.EditorCaret, 0, static_cast<int>(text.size()));
                Gdiplus::RectF bbox(0, 0, 0, 0);
                if (caret > 0) {
                    Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
                    fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
                    g.MeasureString(text.substr(0, caret).c_str(), caret, &font, Gdiplus::PointF(0, 0), &fmt, &bbox);
                }
                float cx = tx + bbox.Width + 1.0f;
                Gdiplus::Pen caretPen(pal.Accent, 2.0f);
                g.DrawLine(&caretPen, cx, w.rect.Y + 4.0f, cx, w.rect.Y + w.rect.Height - 4.0f);
            }
            break;
        }
        case WType::Dropdown: {
            bool open = app.EditorOpenDropdown == w.id;
            Gdiplus::SolidBrush bg(hover || open ? pal.PanelBg2 : pal.PanelBg);
            g.FillRectangle(&bg, w.rect);
            Gdiplus::Pen border(enabled ? pal.Ink : pal.Ink45, open ? 2.0f : 1.0f);
            g.DrawRectangle(&border, w.rect);
            std::wstring text;
            if (w.items && w.getSel) {
                auto items = w.items();
                int sel = w.getSel();
                if (sel >= 0 && sel < static_cast<int>(items.size())) text = items[sel];
            }
            Gdiplus::RectF textRect(w.rect.X + 6.0f, w.rect.Y, w.rect.Width - 30.0f, w.rect.Height);
            DrawLabelText(g, text, textRect, 2.0f, enabled ? pal.Ink : pal.Ink45, false);
            // Arrow.
            Gdiplus::PointF tri[3] = {
                Gdiplus::PointF(w.rect.X + w.rect.Width - 20.0f, w.rect.Y + 10.0f),
                Gdiplus::PointF(w.rect.X + w.rect.Width - 8.0f, w.rect.Y + 10.0f),
                Gdiplus::PointF(w.rect.X + w.rect.Width - 14.0f, w.rect.Y + 18.0f),
            };
            Gdiplus::SolidBrush arrow(enabled ? pal.Ink : pal.Ink45);
            g.FillPolygon(&arrow, tri, 3);
            break;
        }
        case WType::Button: {
            bool primary = w.primary;
            // The two drag-target buttons double as a radio pair.
            if (w.label == EStr::DragHitbox) primary = !app.EditorDragTargetIsHurtbox;
            else if (w.label == EStr::DragHurtbox) primary = app.EditorDragTargetIsHurtbox;
            Gdiplus::GraphicsPath path;
            AddRoundedRect(path, w.rect, 0.0f);
            if (primary) {
                Gdiplus::SolidBrush fill(!enabled ? pal.Border : (hover ? pal.AccentDark : pal.Accent));
                g.FillPath(&fill, &path);
                if (enabled) DrawGlossCap(g, w.rect);
            } else {
                Gdiplus::SolidBrush fill(hover ? pal.PanelBg2 : pal.PanelBg);
                g.FillPath(&fill, &path);
                Gdiplus::Pen border(enabled ? pal.Border : pal.Ink45, 2.0f);
                g.DrawPath(&border, &path);
            }
            Gdiplus::Color textColor = !enabled ? pal.Ink45 : (primary ? pal.White : pal.TextDark);
            DrawLabelText(g, caption, w.rect, w.dot, textColor, true);
            break;
        }
        case WType::Check: {
            bool checked = w.getChecked && w.getChecked();
            Gdiplus::RectF box(w.rect.X, w.rect.Y + 2, 20, 20);
            Gdiplus::SolidBrush bg(pal.PanelBg);
            g.FillRectangle(&bg, box);
            Gdiplus::Pen border(pal.Ink, 1.5f);
            g.DrawRectangle(&border, box);
            if (checked) {
                Gdiplus::SolidBrush mark(pal.Accent);
                g.FillRectangle(&mark, box.X + 4, box.Y + 4, 12.0f, 12.0f);
            }
            Gdiplus::RectF textRect(w.rect.X + 28, w.rect.Y, w.rect.Width - 28, w.rect.Height);
            DrawLabelText(g, caption, textRect, 1.5f, pal.Ink, false);
            break;
        }
    }
}

static void DrawOpenDropdown(Gdiplus::Graphics& g, App& app, const EdWidget& d) {
    const auto& pal = GetPalette();
    auto items = d.items ? d.items() : std::vector<std::wstring>();
    Gdiplus::RectF list = DropdownListRect(d);
    Gdiplus::SolidBrush bg(pal.PanelBg);
    g.FillRectangle(&bg, list);
    int sel = d.getSel ? d.getSel() : -1;
    int rows = std::min(static_cast<int>(items.size()), kDropMaxRows);
    for (int r = 0; r < rows; r++) {
        int idx = r + app.EditorDropdownScroll;
        if (idx >= static_cast<int>(items.size())) break;
        Gdiplus::RectF row(list.X + 2, list.Y + 2 + r * kDropRowH, list.Width - 4, kDropRowH);
        bool hovered = Inside(row, app.EditorMouseX, app.EditorMouseY);
        bool selected = idx == sel;
        if (hovered || selected) {
            Gdiplus::SolidBrush hb(hovered ? pal.Accent : pal.PanelBg2);
            g.FillRectangle(&hb, row);
        }
        Gdiplus::RectF textRect(row.X + 6, row.Y, row.Width - 12, row.Height);
        DrawLabelText(g, items[idx], textRect, 2.0f, hovered ? pal.White : pal.Ink, false);
    }
    if (static_cast<int>(items.size()) > kDropMaxRows) {
        // Scroll hint.
        Gdiplus::RectF hint(list.X + list.Width - 60, list.Y + list.Height - 18, 56, 14);
        DrawLabelText(g, std::to_wstring(app.EditorDropdownScroll + 1) + L"-" + std::to_wstring(std::min(static_cast<int>(items.size()), app.EditorDropdownScroll + rows)) + L"/" + std::to_wstring(items.size()), hint, 1.0f, pal.Ink55, false);
    }
    Gdiplus::Pen border(pal.Ink, 2.0f);
    g.DrawRectangle(&border, list);
}

void App::DrawEditor(Gdiplus::Graphics& g, int w, int h) {
    const auto& pal = GetPalette();
    Gdiplus::SolidBrush headerBg(pal.Accent);
    g.FillRectangle(&headerBg, 0, 0, w, kEditorHeaderHeight);
    Gdiplus::Font headerFont(UiFontFamily(), 20, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    DrawTextCentered(g, L"CHARACTER EDITOR", headerFont, Gdiplus::RectF(0, 0, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(kEditorHeaderHeight)), pal.White);
    if (!EditorStatus.empty() && std::chrono::steady_clock::now() < EditorStatusUntil) {
        Gdiplus::Font statusFont(UiFontFamily(), 15, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        DrawTextLeft(g, EditorStatus, statusFont, 40.0f, 21.0f, pal.White);
    }
    (void)h;

    for (const auto& wd : g_Widgets) {
        if (!ModeVisible(wd, EditorCreatingNew)) continue;
        DrawWidget(g, *this, wd);
    }
    DrawEditorPreview(g);
    if (!EditorCreatingNew) DrawMotionImagePreview(g);
    if (EditorOpenDropdown >= 0) {
        if (const EdWidget* d = Find(EditorOpenDropdown)) DrawOpenDropdown(g, *this, *d);
    }
}

// ---------------------------------------------------------------------
// Preview canvas (unchanged behavior: drag to move, drag corner to resize)
// ---------------------------------------------------------------------
void App::ClampBoxToPreview(double& cx, double& cy, double& w, double& h) {
    float groundX = EditorPreviewRect.X + EditorPreviewRect.Width / 2.0f;
    float groundY = EditorPreviewRect.Y + EditorPreviewRect.Height - 20.0f;
    double worldLeft = (EditorPreviewRect.X - groundX) / EditorPreviewScale;
    double worldRight = (EditorPreviewRect.X + EditorPreviewRect.Width - groundX) / EditorPreviewScale;
    double worldTop = (EditorPreviewRect.Y - groundY) / EditorPreviewScale;
    double worldBottom = (EditorPreviewRect.Y + EditorPreviewRect.Height - groundY) / EditorPreviewScale;

    double maxW = std::max(4.0, worldRight - worldLeft);
    double maxH = std::max(4.0, worldBottom - worldTop);
    w = std::clamp(w, 4.0, maxW);
    h = std::clamp(h, 4.0, maxH);
    cx = std::clamp(cx, worldLeft + w / 2.0, worldRight - w / 2.0);
    cy = std::clamp(cy, worldTop + h / 2.0, worldBottom - h / 2.0);
}

void App::DrawEditorPreview(Gdiplus::Graphics& g) {
    const auto& pal = GetPalette();
    const Gdiplus::RectF& r = EditorPreviewRect;

    Gdiplus::SolidBrush bg(pal.PanelBg);
    g.FillRectangle(&bg, r);
    Gdiplus::Pen border(pal.Ink, 2.0f);
    g.DrawRectangle(&border, r);

    float groundX = r.X + r.Width / 2.0f;
    float groundY = r.Y + r.Height - 20.0f;
    Gdiplus::Pen groundPen(pal.Ink45, 1.5f);
    g.DrawLine(&groundPen, r.X + 8, groundY, r.X + r.Width - 8, groundY);

    Gdiplus::Color bodyColor(255, static_cast<BYTE>(EditorStatsDraft.ColorR), static_cast<BYTE>(EditorStatsDraft.ColorG), static_cast<BYTE>(EditorStatsDraft.ColorB));
    HumanoidPose pose;
    pose.heightScale = EditorPreviewScale;
    pose.crouch = (EditorHurtStance == 1);
    pose.jump = (EditorHurtStance == 2);
    DrawHumanoid(g, groundX, groundY, bodyColor, pose);

    auto drawBox = [&](double cx, double cy, double w, double h, Gdiplus::Color color, bool active) {
        float pcx = groundX + static_cast<float>(cx * EditorPreviewScale);
        float pcy = groundY + static_cast<float>(cy * EditorPreviewScale);
        float pw = static_cast<float>(w * EditorPreviewScale), ph = static_cast<float>(h * EditorPreviewScale);
        Gdiplus::RectF br(pcx - pw / 2.0f, pcy - ph / 2.0f, pw, ph);
        Gdiplus::SolidBrush fill(Gdiplus::Color(active ? 70 : 35, color.GetR(), color.GetG(), color.GetB()));
        g.FillRectangle(&fill, br);
        Gdiplus::Pen pen(color, active ? 2.5f : 1.0f);
        g.DrawRectangle(&pen, br);
        if (active) {
            Gdiplus::SolidBrush handleBrush(color);
            g.FillRectangle(&handleBrush, br.X + br.Width - 5, br.Y + br.Height - 5, 10.0f, 10.0f);
        }
    };

    auto& hurtParts = EditorHurtboxDraft.PartsForStance(StanceKey(EditorHurtStance));
    for (size_t i = 0; i < hurtParts.size(); i++) {
        bool active = EditorDragTargetIsHurtbox && static_cast<int>(i) == EditorHurtPartIndex;
        drawBox(hurtParts[i].Box.CenterX, hurtParts[i].Box.CenterY, hurtParts[i].Box.Width, hurtParts[i].Box.Height,
                Gdiplus::Color(255, 40, 190, 70), active);
    }
    for (size_t i = 0; i < EditorHitboxDraftList.size(); i++) {
        bool active = !EditorDragTargetIsHurtbox && static_cast<int>(i) == EditorHitboxIndex;
        const HitboxDef& hb = EditorHitboxDraftList[i];
        drawBox(hb.offsetX, hb.offsetY, hb.width, hb.height, Gdiplus::Color(255, 230, 30, 30), active);
    }

    Gdiplus::RectF legendRect(r.X, r.Y + r.Height + 4, r.Width, 16.0f);
    DrawLabelText(g, Str(EStr::Legend, EditorLanguage), legendRect, 1.5f, pal.Ink55, false);
}

// ---------------------------------------------------------------------
// Motion reference image
// ---------------------------------------------------------------------
static fs::path ResolveMotionImagePath(const fs::path& baseDataDir, const std::string& stored) {
    fs::path p(stored);
    return p.is_absolute() ? p : (baseDataDir / p);
}

void App::BrowseMotionImage() {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Hwnd;
    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = EditorLanguage == 1 ? L"モーション画像を選択" : L"Select Motion Image";
    if (!GetOpenFileNameW(&ofn)) return;

    fs::path chosen(fileBuf);
    std::error_code ec;
    fs::path rel = fs::relative(chosen, BaseDataDir, ec);
    std::string stored;
    if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) stored = rel.generic_string();
    else stored = WideToUtf8(chosen.wstring());
    EditorMotionImagePath = stored;
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::DrawMotionImagePreview(Gdiplus::Graphics& g) {
    const auto& pal = GetPalette();
    const Gdiplus::RectF& r = EditorMotionImageRect;
    Gdiplus::SolidBrush bg(pal.PanelBg);
    g.FillRectangle(&bg, r);
    Gdiplus::Pen border(pal.Ink, 1.5f);
    g.DrawRectangle(&border, r);

    if (EditorMotionImagePath.empty()) {
        EditorMotionImageCache.reset();
        EditorMotionImageCachedPath.clear();
        return;
    }
    if (EditorMotionImageCachedPath != EditorMotionImagePath) {
        fs::path full = ResolveMotionImagePath(BaseDataDir, EditorMotionImagePath);
        EditorMotionImageCache = std::make_unique<Gdiplus::Image>(full.wstring().c_str());
        EditorMotionImageCachedPath = EditorMotionImagePath;
    }
    if (!EditorMotionImageCache || EditorMotionImageCache->GetLastStatus() != Gdiplus::Ok) return;

    Gdiplus::REAL iw = static_cast<Gdiplus::REAL>(EditorMotionImageCache->GetWidth());
    Gdiplus::REAL ih = static_cast<Gdiplus::REAL>(EditorMotionImageCache->GetHeight());
    if (iw <= 0 || ih <= 0) return;
    float pad = 6.0f;
    Gdiplus::REAL scale = std::min((r.Width - pad * 2) / iw, (r.Height - pad * 2) / ih);
    Gdiplus::REAL dw = iw * scale, dh = ih * scale;
    Gdiplus::REAL dx = r.X + (r.Width - dw) / 2.0f, dy = r.Y + (r.Height - dh) / 2.0f;
    g.DrawImage(EditorMotionImageCache.get(), dx, dy, dw, dh);
}

} // namespace kakuge

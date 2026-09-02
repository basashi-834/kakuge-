// platform/Editor.cpp
// Character Editor screen. Unlike the other screens, this one is data-
// entry heavy, so it uses real native Win32 child controls (Edit/
// ComboBox/Button) instead of the custom GDI+ button system - simpler and
// more robust for text/number entry than hand-rolling a text widget.
//
// Visual unification with the rest of the game (per user request): every
// label/button/combo is owner-drawn with the same HARD CANDY palette and
// hand-authored pixel font (DrawPixelText*, see Draw.h) the rest of the
// game uses, and plain EDIT controls are recolored (WM_CTLCOLOREDIT, see
// WinMain.cpp) to the same palette. EDIT controls keep the real system
// font for their live-typed text - Win32 offers no owner-draw hook for
// EDIT, so a genuinely blocky font there isn't achievable without a fully
// custom text-input control - but every other element matches.
//
// Also supports:
//   - JP/EN language toggle (BtnLanguage) - see the EStr table below.
//     MakeLabel/MakeButton register (HWND, EStr) pairs so
//     ApplyEditorLanguage() can retext every control in place.
//   - Multi-part hurtboxes (head/torso/arm/hand/waist/leg/foot, or any
//     freeform name) and multi-hitbox moves, each edited as a selectable
//     list (ComboHurtPart / ComboHitboxIndex) rather than a single fixed
//     box - see engine/Boxes.h's HurtboxSet.
//   - A button selector for Normal moves (ComboMoveButton) and a
//     directional-numpad command builder + button choice for Special/
//     Super moves (BtnDigit[]/EditCommandPreview/ComboSpecialButton),
//     which write MoveData::Button/InputCommand directly.
//   - Cancel-window start/end frame fields (already existed in MoveData,
//     just weren't editable in the UI).
//   - ChkDynamicHitbox: a reserved, not-yet-functional flag
//     (MoveData::HasDynamicHitbox) for moves whose hitbox would need to
//     move under its own trajectory independent of the character - full
//     keyframe editing is out of scope for now, this just reserves the
//     data's place per the user's "spec only" request.
//
// Two fixes/features requested by the user vs. the earlier WinForms
// edition:
//   1) The move dropdown now shows MoveData::Name (its Japanese display
//      name), not the underlying file/id - EditorMoveIdsInCombo keeps the
//      combo index -> move id mapping since a native ComboBox only stores
//      display strings.
//   2) A "+ NEW CHARACTER" flow lets the player create a brand-new
//      character (cloned from a template's moveset) entirely in-game,
//      persisted via DataManager::CreateCharacter, rather than only being
//      able to edit characters that already exist.
//
// The editor always opens at a fixed, comfortably-sized window regardless
// of the chosen game resolution (native controls don't follow the GDI+
// world-transform used to scale the game/menu screens to arbitrary
// resolutions) and restores the game's resolution on exit.
#include "App.h"
#include <commctrl.h>
#include <string>
#include <sstream>
#include <algorithm>

namespace kakuge {

namespace {
constexpr int ID_COMBO_CHARACTER = 2001;
constexpr int ID_EDIT_CHAR_NAME = 2002;
constexpr int ID_EDIT_MAX_HP = 2003;
constexpr int ID_EDIT_WALK_FWD = 2004;
constexpr int ID_EDIT_WALK_BACK = 2005;
constexpr int ID_EDIT_DASH = 2006;
constexpr int ID_EDIT_JUMP_VEL = 2007;
constexpr int ID_EDIT_GRAVITY = 2008;
constexpr int ID_BTN_SAVE = 2009;
constexpr int ID_BTN_NEW_CHARACTER = 2010;
constexpr int ID_BTN_BACK = 2011;

constexpr int ID_COMBO_MOVE = 2020;
constexpr int ID_EDIT_MOVE_NAME = 2021;
constexpr int ID_EDIT_STARTUP = 2022;
constexpr int ID_EDIT_ACTIVE = 2023;
constexpr int ID_EDIT_RECOVERY = 2024;
constexpr int ID_EDIT_DAMAGE = 2025;
constexpr int ID_EDIT_HITSTUN = 2026;
constexpr int ID_EDIT_BLOCKSTUN = 2027;
constexpr int ID_EDIT_HITSTOP = 2028;
constexpr int ID_BTN_SAVE_MOVE = 2029;

constexpr int ID_EDIT_NEW_ID = 2040;
constexpr int ID_EDIT_NEW_NAME = 2041;
constexpr int ID_COMBO_TEMPLATE = 2042;
constexpr int ID_BTN_CREATE_CONFIRM = 2043;
constexpr int ID_BTN_CREATE_CANCEL = 2044;

constexpr int ID_EDIT_HIT_X = 2050;
constexpr int ID_EDIT_HIT_Y = 2051;
constexpr int ID_EDIT_HIT_W = 2052;
constexpr int ID_EDIT_HIT_H = 2053;
constexpr int ID_COMBO_HURT_STANCE = 2054;
constexpr int ID_EDIT_HURT_X = 2055;
constexpr int ID_EDIT_HURT_Y = 2056;
constexpr int ID_EDIT_HURT_W = 2057;
constexpr int ID_EDIT_HURT_H = 2058;
constexpr int ID_BTN_DRAG_HITBOX = 2059;
constexpr int ID_BTN_DRAG_HURTBOX = 2060;

constexpr int ID_COMBO_HITBOX_INDEX = 2070;
constexpr int ID_BTN_ADD_HITBOX = 2071;
constexpr int ID_BTN_REMOVE_HITBOX = 2072;
constexpr int ID_COMBO_HURT_PART = 2073;
constexpr int ID_BTN_ADD_PART = 2074;
constexpr int ID_BTN_REMOVE_PART = 2075;

constexpr int ID_COMBO_MOVE_BUTTON = 2080;
constexpr int ID_EDIT_CANCEL_START = 2081;
constexpr int ID_EDIT_CANCEL_END = 2082;
constexpr int ID_BTN_DIGIT_BASE = 2090; // digits 1-9 -> ID_BTN_DIGIT_BASE+1 .. +9
constexpr int ID_BTN_COMMAND_CLEAR = 2100;
constexpr int ID_COMBO_SPECIAL_BUTTON = 2101;
constexpr int ID_CHK_DYNAMIC_HITBOX = 2102;

constexpr int ID_BTN_LANGUAGE = 2110;

// ---------------------------------------------------------------------
// Language table. Every static label / button created via MakeLabel/
// MakeButton(EStr) registers into g_LangControls so ApplyEditorLanguage()
// can retext every one of them in place, no control recreation needed.
// Short universal game codes (LP/MP/HP/LK/MK/HK/AnyP/AnyK, digits) and
// stored identifiers (hurtbox part names) are deliberately NOT in this
// table - see PartDisplayName/StanceDisplayName for those.
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
    L"HITBOX MOVES INDEPENDENTLY\n(SPEC ONLY - NOT FUNCTIONAL)", L"SAVE MOVE",
    L"HITBOX / HURTBOX PREVIEW (drag = move, drag corner = resize)", L"DRAG: HITBOX", L"DRAG: HURTBOX",
    L"MOVE HITBOX  X / Y / W / H", L"+ ADD", L"- REMOVE",
    L"CHARACTER HURTBOX  X / Y / W / H", L"HURTBOX STANCE", L"HURTBOX PART",
    L"blue = hurtbox   red = hitbox", L"EN",
};
const wchar_t* const kStringsJP[static_cast<size_t>(EStr::Count)] = {
    L"キャラクター", L"名前", L"最大HP", L"前歩き速度", L"後ろ歩き速度", L"ダッシュ速度",
    L"ジャンプ初速(負=上方向)", L"重力",
    L"キャラクターを保存", L"+ 新規キャラクター", L"タイトルへ戻る",
    L"新規キャラID (例: ken)", L"表示名", L"技構成のコピー元", L"作成", L"キャンセル",
    L"技", L"表示名", L"発生", L"持続", L"硬直", L"ダメージ", L"ヒット硬直", L"ガード硬直", L"ヒットストップ",
    L"ボタン(通常技のみ)", L"キャンセル開始F", L"キャンセル終了F",
    L"必殺技コマンド(必殺技/超必殺技のみ)", L"クリア",
    L"判定が独自に移動する技\n(仕様のみ・未実装)", L"技を保存",
    L"当たり判定プレビュー(ドラッグで移動、角をドラッグでリサイズ)", L"操作対象:攻撃判定", L"操作対象:喰らい判定",
    L"攻撃判定 X/Y/W/H", L"+ 追加", L"- 削除",
    L"喰らい判定 X/Y/W/H", L"姿勢", L"部位",
    L"青=喰らい判定  赤=攻撃判定", L"JP",
};

const wchar_t* Str(EStr id, int lang) {
    return lang == 1 ? kStringsJP[static_cast<size_t>(id)] : kStringsEN[static_cast<size_t>(id)];
}

std::vector<std::string> g_EditorMoveIdsInCombo;
std::vector<HWND> g_NormalModeControls;
std::vector<HWND> g_CreateModeControls;
HWND g_LblCreateId = nullptr, g_LblCreateName = nullptr, g_LblCreateTemplate = nullptr;

// (HWND, EStr) pairs for language retexting - see ApplyEditorLanguage.
std::vector<std::pair<HWND, EStr>> g_LangControls;

std::wstring PartDisplayName(const std::string& name, int lang) {
    static const std::pair<const char*, std::pair<const wchar_t*, const wchar_t*>> kPresets[] = {
        {"head", {L"HEAD", L"頭"}}, {"torso", {L"TORSO", L"胴体"}}, {"arm", {L"ARM", L"腕"}},
        {"hand", {L"HAND", L"手"}}, {"waist", {L"WAIST", L"腰"}}, {"leg", {L"LEG", L"足"}},
        {"foot", {L"FOOT", L"足先"}}, {"body", {L"BODY", L"全身"}},
    };
    for (const auto& p : kPresets) {
        if (name == p.first) return lang == 1 ? p.second.second : p.second.first;
    }
    return Utf8ToWide(name); // custom/freeform part name - shown verbatim
}

std::wstring StanceDisplayName(int stance, int lang) {
    if (lang == 1) return stance == 1 ? L"しゃがみ" : stance == 2 ? L"空中" : L"立ち";
    return stance == 1 ? L"CROUCH" : stance == 2 ? L"AIR" : L"STAND";
}

// Preset part names offered by "+ ADD PART", in order - BtnAddPart adds
// whichever of these isn't already present on the current stance, or a
// generic "part8"/"part9"/... once all seven are used.
const char* const kPartPresets[] = {"head", "torso", "arm", "hand", "waist", "leg", "foot"};

HWND MakeEdit(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}
// SS_OWNERDRAW (handled in Editor_OnDrawItem below, ODT_STATIC) so labels
// read in the same hand-authored pixel font and HARD CANDY palette as
// every other screen, instead of the plain system-font STATIC default.
HWND MakeLabel(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, EStr strId) {
    HWND h2 = CreateWindowExW(0, L"STATIC", Str(strId, 0), WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                               x, y, w, h, parent, nullptr, hInst, nullptr);
    g_LangControls.push_back({h2, strId});
    return h2;
}
// BS_OWNERDRAW + WM_DRAWITEM (see Editor_OnDrawItem below) so these read
// as the same red/white rounded buttons used on the custom-drawn screens,
// instead of a plain system button - this is what "keep it looking like
// the play screen" ends up meaning for a native-control-based form.
HWND MakeButton(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h, EStr strId) {
    HWND h2 = CreateWindowExW(0, L"BUTTON", Str(strId, 0), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                               x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
    g_LangControls.push_back({h2, strId});
    return h2;
}
// Small fixed-caption buttons (single letter/digit, never translated) -
// still owner-drawn for the same look, just not registered for retexting.
HWND MakeButtonRaw(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h, const std::wstring& text) {
    return CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                            x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}
// CBS_OWNERDRAWFIXED + CBS_HASSTRINGS (handled in Editor_OnDrawItem,
// ODT_COMBOBOX) so the dropdown reads in palette colors + pixel font too.
HWND MakeCombo(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"",
                              WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
    SendMessageW(c, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 22);
    SendMessageW(c, CB_SETITEMHEIGHT, 0, 22);
    return c;
}
// CB_SETCURSEL alone doesn't reliably repaint an owner-draw combo's closed
// selection field under Wine (a real Windows install repaints it
// immediately) - force it explicitly so the chosen item's text actually
// shows without waiting for some unrelated event to trigger a redraw.
void SetComboSel(HWND combo, int idx) {
    if (!combo) return;
    SendMessageW(combo, CB_SETCURSEL, idx, 0);
    InvalidateRect(combo, nullptr, TRUE);
    UpdateWindow(combo);
}

std::wstring GetEditText(HWND h) {
    wchar_t buf[256];
    GetWindowTextW(h, buf, 256);
    return buf;
}
// Guards against EN_CHANGE reentrancy: while a Sync*FieldsFromDraft() is
// pushing several SetEditDouble() calls in a row (one per X/Y/W/H field),
// each individual SetWindowTextW synchronously fires EN_CHANGE, which
// would otherwise call Apply*FieldsToDraft() and write a mix of
// already-updated and not-yet-updated field text back into the draft
// mid-sync. Set around every Sync*FieldsFromDraft() body; checked at the
// top of both Apply*FieldsToDraft() functions.
bool g_SyncingBoxFields = false;

void SetEditText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }
void SetEditInt(HWND h, int v) { SetEditText(h, std::to_wstring(v)); }
void SetEditDouble(HWND h, double v) {
    std::wstringstream ss; ss << v;
    SetEditText(h, ss.str());
}
int GetEditInt(HWND h, int fallback) {
    try { return std::stoi(GetEditText(h)); } catch (...) { return fallback; }
}
double GetEditDouble(HWND h, double fallback) {
    try { return std::stod(GetEditText(h)); } catch (...) { return fallback; }
}

// The eight buttons a move can bind to - LP/MP/HP/LK/MK/HK for Normal
// moves, plus AnyP/AnyK for Special/Super moves that fire off any punch
// or kick (see CommandParser::ButtonSatisfies).
const wchar_t* const kNormalButtons[] = {L"LP", L"MP", L"HP", L"LK", L"MK", L"HK"};
const wchar_t* const kAllButtons[] = {L"LP", L"MP", L"HP", L"LK", L"MK", L"HK", L"AnyP", L"AnyK"};

} // namespace

void App::EnterEditor() {
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(Hwnd, GWLP_HINSTANCE));

    GetWindowRect(Hwnd, &PreEditorWindowRect);
    EditorSizeSaved = true;
    // A much bigger, more spacious canvas (was 1040x830) - three generously
    // spaced columns with a large hitbox/hurtbox preview, closer to a
    // video-editor's roomy panel layout than the old cramped form.
    RECT rc{0, 0, 1600, 980};
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtr(Hwnd, GWL_STYLE)), FALSE);
    SetWindowPos(Hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    CreateEditorControls();
    LayoutEditorControls();
    PopulateCharacterCombo();
    PopulateMoveCombo();
    PopulateTemplateCombo();
    ApplyEditorLanguage();
    // EditorCreatingNew may already be true if Character Select routed us
    // here via the "+ ADD CHARACTER" tile - respect that starting mode.
    if (EditorCreatingNew) ShowCreateCharacterPrompt(); else HideCreateCharacterPrompt();
    if (!EditorCreatingNew && !Dm->GetCharacterIds().empty()) {
        LoadCharacterIntoForm(Dm->GetCharacterIds()[0]);
    }
    InvalidateRect(Hwnd, nullptr, TRUE);
    (void)hInst;
}

void App::LeaveEditor() {
    DestroyEditorControls();
    if (EditorSizeSaved) {
        SetWindowPos(Hwnd, nullptr, 0, 0, PreEditorWindowRect.right - PreEditorWindowRect.left,
                     PreEditorWindowRect.bottom - PreEditorWindowRect.top, SWP_NOMOVE | SWP_NOZORDER);
        EditorSizeSaved = false;
    }
    EditorCreatingNew = false;
}

void App::CreateEditorControls() {
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(Hwnd, GWLP_HINSTANCE));
    if (ComboCharacter) return; // already created

    // Three generously spaced columns (video-editor-style roomy panels,
    // was a cramped two-column 1040-wide form): character stats, move
    // data, and a large hitbox/hurtbox preview.
    const int col1 = 40, col2 = 420, col3 = 800;
    const int fieldW = 320;

    BtnLanguage = MakeButtonRaw(Hwnd, hInst, ID_BTN_LANGUAGE, 1600 - 40 - 70, 20, 70, 30, L"EN");

    // ---- Column 1: character stats ----
    // Every label here is captured into g_NormalModeControls too (not just
    // the edit/combo/button controls) so the "+ NEW CHARACTER" overlay
    // (which reuses this same screen region) doesn't leave stray label
    // text showing through underneath it.
    HWND lbl;
    lbl = MakeLabel(Hwnd, hInst, col1, 90, fieldW, 20, EStr::Character); g_NormalModeControls.push_back(lbl);
    ComboCharacter = MakeCombo(Hwnd, hInst, ID_COMBO_CHARACTER, col1, 112, fieldW, 220);
    lbl = MakeLabel(Hwnd, hInst, col1, 150, fieldW, 20, EStr::Name); g_NormalModeControls.push_back(lbl);
    EditCharName = MakeEdit(Hwnd, hInst, ID_EDIT_CHAR_NAME, col1, 172, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 210, fieldW, 20, EStr::MaxHp); g_NormalModeControls.push_back(lbl);
    EditMaxHp = MakeEdit(Hwnd, hInst, ID_EDIT_MAX_HP, col1, 232, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 270, fieldW, 20, EStr::WalkFwd); g_NormalModeControls.push_back(lbl);
    EditWalkFwd = MakeEdit(Hwnd, hInst, ID_EDIT_WALK_FWD, col1, 292, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 330, fieldW, 20, EStr::WalkBack); g_NormalModeControls.push_back(lbl);
    EditWalkBack = MakeEdit(Hwnd, hInst, ID_EDIT_WALK_BACK, col1, 352, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 390, fieldW, 20, EStr::DashSpeed); g_NormalModeControls.push_back(lbl);
    EditDash = MakeEdit(Hwnd, hInst, ID_EDIT_DASH, col1, 412, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 450, fieldW, 20, EStr::JumpVel); g_NormalModeControls.push_back(lbl);
    EditJumpVel = MakeEdit(Hwnd, hInst, ID_EDIT_JUMP_VEL, col1, 472, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, col1, 510, fieldW, 20, EStr::Gravity); g_NormalModeControls.push_back(lbl);
    EditGravity = MakeEdit(Hwnd, hInst, ID_EDIT_GRAVITY, col1, 532, fieldW, 26);
    BtnSave = MakeButton(Hwnd, hInst, ID_BTN_SAVE, col1, 580, fieldW, 44, EStr::SaveCharacter);
    BtnNewCharacter = MakeButton(Hwnd, hInst, ID_BTN_NEW_CHARACTER, col1, 634, fieldW, 44, EStr::NewCharacter);

    for (HWND h : {ComboCharacter, EditCharName, EditMaxHp, EditWalkFwd, EditWalkBack, EditDash, EditJumpVel, EditGravity, BtnSave, BtnNewCharacter}) {
        g_NormalModeControls.push_back(h);
    }

    // ---- Create-new-character overlay (shares column 1) ----
    g_LblCreateId = MakeLabel(Hwnd, hInst, col1, 90, fieldW, 20, EStr::NewCharId);
    EditCharId = MakeEdit(Hwnd, hInst, ID_EDIT_NEW_ID, col1, 112, fieldW, 26);
    g_LblCreateName = MakeLabel(Hwnd, hInst, col1, 150, fieldW, 20, EStr::DisplayName);
    // reuse EditCharName isn't safe here since it's shared with normal mode; make a dedicated one.
    HWND editNewName = MakeEdit(Hwnd, hInst, ID_EDIT_NEW_NAME, col1, 172, fieldW, 26);
    g_LblCreateTemplate = MakeLabel(Hwnd, hInst, col1, 210, fieldW, 20, EStr::CloneFrom);
    ComboTemplate = MakeCombo(Hwnd, hInst, ID_COMBO_TEMPLATE, col1, 232, fieldW, 220);
    BtnCreateConfirm = MakeButton(Hwnd, hInst, ID_BTN_CREATE_CONFIRM, col1, 280, fieldW, 44, EStr::Create);
    BtnCreateCancel = MakeButton(Hwnd, hInst, ID_BTN_CREATE_CANCEL, col1, 334, fieldW, 44, EStr::CancelBtn);
    for (HWND h : {g_LblCreateId, EditCharId, g_LblCreateName, editNewName, g_LblCreateTemplate, ComboTemplate, BtnCreateConfirm, BtnCreateCancel}) {
        g_CreateModeControls.push_back(h);
    }

    // ---- Column 2: move editor ----
    int rx = col2;
    int thirdW = 96, thirdGap = 16;
    lbl = MakeLabel(Hwnd, hInst, rx, 90, fieldW, 20, EStr::Move); g_NormalModeControls.push_back(lbl);
    ComboMove = MakeCombo(Hwnd, hInst, ID_COMBO_MOVE, rx, 112, fieldW, 320);
    lbl = MakeLabel(Hwnd, hInst, rx, 150, fieldW, 20, EStr::MoveDisplayName); g_NormalModeControls.push_back(lbl);
    EditMoveName = MakeEdit(Hwnd, hInst, ID_EDIT_MOVE_NAME, rx, 172, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, rx, 210, thirdW, 20, EStr::Startup); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + thirdW + thirdGap, 210, thirdW, 20, EStr::Active); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + (thirdW + thirdGap) * 2, 210, thirdW, 20, EStr::Recovery); g_NormalModeControls.push_back(lbl);
    EditStartup = MakeEdit(Hwnd, hInst, ID_EDIT_STARTUP, rx, 232, thirdW, 26);
    EditActive = MakeEdit(Hwnd, hInst, ID_EDIT_ACTIVE, rx + thirdW + thirdGap, 232, thirdW, 26);
    EditRecovery = MakeEdit(Hwnd, hInst, ID_EDIT_RECOVERY, rx + (thirdW + thirdGap) * 2, 232, thirdW, 26);
    lbl = MakeLabel(Hwnd, hInst, rx, 270, fieldW, 20, EStr::Damage); g_NormalModeControls.push_back(lbl);
    EditDamage = MakeEdit(Hwnd, hInst, ID_EDIT_DAMAGE, rx, 292, fieldW, 26);
    lbl = MakeLabel(Hwnd, hInst, rx, 330, thirdW, 20, EStr::Hitstun); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + thirdW + thirdGap, 330, thirdW, 20, EStr::Blockstun); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + (thirdW + thirdGap) * 2, 330, thirdW, 20, EStr::Hitstop); g_NormalModeControls.push_back(lbl);
    EditHitstun = MakeEdit(Hwnd, hInst, ID_EDIT_HITSTUN, rx, 352, thirdW, 26);
    EditBlockstun = MakeEdit(Hwnd, hInst, ID_EDIT_BLOCKSTUN, rx + thirdW + thirdGap, 352, thirdW, 26);
    EditHitstop = MakeEdit(Hwnd, hInst, ID_EDIT_HITSTOP, rx + (thirdW + thirdGap) * 2, 352, thirdW, 26);
    LabelAdvantage = MakeLabel(Hwnd, hInst, rx, 400, fieldW, 40, EStr::CancelStart); // retexted dynamically by UpdateAdvantagePreview

    // Button binding (Normal moves only) + cancel window (all moves).
    lbl = MakeLabel(Hwnd, hInst, rx, 450, fieldW, 20, EStr::ButtonLabel); g_NormalModeControls.push_back(lbl);
    ComboMoveButton = MakeCombo(Hwnd, hInst, ID_COMBO_MOVE_BUTTON, rx, 472, thirdW * 2 + thirdGap, 200);
    for (const wchar_t* b : kAllButtons) SendMessageW(ComboMoveButton, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(b));
    lbl = MakeLabel(Hwnd, hInst, rx, 510, thirdW, 20, EStr::CancelStart); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + thirdW + thirdGap, 510, thirdW, 20, EStr::CancelEnd); g_NormalModeControls.push_back(lbl);
    EditCancelStart = MakeEdit(Hwnd, hInst, ID_EDIT_CANCEL_START, rx, 532, thirdW, 26);
    EditCancelEnd = MakeEdit(Hwnd, hInst, ID_EDIT_CANCEL_END, rx + thirdW + thirdGap, 532, thirdW, 26);

    // Special-move command builder: a 3x3 numpad-notation direction grid
    // (7 8 9 / 4 6 / 1 2 3 - numpad layout, so it reads the way every
    // fighting-game player already expects "236" or "214" to look) plus a
    // running preview of the digit string and a button choice.
    lbl = MakeLabel(Hwnd, hInst, rx, 578, fieldW, 20, EStr::CommandLabel); g_NormalModeControls.push_back(lbl);
    static const int kPadLayout[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
    int padCell = 34, padGap = 4;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int digit = kPadLayout[row][col];
            int bx = rx + col * (padCell + padGap);
            int by = 602 + row * (padCell + padGap);
            HWND btn = MakeButtonRaw(Hwnd, hInst, ID_BTN_DIGIT_BASE + digit, bx, by, padCell, padCell, std::to_wstring(digit));
            BtnDigit[digit - 1] = btn;
            g_NormalModeControls.push_back(btn);
        }
    }
    int padRightX = rx + 3 * (padCell + padGap) + 16;
    EditCommandPreview = MakeEdit(Hwnd, hInst, 0, padRightX, 602, fieldW - (padRightX - rx), 26);
    SendMessageW(EditCommandPreview, EM_SETREADONLY, TRUE, 0);
    BtnCommandClear = MakeButton(Hwnd, hInst, ID_BTN_COMMAND_CLEAR, padRightX, 636, 100, 30, EStr::ClearBtn);
    ComboSpecialButton = MakeCombo(Hwnd, hInst, ID_COMBO_SPECIAL_BUTTON, padRightX, 672, fieldW - (padRightX - rx), 200);
    for (const wchar_t* b : kAllButtons) SendMessageW(ComboSpecialButton, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(b));
    g_NormalModeControls.push_back(EditCommandPreview);

    ChkDynamicHitbox = CreateWindowExW(0, L"BUTTON", Str(EStr::MovingHitbox, 0),
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_MULTILINE,
                                        rx, 714, fieldW, 44, Hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CHK_DYNAMIC_HITBOX)), hInst, nullptr);
    g_LangControls.push_back({ChkDynamicHitbox, EStr::MovingHitbox});
    g_NormalModeControls.push_back(ChkDynamicHitbox);

    HWND btnSaveMove = MakeButton(Hwnd, hInst, ID_BTN_SAVE_MOVE, rx, 764, fieldW, 44, EStr::SaveMove);
    for (HWND h : {ComboMove, EditMoveName, EditStartup, EditActive, EditRecovery, EditDamage, EditHitstun, EditBlockstun, EditHitstop,
                    LabelAdvantage, ComboMoveButton, EditCancelStart, EditCancelEnd, BtnCommandClear, ComboSpecialButton, btnSaveMove}) {
        g_NormalModeControls.push_back(h);
    }

    BtnBack = MakeButton(Hwnd, hInst, ID_BTN_BACK, col1, 900, fieldW, 44, EStr::BackToTitle);

    // ---- Column 3: hitbox/hurtbox visual+numeric editor ----
    // The preview canvas itself (drag-to-move, drag-corner-to-resize) is
    // custom-painted in App::DrawEditorPreview, called from App::OnPaint's
    // Editor branch - it isn't a child control, just real window pixels
    // drawn directly, matching how the red header bar is drawn.
    int px = col3;
    int previewSize = 520;
    lbl = MakeLabel(Hwnd, hInst, px, 90, 700, 20, EStr::PreviewTitle);
    g_NormalModeControls.push_back(lbl);
    EditorPreviewRect = Gdiplus::RectF(static_cast<Gdiplus::REAL>(px), 116.0f, static_cast<Gdiplus::REAL>(previewSize), static_cast<Gdiplus::REAL>(previewSize));

    int belowPreview = 116 + previewSize + 20; // 656
    int quarterW = 130, quarterGap = 16;
    BtnDragHitbox = MakeButton(Hwnd, hInst, ID_BTN_DRAG_HITBOX, px, belowPreview, 250, 36, EStr::DragHitbox);
    BtnDragHurtbox = MakeButton(Hwnd, hInst, ID_BTN_DRAG_HURTBOX, px + 266, belowPreview, 250, 36, EStr::DragHurtbox);

    // Hitbox list (multiple hitboxes per move).
    int hbListY = belowPreview + 48;
    ComboHitboxIndex = MakeCombo(Hwnd, hInst, ID_COMBO_HITBOX_INDEX, px, hbListY, 180, 200);
    BtnAddHitbox = MakeButton(Hwnd, hInst, ID_BTN_ADD_HITBOX, px + 194, hbListY, 90, 28, EStr::AddBtn);
    BtnRemoveHitbox = MakeButton(Hwnd, hInst, ID_BTN_REMOVE_HITBOX, px + 292, hbListY, 90, 28, EStr::RemoveBtn);

    int hitRowY = hbListY + 36;
    lbl = MakeLabel(Hwnd, hInst, px, hitRowY, 400, 18, EStr::HitboxSection); g_NormalModeControls.push_back(lbl);
    EditHitX = MakeEdit(Hwnd, hInst, ID_EDIT_HIT_X, px, hitRowY + 22, quarterW, 26);
    EditHitY = MakeEdit(Hwnd, hInst, ID_EDIT_HIT_Y, px + (quarterW + quarterGap), hitRowY + 22, quarterW, 26);
    EditHitW = MakeEdit(Hwnd, hInst, ID_EDIT_HIT_W, px + (quarterW + quarterGap) * 2, hitRowY + 22, quarterW, 26);
    EditHitH = MakeEdit(Hwnd, hInst, ID_EDIT_HIT_H, px + (quarterW + quarterGap) * 3, hitRowY + 22, quarterW, 26);

    // Hurtbox stance + part list (multiple named parts per stance).
    int stanceRowY = hitRowY + 66;
    ComboHurtStance = MakeCombo(Hwnd, hInst, ID_COMBO_HURT_STANCE, px, stanceRowY, 120, 100);
    ComboHurtPart = MakeCombo(Hwnd, hInst, ID_COMBO_HURT_PART, px + 134, stanceRowY, 180, 200);
    BtnAddPart = MakeButton(Hwnd, hInst, ID_BTN_ADD_PART, px + 328, stanceRowY, 90, 28, EStr::AddBtn);
    BtnRemovePart = MakeButton(Hwnd, hInst, ID_BTN_REMOVE_PART, px + 426, stanceRowY, 90, 28, EStr::RemoveBtn);

    int hurtRowY = stanceRowY + 36;
    lbl = MakeLabel(Hwnd, hInst, px, hurtRowY, 400, 18, EStr::HurtboxSection); g_NormalModeControls.push_back(lbl);
    EditHurtX = MakeEdit(Hwnd, hInst, ID_EDIT_HURT_X, px, hurtRowY + 22, quarterW, 26);
    EditHurtY = MakeEdit(Hwnd, hInst, ID_EDIT_HURT_Y, px + (quarterW + quarterGap), hurtRowY + 22, quarterW, 26);
    EditHurtW = MakeEdit(Hwnd, hInst, ID_EDIT_HURT_W, px + (quarterW + quarterGap) * 2, hurtRowY + 22, quarterW, 26);
    EditHurtH = MakeEdit(Hwnd, hInst, ID_EDIT_HURT_H, px + (quarterW + quarterGap) * 3, hurtRowY + 22, quarterW, 26);

    for (HWND h : {BtnDragHitbox, BtnDragHurtbox, ComboHitboxIndex, BtnAddHitbox, BtnRemoveHitbox,
                    EditHitX, EditHitY, EditHitW, EditHitH,
                    ComboHurtStance, ComboHurtPart, BtnAddPart, BtnRemovePart,
                    EditHurtX, EditHurtY, EditHurtW, EditHurtH}) {
        g_NormalModeControls.push_back(h);
    }
}

void App::DestroyEditorControls() {
    for (HWND& h : EditorControls) { if (h) DestroyWindow(h); h = nullptr; }
    EditorControls.clear();
    auto destroyAll = [](std::vector<HWND>& v) { for (HWND h : v) if (h) DestroyWindow(h); v.clear(); };
    destroyAll(g_NormalModeControls);
    destroyAll(g_CreateModeControls);
    if (BtnBack) { DestroyWindow(BtnBack); BtnBack = nullptr; }
    if (BtnLanguage) { DestroyWindow(BtnLanguage); BtnLanguage = nullptr; }
    g_LangControls.clear();
    ComboCharacter = ComboMove = ComboTemplate = nullptr;
    EditCharId = EditCharName = EditMaxHp = EditWalkFwd = EditWalkBack = EditDash = EditJumpVel = EditGravity = nullptr;
    EditMoveName = EditStartup = EditActive = EditRecovery = EditDamage = EditHitstun = EditBlockstun = EditHitstop = nullptr;
    LabelAdvantage = nullptr;
    BtnSave = BtnNewCharacter = BtnCreateConfirm = BtnCreateCancel = nullptr;
    ComboMoveButton = EditCancelStart = EditCancelEnd = nullptr;
    for (HWND& h : BtnDigit) h = nullptr;
    EditCommandPreview = BtnCommandClear = ComboSpecialButton = ChkDynamicHitbox = nullptr;
    ComboHitboxIndex = BtnAddHitbox = BtnRemoveHitbox = nullptr;
    EditHitX = EditHitY = EditHitW = EditHitH = nullptr;
    ComboHurtStance = ComboHurtPart = BtnAddPart = BtnRemovePart = nullptr;
    EditHurtX = EditHurtY = EditHurtW = EditHurtH = nullptr;
    BtnDragHitbox = BtnDragHurtbox = nullptr;
}

void App::LayoutEditorControls() {
    // Fixed absolute layout (set at creation time above); nothing extra to
    // do here since the editor window doesn't resize.
}

void App::ApplyEditorLanguage() {
    for (auto& kv : g_LangControls) {
        if (kv.first) SetWindowTextW(kv.first, Str(kv.second, EditorLanguage));
    }
    if (BtnLanguage) SetWindowTextW(BtnLanguage, EditorLanguage == 1 ? L"EN" : L"JP"); // shows the language it would SWITCH to
    RebuildHitboxCombo();
    RebuildHurtPartCombo();
    // Stance combo items are language-dependent display strings too.
    if (ComboHurtStance) {
        int sel = static_cast<int>(SendMessageW(ComboHurtStance, CB_GETCURSEL, 0, 0));
        if (sel < 0) sel = EditorHurtStance;
        SendMessageW(ComboHurtStance, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < 3; i++) {
            std::wstring s = StanceDisplayName(i, EditorLanguage);
            SendMessageW(ComboHurtStance, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
        }
        SetComboSel(ComboHurtStance, sel);
    }
    UpdateAdvantagePreview();
    InvalidateRect(Hwnd, nullptr, TRUE);
}

void App::PopulateCharacterCombo() {
    if (!ComboCharacter) return;
    SendMessageW(ComboCharacter, CB_RESETCONTENT, 0, 0);
    int selectIndex = 0;
    const auto& ids = Dm->GetCharacterIds();
    for (size_t i = 0; i < ids.size(); i++) {
        const auto* stats = Dm->GetCharacter(ids[i]);
        std::wstring display = Utf8ToWide((stats ? stats->Name : ids[i]) + " (" + ids[i] + ")");
        SendMessageW(ComboCharacter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        // Keep the combo's selection in sync with whichever character is
        // actually loaded into the form (e.g. right after creating a new
        // one) instead of always snapping back to the first entry.
        if (ids[i] == EditorCharId) selectIndex = static_cast<int>(i);
    }
    if (!ids.empty()) SetComboSel(ComboCharacter, selectIndex);
}

void App::PopulateTemplateCombo() {
    if (!ComboTemplate) return;
    SendMessageW(ComboTemplate, CB_RESETCONTENT, 0, 0);
    for (const auto& id : Dm->GetCharacterIds()) {
        const auto* stats = Dm->GetCharacter(id);
        std::wstring display = Utf8ToWide((stats ? stats->Name : id) + " (" + id + ")");
        SendMessageW(ComboTemplate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
    if (!Dm->GetCharacterIds().empty()) SetComboSel(ComboTemplate, 0);
}

void App::PopulateMoveCombo() {
    if (!ComboMove || EditorCharId.empty()) return;
    SendMessageW(ComboMove, CB_RESETCONTENT, 0, 0);
    g_EditorMoveIdsInCombo.clear();
    const auto* moves = Dm->GetMoveset(EditorCharId);
    if (!moves) return;
    // Sort for a stable, predictable dropdown order (map iteration order
    // isn't guaranteed) - by startup frame, which roughly groups
    // light/medium/heavy/specials the way a player expects.
    std::vector<const MoveData*> sorted;
    for (const auto& kv : *moves) sorted.push_back(&kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const MoveData* a, const MoveData* b) { return a->Id < b->Id; });
    for (const auto* m : sorted) {
        // FIX (user-reported): show the move's readable display name, not
        // its id/filename.
        std::wstring display = Utf8ToWide(m->Name);
        SendMessageW(ComboMove, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        g_EditorMoveIdsInCombo.push_back(m->Id);
    }
    if (!g_EditorMoveIdsInCombo.empty()) {
        SetComboSel(ComboMove, 0);
        LoadMoveIntoForm(g_EditorMoveIdsInCombo[0]);
    }
}

void App::LoadCharacterIntoForm(const std::string& charId) {
    EditorCharId = charId;
    const auto* stats = Dm->GetCharacter(charId);
    if (!stats) return;
    SetEditText(EditCharName, Utf8ToWide(stats->Name));
    SetEditInt(EditMaxHp, stats->MaxHP);
    SetEditDouble(EditWalkFwd, stats->WalkForwardSpeed);
    SetEditDouble(EditWalkBack, stats->WalkBackwardSpeed);
    SetEditDouble(EditDash, stats->DashSpeed);
    SetEditDouble(EditJumpVel, stats->JumpVelocity);
    SetEditDouble(EditGravity, stats->Gravity);

    EditorHurtboxDraft = stats->Hurtboxes;
    EditorHurtStance = 0;
    EditorHurtPartIndex = 0;
    if (ComboHurtStance) SetComboSel(ComboHurtStance, 0);
    RebuildHurtPartCombo();
    SyncHurtboxFieldsFromDraft();

    PopulateMoveCombo();
}

void App::LoadMoveIntoForm(const std::string& moveId) {
    EditorMoveId = moveId;
    const auto* move = Dm->GetMove(EditorCharId, moveId);
    if (!move) return;
    SetEditText(EditMoveName, Utf8ToWide(move->Name));
    SetEditInt(EditStartup, move->Startup);
    SetEditInt(EditActive, move->Active);
    SetEditInt(EditRecovery, move->Recovery);
    SetEditInt(EditDamage, move->Damage);
    SetEditInt(EditHitstun, move->Hitstun);
    SetEditInt(EditBlockstun, move->Blockstun);
    SetEditInt(EditHitstop, move->Hitstop);
    SetEditInt(EditCancelStart, move->CancelStartFrame);
    SetEditInt(EditCancelEnd, move->CancelEndFrame);
    UpdateAdvantagePreview();

    bool isNormal = move->HasTag("Normal");
    bool isSpecialOrSuper = move->HasTag("Special") || move->HasTag("Super");
    if (ComboMoveButton) {
        int idx = -1;
        for (size_t i = 0; i < 6; i++) if (move->Button == WideToUtf8(kNormalButtons[i])) idx = static_cast<int>(i);
        SetComboSel(ComboMoveButton, idx);
        EnableWindow(ComboMoveButton, isNormal);
    }
    if (ComboSpecialButton) {
        int idx = -1;
        for (size_t i = 0; i < 8; i++) if (move->Button == WideToUtf8(kAllButtons[i])) idx = static_cast<int>(i);
        SetComboSel(ComboSpecialButton, idx);
        EnableWindow(ComboSpecialButton, isSpecialOrSuper);
    }
    EditorCommandDigits = move->InputCommand;
    UpdateCommandPreview();
    for (HWND h : BtnDigit) if (h) EnableWindow(h, isSpecialOrSuper);
    if (BtnCommandClear) EnableWindow(BtnCommandClear, isSpecialOrSuper);
    if (ChkDynamicHitbox) SendMessageW(ChkDynamicHitbox, BM_SETCHECK, move->HasDynamicHitbox ? BST_CHECKED : BST_UNCHECKED, 0);

    EditorHitboxDraftList = move->Hitboxes;
    EditorHitboxIndex = 0;
    RebuildHitboxCombo();
    SyncHitboxFieldsFromDraft();
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::RebuildHitboxCombo() {
    if (!ComboHitboxIndex) return;
    SendMessageW(ComboHitboxIndex, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < EditorHitboxDraftList.size(); i++) {
        std::wstring s = (EditorLanguage == 1 ? L"判定 " : L"HITBOX ") + std::to_wstring(i + 1);
        SendMessageW(ComboHitboxIndex, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
    }
    if (EditorHitboxIndex >= static_cast<int>(EditorHitboxDraftList.size())) EditorHitboxIndex = static_cast<int>(EditorHitboxDraftList.size()) - 1;
    if (EditorHitboxIndex < 0 && !EditorHitboxDraftList.empty()) EditorHitboxIndex = 0;
    if (!EditorHitboxDraftList.empty()) SetComboSel(ComboHitboxIndex, EditorHitboxIndex);
    bool has = !EditorHitboxDraftList.empty();
    EnableWindow(EditHitX, has); EnableWindow(EditHitY, has); EnableWindow(EditHitW, has); EnableWindow(EditHitH, has);
}

void App::RebuildHurtPartCombo() {
    if (!ComboHurtPart) return;
    auto& parts = EditorHurtboxDraft.PartsForStance(EditorHurtStance == 1 ? "crouch" : EditorHurtStance == 2 ? "air" : "stand");
    SendMessageW(ComboHurtPart, CB_RESETCONTENT, 0, 0);
    for (const auto& part : parts) {
        std::wstring s = PartDisplayName(part.Name, EditorLanguage);
        SendMessageW(ComboHurtPart, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
    }
    if (EditorHurtPartIndex >= static_cast<int>(parts.size())) EditorHurtPartIndex = static_cast<int>(parts.size()) - 1;
    if (EditorHurtPartIndex < 0 && !parts.empty()) EditorHurtPartIndex = 0;
    if (!parts.empty()) SetComboSel(ComboHurtPart, EditorHurtPartIndex);
}

void App::UpdateCommandPreview() {
    if (!EditCommandPreview) return;
    SetEditText(EditCommandPreview, Utf8ToWide(EditorCommandDigits));
}

void App::SyncHitboxFieldsFromDraft() {
    if (!EditHitX) return;
    if (EditorHitboxIndex < 0 || EditorHitboxIndex >= static_cast<int>(EditorHitboxDraftList.size())) {
        SetEditText(EditHitX, L""); SetEditText(EditHitY, L""); SetEditText(EditHitW, L""); SetEditText(EditHitH, L"");
        return;
    }
    HitboxDef box = EditorHitboxDraftList[EditorHitboxIndex];
    g_SyncingBoxFields = true;
    SetEditDouble(EditHitX, box.offsetX);
    SetEditDouble(EditHitY, box.offsetY);
    SetEditDouble(EditHitW, box.width);
    SetEditDouble(EditHitH, box.height);
    g_SyncingBoxFields = false;
}

void App::ApplyHitboxFieldsToDraft() {
    if (!EditHitX || g_SyncingBoxFields) return;
    if (EditorHitboxIndex < 0 || EditorHitboxIndex >= static_cast<int>(EditorHitboxDraftList.size())) return;
    HitboxDef& box = EditorHitboxDraftList[EditorHitboxIndex];
    double x = GetEditDouble(EditHitX, box.offsetX);
    double y = GetEditDouble(EditHitY, box.offsetY);
    double w = GetEditDouble(EditHitW, box.width);
    double h = GetEditDouble(EditHitH, box.height);
    ClampBoxToPreview(x, y, w, h);
    box.offsetX = x; box.offsetY = y; box.width = w; box.height = h;
}

static RectBox& HurtboxForStance(HurtboxSet& set, int stance, int partIndex) {
    auto& parts = set.PartsForStance(stance == 1 ? "crouch" : stance == 2 ? "air" : "stand");
    if (parts.empty()) parts.push_back({"body", RectBox{0, -40, 40, 80}});
    if (partIndex < 0 || partIndex >= static_cast<int>(parts.size())) partIndex = 0;
    return parts[partIndex].Box;
}

void App::SyncHurtboxFieldsFromDraft() {
    if (!EditHurtX) return;
    RectBox box = HurtboxForStance(EditorHurtboxDraft, EditorHurtStance, EditorHurtPartIndex);
    g_SyncingBoxFields = true;
    SetEditDouble(EditHurtX, box.CenterX);
    SetEditDouble(EditHurtY, box.CenterY);
    SetEditDouble(EditHurtW, box.Width);
    SetEditDouble(EditHurtH, box.Height);
    g_SyncingBoxFields = false;
}

void App::ApplyHurtboxFieldsToDraft() {
    if (!EditHurtX || g_SyncingBoxFields) return;
    RectBox& box = HurtboxForStance(EditorHurtboxDraft, EditorHurtStance, EditorHurtPartIndex);
    double cx = GetEditDouble(EditHurtX, box.CenterX);
    double cy = GetEditDouble(EditHurtY, box.CenterY);
    double w = GetEditDouble(EditHurtW, box.Width);
    double h = GetEditDouble(EditHurtH, box.Height);
    ClampBoxToPreview(cx, cy, w, h);
    box.CenterX = cx; box.CenterY = cy; box.Width = w; box.Height = h;
}

// Keeps a hitbox/hurtbox box fully representable within the preview
// panel's world-space span - typing a value directly into the X/Y/W/H
// fields could otherwise place a box far outside anything the preview
// (or, in practice, the visible play area) can show. Dragging in the
// preview was already implicitly bounded by the mouse staying inside the
// panel; this makes typed values follow the same limit.
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

void App::UpdateAdvantagePreview() {
    if (!LabelAdvantage) return;
    int startup = GetEditInt(EditStartup, 1);
    int active = GetEditInt(EditActive, 1);
    int recovery = GetEditInt(EditRecovery, 1);
    int hitstun = GetEditInt(EditHitstun, 0);
    int blockstun = GetEditInt(EditBlockstun, 0);
    (void)startup; (void)active;
    int onHit = hitstun - recovery;
    int onBlock = blockstun - recovery;
    std::wstring text = EditorLanguage == 1
        ? (L"ヒット時: " + std::to_wstring(onHit) + L"  ガード時: " + std::to_wstring(onBlock))
        : (L"On-hit: " + std::to_wstring(onHit) + L"  On-block: " + std::to_wstring(onBlock));
    SetEditText(LabelAdvantage, text);
}

void App::ApplyStatsForm() {
    if (EditorCharId.empty()) return;
    const auto* existing = Dm->GetCharacter(EditorCharId);
    if (!existing) return;
    CharacterStats stats = *existing;
    stats.Name = WideToUtf8(GetEditText(EditCharName));
    stats.MaxHP = GetEditInt(EditMaxHp, stats.MaxHP);
    stats.WalkForwardSpeed = GetEditDouble(EditWalkFwd, stats.WalkForwardSpeed);
    stats.WalkBackwardSpeed = GetEditDouble(EditWalkBack, stats.WalkBackwardSpeed);
    stats.DashSpeed = GetEditDouble(EditDash, stats.DashSpeed);
    stats.JumpVelocity = GetEditDouble(EditJumpVel, stats.JumpVelocity);
    stats.Gravity = GetEditDouble(EditGravity, stats.Gravity);
    ApplyHurtboxFieldsToDraft();
    stats.Hurtboxes = EditorHurtboxDraft;
    Dm->SaveCharacter(stats);
    PopulateCharacterCombo();
    PopulateTemplateCombo();
}

void App::ApplyMoveForm() {
    if (EditorCharId.empty() || EditorMoveId.empty()) return;
    const auto* existing = Dm->GetMove(EditorCharId, EditorMoveId);
    if (!existing) return;
    MoveData move = *existing;
    move.Name = WideToUtf8(GetEditText(EditMoveName));
    move.Startup = GetEditInt(EditStartup, move.Startup);
    move.Active = GetEditInt(EditActive, move.Active);
    move.Recovery = GetEditInt(EditRecovery, move.Recovery);
    move.TotalFrame = move.Startup + move.Active + move.Recovery;
    move.Damage = GetEditInt(EditDamage, move.Damage);
    move.Hitstun = GetEditInt(EditHitstun, move.Hitstun);
    move.Blockstun = GetEditInt(EditBlockstun, move.Blockstun);
    move.Hitstop = GetEditInt(EditHitstop, move.Hitstop);
    move.CancelStartFrame = GetEditInt(EditCancelStart, move.CancelStartFrame);
    move.CancelEndFrame = GetEditInt(EditCancelEnd, move.CancelEndFrame);

    bool isNormal = move.HasTag("Normal");
    bool isSpecialOrSuper = move.HasTag("Special") || move.HasTag("Super");
    if (isNormal && ComboMoveButton) {
        int idx = static_cast<int>(SendMessageW(ComboMoveButton, CB_GETCURSEL, 0, 0));
        if (idx >= 0 && idx < 6) move.Button = WideToUtf8(kNormalButtons[idx]);
    } else if (isSpecialOrSuper) {
        if (ComboSpecialButton) {
            int idx = static_cast<int>(SendMessageW(ComboSpecialButton, CB_GETCURSEL, 0, 0));
            if (idx >= 0 && idx < 8) move.Button = WideToUtf8(kAllButtons[idx]);
        }
        move.InputCommand = EditorCommandDigits;
    }
    if (ChkDynamicHitbox) move.HasDynamicHitbox = SendMessageW(ChkDynamicHitbox, BM_GETCHECK, 0, 0) == BST_CHECKED;

    ApplyHitboxFieldsToDraft();
    move.Hitboxes = EditorHitboxDraftList;
    Dm->SaveMove(EditorCharId, move);
    PopulateMoveCombo();
}

void App::ShowCreateCharacterPrompt() {
    for (HWND h : g_NormalModeControls) ShowWindow(h, SW_HIDE);
    for (HWND h : g_CreateModeControls) ShowWindow(h, SW_SHOW);
}
void App::HideCreateCharacterPrompt() {
    for (HWND h : g_CreateModeControls) ShowWindow(h, SW_HIDE);
    for (HWND h : g_NormalModeControls) ShowWindow(h, SW_SHOW);
}

bool HasNonAscii(const std::wstring& text) {
    for (wchar_t c : text) if (c > 127) return true;
    return false;
}

// Draws label/button/combo-item text: the hand-authored pixel font for
// plain ASCII (guaranteed to render everywhere, matching the rest of the
// game), falling back to the normal GDI+ system font for anything
// containing non-ASCII - the pixel font only has Latin/digit/punctuation
// glyphs (see Draw.cpp), so Japanese label text (EditorLanguage == 1)
// would otherwise draw as nothing at all.
void DrawLabelText(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::RectF& rect, float dot, Gdiplus::Color color, bool centered) {
    if (HasNonAscii(text)) {
        Gdiplus::Font font(UiFontFamily(), dot * 6.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (centered) DrawTextCentered(g, text, font, rect, color);
        else DrawTextLeft(g, text, font, rect.X, rect.Y, color);
    } else {
        if (centered) DrawPixelTextCentered(g, text, rect, dot, color);
        else DrawPixelText(g, text, rect.X, rect.Y, dot, color);
    }
}

// Owner-draw for the editor's buttons/labels/combos (see MakeButton/
// MakeLabel/MakeCombo above) so they read as the same red/white rounded
// buttons, palette-colored labels, and pixel-font combo items as every
// other screen instead of plain system controls. SAVE CHARACTER/SAVE
// MOVE/CREATE are the primary (red) action in their respective forms; the
// rest are secondary (outlined).
void Editor_OnDrawItem(DRAWITEMSTRUCT* dis) {
    const auto& pal = GetPalette();
    Gdiplus::Graphics g(dis->hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (dis->CtlType == ODT_STATIC) {
        Gdiplus::RectF rect(0.0f, 0.0f, static_cast<Gdiplus::REAL>(dis->rcItem.right - dis->rcItem.left),
                             static_cast<Gdiplus::REAL>(dis->rcItem.bottom - dis->rcItem.top));
        Gdiplus::SolidBrush bg(pal.Bg);
        g.FillRectangle(&bg, rect);
        wchar_t text[256];
        GetWindowTextW(dis->hwndItem, text, 256);
        Gdiplus::RectF textRect(0.0f, (rect.Height - 14.0f) / 2.0f, rect.Width, 14.0f);
        DrawLabelText(g, text, textRect, 1.5f, pal.Ink, false);
        return;
    }

    if (dis->CtlType == ODT_COMBOBOX) {
        Gdiplus::RectF rect(0.0f, 0.0f, static_cast<Gdiplus::REAL>(dis->rcItem.right - dis->rcItem.left),
                             static_cast<Gdiplus::REAL>(dis->rcItem.bottom - dis->rcItem.top));
        bool selected = (dis->itemState & ODS_SELECTED) != 0;
        Gdiplus::SolidBrush bg(selected ? pal.Accent : pal.PanelBg);
        g.FillRectangle(&bg, rect);
        // itemID is -1 when redrawing the closed combo's own selection
        // field rather than a dropdown list row (also seen, under Wine, on
        // the very first paint of a list row before hover/selection ever
        // touches it) - fall back to the control's actual current
        // selection so the chosen item's text still shows.
        int idx = static_cast<int>(dis->itemID);
        if (idx < 0) idx = static_cast<int>(SendMessageW(dis->hwndItem, CB_GETCURSEL, 0, 0));
        if (idx >= 0) {
            wchar_t text[256] = L"";
            SendMessageW(dis->hwndItem, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(text));
            Gdiplus::RectF textRect(4.0f, (rect.Height - 14.0f) / 2.0f, rect.Width - 8.0f, 14.0f);
            DrawLabelText(g, text, textRect, 2.0f, selected ? pal.White : pal.TextDark, false);
        }
        if (dis->itemState & ODS_FOCUS) {
            Gdiplus::Pen focusPen(pal.Ink, 1.0f);
            g.DrawRectangle(&focusPen, 0.0f, 0.0f, rect.Width - 1.0f, rect.Height - 1.0f);
        }
        return;
    }

    // ODT_BUTTON.
    bool primary = (dis->CtlID == ID_BTN_SAVE || dis->CtlID == ID_BTN_SAVE_MOVE || dis->CtlID == ID_BTN_CREATE_CONFIRM);
    // The two drag-mode toggle buttons double as a radio group - whichever
    // one matches the live EditorDragTargetIsHurtbox state reads as "on"
    // (primary/red) so the user can see which box the preview canvas will
    // move when they drag.
    if (g_App) {
        if (dis->CtlID == ID_BTN_DRAG_HITBOX) primary = !g_App->EditorDragTargetIsHurtbox;
        else if (dis->CtlID == ID_BTN_DRAG_HURTBOX) primary = g_App->EditorDragTargetIsHurtbox;
    }
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    wchar_t text[128];
    GetWindowTextW(dis->hwndItem, text, 128);

    Gdiplus::RectF rect(0.0f, 0.0f, static_cast<Gdiplus::REAL>(dis->rcItem.right - dis->rcItem.left),
                         static_cast<Gdiplus::REAL>(dis->rcItem.bottom - dis->rcItem.top));
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, 0.0f);

    if (primary) {
        Gdiplus::SolidBrush fill(disabled ? pal.Border : (pressed ? pal.AccentDark : pal.Accent));
        g.FillPath(&fill, &path);
    } else {
        Gdiplus::SolidBrush fill(pressed ? pal.PanelBg2 : pal.PanelBg);
        g.FillPath(&fill, &path);
        Gdiplus::Pen border(pal.Border, 2.0f);
        g.DrawPath(&border, &path);
    }

    float dot = rect.Height >= 34.0f ? 2.0f : 1.5f;
    Gdiplus::Color textColor = disabled ? pal.Ink45 : (primary ? pal.White : pal.TextDark);
    DrawLabelText(g, text, rect, dot, textColor, true);
}

void Editor_OnCommand(App& app, int controlId, int notifyCode, HWND ctrl) {
    (void)ctrl;
    if (controlId == ID_COMBO_CHARACTER && notifyCode == CBN_SELCHANGE) {
        int idx = static_cast<int>(SendMessageW(app.ComboCharacter, CB_GETCURSEL, 0, 0));
        if (idx >= 0 && idx < static_cast<int>(app.Dm->GetCharacterIds().size())) {
            app.LoadCharacterIntoForm(app.Dm->GetCharacterIds()[idx]);
        }
    } else if (controlId == ID_COMBO_MOVE && notifyCode == CBN_SELCHANGE) {
        int idx = static_cast<int>(SendMessageW(app.ComboMove, CB_GETCURSEL, 0, 0));
        if (idx >= 0 && idx < static_cast<int>(g_EditorMoveIdsInCombo.size())) {
            app.LoadMoveIntoForm(g_EditorMoveIdsInCombo[idx]);
        }
    } else if (controlId == ID_BTN_LANGUAGE && notifyCode == BN_CLICKED) {
        app.EditorLanguage = app.EditorLanguage == 1 ? 0 : 1;
        app.ApplyEditorLanguage();
    } else if (controlId == ID_BTN_SAVE && notifyCode == BN_CLICKED) {
        app.ApplyStatsForm();
        MessageBoxW(app.Hwnd, app.EditorLanguage == 1 ? L"キャラクターを保存しました。" : L"Character saved.",
                    L"Kakuge", MB_OK | MB_ICONINFORMATION);
    } else if (controlId == ID_BTN_SAVE_MOVE && notifyCode == BN_CLICKED) {
        app.ApplyMoveForm();
        MessageBoxW(app.Hwnd, app.EditorLanguage == 1 ? L"技を保存しました。" : L"Move saved.",
                    L"Kakuge", MB_OK | MB_ICONINFORMATION);
    } else if (controlId == ID_BTN_NEW_CHARACTER && notifyCode == BN_CLICKED) {
        app.EditorCreatingNew = true;
        app.ShowCreateCharacterPrompt();
    } else if (controlId == ID_BTN_CREATE_CANCEL && notifyCode == BN_CLICKED) {
        app.EditorCreatingNew = false;
        app.HideCreateCharacterPrompt();
    } else if (controlId == ID_BTN_CREATE_CONFIRM && notifyCode == BN_CLICKED) {
        std::string newId = WideToUtf8(GetEditText(app.EditCharId));
        std::string newName = WideToUtf8(GetEditText(GetDlgItem(app.Hwnd, ID_EDIT_NEW_NAME)));
        int tIdx = static_cast<int>(SendMessageW(app.ComboTemplate, CB_GETCURSEL, 0, 0));
        if (newId.empty() || tIdx < 0 || tIdx >= static_cast<int>(app.Dm->GetCharacterIds().size())) {
            MessageBoxW(app.Hwnd, app.EditorLanguage == 1 ? L"IDを入力し、コピー元を選んでください。" : L"Enter an id and pick a template.",
                        L"Kakuge", MB_OK | MB_ICONWARNING);
            return;
        }
        std::string templateId = app.Dm->GetCharacterIds()[tIdx];
        bool ok = app.Dm->CreateCharacter(newId, newName, templateId);
        if (!ok) {
            MessageBoxW(app.Hwnd, app.EditorLanguage == 1 ? L"そのIDは既に使われています(または無効です)。" : L"That id is already taken (or invalid).",
                        L"Kakuge", MB_OK | MB_ICONWARNING);
            return;
        }
        app.EditorCreatingNew = false;
        app.HideCreateCharacterPrompt();
        app.LoadCharacterIntoForm(newId); // sets EditorCharId before the combo repopulates, so it selects the new entry
        app.PopulateCharacterCombo();
        app.PopulateTemplateCombo();
        MessageBoxW(app.Hwnd, app.EditorLanguage == 1 ? L"新しいキャラクターを作成しました。" : L"New character created.",
                    L"Kakuge", MB_OK | MB_ICONINFORMATION);
    } else if (controlId == ID_BTN_BACK && notifyCode == BN_CLICKED) {
        app.GoTo(Screen::Title);
    } else if ((controlId == ID_EDIT_STARTUP || controlId == ID_EDIT_RECOVERY || controlId == ID_EDIT_HITSTUN || controlId == ID_EDIT_BLOCKSTUN) && notifyCode == EN_CHANGE) {
        app.UpdateAdvantagePreview();
    } else if (controlId == ID_COMBO_HURT_STANCE && notifyCode == CBN_SELCHANGE) {
        app.ApplyHurtboxFieldsToDraft(); // keep whatever was typed for the old stance/part
        app.EditorHurtStance = static_cast<int>(SendMessageW(app.ComboHurtStance, CB_GETCURSEL, 0, 0));
        app.EditorHurtPartIndex = 0;
        app.RebuildHurtPartCombo();
        app.SyncHurtboxFieldsFromDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_COMBO_HURT_PART && notifyCode == CBN_SELCHANGE) {
        app.ApplyHurtboxFieldsToDraft();
        app.EditorHurtPartIndex = static_cast<int>(SendMessageW(app.ComboHurtPart, CB_GETCURSEL, 0, 0));
        app.SyncHurtboxFieldsFromDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_ADD_PART && notifyCode == BN_CLICKED) {
        app.ApplyHurtboxFieldsToDraft();
        auto& parts = app.EditorHurtboxDraft.PartsForStance(app.EditorHurtStance == 1 ? "crouch" : app.EditorHurtStance == 2 ? "air" : "stand");
        std::string newName;
        for (const char* preset : kPartPresets) {
            bool taken = false;
            for (const auto& p : parts) if (p.Name == preset) { taken = true; break; }
            if (!taken) { newName = preset; break; }
        }
        if (newName.empty()) newName = "part" + std::to_string(parts.size() + 1);
        parts.push_back({newName, RectBox{0, -40, 20, 30}});
        app.EditorHurtPartIndex = static_cast<int>(parts.size()) - 1;
        app.RebuildHurtPartCombo();
        app.SyncHurtboxFieldsFromDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_REMOVE_PART && notifyCode == BN_CLICKED) {
        auto& parts = app.EditorHurtboxDraft.PartsForStance(app.EditorHurtStance == 1 ? "crouch" : app.EditorHurtStance == 2 ? "air" : "stand");
        if (parts.size() > 1 && app.EditorHurtPartIndex >= 0 && app.EditorHurtPartIndex < static_cast<int>(parts.size())) {
            parts.erase(parts.begin() + app.EditorHurtPartIndex);
            if (app.EditorHurtPartIndex >= static_cast<int>(parts.size())) app.EditorHurtPartIndex = static_cast<int>(parts.size()) - 1;
            app.RebuildHurtPartCombo();
            app.SyncHurtboxFieldsFromDraft();
            InvalidateRect(app.Hwnd, nullptr, FALSE);
        }
    } else if (controlId == ID_COMBO_HITBOX_INDEX && notifyCode == CBN_SELCHANGE) {
        app.ApplyHitboxFieldsToDraft();
        app.EditorHitboxIndex = static_cast<int>(SendMessageW(app.ComboHitboxIndex, CB_GETCURSEL, 0, 0));
        app.SyncHitboxFieldsFromDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_ADD_HITBOX && notifyCode == BN_CLICKED) {
        app.ApplyHitboxFieldsToDraft();
        app.EditorHitboxDraftList.push_back(HitboxDef{});
        app.EditorHitboxIndex = static_cast<int>(app.EditorHitboxDraftList.size()) - 1;
        app.RebuildHitboxCombo();
        app.SyncHitboxFieldsFromDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_REMOVE_HITBOX && notifyCode == BN_CLICKED) {
        if (app.EditorHitboxIndex >= 0 && app.EditorHitboxIndex < static_cast<int>(app.EditorHitboxDraftList.size())) {
            app.EditorHitboxDraftList.erase(app.EditorHitboxDraftList.begin() + app.EditorHitboxIndex);
            if (app.EditorHitboxIndex >= static_cast<int>(app.EditorHitboxDraftList.size())) app.EditorHitboxIndex = static_cast<int>(app.EditorHitboxDraftList.size()) - 1;
            app.RebuildHitboxCombo();
            app.SyncHitboxFieldsFromDraft();
            InvalidateRect(app.Hwnd, nullptr, FALSE);
        }
    } else if ((controlId == ID_EDIT_HIT_X || controlId == ID_EDIT_HIT_Y || controlId == ID_EDIT_HIT_W || controlId == ID_EDIT_HIT_H) && notifyCode == EN_CHANGE) {
        app.ApplyHitboxFieldsToDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if ((controlId == ID_EDIT_HURT_X || controlId == ID_EDIT_HURT_Y || controlId == ID_EDIT_HURT_W || controlId == ID_EDIT_HURT_H) && notifyCode == EN_CHANGE) {
        app.ApplyHurtboxFieldsToDraft();
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_DRAG_HITBOX && notifyCode == BN_CLICKED) {
        app.EditorDragTargetIsHurtbox = false;
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId == ID_BTN_DRAG_HURTBOX && notifyCode == BN_CLICKED) {
        app.EditorDragTargetIsHurtbox = true;
        InvalidateRect(app.Hwnd, nullptr, FALSE);
    } else if (controlId >= ID_BTN_DIGIT_BASE + 1 && controlId <= ID_BTN_DIGIT_BASE + 9 && notifyCode == BN_CLICKED) {
        app.EditorCommandDigits += std::to_string(controlId - ID_BTN_DIGIT_BASE);
        app.UpdateCommandPreview();
    } else if (controlId == ID_BTN_COMMAND_CLEAR && notifyCode == BN_CLICKED) {
        app.EditorCommandDigits.clear();
        app.UpdateCommandPreview();
    }
}

// Custom-painted (not a child control) preview of every hitbox (red) and
// every hurtbox part for the selected stance (blue) over a small humanoid
// silhouette, drawn in real window pixels - see App::OnLButtonDown/
// OnMouseMove for the drag-to-edit half of this. Hitbox/hurtbox offsetX/Y
// are already in the same coordinate space as the in-game virtual canvas
// (see MoveExecutor::GetActiveHitboxRects), so EditorPreviewScale is a
// plain zoom factor - no per-part unit conversion needed. The currently
// selected box (per EditorHitboxIndex / EditorHurtPartIndex, whichever
// EditorDragTargetIsHurtbox names) draws bolder with a resize handle;
// every other box on the same stance/move still draws as a thin outline
// so the whole set stays visible while only one is being edited.
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

    Gdiplus::Color bodyColor(255, 210, 205, 205);
    DrawHumanoid(g, groundX, groundY, bodyColor, {static_cast<double>(EditorPreviewScale), 1});

    auto drawBox = [&](double cx, double cy, double w, double h, Gdiplus::Color color, bool active) {
        float pcx = groundX + static_cast<float>(cx * EditorPreviewScale);
        float pcy = groundY + static_cast<float>(cy * EditorPreviewScale);
        float pw = static_cast<float>(w * EditorPreviewScale), ph = static_cast<float>(h * EditorPreviewScale);
        Gdiplus::RectF br(pcx - pw / 2.0f, pcy - ph / 2.0f, pw, ph);
        Gdiplus::Pen pen(color, active ? 2.5f : 1.0f);
        g.DrawRectangle(&pen, br);
        if (active) {
            // Resize-handle marker at the bottom-right corner.
            Gdiplus::SolidBrush handleBrush(color);
            g.FillRectangle(&handleBrush, br.X + br.Width - 5, br.Y + br.Height - 5, 10.0f, 10.0f);
        }
    };

    const char* stanceName = EditorHurtStance == 1 ? "crouch" : EditorHurtStance == 2 ? "air" : "stand";
    auto& hurtParts = EditorHurtboxDraft.PartsForStance(stanceName);
    for (size_t i = 0; i < hurtParts.size(); i++) {
        bool active = EditorDragTargetIsHurtbox && static_cast<int>(i) == EditorHurtPartIndex;
        drawBox(hurtParts[i].Box.CenterX, hurtParts[i].Box.CenterY, hurtParts[i].Box.Width, hurtParts[i].Box.Height,
                Gdiplus::Color(255, 50, 110, 230), active);
    }
    for (size_t i = 0; i < EditorHitboxDraftList.size(); i++) {
        bool active = !EditorDragTargetIsHurtbox && static_cast<int>(i) == EditorHitboxIndex;
        const HitboxDef& hb = EditorHitboxDraftList[i];
        drawBox(hb.offsetX, hb.offsetY, hb.width, hb.height, Gdiplus::Color(255, 230, 30, 30), active);
    }

    Gdiplus::RectF legendRect(r.X, r.Y + r.Height + 4, r.Width, 16.0f);
    DrawLabelText(g, Str(EStr::Legend, EditorLanguage), legendRect, 1.5f, pal.Ink55, false);
}

} // namespace kakuge

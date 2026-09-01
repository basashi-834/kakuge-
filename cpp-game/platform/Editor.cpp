// platform/Editor.cpp
// Character Editor screen. Unlike the other screens, this one is data-
// entry heavy, so it uses real native Win32 child controls (Edit/
// ComboBox/Button) instead of the custom GDI+ button system - simpler and
// more robust for text/number entry than hand-rolling a text widget.
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

std::vector<std::string> g_EditorMoveIdsInCombo;
std::vector<HWND> g_NormalModeControls;
std::vector<HWND> g_CreateModeControls;
HWND g_LblCreateId = nullptr, g_LblCreateName = nullptr, g_LblCreateTemplate = nullptr;

HWND MakeLabel(HWND parent, HINSTANCE hInst, int x, int y, int w, int h, const std::wstring& text) {
    HWND h2 = CreateWindowExW(0, L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE,
                               x, y, w, h, parent, nullptr, hInst, nullptr);
    return h2;
}
HWND MakeEdit(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}
HWND MakeButton(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h, const std::wstring& text) {
    return CreateWindowExW(0, L"BUTTON", text.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}
HWND MakeCombo(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                            x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
}

std::wstring GetEditText(HWND h) {
    wchar_t buf[256];
    GetWindowTextW(h, buf, 256);
    return buf;
}
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

} // namespace

void App::EnterEditor() {
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(Hwnd, GWLP_HINSTANCE));

    GetWindowRect(Hwnd, &PreEditorWindowRect);
    EditorSizeSaved = true;
    RECT rc{0, 0, 1040, 760};
    AdjustWindowRect(&rc, static_cast<DWORD>(GetWindowLongPtr(Hwnd, GWL_STYLE)), FALSE);
    SetWindowPos(Hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    CreateEditorControls();
    LayoutEditorControls();
    PopulateCharacterCombo();
    PopulateMoveCombo();
    PopulateTemplateCombo();
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

    // ---- Left column: character stats ----
    // Every label here is captured into g_NormalModeControls too (not just
    // the edit/combo/button controls) so the "+ NEW CHARACTER" overlay
    // (which reuses this same screen region) doesn't leave stray label
    // text showing through underneath it.
    HWND lbl;
    lbl = MakeLabel(Hwnd, hInst, 20, 16, 200, 20, L"CHARACTER"); g_NormalModeControls.push_back(lbl);
    ComboCharacter = MakeCombo(Hwnd, hInst, ID_COMBO_CHARACTER, 20, 36, 260, 200);
    lbl = MakeLabel(Hwnd, hInst, 20, 68, 200, 20, L"NAME"); g_NormalModeControls.push_back(lbl);
    EditCharName = MakeEdit(Hwnd, hInst, ID_EDIT_CHAR_NAME, 20, 88, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 120, 200, 20, L"MAX HP"); g_NormalModeControls.push_back(lbl);
    EditMaxHp = MakeEdit(Hwnd, hInst, ID_EDIT_MAX_HP, 20, 140, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 172, 200, 20, L"WALK FORWARD SPEED"); g_NormalModeControls.push_back(lbl);
    EditWalkFwd = MakeEdit(Hwnd, hInst, ID_EDIT_WALK_FWD, 20, 192, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 224, 200, 20, L"WALK BACKWARD SPEED"); g_NormalModeControls.push_back(lbl);
    EditWalkBack = MakeEdit(Hwnd, hInst, ID_EDIT_WALK_BACK, 20, 244, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 276, 200, 20, L"DASH SPEED"); g_NormalModeControls.push_back(lbl);
    EditDash = MakeEdit(Hwnd, hInst, ID_EDIT_DASH, 20, 296, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 328, 200, 20, L"JUMP VELOCITY (negative = up)"); g_NormalModeControls.push_back(lbl);
    EditJumpVel = MakeEdit(Hwnd, hInst, ID_EDIT_JUMP_VEL, 20, 348, 260, 24);
    lbl = MakeLabel(Hwnd, hInst, 20, 380, 200, 20, L"GRAVITY"); g_NormalModeControls.push_back(lbl);
    EditGravity = MakeEdit(Hwnd, hInst, ID_EDIT_GRAVITY, 20, 400, 260, 24);
    BtnSave = MakeButton(Hwnd, hInst, ID_BTN_SAVE, 20, 440, 260, 36, L"SAVE CHARACTER");
    BtnNewCharacter = MakeButton(Hwnd, hInst, ID_BTN_NEW_CHARACTER, 20, 484, 260, 36, L"+ NEW CHARACTER");

    for (HWND h : {ComboCharacter, EditCharName, EditMaxHp, EditWalkFwd, EditWalkBack, EditDash, EditJumpVel, EditGravity, BtnSave, BtnNewCharacter}) {
        g_NormalModeControls.push_back(h);
    }

    // ---- Create-new-character overlay (shares the left column) ----
    g_LblCreateId = MakeLabel(Hwnd, hInst, 20, 16, 260, 20, L"NEW CHARACTER ID (e.g. ken)");
    EditCharId = MakeEdit(Hwnd, hInst, ID_EDIT_NEW_ID, 20, 36, 260, 24);
    g_LblCreateName = MakeLabel(Hwnd, hInst, 20, 68, 260, 20, L"DISPLAY NAME");
    // reuse EditCharName isn't safe here since it's shared with normal mode; make a dedicated one.
    HWND editNewName = MakeEdit(Hwnd, hInst, ID_EDIT_NEW_NAME, 20, 88, 260, 24);
    g_LblCreateTemplate = MakeLabel(Hwnd, hInst, 20, 120, 260, 20, L"CLONE MOVESET FROM");
    ComboTemplate = MakeCombo(Hwnd, hInst, ID_COMBO_TEMPLATE, 20, 140, 260, 200);
    BtnCreateConfirm = MakeButton(Hwnd, hInst, ID_BTN_CREATE_CONFIRM, 20, 180, 260, 36, L"CREATE");
    BtnCreateCancel = MakeButton(Hwnd, hInst, ID_BTN_CREATE_CANCEL, 20, 224, 260, 36, L"CANCEL");
    for (HWND h : {g_LblCreateId, EditCharId, g_LblCreateName, editNewName, g_LblCreateTemplate, ComboTemplate, BtnCreateConfirm, BtnCreateCancel}) {
        g_CreateModeControls.push_back(h);
    }

    // ---- Right column: move editor ----
    int rx = 340;
    lbl = MakeLabel(Hwnd, hInst, rx, 16, 260, 20, L"MOVE"); g_NormalModeControls.push_back(lbl);
    ComboMove = MakeCombo(Hwnd, hInst, ID_COMBO_MOVE, rx, 36, 300, 300);
    lbl = MakeLabel(Hwnd, hInst, rx, 68, 260, 20, L"DISPLAY NAME"); g_NormalModeControls.push_back(lbl);
    EditMoveName = MakeEdit(Hwnd, hInst, ID_EDIT_MOVE_NAME, rx, 88, 300, 24);
    lbl = MakeLabel(Hwnd, hInst, rx, 120, 90, 20, L"STARTUP"); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + 100, 120, 90, 20, L"ACTIVE"); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + 200, 120, 90, 20, L"RECOVERY"); g_NormalModeControls.push_back(lbl);
    EditStartup = MakeEdit(Hwnd, hInst, ID_EDIT_STARTUP, rx, 140, 90, 24);
    EditActive = MakeEdit(Hwnd, hInst, ID_EDIT_ACTIVE, rx + 100, 140, 90, 24);
    EditRecovery = MakeEdit(Hwnd, hInst, ID_EDIT_RECOVERY, rx + 200, 140, 90, 24);
    lbl = MakeLabel(Hwnd, hInst, rx, 172, 260, 20, L"DAMAGE"); g_NormalModeControls.push_back(lbl);
    EditDamage = MakeEdit(Hwnd, hInst, ID_EDIT_DAMAGE, rx, 192, 300, 24);
    lbl = MakeLabel(Hwnd, hInst, rx, 224, 90, 20, L"HITSTUN"); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + 100, 224, 90, 20, L"BLOCKSTUN"); g_NormalModeControls.push_back(lbl);
    lbl = MakeLabel(Hwnd, hInst, rx + 200, 224, 90, 20, L"HITSTOP"); g_NormalModeControls.push_back(lbl);
    EditHitstun = MakeEdit(Hwnd, hInst, ID_EDIT_HITSTUN, rx, 244, 90, 24);
    EditBlockstun = MakeEdit(Hwnd, hInst, ID_EDIT_BLOCKSTUN, rx + 100, 244, 90, 24);
    EditHitstop = MakeEdit(Hwnd, hInst, ID_EDIT_HITSTOP, rx + 200, 244, 90, 24);
    LabelAdvantage = MakeLabel(Hwnd, hInst, rx, 280, 300, 40, L"On-hit: -  On-block: -");
    HWND btnSaveMove = MakeButton(Hwnd, hInst, ID_BTN_SAVE_MOVE, rx, 330, 300, 36, L"SAVE MOVE");
    for (HWND h : {ComboMove, EditMoveName, EditStartup, EditActive, EditRecovery, EditDamage, EditHitstun, EditBlockstun, EditHitstop, LabelAdvantage, btnSaveMove}) {
        g_NormalModeControls.push_back(h);
    }

    BtnBack = MakeButton(Hwnd, hInst, ID_BTN_BACK, 20, 700, 260, 36, L"BACK TO TITLE");
}

void App::DestroyEditorControls() {
    for (HWND& h : EditorControls) { if (h) DestroyWindow(h); h = nullptr; }
    EditorControls.clear();
    auto destroyAll = [](std::vector<HWND>& v) { for (HWND h : v) if (h) DestroyWindow(h); v.clear(); };
    destroyAll(g_NormalModeControls);
    destroyAll(g_CreateModeControls);
    if (BtnBack) { DestroyWindow(BtnBack); BtnBack = nullptr; }
    ComboCharacter = ComboMove = ComboTemplate = nullptr;
    EditCharId = EditCharName = EditMaxHp = EditWalkFwd = EditWalkBack = EditDash = EditJumpVel = EditGravity = nullptr;
    EditMoveName = EditStartup = EditActive = EditRecovery = EditDamage = EditHitstun = EditBlockstun = EditHitstop = nullptr;
    LabelAdvantage = nullptr;
    BtnSave = BtnNewCharacter = BtnCreateConfirm = BtnCreateCancel = nullptr;
}

void App::LayoutEditorControls() {
    // Fixed absolute layout (set at creation time above); nothing extra to
    // do here since the editor window doesn't resize.
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
    if (!ids.empty()) SendMessageW(ComboCharacter, CB_SETCURSEL, selectIndex, 0);
}

void App::PopulateTemplateCombo() {
    if (!ComboTemplate) return;
    SendMessageW(ComboTemplate, CB_RESETCONTENT, 0, 0);
    for (const auto& id : Dm->GetCharacterIds()) {
        const auto* stats = Dm->GetCharacter(id);
        std::wstring display = Utf8ToWide((stats ? stats->Name : id) + " (" + id + ")");
        SendMessageW(ComboTemplate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
    if (!Dm->GetCharacterIds().empty()) SendMessageW(ComboTemplate, CB_SETCURSEL, 0, 0);
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
        SendMessageW(ComboMove, CB_SETCURSEL, 0, 0);
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
    UpdateAdvantagePreview();
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
    std::wstring text = L"On-hit: " + std::to_wstring(onHit) + L"  On-block: " + std::to_wstring(onBlock);
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
    } else if (controlId == ID_BTN_SAVE && notifyCode == BN_CLICKED) {
        app.ApplyStatsForm();
        MessageBoxW(app.Hwnd, L"Character saved.", L"Kakuge", MB_OK | MB_ICONINFORMATION);
    } else if (controlId == ID_BTN_SAVE_MOVE && notifyCode == BN_CLICKED) {
        app.ApplyMoveForm();
        MessageBoxW(app.Hwnd, L"Move saved.", L"Kakuge", MB_OK | MB_ICONINFORMATION);
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
            MessageBoxW(app.Hwnd, L"Enter an id and pick a template.", L"Kakuge", MB_OK | MB_ICONWARNING);
            return;
        }
        std::string templateId = app.Dm->GetCharacterIds()[tIdx];
        bool ok = app.Dm->CreateCharacter(newId, newName, templateId);
        if (!ok) {
            MessageBoxW(app.Hwnd, L"That id is already taken (or invalid).", L"Kakuge", MB_OK | MB_ICONWARNING);
            return;
        }
        app.EditorCreatingNew = false;
        app.HideCreateCharacterPrompt();
        app.LoadCharacterIntoForm(newId); // sets EditorCharId before the combo repopulates, so it selects the new entry
        app.PopulateCharacterCombo();
        app.PopulateTemplateCombo();
        MessageBoxW(app.Hwnd, L"New character created.", L"Kakuge", MB_OK | MB_ICONINFORMATION);
    } else if (controlId == ID_BTN_BACK && notifyCode == BN_CLICKED) {
        app.GoTo(Screen::Title);
    } else if ((controlId == ID_EDIT_STARTUP || controlId == ID_EDIT_RECOVERY || controlId == ID_EDIT_HITSTUN || controlId == ID_EDIT_BLOCKSTUN) && notifyCode == EN_CHANGE) {
        app.UpdateAdvantagePreview();
    }
}

} // namespace kakuge

// platform/App.h
// Central application/screen state machine. Menu-ish screens (Title,
// Character Select, VS, Result, Settings, and the in-Game pause overlay)
// are drawn entirely with GDI+ and hit-tested via UiButton rects in
// virtual-canvas space (see Layout.h) - no native HWND controls needed
// there. The Character Editor is data-entry heavy, so it alone uses real
// Win32 child controls (Edit/ComboBox/Button), created on entry and
// destroyed on exit.
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <chrono>
#include "Layout.h"
#include "Palette.h"
#include "Draw.h"
#include "../engine/DataManager.h"
#include "../engine/BattleSystem.h"
#include "../engine/Settings.h"

namespace kakuge {

enum class Screen { Title, CharacterSelect, VS, Game, Result, Settings, Editor };

// Height of the Editor screen's red GDI+-drawn header bar (real window
// pixels) - native controls are laid out starting below it.
constexpr int kEditorHeaderHeight = 60;

struct SelectSlotVisual {
    std::string id;
    bool isAddTile = false;
};

class App {
public:
    HWND Hwnd = nullptr;
    fs::path BaseDataDir;
    fs::path UserDir;
    std::unique_ptr<DataManager> Dm;
    Settings AppSettings;

    Screen Current = Screen::Title;
    std::vector<UiButton> Buttons;

    // Off-screen back buffer: every screen is drawn into this bitmap, then
    // blitted to the window in one shot at the end of OnPaint. Without it,
    // each individual GDI+ fill/line/path call would be briefly visible on
    // the real window surface as it happens (visible flicker, worse the
    // busier a frame is - reported on a real machine after the WM_PAINT
    // Timer-driven redraws every ~15ms started hitting real hardware).
    std::unique_ptr<Gdiplus::Bitmap> BackBuffer;
    int BackBufferW = 0, BackBufferH = 0;
    // Fixed 384x224 pixel-art canvas every non-Editor screen actually draws
    // into (see App::OnPaint) - never resized, unlike BackBuffer.
    std::unique_ptr<Gdiplus::Bitmap> LowResBuffer;

    // ---- Mode (set from Title before entering CharacterSelect) ----
    // Training mode: unlimited round timer, neither KO ends the match, and
    // the CPU-behavior/auto-heal controls live in the pause menu instead
    // of a normal Versus match's pause menu (which stays simple).
    bool IsTrainingMode = false;
    bool TrainingAutoHealPref = true; // remembered across rematches this session

    // ---- Character Select state ----
    int SelectStep = 0; // 0 = picking P1, 1 = picking P2 (CPU)
    std::string P1CharId, P2CharId;
    int SelectHoverIndex = -1;

    // ---- VS screen state ----
    double VsTimer = 0.0;

    // ---- Game screen state ----
    std::unique_ptr<BattleSystem> Battle;
    std::unordered_set<int> HeldKeys;
    bool DebugVisible = false;
    bool Paused = false;
    std::vector<LiveEffect> Effects;
    std::chrono::steady_clock::time_point LastTick;
    double Accumulator = 0.0;
    static constexpr double FixedDt = 1.0 / 60.0;
    bool MatchFinished = false;
    DummyMode P2DummyMode = DummyMode::CPU;
    double P1ComboFade = 0.0, P2ComboFade = 0.0;
    int LastP1Combo = 0, LastP2Combo = 0;

    // ---- Result screen cache (captured at match end, since Battle may be
    // torn down before the Result screen is drawn) ----
    struct ResultData {
        bool winnerIsPlayer = false;
        bool isDraw = false;
        int maxCombo = 0;
        int timeLeftSeconds = 0;
        int damageTaken = 0;
    } LastResult;

    // ---- Settings screen state ----
    int PendingResIndex = 0;

    // ---- Editor state (see Editor.cpp) ----
    bool EditorCreatingNew = false;
    std::string EditorCharId;
    std::string EditorMoveId;
    std::vector<HWND> EditorControls;
    HWND EditCharId = nullptr, EditCharName = nullptr, EditMaxHp = nullptr, EditWalkFwd = nullptr,
         EditWalkBack = nullptr, EditDash = nullptr, EditJumpVel = nullptr, EditGravity = nullptr;
    HWND ComboCharacter = nullptr, ComboMove = nullptr, ComboTemplate = nullptr;
    HWND EditMoveName = nullptr, EditStartup = nullptr, EditActive = nullptr, EditRecovery = nullptr,
         EditDamage = nullptr, EditHitstun = nullptr, EditBlockstun = nullptr, EditHitstop = nullptr;
    HWND LabelAdvantage = nullptr;
    HWND BtnSave = nullptr, BtnNewCharacter = nullptr, BtnBack = nullptr, BtnCreateConfirm = nullptr, BtnCreateCancel = nullptr;
    RECT PreEditorWindowRect{};
    bool EditorSizeSaved = false;

    // ---- Move: button binding, cancel window, special-move command builder ----
    // ComboMoveButton is enabled (and meaningful) only for Normal-tagged
    // moves - LP/MP/HP/LK/MK/HK direct binding. Specials/supers instead
    // build an input command (digit sequence + button) below.
    HWND ComboMoveButton = nullptr;
    HWND EditCancelStart = nullptr, EditCancelEnd = nullptr;
    HWND BtnDigit[9] = {}; // numpad-notation direction buttons, indices for digits 1-9 at BtnDigit[d-1]
    HWND EditCommandPreview = nullptr, BtnCommandClear = nullptr, ComboSpecialButton = nullptr;
    HWND ChkDynamicHitbox = nullptr;
    std::string EditorCommandDigits; // e.g. "236", built up by clicking BtnDigit

    // ---- Motion reference image (see Editor.cpp / App::DrawMotionImagePreview) ----
    // An optional per-move reference image, browsed/attached in the editor
    // and stored on MoveData::MotionImagePath - authoring reference only,
    // not yet drawn in actual gameplay (see MoveData.h's comment on the
    // field). EditMotionImagePath displays the current path (read-only);
    // a small thumbnail is custom-painted below it from
    // EditorMotionImageCache, reloaded only when the path actually changes
    // so the editor isn't re-decoding the file from disk every repaint.
    HWND EditMotionImagePath = nullptr, BtnBrowseMotionImage = nullptr, BtnClearMotionImage = nullptr;
    std::string EditorMotionImagePath;
    std::unique_ptr<Gdiplus::Image> EditorMotionImageCache;
    std::string EditorMotionImageCachedPath;
    Gdiplus::RectF EditorMotionImageRect{40.0f, 756.0f, 320.0f, 136.0f}; // overwritten to match by CreateEditorControls
    void BrowseMotionImage();
    void DrawMotionImagePreview(Gdiplus::Graphics& g);

    // ---- Hitbox/hurtbox editor (see Editor.cpp / App::DrawEditorPreview) ----
    // A move's hitboxes and a character's per-stance hurtbox parts are both
    // edited as an in-memory draft (numeric fields + drag-in-preview),
    // written back to the real MoveData/CharacterStats only on SAVE
    // MOVE/SAVE CHARACTER. Multiple hitboxes/parts are supported - only the
    // currently-selected one (EditorHitboxIndex / EditorHurtPartIndex) is
    // shown in the X/Y/W/H fields and draggable in the preview, but every
    // one is drawn as an outline so the whole set stays visible at once.
    HWND ComboHitboxIndex = nullptr, BtnAddHitbox = nullptr, BtnRemoveHitbox = nullptr;
    HWND EditHitX = nullptr, EditHitY = nullptr, EditHitW = nullptr, EditHitH = nullptr;
    std::vector<HitboxDef> EditorHitboxDraftList;
    int EditorHitboxIndex = 0;

    HWND ComboHurtStance = nullptr, ComboHurtPart = nullptr, BtnAddPart = nullptr, BtnRemovePart = nullptr;
    HWND EditHurtX = nullptr, EditHurtY = nullptr, EditHurtW = nullptr, EditHurtH = nullptr;
    HurtboxSet EditorHurtboxDraft;
    int EditorHurtStance = 0; // 0=stand, 1=crouch, 2=air
    int EditorHurtPartIndex = 0;

    // Which box the preview canvas' mouse drag currently controls.
    HWND BtnDragHitbox = nullptr, BtnDragHurtbox = nullptr;
    bool EditorDragTargetIsHurtbox = false;
    bool EditorDragging = false, EditorDragResizing = false;
    int EditorDragStartMouseX = 0, EditorDragStartMouseY = 0;
    double EditorDragStartCenterX = 0, EditorDragStartCenterY = 0, EditorDragStartW = 0, EditorDragStartH = 0;
    Gdiplus::RectF EditorPreviewRect{800.0f, 116.0f, 520.0f, 520.0f}; // overwritten to match by CreateEditorControls
    // world px -> preview px. Recalibrated (0.6 / 0.282609) for the 384x224
    // pixel-art rearchitecture's world-space shrink (kCharScale 4.6 -> 1.3,
    // see platform/Draw.cpp) so the Editor's preview panel - real window
    // pixels, independent of the low-res pipeline - keeps drawing the
    // character and hitbox/hurtbox boxes at the same on-screen size as
    // before, despite their world-unit values now being ~3.5x smaller.
    double EditorPreviewScale = 2.1231;

    // ---- Language (JP/EN) ----
    // 0 = English, 1 = Japanese. See Editor.cpp's EStr table - every
    // label/button created through MakeLabel/MakeButton(EStr) registers
    // itself so ApplyEditorLanguage() can retext them all in place without
    // recreating any control.
    int EditorLanguage = 0;
    HWND BtnLanguage = nullptr;
    void ApplyEditorLanguage();

    void SyncHitboxFieldsFromDraft();
    void ApplyHitboxFieldsToDraft();
    void SyncHurtboxFieldsFromDraft();
    void ApplyHurtboxFieldsToDraft();
    void ClampBoxToPreview(double& cx, double& cy, double& w, double& h);
    void RebuildHitboxCombo();
    void RebuildHurtPartCombo();
    void UpdateCommandPreview();
    void DrawEditorPreview(Gdiplus::Graphics& g);
    void OnMouseMove(int x, int y);
    void OnLButtonUp(int x, int y);

    void Init(HWND hwnd, const fs::path& baseDataDir, const fs::path& userDir);
    void Shutdown();

    void OnPaint();
    void OnResize(int w, int h);
    void OnLButtonDown(int x, int y);
    void OnKeyDown(int vk);
    void OnKeyUp(int vk);
    void OnTimer();
    void OnCommand(int controlId, int notifyCode, HWND ctrl);
    void OnDrawItem(DRAWITEMSTRUCT* dis);

    ViewTransform CurrentTransform() const;
    void GoTo(Screen s);

    // Screen builders/drawers (Screens.cpp)
    void BuildTitleButtons();
    void DrawTitle(Gdiplus::Graphics& g);

    void BuildSelectButtons();
    void DrawCharacterSelect(Gdiplus::Graphics& g);

    void DrawVS(Gdiplus::Graphics& g);

    void BuildGameButtons();
    void DrawGame(Gdiplus::Graphics& g);
    void StartMatch();

    void BuildResultButtons();
    void DrawResult(Gdiplus::Graphics& g);

    void BuildSettingsButtons();
    void DrawSettings(Gdiplus::Graphics& g);
    void ApplyResolution(int w, int h);

    // Editor (Editor.cpp)
    void EnterEditor();
    void LeaveEditor();
    void CreateEditorControls();
    void DestroyEditorControls();
    void HideEditorControls();
    void LayoutEditorControls();
    void PopulateCharacterCombo();
    void PopulateMoveCombo();
    void PopulateTemplateCombo();
    void LoadCharacterIntoForm(const std::string& charId);
    void LoadMoveIntoForm(const std::string& moveId);
    void ApplyStatsForm();
    void ApplyMoveForm();
    void UpdateAdvantagePreview();
    void ShowCreateCharacterPrompt();
    void HideCreateCharacterPrompt();

    RawInput CollectP1Input() const;
};

extern App* g_App;

} // namespace kakuge

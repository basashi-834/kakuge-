// platform/App.h
// Central application/screen state machine. Every screen - including the
// Character Editor - is drawn entirely with GDI+ and driven by this app's
// own mouse/keyboard handling; no native Win32 child controls anywhere.
// Menu screens hit-test UiButton rects in virtual-canvas space (see
// Layout.h); the Editor uses its own custom-drawn widget set (Editor.cpp)
// at real window-pixel scale, since a data-entry form needs more room
// than the 384x224 game canvas offers.
#pragma once
#include "GdiPlusInclude.h"
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
// pixels) - the form is laid out below it.
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
    // The editor is a custom-drawn form: every field edits an in-memory
    // draft (EditorStatsDraft / EditorMoveDraft / the hitbox & hurtbox
    // drafts below) and only SAVE CHARACTER / SAVE MOVE write back through
    // DataManager. Widgets themselves live in Editor.cpp and bind to these
    // drafts through small getter/setter closures, so there's no per-
    // control state to keep in sync here.
    bool EditorCreatingNew = false;
    std::string EditorCharId;
    std::string EditorMoveId;
    std::vector<std::string> EditorMoveIds; // dropdown order (sorted by id)
    CharacterStats EditorStatsDraft;
    MoveData EditorMoveDraft;
    std::string EditorNewId, EditorNewName; // "+ NEW CHARACTER" form
    int EditorTemplateIndex = 0;
    RECT PreEditorWindowRect{};
    bool EditorSizeSaved = false;

    // Widget interaction state: which text field has keyboard focus (and
    // its live edit buffer + caret), which dropdown is open, hover.
    int EditorFocusId = -1;
    std::wstring EditorEditBuffer;
    int EditorCaret = 0;
    int EditorOpenDropdown = -1;
    int EditorDropdownScroll = 0;
    int EditorHoverId = -1;
    int EditorMouseX = 0, EditorMouseY = 0;
    // Transient status line in the header ("Character saved." etc.),
    // replacing the old modal MessageBox popups.
    std::wstring EditorStatus;
    std::chrono::steady_clock::time_point EditorStatusUntil{};

    std::string EditorCommandDigits; // e.g. "236", built up by the numpad buttons

    // ---- Motion reference image (see Editor.cpp / App::DrawMotionImagePreview) ----
    // An optional per-move reference image, browsed/attached in the editor
    // and stored on MoveData::MotionImagePath - authoring reference only,
    // not yet drawn in actual gameplay (see MoveData.h's comment on the
    // field). A small thumbnail is custom-painted from
    // EditorMotionImageCache, reloaded only when the path actually changes
    // so the editor isn't re-decoding the file from disk every repaint.
    std::string EditorMotionImagePath;
    std::unique_ptr<Gdiplus::Image> EditorMotionImageCache;
    std::string EditorMotionImageCachedPath;
    Gdiplus::RectF EditorMotionImageRect{40.0f, 756.0f, 320.0f, 136.0f};
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
    std::vector<HitboxDef> EditorHitboxDraftList;
    int EditorHitboxIndex = 0;
    HurtboxSet EditorHurtboxDraft;
    int EditorHurtStance = 0; // 0=stand, 1=crouch, 2=air
    int EditorHurtPartIndex = 0;

    // Which box the preview canvas' mouse drag currently controls.
    bool EditorDragTargetIsHurtbox = false;
    bool EditorDragging = false, EditorDragResizing = false;
    int EditorDragStartMouseX = 0, EditorDragStartMouseY = 0;
    double EditorDragStartCenterX = 0, EditorDragStartCenterY = 0, EditorDragStartW = 0, EditorDragStartH = 0;
    Gdiplus::RectF EditorPreviewRect{800.0f, 116.0f, 520.0f, 520.0f};
    // world px -> preview px. Recalibrated (0.6 / 0.282609) for the 384x224
    // pixel-art rearchitecture's world-space shrink (kCharScale 4.6 -> 1.3,
    // see platform/Draw.cpp) so the Editor's preview panel - real window
    // pixels, independent of the low-res pipeline - keeps drawing the
    // character and hitbox/hurtbox boxes at the same on-screen size as
    // before, despite their world-unit values now being ~3.5x smaller.
    double EditorPreviewScale = 2.1231;

    // ---- Language (JP/EN) ----
    // 0 = English, 1 = Japanese. Every widget label is looked up through
    // Editor.cpp's EStr table at draw time, so toggling is just this flag.
    int EditorLanguage = 0;

    void ClampBoxToPreview(double& cx, double& cy, double& w, double& h);
    void DrawEditorPreview(Gdiplus::Graphics& g);
    void OnMouseMove(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseWheel(int delta);
    void OnChar(wchar_t c);

    void Init(HWND hwnd, const fs::path& baseDataDir, const fs::path& userDir);
    void Shutdown();

    void OnPaint();
    void OnResize(int w, int h);
    void OnLButtonDown(int x, int y);
    void OnKeyDown(int vk);
    void OnKeyUp(int vk);
    void OnTimer();

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
    void DrawEditor(Gdiplus::Graphics& g, int w, int h);
    bool EditorMouseDown(int x, int y);   // true if a widget consumed the click
    void EditorMouseMove(int x, int y);
    bool EditorKeyDown(int vk);           // true if consumed
    void EditorChar(wchar_t c);
    void EditorWheel(int delta);
    void EditorSetStatus(const std::wstring& text);
    void EditorBlur();
    void LoadCharacterIntoForm(const std::string& charId);
    void LoadMoveIntoForm(const std::string& moveId);
    void ApplyStatsForm();
    void ApplyMoveForm();

    RawInput CollectP1Input() const;
};

extern App* g_App;

} // namespace kakuge

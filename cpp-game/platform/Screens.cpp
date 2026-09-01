// platform/Screens.cpp
// Draw + button-build logic for every custom-drawn (non-Editor) screen:
// Title, Character Select (new), VS (new), Game (+ pause menu with the new
// P2 dummy-mode toggle), Result, and Settings (new, resolution/aspect).
#include "App.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Gdiplus;

namespace kakuge {

// ---------------------------------------------------------------------
// Small shared UI chrome helpers
// ---------------------------------------------------------------------
static void DrawUiButton(Graphics& g, const UiButton& b) {
    const auto& pal = GetPalette();
    GraphicsPath path;
    AddRoundedRect(path, b.Rect, 0.0f);
    if (b.Primary) {
        SolidBrush fill(b.Enabled ? pal.Accent : pal.Border);
        g.FillPath(&fill, &path);
        if (b.Enabled) DrawGlossCap(g, b.Rect);
        Pen border(pal.Ink, 2.0f);
        g.DrawPath(&border, &path);
    } else {
        SolidBrush fill(pal.PanelBg);
        g.FillPath(&fill, &path);
        Pen border(pal.Border, 2.0f);
        g.DrawPath(&border, &path);
    }
    Font font(UiFontFamily(), 16, FontStyleBold, UnitPixel);
    Color textColor = b.Primary ? pal.White : pal.TextDark;
    DrawTextCentered(g, Utf8ToWide(b.Text), font, b.Rect, textColor);
}

static void DrawButtons(Graphics& g, const std::vector<UiButton>& buttons) {
    for (const auto& b : buttons) DrawUiButton(g, b);
}

static UiButton MakeButton(float x, float y, float w, float h, const std::string& text, bool primary, std::function<void()> onClick) {
    UiButton b;
    b.Rect = RectF(x, y, w, h);
    b.Text = text;
    b.Primary = primary;
    b.OnClick = std::move(onClick);
    return b;
}

// ---------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------
void App::BuildTitleButtons() {
    Buttons.clear();
    float colX = 60, colW = 460;
    Buttons.push_back(MakeButton(colX, 300, colW, 58, "GAME START", true, [this]() { IsTrainingMode = false; GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(colX, 364, colW, 46, "TRAINING MODE", false, [this]() { IsTrainingMode = true; GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(colX, 416, colW, 46, "CHARACTER EDIT", false, [this]() { GoTo(Screen::Editor); }));
    Buttons.push_back(MakeButton(colX, 468, colW, 46, "SETTINGS", false, [this]() { GoTo(Screen::Settings); }));
    Buttons.push_back(MakeButton(colX, 520, colW, 46, "EXIT", false, []() { PostQuitMessage(0); }));
}

// Left text column + right "key visual" panel, an asymmetric composition
// (per the HARD CANDY reference's Screen 01 - left-aligned eyebrow/title/
// tagline stacked over a button group, with a bordered art panel filling
// the right side) rather than the old fully-centered layout.
void App::DrawTitle(Graphics& g) {
    const auto& pal = GetPalette();
    Font eyebrowFont(UiFontFamily(), 13, FontStyleBold, UnitPixel);
    Font titleFont(UiFontFamily(), 72, FontStyleBold, UnitPixel);
    Font taglineFont(UiFontFamily(), 15, FontStyleRegular, UnitPixel);
    Font captionFont(UiFontFamily(), 12, FontStyleRegular, UnitPixel);
    Font footerFont(UiFontFamily(), 11, FontStyleRegular, UnitPixel);

    float colX = 60.0f;

    SolidBrush markBrush(pal.Accent);
    g.FillRectangle(&markBrush, colX, 66.0f, 22.0f, 22.0f);
    DrawTextLeft(g, L"2D FIGHTING GAME", eyebrowFont, colX + 34, 70, pal.Accent);

    DrawTextLeft(g, L"KAKUGE", titleFont, colX - 4, 96, pal.Ink);

    float ruleW = 120;
    SolidBrush ruleBrush(pal.Accent);
    g.FillRectangle(&ruleBrush, colX, 206.0f, ruleW, 4.0f);

    DrawTextLeft(g, L"6-button, frame-data fighting.", taglineFont, colX, 228, pal.Ink70);
    DrawTextLeft(g, L"1 CHARACTER (IN DEVELOPMENT) - VERSUS / TRAINING", captionFont, colX, 254, pal.Ink55);

    DrawButtons(g, Buttons);
    if (!Buttons.empty()) DrawDiagonalShine(g, Buttons[0].Rect); // GAME START - primary CTA gets the shine

    DrawTextLeft(g, L"A/D move  S crouch  Space jump  U I O punch  J K L kick  Esc pause",
                 footerFont, colX, static_cast<REAL>(VirtualH - 34), pal.Ink55);

    // Right "key visual" panel - the provided character render, on a
    // tinted, hard-bordered, hard-shadowed panel.
    RectF artPanel(580, 90, 640, 560);
    DrawHardShadow(g, artPanel);
    GraphicsPath artPath;
    AddRoundedRect(artPath, artPanel, 0.0f);
    SolidBrush artBg(pal.TintRed);
    g.FillPath(&artBg, &artPath);
    Pen artBorder(pal.Ink, 2.0f);
    g.DrawPath(&artBorder, &artPath);
    if (!DrawSpriteFit(g, L"fighter_stand.png", artPanel, 0.05f)) {
        Font phFont(UiFontFamily(), 13, FontStyleRegular, UnitPixel);
        DrawTextCentered(g, L"KEY VISUAL", phFont, artPanel, pal.Ink45);
    }

    Font copyFont(UiFontFamily(), 11, FontStyleRegular, UnitPixel);
    DrawTextLeft(g, L"© 2026 KAKUGE PROJECT - PROTOTYPE BUILD", copyFont, colX, static_cast<REAL>(VirtualH - 16), pal.Ink45);
}

// ---------------------------------------------------------------------
// Character Select (new)
// ---------------------------------------------------------------------
struct StatBars { int power, stamina, speed, reach; };

static StatBars ComputeStatBars(DataManager& dm, const std::string& charId) {
    const auto* stats = dm.GetCharacter(charId);
    StatBars sb{50, 50, 50, 50};
    if (!stats) return sb;
    sb.stamina = std::clamp(static_cast<int>(stats->MaxHP / 12.0), 0, 100);
    sb.speed = std::clamp(static_cast<int>(stats->WalkForwardSpeed / 3.0), 0, 100);
    const auto* moves = dm.GetMoveset(charId);
    if (moves && !moves->empty()) {
        double totalDmg = 0, totalRange = 0;
        int n = 0;
        for (const auto& kv : *moves) {
            if (kv.second.HasTag("Normal")) {
                totalDmg += kv.second.Damage;
                totalRange += kv.second.EffectiveRange;
                n++;
            }
        }
        if (n > 0) {
            sb.power = std::clamp(static_cast<int>((totalDmg / n) * 1.4), 0, 100);
            sb.reach = std::clamp(static_cast<int>((totalRange / n) * 0.9), 0, 100);
        }
    }
    return sb;
}

static void DrawMiniStatBar(Graphics& g, float x, float y, float w, const std::string& label, int value) {
    const auto& pal = GetPalette();
    Font f(UiFontFamily(), 10, FontStyleBold, UnitPixel);
    DrawTextLeft(g, Utf8ToWide(label), f, x, y, pal.TextGray);
    DrawBar(g, x, y + 14, w, 8, value / 100.0, pal.Accent, pal.HpEmpty, false);
}

// Compact tile grid on the left + one big detail panel on the right (per
// the reference's Screen 02: a wall of small "?" tiles feeding a single
// large portrait/stats panel), replacing the old layout where every tile
// carried its own full stat block.
static constexpr float kSelectTileW = 132, kSelectTileH = 150, kSelectGap = 14;
static constexpr float kSelectGridX = 40, kSelectGridY = 108;
static constexpr int kSelectCols = 4;

void App::BuildSelectButtons() {
    Buttons.clear();
    auto ids = Dm->GetCharacterIds();
    for (size_t i = 0; i < ids.size(); i++) {
        int col = static_cast<int>(i) % kSelectCols, row = static_cast<int>(i) / kSelectCols;
        float x = kSelectGridX + col * (kSelectTileW + kSelectGap);
        float y = kSelectGridY + row * (kSelectTileH + kSelectGap);
        std::string id = ids[i];
        Buttons.push_back(MakeButton(x, y, kSelectTileW, kSelectTileH, "", true, [this, id]() {
            if (SelectStep == 0) P1CharId = id; else P2CharId = id;
        }));
    }
    // "+ ADD CHARACTER" tile -> jump into the editor's creation flow.
    int col = static_cast<int>(ids.size()) % kSelectCols, row = static_cast<int>(ids.size()) / kSelectCols;
    float x = kSelectGridX + col * (kSelectTileW + kSelectGap);
    float y = kSelectGridY + row * (kSelectTileH + kSelectGap);
    Buttons.push_back(MakeButton(x, y, kSelectTileW, kSelectTileH, "+\nADD", false, [this]() {
        EditorCreatingNew = true;
        GoTo(Screen::Editor);
    }));

    Buttons.push_back(MakeButton(60, 648, 200, 52, "BACK", false, [this]() { GoTo(Screen::Title); }));
    Buttons.push_back(MakeButton(VirtualW - 260.0f, 648, 200, 52, "CONFIRM", true, [this]() {
        if (SelectStep == 0) {
            SelectStep = 1;
            BuildSelectButtons();
        } else if (IsTrainingMode) {
            GoTo(Screen::Game); // skip the VS intro for a quicker practice setup
        } else {
            GoTo(Screen::VS);
        }
    }));
}

void App::DrawCharacterSelect(Graphics& g) {
    const auto& pal = GetPalette();
    Font headerFont(UiFontFamily(), 26, FontStyleBold, UnitPixel);
    Font plusFont(UiFontFamily(), 13, FontStyleBold, UnitPixel);

    std::wstring header = SelectStep == 0 ? L"SELECT YOUR FIGHTER" : L"SELECT CPU OPPONENT";
    SolidBrush headerBg(pal.Accent);
    g.FillRectangle(&headerBg, 0.0f, 0.0f, static_cast<REAL>(VirtualW), 90.0f);
    DrawTextCentered(g, header, headerFont, RectF(0, 0, static_cast<REAL>(VirtualW), 90), pal.White);

    auto ids = Dm->GetCharacterIds();
    const std::string& selectedId = (SelectStep == 0) ? P1CharId : P2CharId;

    for (size_t i = 0; i < Buttons.size() && i < ids.size() + 1; i++) {
        const auto& b = Buttons[i];
        bool isAddTile = (i == ids.size());
        GraphicsPath path;
        AddRoundedRect(path, b.Rect, 0.0f);
        bool selected = !isAddTile && ids[i] == selectedId;
        SolidBrush fill(selected ? pal.PanelBg2 : pal.PanelBg);
        g.FillPath(&fill, &path);
        Pen border(selected ? pal.Accent : pal.Border, selected ? 3.0f : 2.0f);
        g.DrawPath(&border, &path);

        if (isAddTile) {
            DrawTextCentered(g, L"+\nADD", plusFont, b.Rect, pal.Ink55);
            continue;
        }
        const std::string& id = ids[i];
        double px = b.Rect.X + b.Rect.Width / 2.0;
        double py = b.Rect.Y + 90;
        auto* stats = Dm->GetCharacter(id);
        Color bodyColor = stats ? Color(255, static_cast<BYTE>(stats->ColorR), static_cast<BYTE>(stats->ColorG), static_cast<BYTE>(stats->ColorB)) : pal.TextDark;
        DrawHumanoid(g, px, py, bodyColor, {0.16, 1});
        Font tileNameFont(UiFontFamily(), 12, FontStyleBold, UnitPixel);
        DrawTextCentered(g, Utf8ToWide(stats ? stats->Name : id), tileNameFont, RectF(b.Rect.X, b.Rect.Y + b.Rect.Height - 22, b.Rect.Width, 20), pal.TextDark);
    }

    // Right-side detail panel: big portrait on top, name + full stat block
    // below - the single large panel the reference feeds every tile into,
    // instead of repeating stats inside each small tile.
    RectF detailPanel(700, 108, 520, 512);
    DrawHardShadow(g, detailPanel);
    GraphicsPath dp;
    AddRoundedRect(dp, detailPanel, 0.0f);
    SolidBrush detailBg(pal.PanelBg);
    g.FillPath(&detailBg, &dp);
    Pen detailBorder(pal.Ink, 2.0f);
    g.DrawPath(&detailBorder, &dp);

    RectF portraitRect(detailPanel.X, detailPanel.Y, detailPanel.Width, 300);
    SolidBrush portraitBg(pal.TintRed);
    g.FillRectangle(&portraitBg, portraitRect);
    auto* selStats = Dm->GetCharacter(selectedId);
    if (!DrawSpriteFit(g, L"fighter_stand.png", portraitRect, 0.04f)) {
        Color bodyColor = selStats ? Color(255, static_cast<BYTE>(selStats->ColorR), static_cast<BYTE>(selStats->ColorG), static_cast<BYTE>(selStats->ColorB)) : pal.TextDark;
        DrawHumanoid(g, portraitRect.X + portraitRect.Width / 2.0, portraitRect.Y + portraitRect.Height - 20, bodyColor, {0.55, 1});
    }
    Pen portraitDivider(pal.Ink, 2.0f);
    g.DrawLine(&portraitDivider, detailPanel.X, detailPanel.Y + 300, detailPanel.X + detailPanel.Width, detailPanel.Y + 300);

    Font detailNameFont(UiFontFamily(), 26, FontStyleBold, UnitPixel);
    DrawTextLeft(g, Utf8ToWide(selStats ? selStats->Name : selectedId), detailNameFont, detailPanel.X + 24, detailPanel.Y + 316, pal.Ink);

    StatBars sb = ComputeStatBars(*Dm, selectedId);
    float statX = detailPanel.X + 24, statW = detailPanel.Width - 48, statY = detailPanel.Y + 366;
    DrawMiniStatBar(g, statX, statY, statW, "POWER", sb.power);
    DrawMiniStatBar(g, statX, statY + 40, statW, "STAMINA", sb.stamina);
    DrawMiniStatBar(g, statX, statY + 80, statW, "SPEED", sb.speed);
    DrawMiniStatBar(g, statX, statY + 120, statW, "REACH", sb.reach);

    // BACK/CONFIRM are the last two entries in Buttons (DrawUiButton already
    // applies the gloss cap itself for the primary CONFIRM button).
    for (size_t i = ids.size() + 1; i < Buttons.size(); i++) DrawUiButton(g, Buttons[i]);
}

// ---------------------------------------------------------------------
// VS screen (new)
// ---------------------------------------------------------------------
void App::DrawVS(Graphics& g) {
    const auto& pal = GetPalette();
    Font nameFont(UiFontFamily(), 30, FontStyleBold, UnitPixel);
    Font hintFont(UiFontFamily(), 12, FontStyleRegular, UnitPixel);

    double t = std::min(1.0, VsTimer / 0.5);
    float half = VirtualW / 2.0f;
    float leftW = static_cast<float>(half * t);
    float rightW = static_cast<float>(half * t);

    SolidBrush leftBrush(pal.Accent);
    RectF leftPanel(half - leftW, 0.0f, leftW, static_cast<REAL>(VirtualH));
    g.FillRectangle(&leftBrush, leftPanel);
    if (leftW > 4.0f) DrawDiagonalShine(g, leftPanel); // large red block - candy shine per spec
    SolidBrush rightBrush(Color(255, 60, 58, 58));
    g.FillRectangle(&rightBrush, half, 0.0f, rightW, static_cast<REAL>(VirtualH));

    auto* p1 = Dm->GetCharacter(P1CharId);
    auto* p2 = Dm->GetCharacter(P2CharId);
    if (t > 0.3) {
        DrawTextCentered(g, Utf8ToWide(p1 ? p1->Name : P1CharId), nameFont, RectF(0, VirtualH / 2.0f - 60, half, 60), pal.White);
        DrawTextCentered(g, Utf8ToWide(p2 ? p2->Name : P2CharId), nameFont, RectF(half, VirtualH / 2.0f - 60, half, 60), pal.White);
    }

    if (VsTimer > 0.5) {
        double diamondT = std::min(1.0, (VsTimer - 0.5) / 0.3);
        float size = static_cast<float>(90 * diamondT);
        PointF pts[4] = {
            PointF(VirtualW / 2.0f, VirtualH / 2.0f - size),
            PointF(VirtualW / 2.0f + size, VirtualH / 2.0f),
            PointF(VirtualW / 2.0f, VirtualH / 2.0f + size),
            PointF(VirtualW / 2.0f - size, VirtualH / 2.0f),
        };
        SolidBrush diaBrush(pal.White);
        g.FillPolygon(&diaBrush, pts, 4);
        Font vsSmall(UiFontFamily(), static_cast<REAL>(std::max(10.0, 32 * diamondT)), FontStyleBold, UnitPixel);
        DrawTextCentered(g, L"VS", vsSmall, RectF(VirtualW / 2.0f - size, VirtualH / 2.0f - size, size * 2, size * 2), pal.Accent);
    }

    DrawTextCentered(g, L"press any key to continue", hintFont, RectF(0, static_cast<REAL>(VirtualH - 40), static_cast<REAL>(VirtualW), 24), pal.White);
}

// ---------------------------------------------------------------------
// Game (+ pause menu with the new P2 dummy-mode toggle)
// ---------------------------------------------------------------------
void App::StartMatch() {
    extern int g_RoundTimeSeconds;
    Battle = std::make_unique<BattleSystem>();
    auto* p1s = Dm->GetCharacter(P1CharId);
    auto* p2s = Dm->GetCharacter(P2CharId);
    if (!p1s) p1s = Dm->GetCharacter(Dm->GetCharacterIds().front());
    if (!p2s) p2s = Dm->GetCharacter(Dm->GetCharacterIds().front());
    Battle->StartMatch(*p1s, Dm->GetMoveset(P1CharId), *p2s, Dm->GetMoveset(P2CharId), g_RoundTimeSeconds);
    Battle->TrainingMode = IsTrainingMode;
    Battle->TrainingAutoHeal = TrainingAutoHealPref;
    // The dummy-mode toggle only lives in Training mode's pause menu now,
    // so a Versus match always fights a real CPU regardless of whatever
    // was left selected from a previous Training session.
    if (!IsTrainingMode) P2DummyMode = DummyMode::CPU;
    Battle->CpuAI->Mode = P2DummyMode;
    HeldKeys.clear();
    Paused = false;
    MatchFinished = false;
    Effects.clear();
    P1ComboFade = P2ComboFade = 0.0;
    LastTick = std::chrono::steady_clock::now();
    Accumulator = 0.0;
}

void App::BuildGameButtons() {
    Buttons.clear();
    if (!Paused) return;
    float cx = VirtualW / 2.0f;
    float panelTop = VirtualH / 2.0f - 190;

    if (!IsTrainingMode) {
        // Versus mode: a real CPU opponent, nothing to configure - keep
        // the pause menu simple.
        Buttons.push_back(MakeButton(cx - 150, panelTop + 70, 300, 50, "RESUME (Esc)", true, [this]() { Paused = false; BuildGameButtons(); }));
        Buttons.push_back(MakeButton(cx - 150, panelTop + 140, 300, 50, "GIVE UP -> TITLE", false, [this]() { GoTo(Screen::Title); }));
        return;
    }

    Buttons.push_back(MakeButton(cx - 150, panelTop + 70, 300, 50, "RESUME (Esc)", true, [this]() { Paused = false; BuildGameButtons(); }));

    auto modeButton = [this, cx, panelTop](float x, const char* label, DummyMode mode) {
        bool active = (P2DummyMode == mode);
        return MakeButton(x, panelTop + 140, 140, 40, label, active, [this, mode]() {
            P2DummyMode = mode;
            if (Battle) Battle->CpuAI->Mode = mode;
            BuildGameButtons();
        });
    };
    Buttons.push_back(modeButton(cx - 300, "P2: CPU", DummyMode::CPU));
    Buttons.push_back(modeButton(cx - 150, "P2: STAND", DummyMode::Stand));
    Buttons.push_back(modeButton(cx, "P2: CROUCH", DummyMode::Crouch));
    Buttons.push_back(modeButton(cx + 150, "P2: JUMP", DummyMode::Jump));

    Buttons.push_back(MakeButton(cx - 300, panelTop + 200, 220, 44,
        TrainingAutoHealPref ? "AUTO HEAL: ON" : "AUTO HEAL: OFF", TrainingAutoHealPref, [this]() {
            TrainingAutoHealPref = !TrainingAutoHealPref;
            if (Battle) Battle->TrainingAutoHeal = TrainingAutoHealPref;
            BuildGameButtons();
        }));
    Buttons.push_back(MakeButton(cx - 60, panelTop + 200, 220, 44, "RESET HP", false, [this]() {
        if (Battle) Battle->ResetHP();
    }));

    Buttons.push_back(MakeButton(cx - 150, panelTop + 260, 300, 50, "GIVE UP -> TITLE", false, [this]() { GoTo(Screen::Title); }));
}

void App::DrawGame(Graphics& g) {
    const auto& pal = GetPalette();

    // Counter-hit screen shake (see BattleSystem::ShakeFrames/ShakeMagnitude,
    // set from Fighter::CounterKind in ResolveCombat): a small camera jolt
    // on Counter, a bigger one on Effective Counter, decaying to nothing as
    // ShakeFrames counts down. Applied to the whole scene (arena + HUD),
    // restored before the pause overlay so the pause panel itself never
    // shakes.
    GraphicsState shakeState = g.Save();
    if (Battle->ShakeFrames > 0) {
        double t = std::min(1.0, Battle->ShakeFrames / 18.0);
        double amp = Battle->ShakeMagnitude * t;
        double ox = (static_cast<double>(std::rand()) / RAND_MAX * 2.0 - 1.0) * amp;
        double oy = (static_cast<double>(std::rand()) / RAND_MAX * 2.0 - 1.0) * amp * 0.6;
        g.TranslateTransform(static_cast<REAL>(ox), static_cast<REAL>(oy));
    }

    // In-match arena runs on a dark ground (Screen 03 in the reference),
    // unlike every other (light) screen - overpainted here, oversized by a
    // margin so the shake transform above never reveals the light canvas
    // fill (pal.Bg) App::OnPaint already laid down behind this screen.
    SolidBrush arenaBg(pal.ArenaBg);
    g.FillRectangle(&arenaBg, -40.0f, -40.0f, static_cast<REAL>(VirtualW + 80), static_cast<REAL>(VirtualH + 80));

    // Thin floor line rather than a tall band: the ground line moved down
    // (OriginY=647, see Draw.h) to hit the user's footroom spec, leaving
    // little clearance before the GAUGE HUD row starts at y=665.
    SolidBrush groundBrush(pal.ArenaPanel);
    g.FillRectangle(&groundBrush, 0.0f, static_cast<REAL>(ToScreenY(0)), static_cast<REAL>(VirtualW), 12.0f);

    DrawFighter(g, Battle->Player1);
    DrawFighter(g, Battle->Player2);
    for (const auto& proj : Battle->Projectiles) DrawProjectile(g, proj);
    for (const auto& fx : Effects) DrawEffect(g, fx);

    DrawHUD(g, *Battle, LastP1Combo, LastP2Combo, std::max(P1ComboFade, P2ComboFade));
    if (DebugVisible && IsTrainingMode) DrawDebugOverlay(g, *Battle);

    g.Restore(shakeState);

    if (Paused) {
        SolidBrush dim(Color(140, 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, static_cast<REAL>(VirtualW), static_cast<REAL>(VirtualH));

        float cx = VirtualW / 2.0f;
        float panelTop = VirtualH / 2.0f - 190;
        float panelHeight = IsTrainingMode ? 350.0f : 220.0f;
        RectF panelRect(cx - 340, panelTop, 680, panelHeight);
        DrawHardShadow(g, panelRect);
        GraphicsPath path;
        AddRoundedRect(path, panelRect, 0.0f);
        SolidBrush panelBg(pal.PanelBg);
        g.FillPath(&panelBg, &path);
        Pen border(pal.Ink, 2.0f);
        g.DrawPath(&border, &path);

        Font titleFont(UiFontFamily(), 24, FontStyleBold, UnitPixel);
        DrawTextCentered(g, IsTrainingMode ? L"TRAINING - PAUSED" : L"PAUSED", titleFont, RectF(cx - 340, panelTop + 14, 680, 40), pal.TextDark);
        if (IsTrainingMode) {
            Font hintFont(UiFontFamily(), 11, FontStyleRegular, UnitPixel);
            DrawTextCentered(g, L"P2 control mode (practice)", hintFont, RectF(cx - 340, panelTop + 118, 680, 18), pal.TextGray);
        }

        DrawButtons(g, Buttons);
    }
}

// ---------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------
void App::BuildResultButtons() {
    Buttons.clear();
    float colX = 60, colW = 520;
    Buttons.push_back(MakeButton(colX, 470, colW, 58, "REMATCH", true, [this]() { GoTo(Screen::Game); }));
    Buttons.push_back(MakeButton(colX, 536, colW, 46, "CHARACTER SELECT", false, [this]() { GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(colX, 588, colW, 46, "TITLE", false, [this]() { GoTo(Screen::Title); }));
}

// Left column (banner/quote/stat tiles/buttons) + right "WIN POSE" panel,
// per the reference's Screen 05 - replacing the old fully-centered layout.
void App::DrawResult(Graphics& g) {
    const auto& pal = GetPalette();
    std::wstring resultText = L"DRAW";
    std::wstring quote = L"A close match.";
    if (!LastResult.isDraw) {
        if (LastResult.winnerIsPlayer) { resultText = L"YOU WIN"; quote = L"Victory earned through discipline."; }
        else { resultText = L"CPU WIN"; quote = L"Train harder. Try again."; }
    }

    float colX = 60;
    RectF banner(colX, 60, 360, 90);
    DrawHardShadow(g, banner);
    GraphicsPath path;
    AddRoundedRect(path, banner, 0.0f);
    SolidBrush bannerBrush(pal.Accent);
    g.FillPath(&bannerBrush, &path);
    DrawGlossCap(g, banner);
    DrawDiagonalShine(g, banner);
    Pen bannerBorder(pal.Ink, 2.0f);
    g.DrawPath(&bannerBorder, &path);
    Font bannerFont(UiFontFamily(), 34, FontStyleBold, UnitPixel);
    DrawTextCentered(g, resultText, bannerFont, banner, pal.White);

    Font quoteFont(UiFontFamily(), 13, FontStyleItalic, UnitPixel);
    DrawTextLeft(g, quote, quoteFont, colX, 172, pal.Ink70);

    struct Tile { std::wstring label; std::wstring value; };
    std::vector<Tile> tiles = {
        {L"MAX COMBO", std::to_wstring(LastResult.maxCombo) + L" HITS"},
        {L"TIME LEFT", std::to_wstring(LastResult.timeLeftSeconds) + L"s"},
        {L"DAMAGE TAKEN", std::to_wstring(LastResult.damageTaken)},
    };
    float tileW = 165, tileH = 84, gap = 20;
    Font valueFont(UiFontFamily(), 20, FontStyleBold, UnitPixel);
    Font labelFont(UiFontFamily(), 10, FontStyleBold, UnitPixel);
    for (size_t i = 0; i < tiles.size(); i++) {
        RectF tr(colX + i * (tileW + gap), 208, tileW, tileH);
        GraphicsPath tp;
        AddRoundedRect(tp, tr, 0.0f);
        SolidBrush tileBg(pal.PanelBg);
        g.FillPath(&tileBg, &tp);
        Pen tileBorder(pal.Border, 2.0f);
        g.DrawPath(&tileBorder, &tp);
        DrawTextCentered(g, tiles[i].value, valueFont, RectF(tr.X, tr.Y + 10, tr.Width, 34), pal.Accent);
        DrawTextCentered(g, tiles[i].label, labelFont, RectF(tr.X, tr.Y + 50, tr.Width, 20), pal.TextGray);
    }

    DrawButtons(g, Buttons);

    // Right "WIN POSE" panel - the winner's render, on a tinted, bordered
    // panel with a name/subtitle strip below it.
    RectF winPanel(660, 60, 560, 574);
    DrawHardShadow(g, winPanel);
    GraphicsPath wp;
    AddRoundedRect(wp, winPanel, 0.0f);
    SolidBrush winBg(pal.PanelBg);
    g.FillPath(&winBg, &wp);
    Pen winBorder(pal.Ink, 2.0f);
    g.DrawPath(&winBorder, &wp);

    RectF poseRect(winPanel.X, winPanel.Y, winPanel.Width, winPanel.Height - 70);
    SolidBrush poseBg(pal.TintRed);
    g.FillRectangle(&poseBg, poseRect);
    bool winnerIsP1 = LastResult.isDraw || LastResult.winnerIsPlayer;
    const wchar_t* poseSprite = winnerIsP1 ? L"fighter_punch.png" : L"fighter_kick.png";
    if (!DrawSpriteFit(g, poseSprite, poseRect, 0.05f)) {
        Font phFont(UiFontFamily(), 13, FontStyleRegular, UnitPixel);
        DrawTextCentered(g, L"WIN POSE", phFont, poseRect, pal.Ink45);
    }
    Pen poseDivider(pal.Ink, 2.0f);
    g.DrawLine(&poseDivider, winPanel.X, winPanel.Y + poseRect.Height, winPanel.X + winPanel.Width, winPanel.Y + poseRect.Height);

    const std::string& winnerId = winnerIsP1 ? P1CharId : P2CharId;
    auto* winnerStats = Dm->GetCharacter(winnerId);
    Font winNameFont(UiFontFamily(), 22, FontStyleBold, UnitPixel);
    DrawTextLeft(g, Utf8ToWide(winnerStats ? winnerStats->Name : winnerId), winNameFont, winPanel.X + 24, winPanel.Y + poseRect.Height + 14, pal.Ink);
    Font winSubFont(UiFontFamily(), 12, FontStyleRegular, UnitPixel);
    DrawTextLeft(g, LastResult.isDraw ? L"DRAW" : (winnerIsP1 ? L"PLAYER 1" : L"CPU"), winSubFont, winPanel.X + 24, winPanel.Y + poseRect.Height + 42, pal.Ink55);
}

// ---------------------------------------------------------------------
// Settings (new): resolution / aspect ratio
// ---------------------------------------------------------------------
void App::ApplyResolution(int w, int h) {
    RECT rc{0, 0, w, h};
    DWORD style = static_cast<DWORD>(GetWindowLongPtr(Hwnd, GWL_STYLE));
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(Hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    AppSettings.Width = w;
    AppSettings.Height = h;
    Dm->SaveSettings(AppSettings);
}

void App::BuildSettingsButtons() {
    Buttons.clear();
    const auto& presets = ResolutionPresets();
    float tileW = 220, tileH = 70, gap = 16;
    int cols = 3;
    float startX = (VirtualW - (tileW * cols + gap * (cols - 1))) / 2.0f;
    float startY = 170;
    for (size_t i = 0; i < presets.size(); i++) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float x = startX + col * (tileW + gap);
        float y = startY + row * (tileH + gap);
        Buttons.push_back(MakeButton(x, y, tileW, tileH, "", static_cast<int>(i) == PendingResIndex, [this, i]() { PendingResIndex = static_cast<int>(i); BuildSettingsButtons(); }));
    }
    float cx = VirtualW / 2.0f;
    Buttons.push_back(MakeButton(cx - 260, 600, 240, 56, "BACK", false, [this]() { GoTo(Screen::Title); }));
    Buttons.push_back(MakeButton(cx + 20, 600, 240, 56, "APPLY", true, [this]() {
        const auto& p = ResolutionPresets()[PendingResIndex];
        ApplyResolution(p.width, p.height);
        GoTo(Screen::Title);
    }));
}

void App::DrawSettings(Graphics& g) {
    const auto& pal = GetPalette();
    Font headerFont(UiFontFamily(), 26, FontStyleBold, UnitPixel);
    Font labelFont(UiFontFamily(), 15, FontStyleBold, UnitPixel);
    Font subFont(UiFontFamily(), 11, FontStyleRegular, UnitPixel);

    DrawTextCentered(g, L"SETTINGS", headerFont, RectF(0, 60, static_cast<REAL>(VirtualW), 40), pal.TextDark);
    DrawTextCentered(g, L"DISPLAY RESOLUTION (320x200 - 1920x1080, 4:3 or 16:9)", subFont, RectF(0, 108, static_cast<REAL>(VirtualW), 24), pal.TextGray);

    const auto& presets = ResolutionPresets();
    for (size_t i = 0; i < Buttons.size() - 2 && i < presets.size(); i++) {
        const auto& b = Buttons[i];
        bool selected = static_cast<int>(i) == PendingResIndex;
        GraphicsPath path;
        AddRoundedRect(path, b.Rect, 0.0f);
        SolidBrush fill(selected ? pal.Accent : pal.PanelBg);
        g.FillPath(&fill, &path);
        Pen border(selected ? pal.Accent : pal.Border, 2.0f);
        g.DrawPath(&border, &path);
        Color textColor = selected ? pal.White : pal.TextDark;
        DrawTextCentered(g, Utf8ToWide(presets[i].label), labelFont, RectF(b.Rect.X, b.Rect.Y + 8, b.Rect.Width, 26), textColor);
        DrawTextCentered(g, Utf8ToWide(presets[i].aspect), subFont, RectF(b.Rect.X, b.Rect.Y + 38, b.Rect.Width, 20), selected ? pal.White : pal.TextGray);
    }
    DrawUiButton(g, Buttons[Buttons.size() - 2]);
    DrawUiButton(g, Buttons[Buttons.size() - 1]);
}

} // namespace kakuge

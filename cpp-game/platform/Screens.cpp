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
        Pen border(pal.Ink, 1.0f);
        g.DrawPath(&border, &path);
    } else {
        SolidBrush fill(pal.PanelBg);
        g.FillPath(&fill, &path);
        Pen border(pal.Border, 1.0f);
        g.DrawPath(&border, &path);
    }
    Color textColor = b.Primary ? pal.White : pal.TextDark;
    DrawPixelTextCentered(g, Utf8ToWide(b.Text), b.Rect, 1.0f, textColor);
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
    float colX = 92, colW = 200;
    Buttons.push_back(MakeButton(colX, 78, colW, 18, "GAME START", true, [this]() { IsTrainingMode = false; GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(colX, 98, colW, 14, "TRAINING MODE", false, [this]() { IsTrainingMode = true; GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(colX, 114, colW, 14, "CHARACTER EDIT", false, [this]() { GoTo(Screen::Editor); }));
    Buttons.push_back(MakeButton(colX, 130, colW, 14, "SETTINGS", false, [this]() { GoTo(Screen::Settings); }));
    Buttons.push_back(MakeButton(colX, 146, colW, 14, "EXIT", false, []() { PostQuitMessage(0); }));
}

// Centered composition (the 384x224 canvas is too narrow for the old
// left-text/right-art two-column layout): a small character silhouette
// flanks each side of the wordmark, purely as pixel-art decoration - the
// photo-sprite "key visual" panel from earlier rounds is gone per the
// user's explicit request to express everything as pixel art.
void App::DrawTitle(Graphics& g) {
    const auto& pal = GetPalette();

    SolidBrush topRule(pal.Accent);
    g.FillRectangle(&topRule, 0.0f, 0.0f, static_cast<REAL>(VirtualW), 3.0f);

    DrawPixelTextCentered(g, L"2D FIGHTING GAME", RectF(0, 8, static_cast<REAL>(VirtualW), 8), 1.0f, pal.Accent);
    DrawPixelTextCentered(g, L"KAKUGE", RectF(0, 14, static_cast<REAL>(VirtualW), 36), 4.0f, pal.Ink);

    float ruleW = 40;
    SolidBrush ruleBrush(pal.Accent);
    g.FillRectangle(&ruleBrush, (VirtualW - ruleW) / 2.0f, 52.0f, ruleW, 2.0f);
    DrawPixelTextCentered(g, L"6-BUTTON FIGHTING GAME", RectF(0, 56, static_cast<REAL>(VirtualW), 9), 1.0f, pal.Ink70);

    DrawButtons(g, Buttons);
    if (!Buttons.empty()) DrawDiagonalShine(g, Buttons[0].Rect); // GAME START - primary CTA gets the shine

    DrawPixelTextCentered(g, L"A/D MOVE  S CROUCH  SPACE JUMP", RectF(0, 194, static_cast<REAL>(VirtualW), 8), 1.0f, pal.Ink55);
    DrawPixelTextCentered(g, L"U I O PUNCH  J K L KICK  ESC PAUSE", RectF(0, 203, static_cast<REAL>(VirtualW), 8), 1.0f, pal.Ink55);
    DrawPixelTextCentered(g, L"(C) 2026 KAKUGE PROJECT - PROTOTYPE BUILD", RectF(0, 216, static_cast<REAL>(VirtualW), 7), 1.0f, pal.Ink45);
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
    // WalkForwardSpeed is a world-space (spatial) value, rescaled by
    // ~0.282609 alongside every other world-unit constant for the 384x224
    // pixel-art canvas (see engine/Fighter.h) - this divisor is rescaled by
    // the same factor (3.0 * 0.282609) so the stat bar still reads the same
    // relative fill it did before that rescale.
    sb.speed = std::clamp(static_cast<int>(stats->WalkForwardSpeed / 0.848), 0, 100);
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
            // EffectiveRange is likewise a rescaled world-space value - the
            // multiplier is rescaled by the inverse factor (0.9 / 0.282609)
            // for the same reason as sb.speed above.
            sb.reach = std::clamp(static_cast<int>((totalRange / n) * 3.185), 0, 100);
        }
    }
    return sb;
}

static void DrawMiniStatBar(Graphics& g, float x, float y, float w, const std::string& label, int value) {
    const auto& pal = GetPalette();
    DrawPixelText(g, Utf8ToWide(label), x, y, 1.0f, pal.TextGray);
    DrawBar(g, x, y + 8, w, 4, value / 100.0, pal.Accent, pal.HpEmpty, false);
}

// Compact tile grid on the left + one detail panel on the right - the old
// 4-column/132px-tile layout doesn't fit the 384px canvas, so tiles shrink
// to small icon buttons and the detail panel carries the actual portrait
// and stats.
static constexpr float kSelectTileW = 40, kSelectTileH = 40, kSelectGap = 4;
static constexpr float kSelectGridX = 4, kSelectGridY = 18;
static constexpr int kSelectCols = 3;

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
    Buttons.push_back(MakeButton(x, y, kSelectTileW, kSelectTileH, "+", false, [this]() {
        EditorCreatingNew = true;
        GoTo(Screen::Editor);
    }));

    Buttons.push_back(MakeButton(4, 202, 90, 18, "BACK", false, [this]() { GoTo(Screen::Title); }));
    Buttons.push_back(MakeButton(VirtualW - 94.0f, 202, 90, 18, "CONFIRM", true, [this]() {
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

    std::wstring header = SelectStep == 0 ? L"SELECT YOUR FIGHTER" : L"SELECT CPU OPPONENT";
    SolidBrush headerBg(pal.Accent);
    g.FillRectangle(&headerBg, 0.0f, 0.0f, static_cast<REAL>(VirtualW), 14.0f);
    DrawPixelTextCentered(g, header, RectF(0, 0, static_cast<REAL>(VirtualW), 14), 1.0f, pal.White);

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
        Pen border(selected ? pal.Accent : pal.Border, selected ? 2.0f : 1.0f);
        g.DrawPath(&border, &path);

        if (isAddTile) {
            DrawPixelTextCentered(g, L"+", b.Rect, 2.0f, pal.Ink55);
            continue;
        }
        const std::string& id = ids[i];
        double px = b.Rect.X + b.Rect.Width / 2.0;
        double py = b.Rect.Y + b.Rect.Height - 4.0;
        auto* stats = Dm->GetCharacter(id);
        Color bodyColor = stats ? Color(255, static_cast<BYTE>(stats->ColorR), static_cast<BYTE>(stats->ColorG), static_cast<BYTE>(stats->ColorB)) : pal.TextDark;
        DrawHumanoid(g, px, py, bodyColor, {0.24, 1});
    }

    // Right-side detail panel: portrait on top, name + stat block below.
    RectF detailPanel(152, 18, 228, 180);
    GraphicsPath dp;
    AddRoundedRect(dp, detailPanel, 0.0f);
    SolidBrush detailBg(pal.PanelBg);
    g.FillPath(&detailBg, &dp);
    Pen detailBorder(pal.Ink, 1.5f);
    g.DrawPath(&detailBorder, &dp);

    RectF portraitRect(detailPanel.X, detailPanel.Y, detailPanel.Width, 96);
    SolidBrush portraitBg(pal.TintRed);
    g.FillRectangle(&portraitBg, portraitRect);
    auto* selStats = Dm->GetCharacter(selectedId);
    Color bodyColor = selStats ? Color(255, static_cast<BYTE>(selStats->ColorR), static_cast<BYTE>(selStats->ColorG), static_cast<BYTE>(selStats->ColorB)) : pal.TextDark;
    DrawHumanoid(g, portraitRect.X + portraitRect.Width / 2.0, portraitRect.Y + portraitRect.Height - 8, bodyColor, {0.62, 1});
    Pen portraitDivider(pal.Ink, 1.5f);
    g.DrawLine(&portraitDivider, detailPanel.X, detailPanel.Y + 96, detailPanel.X + detailPanel.Width, detailPanel.Y + 96);

    DrawPixelText(g, Utf8ToWide(selStats ? selStats->Name : selectedId), detailPanel.X + 6, detailPanel.Y + 100, 1.5f, pal.Ink);

    StatBars sb = ComputeStatBars(*Dm, selectedId);
    float statX = detailPanel.X + 6, statW = detailPanel.Width - 12, statY = detailPanel.Y + 116;
    DrawMiniStatBar(g, statX, statY, statW, "POWER", sb.power);
    DrawMiniStatBar(g, statX, statY + 15, statW, "STAMINA", sb.stamina);
    DrawMiniStatBar(g, statX, statY + 30, statW, "SPEED", sb.speed);
    DrawMiniStatBar(g, statX, statY + 45, statW, "REACH", sb.reach);

    // BACK/CONFIRM are the last two entries in Buttons (DrawUiButton already
    // applies the gloss cap itself for the primary CONFIRM button).
    for (size_t i = ids.size() + 1; i < Buttons.size(); i++) DrawUiButton(g, Buttons[i]);
}

// ---------------------------------------------------------------------
// VS screen (new)
// ---------------------------------------------------------------------
void App::DrawVS(Graphics& g) {
    const auto& pal = GetPalette();

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

    // Face-off silhouettes: standing in the two symmetric placement frames
    // (see ComputeTwoBoxLayout in Layout.h - the same 80x95/gap-80/bottom-8
    // pattern used elsewhere), facing inward toward each other. Drawn once
    // the color panels have swept in but before the name/diamond/prompt
    // text, so that text stays legible on top.
    if (t > 0.5) {
        double poseT = std::min(1.0, (t - 0.5) / 0.5);
        TwoBoxLayout frames = ComputeTwoBoxLayout(static_cast<float>(VirtualW), static_cast<float>(VirtualH));
        // heightScale so an idle silhouette (90px tall at scale 1.0, i.e.
        // 108*kCharScale+2 matching the sprite-path height formula, see
        // kCharScale's comment in Draw.cpp) fits within the frame's height.
        float heightScale = static_cast<float>((frames.Left.Height / 90.0) * poseT);
        Color p1Color = p1 ? Color(255, static_cast<BYTE>(p1->ColorR), static_cast<BYTE>(p1->ColorG), static_cast<BYTE>(p1->ColorB)) : pal.White;
        Color p2Color = p2 ? Color(255, static_cast<BYTE>(p2->ColorR), static_cast<BYTE>(p2->ColorG), static_cast<BYTE>(p2->ColorB)) : pal.White;
        DrawHumanoid(g, frames.Left.X + frames.Left.Width / 2.0, frames.Left.Y + frames.Left.Height, p1Color, {heightScale, 1});
        DrawHumanoid(g, frames.Right.X + frames.Right.Width / 2.0, frames.Right.Y + frames.Right.Height, p2Color, {heightScale, -1});
    }

    if (t > 0.3) {
        DrawPixelTextCentered(g, Utf8ToWide(p1 ? p1->Name : P1CharId), RectF(0, VirtualH / 2.0f - 24, half, 24), 2.0f, pal.White);
        DrawPixelTextCentered(g, Utf8ToWide(p2 ? p2->Name : P2CharId), RectF(half, VirtualH / 2.0f - 24, half, 24), 2.0f, pal.White);
    }

    if (VsTimer > 0.5) {
        double diamondT = std::min(1.0, (VsTimer - 0.5) / 0.3);
        float size = static_cast<float>(36 * diamondT);
        PointF pts[4] = {
            PointF(VirtualW / 2.0f, VirtualH / 2.0f - size),
            PointF(VirtualW / 2.0f + size, VirtualH / 2.0f),
            PointF(VirtualW / 2.0f, VirtualH / 2.0f + size),
            PointF(VirtualW / 2.0f - size, VirtualH / 2.0f),
        };
        SolidBrush diaBrush(pal.White);
        g.FillPolygon(&diaBrush, pts, 4);
        float vsDot = static_cast<float>(std::max(1.0, 2.0 * diamondT));
        DrawPixelTextCentered(g, L"VS", RectF(VirtualW / 2.0f - size, VirtualH / 2.0f - size, size * 2, size * 2), vsDot, pal.Accent);
    }

    DrawPixelTextCentered(g, L"PRESS ANY KEY TO CONTINUE", RectF(0, static_cast<REAL>(VirtualH - 18), static_cast<REAL>(VirtualW), 12), 1.0f, pal.White);
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
    ResetCamera(Battle->Player1.PositionX, Battle->Player2.PositionX);
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

    if (!IsTrainingMode) {
        // Versus mode: a real CPU opponent, nothing to configure - keep
        // the pause menu simple.
        float panelTop = VirtualH / 2.0f - 35;
        Buttons.push_back(MakeButton(cx - 70, panelTop + 26, 140, 18, "RESUME (Esc)", true, [this]() { Paused = false; BuildGameButtons(); }));
        Buttons.push_back(MakeButton(cx - 70, panelTop + 48, 140, 18, "GIVE UP -> TITLE", false, [this]() { GoTo(Screen::Title); }));
        return;
    }

    float panelTop = VirtualH / 2.0f - 75;
    Buttons.push_back(MakeButton(cx - 60, panelTop + 22, 120, 16, "RESUME (Esc)", true, [this]() { Paused = false; BuildGameButtons(); }));

    auto modeButton = [this, cx, panelTop](float x, const char* label, DummyMode mode) {
        bool active = (P2DummyMode == mode);
        return MakeButton(x, panelTop + 46, 58, 14, label, active, [this, mode]() {
            P2DummyMode = mode;
            if (Battle) Battle->CpuAI->Mode = mode;
            BuildGameButtons();
        });
    };
    Buttons.push_back(modeButton(cx - 124, "CPU", DummyMode::CPU));
    Buttons.push_back(modeButton(cx - 62, "STAND", DummyMode::Stand));
    Buttons.push_back(modeButton(cx, "CROUCH", DummyMode::Crouch));
    Buttons.push_back(modeButton(cx + 62, "JUMP", DummyMode::Jump));

    Buttons.push_back(MakeButton(cx - 118, panelTop + 66, 114, 14,
        TrainingAutoHealPref ? "AUTO HEAL: ON" : "AUTO HEAL: OFF", TrainingAutoHealPref, [this]() {
            TrainingAutoHealPref = !TrainingAutoHealPref;
            if (Battle) Battle->TrainingAutoHeal = TrainingAutoHealPref;
            BuildGameButtons();
        }));
    Buttons.push_back(MakeButton(cx + 4, panelTop + 66, 114, 14, "RESET HP", false, [this]() {
        if (Battle) Battle->ResetHP();
    }));

    Buttons.push_back(MakeButton(cx - 70, panelTop + 90, 140, 16, "GIVE UP -> TITLE", false, [this]() { GoTo(Screen::Title); }));
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

    // Thin floor line at the ground (OriginY, see Draw.h), leaving a
    // narrow footroom band before the GAUGE HUD row at the bottom edge.
    SolidBrush groundBrush(pal.ArenaPanel);
    g.FillRectangle(&groundBrush, 0.0f, static_cast<REAL>(ToScreenY(0)), static_cast<REAL>(VirtualW), 4.0f);

    DrawFighter(g, Battle->Player1, BaseDataDir, UserDir);
    DrawFighter(g, Battle->Player2, BaseDataDir, UserDir);
    for (const auto& proj : Battle->Projectiles) DrawProjectile(g, proj);
    for (const auto& fx : Effects) DrawEffect(g, fx);
    // Counter/Effective Counter info is pinned to the scoring player's
    // screen edge (1P left, 2P right) rather than floating over the hit.
    for (const auto& fx : Effects) DrawCounterEdgeLabel(g, fx);

    DrawHUD(g, *Battle, LastP1Combo, LastP2Combo, std::max(P1ComboFade, P2ComboFade));
    if (DebugVisible && IsTrainingMode) DrawDebugOverlay(g, *Battle);

    g.Restore(shakeState);

    if (Paused) {
        SolidBrush dim(Color(140, 0, 0, 0));
        g.FillRectangle(&dim, 0.0f, 0.0f, static_cast<REAL>(VirtualW), static_cast<REAL>(VirtualH));

        float cx = VirtualW / 2.0f;
        RectF panelRect = IsTrainingMode ? RectF(cx - 135, VirtualH / 2.0f - 85, 270, 140)
                                          : RectF(cx - 80, VirtualH / 2.0f - 45, 160, 90);
        GraphicsPath path;
        AddRoundedRect(path, panelRect, 0.0f);
        SolidBrush panelBg(pal.PanelBg);
        g.FillPath(&panelBg, &path);
        Pen border(pal.Ink, 1.5f);
        g.DrawPath(&border, &path);

        DrawPixelTextCentered(g, IsTrainingMode ? L"TRAINING - PAUSED" : L"PAUSED", RectF(panelRect.X, panelRect.Y + 2, panelRect.Width, 14), 1.5f, pal.TextDark);
        if (IsTrainingMode) {
            DrawPixelTextCentered(g, L"P2 CONTROL MODE (PRACTICE)", RectF(panelRect.X, panelRect.Y + 34, panelRect.Width, 8), 1.0f, pal.TextGray);
        }

        DrawButtons(g, Buttons);
    }
}

// ---------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------
void App::BuildResultButtons() {
    Buttons.clear();
    float y = 160, w = 122, gap = 6, x = 3;
    Buttons.push_back(MakeButton(x, y, w, 16, "REMATCH", true, [this]() { GoTo(Screen::Game); }));
    Buttons.push_back(MakeButton(x + w + gap, y, w, 16, "SELECT", false, [this]() { GoTo(Screen::CharacterSelect); }));
    Buttons.push_back(MakeButton(x + 2 * (w + gap), y, w, 16, "TITLE", false, [this]() { GoTo(Screen::Title); }));
}

// Centered composition: banner, quote, three stat tiles, a small "win
// pose" panel (the winner's pixel-art silhouette, no photo sprite), then
// the action buttons - replaces the old two-column layout that no longer
// fits the 384x224 canvas.
void App::DrawResult(Graphics& g) {
    const auto& pal = GetPalette();
    std::wstring resultText = L"DRAW";
    std::wstring quote = L"A close match.";
    if (!LastResult.isDraw) {
        if (LastResult.winnerIsPlayer) { resultText = L"YOU WIN"; quote = L"Victory earned through discipline."; }
        else { resultText = L"CPU WIN"; quote = L"Train harder. Try again."; }
    }

    RectF banner((VirtualW - 170) / 2.0f, 4, 170, 22);
    GraphicsPath path;
    AddRoundedRect(path, banner, 0.0f);
    SolidBrush bannerBrush(pal.Accent);
    g.FillPath(&bannerBrush, &path);
    DrawGlossCap(g, banner);
    Pen bannerBorder(pal.Ink, 1.5f);
    g.DrawPath(&bannerBorder, &path);
    DrawPixelTextCentered(g, resultText, banner, 2.0f, pal.White);

    DrawPixelTextCentered(g, quote, RectF(0, 28, static_cast<REAL>(VirtualW), 8), 1.0f, pal.Ink70);

    struct Tile { std::wstring label; std::wstring value; };
    std::vector<Tile> tiles = {
        {L"COMBO", std::to_wstring(LastResult.maxCombo) + L" HITS"},
        {L"TIME", std::to_wstring(LastResult.timeLeftSeconds) + L"S"},
        {L"DAMAGE", std::to_wstring(LastResult.damageTaken)},
    };
    float tileW = 72, tileH = 30, gap = 4;
    float tilesStartX = (VirtualW - (tileW * 3 + gap * 2)) / 2.0f;
    for (size_t i = 0; i < tiles.size(); i++) {
        RectF tr(tilesStartX + i * (tileW + gap), 38, tileW, tileH);
        GraphicsPath tp;
        AddRoundedRect(tp, tr, 0.0f);
        SolidBrush tileBg(pal.PanelBg);
        g.FillPath(&tileBg, &tp);
        Pen tileBorder(pal.Border, 1.0f);
        g.DrawPath(&tileBorder, &tp);
        DrawPixelTextCentered(g, tiles[i].value, RectF(tr.X, tr.Y + 3, tr.Width, 14), 1.5f, pal.Accent);
        DrawPixelTextCentered(g, tiles[i].label, RectF(tr.X, tr.Y + 18, tr.Width, 10), 1.0f, pal.TextGray);
    }

    // "WIN POSE" panel - the winner's pixel-art silhouette, tinted panel
    // with a name/subtitle strip below it.
    RectF winPanel((VirtualW - 140) / 2.0f, 76, 140, 78);
    GraphicsPath wp;
    AddRoundedRect(wp, winPanel, 0.0f);
    SolidBrush winBg(pal.PanelBg);
    g.FillPath(&winBg, &wp);
    Pen winBorder(pal.Ink, 1.5f);
    g.DrawPath(&winBorder, &wp);

    RectF poseRect(winPanel.X, winPanel.Y, winPanel.Width, winPanel.Height - 22);
    SolidBrush poseBg(pal.TintRed);
    g.FillRectangle(&poseBg, poseRect);
    bool winnerIsP1 = LastResult.isDraw || LastResult.winnerIsPlayer;
    const std::string& winnerId = winnerIsP1 ? P1CharId : P2CharId;
    auto* winnerStats = Dm->GetCharacter(winnerId);
    Color bodyColor = winnerStats ? Color(255, static_cast<BYTE>(winnerStats->ColorR), static_cast<BYTE>(winnerStats->ColorG), static_cast<BYTE>(winnerStats->ColorB)) : pal.TextDark;
    DrawHumanoid(g, poseRect.X + poseRect.Width / 2.0, poseRect.Y + poseRect.Height - 4, bodyColor, {0.35, 1, 20, 0, 0, 0});
    Pen poseDivider(pal.Ink, 1.5f);
    g.DrawLine(&poseDivider, winPanel.X, winPanel.Y + poseRect.Height, winPanel.X + winPanel.Width, winPanel.Y + poseRect.Height);

    DrawPixelTextCentered(g, Utf8ToWide(winnerStats ? winnerStats->Name : winnerId), RectF(winPanel.X, winPanel.Y + poseRect.Height + 2, winPanel.Width, 10), 1.5f, pal.Ink);
    DrawPixelTextCentered(g, LastResult.isDraw ? L"DRAW" : (winnerIsP1 ? L"PLAYER 1" : L"CPU"), RectF(winPanel.X, winPanel.Y + poseRect.Height + 12, winPanel.Width, 8), 1.0f, pal.Ink55);

    DrawButtons(g, Buttons);
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
    float tileW = 118, tileH = 26, gap = 3;
    int cols = 3;
    float startX = (VirtualW - (tileW * cols + gap * (cols - 1))) / 2.0f;
    float startY = 28;
    for (size_t i = 0; i < presets.size(); i++) {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;
        float x = startX + col * (tileW + gap);
        float y = startY + row * (tileH + gap);
        Buttons.push_back(MakeButton(x, y, tileW, tileH, "", static_cast<int>(i) == PendingResIndex, [this, i]() { PendingResIndex = static_cast<int>(i); BuildSettingsButtons(); }));
    }
    float cx = VirtualW / 2.0f;
    Buttons.push_back(MakeButton(cx - 118, 204, 110, 16, "BACK", false, [this]() { GoTo(Screen::Title); }));
    Buttons.push_back(MakeButton(cx + 8, 204, 110, 16, "APPLY", true, [this]() {
        const auto& p = ResolutionPresets()[PendingResIndex];
        ApplyResolution(p.width, p.height);
        GoTo(Screen::Title);
    }));
}

void App::DrawSettings(Graphics& g) {
    const auto& pal = GetPalette();

    DrawPixelTextCentered(g, L"SETTINGS", RectF(0, 4, static_cast<REAL>(VirtualW), 12), 1.5f, pal.TextDark);
    DrawPixelTextCentered(g, L"DISPLAY RESOLUTION (320X200 - 1920X1080)", RectF(0, 16, static_cast<REAL>(VirtualW), 8), 1.0f, pal.TextGray);

    const auto& presets = ResolutionPresets();
    for (size_t i = 0; i < Buttons.size() - 2 && i < presets.size(); i++) {
        const auto& b = Buttons[i];
        bool selected = static_cast<int>(i) == PendingResIndex;
        GraphicsPath path;
        AddRoundedRect(path, b.Rect, 0.0f);
        SolidBrush fill(selected ? pal.Accent : pal.PanelBg);
        g.FillPath(&fill, &path);
        Pen border(selected ? pal.Accent : pal.Border, 1.0f);
        g.DrawPath(&border, &path);
        Color textColor = selected ? pal.White : pal.TextDark;
        DrawPixelTextCentered(g, Utf8ToWide(presets[i].label), RectF(b.Rect.X, b.Rect.Y + 3, b.Rect.Width, 12), 1.0f, textColor);
        DrawPixelTextCentered(g, Utf8ToWide(presets[i].aspect), RectF(b.Rect.X, b.Rect.Y + 15, b.Rect.Width, 9), 1.0f, selected ? pal.White : pal.TextGray);
    }
    DrawUiButton(g, Buttons[Buttons.size() - 2]);
    DrawUiButton(g, Buttons[Buttons.size() - 1]);
}

} // namespace kakuge

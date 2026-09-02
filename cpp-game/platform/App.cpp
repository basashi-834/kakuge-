// platform/App.cpp
#include "App.h"
#include <algorithm>
#include <cmath>

using namespace Gdiplus;

namespace kakuge {

App* g_App = nullptr;

void App::Init(HWND hwnd, const fs::path& baseDataDir, const fs::path& userDir) {
    Hwnd = hwnd;
    BaseDataDir = baseDataDir;
    UserDir = userDir;
    Dm = std::make_unique<DataManager>(baseDataDir, userDir);
    Dm->ReloadAll();
    AppSettings = Dm->LoadSettings();
    if (Dm->GetCharacterIds().empty()) {
        // Should never happen (ryu.json ships in Data/characters), but
        // guard against a corrupted/missing install rather than crashing.
    } else {
        P1CharId = Dm->GetCharacterIds()[0];
        P2CharId = Dm->GetCharacterIds()[0];
    }
    GoTo(Screen::Title);
}

void App::Shutdown() {
    DestroyEditorControls();
}

ViewTransform App::CurrentTransform() const {
    RECT rc;
    GetClientRect(Hwnd, &rc);
    return ViewTransform::Compute(rc.right - rc.left, rc.bottom - rc.top);
}

void App::GoTo(Screen s) {
    if (Current == Screen::Editor && s != Screen::Editor) LeaveEditor();
    Current = s;
    switch (s) {
        case Screen::Title: BuildTitleButtons(); break;
        case Screen::CharacterSelect: SelectStep = 0; BuildSelectButtons(); break;
        case Screen::VS: VsTimer = 0.0; Buttons.clear(); break;
        case Screen::Game: BuildGameButtons(); StartMatch(); break;
        case Screen::Result: BuildResultButtons(); break;
        case Screen::Settings: {
            const auto& presets = ResolutionPresets();
            PendingResIndex = 0;
            for (size_t i = 0; i < presets.size(); i++) {
                if (presets[i].width == AppSettings.Width && presets[i].height == AppSettings.Height) { PendingResIndex = static_cast<int>(i); break; }
            }
            BuildSettingsButtons();
            break;
        }
        case Screen::Editor: EnterEditor(); break;
    }
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(Hwnd, &ps);
    RECT rc;
    GetClientRect(Hwnd, &rc);
    int w = std::max(1L, rc.right - rc.left), h = std::max(1L, rc.bottom - rc.top);

    if (!BackBuffer || BackBufferW != w || BackBufferH != h) {
        BackBuffer = std::make_unique<Bitmap>(w, h, PixelFormat32bppRGB);
        BackBufferW = w;
        BackBufferH = h;
    }

    Graphics g(BackBuffer.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    const auto& pal = GetPalette();
    SolidBrush winBg(pal.Bg);
    g.FillRectangle(&winBg, 0, 0, w, h);

    if (Current != Screen::Editor) {
        // Pixel-art pipeline: every custom-drawn screen renders into a
        // genuine 384x224 low-res buffer first (crisp, AA off), which is
        // then nearest-neighbor-scaled onto BackBuffer at the letterboxed
        // size - this is what keeps the whole game reading as real pixel
        // art instead of blurry stretched vector art at any output
        // resolution. ScreenToVirtual (used for mouse hit-testing) still
        // maps through the exact same ViewTransform math.
        if (!LowResBuffer) {
            LowResBuffer = std::make_unique<Bitmap>(VirtualW, VirtualH, PixelFormat32bppRGB);
        }
        Graphics lg(LowResBuffer.get());
        lg.SetSmoothingMode(SmoothingModeNone);
        lg.SetPixelOffsetMode(PixelOffsetModeHalf);
        lg.SetTextRenderingHint(TextRenderingHintSingleBitPerPixelGridFit);
        lg.SetInterpolationMode(InterpolationModeNearestNeighbor);

        SolidBrush canvasBg(pal.Bg);
        lg.FillRectangle(&canvasBg, 0, 0, VirtualW, VirtualH);

        switch (Current) {
            case Screen::Title: DrawTitle(lg); break;
            case Screen::CharacterSelect: DrawCharacterSelect(lg); break;
            case Screen::VS: DrawVS(lg); break;
            case Screen::Game: DrawGame(lg); break;
            case Screen::Result: DrawResult(lg); break;
            case Screen::Settings: DrawSettings(lg); break;
            default: break;
        }

        ViewTransform t = ViewTransform::Compute(w, h);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetSmoothingMode(SmoothingModeNone);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        RectF destRect(static_cast<REAL>(t.offsetX), static_cast<REAL>(t.offsetY),
                        static_cast<REAL>(VirtualW * t.scale), static_cast<REAL>(VirtualH * t.scale));
        g.DrawImage(LowResBuffer.get(), destRect, 0, 0, static_cast<REAL>(VirtualW), static_cast<REAL>(VirtualH), UnitPixel);
    } else {
        // Editor: native controls own the form area below, but the header
        // bar itself is drawn with GDI+ (in real window pixels, not the
        // low-res pixel-art pipeline, since it sits alongside native
        // controls that use real pixel coordinates) so the screen reads as
        // part of the same app instead of a bare Win32 dialog.
        SolidBrush headerBg(pal.Accent);
        g.FillRectangle(&headerBg, 0, 0, w, kEditorHeaderHeight);
        Font headerFont(UiFontFamily(), 20, FontStyleBold, UnitPixel);
        DrawTextCentered(g, L"CHARACTER EDITOR", headerFont, RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(kEditorHeaderHeight)), pal.White);
        DrawEditorPreview(g);
        DrawMotionImagePreview(g);
    }

    // Single blit of the fully-rendered frame onto the real window surface
    // - this is what actually eliminates the flicker (see BackBuffer's
    // declaration comment in App.h).
    Graphics screenG(hdc);
    screenG.DrawImage(BackBuffer.get(), 0, 0);

    EndPaint(Hwnd, &ps);
}

void App::OnResize(int, int) {
    if (Current != Screen::Editor) InvalidateRect(Hwnd, nullptr, FALSE);
}

namespace {
// The currently-selected hurtbox part (for the active stance) or hitbox
// (by EditorHitboxIndex), whichever EditorDragTargetIsHurtbox names -
// shared by OnLButtonDown/OnMouseMove below. Returns nullptr pairs if
// there's nothing selected (e.g. a move with zero hitboxes).
void EditorSelectedBox(App& app, double** cx, double** cy, double** bw, double** bh) {
    *cx = *cy = *bw = *bh = nullptr;
    if (app.EditorDragTargetIsHurtbox) {
        const char* stance = app.EditorHurtStance == 1 ? "crouch" : app.EditorHurtStance == 2 ? "air" : "stand";
        auto& parts = app.EditorHurtboxDraft.PartsForStance(stance);
        if (app.EditorHurtPartIndex < 0 || app.EditorHurtPartIndex >= static_cast<int>(parts.size())) return;
        RectBox& box = parts[app.EditorHurtPartIndex].Box;
        *cx = &box.CenterX; *cy = &box.CenterY; *bw = &box.Width; *bh = &box.Height;
    } else {
        if (app.EditorHitboxIndex < 0 || app.EditorHitboxIndex >= static_cast<int>(app.EditorHitboxDraftList.size())) return;
        HitboxDef& box = app.EditorHitboxDraftList[app.EditorHitboxIndex];
        *cx = &box.offsetX; *cy = &box.offsetY; *bw = &box.width; *bh = &box.height;
    }
}
} // namespace

void App::OnLButtonDown(int x, int y) {
    if (Current == Screen::Editor) {
        // Native controls handle their own clicks; the only thing this
        // window proc needs to do is start a drag if the click landed
        // inside the hitbox/hurtbox preview canvas (drawn directly in real
        // window pixels by DrawEditorPreview, not a child control).
        double *cxp, *cyp, *bwp, *bhp;
        EditorSelectedBox(*this, &cxp, &cyp, &bwp, &bhp);
        if (!cxp) return; // nothing selected to drag (e.g. move has no hitboxes)
        double& cx = *cxp; double& cy = *cyp; double& bw = *bwp; double& bh = *bhp;

        float groundX = EditorPreviewRect.X + EditorPreviewRect.Width / 2.0f;
        float groundY = EditorPreviewRect.Y + EditorPreviewRect.Height - 20.0f;
        double pcx = groundX + cx * EditorPreviewScale;
        double pcy = groundY + cy * EditorPreviewScale;
        double pw = bw * EditorPreviewScale, ph = bh * EditorPreviewScale;
        double left = pcx - pw / 2.0, top = pcy - ph / 2.0, right = pcx + pw / 2.0, bottom = pcy + ph / 2.0;

        bool inPanel = x >= EditorPreviewRect.X && x <= EditorPreviewRect.X + EditorPreviewRect.Width &&
                       y >= EditorPreviewRect.Y && y <= EditorPreviewRect.Y + EditorPreviewRect.Height;
        if (!inPanel) return;

        bool nearCorner = std::abs(x - right) <= 10 && std::abs(y - bottom) <= 10;
        EditorDragStartMouseX = x;
        EditorDragStartMouseY = y;
        EditorDragStartCenterX = cx;
        EditorDragStartCenterY = cy;
        EditorDragStartW = bw;
        EditorDragStartH = bh;
        if (nearCorner) {
            EditorDragResizing = true;
            EditorDragging = false;
        } else if (x >= left && x <= right && y >= top && y <= bottom) {
            EditorDragging = true;
            EditorDragResizing = false;
        }
        return;
    }
    ViewTransform t = CurrentTransform();
    double vx, vy;
    t.ScreenToVirtual(x, y, vx, vy);
    for (auto& b : Buttons) {
        if (b.HitTest(vx, vy)) {
            if (b.OnClick) b.OnClick();
            InvalidateRect(Hwnd, nullptr, FALSE);
            return;
        }
    }
}

void App::OnMouseMove(int x, int y) {
    if (Current != Screen::Editor || (!EditorDragging && !EditorDragResizing)) return;
    double dxWorld = (x - EditorDragStartMouseX) / EditorPreviewScale;
    double dyWorld = (y - EditorDragStartMouseY) / EditorPreviewScale;

    double *cx, *cy, *bw, *bh;
    EditorSelectedBox(*this, &cx, &cy, &bw, &bh);
    if (!cx) return;

    if (EditorDragging) {
        *cx = EditorDragStartCenterX + dxWorld;
        *cy = EditorDragStartCenterY + dyWorld;
    } else if (EditorDragResizing) {
        *bw = std::max(8.0, EditorDragStartW + dxWorld * 2.0);
        *bh = std::max(8.0, EditorDragStartH + dyWorld * 2.0);
    }
    ClampBoxToPreview(*cx, *cy, *bw, *bh);
    if (EditorDragTargetIsHurtbox) SyncHurtboxFieldsFromDraft(); else SyncHitboxFieldsFromDraft();
    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::OnLButtonUp(int, int) {
    EditorDragging = false;
    EditorDragResizing = false;
}

void App::OnKeyDown(int vk) {
    if (Current == Screen::Game) {
        if (vk == VK_ESCAPE) {
            Paused = !Paused;
            if (Paused) HeldKeys.clear();
            BuildGameButtons();
            InvalidateRect(Hwnd, nullptr, FALSE);
            return;
        }
        if (vk == VK_F1 && IsTrainingMode) {
            // Debug display is a practice-mode tool only (not shown in a
            // real Versus match), so F1 is simply inert there.
            DebugVisible = !DebugVisible;
            InvalidateRect(Hwnd, nullptr, FALSE);
            return;
        }
        HeldKeys.insert(vk);
    } else if (Current == Screen::VS) {
        GoTo(Screen::Game);
    }
}

void App::OnKeyUp(int vk) {
    HeldKeys.erase(vk);
}

RawInput App::CollectP1Input() const {
    RawInput input;
    input.Left = HeldKeys.count('A') > 0;
    input.Right = HeldKeys.count('D') > 0;
    input.Down = HeldKeys.count('S') > 0;
    input.Up = HeldKeys.count(VK_SPACE) > 0;
    // 6-button scheme: U/I/O = LP/MP/HP, J/K/L = LK/MK/HK.
    input.Buttons.LP = HeldKeys.count('U') > 0;
    input.Buttons.MP = HeldKeys.count('I') > 0;
    input.Buttons.HP = HeldKeys.count('O') > 0;
    input.Buttons.LK = HeldKeys.count('J') > 0;
    input.Buttons.MK = HeldKeys.count('K') > 0;
    input.Buttons.HK = HeldKeys.count('L') > 0;
    return input;
}

void App::OnTimer() {
    if (Current == Screen::VS) {
        VsTimer += 0.015;
        if (VsTimer > 1.8) GoTo(Screen::Game);
        else InvalidateRect(Hwnd, nullptr, FALSE);
        return;
    }
    if (Current != Screen::Game || MatchFinished) return;

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - LastTick).count();
    LastTick = now;
    if (elapsed > 0.25) elapsed = 0.25;

    if (!Paused) {
        Accumulator += elapsed;
        int steps = 0;
        while (Accumulator >= FixedDt && steps < 6) {
            RawInput p1 = CollectP1Input();
            Battle->CpuAI->Mode = P2DummyMode;
            Battle->Update(FixedDt, p1);
            UpdateCamera(Battle->Player1.PositionX, Battle->Player2.PositionX, FixedDt);
            extern void PlaySoundEvent(const std::string&);
            for (const auto& snd : Battle->AllSounds) PlaySoundEvent(snd);
            for (const auto& fx : Battle->AllEffects) Effects.push_back({fx.kind, fx.x, fx.y, 0.0, fx.side});
            Accumulator -= FixedDt;
            steps++;
        }

        std::vector<LiveEffect> survivors;
        for (auto& fx : Effects) {
            fx.age += elapsed;
            auto style = GetEffectStyle(fx.kind);
            if (fx.age < style.duration) survivors.push_back(fx);
        }
        Effects = std::move(survivors);

        int p1c = Battle->P1ComboCount, p2c = Battle->P2ComboCount;
        if (p1c > 0) { LastP1Combo = p1c; P1ComboFade = 1.0; } else if (P1ComboFade > 0) P1ComboFade -= elapsed * 0.6;
        if (p2c > 0) { LastP2Combo = p2c; P2ComboFade = 1.0; } else if (P2ComboFade > 0) P2ComboFade -= elapsed * 0.6;

        if (!Battle->MatchActive && !MatchFinished) {
            MatchFinished = true;
            LastResult.winnerIsPlayer = (Battle->Winner == &Battle->Player1);
            LastResult.isDraw = Battle->IsDraw;
            LastResult.maxCombo = std::max(Battle->P1MaxCombo, Battle->P2MaxCombo);
            LastResult.timeLeftSeconds = std::max(0, Battle->FramesLeft / 60);
            LastResult.damageTaken = Battle->Player1.Stats.MaxHP - Battle->Player1.CurrentHP;
            GoTo(Screen::Result);
            return;
        }
    }

    InvalidateRect(Hwnd, nullptr, FALSE);
}

void App::OnCommand(int controlId, int notifyCode, HWND ctrl) {
    if (Current == Screen::Editor) {
        extern void Editor_OnCommand(App&, int, int, HWND);
        Editor_OnCommand(*this, controlId, notifyCode, ctrl);
    }
}

void App::OnDrawItem(DRAWITEMSTRUCT* dis) {
    // ODT_BUTTON (owner-draw buttons), ODT_STATIC (pixel-font labels) and
    // ODT_COMBOBOX (palette-styled dropdowns) all route to the same
    // handler - see Editor_OnDrawItem's dispatch on dis->CtlType.
    if (Current == Screen::Editor) {
        extern void Editor_OnDrawItem(DRAWITEMSTRUCT*);
        Editor_OnDrawItem(dis);
    }
}

} // namespace kakuge

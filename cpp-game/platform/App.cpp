// platform/App.cpp
#include "App.h"
#include <algorithm>

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
        ViewTransform t = ViewTransform::Compute(w, h);
        t.ApplyTo(g);
        SolidBrush canvasBg(pal.Bg);
        g.FillRectangle(&canvasBg, 0.0f, 0.0f, static_cast<REAL>(VirtualW), static_cast<REAL>(VirtualH));

        switch (Current) {
            case Screen::Title: DrawTitle(g); break;
            case Screen::CharacterSelect: DrawCharacterSelect(g); break;
            case Screen::VS: DrawVS(g); break;
            case Screen::Game: DrawGame(g); break;
            case Screen::Result: DrawResult(g); break;
            case Screen::Settings: DrawSettings(g); break;
            default: break;
        }
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

void App::OnLButtonDown(int x, int y) {
    if (Current == Screen::Editor) return; // native controls handle their own clicks
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

void App::OnKeyDown(int vk) {
    if (Current == Screen::Game) {
        if (vk == VK_ESCAPE) {
            Paused = !Paused;
            if (Paused) HeldKeys.clear();
            BuildGameButtons();
            InvalidateRect(Hwnd, nullptr, FALSE);
            return;
        }
        if (vk == VK_F1) {
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
    input.Buttons.Light = HeldKeys.count('J') > 0;
    input.Buttons.Medium = HeldKeys.count('K') > 0;
    input.Buttons.Heavy = HeldKeys.count('L') > 0;
    input.Buttons.Special = HeldKeys.count('U') > 0;
    input.Buttons.Super = HeldKeys.count('I') > 0;
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
            extern void PlaySoundEvent(const std::string&);
            for (const auto& snd : Battle->AllSounds) PlaySoundEvent(snd);
            for (const auto& fx : Battle->AllEffects) Effects.push_back({fx.kind, fx.x, fx.y, 0.0});
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

} // namespace kakuge

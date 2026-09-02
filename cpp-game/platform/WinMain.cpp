// platform/WinMain.cpp
// Entry point: window class registration, GDI+ startup/shutdown, WndProc,
// message loop, and the fixed-timestep game loop timer. Native-Windows-
// only, zero-install (Win32 + GDI+ ship with every Windows 10/11 desktop).
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <string>
#include <fstream>
#include "App.h"
#include <nlohmann/json.hpp>

using namespace kakuge;

namespace kakuge {
int g_RoundTimeSeconds = 99;
fs::path g_AudioDir;

// Sound playback: same convention as the earlier WinForms edition's
// AudioHelper.ps1 - no .wav files ship by default, and playback silently
// no-ops (SND_NODEFAULT suppresses the fallback system beep) when a clip
// is missing, so gameplay never depends on audio existing. Drop matching
// attack.wav / hit.wav / block.wav / ko.wav files into Audio\ next to the
// .exe and they play automatically, no code changes required.
void PlaySoundEvent(const std::string& kind) {
    if (g_AudioDir.empty()) return;
    fs::path wav = g_AudioDir / (kind + ".wav");
    std::error_code ec;
    if (!fs::is_regular_file(wav, ec)) return;
    PlaySoundW(wav.wstring().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}
} // namespace kakuge

static ULONG_PTR g_GdiplusToken;
static constexpr UINT_PTR TIMER_ID = 1;
static constexpr wchar_t kWindowClass[] = L"KakugeWindowClass";

static fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path p(buf);
    return p.parent_path();
}

static fs::path UserAppDataDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return fs::path(path) / L"Kakuge";
    }
    return ExeDir() / "UserData";
}

static void LoadMatchRules(const fs::path& dataDir) {
    std::ifstream f(dataDir / "match_rules.json", std::ios::binary);
    if (!f.good()) return;
    try {
        nlohmann::json j;
        f >> j;
        g_RoundTimeSeconds = j.value("roundTimeSeconds", 99);
    } catch (...) {
        // Keep the default on any parse error - never let a corrupted/
        // hand-edited config file prevent the game from starting.
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            if (g_App) g_App->OnPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1; // painted fully in WM_PAINT; avoids flicker
        case WM_SIZE:
            if (g_App) g_App->OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN:
            if (g_App) g_App->OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if (g_App) g_App->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            if (g_App) g_App->OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYDOWN:
            if (g_App) g_App->OnKeyDown(static_cast<int>(wParam));
            return 0;
        case WM_KEYUP:
            if (g_App) g_App->OnKeyUp(static_cast<int>(wParam));
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_ID && g_App) g_App->OnTimer();
            return 0;
        case WM_COMMAND:
            if (g_App) g_App->OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_DRAWITEM:
            if (g_App) g_App->OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLOREDIT: {
            // Recolors every Character Editor EDIT box to the same panel/
            // ink palette the rest of the game uses (a flat white field
            // with dark text), so it reads as part of the same design even
            // though - unlike owner-drawn buttons/labels/combos - a plain
            // EDIT control's live-typed text has to stay in the real
            // system font (Win32 has no owner-draw hook for EDIT).
            if (g_App && g_App->Current == Screen::Editor) {
                static HBRUSH editBg = CreateSolidBrush(RGB(GetPalette().PanelBg.GetR(), GetPalette().PanelBg.GetG(), GetPalette().PanelBg.GetB()));
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, RGB(GetPalette().Ink.GetR(), GetPalette().Ink.GetG(), GetPalette().Ink.GetB()));
                SetBkColor(hdc, RGB(GetPalette().PanelBg.GetR(), GetPalette().PanelBg.GetG(), GetPalette().PanelBg.GetB()));
                return reinterpret_cast<INT_PTR>(editBg);
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_GdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    fs::path dataDir = ExeDir() / "Data";
    fs::path userDir = UserAppDataDir();
    LoadMatchRules(dataDir);
    g_AudioDir = ExeDir() / "Audio";

    App app;
    g_App = &app;

    // Provisional window at VirtualW/H (384x224) so App::Init (which needs
    // a valid Hwnd for later resizing) has somewhere to attach to; resized
    // to the saved Settings immediately after Init loads them.
    RECT rc{0, 0, VirtualW, VirtualH};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"KAKUGE", (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
                                 CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                 nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    app.Init(hwnd, dataDir, userDir);
    {
        RECT rc2{0, 0, app.AppSettings.Width, app.AppSettings.Height};
        AdjustWindowRect(&rc2, static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE)), FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, rc2.right - rc2.left, rc2.bottom - rc2.top, SWP_NOMOVE | SWP_NOZORDER);
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetTimer(hwnd, TIMER_ID, 15, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, TIMER_ID);
    app.Shutdown();
    g_App = nullptr;
    Gdiplus::GdiplusShutdown(g_GdiplusToken);
    return static_cast<int>(msg.wParam);
}

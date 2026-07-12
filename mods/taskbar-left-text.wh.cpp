// ==WindhawkMod==
// @id              taskbar-left-text
// @name            Taskbar Left Text
// @description     Shows custom text on the left side of the taskbar
// @version         0.2
// @author          Your Name
// @github          https://github.com/your-handle
// @include         explorer.exe
// @compilerOptions -lcomctl32 -lgdi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Left Text

Shows configurable text on the left side of the Windows taskbar.

It renders a click-through, per-pixel-alpha overlay window owned by the
taskbar, so the text floats over the taskbar (including the native Windows 11
XAML taskbar) without blocking clicks.

## Notes
- Targets `explorer.exe` (the shell that hosts the taskbar).
- Primary taskbar only (multi-monitor is not supported in this version).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- Text: "★ Taskbar"
  $name: Text
  $description: Text to display on the left side of the taskbar
- OffsetX: 12
  $name: Offset from left
  $description: Horizontal offset from the taskbar's left edge, in pixels
- FontSize: 13
  $name: Font size
- Color: "#FFFFFF"
  $name: Text color
  $description: Hex color, e.g. #FFFFFF or #FFAA00
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <string.h>
#include <windhawk_utils.h>

#define WM_TBLT_INIT    (WM_USER + 0x100)
#define WM_TBLT_APPLY   (WM_USER + 0x101)
#define WM_TBLT_DESTROY (WM_USER + 0x102)
#define TIMER_ID        4242
#define OVERLAY_CLASS   L"TaskbarLeftTextOverlay"

struct Settings {
    PCWSTR text;
    int offsetX;
    int fontSize;
    COLORREF color;
};
Settings g_settings;

HWND g_hTaskbar = nullptr;
HWND g_hwndOverlay = nullptr;
HFONT g_font = nullptr;
bool g_classRegistered = false;

static int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

static COLORREF ParseColor(PCWSTR hex) {
    if (!hex) return RGB(255, 255, 255);
    if (*hex == L'#') hex++;
    unsigned long v = 0;
    int n = 0;
    while (*hex && n < 6) {
        int d = HexVal(*hex);
        if (d < 0) break;
        v = (v << 4) | (unsigned long)d;
        hex++;
        n++;
    }
    if (n == 0) return RGB(255, 255, 255);
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void LoadSettings() {
    g_settings.text = Wh_GetStringSetting(L"Text");
    g_settings.offsetX = Wh_GetIntSetting(L"OffsetX");
    g_settings.fontSize = Wh_GetIntSetting(L"FontSize");
    PCWSTR colorStr = Wh_GetStringSetting(L"Color");
    g_settings.color = ParseColor(colorStr);
    Wh_FreeStringSetting(colorStr);
}

void FreeSettings() {
    if (g_settings.text) {
        Wh_FreeStringSetting(g_settings.text);
        g_settings.text = nullptr;
    }
}

void CreateOverlayFont() {
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    int size = g_settings.fontSize > 4 ? g_settings.fontSize : 13;
    g_font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void UpdateOverlay() {
    if (!g_hwndOverlay || !g_hTaskbar || !g_font) return;

    RECT rcT;
    if (!GetWindowRect(g_hTaskbar, &rcT)) return;
    int barH = rcT.bottom - rcT.top;
    if (barH <= 0) return;

    PCWSTR txt = g_settings.text ? g_settings.text : L"";
    int len = (int)wcslen(txt);

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ oldFont = SelectObject(mem, g_font);

    SIZE textSize = {0, 0};
    if (len > 0) GetTextExtentPoint32W(mem, txt, len, &textSize);

    const int padX = 8;
    int overlayW = (len > 0 ? textSize.cx : 0) + padX * 2;
    int overlayH = barH;
    if (overlayW < 4) overlayW = 4;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = overlayW;
    bmi.bmiHeader.biHeight = -overlayH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        SelectObject(mem, oldFont);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(mem, dib);

    SetTextColor(mem, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    RECT rcText = {padX, 0, overlayW, overlayH};
    DrawTextW(mem, txt, len, &rcText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    BYTE tr = GetRValue(g_settings.color);
    BYTE tg = GetGValue(g_settings.color);
    BYTE tb = GetBValue(g_settings.color);
    BYTE* p = (BYTE*)bits;
    for (int i = 0, n = overlayW * overlayH; i < n; i++) {
        BYTE b = p[0], g = p[1], r = p[2];
        BYTE a = (BYTE)(((unsigned)r + g + b) / 3);
        p[0] = (BYTE)((unsigned)tb * a / 255);
        p[1] = (BYTE)((unsigned)tg * a / 255);
        p[2] = (BYTE)((unsigned)tr * a / 255);
        p[3] = a;
        p += 4;
    }

    POINT ptPos = {rcT.left + g_settings.offsetX, rcT.top};
    SIZE sz = {overlayW, overlayH};
    POINT ptZero = {0, 0};
    BLENDFUNCTION bf;
    memset(&bf, 0, sizeof(bf));
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_hwndOverlay, screen, &ptPos, &sz, mem, &ptZero, 0, &bf,
                        ULW_ALPHA);

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    SelectObject(mem, oldFont);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    ShowWindow(g_hwndOverlay, SW_SHOWNOACTIVATE);
    Wh_Log(L"UpdateOverlay: pos=%d,%d size=%dx%d barH=%d", ptPos.x, ptPos.y,
           overlayW, overlayH, barH);
}

LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == TIMER_ID) UpdateOverlay();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            UpdateOverlay();
            return 0;
        case WM_NCDESTROY:
            if (g_hwndOverlay == hWnd) g_hwndOverlay = nullptr;
            if (g_font) {
                DeleteObject(g_font);
                g_font = nullptr;
            }
            return 0;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void CreateOverlay() {
    if (g_hwndOverlay && IsWindow(g_hwndOverlay)) {
        UpdateOverlay();
        return;
    }
    CreateOverlayFont();
    g_hwndOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS, L"", WS_POPUP, 0, 0, 100, 30, g_hTaskbar, nullptr, nullptr,
        nullptr);
    Wh_Log(L"CreateWindowEx overlay -> %p (err=%lu)", g_hwndOverlay, GetLastError());
    if (g_hwndOverlay) {
        SetTimer(g_hwndOverlay, TIMER_ID, 1000, nullptr);
        UpdateOverlay();
    }
}

LRESULT CALLBACK TaskbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_TBLT_INIT:
            CreateOverlay();
            return 0;

        case WM_SIZE:
        case WM_DISPLAYCHANGE: {
            LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            UpdateOverlay();
            return r;
        }

        case WM_TBLT_APPLY:
            CreateOverlayFont();
            UpdateOverlay();
            return 0;

        case WM_TBLT_DESTROY:
            if (g_hwndOverlay && IsWindow(g_hwndOverlay)) {
                KillTimer(g_hwndOverlay, TIMER_ID);
                DestroyWindow(g_hwndOverlay);
            }
            g_hwndOverlay = nullptr;
            if (g_font) {
                DeleteObject(g_font);
                g_font = nullptr;
            }
            return 0;

        case WM_NCDESTROY:
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(hWnd,
                                                             TaskbarSubclassProc);
            break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static BOOL CALLBACK FindTaskbarEnumProc(HWND hWnd, LPARAM lParam) {
    DWORD dwProcessId;
    WCHAR className[32];
    if (GetWindowThreadProcessId(hWnd, &dwProcessId) &&
        dwProcessId == GetCurrentProcessId() &&
        GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
        _wcsicmp(className, L"Shell_TrayWnd") == 0) {
        *reinterpret_cast<HWND*>(lParam) = hWnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindCurrentProcessTaskbarWnd() {
    HWND hTaskbarWnd = nullptr;
    EnumWindows(FindTaskbarEnumProc, reinterpret_cast<LPARAM>(&hTaskbarWnd));
    return hTaskbarWnd;
}

void InitTaskbar(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;
    if (g_hTaskbar == hWnd) return;
    g_hTaskbar = hWnd;
    Wh_Log(L"InitTaskbar: hwnd=%p", hWnd);
    BOOL ok = WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd, TaskbarSubclassProc, 0);
    Wh_Log(L"SetWindowSubclassFromAnyThread -> %d", ok);
    SendMessageW(hWnd, WM_TBLT_INIT, 0, 0);
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD dwExStyle, LPCWSTR lpClassName,
                                        LPCWSTR lpWindowName, DWORD dwStyle,
                                        int X, int Y, int nWidth, int nHeight,
                                        HWND hWndParent, HMENU hMenu,
                                        HINSTANCE hInstance, LPVOID lpParam);
CreateWindowExW_t CreateWindowExW_orig;
HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle, LPCWSTR lpClassName,
                                LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y,
                                int nWidth, int nHeight, HWND hWndParent,
                                HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hWnd = CreateWindowExW_orig(dwExStyle, lpClassName, lpWindowName, dwStyle,
                                     X, Y, nWidth, nHeight, hWndParent, hMenu,
                                     hInstance, lpParam);
    BOOL isTextual = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;
    if (hWnd && isTextual && lpClassName &&
        _wcsicmp(lpClassName, L"Shell_TrayWnd") == 0) {
        InitTaskbar(hWnd);
    }
    return hWnd;
}

BOOL Wh_ModInit() {
    Wh_Log(L"Initializing " WH_MOD_ID L" v" WH_MOD_VERSION);
    LoadSettings();

    if (!g_classRegistered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = OVERLAY_CLASS;
        g_classRegistered = RegisterClassExW(&wc) != 0;
        Wh_Log(L"RegisterClass -> %d", g_classRegistered);
    }

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    void* target = (void*)GetProcAddress(hUser32, "CreateWindowExW");
    Wh_SetFunctionHook(target, (void*)CreateWindowExW_Hook,
                       (void**)&CreateWindowExW_orig);
    return TRUE;
}

void Wh_ModAfterInit() {
    WNDCLASS wc;
    if (!GetClassInfoW(GetModuleHandleW(nullptr), L"Shell_TrayWnd", &wc)) {
        Wh_Log(L"Shell_TrayWnd class not registered yet");
        return;
    }
    HWND h = FindCurrentProcessTaskbarWnd();
    Wh_Log(L"AfterInit: taskbar hwnd=%p", h);
    if (h) {
        InitTaskbar(h);
    } else {
        Wh_Log(L"Shell_TrayWnd window not found; waiting for creation");
    }
}

void Wh_ModUninit() {
    if (g_hTaskbar && IsWindow(g_hTaskbar)) {
        SendMessageW(g_hTaskbar, WM_TBLT_DESTROY, 0, 0);
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(g_hTaskbar,
                                                         TaskbarSubclassProc);
        g_hTaskbar = nullptr;
    }
    FreeSettings();
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    FreeSettings();
    LoadSettings();
    if (g_hTaskbar && IsWindow(g_hTaskbar)) {
        SendMessageW(g_hTaskbar, WM_TBLT_APPLY, 0, 0);
    }
    *bReload = FALSE;
    return TRUE;
}

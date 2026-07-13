// ==WindhawkMod==
// @id              taskbar-left-text
// @name            Taskbar Left Text
// @description     Shows custom text on the left side of the taskbar
// @version         0.7.2
// @author          ovenmakemeheat
// @github          https://github.com/ovenmakemeheat/windhawk-taskbar-ai-usage
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

An animated ASCII cat can track Codex, Claude Code, OpenCode, or all agents.
State detection uses process presence, CPU progress, and latest local session
activity without reading prompt contents.

In usage mode, edit the Template setting to arrange values from the bridge
file. For example:

`C5 {claudeBlockPct}% | CW {claudeWeeklyPct}% | X {codexWeeklyPct}% | O
{opencodeWeeklyPct}%`

Daily/monthly/session example:

`X today ${codexDailyTotalCost} | month ${codexMonthlyTotalCost} | session
${codexSessionTotalCost}`

Remove the `$` signs above if you don't want currency markers. Replace `codex`
with `claude` or `opencode`. Available field suffixes include `Period`,
`TotalCost`, `TotalTokens`, `InputTokens`, `OutputTokens`,
`CacheCreationTokens`, `CacheReadTokens`, `ReasoningOutputTokens`, `Models`,
`ModelBreakdowns`, `LastActivity`, and `LastActivityAgo`.

## Notes
- Targets `explorer.exe` (the shell that hosts the taskbar).
- Primary taskbar only (multi-monitor is not supported in this version).
- AutoUpdate launches the configured PowerShell updater hidden on startup and
  at the RefreshSeconds interval. It never waits on the Explorer UI thread and
  won't start another updater while one is running.
*/
// ==/WindhawkModReadme==

// clang-format off
// ==WindhawkModSettings==
/*
- Mode: usage
  $name: Display mode
  $description: Select usage to read from the bridge file, or text for static text
  $options:
    - usage: Usage from file
    - text: Static text
- UsageFile: ""
  $name: Usage file path
  $description: File shown in usage mode. Leave empty for %USERPROFILE%\.taskbar-usage.txt. Environment variables are expanded.
- AutoUpdate: true
  $name: Automatically update usage
  $description: Launch the updater script in a hidden process when the mod starts and at the configured interval
- UpdaterScript: '%USERPROFILE%\Workspace\Workspace\projects\agents-ctx\Scripts\Update-TaskbarUsage.ps1'
  $name: Updater script path
  $description: Absolute path or environment-variable path to Update-TaskbarUsage.ps1
- UpdaterArguments: ""
  $name: Updater arguments
  $description: Optional limit overrides passed to the updater script
- RefreshSeconds: 5
  $name: Refresh interval in seconds
- Template: "C5 {claudeBlockPct}% | CW {claudeWeeklyPct}% | X {codexWeeklyPct}% | O {opencodeWeeklyPct}%"
  $name: Usage template
  $description: Customize the label using placeholders from the usage bridge file
- PetEnabled: true
  $name: Show animated pet
- PetAgent: codex
  $name: Pet agent
  $description: Select which agent drives the pet, or agents for an aggregate state
  $options:
    - codex: Codex
    - claude: Claude Code
    - opencode: OpenCode
    - agents: All agents
- PetTemplate: "{pet} {agent}: {state}"
  $name: Pet template
  $description: First line content. Usage is always shown on the second line
- PetAnimationMs: 500
  $name: Pet animation speed in milliseconds
- PetStateOverride: auto
  $name: Pet state override
  $description: Force a state to test animation frames
  $options:
    - auto: Automatic detection
    - working: Working
    - idle: Idle
    - blocked: Blocked
- WorkingThresholdSeconds: 15
  $name: Working activity threshold in seconds
- BlockedThresholdSeconds: 60
  $name: Blocked inactivity threshold in seconds
- UsageRefreshSeconds: 60
  $name: Full ccusage refresh interval in seconds
- Text: "★ Taskbar"
  $name: Fallback text
  $description: Shown in text mode, or when the usage file is missing or empty
- OffsetX: 12
  $name: Offset from left
  $description: Horizontal offset from the taskbar's left edge, in pixels
- FontSize: 13
  $name: Font size
- Color: "#FFFFFF"
  $name: Text color
  $description: "Hex color, e.g. #FFFFFF or #FFAA00"
*/
// ==/WindhawkModSettings==
// clang-format on

#include <string.h>
#include <windhawk_utils.h>
#include <windows.h>
#include <string>

#define WM_TBLT_INIT (WM_USER + 0x100)
#define WM_TBLT_APPLY (WM_USER + 0x101)
#define WM_TBLT_DESTROY (WM_USER + 0x102)
#define TIMER_ID 4242
#define OVERLAY_CLASS L"TaskbarLeftTextOverlay"

struct Settings {
    PCWSTR text;
    PCWSTR templateText;
    PCWSTR updaterScript;
    PCWSTR updaterArguments;
    PCWSTR petAgent;
    PCWSTR petTemplate;
    PCWSTR petStateOverride;
    bool autoUpdate;
    bool petEnabled;
    int refreshSeconds;
    int petAnimationMs;
    int workingThresholdSeconds;
    int blockedThresholdSeconds;
    int usageRefreshSeconds;
    int offsetX;
    int fontSize;
    COLORREF color;
};
Settings g_settings;

HWND g_hTaskbar = nullptr;
HWND g_hwndOverlay = nullptr;
HFONT g_font = nullptr;
bool g_classRegistered = false;
HANDLE g_updaterProcess = nullptr;
ULONGLONG g_nextUpdateTick = 0;

std::wstring g_lastText;
RECT g_lastRect = {};

static int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

static COLORREF ParseColor(PCWSTR hex) {
    if (!hex)
        return RGB(255, 255, 255);
    if (*hex == L'#')
        hex++;
    unsigned long v = 0;
    int n = 0;
    while (*hex && n < 6) {
        int d = HexVal(*hex);
        if (d < 0)
            break;
        v = (v << 4) | (unsigned long)d;
        hex++;
        n++;
    }
    if (n == 0)
        return RGB(255, 255, 255);
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void LoadSettings() {
    g_settings.text = Wh_GetStringSetting(L"Text");
    g_settings.templateText = Wh_GetStringSetting(L"Template");
    g_settings.updaterScript = Wh_GetStringSetting(L"UpdaterScript");
    g_settings.updaterArguments = Wh_GetStringSetting(L"UpdaterArguments");
    g_settings.petAgent = Wh_GetStringSetting(L"PetAgent");
    g_settings.petTemplate = Wh_GetStringSetting(L"PetTemplate");
    g_settings.petStateOverride = Wh_GetStringSetting(L"PetStateOverride");
    g_settings.autoUpdate = Wh_GetIntSetting(L"AutoUpdate") != 0;
    g_settings.petEnabled = Wh_GetIntSetting(L"PetEnabled") != 0;
    g_settings.refreshSeconds = Wh_GetIntSetting(L"RefreshSeconds");
    g_settings.petAnimationMs = Wh_GetIntSetting(L"PetAnimationMs");
    g_settings.workingThresholdSeconds =
        Wh_GetIntSetting(L"WorkingThresholdSeconds");
    g_settings.blockedThresholdSeconds =
        Wh_GetIntSetting(L"BlockedThresholdSeconds");
    g_settings.usageRefreshSeconds = Wh_GetIntSetting(L"UsageRefreshSeconds");
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
    if (g_settings.templateText) {
        Wh_FreeStringSetting(g_settings.templateText);
        g_settings.templateText = nullptr;
    }
    if (g_settings.updaterScript) {
        Wh_FreeStringSetting(g_settings.updaterScript);
        g_settings.updaterScript = nullptr;
    }
    if (g_settings.updaterArguments) {
        Wh_FreeStringSetting(g_settings.updaterArguments);
        g_settings.updaterArguments = nullptr;
    }
    if (g_settings.petAgent) {
        Wh_FreeStringSetting(g_settings.petAgent);
        g_settings.petAgent = nullptr;
    }
    if (g_settings.petTemplate) {
        Wh_FreeStringSetting(g_settings.petTemplate);
        g_settings.petTemplate = nullptr;
    }
    if (g_settings.petStateOverride) {
        Wh_FreeStringSetting(g_settings.petStateOverride);
        g_settings.petStateOverride = nullptr;
    }
}

std::wstring UsageFilePath() {
    std::wstring result;
    PCWSTR s = Wh_GetStringSetting(L"UsageFile");
    if (s && *s) {
        WCHAR expanded[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsW(s, expanded, MAX_PATH);
        result = (n && n < MAX_PATH) ? expanded : s;
    } else {
        WCHAR profile[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
        if (n && n < MAX_PATH) {
            result = std::wstring(profile) + L"\\.taskbar-usage.txt";
        } else {
            result = L"C:\\Users\\Public\\.taskbar-usage.txt";
        }
    }
    Wh_FreeStringSetting(s);
    return result;
}

std::wstring ReadUsageFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L"";
    std::string s;
    CHAR buf[1024];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
        s.append(buf, rd);
        if (s.size() > 65536)
            break;
    }
    CloseHandle(h);

    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s.erase(0, 3);
    }
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                          s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos)
        return L"";
    s.erase(0, start);
    if (s.empty())
        return L"";

    int wlen =
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (wlen <= 0)
        return L"";
    std::wstring out(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), wlen);
    return out;
}

std::wstring Trim(std::wstring value) {
    size_t first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return L"";
    size_t last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

void ReplaceAll(std::wstring& text,
                const std::wstring& search,
                const std::wstring& replacement) {
    if (search.empty())
        return;
    size_t pos = 0;
    while ((pos = text.find(search, pos)) != std::wstring::npos) {
        text.replace(pos, search.size(), replacement);
        pos += replacement.size();
    }
}

std::wstring ApplyBridgePlaceholders(std::wstring result,
                                     const std::wstring& data) {
    if (data.find(L'=') == std::wstring::npos)
        return result;

    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find(L'\n', start);
        std::wstring line =
            data.substr(start, end == std::wstring::npos ? std::wstring::npos
                                                         : end - start);
        size_t equals = line.find(L'=');
        if (equals != std::wstring::npos) {
            std::wstring key = Trim(line.substr(0, equals));
            std::wstring value = Trim(line.substr(equals + 1));
            if (!key.empty()) {
                ReplaceAll(result, L"{" + key + L"}", value);
            }
        }
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return result;
}

std::wstring ApplyUsageTemplate(const std::wstring& data) {
    // Keep accepting the original one-line bridge format.
    if (data.find(L'=') == std::wstring::npos)
        return data;

    std::wstring result =
        g_settings.templateText ? g_settings.templateText : L"";
    return ApplyBridgePlaceholders(result, data);
}

std::wstring GetBridgeValue(const std::wstring& data,
                            const std::wstring& wantedKey) {
    size_t start = 0;
    while (start <= data.size()) {
        size_t end = data.find(L'\n', start);
        std::wstring line =
            data.substr(start, end == std::wstring::npos ? std::wstring::npos
                                                         : end - start);
        size_t equals = line.find(L'=');
        if (equals != std::wstring::npos &&
            Trim(line.substr(0, equals)) == wantedKey) {
            return Trim(line.substr(equals + 1));
        }
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return L"";
}

std::wstring StateText(const std::wstring& state) {
    if (state.empty())
        return L"Idle";
    std::wstring result = state;
    if (result[0] >= L'a' && result[0] <= L'z')
        result[0] -= L'a' - L'A';
    return result;
}

std::wstring PetFrame(const std::wstring& state) {
    PCWSTR frame0;
    PCWSTR frame1;
    if (state == L"working") {
        frame0 = L"(=^.^=)>";
        frame1 = L"(=^o^=)>";
    } else if (state == L"blocked") {
        frame0 = L"(=x.x=)!";
        frame1 = L"(=o.o=)?";
    } else {
        frame0 = L"(=-.-=)z";
        frame1 = L"(=-.-=)Z";
    }
    int animationMs =
        g_settings.petAnimationMs >= 100 ? g_settings.petAnimationMs : 500;
    return ((GetTickCount64() / animationMs) % 2) ? frame1 : frame0;
}

UINT OverlayTimerInterval() {
    if (!g_settings.petEnabled)
        return 1000;
    int animationMs = g_settings.petAnimationMs;
    if (animationMs < 100)
        animationMs = 100;
    if (animationMs > 5000)
        animationMs = 5000;
    return (UINT)animationMs;
}

std::wstring ResolvePetLine(const std::wstring& data) {
    if (!g_settings.petEnabled)
        return L"";

    std::wstring agent = g_settings.petAgent ? g_settings.petAgent : L"codex";
    if (agent != L"claude" && agent != L"codex" && agent != L"opencode" &&
        agent != L"agents") {
        agent = L"codex";
    }

    std::wstring stateKey =
        agent == L"agents" ? L"agentsState" : agent + L"State";
    std::wstring stateTextKey =
        agent == L"agents" ? L"agentsStateText" : agent + L"StateText";
    std::wstring state = GetBridgeValue(data, stateKey);
    std::wstring overrideState =
        g_settings.petStateOverride ? g_settings.petStateOverride : L"auto";
    if (overrideState == L"working" || overrideState == L"idle" ||
        overrideState == L"blocked") {
        state = overrideState;
    }
    if (state != L"working" && state != L"blocked")
        state = L"idle";

    std::wstring stateText = GetBridgeValue(data, stateTextKey);
    if (overrideState != L"auto" || stateText.empty())
        stateText = StateText(state);

    std::wstring agentText;
    if (agent == L"claude")
        agentText = L"Claude";
    else if (agent == L"opencode")
        agentText = L"OpenCode";
    else if (agent == L"agents")
        agentText = L"Agents";
    else
        agentText = L"Codex";

    std::wstring result = g_settings.petTemplate ? g_settings.petTemplate
                                                 : L"{pet} {agent}: {state}";
    ReplaceAll(result, L"{pet}", PetFrame(state));
    ReplaceAll(result, L"{agent}", agentText);
    ReplaceAll(result, L"{state}", stateText);

    // Apply bridge placeholders so they can be used on the pet line too
    result = ApplyBridgePlaceholders(result, data);

    // Fallback: cleanup {usage} token if user has the old template
    ReplaceAll(result, L" | {usage}", L"");
    ReplaceAll(result, L"{usage}", L"");
    return result;
}

std::wstring ResolveUsageLine(const std::wstring& data) {
    PCWSTR mode = Wh_GetStringSetting(L"Mode");
    bool usageMode = mode && _wcsicmp(mode, L"usage") == 0;
    Wh_FreeStringSetting(mode);
    std::wstring usage = g_settings.text ? g_settings.text : L"";
    if (usageMode && !data.empty())
        usage = ApplyUsageTemplate(data);
    return usage;
}

void ResolveDisplayLines(std::wstring& line1, std::wstring& line2) {
    std::wstring data = ReadUsageFile(UsageFilePath());
    line1 = ResolvePetLine(data);
    line2 = ResolveUsageLine(data);
}

std::wstring ExpandEnvironmentPath(PCWSTR value) {
    if (!value || !*value)
        return L"";
    WCHAR expanded[32768];
    DWORD n = ExpandEnvironmentStringsW(value, expanded, ARRAYSIZE(expanded));
    if (!n || n > ARRAYSIZE(expanded))
        return value;
    return expanded;
}

void CloseFinishedUpdater() {
    if (!g_updaterProcess)
        return;
    DWORD wait = WaitForSingleObject(g_updaterProcess, 0);
    if (wait == WAIT_TIMEOUT)
        return;
    DWORD exitCode = 0;
    GetExitCodeProcess(g_updaterProcess, &exitCode);
    Wh_Log(L"Usage updater finished with exit code %lu", exitCode);
    CloseHandle(g_updaterProcess);
    g_updaterProcess = nullptr;
}

void MaybeRunUpdater(bool force) {
    CloseFinishedUpdater();
    if (!g_settings.autoUpdate || g_updaterProcess)
        return;

    ULONGLONG now = GetTickCount64();
    if (!force && now < g_nextUpdateTick)
        return;

    int refreshSeconds =
        g_settings.refreshSeconds > 0 ? g_settings.refreshSeconds : 5;
    g_nextUpdateTick = now + (ULONGLONG)refreshSeconds * 1000;

    std::wstring script = ExpandEnvironmentPath(g_settings.updaterScript);
    DWORD attributes = script.empty() ? INVALID_FILE_ATTRIBUTES
                                      : GetFileAttributesW(script.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        Wh_Log(L"Updater script not found: %s", script.c_str());
        g_nextUpdateTick = now + 60 * 1000;
        return;
    }

    WCHAR windowsDirectory[MAX_PATH];
    if (!GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory))) {
        Wh_Log(L"GetWindowsDirectoryW failed: %lu", GetLastError());
        return;
    }
    std::wstring powershell =
        std::wstring(windowsDirectory) +
        L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command =
        L"\"" + powershell + L"\" -NoProfile -ExecutionPolicy Bypass -File \"" +
        script + L"\" -OutputFile \"" + UsageFilePath() + L"\"";
    int usageRefreshSeconds = g_settings.usageRefreshSeconds > 0
                                  ? g_settings.usageRefreshSeconds
                                  : 60;
    int workingThresholdSeconds = g_settings.workingThresholdSeconds > 0
                                      ? g_settings.workingThresholdSeconds
                                      : 15;
    int blockedThresholdSeconds =
        g_settings.blockedThresholdSeconds > workingThresholdSeconds
            ? g_settings.blockedThresholdSeconds
            : workingThresholdSeconds + 1;
    command += L" -FullRefreshSeconds " + std::to_wstring(usageRefreshSeconds) +
               L" -WorkingThresholdSeconds " +
               std::to_wstring(workingThresholdSeconds) +
               L" -BlockedThresholdSeconds " +
               std::to_wstring(blockedThresholdSeconds);
    if (g_settings.updaterArguments && *g_settings.updaterArguments) {
        command += L" ";
        command += g_settings.updaterArguments;
    }

    std::wstring workingDirectory;
    size_t slash = script.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        workingDirectory = script.substr(0, slash);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    BOOL created = CreateProcessW(
        powershell.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup,
        &process);
    if (!created) {
        Wh_Log(L"Failed to launch usage updater: %lu", GetLastError());
        g_nextUpdateTick = now + 60 * 1000;
        return;
    }

    CloseHandle(process.hThread);
    g_updaterProcess = process.hProcess;
    Wh_Log(L"Started usage updater, pid=%lu", process.dwProcessId);
}

void CreateOverlayFont() {
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    int size = g_settings.fontSize > 4 ? g_settings.fontSize : 13;
    g_font =
        CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void UpdateOverlay() {
    if (!g_hwndOverlay || !g_hTaskbar || !g_font)
        return;

    std::wstring line1, line2;
    ResolveDisplayLines(line1, line2);

    std::wstring lines[2];
    int lineCount = 0;
    if (!line1.empty())
        lines[lineCount++] = line1;
    if (!line2.empty())
        lines[lineCount++] = line2;

    RECT rcT;
    if (!GetWindowRect(g_hTaskbar, &rcT))
        return;
    int barH = rcT.bottom - rcT.top;
    if (barH <= 0)
        return;

    // Re-render only when the text or the taskbar rect actually changes.
    std::wstring disp = line1 + L'\n' + line2;
    if (disp == g_lastText && memcmp(&rcT, &g_lastRect, sizeof(RECT)) == 0) {
        return;
    }
    g_lastText = disp;
    g_lastRect = rcT;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ oldFont = SelectObject(mem, g_font);

    SIZE sizes[2] = {{0, 0}, {0, 0}};
    int maxWidth = 0;
    for (int i = 0; i < lineCount; i++) {
        if (!lines[i].empty())
            GetTextExtentPoint32W(mem, lines[i].c_str(), (int)lines[i].size(),
                                  &sizes[i]);
        if (sizes[i].cx > maxWidth)
            maxWidth = sizes[i].cx;
    }

    TEXTMETRICW tm;
    if (!GetTextMetricsW(mem, &tm))
        memset(&tm, 0, sizeof(tm));
    int lineH = tm.tmHeight > 0
                    ? tm.tmHeight
                    : (sizes[0].cy > 0 ? sizes[0].cy : g_settings.fontSize);

    const int padX = 8;
    int overlayW = maxWidth + padX * 2;
    int overlayH = barH;
    if (overlayW < 4)
        overlayW = 4;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = overlayW;
    bmi.bmiHeader.biHeight = -overlayH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib =
        CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        SelectObject(mem, oldFont);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(mem, dib);

    SetTextColor(mem, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    int blockH = lineCount * lineH;
    int startY = (overlayH - blockH) / 2;
    for (int i = 0; i < lineCount; i++) {
        RECT rcLine;
        rcLine.left = padX;
        rcLine.right = overlayW;
        rcLine.top = startY + i * lineH;
        rcLine.bottom = startY + (i + 1) * lineH;
        DrawTextW(mem, lines[i].c_str(), (int)lines[i].size(), &rcLine,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

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
    UpdateLayeredWindow(g_hwndOverlay, screen, &ptPos, &sz, mem, &ptZero, 0,
                        &bf, ULW_ALPHA);

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    SelectObject(mem, oldFont);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    ShowWindow(g_hwndOverlay, SW_SHOWNOACTIVATE);
}

LRESULT CALLBACK OverlayWndProc(HWND hWnd,
                                UINT uMsg,
                                WPARAM wParam,
                                LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == TIMER_ID) {
                MaybeRunUpdater(false);
                UpdateOverlay();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            UpdateOverlay();
            return 0;
        case WM_NCDESTROY:
            if (g_hwndOverlay == hWnd)
                g_hwndOverlay = nullptr;
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
        OVERLAY_CLASS, L"", WS_POPUP, 0, 0, 100, 30, g_hTaskbar, nullptr,
        nullptr, nullptr);
    Wh_Log(L"CreateWindowEx overlay -> %p (err=%lu)", g_hwndOverlay,
           GetLastError());
    if (g_hwndOverlay) {
        SetTimer(g_hwndOverlay, TIMER_ID, OverlayTimerInterval(), nullptr);
        MaybeRunUpdater(true);
        UpdateOverlay();
    }
}

LRESULT CALLBACK TaskbarSubclassProc(HWND hWnd,
                                     UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     DWORD_PTR dwRefData) {
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
            g_lastText.clear();  // force re-render even if text is unchanged
            if (g_hwndOverlay) {
                KillTimer(g_hwndOverlay, TIMER_ID);
                SetTimer(g_hwndOverlay, TIMER_ID, OverlayTimerInterval(),
                         nullptr);
            }
            g_nextUpdateTick = 0;
            MaybeRunUpdater(true);
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
            if (g_updaterProcess) {
                CloseHandle(g_updaterProcess);
                g_updaterProcess = nullptr;
            }
            return 0;

        case WM_NCDESTROY:
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                hWnd, TaskbarSubclassProc);
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
    if (!hWnd || !IsWindow(hWnd))
        return;
    if (g_hTaskbar == hWnd)
        return;
    g_hTaskbar = hWnd;
    Wh_Log(L"InitTaskbar: hwnd=%p", hWnd);
    BOOL ok = WindhawkUtils::SetWindowSubclassFromAnyThread(
        hWnd, TaskbarSubclassProc, 0);
    Wh_Log(L"SetWindowSubclassFromAnyThread -> %d", ok);
    SendMessageW(hWnd, WM_TBLT_INIT, 0, 0);
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD dwExStyle,
                                        LPCWSTR lpClassName,
                                        LPCWSTR lpWindowName,
                                        DWORD dwStyle,
                                        int X,
                                        int Y,
                                        int nWidth,
                                        int nHeight,
                                        HWND hWndParent,
                                        HMENU hMenu,
                                        HINSTANCE hInstance,
                                        LPVOID lpParam);
CreateWindowExW_t CreateWindowExW_orig;
HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle,
                                 LPCWSTR lpClassName,
                                 LPCWSTR lpWindowName,
                                 DWORD dwStyle,
                                 int X,
                                 int Y,
                                 int nWidth,
                                 int nHeight,
                                 HWND hWndParent,
                                 HMENU hMenu,
                                 HINSTANCE hInstance,
                                 LPVOID lpParam) {
    HWND hWnd = CreateWindowExW_orig(dwExStyle, lpClassName, lpWindowName,
                                     dwStyle, X, Y, nWidth, nHeight, hWndParent,
                                     hMenu, hInstance, lpParam);
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

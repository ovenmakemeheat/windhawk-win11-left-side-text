# Taskbar AI Usage for Windhawk

A Windhawk mod that displays Claude Code, Codex, and OpenCode usage on the
Windows taskbar. The label is rendered as a transparent, click-through overlay
that works with the native Windows 11 XAML taskbar.

Usage data comes from local agent logs through
[ccusage](https://github.com/ccusage/ccusage). A PowerShell updater writes a
small key-value bridge file, and the mod formats those values using a
user-configurable template.

## Features

- Native Windows 11 taskbar overlay with per-pixel alpha rendering.
- Claude Code 5-hour block and weekly estimates.
- Claude Code, Codex, and OpenCode daily, monthly, and latest-session reports.
- Template-based display similar to a configurable clock extension.
- Automatic hidden refresh process with overlap protection.
- Configurable text, position, font size, color, updater arguments, and interval.
- Plain-text bridge compatibility for custom scripts.
- Local formatting, syntax checking, and editor integration.

## Requirements

Runtime requirements:

- Windows 10 or Windows 11.
- [Windhawk](https://windhawk.net/) with developer mode enabled.
- Windows PowerShell 5.1 (included with Windows).
- Node.js with `npx`, or a globally installed `ccusage` command.
- Local Claude Code, Codex, or OpenCode logs for the agents being displayed.

Optional development requirements:

- GNU Make.
- Native LLVM `clang-format`.
- MSYS2 ucrt64/mingw64 `g++`, or a usable mingw Clang toolchain.

The updater first looks for a global `ccusage`. If unavailable, it uses `npx`.
It also checks these locations when Explorer has an older PATH:

```text
%ProgramFiles%\Volta\npx.exe
%APPDATA%\npm\npx.cmd
```

## Quick Start

### 1. Check The Source

```powershell
make check
```

This performs a local C++23 syntax check. Windhawk still performs the final,
authoritative compilation with its bundled Clang.

### 2. Install The Mod

1. Open Windhawk and enable developer mode.
2. Choose **Create new mod**.
3. Paste the contents of `mods/taskbar-left-text.wh.cpp` into the Windhawk editor.
4. Save and enable the mod.
5. Enable detailed logging while testing.

The mod targets `explorer.exe`. Restart Windows Explorer if Windhawk does not
inject it into the existing process immediately.

### 3. Check The Updater

```powershell
make usage-dry
```

This queries ccusage and prints the data without changing the bridge file.

Write the bridge file once:

```powershell
make usage
```

The default bridge path is:

```text
%USERPROFILE%\.taskbar-usage.txt
```

### 4. Enable Automatic Updates

Automatic updates are enabled by default. Confirm these Windhawk settings:

```text
Mode: usage
AutoUpdate: true
RefreshSeconds: 5
```

The default updater path matches this repository location:

```text
%USERPROFILE%\Workspace\Workspace\projects\agents-ctx\Scripts\Update-TaskbarUsage.ps1
```

Update `UpdaterScript` if the repository is moved.

## Configuration

| Setting | Default | Description |
|---|---|---|
| `Mode` | `usage` | Reads and formats the bridge file. Use `text` for a static label. |
| `UsageFile` | Empty | Bridge path. Empty means `%USERPROFILE%\.taskbar-usage.txt`. Environment variables are expanded. |
| `AutoUpdate` | `true` | Runs the PowerShell updater automatically. |
| `UpdaterScript` | Repository script path | Path to `Update-TaskbarUsage.ps1`. Environment variables are expanded. |
| `UpdaterArguments` | Empty | Optional PowerShell arguments, including estimated limit overrides. |
| `RefreshSeconds` | `5` | Minimum delay between updater launches. |
| `Template` | Compact usage label | Controls the rendered text using placeholders. |
| `Text` | `★ Taskbar` | Static-mode text and fallback when usage data is unavailable. |
| `OffsetX` | `12` | Horizontal offset from the left edge of the primary taskbar. |
| `FontSize` | `13` | Segoe UI font size. |
| `Color` | `#FFFFFF` | Text color in `#RRGGBB` format. |

The updater is asynchronous. Explorer never waits for ccusage to finish. If an
update takes longer than five seconds, another updater is not started until the
current process exits.

## Usage Estimates

ccusage calculates cost and token usage from local logs. It does not expose the
authoritative subscription quota percentage maintained by Claude or Codex
servers.

The percentage placeholders divide local cost by configurable USD limits:

```text
Claude 5-hour block:  $25
Claude weekly:        $100
Codex weekly:         $50
OpenCode weekly:      $50
```

Override these values through the Windhawk `UpdaterArguments` setting:

```text
-ClaudeBlockLimitUSD 25 -ClaudeWeeklyLimitUSD 100 -CodexWeeklyLimitUSD 50 -OpenCodeWeeklyLimitUSD 50
```

The same overrides can be used manually:

```powershell
make usage USAGE_ARGS="-ClaudeBlockLimitUSD 25 -ClaudeWeeklyLimitUSD 100 -CodexWeeklyLimitUSD 50 -OpenCodeWeeklyLimitUSD 50"
```

Claude's 5-hour percentage is `0` when no block is active.

## Templates

The Windhawk `Template` setting controls presentation. The updater only writes
structured values, so changing the template does not require changing the
PowerShell script.

Default template:

```text
C5 {claudeBlockPct}% | CW {claudeWeeklyPct}% | X {codexWeeklyPct}% | O {opencodeWeeklyPct}%
```

### Common Fields

| Placeholder | Description |
|---|---|
| `{claudeBlockPct}` | Estimated active Claude 5-hour block percentage. |
| `{claudeWeeklyPct}` | Estimated Claude current-week percentage. |
| `{codexWeeklyPct}` | Estimated Codex current-week percentage. |
| `{opencodeWeeklyPct}` | Estimated OpenCode current-week percentage. |
| `{claudeBlockCost}` | Active Claude block cost. |
| `{claudeWeeklyCost}` | Claude current-week cost. |
| `{codexWeeklyCost}` | Codex current-week cost. |
| `{opencodeWeeklyCost}` | OpenCode current-week cost. |
| `{updatedAt}` | Bridge update time in `HH:mm` format. |

### Report Fields

Daily, monthly, and latest-session values use this naming convention:

```text
{<agent><period><field>}
```

Agents:

```text
claude
codex
opencode
```

Periods:

```text
Daily
Monthly
Session
```

Fields:

| Field | Description |
|---|---|
| `Period` | Date, month, or session identifier. |
| `TotalCost` | Estimated total cost in USD. |
| `TotalTokens` | Total tokens. |
| `InputTokens` | Input tokens. |
| `OutputTokens` | Output tokens. |
| `CacheCreationTokens` | Cache creation input tokens. |
| `CacheReadTokens` | Cache read input tokens. |
| `ReasoningOutputTokens` | Codex reasoning tokens when available. |
| `Models` | Comma-separated model names. |
| `ModelBreakdowns` | Comma-separated `model:cost` values. |
| `LastActivity` | Latest session activity in local time. |

If an agent has no activity today, its daily fields contain zeroes and `-`
placeholders. Monthly fields describe the current month. Session fields describe
the most recently active session in the current month.

### Template Examples

Compact all-agent usage:

```text
C5 {claudeBlockPct}% | CW {claudeWeeklyPct}% | X {codexWeeklyPct}% | O {opencodeWeeklyPct}%
```

Costs and update time:

```text
C ${claudeWeeklyCost} | X ${codexWeeklyCost} | O ${opencodeWeeklyCost} @ {updatedAt}
```

Codex summary:

```text
Codex W:{codexWeeklyPct}% D:${codexDailyTotalCost} M:${codexMonthlyTotalCost} S:${codexSessionTotalCost} {codexSessionModels}
```

Full Codex report:

```text
CODEX | Week {codexWeeklyPct}% (${codexWeeklyCost}) | Daily [{codexDailyPeriod}] ${codexDailyTotalCost} T:{codexDailyTotalTokens} I:{codexDailyInputTokens} O:{codexDailyOutputTokens} Cache:{codexDailyCacheReadTokens} Reason:{codexDailyReasoningOutputTokens} Models:{codexDailyModels} | Monthly [{codexMonthlyPeriod}] ${codexMonthlyTotalCost} T:{codexMonthlyTotalTokens} I:{codexMonthlyInputTokens} O:{codexMonthlyOutputTokens} Cache:{codexMonthlyCacheReadTokens} Models:{codexMonthlyModels} | Session ${codexSessionTotalCost} T:{codexSessionTotalTokens} I:{codexSessionInputTokens} O:{codexSessionOutputTokens} Cache:{codexSessionCacheReadTokens} Reason:{codexSessionReasoningOutputTokens} Models:{codexSessionModels} Last:{codexSessionLastActivity}
```

OpenCode token summary:

```text
OpenCode {opencodeDailyTotalTokens} today | {opencodeMonthlyTotalTokens} month | {opencodeSessionModels}
```

Long templates automatically increase the overlay width and can overlap taskbar
icons. Use shorter labels or increase `OffsetX` as needed.

## Bridge Format

The updater writes UTF-8 `key=value` lines:

```text
claudeBlockPct=0
claudeWeeklyPct=53
codexWeeklyPct=29
opencodeWeeklyPct=59
codexMonthlyTotalCost=14.70
codexSessionModels=gpt-5.6-sol
updatedAt=01:43
```

The mod replaces `{key}` in the template with the corresponding value. A custom
script may write the same format. For backward compatibility, a bridge file
without `=` is displayed directly as plain text.

## Commands

| Command | Description |
|---|---|
| `make` | Show available targets. |
| `make check` | Syntax-check every mod. |
| `make check MOD=mods/taskbar-left-text.wh.cpp` | Check one mod. |
| `make format` | Format mod source. |
| `make format-check` | Verify formatting without changes. |
| `make usage-dry` | Preview ccusage output without writing. |
| `make usage` | Update the bridge once. |
| `make usage-watch` | Foreground refresh every five seconds until Ctrl+C. |
| `make clean` | Remove build artifacts. |

Automatic updates make `usage-watch` unnecessary during normal use. It remains
useful for debugging with `AutoUpdate` disabled.

## Architecture

The runtime flow is:

```text
ccusage local logs
       |
Update-TaskbarUsage.ps1
       |
%USERPROFILE%\.taskbar-usage.txt
       |
taskbar-left-text.wh.cpp
       |
Windows taskbar overlay
```

The mod:

- Finds and subclasses `Shell_TrayWnd` in `explorer.exe`.
- Creates a layered, click-through popup owned by the taskbar.
- Draws premultiplied alpha text over the Windows 11 XAML taskbar.
- Reads the bridge every second and redraws only when text or taskbar position changes.
- Starts hidden PowerShell asynchronously according to `RefreshSeconds`.
- Tracks the updater process handle and prevents overlapping launches.
- Destroys its window and removes the subclass on unload.

## Project Layout

```text
.
|-- mods/
|   `-- taskbar-left-text.wh.cpp
|-- Scripts/
|   |-- Test-ModCompile.ps1
|   `-- Update-TaskbarUsage.ps1
|-- .vscode/
|   |-- c_cpp_properties.json
|   `-- windhawk_headers_1.7.3/
|-- .clang-format
|-- compile_flags.txt
|-- Makefile
`-- README.md
```

## Troubleshooting

### No Text Appears

- Confirm the Windhawk log says `Initializing ... v0.6.1` or newer.
- Confirm `AfterInit: taskbar hwnd=...` and `CreateWindowEx overlay -> ...` appear.
- Restart Windows Explorer after enabling the mod.
- Try `Mode: text` to verify rendering independently of ccusage.
- Increase `OffsetX` if the label is hidden under Start or Widgets.

### Fallback Text Appears

- Run `make usage-dry` and inspect warnings.
- Run `make usage` and verify `%USERPROFILE%\.taskbar-usage.txt` exists.
- Confirm `UsageFile` matches the updater's `-OutputFile` path.
- Confirm the Windhawk `Mode` setting is `usage`.

### Automatic Updates Fail

- Confirm `UpdaterScript` points to an existing file.
- Check Windhawk logs for `Started usage updater` and its exit code.
- Verify `ccusage` or `npx` is available.
- Use `make usage-dry` to expose verbose parsing errors.
- Install ccusage globally to reduce startup overhead:

```powershell
npm install --global ccusage
```

### Values Are Zero

- Daily values are zero when that agent has no activity today.
- Claude 5-hour usage is zero when no block is active.
- The updater only reports data found in local agent logs.
- Use `-ShowRaw` when running the updater to inspect ccusage JSON.

### Updates Are Slower Than Five Seconds

ccusage may take longer than five seconds, especially when invoked through
`npx`. The mod intentionally prevents overlapping processes. Therefore, the
effective refresh interval is at least the updater's execution time.

### Windhawk Reports YAML Errors

The settings block is protected with `clang-format off/on`. Keep these markers
when editing. YAML descriptions containing `#` or embedded punctuation should
remain quoted.

## Development

The local checker injects offline Windhawk API stubs and performs a C++23
`-fsyntax-only` pass. It probes compiler candidates and selects one that can
actually compile a Windows/Windhawk translation unit.

```powershell
make format
make check
```

The local g++ fallback cannot detect every Clang-specific issue. In particular,
use named `CALLBACK` functions rather than lambdas for Win32 callback types such
as `WNDENUMPROC`.

Useful references:

- [Creating a new Windhawk mod](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod)
- [Debugging Windhawk mods](https://github.com/ramensoftware/windhawk/wiki/Debugging-the-mods)
- [Official Windhawk mods](https://github.com/ramensoftware/windhawk-mods)
- [ccusage documentation](https://ccusage.com/)

## Limitations

- Only the primary taskbar is supported.
- Percentages are configurable local cost estimates, not server quota values.
- Daily/monthly/session values are based on local logs and ccusage pricing.
- Very long templates can overlap taskbar controls.
- Automatic refresh depends on the configured PowerShell script path.
- Closing the updater process handle on mod unload does not terminate an updater
  that was already launched; it may finish writing the bridge file normally.

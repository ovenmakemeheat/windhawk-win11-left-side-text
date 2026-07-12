<#
.SYNOPSIS
    Updates the text displayed by the Taskbar Left Text Windhawk mod.

.DESCRIPTION
    Reads local Claude Code and Codex logs through ccusage, estimates usage as
    a percentage of configurable USD limits, and writes a short UTF-8 text file
    that the mod watches.

    These percentages are local cost estimates, not authoritative account
    quota values. Claude and Codex don't persist the server-side quota percent
    locally, so set the limits below to values appropriate for your plan.

.EXAMPLE
    .\Scripts\Update-TaskbarUsage.ps1 -DryRun -Verbose

.EXAMPLE
    .\Scripts\Update-TaskbarUsage.ps1 `
        -ClaudeBlockLimitUSD 25 `
        -ClaudeWeeklyLimitUSD 100 `
        -CodexWeeklyLimitUSD 50
#>
[CmdletBinding()]
param(
    [string]$OutputFile = "$env:USERPROFILE\.taskbar-usage.txt",

    # Change these estimates to match your plan and preferred denominator.
    [double]$ClaudeBlockLimitUSD = 25.0,
    [double]$ClaudeWeeklyLimitUSD = 100.0,
    [double]$CodexWeeklyLimitUSD = 50.0,
    [double]$OpenCodeWeeklyLimitUSD = 50.0,

    # Available placeholders are documented below where the text is built.
    [string]$Format = 'C 5h:{claudeBlockPct}% w:{claudeWeeklyPct}% | X:{codexWeeklyPct}% | O:{opencodeWeeklyPct}%',

    [switch]$DryRun,
    [switch]$ShowRaw
)

$ErrorActionPreference = 'Continue'

function Invoke-CcusageJson {
    param([string[]]$CcArgs)

    $globalCcusage = Get-Command ccusage -ErrorAction SilentlyContinue
    if ($globalCcusage) {
        $output = & $globalCcusage.Source @CcArgs --json 2>&1 |
            ForEach-Object { $_.ToString() }
    } else {
        $output = & npx -y ccusage@latest @CcArgs --json 2>&1 |
            ForEach-Object { $_.ToString() }
    }
    $exitCode = $LASTEXITCODE
    $text = $output -join "`n"
    if ($exitCode -ne 0) {
        throw "ccusage $($CcArgs -join ' ') failed (exit $exitCode):`n$text"
    }
    if ($ShowRaw) {
        Write-Host "`n--- ccusage $($CcArgs -join ' ') --json ---"
        Write-Host $text
    }
    return $text | ConvertFrom-Json
}

function ConvertTo-Percent {
    param([double]$Value, [double]$Limit)

    if ($Limit -le 0) { return '?' }
    $pct = [math]::Round(($Value / $Limit) * 100)
    return [math]::Min(100, [math]::Max(0, $pct)).ToString('0')
}

$claudeBlockCost = 0.0
$claudeWeeklyCost = 0.0
$codexWeeklyCost = 0.0
$opencodeWeeklyCost = 0.0

try {
    Write-Verbose 'Reading Claude 5-hour blocks...'
    $blockReport = Invoke-CcusageJson @('blocks')
    $activeBlock = @($blockReport.blocks |
        Where-Object { $_.isActive -and -not $_.isGap } |
        Sort-Object { [datetime]$_.startTime })[-1]
    if ($activeBlock) {
        $claudeBlockCost = [double]$activeBlock.costUSD
    }
} catch {
    Write-Warning "Claude block usage unavailable: $_"
}

try {
    Write-Verbose 'Reading Claude weekly usage...'
    $weeklyReport = Invoke-CcusageJson @('claude', 'weekly')
    $currentClaudeWeek = @($weeklyReport.weekly |
        Sort-Object { [datetime]$_.week })[-1]
    if ($currentClaudeWeek) {
        $claudeWeeklyCost = [double]$currentClaudeWeek.totalCost
    }
} catch {
    Write-Warning "Claude weekly usage unavailable: $_"
}

try {
    Write-Verbose 'Reading Codex daily usage...'
    $codexReport = Invoke-CcusageJson @('codex', 'daily')
    $today = (Get-Date).Date
    $weekStart = $today.AddDays(-[int]$today.DayOfWeek)
    $currentCodexDays = @($codexReport.daily | Where-Object {
        $date = [datetime]::ParseExact($_.date, 'yyyy-MM-dd', $null)
        $date -ge $weekStart -and $date -le $today
    })
    if ($currentCodexDays.Count -gt 0) {
        $codexWeeklyCost = [double](
            $currentCodexDays | Measure-Object -Property costUSD -Sum
        ).Sum
    }
} catch {
    Write-Warning "Codex weekly usage unavailable: $_"
}

try {
    Write-Verbose 'Reading OpenCode weekly usage...'
    $opencodeReport = Invoke-CcusageJson @('opencode', 'weekly')
    $currentOpenCodeWeek = @($opencodeReport.weekly |
        Sort-Object { [datetime]$_.week })[-1]
    if ($currentOpenCodeWeek) {
        $opencodeWeeklyCost = [double]$currentOpenCodeWeek.totalCost
    }
} catch {
    Write-Warning "OpenCode weekly usage unavailable: $_"
}

$values = @{
    claudeBlockPct = ConvertTo-Percent $claudeBlockCost $ClaudeBlockLimitUSD
    claudeWeeklyPct = ConvertTo-Percent $claudeWeeklyCost $ClaudeWeeklyLimitUSD
    codexWeeklyPct = ConvertTo-Percent $codexWeeklyCost $CodexWeeklyLimitUSD
    opencodeWeeklyPct = ConvertTo-Percent $opencodeWeeklyCost $OpenCodeWeeklyLimitUSD
    claudeBlockCost = $claudeBlockCost.ToString('0.00')
    claudeWeeklyCost = $claudeWeeklyCost.ToString('0.00')
    codexWeeklyCost = $codexWeeklyCost.ToString('0.00')
    opencodeWeeklyCost = $opencodeWeeklyCost.ToString('0.00')
}

$text = $Format
foreach ($key in $values.Keys) {
    $text = $text.Replace("{$key}", [string]$values[$key])
}

Write-Verbose (
    'Costs: Claude block=${0:0.00}, Claude week=${1:0.00}, Codex week=${2:0.00}, OpenCode week=${3:0.00}' -f
    $claudeBlockCost, $claudeWeeklyCost, $codexWeeklyCost, $opencodeWeeklyCost
)

if ($DryRun) {
    Write-Host "Would write to: $OutputFile" -ForegroundColor Yellow
    Write-Host "Content: $text" -ForegroundColor Yellow
    return
}

$parent = Split-Path $OutputFile -Parent
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
[System.IO.File]::WriteAllText(
    $OutputFile,
    $text,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "Updated $OutputFile" -ForegroundColor Green
Write-Host "Content: $text"

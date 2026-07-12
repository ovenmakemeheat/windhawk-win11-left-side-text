<#
.SYNOPSIS
    Updates usage data consumed by the Taskbar Left Text Windhawk mod.

.DESCRIPTION
    Reads local Claude Code, Codex, and OpenCode logs through ccusage. It writes
    a UTF-8 key=value bridge file containing:

    - Claude 5-hour and all-agent weekly estimates.
    - Current-day, current-month, and latest-session fields for each agent.
    - Costs, token counts, model lists, periods, and last-activity timestamps.

    Percentage values are local cost estimates, not authoritative account quota
    values. Set the USD limits below to values appropriate for your plan.

.EXAMPLE
    .\Scripts\Update-TaskbarUsage.ps1 -DryRun -Verbose

.EXAMPLE
    .\Scripts\Update-TaskbarUsage.ps1 `
        -ClaudeBlockLimitUSD 25 `
        -ClaudeWeeklyLimitUSD 100 `
        -CodexWeeklyLimitUSD 50 `
        -OpenCodeWeeklyLimitUSD 50
#>
[CmdletBinding()]
param(
    [string]$OutputFile = "$env:USERPROFILE\.taskbar-usage.txt",

    [double]$ClaudeBlockLimitUSD = 25.0,
    [double]$ClaudeWeeklyLimitUSD = 100.0,
    [double]$CodexWeeklyLimitUSD = 50.0,
    [double]$OpenCodeWeeklyLimitUSD = 50.0,

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
        $npx = Get-Command npx -ErrorAction SilentlyContinue
        if (-not $npx) {
            $npxCandidates = @(
                "$env:ProgramFiles\Volta\npx.exe",
                "$env:APPDATA\npm\npx.cmd"
            )
            $npxPath = $npxCandidates |
                Where-Object { Test-Path -LiteralPath $_ } |
                Select-Object -First 1
            if (-not $npxPath) {
                throw 'Neither ccusage nor npx was found. Install ccusage globally or configure Explorer PATH.'
            }
        } else {
            $npxPath = $npx.Source
        }
        $output = & $npxPath -y ccusage@latest @CcArgs --json 2>&1 |
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

function Add-ReportEntry {
    param(
        [System.Collections.IDictionary]$Values,
        [string]$Prefix,
        $Entry
    )

    $defaults = [ordered]@{
        Period = '-'
        TotalCost = '0.00'
        TotalTokens = '0'
        InputTokens = '0'
        OutputTokens = '0'
        CacheCreationTokens = '0'
        CacheReadTokens = '0'
        ReasoningOutputTokens = '0'
        Models = '-'
        ModelBreakdowns = '-'
        LastActivity = '-'
    }
    foreach ($field in $defaults.Keys) {
        $Values["$Prefix$field"] = $defaults[$field]
    }
    if ($null -eq $Entry) { return }

    $period = if ($Entry.period) { $Entry.period } else { $Entry.date }
    if ($period) { $Values["${Prefix}Period"] = [string]$period }

    $cost = if ($null -ne $Entry.totalCost) {
        [double]$Entry.totalCost
    } elseif ($null -ne $Entry.costUSD) {
        [double]$Entry.costUSD
    } else {
        0.0
    }
    $Values["${Prefix}TotalCost"] = $cost.ToString('0.00')

    foreach ($field in @(
        'totalTokens',
        'inputTokens',
        'outputTokens',
        'cacheCreationTokens',
        'cacheReadTokens',
        'reasoningOutputTokens'
    )) {
        if ($null -ne $Entry.$field) {
            $suffix = $field.Substring(0, 1).ToUpper() + $field.Substring(1)
            $Values["$Prefix$suffix"] = [string]$Entry.$field
        }
    }

    if ($Entry.metadata) {
        if ($null -ne $Entry.metadata.reasoningOutputTokens) {
            $Values["${Prefix}ReasoningOutputTokens"] =
                [string]$Entry.metadata.reasoningOutputTokens
        }
        if ($Entry.metadata.lastActivity) {
            $lastActivity = ([datetime]$Entry.metadata.lastActivity).ToLocalTime()
            $Values["${Prefix}LastActivity"] =
                $lastActivity.ToString('yyyy-MM-dd HH:mm')
        }
    }

    if ($Entry.modelsUsed) {
        $Values["${Prefix}Models"] = @($Entry.modelsUsed) -join ','
    }
    if ($Entry.modelBreakdowns) {
        $breakdowns = foreach ($model in @($Entry.modelBreakdowns)) {
            $modelCost = [double]$model.cost
            "$($model.modelName):$($modelCost.ToString('0.00'))"
        }
        $Values["${Prefix}ModelBreakdowns"] = $breakdowns -join ','
    }
}

function Expand-AgentRows {
    param($Rows)

    foreach ($row in @($Rows)) {
        if ($row.agents) {
            foreach ($agentRow in @($row.agents)) {
                $agentRow | Add-Member -NotePropertyName period `
                    -NotePropertyValue $row.period -Force
                $agentRow
            }
        } elseif ($row.agent -and $row.agent -ne 'all') {
            $row
        }
    }
}

$claudeBlockCost = 0.0
$activityReport = $null

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
    $monthStart = Get-Date -Day 1 -Format 'yyyy-MM-dd'
    Write-Verbose "Reading unified reports since $monthStart..."
    $activityReport = Invoke-CcusageJson @(
        'daily',
        '--sections', 'daily,monthly,session',
        '--by-agent',
        '--since', $monthStart
    )
} catch {
    Write-Warning "Unified usage reports unavailable: $_"
}

$today = (Get-Date).Date
$todayText = $today.ToString('yyyy-MM-dd')
$monthText = $today.ToString('yyyy-MM')
$weekStart = $today.AddDays(-[int]$today.DayOfWeek)

$agents = @('claude', 'codex', 'opencode')
$dailyByAgent = @(Expand-AgentRows $activityReport.daily)
$monthlyByAgent = @(Expand-AgentRows $activityReport.monthly)
$sessionByAgent = @($activityReport.session)
$weeklyCosts = @{
    claude = 0.0
    codex = 0.0
    opencode = 0.0
}

$values = [ordered]@{}
foreach ($agent in $agents) {
    $dailyEntries = @($dailyByAgent | Where-Object { $_.agent -eq $agent })

    foreach ($entry in $dailyEntries) {
        $date = [datetime]::ParseExact($entry.period, 'yyyy-MM-dd', $null)
        if ($date -ge $weekStart -and $date -le $today) {
            $weeklyCosts[$agent] += [double]$entry.totalCost
        }
    }

    $dailyEntry = @($dailyEntries |
        Where-Object { $_.period -eq $todayText })[-1]
    $monthlyEntry = @($monthlyByAgent |
        Where-Object { $_.agent -eq $agent -and $_.period -eq $monthText })[-1]
    $sessionEntry = @($sessionByAgent |
        Where-Object { $_.agent -eq $agent } |
        Sort-Object { [datetime]$_.metadata.lastActivity })[-1]

    Add-ReportEntry $values "${agent}Daily" $dailyEntry
    Add-ReportEntry $values "${agent}Monthly" $monthlyEntry
    Add-ReportEntry $values "${agent}Session" $sessionEntry
}

$values['claudeBlockPct'] =
    ConvertTo-Percent $claudeBlockCost $ClaudeBlockLimitUSD
$values['claudeWeeklyPct'] =
    ConvertTo-Percent $weeklyCosts.claude $ClaudeWeeklyLimitUSD
$values['codexWeeklyPct'] =
    ConvertTo-Percent $weeklyCosts.codex $CodexWeeklyLimitUSD
$values['opencodeWeeklyPct'] =
    ConvertTo-Percent $weeklyCosts.opencode $OpenCodeWeeklyLimitUSD
$values['claudeBlockCost'] = $claudeBlockCost.ToString('0.00')
$values['claudeWeeklyCost'] = $weeklyCosts.claude.ToString('0.00')
$values['codexWeeklyCost'] = $weeklyCosts.codex.ToString('0.00')
$values['opencodeWeeklyCost'] = $weeklyCosts.opencode.ToString('0.00')
$values['updatedAt'] = (Get-Date).ToString('HH:mm')

$payload = ($values.GetEnumerator() | ForEach-Object {
    "$($_.Key)=$($_.Value)"
}) -join "`n"

Write-Verbose (
    'Costs: Claude block=${0:0.00}, Claude week=${1:0.00}, Codex week=${2:0.00}, OpenCode week=${3:0.00}' -f
    $claudeBlockCost,
    $weeklyCosts.claude,
    $weeklyCosts.codex,
    $weeklyCosts.opencode
)

if ($DryRun) {
    Write-Host "Would write to: $OutputFile" -ForegroundColor Yellow
    Write-Host "Data:`n$payload" -ForegroundColor Yellow
    return
}

$parent = Split-Path $OutputFile -Parent
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
[System.IO.File]::WriteAllText(
    $OutputFile,
    $payload,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "Updated $OutputFile" -ForegroundColor Green
Write-Host "Data:`n$payload"

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

    [int]$FullRefreshSeconds = 60,
    [int]$WorkingThresholdSeconds = 15,
    [int]$BlockedThresholdSeconds = 60,

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
        LastActivityEpoch = '0'
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
            $Values["${Prefix}LastActivityEpoch"] =
                ([DateTimeOffset]$lastActivity).ToUnixTimeSeconds().ToString()
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

function Read-BridgeValues {
    param([string]$Path)

    $result = [ordered]@{}
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $result
    }
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $equals = $line.IndexOf('=')
        if ($equals -le 0) { continue }
        $key = $line.Substring(0, $equals).Trim()
        $value = $line.Substring($equals + 1).Trim()
        if ($key) { $result[$key] = $value }
    }
    return $result
}

function Get-AgentProcesses {
    $result = @{
        claude = @{ Running = $false; CpuTicks = [uint64]0 }
        codex = @{ Running = $false; CpuTicks = [uint64]0 }
        opencode = @{ Running = $false; CpuTicks = [uint64]0 }
    }

    try {
        $processes = Get-CimInstance Win32_Process -Property Name, CommandLine, KernelModeTime, UserModeTime
        foreach ($process in $processes) {
            $name = ([string]$process.Name).ToLowerInvariant()
            $command = ([string]$process.CommandLine).ToLowerInvariant()
            $text = "$name $command"
            $agent = $null

            if ($name -eq 'claude.exe' -or
                $text -match '@anthropic-ai[\\/]claude-code' -or
                $text -match '(^|[\\/\s])claude(?:\.exe|\.cmd|\.js)?(?:\s|$)') {
                $agent = 'claude'
            } elseif ($name -eq 'codex.exe' -or
                      $text -match '@openai[\\/]codex' -or
                      $text -match '(^|[\\/\s])codex(?:\.exe|\.cmd|\.js)?(?:\s|$)') {
                $agent = 'codex'
            } elseif ($name -eq 'opencode.exe' -or
                      $text -match '(^|[\\/\s])opencode(?:\.exe|\.cmd|\.js)?(?:\s|$)') {
                $agent = 'opencode'
            }

            if ($agent) {
                $result[$agent].Running = $true
                $kernel = if ($null -ne $process.KernelModeTime) {
                    [uint64]$process.KernelModeTime
                } else { [uint64]0 }
                $user = if ($null -ne $process.UserModeTime) {
                    [uint64]$process.UserModeTime
                } else { [uint64]0 }
                $result[$agent].CpuTicks += $kernel + $user
            }
        }
    } catch {
        Write-Warning "Process state detection failed: $_"
        foreach ($agent in @('claude', 'codex', 'opencode')) {
            if (Get-Process -Name $agent -ErrorAction SilentlyContinue) {
                $result[$agent].Running = $true
            }
        }
    }

    return $result
}

function Add-AgentStates {
    param(
        [System.Collections.IDictionary]$Values,
        [hashtable]$Processes,
        [long]$NowEpoch,
        [int]$WorkingSeconds,
        [int]$BlockedSeconds
    )

    $states = @{}
    foreach ($agent in @('claude', 'codex', 'opencode')) {
        $running = [bool]$Processes[$agent].Running
        $cpuTicks = [uint64]$Processes[$agent].CpuTicks
        $previousCpu = if ($Values.Contains("${agent}CpuTicks")) {
            [uint64]$Values["${agent}CpuTicks"]
        } else { [uint64]0 }
        $lastProgress = if ($Values.Contains("${agent}LastProgressEpoch")) {
            [long]$Values["${agent}LastProgressEpoch"]
        } else { [long]0 }
        $sessionActivity = if ($Values.Contains("${agent}SessionLastActivityEpoch")) {
            [long]$Values["${agent}SessionLastActivityEpoch"]
        } else { [long]0 }

        if ($sessionActivity -gt $lastProgress) {
            $lastProgress = $sessionActivity
        }
        if ($running -and ($previousCpu -eq 0 -or $cpuTicks -gt $previousCpu)) {
            $lastProgress = $NowEpoch
        }

        $age = if ($lastProgress -gt 0) {
            [math]::Max(0, $NowEpoch - $lastProgress)
        } else { -1 }

        if (($age -ge 0 -and $age -le $WorkingSeconds)) {
            $state = 'working'
        } elseif ($running -and $age -ge $BlockedSeconds) {
            $state = 'blocked'
        } else {
            $state = 'idle'
        }

        $Values["${agent}State"] = $state
        $Values["${agent}StateText"] =
            $state.Substring(0, 1).ToUpper() + $state.Substring(1)
        $Values["${agent}ProcessRunning"] = $running.ToString().ToLowerInvariant()
        $Values["${agent}ActivityAgeSeconds"] = [string]$age
        $Values["${agent}CpuTicks"] = [string]$cpuTicks
        $Values["${agent}LastProgressEpoch"] = [string]$lastProgress
        $states[$agent] = @{ State = $state; Age = $age }
    }

    $blocked = @($states.GetEnumerator() | Where-Object { $_.Value.State -eq 'blocked' })
    $working = @($states.GetEnumerator() | Where-Object { $_.Value.State -eq 'working' })
    if ($blocked.Count -gt 0) {
        $aggregate = 'blocked'
        $candidates = $blocked
    } elseif ($working.Count -gt 0) {
        $aggregate = 'working'
        $candidates = $working
    } else {
        $aggregate = 'idle'
        $candidates = @($states.GetEnumerator())
    }
    $active = $candidates | Sort-Object { if ($_.Value.Age -lt 0) { [long]::MaxValue } else { $_.Value.Age } } | Select-Object -First 1

    $Values['agentsState'] = $aggregate
    $Values['agentsStateText'] =
        $aggregate.Substring(0, 1).ToUpper() + $aggregate.Substring(1)
    $Values['activeAgent'] = if ($active) { $active.Key } else { 'agents' }
    $Values['agentsWorkingCount'] = [string]$working.Count
    $Values['agentsBlockedCount'] = [string]$blocked.Count
}

function Write-BridgeAtomic {
    param([string]$Path, [string]$Content)

    $parent = Split-Path $Path -Parent
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $temp = "$Path.tmp.$PID"
    try {
        [System.IO.File]::WriteAllText(
            $temp,
            $Content,
            [System.Text.UTF8Encoding]::new($false)
        )
        if (Test-Path -LiteralPath $Path) {
            [System.IO.File]::Replace($temp, $Path, $null)
        } else {
            [System.IO.File]::Move($temp, $Path)
        }
    } catch {
        if (Test-Path -LiteralPath $temp) {
            Move-Item -LiteralPath $temp -Destination $Path -Force
        } else {
            throw
        }
    } finally {
        if (Test-Path -LiteralPath $temp) {
            Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
        }
    }
}

$values = Read-BridgeValues $OutputFile
$now = [DateTimeOffset]::Now
$nowEpoch = $now.ToUnixTimeSeconds()
$lastUsageEpoch = if ($values.Contains('usageUpdatedEpoch')) {
    [long]$values['usageUpdatedEpoch']
} else { [long]0 }
$fullRefreshDue = $DryRun -or $lastUsageEpoch -le 0 -or
    ($nowEpoch - $lastUsageEpoch) -ge [math]::Max(1, $FullRefreshSeconds)

if ($fullRefreshDue) {
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

    if ($activityReport) {
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

        foreach ($agent in $agents) {
            $dailyEntries = @($dailyByAgent |
                Where-Object { $_.agent -eq $agent })
            foreach ($entry in $dailyEntries) {
                $date = [datetime]::ParseExact(
                    $entry.period,
                    'yyyy-MM-dd',
                    $null
                )
                if ($date -ge $weekStart -and $date -le $today) {
                    $weeklyCosts[$agent] += [double]$entry.totalCost
                }
            }

            $dailyEntry = @($dailyEntries |
                Where-Object { $_.period -eq $todayText })[-1]
            $monthlyEntry = @($monthlyByAgent |
                Where-Object {
                    $_.agent -eq $agent -and $_.period -eq $monthText
                })[-1]
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
        $values['usageUpdatedEpoch'] = [string]$nowEpoch
        $values['usageUpdatedAt'] = $now.ToString('HH:mm:ss')

        Write-Verbose (
            'Costs: Claude block=${0:0.00}, Claude week=${1:0.00}, Codex week=${2:0.00}, OpenCode week=${3:0.00}' -f
            $claudeBlockCost,
            $weeklyCosts.claude,
            $weeklyCosts.codex,
            $weeklyCosts.opencode
        )
    }
} else {
    Write-Verbose 'Reusing cached ccusage report values.'
}

$processes = Get-AgentProcesses
Add-AgentStates $values $processes $nowEpoch `
    ([math]::Max(1, $WorkingThresholdSeconds)) `
    ([math]::Max($WorkingThresholdSeconds + 1, $BlockedThresholdSeconds))
$values['updatedAt'] = $now.ToString('HH:mm:ss')

$payload = ($values.GetEnumerator() | ForEach-Object {
    "$($_.Key)=$($_.Value)"
}) -join "`n"

if ($DryRun) {
    Write-Host "Would write to: $OutputFile" -ForegroundColor Yellow
    Write-Host "Data:`n$payload" -ForegroundColor Yellow
    return
}

Write-BridgeAtomic $OutputFile $payload
Write-Host "Updated $OutputFile" -ForegroundColor Green
Write-Host "Usage report: $(if ($fullRefreshDue) { 'refreshed' } else { 'cached' })"
Write-Host (
    'States: Claude={0}, Codex={1}, OpenCode={2}, Agents={3}' -f
    $values['claudeState'],
    $values['codexState'],
    $values['opencodeState'],
    $values['agentsState']
)
Write-Verbose "Data:`n$payload"

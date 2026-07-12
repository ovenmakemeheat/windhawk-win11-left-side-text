<#
.SYNOPSIS
    Syntax-checks Windhawk mod source files (.wh.cpp) locally without the
    Windhawk UI.

.DESCRIPTION
    Mirrors the flags used by the Windhawk engine (Clang/mingw, C++23) and runs
    a -fsyntax-only pass. The mod API is provided via the offline editor stubs
    bundled in .vscode/windhawk_headers_1.7.3, so no Windhawk install is needed.

    A compiler is only accepted if it can actually compile a mod translation
    unit (a stdlib + windows.h under our flags). This picks a mingw toolchain
    and ignores, e.g., an MSVC-ABI LLVM clang++ that has no mingw sysroot.
    Only Windhawk's bundled Clang produces the final binary.

.PARAMETER Path
    A mod file or a folder to scan. Defaults to the mods\ folder.

.PARAMETER Compiler
    Force a specific compiler executable. By default the best available is used.

.EXAMPLE
    ./Scripts/Test-ModCompile.ps1
    ./Scripts/Test-ModCompile.ps1 mods/example-mod.wh.cpp
#>
[CmdletBinding()]
param(
    [string]$Path,
    [string]$Compiler
)

$ErrorActionPreference = 'Stop'
# Resolve defaults in the body: $PSScriptRoot is empty during param binding
# under Windows PowerShell 5.1.
if ([string]::IsNullOrEmpty($Path)) {
    $Path = Join-Path $PSScriptRoot '..\mods'
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$headersDir = Join-Path $repoRoot '.vscode\windhawk_headers_1.7.3'

if (-not (Test-Path -LiteralPath $headersDir)) {
    throw "Headers not found: $headersDir"
}

$commonArgs = @(
    '-fsyntax-only',
    '-x', 'c++',
    '-std=c++23',
    '-DWH_MOD',
    '-DWH_EDITING',
    '-DUNICODE',
    '-D_UNICODE',
    '-D_WIN32_WINNT=0x0A00',
    '-fno-exceptions',
    '-fno-rtti',
    '-Wall',
    '-Wno-unused-parameter',
    '-Wno-unused-variable',
    '-I', $headersDir,
    '-include', 'windhawk_api.h'
)

# MSYS2 compilers need their runtime DLLs on PATH to launch cc1plus.
function Get-MsysPath($exe) {
    $m = [regex]::Match($exe, '(?i)(.*[\\/]msys2[\\/]).*')
    if (-not $m.Success) { return $null }
    $root = $m.Groups[1].Value.TrimEnd('\', '/')
    return @((Split-Path $exe -Parent), (Join-Path $root 'usr\bin'))
}

# libstdc++ (g++) doesn't expose the unqualified global `nullptr_t` that the
# upstream Windhawk headers use (clang/MSVC do). Add a compat shim for g++.
function Get-CandidateArgs($exe) {
    $a = @() + $commonArgs
    if ([System.IO.Path]::GetFileName($exe) -match '(?i)^g\+\+|^gcc') {
        $a += '-include', '_local_gcc_compat.h'
    }
    return [string[]]$a
}

# A compiler is usable only if it can compile a mod TU: a stdlib + windows.h
# (pulled in by the force-included windhawk_api.h) under our flags.
$probeFile = Join-Path $env:TEMP 'wh_probe.cpp'
Set-Content -LiteralPath $probeFile -Value "#include <cstddef>`r`nint wh_probe_main(){return 0;}"

function Test-CompilerUsable($exe) {
    if (-not $exe -or -not (Test-Path -LiteralPath $exe)) { return $false }
    $mp = Get-MsysPath $exe
    $prevPath = $env:PATH
    if ($mp) { $env:PATH = "$($mp -join ';');$env:PATH" }
    try {
        $cArgs = Get-CandidateArgs $exe
        & $exe @cArgs $probeFile *> $null
        $ok = ($LASTEXITCODE -eq 0)
    } catch { $ok = $false }
    finally { $env:PATH = $prevPath }
    return $ok
}

$candidates = @()
if ($Compiler) { $candidates += $Compiler }
$candidates += @(
    'C:\Users\Gunte\scoop\apps\msys2\current\ucrt64\bin\clang++.exe',
    'C:\Users\Gunte\scoop\apps\msys2\current\mingw64\bin\clang++.exe',
    'C:\Users\Gunte\scoop\apps\msys2\current\ucrt64\bin\g++.exe',
    'C:\Users\Gunte\scoop\apps\msys2\current\mingw64\bin\g++.exe',
    (Get-Command clang++ -ErrorAction SilentlyContinue).Source,
    (Get-Command clang -ErrorAction SilentlyContinue).Source,
    (Get-Command g++ -ErrorAction SilentlyContinue).Source
) | Where-Object { $_ }

$cc = $null
foreach ($c in $candidates) {
    if (Test-CompilerUsable $c) { $cc = $c; break }
}
if (-not $cc) {
    throw "No working C++ compiler found. Install MSYS2 (g++/clang, mingw) or pass -Compiler <path>."
}
Write-Host "Using compiler: $cc" -ForegroundColor DarkGray

# Persist PATH for the chosen compiler so the real runs can launch cc1plus.
$mp = Get-MsysPath $cc
if ($mp) { $env:PATH = "$($mp -join ';');$env:PATH" }
$finalArgs = Get-CandidateArgs $cc

if (Test-Path -LiteralPath $Path -PathType Container) {
    $files = @(Get-ChildItem -LiteralPath $Path -Filter '*.wh.cpp' -Recurse | Select-Object -ExpandProperty FullName)
} else {
    $files = @(Resolve-Path -LiteralPath $Path | Select-Object -ExpandProperty Path)
}

if ($files.Count -eq 0) {
    Write-Warning "No .wh.cpp files found under: $Path"
    return
}

$failed = 0
foreach ($f in $files) {
    # GetRelativePath is .NET Core only; compute it for Windows PowerShell 5.1.
    if ($f.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $f.Substring($repoRoot.Length).TrimStart('\', '/')
    } else {
        $rel = $f
    }
    Write-Host "`n==> $rel" -ForegroundColor Cyan

    # Capture stderr as text without throwing under Windows PowerShell 5.1,
    # where native-command stderr becomes ErrorRecord objects under Stop.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & $cc @finalArgs $f 2>&1 | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { [string]$_ }
    }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP

    if ($code -eq 0) {
        Write-Host "    OK" -ForegroundColor Green
    } else {
        $failed++
        Write-Host "    FAILED (exit $code)" -ForegroundColor Red
        $out | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkYellow }
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host "$failed file(s) failed syntax check." -ForegroundColor Red
    exit 1
} else {
    Write-Host "All $($files.Count) file(s) passed." -ForegroundColor Green
}

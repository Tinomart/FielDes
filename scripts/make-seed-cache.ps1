<#
.SYNOPSIS
  Makes seed-cache\result-cache: the solved problems of the guided tour, which the packages carry (docs/caching-and-performance.md, "Solved problems that come with the program").

.DESCRIPTION
  The tour shows a solved beam, a flow, an optimization.  Asked cold, each is a wait of seconds with nothing to look at; the packages therefore carry the files the solvers keep (`.fdresult` and
  `.json` in a result cache), and the program reads a problem from them when its user's own cache has no file for it.  A file is made for ONE version of the solvers (`RESULT_VERSION` in
  python\fieldes\stdlib\result_cache.py is in its name): make them again after any change of a solver, from the program that is packaged, and before the packages are made.

  What it does: starts the real program (dist\FielDes\FielDes.exe) twice, each time in a window of its own with a result cache that is an empty folder of its own, FIELDES_NO_SEED=1 and settings of its own:
    1. the tour is started, every step is asked for once and its title and the number of steps are read from the log;
    2. every step is walked to -- the model the step shows is made, solved and kept -- and for the steps that SOLVE what the user's own hand writes (the titles in -ShowMe), "Show me" is run too, so that the
       problem the menus write (element size by default) is kept as well as the one the step starts from.
  The files are then in seed-cache\result-cache.  The build scripts copy that folder into the packages; it is not in git.

.PARAMETER ShowMe
  The titles of the steps whose "Show me" is run (the English titles of app/src/tutorial.cpp).
.PARAMETER Exe
  The program (default dist\FielDes\FielDes.exe of this repository).
.PARAMETER MinutesCap
  The longest the walk may take; the window is stopped after it (default 150).
#>
param(
    [string[]]$ShowMe = @('Run the analysis', 'Solve the flow', 'Find the lightest shape', 'Allow less material'),
    [string]$Exe,
    [int]$MinutesCap = 150
)
$ErrorActionPreference = 'Stop'
$Source = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $Source 'dist\FielDes\FielDes.exe' }
if (-not (Test-Path -LiteralPath $Exe)) { throw "There is no program at $Exe" }
$seedRoot = Join-Path $Source 'seed-cache'
$seed = Join-Path $seedRoot 'result-cache'
$work = Join-Path ([IO.Path]::GetTempPath()) 'fieldes-seed-work'
foreach ($d in @($work, $seedRoot)) { if (Test-Path -LiteralPath $d) { [IO.Directory]::Delete($d, $true) } }
foreach ($d in @($work, $seed, "$work\user", "$work\render", "$work\scene", "$work\settings", "$work\solvers")) { New-Item -ItemType Directory -Force $d | Out-Null }
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$work\tour.py", "from fieldes import *`n", $utf8)

function Start-Window([string]$name, [string[]]$automation, [int]$capSeconds) {
    # (a window of the program with its own settings and caches, and the result cache that is being made; nothing of the user's is read or written)
    $file = "$work\$name.txt"
    [IO.File]::WriteAllText($file, ($automation -join "`n") + "`n", $utf8)
    $env:FIELDES_AUTOMATION = $file
    $env:FIELDES_RESULT_CACHE_DIR = $seed
    $env:FIELDES_NO_SEED = '1'
    $env:FIELDES_RENDER_CACHE_DIR = "$work\render"
    $env:FIELDES_SCENE_CACHE_DIR = "$work\scene"
    $env:FIELDES_SETTINGS_DIR = "$work\settings"
    $env:FIELDES_USER_DIR = "$work\user"
    $env:FIELDES_SOLVERS = "$work\solvers"
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $proc = Start-Process -FilePath $Exe -ArgumentList @("`"$work\tour.py`"") -RedirectStandardError "$work\$name.err.log" -RedirectStandardOutput "$work\$name.out.log" -PassThru
    if (-not $proc.WaitForExit($capSeconds * 1000)) {
        Write-Warning "${name}: stopped after $capSeconds s (this window is mine: pid $($proc.Id))"
        Stop-Process -Id $proc.Id -Force
    }
}

# 1. the steps: every one is asked for once, and the log says which it is
$ask = @('wait 3000', 'tour start', 'wait 2500')
for ($i = 0; $i -le 400; $i++) { $ask += "tour goto $i" }
Write-Host 'Reading the steps of the tour ...'
# (a step beyond the last ends the tour: the walk stops being asked after it, the log says how many there are)
Start-Window 'steps' ($ask + @('quit')) 240
$titles = @{}
$last = 0
foreach ($line in Get-Content -LiteralPath "$work\steps.err.log" -Encoding UTF8) {
    if ($line -match 'tour step (\d+)/(\d+) \[(.*)\] done=') {
        $titles[[int]$Matches[1]] = $Matches[3]
        $last = [int]$Matches[2]
    }
}
if ($last -lt 10) { throw "The log of the first window does not list the steps of the tour (see $work\steps.err.log)" }
Write-Host "The tour has $($last + 1) steps."
# (what the first pass made is not wanted: only what the walk below solves)
Get-ChildItem -LiteralPath $seed -File | ForEach-Object { [IO.File]::Delete($_.FullName) }

# 2. the walk
$walk = @('wait 3000', 'tour start', 'wait 2500')
for ($i = 1; $i -lt $last; $i++) {
    $walk += "tour goto $i"
    $walk += 'waitrender 900 25'
    if ($ShowMe -contains $titles[$i]) {
        $walk += 'tour showme'
        $walk += 'waitdemo 900'
        $walk += 'waitrender 900 25'
    }
}
$walk += 'quit'
Write-Host "Walking the steps ($($ShowMe.Count) titles with Show me), at most $MinutesCap minutes ..."
Start-Window 'walk' $walk ($MinutesCap * 60)

$files = Get-ChildItem -LiteralPath $seed -File
if ($files.Count -eq 0) { throw 'Nothing was solved: seed-cache is empty (see the logs in ' + $work + ')' }
$mb = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "seed-cache\result-cache: $($files.Count) files, $mb MB" -ForegroundColor Green
Write-Host "(the logs of the two windows: $work)"

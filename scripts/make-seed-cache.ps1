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
    [string[]]$ShowMe = @('Run the analysis', 'Solve the flow', 'Find the lightest shape', 'Allow less material', 'See where it breaks the rule', 'Let the tool fix it'),
    [string]$MeshFrom = 'The rules of a printer',
    [string]$MeshUntil = 'An input instead of a number',
    [switch]$ManufacturingOnly,
    [string]$Exe,
    [int]$MinutesCap = 150
)
$ErrorActionPreference = 'Stop'
$Source = Split-Path -Parent $PSScriptRoot

function Remove-Folder([string]$path) {
    # (a folder of a repository in OneDrive is read-only for the sync, and a delete of it is refused: freed first)
    # (and one file at a time, then the folders from the deepest: a recursive delete of a folder with files that OneDrive keeps as placeholders says "the directory is not empty")
    if (-not (Test-Path -LiteralPath $path)) { return }
    foreach ($f in @(Get-ChildItem -LiteralPath $path -Recurse -Force -File)) {
        try { $f.Attributes = 'Normal' } catch { }
        [IO.File]::Delete($f.FullName)
    }
    foreach ($d in @(Get-ChildItem -LiteralPath $path -Recurse -Force -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
        try { $d.Attributes = 'Directory' } catch { }
        [IO.Directory]::Delete($d.FullName, $false)
    }
    try { (Get-Item -LiteralPath $path -Force).Attributes = 'Directory' } catch { }
    [IO.Directory]::Delete($path, $false)
}
if (-not $Exe) { $Exe = Join-Path $Source 'dist\FielDes\FielDes.exe' }
if (-not (Test-Path -LiteralPath $Exe)) { throw "There is no program at $Exe" }
$seedRoot = Join-Path $Source 'seed-cache'
$seed = Join-Path $seedRoot 'result-cache'
$work = Join-Path ([IO.Path]::GetTempPath()) 'fieldes-seed-work'
# (-ManufacturingOnly: the solved problems that are there stay, and only the manufacturing results and the meshes are made again -- the steps of -MeshSteps are walked, nothing else)
$remake = if ($ManufacturingOnly) { @($work, "$seed\manufacturing", "$seedRoot\render-cache") } else { @($work, $seedRoot) }
foreach ($d in $remake) { Remove-Folder $d }
foreach ($d in @($work, $seed, "$work\user", "$work\render", "$work\scene", "$work\settings", "$work\solvers")) { New-Item -ItemType Directory -Force $d | Out-Null }
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$work\tour.py", "from fieldes import *`n", $utf8)

function Start-Window([string]$name, [string[]]$automation, [int]$capSeconds, [string]$render = '') {
    # (a window of the program with its own settings and caches, and the result cache that is being made; nothing of the user's is read or written)
    $file = "$work\$name.txt"
    [IO.File]::WriteAllText($file, ($automation -join "`n") + "`n", $utf8)
    $env:FIELDES_AUTOMATION = $file
    $env:FIELDES_RESULT_CACHE_DIR = $seed
    $env:FIELDES_NO_SEED = '1'
    $env:FIELDES_TOUR_HOLD_MS = '600000'           # (how long the result of a Show me stays on show: until the next step, however long its picture takes)
    $env:FIELDES_RENDER_CACHE_DIR = if ($render) { $render } else { "$work\render" }
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
# (the steps of the manufacturing chapter, from the step that starts it to the one before the next chapter's first: the ones that ask for the result of the tool are among them -- the isolate steps make the model with the
# optimization in it -- and the ones that show it)
$from = ($titles.GetEnumerator() | Where-Object { $_.Value -eq $MeshFrom } | Select-Object -First 1).Key
$until = ($titles.GetEnumerator() | Where-Object { $_.Value -eq $MeshUntil } | Select-Object -First 1).Key
if ($null -eq $from -or $null -eq $until -or $until -le $from) { throw "The steps '$MeshFrom' to '$MeshUntil' are not in the tour (see $work\steps.err.log)" }
$meshSteps = @($from..($until - 1))
Write-Host "The manufacturing chapter is the steps $from to $($until - 1)."
# (what the first pass made is not wanted: only what the walk below solves)
if (-not $ManufacturingOnly) { Get-ChildItem -LiteralPath $seed -File | ForEach-Object { [IO.File]::Delete($_.FullName) } }
Remove-Folder "$seed\manufacturing"          # (the manufacturing results of the first pass: python\fieldes\stdlib\manufacturing_store.py)

# 2. the walk
$walk = @('wait 3000', 'tour start', 'wait 2500')
for ($i = 1; $i -lt $last; $i++) {
    if ($ManufacturingOnly -and -not ($meshSteps -contains $i)) { continue }
    $walk += "tour goto $i"
    $walk += 'waitrender 900 25'
    if ($ShowMe -contains $titles[$i]) {
        # (the show is waited for until its result is ON THE SCREEN and held there -- FIELDES_TOUR_HOLD_MS, below --: a show that is over puts everything back, and a render that is still going, the picture of
        # a check of the part with the placement of a selected part takes a minute and a half, is stopped with it: nothing would be kept of it)
        $walk += 'tour showme'
        $walk += 'waitdemohold 900'
    }
}
$walk += 'quit'
Write-Host "Walking the steps ($($ShowMe.Count) titles with Show me), at most $MinutesCap minutes ..."
Start-Window 'walk' $walk ($MinutesCap * 60)

# 3. the pictures: the result of a manufacturing check or optimization is meshed from its fields, which takes seconds the first time (a result read back from its folder is drawn in 3 to 10 s), so the program carries the
# meshes of those steps as well (`seed-cache\render-cache`, read where it is by the render cache when the user's own folder has no mesh for the key).  The steps are walked once more, in a window with a render cache of its own
# and the result cache that was just made (the results are read back, not made again); the meshes it kept are the ones that are shipped
$meshDir = Join-Path $seedRoot 'render-cache'
$renderWork = "$work\render-mesh"
New-Item -ItemType Directory -Force $meshDir, $renderWork | Out-Null
$pictures = @('wait 3000', 'tour start', 'wait 2500')
for ($i = 1; $i -lt $last; $i++) {
    if ($meshSteps -contains $i) {
        $pictures += "tour goto $i"
        $pictures += 'waitrender 900 60'
        if ($ShowMe -contains $titles[$i]) {
            $pictures += 'tour showme'
            $pictures += 'waitdemohold 900'
        }
    }
}
$pictures += 'quit'
Write-Host "Meshing the $($meshSteps.Count) steps of the manufacturing chapter for the render cache ..."
Start-Window 'pictures' $pictures 900 $renderWork
Get-ChildItem -LiteralPath $renderWork -Filter *.fdmesh -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $meshDir -Force }

$files = Get-ChildItem -LiteralPath $seed -File
if ($files.Count -eq 0) { throw 'Nothing was solved: seed-cache is empty (see the logs in ' + $work + ')' }
$mb = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "seed-cache\result-cache: $($files.Count) files, $mb MB" -ForegroundColor Green
$mfg = if (Test-Path -LiteralPath "$seed\manufacturing") { @(Get-ChildItem -LiteralPath "$seed\manufacturing" -Directory) } else { @() }
$mfgMb = [math]::Round((($mfg | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -File -Recurse } | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host "seed-cache\result-cache\manufacturing: $($mfg.Count) results, $mfgMb MB" -ForegroundColor Green
$meshes = @(Get-ChildItem -LiteralPath $meshDir -Filter *.fdmesh -File)
$meshMb = [math]::Round((($meshes | Measure-Object Length -Sum).Sum) / 1MB, 1)
Write-Host "seed-cache\render-cache: $($meshes.Count) meshes, $meshMb MB" -ForegroundColor Green
if ($meshes.Count -eq 0) { Write-Warning 'No mesh was kept: the printer steps of the tour draw their results from nothing the first time (see the logs in the work folder: [render cache] lines).' }
if ($mfg.Count -eq 0) { Write-Warning 'No manufacturing result was kept: the tour shows its printer step with an empty viewport for the seconds the tool takes (see the logs in the work folder: [manufacturing store] lines).' }
Write-Host "(the logs of the two windows: $work)"

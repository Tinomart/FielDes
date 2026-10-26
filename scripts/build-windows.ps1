<#
.SYNOPSIS
    Build FielDes on Windows (MSVC + vcpkg) and, optionally, make a portable folder.

.DESCRIPTION
    One-time prerequisites:
      * Visual Studio 2022 Build Tools: MSVC v143, a Windows SDK, and "C++ CMake tools for Windows"
      * vcpkg cloned and bootstrapped at -Vcpkg (keep it outside OneDrive and other synced folders)
      * the dependencies:  .\scripts\build-windows.ps1 -InstallDeps   (about an hour: it builds Qt)

.EXAMPLE
    .\scripts\build-windows.ps1                 # incremental build of FielDes.exe
    .\scripts\build-windows.ps1 -Package        # build, then make / refresh the portable folder
    .\scripts\build-windows.ps1 -InstallDeps    # first time only: the vcpkg dependencies

.NOTES
    FielDes.exe finds its Python package by walking up from its own folder looking for
    python\fieldes, and its Python runtime for runtime\python3 (the portable folder) or
    vcpkg\installed\x64-windows\tools\python3 (next to a vcpkg checkout).
#>
param(
    [string]$Source     = (Split-Path -Parent $PSScriptRoot),
    [string]$Vcpkg      = 'C:\dev\vcpkg',
    [string]$BuildDir   = 'C:\dev\fieldes-build',
    [string]$Config     = 'Release',
    [string]$PackageDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\FielDes'),
    [switch]$InstallDeps,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$Triplet = 'x64-windows'
$Installed = Join-Path $Vcpkg "installed\$Triplet"

function Run($exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# --- CMake (the copy bundled with the VS Build Tools) --------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'VS Build Tools not found. Install "Visual Studio 2022 Build Tools" (C++ workload).' }
$vsRoot = & $vswhere -products * -latest -property installationPath
$cmake  = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw "CMake tools missing from $vsRoot (add the component 'C++ CMake tools for Windows')." }

# --- Dependencies via vcpkg ----------------------------------------------------
$vcpkgExe = Join-Path $Vcpkg 'vcpkg.exe'
if (-not (Test-Path $vcpkgExe)) { throw "vcpkg not bootstrapped at $Vcpkg (run bootstrap-vcpkg.bat -disableMetrics)." }

if ($InstallDeps) {
    Run $vcpkgExe @('install', '--triplet', $Triplet, '--clean-after-build',
        'eigen3', 'boost-container', 'boost-bimap', 'boost-interval', 'boost-lockfree',
        'boost-functional', 'boost-algorithm', 'boost-math', 'libpng', 'qt5-base', 'python3',
        'manifold')
}
foreach ($need in 'include\eigen3\Eigen', 'tools\python3\python.exe', 'bin\Qt5Core.dll', 'lib\libpng16.lib', 'include\manifold\polygon.h') {
    if (-not (Test-Path (Join-Path $Installed $need))) {
        throw "Missing $need under $Installed. Run: .\scripts\build-windows.ps1 -InstallDeps"
    }
}

# --- Configure and build -------------------------------------------------------
# A build tree that builds FielDes.exe straight into a folder (FIELDES_OUTPUT_DIR) keeps an older copy of the exe and the DLLs in app\Release, and the portable
# folder is a mirror of that: mirrored, it would put the old files over the new ones.  When that folder IS the portable folder (the one build tree of a developer, which
# builds into dist\FielDes), the exe and the kernel's DLLs are already there, new: the first step of the package then copies only what is newer or missing, and mirrors
# nothing.  A build tree that builds into some other folder cannot be packaged: its files are not the ones in the folder.
$cache = Join-Path $BuildDir 'CMakeCache.txt'
$intoPackage = $false
if ($Package -and (Test-Path $cache)) {
    $line = Select-String -Path $cache -Pattern '^FIELDES_OUTPUT_DIR:PATH=(.+)$' | Select-Object -First 1
    if ($line) {
        $dir = $line.Matches[0].Groups[1].Value
        $same = (Resolve-Path -LiteralPath $dir -ErrorAction SilentlyContinue) -and (Resolve-Path -LiteralPath $PackageDir -ErrorAction SilentlyContinue) -and
                ((Resolve-Path -LiteralPath $dir).Path.TrimEnd('\') -ieq (Resolve-Path -LiteralPath $PackageDir).Path.TrimEnd('\'))
        if (-not $same) {
            throw "$BuildDir builds into $dir, which is not the portable folder ($PackageDir); package from a build tree that builds into it, or without FIELDES_OUTPUT_DIR."
        }
        $intoPackage = $true
    }
}
Run $cmake @('-S', $Source, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DEIGEN_INCLUDE_DIRS=$($Installed.Replace('\','/'))/include/eigen3")

# /nr:false: no MSBuild node reuse (a worker left over from this build can keep the
# DLLs locked and make the next build's copy step fail)
Run $cmake @('--build', $BuildDir, '--config', $Config, '--target', 'FielDes', '--parallel', '--', '/nr:false')

$out = Join-Path $BuildDir "app\$Config"
Write-Host "`nBuilt: $out\FielDes.exe" -ForegroundColor Green

# --- Portable folder -----------------------------------------------------------
if ($Package) {
    function Mirror($from, $to, [string[]]$extra) {
        robocopy $from $to /MIR /NFL /NDL /NJH /NJS /NP @extra | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "robocopy $from -> $to failed ($LASTEXITCODE)" }
        $global:LASTEXITCODE = 0
    }
    # 1. FielDes.exe, the kernel's DLLs, Qt / Python / PNG runtime DLLs, Qt plugins
    #    (/MIR would remove the pieces below, so this one goes first)
    if ($intoPackage) {
        # (the build wrote the exe and the DLLs here already: only what is missing or newer comes over, and nothing is deleted)
        robocopy $out $PackageDir /E /XO /NFL /NDL /NJH /NJS /NP /XF *.lib *.exp *.pdb /XD vcpkg python runtime examples | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "robocopy $out -> $PackageDir failed ($LASTEXITCODE)" }
        $global:LASTEXITCODE = 0
    } else {
        Mirror $out $PackageDir @('/XF', '*.lib', '*.exp', '*.pdb', '/XD', 'vcpkg', 'python', 'runtime', 'examples')
    }
    # 2. The Python runtime
    Mirror (Join-Path $Installed 'tools\python3') (Join-Path $PackageDir 'runtime\python3') `
        @('/XD', '__pycache__', 'test', 'idlelib', 'turtledemo', '/XF', '_freeze_module.exe')
    # 3. The Python package
    Mirror (Join-Path $Source 'python') (Join-Path $PackageDir 'python') @('/XD', '__pycache__')
    # 4. The examples (without sample STEP files: those are GrabCAD downloads that may not be redistributed -- except the one
    #    bracket that is distributed at the maintainer's decision, copied in below)
    Mirror (Join-Path $Source 'examples') (Join-Path $PackageDir 'examples') @('/XD', '__pycache__', '*.fieldes-cache.trees', '*.fieldes-bspline', '*.fieldes-tessellation', 'meshes', '/XF', '*.fieldes-cache.py', '*.step', '*.stp')
    Copy-Item (Join-Path $Source 'examples\step\PivotBearingSupportBracket.STEP') (Join-Path $PackageDir 'examples\step') -Force
    # The command line of the apps (`fieldes run tool.fieldesapp --Radius 7`): FielDes.exe is a window program, this waits for it
    Copy-Item (Join-Path $Source 'scripts\fieldes.cmd') $PackageDir -Force
    foreach ($doc in 'README.md', 'LICENSE-GPL-2.0', 'LICENSE-MPL-2.0', 'NOTICE.md', 'CHANGELOG.md') {
        if (Test-Path (Join-Path $Source $doc)) { Copy-Item (Join-Path $Source $doc) $PackageDir -Force }
    }
    Mirror (Join-Path $Source 'docs') (Join-Path $PackageDir 'docs') @()
    # The translations of the program's texts (translations/<code>.json; English needs no file)
    Mirror (Join-Path $Source 'translations') (Join-Path $PackageDir 'translations') @()
    # The solved problems of the guided tour (seed-cache/result-cache, made by scripts/make-seed-cache.ps1 from the solvers that are packaged): the first time a chapter shows a solved beam or flow it is
    # read from a file and not solved while the screen is empty.  They are not in git: a file is made for one version of the solvers (RESULT_VERSION)
    if (Test-Path (Join-Path $Source 'seed-cache')) {
        Mirror (Join-Path $Source 'seed-cache') (Join-Path $PackageDir 'seed-cache') @()
    } else {
        Write-Warning 'seed-cache/ is not there: the tour solves its beam and its flow the first time it shows them (run scripts/make-seed-cache.ps1 first).'
    }
    # The custom blocks folder (the sample blocks; the user's own blocks go here or in a folder of their choice)
    Mirror (Join-Path $Source 'blocks') (Join-Path $PackageDir 'blocks') @('/XD', '__pycache__')
    # 5. The MSVC runtime, so it runs without the VC++ redistributable installed
    $crt = Get-ChildItem (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
        ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } | Where-Object { Test-Path $_ } | Select-Object -Last 1
    if ($crt) { Copy-Item "$crt\*.dll" $PackageDir -Force } else { Write-Warning 'MSVC CRT redist not found; target PCs need the VC++ 2015-2022 redistributable.' }

    $mb = [math]::Round((Get-ChildItem $PackageDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
    Write-Host "Portable folder: $PackageDir  ($mb MB)" -ForegroundColor Green
}

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
    Mirror $out $PackageDir @('/XF', '*.lib', '*.exp', '*.pdb', '/XD', 'vcpkg', 'python', 'runtime', 'examples')
    # 2. The Python runtime
    Mirror (Join-Path $Installed 'tools\python3') (Join-Path $PackageDir 'runtime\python3') `
        @('/XD', '__pycache__', 'test', 'idlelib', 'turtledemo', '/XF', '_freeze_module.exe')
    # 3. The Python package
    Mirror (Join-Path $Source 'python') (Join-Path $PackageDir 'python') @('/XD', '__pycache__')
    # 4. The examples (without sample STEP files: those are GrabCAD downloads that may not be redistributed -- except the one
    #    bracket that is distributed at the maintainer's decision, copied in below)
    Mirror (Join-Path $Source 'examples') (Join-Path $PackageDir 'examples') @('/XD', '__pycache__', '*.fieldes-cache.trees', '*.fieldes-bspline', '*.fieldes-tessellation', 'meshes', '/XF', '*.fieldes-cache.py', '*.step', '*.stp')
    Copy-Item (Join-Path $Source 'examples\step\PivotBearingSupportBracket.STEP') (Join-Path $PackageDir 'examples\step') -Force
    foreach ($doc in 'README.md', 'LICENSE-GPL-2.0', 'LICENSE-MPL-2.0', 'NOTICE.md', 'CHANGELOG.md') {
        if (Test-Path (Join-Path $Source $doc)) { Copy-Item (Join-Path $Source $doc) $PackageDir -Force }
    }
    Mirror (Join-Path $Source 'docs') (Join-Path $PackageDir 'docs') @()
    # The custom blocks folder (the sample blocks; the user's own blocks go here or in a folder of their choice)
    Mirror (Join-Path $Source 'blocks') (Join-Path $PackageDir 'blocks') @('/XD', '__pycache__')
    # 5. The MSVC runtime, so it runs without the VC++ redistributable installed
    $crt = Get-ChildItem (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory |
        ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } | Where-Object { Test-Path $_ } | Select-Object -Last 1
    if ($crt) { Copy-Item "$crt\*.dll" $PackageDir -Force } else { Write-Warning 'MSVC CRT redist not found; target PCs need the VC++ 2015-2022 redistributable.' }

    $mb = [math]::Round((Get-ChildItem $PackageDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
    Write-Host "Portable folder: $PackageDir  ($mb MB)" -ForegroundColor Green
}

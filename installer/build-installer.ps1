<#
.SYNOPSIS
    Builds the Windows installer of FielDes: one .exe that asks for the language and the folder, installs for the
    current user (no administrator rights), makes the shortcuts and the entry in Settings > Apps, and uninstalls.

.DESCRIPTION
    The portable folder (dist\FielDes, made by  scripts\build-windows.ps1 -Package ) goes into the installer as a zip.
    Setup.cs is compiled twice: once as the uninstaller (no payload), once as Setup with the payload, the uninstaller
    and the version inside.  The compiler is the one of the Visual Studio Build Tools that builds FielDes
    (or the .NET Framework's csc.exe, which every Windows has).

.EXAMPLE
    .\installer\build-installer.ps1                 # dist\FielDes-<version>-windows-x64-setup.exe
    .\installer\build-installer.ps1 -Folder D:\FielDes -Out D:\setup.exe

.NOTES
    The installer is not signed: Windows SmartScreen says "unknown publisher" the first time it is run
    (More info > Run anyway).  Signing needs a code-signing certificate; add signtool to this script's end when there is one.
    /S installs silently; see the top of Setup.cs for the other switches.
#>
param(
    [string]$Source = (Split-Path -Parent $PSScriptRoot),
    [string]$Folder = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\FielDes'),
    [string]$Out    = ''
)
$ErrorActionPreference = 'Stop'

$version = [regex]::Match((Get-Content (Join-Path $Source 'CMakeLists.txt') -Raw), 'project\(FielDes VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'The version is not in CMakeLists.txt (project(FielDes VERSION x.y.z ...)).' }
if (-not $Out) { $Out = Join-Path $Source "dist\FielDes-$version-windows-x64-setup.exe" }
if (-not (Test-Path (Join-Path $Folder 'FielDes.exe'))) { throw "$Folder is not the portable folder (no FielDes.exe): run scripts\build-windows.ps1 -Package first." }

# --- the compiler ------------------------------------------------------------------------
$csc = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -products * -latest -property installationPath
    $roslyn = Join-Path $vsRoot 'MSBuild\Current\Bin\Roslyn\csc.exe'
    if ($vsRoot -and (Test-Path $roslyn)) { $csc = $roslyn }
}
if (-not $csc) { $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe' }
if (-not (Test-Path $csc)) { throw 'No C# compiler found (neither the Visual Studio Build Tools nor the .NET Framework).' }

$work = Join-Path ([IO.Path]::GetTempPath()) ('fieldes-installer-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
try {
    $src  = Join-Path $PSScriptRoot 'Setup.cs'
    $icon = Join-Path $Source 'app\deploy\windows\fieldes.ico'
    Set-Content (Join-Path $work 'version.txt') $version -Encoding ascii
    $refs = @('/r:System.dll', '/r:System.Core.dll', '/r:System.Drawing.dll', '/r:System.Windows.Forms.dll',
              '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll')
    $common = @('/nologo', '/target:winexe', '/optimize+', '/langversion:5', '/codepage:65001', "/win32icon:$icon") + $refs

    # 1. the uninstaller: the same program without a payload
    $uninstall = Join-Path $work 'uninstall.exe'
    & $csc @common "/out:$uninstall" "/resource:$work\version.txt,version.txt" $src
    if ($LASTEXITCODE -ne 0) { throw 'compiling the uninstaller failed' }

    # 2. the payload: the portable folder as a zip (forward slashes in the entry names, as the zip format has them)
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $payload = Join-Path $work 'payload.zip'
    $root = (Resolve-Path $Folder).Path.TrimEnd('\')
    $zip = [IO.Compression.ZipFile]::Open($payload, 'Create')
    try {
        $count = 0
        Get-ChildItem $root -Recurse -File -Force | Where-Object {
            $_.FullName -notmatch '\\__pycache__\\' -and $_.Extension -ne '.pyc' -and $_.Name -notlike 'FielDes-*-setup.exe'
        } | ForEach-Object {
            $rel = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal)
            $count++
        }
    } finally { $zip.Dispose() }
    Write-Host ("payload: {0} files, {1:N0} MB" -f $count, ((Get-Item $payload).Length / 1MB))

    # 3. Setup: the program with the payload, the uninstaller and the version inside
    New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
    & $csc @common "/out:$Out" "/resource:$payload,payload.zip" "/resource:$uninstall,uninstall.exe" "/resource:$work\version.txt,version.txt" $src
    if ($LASTEXITCODE -ne 0) { throw 'compiling Setup failed' }
    Write-Host ("Installer: {0}  ({1:N0} MB)" -f $Out, ((Get-Item $Out).Length / 1MB)) -ForegroundColor Green
} finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

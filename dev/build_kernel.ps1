$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -products * -latest -property installationPath
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$t0 = Get-Date
& $cmake --build C:\dev\fieldes-build --config Release --target libfive --parallel -- /nr:false
if ($LASTEXITCODE -ne 0) { throw "build failed $LASTEXITCODE" }
"KERNEL-COMPILE-OK in {0:N0} s (not copied)" -f ((Get-Date) - $t0).TotalSeconds

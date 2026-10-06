$ErrorActionPreference = 'Continue'   # (a warning of CMake on stderr is not an error: the exit code below says)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -products * -latest -property installationPath
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$t0 = Get-Date
& $cmake --build C:\dev\fieldes-build --config Release --target FielDes --parallel -- /nr:false 2>&1 | Select-String -Pattern "error|warning C4|FielDes.vcxproj ->|libfive.vcxproj ->" | Select-Object -First 60
if ($LASTEXITCODE -ne 0) { throw "build failed $LASTEXITCODE" }
"APP-BUILD-OK in {0:N0} s" -f ((Get-Date) - $t0).TotalSeconds

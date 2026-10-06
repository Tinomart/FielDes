# Copies the build's exe and library into dist\FielDes (the one copy the taskbar pin and every test use), and
# mirrors the Python package, the examples, the custom blocks and the docs.  A FielDes window that is open keeps the library it loaded.
$ErrorActionPreference = 'Stop'
$R = 'C:\Users\tinmi\OneDrive\FielDes\FielDes'
$B = 'C:\dev\fieldes-build'
Get-Process FielDes -ErrorAction SilentlyContinue | Stop-Process -Force
# (the files are free once the processes have really gone, a moment after the kill; a window that was just closed by
# its own script may still be going down)
Get-Process FielDes -ErrorAction SilentlyContinue | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue
foreach ($pair in @(@("$B\app\Release\FielDes.exe", "$R\dist\FielDes\FielDes.exe"),
                    @("$B\kernel\src\Release\fieldes.dll", "$R\dist\FielDes\fieldes.dll"))) {
    for ($try = 1; $try -le 20; $try++) {
        try { Copy-Item $pair[0] $pair[1] -Force -ErrorAction Stop; break }
        catch { if ($try -eq 20) { throw } ; Start-Sleep -Milliseconds 500 }
    }
}
Copy-Item "$R\python\fieldes\*" "$R\dist\FielDes\python\fieldes\" -Recurse -Force
Copy-Item "$R\examples\*.py" "$R\dist\FielDes\examples\" -Force
Copy-Item "$R\examples\README.md" "$R\dist\FielDes\examples\" -Force
# (the one sample part that is distributed: see examples\step\README.md)
New-Item -ItemType Directory -Force "$R\dist\FielDes\examples\step" | Out-Null
Copy-Item "$R\examples\step\README.md", "$R\examples\step\PivotBearingSupportBracket.STEP" "$R\dist\FielDes\examples\step\" -Force
Copy-Item "$R\docs\*.md" "$R\dist\FielDes\docs\" -Force
if (Test-Path "$R\docs\images") {
    New-Item -ItemType Directory -Force "$R\dist\FielDes\docs\images" | Out-Null
    Copy-Item "$R\docs\images\*" "$R\dist\FielDes\docs\images\" -Force
}
New-Item -ItemType Directory -Force "$R\dist\FielDes\blocks" | Out-Null
Copy-Item "$R\blocks\*" "$R\dist\FielDes\blocks\" -Force
Copy-Item "$R\CHANGELOG.md", "$R\README.md" "$R\dist\FielDes\" -Force
"deployed to dist\FielDes"

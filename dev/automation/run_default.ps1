param([string]$tag, [string]$auto, [int]$capSeconds = 300)
# the application with NO script argument (a new file: the starting script), beside any other FielDes window
$sp = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$dist = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dist\FielDes'
foreach ($d in "$sp\logs\fc_$tag", "$sp\logs\rc_$tag") { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }
$env:FIELDES_TIMING = '1'
$env:FIELDES_FIELD_CACHE_DIR = "$sp\logs\fc_$tag"
$env:FIELDES_RENDER_CACHE_DIR = "$sp\logs\rc_$tag"
$env:FIELDES_AUTOMATION = "$sp\automation\$auto"
$t = Get-Date
$p = Start-Process -FilePath "$dist\FielDes.exe" -WorkingDirectory $dist -RedirectStandardError "$sp\logs\$tag.log" -PassThru
$p.WaitForExit($capSeconds * 1000) | Out-Null
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "killed after cap" } else { "finished after {0:N0} s" -f ((Get-Date) - $t).TotalSeconds }

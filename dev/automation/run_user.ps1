param([string]$script, [string]$auto, [int]$capSeconds = 600)
# the application as the user starts it: no console, no stderr, the real cache folders; only the automation script is set
$sp = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$dist = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dist\FielDes'
Get-Process FielDes -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item Env:FIELDES_FIELD_CACHE_DIR -ErrorAction SilentlyContinue
Remove-Item Env:FIELDES_RENDER_CACHE_DIR -ErrorAction SilentlyContinue
Remove-Item Env:FIELDES_RESULT_CACHE_DIR -ErrorAction SilentlyContinue
$env:FIELDES_AUTOMATION = "$sp\automation\$auto"
$t = Get-Date
$p = Start-Process -FilePath "$dist\FielDes.exe" -ArgumentList "`"$script`"" -WorkingDirectory $dist -PassThru
$p.WaitForExit($capSeconds * 1000) | Out-Null
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "killed after cap" } else { "finished after {0:N0} s" -f ((Get-Date) - $t).TotalSeconds }

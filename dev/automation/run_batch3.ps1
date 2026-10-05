$d = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
Set-Location 'C:\Users\tinmi\OneDrive\FielDes\FielDes'
"=== build"
powershell -NoProfile -ExecutionPolicy Bypass -File "$d\build_app.ps1" 2>&1 | Select-Object -Last 4
powershell -NoProfile -ExecutionPolicy Bypass -File "$d\deploy.ps1" 2>&1 | Select-Object -Last 1
$run = "$d\automation\run_beside.ps1"
$jobs = @(
  @('multi_demo.py', 'modes4',  'auto_modes1.txt',  260),
  @('multi_demo.py', 'multi5d', 'auto_multi5.txt',  200)
)
foreach ($j in $jobs) {
  "=== $($j[1])"
  & powershell -NoProfile -ExecutionPolicy Bypass -File $run -script "$d\tests\$($j[0])" -tag $j[1] -auto $j[2] -capSeconds $j[3] 2>&1 | Select-Object -Last 3
}
"ALL DONE"

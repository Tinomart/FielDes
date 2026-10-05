$d = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$run = "$d\automation\run_beside.ps1"
$jobs = @(
  @('multi_demo.py',  'modes2',    'auto_modes1.txt',          260),
  @('multi_demo.py',  'multi5b',   'auto_multi5b.txt',         200),
  @('ex08_holes.py',  'ex08final3','auto_ex08_holes_long.txt', 420)
)
foreach ($j in $jobs) {
  "=== $($j[1])"
  & powershell -NoProfile -ExecutionPolicy Bypass -File $run -script "$d\tests\$($j[0])" -tag $j[1] -auto $j[2] -capSeconds $j[3] 2>&1 | Select-Object -Last 4
}
"ALL DONE"

# the window tests of this round, one after another (each in its own window, alone, so that timing is not disturbed)
$d = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$run = "$d\automation\run_beside.ps1"
$jobs = @(
  @('ex08_holes.py',  'ex08final', 'auto_ex08_holes_long.txt', 420),
  @('multi_demo.py',  'mouse1',    'auto_mouse1.txt',          200),
  @('multi_demo.py',  'modes1',    'auto_modes1.txt',          250),
  @('multi_demo.py',  'delete1',   'auto_delete1.txt',         200),
  @('multi_demo.py',  'multi5',    'auto_multi5.txt',          200)
)
foreach ($j in $jobs) {
  "=== $($j[1])"
  & powershell -NoProfile -ExecutionPolicy Bypass -File $run -script "$d\tests\$($j[0])" -tag $j[1] -auto $j[2] -capSeconds $j[3] 2>&1 | Select-Object -Last 3
}
"ALL DONE"

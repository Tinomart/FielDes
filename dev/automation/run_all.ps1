# Every window test of the fields update, one after another (each in its own window, alone, so that timing is not disturbed)
$d = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$run = "$d\automation\run_auto.ps1"
$tour = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\app\examples\tour.py'
$jobs = @(
  @("$d\tests\win_tree.py",      'tree',      'auto_tree.txt',      200),
  @("$d\tests\win_chain.py",     'chain',     'auto_chain.txt',     100),
  @("$d\tests\win_fielddrop.py", 'fielddrop', 'auto_fielddrop.txt', 120),
  @("$d\tests\win_blocks.py",    'blocks',    'auto_blocks.txt',    200),
  @("$d\tests\win_nesting.py",   'nesting',   'auto_nesting.txt',   150),
  @("$d\tests\win_nesting.py",   'shadowdel', 'auto_shadowdel.txt', 100),
  @("$d\tests\win_nested.py",    'nested',    'auto_nested.txt',    150),
  @("$d\tests\win_shadow.py",    'shadow',    'auto_shadow.txt',    120),
  @("$d\tests\win_shadow.py",    'shadow2',   'auto_shadow2.txt',   90),
  @("$d\tests\win_shadow.py",    'shadow3',   'auto_shadow3.txt',   90),
  @("$d\tests\win_rename.py",    'rename',    'auto_rename.txt',    120),
  @("$d\tests\win_sections.py",  'sections',  'auto_sections.txt',  100),
  @("$d\tests\win_newmenus.py",  'newmenus',  'auto_newmenus.txt',  100),
  @($tour,                       'tour',      'auto_tour.txt',      150),
  @($tour,                       'tour2',     'auto_tour2.txt',     260),
  @($tour,                       'tour3',     'auto_tour3.txt',     220)
)
(Get-Content "$d\logs\blocks_test_folder\my_blocks.py" -Raw) -replace 'twice v2','twice v1' | Set-Content "$d\logs\blocks_test_folder\my_blocks.py" -NoNewline
$job = Start-Job -ScriptBlock { Start-Sleep -Seconds 305; $f = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev\logs\blocks_test_folder\my_blocks.py'; (Get-Content $f -Raw) -replace 'twice v1','twice v2' | Set-Content $f -NoNewline }
foreach ($j in $jobs) {
  "=== $($j[1])"
  & powershell -NoProfile -ExecutionPolicy Bypass -File $run -script $j[0] -tag $j[1] -auto $j[2] -capSeconds $j[3] 2>&1 | Select-String "finished|killed|automation: (no|drop|dragging)|automation\] "
}
Receive-Job $job -Wait | Out-Null
"ALL DONE"

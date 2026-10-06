# The window tests of the fields update, one after another (each in its own window, alone, so that timing is not disturbed)
$d = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev'
$run = "$d\automation\run_auto.ps1"
Remove-Item "$d\logs\tree_?.txt" -ErrorAction SilentlyContinue
$jobs = @(
  @('win_tree.py',      'tree',      'auto_tree.txt',      200),
  @('win_chain.py',     'chain',     'auto_chain.txt',     100),
  @('win_fielddrop.py', 'fielddrop', 'auto_fielddrop.txt', 120),
  @('win_blocks.py',    'blocks',    'auto_blocks.txt',    200)
)
# (the block file the watcher test changes: back to its first version)
(Get-Content "$d\logs\blocks_test_folder\my_blocks.py" -Raw) -replace 'twice v2','twice v1' | Set-Content "$d\logs\blocks_test_folder\my_blocks.py" -NoNewline
$job = Start-Job -ScriptBlock { Start-Sleep -Seconds 305; $f = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev\logs\blocks_test_folder\my_blocks.py'; (Get-Content $f -Raw) -replace 'twice v1','twice v2' | Set-Content $f -NoNewline }
foreach ($j in $jobs) {
  "=== $($j[1])"
  & powershell -NoProfile -ExecutionPolicy Bypass -File $run -script "$d\tests\$($j[0])" -tag $j[1] -auto $j[2] -capSeconds $j[3] 2>&1 | Select-String "finished|killed|automation: (no|drop|dragging)"
}
Receive-Job $job -Wait | Out-Null
"ALL DONE"

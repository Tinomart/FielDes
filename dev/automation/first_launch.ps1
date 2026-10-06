# What a user who has never run FielDes sees: the settings of FielDes are put away, the program is started without a file, its window is
# brought to the front and photographed a few seconds later; then the settings are put back exactly as they were (also when something fails).
#   first_launch.ps1 [-seconds 6] [-out <png>]
param([int]$seconds = 6, [string]$out = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dev\grabs\first_launch.png')
$dist = 'C:\Users\tinmi\OneDrive\FielDes\FielDes\dist\FielDes'
$bak = Join-Path $env:TEMP 'fieldes_settings_backup.reg'
$key = 'HKCU\Software\FielDes'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public class FD_Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
'@
Get-Process FielDes -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
$had = Test-Path 'HKCU:\Software\FielDes'
if ($had) { reg export $key $bak /y | Out-Null }
$p = $null
try {
    if ($had) { reg delete $key /f | Out-Null }
    # (no automation, no file: what a first start is)
    Remove-Item Env:FIELDES_AUTOMATION -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath "$dist\FielDes.exe" -WorkingDirectory $dist -PassThru
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) { $p.Refresh(); if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 300 }
    Start-Sleep -Seconds $seconds
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { throw "FielDes has no window" }
    [FD_Win]::ShowWindow($h, 9) | Out-Null                                         # (restored, not minimised)
    [FD_Win]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null        # (on top of everything, where it is)
    Start-Sleep -Milliseconds 1200
    $r = New-Object FD_Win+RECT
    [FD_Win]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hgt
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    "photographed the FielDes window ($w x $hgt) after $seconds s: $out"
}
finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Milliseconds 1200
    if (Test-Path 'HKCU:\Software\FielDes') { reg delete $key /f | Out-Null }
    if ($had) { reg import $bak | Out-Null; "settings put back from $bak" }
}

# A real mouse drag in the FielDes window (what a user does with the physical mouse): press at (x0, y0), move to (x1, y1) in small
# steps, release.  Coordinates are those of a grab of the window (the client area, device pixels).
# usage: real_drag.ps1 -delay <seconds> -x0 .. -y0 .. -x1 .. -y1 .. [-steps 25] [-hold 300] [-ctrl] [-clickx .. -clicky ..]
#   -ctrl            Ctrl is held from before the button goes down until after it comes up (a copy)
#   -clickx/-clicky  a plain click there first (to select a row, as a user does before dragging it)
param([int]$delay = 12, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [int]$steps = 25, [int]$hold = 300, [switch]$ctrl,
      [int]$clickx = -1, [int]$clicky = -1)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class In2 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    public struct RECT { public int L, T, R, B; }
    public struct POINT { public int X, Y; }
}
"@ -ErrorAction SilentlyContinue
[In2]::SetProcessDPIAware() | Out-Null
Start-Sleep -Seconds $delay
$p = Get-Process FielDes -ErrorAction Stop | Select-Object -First 1
$h = $p.MainWindowHandle
[In2]::ShowWindow($h, 5) | Out-Null
[In2]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [In2]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # (an Alt tap: lets this process take the foreground)
[In2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300
"foreground is FielDes: " + ([In2]::GetForegroundWindow() -eq $h)
Start-Sleep -Milliseconds 400
$r = New-Object In2+RECT
[In2]::GetClientRect($h, [ref]$r) | Out-Null
$o = New-Object In2+POINT
[In2]::ClientToScreen($h, [ref]$o) | Out-Null
"client {0}x{1} at {2},{3}" -f $r.R, $r.B, $o.X, $o.Y
function At($x, $y) { [In2]::SetCursorPos([int]($o.X + $x), [int]($o.Y + $y)) | Out-Null }
if ($clickx -ge 0) {
    At $clickx $clicky
    Start-Sleep -Milliseconds 300
    [In2]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [In2]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900
    "clicked"
}
At $x0 $y0
Start-Sleep -Milliseconds 300
if ($ctrl) { [In2]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150 }
[In2]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)         # left button down
Start-Sleep -Milliseconds 250
for ($i = 1; $i -le $steps; $i++) {
    At ($x0 + ($x1 - $x0) * $i / $steps) ($y0 + ($y1 - $y0) * $i / $steps)
    Start-Sleep -Milliseconds 30
}
Start-Sleep -Milliseconds $hold
[In2]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)         # left button up
Start-Sleep -Milliseconds 150
if ($ctrl) { [In2]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
"dragged"

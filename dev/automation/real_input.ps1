# Real mouse and keyboard input into the FielDes window (what a user does), for the tests of what the guided tour holds back.
# usage: real_input.ps1 -delay <seconds before it starts>
param([int]$delay = 12)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class In {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    public struct RECT { public int L, T, R, B; }
    public struct POINT { public int X, Y; }
}
"@ -ErrorAction SilentlyContinue
[In]::SetProcessDPIAware() | Out-Null
Start-Sleep -Seconds $delay
$p = Get-Process FielDes -ErrorAction Stop | Select-Object -First 1
$h = $p.MainWindowHandle
[In]::ShowWindow($h, 5) | Out-Null
[In]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [In]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # (an Alt tap: lets this process take the foreground)
[In]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300
"foreground is FielDes: " + ([In]::GetForegroundWindow() -eq $h)
Start-Sleep -Milliseconds 400
$r = New-Object In+RECT
[In]::GetClientRect($h, [ref]$r) | Out-Null
$o = New-Object In+POINT
[In]::ClientToScreen($h, [ref]$o) | Out-Null
$scale = 1.0          # (the grabs are of the client area at device pixel ratio 1)
"client {0}x{1} at {2},{3} scale {4}" -f $r.R, $r.B, $o.X, $o.Y, $scale
function At($x, $y) { [In]::SetCursorPos([int]($o.X + $x * $scale), [int]($o.Y + $y * $scale)) | Out-Null; Start-Sleep -Milliseconds 150 }
function Click($x, $y) { At $x $y; [In]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [In]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300 }
function Key($vk) { [In]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [In]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Chord($mod, $vk) { [In]::keybd_event($mod, 0, 0, [UIntPtr]::Zero); Key $vk; [In]::keybd_event($mod, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200 }

# 1. a click on the File menu, which the step holds back
Click 115 19
"clicked File"
Start-Sleep -Milliseconds 600
# 2. a click in the script, outside what the step is about, then typing
Click 300 600
foreach ($c in 0x51, 0x51, 0x51, 0x57) { Key $c }
"typed in the script"
# 3. Ctrl+N, Ctrl+O (a new script, the open dialog)
Chord 0x11 0x4E
Chord 0x11 0x4F
"ctrl keys"
Start-Sleep -Milliseconds 600
# 4. the viewport is what the step is about: a click there works, and a key does not
Click 1400 600
foreach ($c in 0x44, 0x44) { Key $c }                 # D (delete the selected models)
"viewport clicked and D pressed"
Start-Sleep -Milliseconds 400
# 5. the Next button of the card (Back, Next): by the keyboard Enter on the focused button is allowed, a click works
"done"

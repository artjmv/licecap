Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$src = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class W4 {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static List<string> Dump(uint pid) {
    var list = new List<string>();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint wpid; GetWindowThreadProcessId(h, out wpid);
      if (wpid == pid) {
        var t = new StringBuilder(256); GetWindowText(h, t, 256);
        var c = new StringBuilder(256); GetClassName(h, c, 256);
        list.Add(h.ToInt64() + "|" + c + "|" + t);
      }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return list;
  }
}
'@
Add-Type -TypeDefinition $src

$exe = "E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe"
$outdir = "E:\Proyectos\licecap\_uicheck"
$target = "E:\Proyectos\licecap\_uicheck\saved_test.gif"
Remove-Item $target -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
$h = $p.MainWindowHandle
[W4]::SetWindowPos($h, [IntPtr](-1), 80, 60, 512, 512, 0x0004) | Out-Null
[W4]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600
"pid=$($p.Id) hwnd=$h"

# ---- arrow click opens Rec menu (should NOT start recording) ----
$rec = [W4]::GetDlgItem($h, 1001)
$rr = New-Object W4+RECT
[W4]::GetWindowRect($rec, [ref]$rr) | Out-Null
[W4]::SetForegroundWindow($h) | Out-Null
[W4]::SetCursorPos($rr.Right - 8, [int](($rr.Top + $rr.Bottom)/2)) | Out-Null
Start-Sleep -Milliseconds 250
[W4]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 80
[W4]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 900
$bmp = New-Object System.Drawing.Bitmap 760, 700
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(40, 20, 0, 0, $bmp.Size)
$bmp.Save("$outdir\07_rec_menu.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
$p.Refresh()
"after arrow click title: " + $p.MainWindowTitle
[W4]::PostMessage($h, 0x0100, [IntPtr]27, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500

# ---- record for ~3.5 s then stop ----
[W4]::SendMessage($rec, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 4
[W4]::SendMessage($rec, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
"temp gifs:"
$tmp = Get-ChildItem "$env:TEMP\LICEcap_*.gif" -ErrorAction SilentlyContinue
$tmp | ForEach-Object { $_.FullName + "  " + $_.Length }

# ---- Save As via the file dialog ----
[W4]::PostMessage($h, 0x0111, [IntPtr]40040, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1500
$fd = [IntPtr]::Zero
foreach ($line in [W4]::Dump([uint32]$p.Id)) { if ($line -match "Save the animation as") { $fd = [IntPtr][int64](($line -split "\|")[0]) } }
"file dialog: $fd"
if ($fd -ne [IntPtr]::Zero) {
  $edt = [W4]::FindWindowEx($fd, [IntPtr]::Zero, "Edit", $null)
  "edit ctrl: $edt"
  if ($edt -ne [IntPtr]::Zero) {
    $sb = New-Object System.Text.StringBuilder
    [W4]::SendMessage($edt, 0x000C, [IntPtr]::Zero, $sb) | Out-Null
    "$([W4]::SendMessage($edt, 0x000D, [IntPtr]$target.Length, $target))"
    Start-Sleep -Milliseconds 300
  }
  [W4]::PostMessage($fd, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 1200
}
"target exists: " + (Test-Path $target)
if (Test-Path $target) {
  $bytes = [System.IO.File]::ReadAllBytes($target)
  "size: $($bytes.Length) header: " + [System.Text.Encoding]::ASCII.GetString($bytes[0..5])
}
"temp gifs after save:"
Get-ChildItem "$env:TEMP\LICEcap_*.gif" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }

# ---- screenshots: saved state + toolbar ----
$r = New-Object W4+RECT
[W4]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.Right-$r.Left), ($r.Bottom-$r.Top)
$g = [System.Drawing.Graphics]::FromImage($bmp)
[W4]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save("$outdir\08_saved.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

[W4]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
if (!$p.HasExited) { $p.Kill() }
"done"

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$src = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class W3 {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static List<string> Dump(uint pid, bool children, IntPtr parent) {
    var list = new List<string>();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint wpid; GetWindowThreadProcessId(h, out wpid);
      if (wpid == pid) {
        var t = new StringBuilder(256); GetWindowText(h, t, 256);
        var c = new StringBuilder(256); GetClassName(h, c, 256);
        list.Add(h.ToInt64() + " | " + c + " | " + t);
      }
      return true;
    };
    if (children) EnumChildWindows(parent, cb, IntPtr.Zero); else EnumWindows(cb, IntPtr.Zero);
    return list;
  }
}
'@
Add-Type -TypeDefinition $src

$exe = "E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe"
$outdir = "E:\Proyectos\licecap\_uicheck"
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
$h = $p.MainWindowHandle
"pid=$($p.Id) hwnd=$h"
[W3]::SetWindowPos($h, [IntPtr](-1), 80, 60, 512, 512, 0x0004) | Out-Null
[W3]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600

"--- children before ---"
[W3]::Dump([uint32]$p.Id, $true, $h) | ForEach-Object { $_ }

[W3]::PostMessage($h, 0x0111, [IntPtr]40021, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1500
"--- top-level windows of process ---"
[W3]::Dump([uint32]$p.Id, $false, [IntPtr]::Zero) | ForEach-Object { $_ }

$dlg = [IntPtr]::Zero
foreach ($line in [W3]::Dump([uint32]$p.Id, $false, [IntPtr]::Zero)) {
  if ($line -match "\| #32770 \| Customization") { $dlg = [IntPtr][int64]($line -split " \| ")[0] }
}
"dlg=$dlg"
if ($dlg -ne [IntPtr]::Zero) {
  $r = New-Object W3+RECT
  [W3]::GetWindowRect($dlg, [ref]$r) | Out-Null
  $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $hgt
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  [W3]::SetForegroundWindow($dlg) | Out-Null
  Start-Sleep -Milliseconds 400
  $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
  $bmp.Save("$outdir\05_customization.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "saved customization ${w}x${hgt}"
  [W3]::PostMessage($dlg, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 500
}

# --- test the Rec drop-down arrow with a real click ---
$rec = [W3]::GetDlgItem($h, 1001)
$rr = New-Object W3+RECT
[W3]::GetWindowRect($rec, [ref]$rr) | Out-Null
$ax = $rr.Right - 8
$ay = [int](($rr.Top + $rr.Bottom) / 2)
[W3]::SetForegroundWindow($h) | Out-Null
[W3]::SetCursorPos($ax, $ay) | Out-Null
Start-Sleep -Milliseconds 250
[W3]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 80
[W3]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
$bmp2 = New-Object System.Drawing.Bitmap 700, 620
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.CopyFromScreen(60, 40, 0, 0, $bmp2.Size)
$bmp2.Save("$outdir\06_rec_menu.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g2.Dispose(); $bmp2.Dispose()
"menu shot saved"
Start-Sleep -Milliseconds 300
[W3]::PostMessage($h, 0x0100, [IntPtr]27, [IntPtr]::Zero) | Out-Null   # ESC
Start-Sleep -Milliseconds 300
[W3]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
if (!$p.HasExited) { $p.Kill() }
"done"

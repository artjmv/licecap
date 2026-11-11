Add-Type -AssemblyName System.Windows.Forms, System.Drawing

$src = @'
using System;
using System.Runtime.InteropServices;
public class W2 {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
Add-Type -TypeDefinition $src

$HWND_TOPMOST = [IntPtr](-1)
$SWP = 0x0040 -bor 0x0001 -bor 0x0002 -bor 0x0010  # SHOWWINDOW|NOSIZE? (we pass size)
$exe = "E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe"
$outdir = "E:\Proyectos\licecap\_uicheck"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null
Get-ChildItem "$outdir\*.png" | Remove-Item -Force

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
$h = $p.MainWindowHandle
"main hwnd: $h title: " + $p.MainWindowTitle
[W2]::SetWindowPos($h, $HWND_TOPMOST, 80, 60, 512, 512, 0x0004) | Out-Null   # NOSIZE off => resize
[W2]::ShowWindow($h, 5) | Out-Null
[W2]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

function Shot($hwnd, $name) {
  [W2]::SetWindowPos($hwnd, $HWND_TOPMOST, 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010) | Out-Null
  [W2]::SetForegroundWindow($hwnd) | Out-Null
  Start-Sleep -Milliseconds 500
  $r = New-Object W2+RECT
  [W2]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $hgt
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
  $bmp.Save("$outdir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "$name : ${w}x${hgt}"
}

Shot $h "01_idle"

$rec = [W2]::GetDlgItem($h, 1001)
[W2]::SendMessage($rec, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # BM_CLICK
Start-Sleep -Milliseconds 1200
Shot $h "02_recording"
Start-Sleep -Seconds 2
Shot $h "03_recording2"
[W2]::SendMessage($rec, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # stop
Start-Sleep -Milliseconds 900
Shot $h "04_stopped"
"temp gifs:"
Get-ChildItem "$env:TEMP\LICEcap_*.gif" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName + "  " + $_.Length }

[W2]::PostMessage($h, 0x0111, [IntPtr]40021, [IntPtr]::Zero) | Out-Null      # IDM_CUSTOMIZE
Start-Sleep -Milliseconds 1200
$dlg = [W2]::FindWindow($null, "Customization")
"customization hwnd: $dlg"
if ($dlg -ne [IntPtr]::Zero) {
  Shot $dlg "05_customization"
  [W2]::PostMessage($dlg, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null
}
Start-Sleep -Milliseconds 600

[W2]::PostMessage($h, 0x0111, [IntPtr]40001, [IntPtr]::Zero) | Out-Null      # IDM_NEW (discard)
Start-Sleep -Milliseconds 400
[W2]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null     # WM_CLOSE
Start-Sleep -Milliseconds 1200
if (!$p.HasExited) { $p.Kill() }
"exited: " + $p.HasExited

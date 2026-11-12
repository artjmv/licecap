# _uicheck4.ps1 -- verificacion del tema oscuro/claro de la UI de Windows.
#
# Lanza el Release x64, hace capturas de la ventana principal, del menu
# desplegable de Rec y del dialogo Customization en tema oscuro, y luego
# cambia a tema claro (menu Edit > Dark Theme) y captura otra vez.
#
# Las imagenes van a _uicheck\*.png (ignorado por git). Usa un licecap.ini
# local junto al .exe para no tocar la configuracion real del usuario
# (%APPDATA%\licecap.ini) y lo borra al terminar.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WTheme {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static IntPtr Find(uint pid, string title) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (w == pid && t.ToString().Contains(title)) { found = h; return false; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
  }
}
'@

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $root 'licecap\licecap\x64\Release\licecap.exe'
$ini  = Join-Path $root 'licecap\licecap\x64\Release\licecap.ini'
$out  = Join-Path $root '_uicheck'
$IDC_REC = 1001
$IDM_CUSTOMIZE = 40021
$IDM_DARK_THEME = 40061

if (-not (Test-Path $exe)) { throw "no existe $exe (compila antes con MSBuild)" }
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Shot([IntPtr]$hwnd, [string]$path, [int]$padRight = 0, [int]$padBottom = 0) {
  $r = New-Object WTheme+RECT
  [WTheme]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  $w = ($r.R - $r.L) + $padRight
  $h = ($r.B - $r.T) + $padBottom
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  ${w}x${h}"
}

# SetForegroundWindow() from a background process is often ignored, so the
# window is activated with a click on its own title bar (the whole client area
# drags the window).
function Activate([IntPtr]$hwnd) {
  $r = New-Object WTheme+RECT
  [WTheme]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  [WTheme]::SetCursorPos($r.L + 60, $r.T + 10) | Out-Null
  Start-Sleep -Milliseconds 200
  [WTheme]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [WTheme]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
}

function ClickButton([IntPtr]$hwnd, [int]$id, [int]$offsetFromRight = -1) {
  $b = [WTheme]::GetDlgItem($hwnd, $id)
  $r = New-Object WTheme+RECT
  [WTheme]::GetWindowRect($b, [ref]$r) | Out-Null
  $x = if ($offsetFromRight -lt 0) { [int](($r.L + $r.R) / 2) } else { $r.R + $offsetFromRight }
  [WTheme]::SetCursorPos($x, [int](($r.T + $r.B) / 2)) | Out-Null
  Start-Sleep -Milliseconds 250
  [WTheme]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [WTheme]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 800
}

Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Set-Content -Path $ini -Value "[licecap]`r`ndark=1`r`nmaxfps=10`r`nwnd_r=140 120 570 520`r`n" -NoNewline

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
$h = $p.MainWindowHandle
# topmost so the screenshots do not depend on what else is on screen
[WTheme]::SetWindowPos($h, [IntPtr](-1), 140, 120, 430, 400, 0x0000) | Out-Null
[WTheme]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
Activate $h

"capturas:"
Shot $h (Join-Path $out '10_dark_main.png')

# menu desplegable de Rec (zona de la flecha)
ClickButton $h $IDC_REC -8
Shot $h (Join-Path $out '11_dark_menu.png') 320 320
[WTheme]::PostMessage($h, 0x0111, [IntPtr]$IDM_CUSTOMIZE, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
$dlg = [WTheme]::Find([uint32]$p.Id, 'Customization')
if ($dlg -ne [IntPtr]::Zero) {
  Start-Sleep -Milliseconds 600
  Shot $dlg (Join-Path $out '12_dark_customization.png')
  [WTheme]::PostMessage($dlg, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null   # IDCANCEL
  Start-Sleep -Milliseconds 500
} else { "  (no aparecio el dialogo Customization)" }

# cambio a tema claro y captura
[WTheme]::PostMessage($h, 0x0111, [IntPtr]$IDM_DARK_THEME, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
Activate $h
Shot $h (Join-Path $out '13_light_main.png')

$p.Refresh()
"tema guardado en licecap.ini: " + ((Get-Content $ini) -join ' | ')
Remove-Item $ini -ErrorAction SilentlyContinue
Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
"ok"

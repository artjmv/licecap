# _uicheck6.ps1 -- verificacion del preset 1024x1024 y del modo "Half Size" (/).
#
# 1. Arranca el Release x64 con un licecap.ini local (sin tocar la config real).
# 2. Abre el menu Presets y captura el desplegable completo: debe aparecer la
#    entrada "Square 1024 x 1024" al final y el item "Half Size (/)".
# 3. Aplica ese preset por WM_COMMAND y comprueba el tamano real de captura.
# 4. Manda "/" (WM_KEYDOWN, lo ve el hook de la app) y comprueba que el area
#    queda a la mitad, y que el status lo marca como "Square (/2)".
# 5. Vuelve a abrir el menu (tamanos a la mitad y "Half Size" marcado) y manda
#    "/" otra vez: el tamano original tiene que volver.
#
# Las imagenes van a _uicheck\*.png (ignorado por git) y el ini local se borra
# al terminar. El ESC se manda como mucho mientras el popup sigue abierto: un
# ESC de mas llegaria a la ventana principal y la cerraria.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WHalf {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern short VkKeyScanA(byte ch);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetClassNameA(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static List<IntPtr> Menus(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint wpid; GetWindowThreadProcessId(h, out wpid);
      if (wpid != pid || !IsWindowVisible(h)) return true;
      var sb = new StringBuilder(64);
      GetClassNameA(h, sb, 64);
      if (sb.ToString() == "#32768") list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
'@

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
# Por defecto el Release x64; LICECAP_EXE permite apuntar a otra copia (p.ej. dist).
$src  = if ($env:LICECAP_EXE) { $env:LICECAP_EXE } else { Join-Path $root 'licecap\licecap\x64\Release\licecap.exe' }
$out  = Join-Path $root '_uicheck'
# Se corre una COPIA con otro nombre de exe: otros scripts de verificacion del
# repo matan todo proceso "licecap" y arruinarian esta prueba a mitad de camino.
$exe  = Join-Path $out 'licecap_halfcheck.exe'
$ini  = Join-Path $out 'licecap.ini'   # la app usa <carpeta del exe>\licecap.ini

$IDC_STATUS       = 1003
$IDC_VIEWRECT     = 1006
$IDC_PRESETS      = 1049
$IDM_PRESET_FIRST = 40100
$PRESET_SQUARE    = 17   # indice de "Square 1024 x 1024"

if (-not (Test-Path $src)) { throw "no existe $src (compila antes con MSBuild)" }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-ChildItem "$out\p*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-Process licecap_halfcheck -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item $src $exe -Force

function Shot([IntPtr]$hwnd, [string]$path, [int]$padRight = 0, [int]$padBottom = 0) {
  $r = New-Object WHalf+RECT
  [WHalf]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  $w = ($r.R - $r.L) + $padRight
  $h = ($r.B - $r.T) + $padBottom
  if ($w -le 0 -or $h -le 0) { "  $([System.IO.Path]::GetFileName($path))  ventana no valida"; return }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  ${w}x${h}"
}

function CaptureSize([IntPtr]$hwnd) {
  $v = [WHalf]::GetDlgItem($hwnd, $IDC_VIEWRECT)
  $r = New-Object WHalf+RECT
  if (-not [WHalf]::GetWindowRect($v, [ref]$r)) { return '?' }
  return "$(($r.R - $r.L) - 2) x $(([math]::Abs($r.B - $r.T)) - 2)"
}

function StatusText([IntPtr]$hwnd) {
  $sb = New-Object System.Text.StringBuilder 512
  [WHalf]::GetWindowTextA([WHalf]::GetDlgItem($hwnd, $IDC_STATUS), $sb, 512) | Out-Null
  return ($sb.ToString() -replace "`r?`n", ' | ')
}

function Activate([IntPtr]$hwnd) {
  $r = New-Object WHalf+RECT
  [WHalf]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  [WHalf]::SetForegroundWindow($hwnd) | Out-Null
  [WHalf]::SetCursorPos($r.L + 60, $r.T + 10) | Out-Null
  Start-Sleep -Milliseconds 200
  [WHalf]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [WHalf]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function OpenPresetMenu([IntPtr]$hwnd, [int]$pid_ = 0) {
  $b = [WHalf]::GetDlgItem($hwnd, $IDC_PRESETS)
  $r = New-Object WHalf+RECT
  [WHalf]::GetWindowRect($b, [ref]$r) | Out-Null
  for ($i = 0; $i -lt 3; $i++) {
    [WHalf]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)) | Out-Null
    Start-Sleep -Milliseconds 250
    [WHalf]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [WHalf]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    if ([WHalf]::Menus([uint32]$pid_).Count -gt 0) { return }
    '  (el menu no se abrio, reintento)'
  }
}

function PressSlash([IntPtr]$hwnd) {
  # WM_KEYDOWN por PostMessage: el hook WH_GETMESSAGE de la app lo ve igual que
  # un tecleo real y el resultado no depende de quien tenga el foco.
  [WHalf]::PostMessage($hwnd, 0x0100, [IntPtr]0xBF, [IntPtr]::Zero) | Out-Null   # VK_OEM_2 = "/"
  Start-Sleep -Milliseconds 700
}

function CloseMenu([IntPtr]$hwnd, [int]$pid_ = 0) {
  # Primero WM_CANCELMODE sobre el popup; si sigue abierto, un ESC (solo con el
  # menu confirmado abierto: un ESC de mas cerraria la ventana principal).
  for ($i = 0; $i -lt 4; $i++) {
    $menus = [WHalf]::Menus([uint32]$pid_)
    if ($menus.Count -eq 0) { break }
    if ($i -eq 0) {
      foreach ($mnu in $menus) { [WHalf]::PostMessage($mnu, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    } else {
      [WHalf]::keybd_event(0x1B, 0, 0, [IntPtr]::Zero)
      Start-Sleep -Milliseconds 120
      [WHalf]::keybd_event(0x1B, 0, 2, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 600
  }
  if ([WHalf]::Menus([uint32]$pid_).Count -ne 0) { '  (el menu sigue abierto!)' }
  if (-not [WHalf]::IsWindow($hwnd)) { '  (la ventana principal ya no existe!)' }
}

$lines = @(
  '[licecap]',
  'dark=1',
  'maxfps=10',
  'cursor=1',
  'wnd_r=140 80 570 520',
  ''
)
Get-Process licecap_halfcheck -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Set-Content -Path $ini -Value ($lines -join "`r`n") -NoNewline

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
$h = $p.MainWindowHandle
[WHalf]::SetWindowPos($h, [IntPtr](-1), 140, 30, 430, 440, 0x0000) | Out-Null
[WHalf]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
Activate $h

"inicio: $((CaptureSize $h))  status: $((StatusText $h))"

"menu Presets (completo):"
OpenPresetMenu $h $p.Id
Shot $h (Join-Path $out 'p7_full_menu.png') 150 460
CloseMenu $h $p.Id
[WHalf]::SetCursorPos(30, 30) | Out-Null
"  app viva: $([WHalf]::IsWindow($h))"

"aplico preset Square 1024x1024 (id $($IDM_PRESET_FIRST + $PRESET_SQUARE)):"
[WHalf]::PostMessage($h, 0x0111, [IntPtr]($IDM_PRESET_FIRST + $PRESET_SQUARE), [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
"  captura: $((CaptureSize $h))"
"  status:  $((StatusText $h))"
Shot $h (Join-Path $out 'p8_1024.png')

"tecla / (Half Size):"
Activate $h
$i = 0
while ((CaptureSize $h) -ne '512 x 512' -and $i -lt 3) { PressSlash $h; $i++; "  intento $i -> $((CaptureSize $h))" }
"  captura: $((CaptureSize $h))"
"  status:  $((StatusText $h))"
Shot $h (Join-Path $out 'p9_half.png')

"menu Presets a la mitad:"
OpenPresetMenu $h $p.Id
Shot $h (Join-Path $out 'p10_half_menu.png') 150 460
CloseMenu $h $p.Id
[WHalf]::SetCursorPos(30, 30) | Out-Null
"  app viva: $([WHalf]::IsWindow($h))"

"tecla / otra vez (vuelve al tamano completo):"
$i = 0
while ((CaptureSize $h) -ne '1024 x 1024' -and $i -lt 3) { PressSlash $h; $i++; "  intento $i -> $((CaptureSize $h))" }
"  captura: $((CaptureSize $h))"
"  status:  $((StatusText $h))"
Shot $h (Join-Path $out 'p11_restored.png')

# --- mismo atajo con el layout activo (es-ES/es-CR: "/" es Shift+7) ---
$scan = [WHalf]::VkKeyScanA([byte][char]'/')
$vkSlash = $scan -band 0xFF
$needShift = (($scan -shr 8) -band 1) -ne 0
"tecla / segun el layout activo: VK=0x$('{0:X2}' -f $vkSlash) shift=$needShift"
if ($needShift) { [WHalf]::keybd_event(0x10, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 150 }
[WHalf]::PostMessage($h, 0x0100, [IntPtr]$vkSlash, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 700
if ($needShift) { [WHalf]::keybd_event(0x10, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 150 }
"  captura: $((CaptureSize $h))"
"  status:  $((StatusText $h))"
Shot $h (Join-Path $out 'p12_layout_slash.png')

Get-Process licecap_halfcheck -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item $ini -ErrorAction SilentlyContinue
Remove-Item $exe -ErrorAction SilentlyContinue
"listo"
# _uicheck11.ps1 -- desplegable de bookmarks debajo de Save (fila fija).
#
# Corre una COPIA del Release x64 en _dropchk\bin (con su propio licecap.ini),
# asi el ini real del usuario y otras instancias no interfieren.
#
#   1. la fila esta justo debajo de Save (mismo borde derecho, 18px de alto)
#   2. clic REAL en la fila -> se abre la lista de bookmarks debajo de la fila,
#      alineada a su borde derecho (61_dropdown_open.png)
#   3. sin grabacion pendiente, clic en la fila 3 (tres): el panel se cierra,
#      el ini guarda last=<tres> y la fila lo pinta (62_after_pick.png)
#   4. reinicio: la seleccion sigue en tres (63_after_restart.png)
#   5. con grabacion pendiente, Enter guarda en el bookmark seleccionado y el
#      gif aparece en esa carpeta
#   6. grabar otra vez y elegir la fila 1 con el raton: guarda en uno
#   7. quitar el bookmark seleccionado con el boton "-" del panel: la seleccion
#      pasa al vecino y el ini lo refleja
#   8. la flechita de Save sigue abriendo la misma lista (comportamiento viejo)
#
# Uso: powershell -ExecutionPolicy Bypass -File _uicheck11.ps1

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WDp {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static IntPtr FindClass(uint pid, string cls) {    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      if (w == pid && c.ToString() == cls) { found = h; return false; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
  }
  public static string Titles(uint pid) {
    var sb = new StringBuilder();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      if (w == pid && IsWindowVisible(h)) sb.Append("[" + c.ToString() + ":" + t.ToString() + "]");
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return sb.ToString();
  }
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (w == pid && IsWindowVisible(h) && t.ToString().Length > 0) { found = h; return false; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
  }
}
'@
[void][WDp]::SetProcessDPIAware()

$root   = Split-Path -Parent $MyInvocation.MyCommand.Path
$src    = Join-Path $root 'licecap\licecap\x64\Release\licecap.exe'
$out    = Join-Path $root '_uicheck'
$drop   = Join-Path $root '_dropchk'
$bin    = Join-Path $drop 'bin'
$exe    = Join-Path $bin 'licecap.exe'
$ini    = Join-Path $bin 'licecap.ini'
$uno    = Join-Path $drop 'uno'
$dos    = Join-Path $drop 'dos'
$tres   = Join-Path $drop 'tres'

$IDC_REC = 1001; $IDC_STOP = 1002; $IDC_SAVE = 1027
$IDC_DROP = 1054
$IDM_BOOKMARKS_PANEL = 40362

if (-not (Test-Path $src)) { throw "no existe $src (compila antes con MSBuild)" }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 20 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
Remove-Item -Recurse -Force $drop -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $bin, $uno, $dos, $tres | Out-Null
for ($i = 0; $i -lt 40; $i++) {
  try { Copy-Item $src $exe -Force -ErrorAction Stop; break }
  catch { Start-Sleep -Milliseconds 300 }
}
if (-not (Test-Path $exe)) { throw "no pude copiar el exe a $exe" }
"exe: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,12))"

$fail = 0
function Check([bool]$ok, [string]$msg) {
  if ($ok) { "  OK: $msg" } else { "  FALLO: $msg"; $script:fail++ }
}

function Shot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object WDp+RECT
  if (-not [WDp]::GetWindowRect($hwnd, [ref]$r)) { "  (ventana no valida)"; return }
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  $($bmp.Width)x$($bmp.Height)"
}

function ShotWin([IntPtr]$hwnd, [string]$path, [IntPtr]$owner, [int]$pad = 8) {
  $r = New-Object WDp+RECT
  [WDp]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  $o = New-Object WDp+RECT
  [WDp]::GetWindowRect($owner, [ref]$o) | Out-Null
  $l = [Math]::Min($r.L - $pad, $o.L)
  $t = [Math]::Min($r.T - $pad, $o.T)
  $rr = [Math]::Max($r.R + $pad, $o.R)
  $b = [Math]::Max($r.B + $pad, $o.B)
  $bmp = New-Object System.Drawing.Bitmap ($rr - $l), ($b - $t)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($l, $t, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  $($bmp.Width)x$($bmp.Height)"
}

function RefH {
  for ($i = 0; $i -lt 15; $i++) {
    $h = [WDp]::Find([uint32]$script:proc.Id)
    if ($h -ne [IntPtr]::Zero) { return $h }
    Start-Sleep -Milliseconds 200
  }
  return [IntPtr]::Zero
}

function Activate([IntPtr]$hwnd) {
  $r = New-Object WDp+RECT
  [WDp]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  [WDp]::SetCursorPos($r.L + 60, $r.T + 10) | Out-Null
  Start-Sleep -Milliseconds 200
  [WDp]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [WDp]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function IniNow {
  for ($i = 0; $i -lt 15; $i++) {
    if (Test-Path $ini) { return (Get-Content $ini -Raw) }
    Start-Sleep -Milliseconds 200
  }
  return ''
}

function Start-App([bool]$reseed) {
  Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
  for ($i = 0; $i -lt 25 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
  if ($reseed) {
    $lines = @(
      '[licecap]',
      'dark=1',
      'maxfps=10',
      'cursor=1',
      'wnd_r=140 120 570 560',
      '',
      '[bookmarks]',
      'count=3',
      ('b1=' + $uno),
      ('b2=' + $dos),
      ('b3=' + $tres),
      ''
    )
    Set-Content -Path $ini -Value ($lines -join "`r`n") -NoNewline
  }
  for ($try = 1; $try -le 3; $try++) {
    $script:proc = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru
    Start-Sleep -Seconds 2
    if ($script:proc.HasExited) { continue }
    $script:h = RefH
    if ($script:h -ne [IntPtr]::Zero) { break }
    $script:proc | Stop-Process -Force
    Start-Sleep -Milliseconds 500
  }
  if ($script:h -eq [IntPtr]::Zero) { throw "no encontre la ventana de licecap" }
  [WDp]::SetWindowPos($script:h, [IntPtr](-1), 140, 120, 430, 440, 0x0000) | Out-Null
  Start-Sleep -Milliseconds 400
  $script:h = RefH
  Activate $script:h
  $script:h = RefH
  if ($script:h -eq [IntPtr]::Zero) { throw "sin ventana tras activar" }
}

function Rect([IntPtr]$hwnd) {
  $r = New-Object WDp+RECT
  [WDp]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  return $r
}

# clic real (down+up) en coordenadas de pantalla
function RealClick([int]$x, [int]$y) {
  [WDp]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 250
  [WDp]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [WDp]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Panel {
  for ($i = 0; $i -lt 15; $i++) {
    $p = [WDp]::FindClass([uint32]$script:proc.Id, 'LiceBookmarkPanel')
    if ($p -ne [IntPtr]::Zero) { return $p }
    Start-Sleep -Milliseconds 200
  }
  return [IntPtr]::Zero
}

# Cerrar implica volcar el ini a disco: se espera a que la ventana desaparezca
# en vez de mirar una sola vez justo despues del clic.
function WaitPanelGone([int]$ms = 5000) {
  for ($i = 0; $i -lt [int]($ms / 100); $i++) {
    if (([WDp]::FindClass([uint32]$script:proc.Id, 'LiceBookmarkPanel')) -eq [IntPtr]::Zero) { return $true }
    Start-Sleep -Milliseconds 100
  }
  return $false
}

function Titles {
  return [WDp]::Titles([uint32]$script:proc.Id)
}

function WaitPending([IntPtr]$hwnd, [int]$ms = 4000) {
  for ($i = 0; $i -lt [int]($ms / 200); $i++) {
    if (IsPending $hwnd) { return $true }
    Start-Sleep -Milliseconds 200
  }
  return $false
}

function IsPending([IntPtr]$hwnd) {
  $sb = New-Object System.Text.StringBuilder 256
  [WDp]::GetWindowText($hwnd, $sb, 256) | Out-Null
  return ($sb.ToString() -like '*not saved*')
}

# fila i (0..) del panel, en unidades de UI: pad 6 + titulo 18 + 4 + fila*24 + 12
function ClickRow([IntPtr]$panel, [int]$row, [double]$scale) {
  $x = [int][Math]::Round(15 * $scale)
  $y = [int][Math]::Round((6 + 18 + 4 + $row * 24 + 12) * $scale)
  [WDp]::PostMessage($panel, 0x0201, [IntPtr]1, [IntPtr](($y -shl 16) -bor $x)) | Out-Null
  Start-Sleep -Milliseconds 800
}

# ---------------------------------------------------------------------------
"fase visual:"
Start-App $true
$h = RefH
$sr = Rect ([WDp]::GetDlgItem($h, $IDC_SAVE))
$dr = Rect ([WDp]::GetDlgItem($h, $IDC_DROP))
$scale = ($sr.B - $sr.T) / 26.0
"  escala deducida del boton Save: $scale"
Check ($dr.B -gt $dr.T) "existe la fila desplegable (IDC_BOOKMARKDROP)"
Check ($dr.T -ge $sr.B - 1 -and $dr.T -le $sr.B + [int](6 * $scale)) ("la fila esta pegada debajo de Save (fila top=" + $dr.T + ", save bottom=" + $sr.B + ")")
Check ([Math]::Abs($dr.R - $sr.R) -le 1) ("mismo borde derecho que Save (fila right=" + $dr.R + ", save right=" + $sr.R + ")")
Check ([Math]::Abs(($dr.B - $dr.T) - [int](18 * $scale)) -le 2) ("alto compacto de 18px (alto=" + ($dr.B - $dr.T) + ")")
Shot $h (Join-Path $out '60_main_dropdown.png')

"clic real en la fila:"
$panel = [IntPtr]::Zero
for ($try = 1; $try -le 3 -and $panel -eq [IntPtr]::Zero; $try++) {
  # otra ventana puede quitarle el primer plano: se reactiva y se reintenta
  Activate (RefH)
  $h = RefH
  $dr = Rect ([WDp]::GetDlgItem($h, $IDC_DROP))
  $cx = [int](($dr.L + $dr.R) / 2)
  $cy = [int](($dr.T + $dr.B) / 2)
  RealClick $cx $cy
  $panel = Panel
  if ($panel -eq [IntPtr]::Zero) { "  (intento ${try}: sin lista, la ventana no estaba al frente; reintento)" }
}
Check ($panel -ne [IntPtr]::Zero) "el clic en la fila abre la lista de bookmarks"
if ($panel -ne [IntPtr]::Zero) {
  $pr = Rect $panel
  Check ($pr.T -ge $dr.B - 2 -and $pr.T -le $dr.B + [int](6 * $scale)) ("la lista sale debajo de la fila (lista top=" + $pr.T + ", fila bottom=" + $dr.B + ")")
  Check ([Math]::Abs($pr.R - $dr.R) -le 2) ("lista alineada al borde derecho de la fila (lista right=" + $pr.R + ", fila right=" + $dr.R + ")")
  ShotWin $panel (Join-Path $out '61_dropdown_open.png') $h
  "seleccion inicial (ultimo usado) y cambio a la fila 3 sin grabacion pendiente:"
  Check (-not (IsPending $h)) "no hay grabacion pendiente al elegir carpeta"
  ClickRow $panel 2 $scale
  Check (WaitPanelGone) "elegir una fila cierra la lista"
  $txt = IniNow
  Check ($txt -match ('last=' + [regex]::Escape($tres))) "el ini recuerda la carpeta elegida (last=tres)"
  Start-Sleep -Milliseconds 300
  Shot $h (Join-Path $out '62_after_pick.png')
}

"reinicio:"
[WDp]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
for ($i = 0; $i -lt 25 -and -not $proc.HasExited; $i++) { Start-Sleep -Milliseconds 200; $proc.Refresh() }
Check ($proc.HasExited) "la app cerro con WM_CLOSE"
$txt = IniNow
Check ($txt -match ('last=' + [regex]::Escape($tres))) "la seleccion sobrevivio al cierre"
Start-App $false
$h = RefH
Shot $h (Join-Path $out '63_after_restart.png')
"tras reiniciar, la lista abre con el ultimo usado (tres) marcado:"
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_DROP, [IntPtr]::Zero) | Out-Null
$panel = Panel
Check ($panel -ne [IntPtr]::Zero) "la fila sigue abriendo la lista tras reiniciar"
if ($panel -ne [IntPtr]::Zero) {
  ShotWin $panel (Join-Path $out '63b_restart_selected.png') $h
  Check (-not (IsPending $h)) "sigue sin grabacion pendiente"
  [WDp]::PostMessage($panel, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null   # ESC
  Start-Sleep -Milliseconds 400
}

# ---------------------------------------------------------------------------
"grabar 1.2s y guardar con Enter en el bookmark seleccionado (tres):"
[WDp]::SetCursorPos(5, 5) | Out-Null
Start-Sleep -Milliseconds 200
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_REC, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1200
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_STOP, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$h = RefH
Check (WaitPending $h) "la grabacion quedo pendiente"
$h = RefH
$dr = Rect ([WDp]::GetDlgItem($h, $IDC_DROP))
RealClick ([int](($dr.L + $dr.R) / 2)) ([int](($dr.T + $dr.B) / 2))
$panel = Panel
Check ($panel -ne [IntPtr]::Zero) "con grabacion pendiente la fila abre la lista"
if ($panel -ne [IntPtr]::Zero) {
  ShotWin $panel (Join-Path $out '64_pending.png') $h
  [WDp]::PostMessage($panel, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero) | Out-Null   # Enter
  Start-Sleep -Milliseconds 1200
}
Check (WaitPanelGone) "Enter guardo y cerro la lista"
$saved = @(Get-ChildItem $tres -File -Filter *.gif -ErrorAction SilentlyContinue)
Check ($saved.Count -ge 1) ("el gif se guardo en el bookmark seleccionado (tres): " + (($saved | ForEach-Object { $_.Name }) -join ', '))
Shot $h (Join-Path $out '65_saved_to_tres.png')

"graba otra vez y elige la fila 1 (uno) con el raton:"
$h = RefH
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_REC, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1200
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_STOP, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$h = RefH
Check (WaitPending $h) "segunda grabacion pendiente"
$h = RefH
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_DROP, [IntPtr]::Zero) | Out-Null
$panel = Panel
Check ($panel -ne [IntPtr]::Zero) "la lista se abre tambien por WM_COMMAND"
if ($panel -ne [IntPtr]::Zero) {
  ClickRow $panel 0 $scale
}
$saved = @(Get-ChildItem $uno -File -Filter *.gif -ErrorAction SilentlyContinue)
Check ($saved.Count -ge 1) ("elegir la fila 1 guardo en uno: " + (($saved | ForEach-Object { $_.Name }) -join ', '))
$txt = IniNow
Check ($txt -match ('last=' + [regex]::Escape($uno))) "el ultimo usado paso a uno"

"quitar el bookmark seleccionado (boton - del panel):"
$h = RefH
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDM_BOOKMARKS_PANEL, [IntPtr]::Zero) | Out-Null
$panel = Panel
Check ($panel -ne [IntPtr]::Zero) "el panel abre desde IDM_BOOKMARKS_PANEL"
if ($panel -ne [IntPtr]::Zero) {
  $pr = Rect $panel
  $bx = ($pr.R - $pr.L) - [int][Math]::Round(16 * $scale)
  $by = [int][Math]::Round((6 + 18 + 4 + 2 * (18 + 3) + 9) * $scale)
  [WDp]::PostMessage($panel, 0x0201, [IntPtr]1, [IntPtr](($by -shl 16) -bor $bx)) | Out-Null
  Start-Sleep -Milliseconds 700
  $txt = IniNow
  Check ($txt -match 'count=2') "el boton - quito el bookmark seleccionado"
  Check ($txt -match ('last=' + [regex]::Escape($dos))) "la seleccion paso al vecino (dos)"
  [WDp]::PostMessage($panel, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null   # ESC
  Start-Sleep -Milliseconds 500
  Check (WaitPanelGone) "ESC cerro el panel"
}

"la flechita de Save sigue abriendo la misma lista:"
$h = RefH
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_REC, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_STOP, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$h = RefH
Check (WaitPending $h) "tercera grabacion pendiente"
$h = RefH
$sr = Rect ([WDp]::GetDlgItem($h, $IDC_SAVE))
[WDp]::SetCursorPos($sr.R - 8, [int](($sr.T + $sr.B) / 2)) | Out-Null
Start-Sleep -Milliseconds 250
[WDp]::PostMessage($h, 0x0111, [IntPtr]$IDC_SAVE, [IntPtr]::Zero) | Out-Null
$panel = Panel
Check ($panel -ne [IntPtr]::Zero) "la flechita de Save abre la lista"
if ($panel -eq [IntPtr]::Zero) { "  ventanas: " + (Titles) }
if ($panel -ne [IntPtr]::Zero) {
  [WDp]::PostMessage($panel, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 300
}

"ini final:"
$txt = IniNow
($txt -split "`r?`n" | Where-Object { $_ -match '^(count|b\d|last)=' }) | ForEach-Object { "  $_" }

Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item -Recurse -Force $drop -ErrorAction SilentlyContinue

""
if ($fail -eq 0) { "TODO OK (capturas en _uicheck\6*.png)" }
else { "FALLOS: $fail"; exit 1 }

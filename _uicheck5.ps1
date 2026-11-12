# _uicheck5.ps1 -- verificacion de los bookmarks de guardado (estilo Blender).
#
# Corre una COPIA del Release x64 en _bmtest\bin (con su propio licecap.ini),
# asi ninguna instancia previa ni el ini real del usuario pueden interferir.
#
# Fase visual (proceso A, se mata al terminar):
#   1. captura la ventana principal
#   2. abre el menu del boton Save (cursor en la flecha + IDC_SAVE) y captura
#      los bookmarks deshabilitados (sin grabacion pendiente)
#   3. graba ~1s y abre el desplegable de bookmarks con la flechita de Save:
#      comprueba que sale debajo del boton, alineado a su derecha, y que ESC
#      lo cierra (22b_save_dropdown.png)
#   4. graba ~1s, Stop y un clic REAL en la primera fila del desplegable: el GIF
#      tiene que aparecer en la carpeta del primer bookmark
#   5. IDM_BOOKMARK_ADD no duplica la carpeta
#   6. Move Up / Remove (ids de los botones del panel) se guardan en el ini
#   7. WM_CLOSE y reinicio: los bookmarks siguen ahi
#   8. panel estilo Blender: se abre, el boton - borra el seleccionado y ESC cierra
#   9. el selector de carpetas abre desde el boton de la carpeta
#
# Las imagenes van a _uicheck\*.png; _bmtest\ se borra al terminar.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WBm {
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
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public static string Titles(uint pid) {
    var sb = new StringBuilder();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (w == pid && IsWindowVisible(h)) sb.Append("[" + t.ToString() + "]");
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return sb.ToString();
  }
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static IntPtr FindClass(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      if (w == pid && c.ToString() == cls) { found = h; return false; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
  }
  public static IntPtr FindTitle(uint pid, string part) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (w == pid && IsWindowVisible(h) && t.ToString().Contains(part)) { found = h; return true; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
  }
  // Cuadro de aviso modal: clase #32770 y titulo EXACTAMENTE "LICEcap" (la
  // ventana principal tambien es #32770 pero lleva el tamano en el titulo).
  public static IntPtr FindBox(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (w == pid && IsWindowVisible(h) && t.ToString() == "LICEcap") { found = h; return false; }
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return found;
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

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src  = Join-Path $root 'licecap\licecap\x64\Release\licecap.exe'
$out  = Join-Path $root '_uicheck'
$bmroot = Join-Path $root '_bmtest'
$bin  = Join-Path $bmroot 'bin'
$exe  = Join-Path $bin 'licecap.exe'
$ini  = Join-Path $bin 'licecap.ini'
$bm1  = Join-Path $bmroot 'uno'
$bm2  = Join-Path $bmroot 'dos'
$IDC_REC  = 1001
$IDC_STOP = 1002
$IDC_SAVE = 1027
$IDM_BOOKMARK_FIRST = 40200
$IDM_BOOKMARK_ADD   = 40360
$IDM_BOOKMARK_PICK  = 40361
$IDM_BOOKMARKS_PANEL = 40362
$IDM_MOVEUP_2       = 40281
$IDM_REMOVE_1       = 40240

if (-not (Test-Path $src)) { throw "no existe $src (compila antes con MSBuild)" }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 20 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
Remove-Item -Recurse -Force $bmroot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $bin, $bm1, $bm2 | Out-Null
# el exe de origen puede seguir bloqueado por una instancia que acaba de morir
for ($i = 0; $i -lt 40; $i++) {
  try { Copy-Item $src $exe -Force -ErrorAction Stop; break }
  catch { Start-Sleep -Milliseconds 300 }
}
if (-not (Test-Path $exe)) { throw "no pude copiar el exe a $exe (sigue bloqueado)" }
$hSrc = (Get-FileHash $src -Algorithm SHA256).Hash
$hExe = (Get-FileHash $exe -Algorithm SHA256).Hash
if ($hSrc -ne $hExe) { throw "el exe copiado no coincide con el compilado" }
"exe: $($hExe.Substring(0,12)) ($((Get-Item $exe).Length) bytes)"

function Shot([IntPtr]$hwnd, [string]$path, [int]$padRight = 0, [int]$padBottom = 0) {
  $r = New-Object WBm+RECT
  if (-not [WBm]::GetWindowRect($hwnd, [ref]$r)) { "  (ventana no valida, sin captura)"; return }
  $w = ($r.R - $r.L) + $padRight
  $hh = ($r.B - $r.T) + $padBottom
  if ($w -le 0 -or $hh -le 0) { "  (rect vacio, sin captura)"; return }
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  ${w}x${hh}"
}

# La ventana se recrea al aplicar tema/modo, asi que el hwnd se busca de nuevo
# antes de cada interaccion.
function RefH {
  for ($i = 0; $i -lt 15; $i++) {
    $h = [WBm]::Find([uint32]$proc.Id)
    if ($h -ne [IntPtr]::Zero) { return $h }
    Start-Sleep -Milliseconds 200
  }
  return [IntPtr]::Zero
}

function Activate([IntPtr]$hwnd) {
  $r = New-Object WBm+RECT
  [WBm]::GetWindowRect($hwnd, [ref]$r) | Out-Null
  [WBm]::SetCursorPos($r.L + 60, $r.T + 10) | Out-Null
  Start-Sleep -Milliseconds 200
  [WBm]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [WBm]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

# El titulo lleva "- not saved" cuando hay una grabacion pendiente: esperamos
# a que el Stop la deje registrada antes de abrir el menu.
# Cerrar el menu modal: WM_CANCELMODE lo cancela (un click sintetico fuera no
# siempre lo cierra y ESC acabaria cerrando la app).
function CloseMenu([IntPtr]$hwnd) {
  [WBm]::PostMessage($hwnd, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 600
  # el menu es una ventana propia (#32768): si sigue abierto lo cerramos con ESC
  for ($i = 0; $i -lt 10; $i++) {
    $m = [WBm]::FindClass([uint32]$script:proc.Id, '#32768')
    if ($m -eq [IntPtr]::Zero) { return }
    [WBm]::PostMessage($m, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
  }
  throw "el menu no se cerro"
}

function WaitPending([IntPtr]$hwnd, [int]$ms = 4000) {
  for ($i = 0; $i -lt [int]($ms / 200); $i++) {
    $sb = New-Object System.Text.StringBuilder 256
    [WBm]::GetWindowText($hwnd, $sb, 256) | Out-Null
    if ($sb.ToString() -like '*not saved*') { return $true }
    Start-Sleep -Milliseconds 200
  }
  return $false
}

function IniNow {
  for ($i = 0; $i -lt 15; $i++) {
    if (Test-Path $ini) { return (Get-Content $ini -Raw) }
    Start-Sleep -Milliseconds 200
  }
  return ''
}

function Start-App {
  # una instancia vieja viva reescribe el ini al salir y pisa lo que hace la nueva
  Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
  for ($i = 0; $i -lt 25 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
  $lines = @(
    '[licecap]',
    'dark=1',
    'maxfps=10',
    'cursor=1',
    'wnd_r=140 120 570 560',
    '',
    '[bookmarks]',
    'count=2',
    ('b1=' + $bm1),
    ('b2=' + $bm2),
    ''
  )
  Set-Content -Path $ini -Value ($lines -join "`r`n") -NoNewline
  # el arranque a veces tarda (o el proceso anterior muere justo ahora): reintentar
  for ($try = 1; $try -le 3; $try++) {
    $script:proc = Start-Process -FilePath $exe -WorkingDirectory $bin -PassThru
    Start-Sleep -Seconds 2
    if ($script:proc.HasExited) {
      ("  (intento " + $try + ": el proceso salio con codigo " + $script:proc.ExitCode + ")")
      continue
    }
    $script:h = RefH
    if ($script:h -ne [IntPtr]::Zero) { break }
    ("  (intento " + $try + ": sin ventana, reintentando)")
    $script:proc | Stop-Process -Force
    Start-Sleep -Milliseconds 500
  }
  if ($script:h -eq [IntPtr]::Zero) { throw "no encontre la ventana de licecap" }
  [WBm]::SetWindowPos($script:h, [IntPtr](-1), 140, 120, 430, 440, 0x0000) | Out-Null
  Start-Sleep -Milliseconds 400
  $script:h = RefH
  Activate $script:h
  $script:h = RefH
  if ($script:h -eq [IntPtr]::Zero) { throw "no encontre la ventana tras activar" }
}

# Captura una ventana suelta con un margen alrededor: el desplegable de
# bookmarks es una ventana propia, asi que no sale en la captura principal.
function ShotWin([IntPtr]$hwnd, [string]$path, [int]$pad = 6) {
  $r = New-Object WBm+RECT
  if (-not [WBm]::GetWindowRect($hwnd, [ref]$r)) { "  (ventana no valida, sin captura)"; return }
  $w = ($r.R - $r.L) + 2 * $pad
  $hh = ($r.B - $r.T) + 2 * $pad
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L - $pad, $r.T - $pad, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  $([System.IO.Path]::GetFileName($path))  ${w}x${hh}"
}

# Con una grabacion pendiente la flecha de Save abre la lista de bookmarks como
# desplegable debajo del boton (IDC_SAVE -> BookmarkPanel_Show), no el menu.
function OpenSaveDropdown {
  $sb = [WBm]::GetDlgItem($h, $IDC_SAVE)
  $r  = New-Object WBm+RECT
  if (-not [WBm]::GetWindowRect($sb, [ref]$r)) { throw "no encontre el boton Save" }
  [WBm]::SetCursorPos($r.R - 8, [int](($r.T + $r.B) / 2)) | Out-Null
  Start-Sleep -Milliseconds 250
  [WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_SAVE, [IntPtr]::Zero) | Out-Null
  $p = [IntPtr]::Zero
  for ($i = 0; $i -lt 15 -and $p -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 200
    $p = [WBm]::FindClass([uint32]$script:proc.Id, 'LiceBookmarkPanel')
  }
  return $p
}

# Cursor en la zona de la flecha del boton Save: IDC_SAVE abre el menu en vez
# de guardar (misma ruta que el click real: ArrowZoneNow + ShowToolMenu).
function OpenSaveMenu {
  $sb = [WBm]::GetDlgItem($h, $IDC_SAVE)
  $r  = New-Object WBm+RECT
  if (-not [WBm]::GetWindowRect($sb, [ref]$r)) { throw "no encontre el boton Save" }
  [WBm]::SetCursorPos($r.R - 8, [int](($r.T + $r.B) / 2)) | Out-Null
  Start-Sleep -Milliseconds 250
  [WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_SAVE, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 1000
}

$fail = 0
function Check([bool]$ok, [string]$msg) {
  if ($ok) { "  OK: $msg" } else { "  FALLO: $msg"; $script:fail++ }
}

# ---------------------------------------------------------------------------
# Fase visual
# ---------------------------------------------------------------------------
"fase visual:"
Start-App
"capturas:"
Shot $h (Join-Path $out '20_main.png')

"menu Save sin grabacion pendiente:"
OpenSaveMenu
Shot $h (Join-Path $out '21_save_menu.png') 400 150
CloseMenu $h
$h = RefH

"grabando 1.2s + menu Save con pendiente:"
$h = RefH
[WBm]::SetCursorPos(5, 5) | Out-Null
Start-Sleep -Milliseconds 200
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_REC, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1200
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_STOP, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
Shot $h (Join-Path $out '22_recorded.png')
$h = RefH
Check (WaitPending $h) "la grabacion quedo pendiente (titulo '- not saved')"
$h = RefH
"desplegable de bookmarks con pendiente (flechita de Save):"
$panel = OpenSaveDropdown
Check ($panel -ne [IntPtr]::Zero) "la flechita de Save abre el desplegable de bookmarks"
if ($panel -ne [IntPtr]::Zero) {
  $pr = New-Object WBm+RECT
  [WBm]::GetWindowRect($panel, [ref]$pr) | Out-Null
  $sr = New-Object WBm+RECT
  $sb = [WBm]::GetDlgItem($h, $IDC_SAVE)
  [WBm]::GetWindowRect($sb, [ref]$sr) | Out-Null
  Check ($pr.T -ge $sr.B - 2) ("el desplegable queda debajo de Save (panel top=" + $pr.T + ", save bottom=" + $sr.B + ")")
  Check ([Math]::Abs($pr.R - $sr.R) -le 2) ("alineado al borde derecho de Save (panel right=" + $pr.R + ", save right=" + $sr.R + ")")
  ShotWin $panel (Join-Path $out '22b_save_dropdown.png')
  [WBm]::PostMessage($panel, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null   # ESC
  Start-Sleep -Milliseconds 400
  Check (([WBm]::FindClass([uint32]$proc.Id, 'LiceBookmarkPanel')) -eq [IntPtr]::Zero) "ESC cerro el desplegable"
}

Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 20 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }

# ---------------------------------------------------------------------------
# Fase funcional (sin menus: se mandan los ids por WM_COMMAND)
# ---------------------------------------------------------------------------
"fase funcional:"
Start-App

"grabando 1.2s:"
[WBm]::SetCursorPos(5, 5) | Out-Null
Start-Sleep -Milliseconds 200
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_REC, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1200
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDC_STOP, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800

"guardando desde el desplegable (clic en la fila 1 -> $bm1):"
$panel = OpenSaveDropdown
Check ($panel -ne [IntPtr]::Zero) "el desplegable se abrio con la grabacion pendiente"
if ($panel -ne [IntPtr]::Zero) {
  # fila 1: pad 6 + titulo 18 + 4 + media fila (24/2) = 40 unidades de UI;
  # el boton Save mide UI_BTN_H (26), asi que escala con el DPI.
  $sr = New-Object WBm+RECT
  $sb = [WBm]::GetDlgItem($h, $IDC_SAVE)
  [WBm]::GetWindowRect($sb, [ref]$sr) | Out-Null
  $scale = ($sr.B - $sr.T) / 26.0
  $clickY = [int][Math]::Round(40 * $scale)
  $clickX = [int][Math]::Round(15 * $scale)
  $panelR = New-Object WBm+RECT
  [WBm]::GetWindowRect($panel, [ref]$panelR) | Out-Null
  "  clic en fila 1 del panel en ($clickX,$clickY) de $($panelR.R - $panelR.L)x$($panelR.B - $panelR.T)"
  [WBm]::PostMessage($panel, 0x0201, [IntPtr]1, [IntPtr](($clickY -shl 16) -bor $clickX)) | Out-Null
  Start-Sleep -Milliseconds 1500
  Check (([WBm]::FindClass([uint32]$proc.Id, 'LiceBookmarkPanel')) -eq [IntPtr]::Zero) "el desplegable se cerro tras guardar"
} else {
  # sin desplegable se guarda igual por id, para no arrastrar el fallo al resto
  [WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_BOOKMARK_FIRST, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 1200
}
Shot $h (Join-Path $out '23_after_quick_save.png')
$saved = @(Get-ChildItem $bm1 -File -Filter *.gif -ErrorAction SilentlyContinue)
Check ($saved.Count -ge 1) ("el gif quedo en el bookmark 1 -> " + (($saved | ForEach-Object { $_.Name + ' (' + $_.Length + ' bytes)' }) -join ', '))

"IDM_BOOKMARK_ADD (carpeta del ultimo guardado):"
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_BOOKMARK_ADD, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 700
$txt = IniNow
Check ($txt -notmatch 'b3=') "no duplico la carpeta que ya era bookmark"
# el aviso modal del duplicado tiene que existir y hay que cerrarlo: si queda
# abierto, el WM_CLOSE posterior no cierra la app y el resto del test miente
$mb = [WBm]::FindBox([uint32]$proc.Id)
Check ($mb -ne [IntPtr]::Zero) "el + duplicado avisa con un cuadro modal"
if ($mb -ne [IntPtr]::Zero) {
  [WBm]::PostMessage($mb, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE del aviso
  Start-Sleep -Milliseconds 600
}
Check (([WBm]::FindBox([uint32]$proc.Id)) -eq [IntPtr]::Zero) "el aviso de duplicado se cerro"

"Bookmarks > Move Up del segundo:"
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_MOVEUP_2, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 700
$txt = IniNow
Check ($txt -match ('b1=' + [regex]::Escape($bm2))) "b2 subio a b1 en el ini"

"Bookmarks > Remove del primero:"
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_REMOVE_1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 700
$txt = IniNow
Check ($txt -match ('b1=' + [regex]::Escape($bm1))) "al borrar el primero quedo b1=$bm1"
Check ($txt -match 'count=1') "count=1 en el ini"

"reinicio de la app:"
[WBm]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
for ($i = 0; $i -lt 25 -and -not $proc.HasExited; $i++) { Start-Sleep -Milliseconds 200; $proc.Refresh() }
if (-not $proc.HasExited) { "  ventanas del proceso al no cerrar: " + [WBm]::Titles([uint32]$proc.Id) }
Check ($proc.HasExited) "la app cerro con WM_CLOSE"
$txt = IniNow
Check ($txt -match ('b1=' + [regex]::Escape($bm1))) "el bookmark sobrevivio al cierre (SaveConfig no lo pisa)"
Check ($txt -match 'maxfps=10') "el resto del ini se guardo (SaveConfig)"

Start-App
Shot $h (Join-Path $out '24_after_restart.png')
"menu Save tras reiniciar:"
OpenSaveMenu
Shot $h (Join-Path $out '25_menu_after_restart.png') 400 150
CloseMenu $h

# --- panel de bookmarks estilo Blender: lista + botones + / carpeta / - / arriba / abajo
$h = RefH
[WBm]::SetCursorPos(20, 20) | Out-Null
Start-Sleep -Milliseconds 200
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_BOOKMARKS_PANEL, [IntPtr]::Zero) | Out-Null
$panel = [IntPtr]::Zero
for ($i = 0; $i -lt 15 -and $panel -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 200
  $panel = [WBm]::FindClass([uint32]$proc.Id, 'LiceBookmarkPanel')
}
Check ($panel -ne [IntPtr]::Zero) "se abrio el panel de bookmarks"
Start-Sleep -Milliseconds 1200
Check (([WBm]::FindClass([uint32]$proc.Id, 'LiceBookmarkPanel')) -ne [IntPtr]::Zero) "el panel sigue abierto (no se cierra solo)"
if ($panel -ne [IntPtr]::Zero) {
  $r = New-Object WBm+RECT
  [WBm]::GetWindowRect($panel, [ref]$r) | Out-Null
  "  panel: $($r.R - $r.L) x $($r.B - $r.T) en $($r.L),$($r.T)"
  $w = New-Object WBm+RECT
  [WBm]::GetWindowRect($h, [ref]$w) | Out-Null
  $padR = [Math]::Max(0, ($r.R - $w.L) - ($w.R - $w.L))
  $padB = [Math]::Max(0, ($r.B - $w.T) - ($w.B - $w.T))
  Shot $h (Join-Path $out '27_panel.png') $padR $padB
  # click en el boton "-" (tercer boton de la columna)
  $bx = ($r.R - $r.L) - 16
  $by = 6 + 18 + 4 + 2 * (18 + 3) + 9
  $lp = [IntPtr](($by -shl 16) -bor $bx)
  [WBm]::PostMessage($panel, 0x0201, [IntPtr]1, $lp) | Out-Null
  Start-Sleep -Milliseconds 500
  $ini2 = Get-Content $ini -Raw
  Check ($ini2 -match ('b1=' + [regex]::Escape($bm2)) -and $ini2 -match 'count=1') "el boton - quito el bookmark seleccionado (quedo b1=dos)"
  [WBm]::PostMessage($panel, 0x0201, [IntPtr]1, $lp) | Out-Null
  Start-Sleep -Milliseconds 700
  $ini2 = Get-Content $ini -Raw
  Check ($ini2 -match 'count=0' -and $ini2 -notmatch 'b1=') "sin bookmarks el panel queda vacio"
  Shot $h (Join-Path $out '28_panel_empty.png') $padR $padB
  [WBm]::PostMessage($panel, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero) | Out-Null   # ESC
  Start-Sleep -Milliseconds 300
  Check (([WBm]::FindClass([uint32]$proc.Id, 'LiceBookmarkPanel')) -eq [IntPtr]::Zero) "ESC cerro el panel"
}

# --- selector de carpetas (IFileOpenDialog): ultima fase, se mata el proceso --
"selector de carpetas para agregar un bookmark:"
# proceso nuevo: el selector es modal y no hace falta seguir despues
Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 20 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
Start-App
$h = RefH
[WBm]::SetCursorPos(20, 20) | Out-Null
Start-Sleep -Milliseconds 300
[WBm]::PostMessage($h, 0x0111, [IntPtr]$IDM_BOOKMARK_PICK, [IntPtr]::Zero) | Out-Null
$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 15 -and $dlg -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 300
  $dlg = [WBm]::FindTitle([uint32]$proc.Id, 'Choose a folder to bookmark')
}
Check ($dlg -ne [IntPtr]::Zero) "se abrio el selector de carpetas"
if ($dlg -ne [IntPtr]::Zero) {
  $bmp = New-Object System.Drawing.Bitmap 900, 700
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
  $bmp.Save((Join-Path $out '26_folder_picker.png'), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "  26_folder_picker.png  900x700"
}

"estado final del ini:"
IniNow | Where-Object { $_ -match '^(b\d+=|count=|\[)' } | ForEach-Object { "  $_" }

Get-Process licecap -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 20 -and (Get-Process licecap -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }
Remove-Item -Recurse -Force $bmroot -ErrorAction SilentlyContinue
"imagenes en _uicheck:"
Get-ChildItem $out -Filter '2*.png' | ForEach-Object { "  " + $_.Name + "  " + $_.Length + " bytes" }
if ($fail -gt 0) { "RESULTADO: $fail comprobacion(es) fallaron" } else { "RESULTADO: todo ok" }

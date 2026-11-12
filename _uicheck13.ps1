# _uicheck13.ps1 -- abrir un GIF del disco en el editor de timeline.
#
# Cubre las tres vias de "abrir un GIF y editarlo":
#   A) arrancar la app con el GIF como argumento (licecap.exe anim.gif),
#   B) el menu del boton Edit / Ctrl+O (con el selector de archivos),
#   C) un archivo que no es un GIF (aviso de error, sin dejar copia pendiente).
#
# Con A se valida el ciclo completo: se abre un GIF hecho con Pillow (ajeno a
# LICEcap, con disposal 2 y paletas locales), se recorta por teclado
# (Right Right A Enter) y se comprueba con Pillow que la copia de trabajo y el
# archivo guardado tienen los frames y duraciones esperados, que el GIF
# original queda intacto (md5) y que Save As propone "<nombre>-edit.gif" en la
# carpeta del original.
#
# Uso: powershell -ExecutionPolicy Bypass -File _uicheck13.ps1

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WOpen {
  public delegate bool EnumProc(IntPtr h, IntPtr p);

  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageGetTextW(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);

  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

  public static string Text(IntPtr h) {
    var s = new StringBuilder(512);
    GetWindowTextA(h, s, 512);
    return s.ToString();
  }

  // GetWindowText no cruza procesos para los controles del dialogo de archivos;
  // WM_GETTEXT si.
  public static string FieldText(IntPtr h) {
    var s = new StringBuilder(1024);
    SendMessageGetTextW(h, 0x000D, (IntPtr)1024, s);
    return s.ToString();
  }

  public static List<string> Dump(uint pid) { return Dump(pid, true); }

  // showAll=true incluye ventanas no visibles: con un aviso modal por delante
  // la ventana principal queda deshabilitada y deja de contar como visible.
  public static List<string> Dump(uint pid, bool visibleOnly) {
    var outp = new List<string>();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      uint w; GetWindowThreadProcessId(h, out w);
      if (w != pid || (visibleOnly && !IsWindowVisible(h))) return true;
      var t = new StringBuilder(512); GetWindowText(h, t, 512);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      outp.Add(String.Format("{0}|{1}|{2}", h, c, t));
      return true;
    };
    EnumWindows(cb, IntPtr.Zero);
    return outp;
  }

  public static List<string> DumpChildren(IntPtr dlg) {
    var outp = new List<string>();
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      if (!IsWindowVisible(h)) return true;
      var t = new StringBuilder(512); GetWindowText(h, t, 512);
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      outp.Add(String.Format("{0}|{1}|id={2}", h, c, GetDlgCtrlID(h)));
      return true;
    };
    EnumChildWindows(dlg, cb, IntPtr.Zero);
    return outp;
  }

  // El texto de un MessageBox vive en un Static, pero el icono es otro Static
  // vacio: se queda el que mas texto tiene.
  public static IntPtr TextOfClass(IntPtr dlg, string cls) {
    IntPtr best = IntPtr.Zero;
    int bestLen = -1;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      if (c.ToString() != cls) return true;
      var t = new StringBuilder(2048); SendMessageGetTextW(h, 0x000D, (IntPtr)2048, t);
      if (t.Length > bestLen) { bestLen = t.Length; best = h; }
      return true;
    };
    EnumChildWindows(dlg, cb, IntPtr.Zero);
    return best;
  }

  public static IntPtr ChildById(IntPtr dlg, int id) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      if (GetDlgCtrlID(h) == id) { found = h; return false; }
      return true;
    };
    EnumChildWindows(dlg, cb, IntPtr.Zero);
    return found;
  }

  public static IntPtr Child(IntPtr dlg, string cls, int id) {
    IntPtr found = IntPtr.Zero;
    EnumProc cb = delegate(IntPtr h, IntPtr p) {
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      if (c.ToString() == cls && GetDlgCtrlID(h) == id) { found = h; return false; }
      return true;
    };
    EnumChildWindows(dlg, cb, IntPtr.Zero);
    return found;
  }
}
'@

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$scratch = Join-Path $root "_opentest"
$shots   = Join-Path $root "_uicheck"
$src     = Join-Path $root "licecap\licecap\x64\Release\licecap.exe"
$exe     = Join-Path $scratch "licecap_opnchk.exe"
$ini     = Join-Path $scratch "licecap.ini"
$srcgif  = Join-Path $scratch "src.gif"
$outgif  = Join-Path $scratch "out.gif"
$badtxt  = Join-Path $scratch "notagif.txt"
$pyinfo  = Join-Path $scratch "gifinfo.py"
$pygen   = Join-Path $scratch "makegif.py"

if (-not (Test-Path $src)) { throw "falta $src (compila primero)" }
New-Item -ItemType Directory -Force -Path $scratch, $shots | Out-Null
Get-Process licecap_opnchk -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
$tries = 0
while ($tries -lt 10) {
  try { Copy-Item $src $exe -Force; break } catch { Start-Sleep -Milliseconds 400; $tries++ }
}
if ($tries -ge 10) { throw "no se pudo copiar $exe (proceso anterior vivo?)" }
Set-Content -Path $ini -NoNewline -Value (@('[licecap]','dark=1','maxfps=10','cursor=1','prefs=0','') -join "`r`n")
Set-Content -Path $pyinfo -NoNewline -Value @'
import sys
from PIL import Image
im = Image.open(sys.argv[1])
d = []
for i in range(getattr(im, "n_frames", 1)):
    im.seek(i)
    d.append(int(im.info.get("duration") or 0))
print("frames=%d total=%d delays=%s" % (len(d), sum(d), ",".join(str(x) for x in d)))
'@
Set-Content -Path $pygen -NoNewline -Value @'
import sys
from PIL import Image
colors = [(220,40,40),(40,180,60),(40,90,220),(230,200,40),(150,50,200),(40,200,200)]
frames = []
for i,c in enumerate(colors):
    im = Image.new("RGB",(96,64),c)
    for y in range(8+i*3, 20+i*3):
        for x in range(8+i*6, 24+i*6):
            im.putpixel((x,y),(255,255,255))
    frames.append(im)
frames[0].save(sys.argv[1], save_all=True, append_images=frames[1:],
               duration=100, loop=0, disposal=2, optimize=False)
'@
Set-Content -Path $badtxt -NoNewline -Value "esto no es un gif`r`n"
if (Test-Path $outgif) { Remove-Item $outgif -Force }

function Say([string]$msg) { Write-Host $msg }
function Assert([bool]$cond, [string]$msg) {
  if (-not $cond) { throw "FALLO: $msg" }
  Say ("ok - " + $msg)
}

function Title([IntPtr]$h) { [WOpen]::Text($h) }

function Shoot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object WOpen+RECT
  [void][WOpen]::GetWindowRect($hwnd, [ref]$r)
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size($b.Width, $b.Height)))
  $g.Dispose()
  $crop = $bmp.Clone((New-Object System.Drawing.Rectangle($r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T))), $bmp.PixelFormat)
  $crop.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $crop.Dispose(); $bmp.Dispose()
  Say ("captura -> $path")
}

function Key([IntPtr]$h, [int]$vk) {
  [void][WOpen]::PostMessage($h, 0x0100, [IntPtr]$vk, [IntPtr]::Zero)
  [void][WOpen]::PostMessage($h, 0x0101, [IntPtr]$vk, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 90
}

function Cmd([IntPtr]$h, [int]$id) {
  [void][WOpen]::PostMessage($h, 0x0111, [IntPtr]$id, [IntPtr]::Zero)
}

# ventana de nivel superior de la app con ese titulo exacto
function TopWindow([int]$procId, [string]$title, [int]$timeoutMs = 6000) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalMilliseconds -lt $timeoutMs) {
    foreach ($line in [WOpen]::Dump([uint32]$procId)) {
      $f = $line -split '\|'
      if ($f[1] -eq '#32770' -and $f[2] -eq $title) { return [IntPtr][int64]$f[0] }
    }
    Start-Sleep -Milliseconds 150
  }
  return [IntPtr]::Zero
}

function MainWindow([int]$procId, [int]$timeoutMs = 8000) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalMilliseconds -lt $timeoutMs) {
    foreach ($line in [WOpen]::Dump([uint32]$procId, $false)) {
      $f = $line -split '\|'
      if ($f[1] -eq '#32770' -and $f[2] -like 'LICEcap *') { return [IntPtr][int64]$f[0] }
    }
    Start-Sleep -Milliseconds 150
  }
  return [IntPtr]::Zero
}

function Start-App([string]$arg) {
  if ($arg) {
    $p = Start-Process -FilePath $exe -ArgumentList ('"' + $arg + '"') -PassThru
  } else {
    $p = Start-Process -FilePath $exe -PassThru
  }
  $h = MainWindow $p.Id
  if ($h -eq [IntPtr]::Zero) { throw "sin ventana principal" }
  Start-Sleep -Milliseconds 500
  # el raton lejos del boton Rec: los scripts vecinos graban, aqui estorba
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(40, 700)
  return @{ Proc = $p; Hwnd = $h }
}

function Stop-App($app) {
  try { [void][WOpen]::PostMessage($app.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) } catch {}
  Start-Sleep -Milliseconds 600
  Stop-Process -Id $app.Proc.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 300
}

# rectangulo en pantalla de un control de la ventana principal
function ControlRect([IntPtr]$h, [int]$id) {
  $b = [WOpen]::GetDlgItem($h, $id)
  if ($b -eq [IntPtr]::Zero) { throw "no existe el control $id" }
  $r = New-Object WOpen+RECT
  [void][WOpen]::GetWindowRect($b, [ref]$r)
  return $r
}

# Un clic real solo llega al boton si la ventana esta en primer plano; Windows
# se lo niega a un proceso en segundo plano, asi que se insiste (el Alt suelta
# el bloqueo) y se comprueba de verdad antes de lanzar el clic.
function Focus-App([IntPtr]$h) {
  for ($i = 0; $i -lt 4; $i++) {
    if ([WOpen]::GetForegroundWindow() -eq $h) { return $true }
    [void][WOpen]::ShowWindow($h, 9)                     # SW_RESTORE
    [void][WOpen]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 200
    if ([WOpen]::GetForegroundWindow() -eq $h) { return $true }
    [WOpen]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)     # Alt abajo
    [WOpen]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)     # Alt arriba
    Start-Sleep -Milliseconds 150
    [void][WOpen]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds 200
  }
  return $false
}

# Con la ventana en primer plano el clic es real (mouse_event). Si Windows no
# deja activarla (sesion desatendida, otro proceso delante...) se mandan
# WM_LBUTTONDOWN/UP al boton: el cursor si se puede colocar, y la decision
# etiqueta/flecha se toma igual con GetCursorPos sobre el rectangulo real.
$script:RealClicks = $false

function ClickAt([IntPtr]$h, [int]$id, [int]$x, [int]$y) {
  [void][WOpen]::SetCursorPos($x, $y)
  Start-Sleep -Milliseconds 150
  if (Focus-App $h) {
    $script:RealClicks = $true
    [WOpen]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [WOpen]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
  } else {
    $b = [WOpen]::GetDlgItem($h, $id)
    if ($b -eq [IntPtr]::Zero) { throw "no existe el control $id" }
    [void][WOpen]::PostMessage($b, 0x0201, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [void][WOpen]::PostMessage($b, 0x0202, [IntPtr]0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
  }
}

function PopupMenu([int]$procId, [int]$timeoutMs = 3000) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalMilliseconds -lt $timeoutMs) {
    foreach ($line in [WOpen]::Dump([uint32]$procId, $false)) {
      $f = $line -split '\|'
      if ($f[1] -eq '#32768') { return [IntPtr][int64]$f[0] }
    }
    Start-Sleep -Milliseconds 100
  }
  return [IntPtr]::Zero
}

function GifInfo([string]$path) {
  $out = & python $pyinfo $path 2>&1
  if ($LASTEXITCODE -ne 0) { throw "python no pudo leer ${path}: $out" }
  $h = @{}
  foreach ($part in ((($out | Out-String).Trim()) -split '\s+')) {
    $kv = $part -split '='
    if ($kv.Count -eq 2) { $h[$kv[0]] = $kv[1] }
  }
  return $h
}

function TempCopies {
  return @(Get-ChildItem $env:TEMP -Filter 'LICEcap_edit_*.gif' -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime)
}

# --- GIF de origen: 6 frames de 100 ms, hecho con Pillow (no por LICEcap) ---
& python $pygen $srcgif
if ($LASTEXITCODE -ne 0) { throw "no se pudo generar $srcgif" }
$srcinfo = GifInfo $srcgif
Assert ($srcinfo['frames'] -eq '6' -and $srcinfo['total'] -eq '600') ("GIF de origen: 6 frames / 600 ms (" + $srcinfo['delays'] + ")")
$srcmd5 = (Get-FileHash $srcgif -Algorithm MD5).Hash

TempCopies | Remove-Item -Force
$before = (TempCopies).Count

# ===========================================================================
# A) abrir el GIF como argumento de linea de comandos
# ===========================================================================
Say ""
Say "=== A) licecap.exe src.gif ==="
$app = Start-App $srcgif
$h = $app.Hwnd
Say ("titulo: " + (Title $h))
Assert ((Title $h) -like "*src.gif (opened) - not saved*") "el titulo dice que hay un GIF abierto sin guardar"

$copies = TempCopies
Assert ($copies.Count -eq 1) ("se creo una copia de trabajo temporal: " + $copies[0].FullName)
$work = $copies[0].FullName
Assert ((Get-FileHash $work -Algorithm MD5).Hash -eq $srcmd5) "la copia de trabajo empieza igual al original"
$status = [WOpen]::ChildById($h, 1003)
Assert ($status -ne [IntPtr]::Zero) "la ventana tiene el bloque de estado"
$stxt = [WOpen]::FieldText($status)
Assert ($stxt -like 'OPENED*') ("el estado dice OPENED, no RECORDED ('" + ($stxt -replace "`r?`n", ' / ') + "')")
Shoot $h (Join-Path $shots '70_opened.png')

# recorte por teclado: playhead al frame 2, A = 2, Apply
Key $h 0x27; Key $h 0x27
Key $h 0x41
Start-Sleep -Milliseconds 200
Key $h 0x0D
Start-Sleep -Milliseconds 1500
Assert ((Title $h) -like "*src.gif (opened) - not saved*") "tras Apply sigue pendiente (sin guardar)"

$workinfo = GifInfo $work
Assert ($workinfo['frames'] -eq '4' -and $workinfo['total'] -eq '400') ("la copia recortada tiene 4 frames / 400 ms (" + $workinfo['delays'] + ")")
Assert ((Get-FileHash $srcgif -Algorithm MD5).Hash -eq $srcmd5) "el GIF original no se toco"
Shoot $h (Join-Path $shots '71_applied.png')

# la etiqueta del boton Edit (sin flecha) reabre el editor sobre lo pendiente;
# Esc lo consume el editor, asi que la app sigue viva y el GIF sigue pendiente
Cmd $h 1053
Start-Sleep -Milliseconds 1000
Assert ($null -ne (Get-Process -Id $app.Proc.Id -ErrorAction SilentlyContinue)) "el boton Edit abre el editor"
Key $h 0x1B
Start-Sleep -Milliseconds 700
Assert ($null -ne (Get-Process -Id $app.Proc.Id -ErrorAction SilentlyContinue)) "Esc cierra el editor, no la aplicacion"
Assert ((Title $h) -like "*src.gif (opened) - not saved*") "cerrar el editor no pierde el GIF pendiente"

# Save As: nombre y carpeta propuestos, luego guardar de verdad
Cmd $h 40040
$save = TopWindow $app.Proc.Id 'Save the animation as'
Assert ($save -ne [IntPtr]::Zero) "Save As abre su dialogo"
$edit = [WOpen]::Child($save, 'Edit', 1001)
Assert ($edit -ne [IntPtr]::Zero) "el dialogo tiene el campo de nombre"
$proposed = [WOpen]::FieldText($edit)
Assert ($proposed -eq 'src-edit.gif') ("Save As propone '" + $proposed + "'")
Shoot $save (Join-Path $shots '72_saveas.png')

[void][WOpen]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $outgif)
Start-Sleep -Milliseconds 300
[void][WOpen]::SendMessage($save, 0x0111, [IntPtr]1, [IntPtr]::Zero)
Start-Sleep -Milliseconds 1800
Assert (Test-Path $outgif) ("se guardo " + $outgif)
$outinfo = GifInfo $outgif
Assert ($outinfo['frames'] -eq '4' -and $outinfo['total'] -eq '400') ("el GIF guardado tiene 4 frames / 400 ms (" + $outinfo['delays'] + ")")
Assert ((Title $h) -eq 'LICEcap 406 x 469' -or (Title $h) -notlike '*(opened)*') "tras guardar el titulo ya no dice (opened)"
Assert ((Get-FileHash $srcgif -Algorithm MD5).Hash -eq $srcmd5) "sigue sin tocarse el original"
Stop-App $app

# ===========================================================================
# B) menu del boton Edit (IDM_OPEN_GIF) y Ctrl+O
# ===========================================================================
Say ""
Say "=== B) menu Edit > Open GIF to Edit... y Ctrl+O ==="
TempCopies | Remove-Item -Force
$app = Start-App $null
$h = $app.Hwnd
Assert ((Title $h) -notlike "*opened*") "arranque limpio, sin nada abierto"

Cmd $h 40043
$open = TopWindow $app.Proc.Id 'Open a GIF to edit'
Assert ($open -ne [IntPtr]::Zero) "el menu open abre el selector de archivos"
Shoot $open (Join-Path $shots '73_picker.png')
Key $open 0x1B
Start-Sleep -Milliseconds 900
Assert ((Title $h) -notlike "*opened*") "cancelar el selector no deja nada abierto"
Assert ((TempCopies).Count -eq 0) "cancelar el selector no deja copias temporales"

# sin nada pendiente, la etiqueta del boton Edit va directa al selector
Cmd $h 1053
$open3 = TopWindow $app.Proc.Id 'Open a GIF to edit'
Assert ($open3 -ne [IntPtr]::Zero) "sin grabacion, Edit abre el selector sin pasar por el menu"
if ($open3 -ne [IntPtr]::Zero) { Key $open3 0x1B }
Start-Sleep -Milliseconds 700

[void][WOpen]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 300
[WOpen]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)          # Ctrl abajo
Start-Sleep -Milliseconds 120
[void][WOpen]::PostMessage($h, 0x0100, [IntPtr]0x4F, [IntPtr]::Zero)   # 'O'
Start-Sleep -Milliseconds 120
[WOpen]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)          # Ctrl arriba
Start-Sleep -Milliseconds 200
$open2 = TopWindow $app.Proc.Id 'Open a GIF to edit'
Assert ($open2 -ne [IntPtr]::Zero) "Ctrl+O abre el mismo selector"
if ($open2 -ne [IntPtr]::Zero) { Key $open2 0x1B }
Start-Sleep -Milliseconds 800
Stop-App $app

# ===========================================================================
# C) un archivo que no es un GIF
# ===========================================================================
Say ""
Say "=== C) abrir algo que no es un GIF ==="
TempCopies | Remove-Item -Force
$app = Start-App $badtxt
$h = $app.Hwnd
$box = TopWindow $app.Proc.Id 'LICEcap'
Assert ($box -ne [IntPtr]::Zero) "avisa de que no se pudo leer"
$stat = [WOpen]::TextOfClass($box, 'Static')
$mtxt = ''
if ($stat -ne [IntPtr]::Zero) { $mtxt = [WOpen]::FieldText($stat) }
Assert ($mtxt -like '*not a GIF*') ("el aviso dice que no es un GIF ('" + ($mtxt -replace "`r?`n", ' / ') + "')")
# el aviso sale antes de tocar el pendiente: ni copia ni titulo "(opened)"
Assert ((Title $h) -notlike "*opened*") "mientras avisa, la ventana no finge tener el archivo abierto"
Assert ((TempCopies).Count -eq 0) "no se creo ninguna copia de trabajo"
Shoot $box (Join-Path $shots '74_badfile.png')
Key $box 0x0D
Start-Sleep -Milliseconds 900
Assert ((Title $h) -notlike "*opened*") "tras cerrar el aviso sigue todo limpio"
Assert ((TempCopies).Count -eq 0) "sin copias temporales"
Stop-App $app

# ===========================================================================
# D) una ruta que no existe
# ===========================================================================
Say ""
Say "=== D) ruta inexistente ==="
$app = Start-App (Join-Path $scratch 'no_existe.gif')
$box = TopWindow $app.Proc.Id 'LICEcap'
Assert ($box -ne [IntPtr]::Zero) "avisa tambien cuando la ruta no existe"
$stat = [WOpen]::TextOfClass($box, 'Static')
$mtxt = ''
if ($stat -ne [IntPtr]::Zero) { $mtxt = [WOpen]::FieldText($stat) }
Assert ($mtxt -like '*Could not open*') ("el aviso habla de no poder abrirla ('" + ($mtxt -replace "`r?`n", ' / ') + "')")
Key $box 0x0D
Start-Sleep -Milliseconds 700
Stop-App $app

# ===========================================================================
# E) raton real sobre el boton Edit: la flecha abre el menu, la etiqueta edita
#    (al arrancar con un GIF el editor ya esta abierto, asi que el primer clic
#    en la etiqueta lo cierra: es el mismo toggle de siempre)
# ===========================================================================
Say ""
Say "=== E) split button Edit con raton real ==="
$app = Start-App $srcgif
$h = $app.Hwnd
$r = ControlRect $h 1053
$y = [int](($r.T + $r.B) / 2)

ClickAt $h 1053 ($r.R - 8) $y                       # flecha (con el editor abierto)
$menu = PopupMenu $app.Proc.Id
Assert ($menu -ne [IntPtr]::Zero) "clic en la flecha abre el menu desplegable"
Shoot $h (Join-Path $shots '75_edit_menu.png')
Key $menu 0x1B
Start-Sleep -Milliseconds 500
Assert ((PopupMenu $app.Proc.Id 800) -eq [IntPtr]::Zero) "Esc cierra el menu"
Assert ((Title $h) -like "*src.gif (opened) - not saved*") "abrir el menu no toca lo pendiente"

ClickAt $h 1053 ($r.L + 18) $y                      # etiqueta: cierra el editor
Start-Sleep -Milliseconds 700
ClickAt $h 1053 ($r.L + 18) $y                      # etiqueta: lo vuelve a abrir
Start-Sleep -Milliseconds 900
Key $h 0x1B                                    # Esc: lo consume el editor
Start-Sleep -Milliseconds 600
Assert ($null -ne (Get-Process -Id $app.Proc.Id -ErrorAction SilentlyContinue)) "clic en la etiqueta abre el editor (Esc no cierra la app)"
Assert ((Title $h) -like "*src.gif (opened) - not saved*") "el GIF abierto sigue pendiente"

$boxes = 0
foreach ($l in [WOpen]::Dump([uint32]$app.Proc.Id, $false)) { if (($l -split '\|')[2] -eq 'LICEcap') { $boxes++ } }
Assert ($boxes -eq 0) "no quedo ningun aviso modal abierto"
if (-not $script:RealClicks) { Say "  (sin primer plano: los clics se mandaron como mensajes al boton)" }
Stop-App $app

Say ""
Say "TODO OK: abrir GIF (CLI, menu, Ctrl+O), recorte sobre la copia, Save As y errores"

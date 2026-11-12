# _uicheck8.ps1 -- editor de timeline (boton Edit).
#
# Graba unos segundos con el boton Rec (captura a un GIF temporal), abre el
# editor con IDC_EDITMODE, comprueba que aparece el strip con A/B y que el
# teclado mueve el playhead, recorta con A/B + Apply y valida el GIF resultante
# (numero de frames y duracion total) con Pillow/gifinfo.
#
# Uso: powershell -ExecutionPolicy Bypass -File _uicheck8.ps1

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WEdit {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
  public static string Text(IntPtr h) {
    var t=new StringBuilder(256); GetWindowTextA(h,t,256); return t.ToString();
  }
}
'@
[void][WEdit]::SetProcessDPIAware()

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$scratch = Join-Path $root "_edittest"
$shots   = Join-Path $root "_uicheck"
$src     = Join-Path $root "licecap\licecap\x64\Release\licecap.exe"
$exe     = Join-Path $scratch "licecap_editchk.exe"
$ini     = Join-Path $scratch "licecap.ini"

if (-not (Test-Path $src)) { throw "falta $src (compila primero)" }
New-Item -ItemType Directory -Force -Path $scratch, $shots | Out-Null
Get-Process licecap_editchk -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item $src $exe -Force
# ventana fija, tema oscuro, sin cursor, sin frame de titulo
Set-Content -Path $ini -NoNewline -Value (@('[licecap]','dark=1','maxfps=10','cursor=1','prefs=0','') -join "`r`n")

function Title([IntPtr]$h) { [WEdit]::Text($h) }

function Shoot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object WEdit+RECT
  [void][WEdit]::GetWindowRect($hwnd,[ref]$r)
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap($b.Width,$b.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0,0,0,0,(New-Object System.Drawing.Size($b.Width,$b.Height)))
  $g.Dispose()
  $crop = $bmp.Clone((New-Object System.Drawing.Rectangle($r.L,$r.T,($r.R-$r.L),($r.B-$r.T))),$bmp.PixelFormat)
  $crop.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
  $crop.Dispose(); $bmp.Dispose()
  "captura -> $path"
}

function Key([IntPtr]$h, [int]$vk) {
  [void][WEdit]::PostMessage($h,0x0100,[IntPtr]$vk,[IntPtr]::Zero)
  [void][WEdit]::PostMessage($h,0x0101,[IntPtr]$vk,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
}

function Cmd([IntPtr]$h, [int]$id) {
  [void][WEdit]::PostMessage($h,0x0111,[IntPtr]$id,[IntPtr]::Zero)
}

$before = Get-ChildItem $env:TEMP -Filter 'LICEcap_*.gif' -ErrorAction SilentlyContinue |
          Select-Object -ExpandProperty FullName

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh(); $h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "sin ventana principal" }
[void][WEdit]::SetWindowPos($h,[IntPtr](-1),160,60,620,480,0)
[void][WEdit]::SetCursorPos(40,700)   # lejos del boton Rec: graba, no abre menu
Start-Sleep -Milliseconds 600
"reposo: $(Title $h)"

$wr = New-Object WEdit+RECT
[void][WEdit]::GetWindowRect($h,[ref]$wr)

Cmd $h 1001                             # IDC_REC -> empieza a grabar
Start-Sleep -Milliseconds 3400         # PREROLL_AMT = 3s antes del primer frame
# sin cambios en pantalla LICEcap no escribe ni un frame: mueve el cursor por
# el area de captura para que haya diffs que grabar
$pts = @(@(120,90),@(300,140),@(180,260),@(360,300),@(240,180),@(90,320))
foreach ($round in 1..2) {
  foreach ($pt in $pts) {
    [void][WEdit]::SetCursorPos($wr.L + $pt[0], $wr.T + 26 + $pt[1])
    Start-Sleep -Milliseconds 130
  }
}
Start-Sleep -Milliseconds 300
Cmd $h 1001                             # IDC_REC -> Stop
Start-Sleep -Milliseconds 700
"grabado: $(Title $h)"

$new = Get-ChildItem $env:TEMP -Filter 'LICEcap_*.gif' |
       Where-Object { $before -notcontains $_.FullName } |
       Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $new) { throw "no se creo el GIF temporal de la grabacion" }
$pending = $new.FullName
"temporal: $pending"
Copy-Item $pending (Join-Path $scratch 'before.gif') -Force
Shoot $h (Join-Path $shots '40_pending.png')

# --- abrir el editor -------------------------------------------------------
Cmd $h 1053                             # IDC_EDITMODE
Start-Sleep -Milliseconds 900
Shoot $h (Join-Path $shots '41_editor.png')

# --- mover playhead y fijar B con el teclado -------------------------------
1..6 | ForEach-Object { Key $h 0x27 }   # VK_RIGHT x6
Key $h 0x42                             # 'B' -> B = playhead
Key $h 0x20                             # space -> play/pause (no debe romper)
Start-Sleep -Milliseconds 400
Key $h 0x20                             # space -> pausa
Start-Sleep -Milliseconds 300
Shoot $h (Join-Path $shots '42_editor_trimmed.png')

# --- aplicar el recorte ----------------------------------------------------
Key $h 0x0D                             # VK_RETURN -> Apply
Start-Sleep -Milliseconds 1200
Shoot $h (Join-Path $shots '43_after_apply.png')

$after = Join-Path $scratch 'after.gif'
if (Test-Path $pending) { Copy-Item $pending $after -Force }

# --- tercera fase: raton real (play + arrastrar A) -------------------------
Cmd $h 1053                             # reabrir el editor sobre el pendiente
Start-Sleep -Milliseconds 800

$cr = New-Object WEdit+RECT
[void][WEdit]::GetClientRect($h,[ref]$cr)
$dpi = [WEdit]::GetDpiForWindow($h); if ($dpi -eq 0) { $dpi = 96 }
$k = $dpi / 96.0
$stripL = 1*$k
$stripR = $cr.R - 88*$k - 2*$k
$stripB = $cr.B - 1*$k
$stripT = $stripB - 64*$k
$trackL = $stripL + 6*$k + 24*$k + 8*$k
$applyL = ($stripR - 6*$k - 44*$k) - 6*$k - 52*$k
$trackR = $applyL - 8*$k
$trackY = $stripT + 26*$k
$playX  = $stripL + 6*$k + 12*$k
$playY  = $stripT + 8*$k + 10*$k

function ToScreen([int]$x, [int]$y) {
  $pt = New-Object WEdit+POINT
  $pt.X = $x; $pt.Y = $y
  [void][WEdit]::ClientToScreen($h,[ref]$pt)
  return $pt
}
function Click([int]$x, [int]$y) {
  $p = ToScreen $x $y
  [void][WEdit]::SetForegroundWindow($h)
  [void][WEdit]::SetCursorPos($p.X,$p.Y)
  Start-Sleep -Milliseconds 120
  [WEdit]::mouse_event(2,0,0,0,[IntPtr]::Zero)   # LEFTDOWN
  Start-Sleep -Milliseconds 90
  [WEdit]::mouse_event(4,0,0,0,[IntPtr]::Zero)   # LEFTUP
  Start-Sleep -Milliseconds 120
}

Click $playX $playY                     # play real
Start-Sleep -Milliseconds 500
Click $playX $playY                     # pausa real
Shoot $h (Join-Path $shots '46_editor_mouse.png')

# arrastrar el handle A hasta el 60% de la pista
$ax = $trackL
$bx = $trackL + [int](($trackR - $trackL) * 0.6)
$p0 = ToScreen ([int]$ax) ([int]$trackY)
[void][WEdit]::SetCursorPos($p0.X,$p0.Y)
Start-Sleep -Milliseconds 150
[WEdit]::mouse_event(2,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 120
$steps = 6
for ($i = 1; $i -le $steps; $i++) {
  $px = [int]($ax + ($bx - $ax) * $i / $steps)
  $pp = ToScreen $px ([int]$trackY)
  [void][WEdit]::SetCursorPos($pp.X,$pp.Y)
  Start-Sleep -Milliseconds 70
}
[WEdit]::mouse_event(4,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 300
Shoot $h (Join-Path $shots '47_editor_dragA.png')

Key $h 0x1B                             # Esc -> cerrar sin recortar
Start-Sleep -Milliseconds 600
Shoot $h (Join-Path $shots '48_after_esc.png')
$after3 = Join-Path $scratch 'after3.gif'
if (Test-Path $pending) { Copy-Item $pending $after3 -Force }

# --- segunda fase: recortar el INICIO (A > 0) ------------------------------
Cmd $h 1053                             # IDC_EDITMODE -> editor sobre el ya recortado
Start-Sleep -Milliseconds 800
1..4 | ForEach-Object { Key $h 0x27 }   # playhead al frame 5
Key $h 0x41                             # 'A' -> A = playhead
Key $h 0x23                             # VK_END -> ultimo frame
Key $h 0x42                             # 'B' -> B = ultimo
Start-Sleep -Milliseconds 300
Shoot $h (Join-Path $shots '44_editor_start.png')
Key $h 0x0D                             # Apply
Start-Sleep -Milliseconds 1200
Shoot $h (Join-Path $shots '45_after_apply_start.png')
$after2 = Join-Path $scratch 'after2.gif'
if (Test-Path $pending) { Copy-Item $pending $after2 -Force }

Stop-Process -Id $p.Id -Force
Start-Sleep -Milliseconds 300

$py = Join-Path $scratch 'gifinfo.py'
@'
import sys

def info(path):
    d = open(path, 'rb').read()
    assert d[:6] in (b'GIF87a', b'GIF89a'), 'no es un GIF: ' + path
    w = d[6] | d[7] << 8
    h = d[8] | d[9] << 8
    packed = d[10]
    pos = 13
    if packed & 0x80:
        pos += 3 * (2 << (packed & 7))
    frames = 0
    total = 0
    delay = 0
    sizes = []
    while pos < len(d):
        b = d[pos]; pos += 1
        if b == 0x3b:
            break
        if b == 0x21:
            label = d[pos]; pos += 1
            if label == 0xf9:
                sz = d[pos]; pos += 1
                if sz >= 4:
                    delay = d[pos+1] | d[pos+2] << 8
                pos += sz
                while pos < len(d) and d[pos]:
                    pos += d[pos] + 1
                pos += 1
            else:
                while pos < len(d):
                    sz = d[pos]; pos += 1
                    if sz == 0:
                        break
                    pos += sz
            continue
        if b == 0x2c:
            fw = d[pos+4] | d[pos+5] << 8
            fh = d[pos+6] | d[pos+7] << 8
            ip = d[pos+8]; pos += 9
            if ip & 0x80:
                pos += 3 * (2 << (ip & 7))
            pos += 1
            while pos < len(d):
                sz = d[pos]; pos += 1
                if sz == 0:
                    break
                pos += sz
            frames += 1
            sizes.append((fw, fh))
            total += delay or 10  # centisegundos
            delay = 0
            continue
        break
    return w, h, frames, total, sizes

for path in sys.argv[1:]:
    try:
        w, h, n, ms, sizes = info(path)
        print("%s: %dx%d frames=%d dur=%dms" % (path, w, h, n, ms * 10))
        if n:
            full = sum(1 for s in sizes if s == (w, h))
            print("   frames de tamano completo: %d/%d" % (full, n))
    except Exception as e:
        print("%s: ERROR %s" % (path, e))
'@ | Set-Content -Path $py -Encoding UTF8

"--- GIF antes / despues ---"
python $py (Join-Path $scratch 'before.gif') $after $after2

"--- comparacion de frames (A>0) ---"
@'
import sys
import numpy as np
from PIL import Image, ImageSequence

a = [np.asarray(f.convert('RGB'), dtype=np.int16) for f in ImageSequence.Iterator(Image.open(sys.argv[1]))]
b = [np.asarray(f.convert('RGB'), dtype=np.int16) for f in ImageSequence.Iterator(Image.open(sys.argv[2]))]
print("base frames:", len(a), " recortado por el inicio:", len(b))
if len(b) >= 1:
    d = np.abs(a[4] - b[0])
    print("frame 0 del recortado vs frame 4 de la base: mean=%.2f max=%d" % (d.mean(), d.max()))
    d2 = np.abs(a[-1] - b[-1])
    print("ultimo frame del recortado vs ultimo de la base: mean=%.2f max=%d" % (d2.mean(), d2.max()))
'@ | Set-Content -Path (Join-Path $scratch 'cmp.py') -Encoding UTF8
python (Join-Path $scratch 'cmp.py') $after $after2

"--- Esc no debe tocar el archivo ---"
if ((Get-FileHash $after).Hash -eq (Get-FileHash $after3).Hash) {
  "OK: el pendiente sigue igual tras abrir/cerrar el editor"
} else {
  "FALLO: el archivo cambio sin aplicar"
}

Remove-Item $exe, $ini -ErrorAction SilentlyContinue
"listo"

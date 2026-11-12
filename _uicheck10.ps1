# _uicheck10.ps1 -- indicador de "ocupado" en las llamadas bloqueantes.
#
# Graba unos segundos, abre el editor (READING) y aplica el recorte (APPLYING)
# tomando capturas en ráfaga mientras la ventana está trabajando. Comprueba que
# el panel de estado dice que está ocupado y que el GIF resultante sigue siendo
# válido (frames y duración) después del cambio.
#
# Uso: powershell -ExecutionPolicy Bypass -File _uicheck9.ps1

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WBusy {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern int GetWindowTextA(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static string Text(IntPtr h) {
    var t=new StringBuilder(256); GetWindowTextA(h,t,256); return t.ToString();
  }
}
'@
[void][WBusy]::SetProcessDPIAware()

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$scratch = Join-Path $root "_edittest"
$shots   = Join-Path $root "_uicheck"
$src     = Join-Path $root "licecap\licecap\x64\Release\licecap.exe"
$exe     = Join-Path $scratch "licecap_busychk.exe"
$ini     = Join-Path $scratch "licecap.ini"

if (-not (Test-Path $src)) { throw "falta $src (compila primero)" }
New-Item -ItemType Directory -Force -Path $scratch, $shots | Out-Null
Get-Process licecap_busychk -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item $src $exe -Force
Set-Content -Path $ini -NoNewline -Value (@('[licecap]','dark=1','maxfps=10','cursor=1','prefs=0','') -join "`r`n")

function Title([IntPtr]$h) { [WBusy]::Text($h) }

function Shoot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object WBusy+RECT
  [void][WBusy]::GetWindowRect($hwnd,[ref]$r)
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
  [void][WBusy]::PostMessage($h,0x0100,[IntPtr]$vk,[IntPtr]::Zero)
  [void][WBusy]::PostMessage($h,0x0101,[IntPtr]$vk,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
}
function Cmd([IntPtr]$h, [int]$id) { [void][WBusy]::PostMessage($h,0x0111,[IntPtr]$id,[IntPtr]::Zero) }

$before = Get-ChildItem $env:TEMP -Filter 'LICEcap_*.gif' -ErrorAction SilentlyContinue |
          Select-Object -ExpandProperty FullName

$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2
$p.Refresh(); $h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "sin ventana principal" }
[void][WBusy]::SetWindowPos($h,[IntPtr](-1),100,40,1100,800,0)
[void][WBusy]::SetCursorPos(40,700)
Start-Sleep -Milliseconds 600
"reposo: $(Title $h)"

$wr = New-Object WBusy+RECT
[void][WBusy]::GetWindowRect($h,[ref]$wr)

# --- grabar ~9 s con diffs reales -----------------------------------------
Cmd $h 1001
Start-Sleep -Milliseconds 3400          # preroll de 3 s
$pts = @(@(200,90),@(700,140),@(400,500),@(900,600),@(500,300),@(150,700),@(800,200),@(300,400))
foreach ($round in 1..5) {
  foreach ($pt in $pts) {
    [void][WBusy]::SetCursorPos($wr.L + $pt[0], $wr.T + 26 + $pt[1])
    Start-Sleep -Milliseconds 150
  }
}
Start-Sleep -Milliseconds 300
Cmd $h 1001
Start-Sleep -Milliseconds 900
"grabado: $(Title $h)"

# el temporal puede dejar hermanos de 1 byte: el bueno es el gordo y reciente
$new = Get-ChildItem $env:TEMP -Filter 'LICEcap_*.gif' |
       Where-Object { $before -notcontains $_.FullName -and $_.Length -gt 1024 } |
       Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $new) { throw "no se creo el GIF temporal" }
$pending = $new.FullName
"temporal: $pending ($([int]($new.Length/1024)) KB)"
Copy-Item $pending (Join-Path $scratch 'before9.gif') -Force

# --- READING: abrir el editor y capturar mientras decodifica ---------------
Cmd $h 1053
$t0 = Get-Date
foreach ($i in 1..4) {
  Shoot $h (Join-Path $shots ("50_open_busy_{0}.png" -f $i))
  Start-Sleep -Milliseconds 120
}
"abrir editor tardo ~$([int]((Get-Date)-$t0).TotalMilliseconds) ms (capturas incluidas)"
Start-Sleep -Milliseconds 500
Shoot $h (Join-Path $shots '51_editor.png')

# --- APPLYING: aplicar y capturar en ráfaga -------------------------------
$t0 = Get-Date
Key $h 0x0D                             # VK_RETURN -> Apply
foreach ($i in 1..12) {
  Shoot $h (Join-Path $shots ("52_apply_busy_{0:00}.png" -f $i))
  Start-Sleep -Milliseconds 250
}
$applyMs = [int]((Get-Date)-$t0).TotalMilliseconds
Start-Sleep -Milliseconds 3000
Shoot $h (Join-Path $shots '53_after_apply.png')
"apply + 12 capturas: ~$applyMs ms ; despues: $(Title $h)"

$after = Join-Path $scratch 'after9.gif'
$alive = -not $p.HasExited
if (Test-Path $pending) { Copy-Item $pending $after -Force }

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

"--- proceso vivo tras apply: $alive ---"
"--- GIF antes / despues ---"
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
            total += delay or 10
            delay = 0
            continue
        break
    return w, h, frames, total * 10

for path in sys.argv[1:]:
    try:
        w, h, n, ms = info(path)
        print("%s: %dx%d frames=%d dur=%dms" % (path, w, h, n, ms))
    except Exception as e:
        print("%s: ERROR %s" % (path, e))
'@ | Set-Content -Path (Join-Path $scratch 'busyinfo.py') -Encoding UTF8
python (Join-Path $scratch 'busyinfo.py') (Join-Path $scratch 'before9.gif') $after

Remove-Item $exe, $ini -ErrorAction SilentlyContinue
"listo"

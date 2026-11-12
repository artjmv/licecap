# _uicheck7.ps1 -- el dialogo Settings se lee en tema oscuro.
#
# Regresion del bug "hay textos que no se leen": uxtheme no recolorea las
# etiquetas de BS_GROUPBOX ni las de los checkboxes/radios, asi que en el tema
# oscuro quedaban casi negras sobre fondo negro. Ahora esos controles se pintan
# a mano (licecap_ui.cpp, UI_DarkPaintControls).
#
# Corre una COPIA del Release x64 en _settest\ (con su propio licecap.ini),
# abre el dialogo con IDM_CUSTOMIZE y:
#   1. captura el dialogo en tema oscuro          -> _uicheck\customize_dark1.png
#   2. captura el dialogo en tema claro           -> _uicheck\customize_dark0.png
#   3. BM_CLICK en dos checkboxes, captura, OK y comprueba "prefs" en el ini
#      (los controles siguen siendo checkboxes de verdad, no owner-draw)
#
# Uso: powershell -ExecutionPolicy Bypass -File _uicheck7.ps1

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Text; using System.Runtime.InteropServices;
public class WU7 {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb,IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc cb,IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public delegate bool EnumProc(IntPtr h,IntPtr p);
  public static string Text(IntPtr h) {
    var t=new StringBuilder(256); GetWindowText(h,t,256); return t.ToString();
  }
  public static IntPtr FindTitle(uint pid,string part) {
    IntPtr found=IntPtr.Zero;
    EnumProc cb=delegate(IntPtr h,IntPtr p){
      uint w; GetWindowThreadProcessId(h,out w);
      if(w==pid && IsWindowVisible(h) && Text(h).Contains(part)) { found=h; return false; }
      return true;};
    EnumWindows(cb,IntPtr.Zero); return found;
  }
  // el dialogo Settings se busca por una de sus etiquetas: el titulo lo fija el .rc
  public static IntPtr FindDialogByChildText(uint pid,string part) {
    IntPtr found=IntPtr.Zero;
    EnumProc cb=delegate(IntPtr h,IntPtr p){
      uint w; GetWindowThreadProcessId(h,out w);
      if(w!=pid || !IsWindowVisible(h)) return true;
      EnumProc child=delegate(IntPtr c,IntPtr q){
        if(Text(c).Contains(part)) { found=h; return false; }
        return true;};
      EnumChildWindows(h,child,IntPtr.Zero);
      return found==IntPtr.Zero;};
    EnumWindows(cb,IntPtr.Zero); return found;
  }
}
'@
[void][WU7]::SetProcessDPIAware()
[void][WU7]::SetCursorPos(5,900)  # cursor lejos: evita tooltips en la captura

$proj    = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe     = Join-Path $proj "licecap\licecap\x64\Release\licecap.exe"
$scratch = Join-Path $proj "_settest"
$shots   = Join-Path $proj "_uicheck"
if (-not (Test-Path $exe)) { throw "falta $exe (compila primero)" }
New-Item -ItemType Directory -Force -Path $scratch,$shots | Out-Null

function Shoot([IntPtr]$hwnd, [string]$path) {
  $r = New-Object WU7+RECT
  [void][WU7]::GetWindowRect($hwnd,[ref]$r)
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap($b.Width,$b.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0,0,0,0,(New-Object System.Drawing.Size($b.Width,$b.Height)))
  $g.Dispose()
  $w = $r.R-$r.L; $h = $r.B-$r.T
  $crop = $bmp.Clone((New-Object System.Drawing.Rectangle($r.L,$r.T,$w,$h)),$bmp.PixelFormat)
  $crop.Save($path,[System.Drawing.Imaging.ImageFormat]::Png)
  $crop.Dispose(); $bmp.Dispose()
  "  -> $path ($w x $h)"
}

function OpenSettings([int]$dark, [scriptblock]$body) {
  Copy-Item $exe (Join-Path $scratch "licecap.exe") -Force
  "[licecap]`r`ndark=$dark`r`n" | Set-Content -Path (Join-Path $scratch "licecap.ini") -Encoding ASCII
  $p = Start-Process -FilePath (Join-Path $scratch "licecap.exe") -PassThru
  try {
    $main = [IntPtr]::Zero
    for ($i=0; $i -lt 80 -and $main -eq [IntPtr]::Zero; $i++) {
      Start-Sleep -Milliseconds 100
      if ($p.HasExited) { throw "la app se cerro al arrancar" }
      $main = [WU7]::FindTitle([uint32]$p.Id,"LICEcap")
    }
    [void][WU7]::PostMessage($main,0x0111,[IntPtr]40021,[IntPtr]::Zero)  # IDM_CUSTOMIZE
    $dlg = [IntPtr]::Zero
    for ($i=0; $i -lt 80 -and $dlg -eq [IntPtr]::Zero; $i++) {
      Start-Sleep -Milliseconds 100
      $dlg = [WU7]::FindDialogByChildText([uint32]$p.Id,"Frame rate")
    }
    if ($dlg -eq [IntPtr]::Zero) { throw "no se abrio el dialogo Settings" }
    Start-Sleep -Milliseconds 600
    & $body $p $main $dlg
  }
  finally {
    if (-not $p.HasExited) { try { $p.Kill() } catch {} ; [void]$p.WaitForExit(8000) }
    Start-Sleep -Milliseconds 300
  }
}

$fail = 0
function Check([bool]$ok,[string]$what) {
  if ($ok) { "OK   $what" } else { $script:fail++; "FAIL $what" }
}

"tema oscuro:"
$darkShot = Join-Path $shots 'customize_dark1.png'
OpenSettings 1 {
  param($p,$main,$dlg)
  Shoot $dlg $darkShot
}

"tema claro:"
$lightShot = Join-Path $shots 'customize_dark0.png'
OpenSettings 0 {
  param($p,$main,$dlg)
  Shoot $dlg $lightShot
}

"checkbox pintados a mano siguen funcionando (BM_CLICK + OK):"
OpenSettings 1 {
  param($p,$main,$dlg)
  foreach ($id in 1036,1042) {   # IDC_CAP_CLICKS, IDC_TITLEUSE2
    $c = [WU7]::GetDlgItem($dlg,$id)
    [void][WU7]::SendMessage($c,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)  # BM_CLICK
    Start-Sleep -Milliseconds 250
  }
  Start-Sleep -Milliseconds 400
  Shoot $dlg (Join-Path $shots 'customize_clicked_dark1.png')
  [void][WU7]::PostMessage($dlg,0x0111,[IntPtr]1,[IntPtr]::Zero)      # IDOK
  Start-Sleep -Milliseconds 1200
  [void][WU7]::PostMessage($main,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) # WM_CLOSE
  Start-Sleep -Milliseconds 800
}
$ini = Get-Content (Join-Path $scratch "licecap.ini") -Raw
Check ($ini -match 'prefs=5') "prefs=5 (title frame + highlight clicks) en el ini: $(($ini -split "`n" | Where-Object {$_ -match 'prefs'}).Trim())"

foreach ($f in $darkShot,$lightShot) { Check (Test-Path $f) "existe $([IO.Path]::GetFileName($f))" }
Remove-Item -Recurse -Force $scratch
""
if ($fail) { "FALLARON $fail comprobaciones"; exit 1 } else { "todo OK" }

# Building LICEcap on Windows

The Windows GUI build uses the Visual Studio project
`licecap/licecap/licecap.vcxproj` (solution `licecap/licecap/licecap.sln`).
There is no CMake/Makefile build for the Windows GUI.

All third-party code needed at build time is vendored in `WDL/` (WDL itself,
`zlib`, `giflib`, `lice`), so no external packages / vcpkg are required.

## Requirements

- Visual Studio 2022 (or newer) with the **Desktop development with C++**
  workload (MSVC toolset `v143` or later + Windows 10/11 SDK).
- MSBuild is shipped with Visual Studio; no need for it to be on `PATH`.

The project was originally `PlatformToolset=v120` (VS2013); it has been
retargeted to `v143`. On a machine with a different toolset, override it on the
command line instead of editing the project:

```
-p:PlatformToolset=v143
```

## Configure + build (Debug, x64)

No separate configure step is needed; MSBuild reads the project directly.
Run from a normal `cmd`/bash shell (no Developer Prompt required):

```
cd E:\Proyectos\licecap\licecap\licecap
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  licecap.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -v:m
```

Output: `E:\Proyectos\licecap\licecap\licecap\x64\Debug\licecap.exe`

## Build (Debug, Win32)

```
cd E:\Proyectos\licecap\licecap\licecap
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  licecap.vcxproj -p:Configuration=Debug -p:Platform=Win32 -m -v:m
```

Output: `E:\Proyectos\licecap\licecap\licecap\Debug_new\licecap.exe`
(the Win32 configurations override `OutDir` to `$(SolutionDir)Debug_new\`).

## Clean

```
cd E:\Proyectos\licecap\licecap\licecap
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  licecap.vcxproj -t:Clean -p:Configuration=Debug -p:Platform=x64
```

or simply delete the generated `x64\`, `Debug_new\` and intermediate
`licecap\<Platform>\<Configuration>\` folders.

## Build (Release, x64) — executable meant for daily use

```
cd E:\Proyectos\licecap\licecap\licecap
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  licecap.vcxproj -p:Configuration=Release -p:Platform=x64 -m -v:m
```

Output: `E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe`
(~379 KB). The Release configuration links the static CRT
(`/MT`), so the EXE depends only on Windows system DLLs:

```
KERNEL32.dll USER32.dll GDI32.dll COMDLG32.dll ADVAPI32.dll ole32.dll WINMM.dll
```

No VC++ redistributable, no DLLs to copy: it is portable.

## Run

```
E:\Proyectos\licecap\run-licecap.bat
%USERPROFILE%\Desktop\run-licecap.bat   (copia en el escritorio)
```

Both files are identical. The launcher resolves the project by absolute path
(`E:\Proyectos\licecap`), so it works when double-clicked from the Desktop; it
falls back to `%~dp0..` / `%~dp0` if the project is moved. If the project is
moved to another drive, edit the `PROJ` line of the `.bat`.

or directly one of:

```
E:\Proyectos\licecap\dist\LICEcap-1.32-win64\licecap.exe   (portable copy, recommended)
E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe
E:\Proyectos\licecap\licecap\licecap\x64\Debug\licecap.exe
```

It is a GUI (`/SUBSYSTEM:WINDOWS`) app: a single capture window appears, with
the capture size in the title (`LICEcap 512 x 512`) and a Rec/Frame/Edit/Save
toolbar on the right (see the next section). Close it with the window close
button or Esc (there is no console output and no `--help`).

## GifCam-style interface

The Windows GUI was redesigned to look and behave like GifCam (the reference
screenshots are a 512x512 GifCam window with a vertical toolbar on the right).

Layout
------

- Standard Windows caption, no menu bar. The caption shows the capture size
  (`LICEcap 512 x 512`) and, while capturing, the state
  (`LICEcap 512 x 512 - recording 0:07 (18 fps)`, `- paused 0:03`,
  `- starting in 2`, or `- not saved`).
- A flat, light side toolbar on the right with four owner-drawn buttons:
  `Rec` (with drop-down arrow), `Frame`, `Edit`, `Save` (with drop-down arrow).
  Hover/press/focus states are drawn with GDI (accent colour `#2F7FD1`), the
  column ends with a small status block (`404 x 469 / 16 fps / Click Rec`).
- The capture area is a real hole in the window region, so the desktop shows
  through and stays click-through; only the toolbar and the 1px frame are part
  of the window. The whole client area can be dragged like a caption and the
  normal thick frame resizes it.
- Layout is DPI-scaled (`UI(px)` helper); the capture area is everything left
  of the 88px toolbar, so resizing keeps the toolbar anchored to the right.

Behaviour
---------

- `Rec` starts capturing **immediately** into a temporary GIF
  (`%TEMP%\LICEcap_YYYYMMDD_HHMMSS.gif`); no file dialog appears. While
  recording the button shows `Stop` in red and clicking it finalises the GIF.
  `Rec` on a paused capture resumes it.
- After stopping, `Save` becomes active (the label darkens) and `Save As...`
  moves the temporary GIF to the path you choose (default name
  `gifcam_YYYY-MM-DD_HH-MM-SS.gif`). `Show in Folder` opens Explorer on it.
- `Rec` drop-down: New, 10/16/30 FPS (check-marked), Custom FPS...,
  Capture Cursor, Highlight Mouse Clicks, Show Elapsed Time, Full Screen,
  Customization...
- `Frame` drop-down: Pause/Resume, Insert Text Frame..., Insert Title Frame,
  Cancel Recording.
- `Edit` drop-down: Reset Window Size (512 x 512), Full Screen, Always on Top,
  Customization...
- `Save` drop-down: Save As..., Show in Folder, Record to File...,
  Customization..., About LICEcap.
- `Record to File...` keeps the classic LICEcap flow (file name + format and
  the old save-options dialog: title frame, elapsed time, loop count,
  transparency, stop-after, `.lcf`/`.webm`), for users who need it.
- Right-clicking any toolbar button opens its menu.
- Closing with an unsaved recording asks whether to save it; closing during a
  capture offers to stop it first.

Keyboard

```
F1 / F2 / F3       10 / 16 / 30 FPS
F4                 pause / resume
Ctrl+N             new (discard pending capture)
Ctrl+S             Save As
Ctrl+U             Customization
Ctrl+Alt+P         start / stop capture (existing global hotkey pref)
Esc                close when idle
```

The shortcuts are handled by a WH_GETMESSAGE hook installed on the UI thread
(a DialogBox has no message loop of its own to attach an accelerator table to).

Customization dialog (`IDD_CUSTOMIZATION`)

- Frame rate: 10 / 16 / 30 / Custom (1-60 FPS).
- Capture: mouse cursor, mouse-click highlight, elapsed-time overlay,
  stop automatically after N seconds.
- GIF output: loop count, transparency, title frame (text, seconds, big font).
- Window size: width/height of the capture area, applied on OK.

New preferences stored in `licecap.ini`: `cursor` (capture mouse cursor) and
`ontop` (always on top), plus `lastsave` (last Save As path). Everything else
reuses the existing keys (`maxfps`, `prefs`, `titlems`, `gifloopcnt`,
`stopafter`, `wnd_r`, `title`).

## Portable "installed" copy inside the repo

`dist/LICEcap-1.32-win64/` holds the self-contained Release build plus the
license and changelog, i.e. the ready-to-run application:

```
dist/LICEcap-1.32-win64/
├── licecap.exe     (Release x64, static CRT)
├── license.txt
└── whatsnew.txt
```

It is portable: the folder can be copied/moved anywhere (including other
machines) and `licecap.exe` runs as-is. Nothing was written to
`C:\Program Files`, the registry or the Start Menu.

The only file created outside the repository is the requested shortcut script
`%USERPROFILE%\Desktop\run-licecap.bat` (o `%USERPROFILE%\OneDrive\Desktop\...`
si el escritorio está redirigido a OneDrive).

There is a NSIS script (`licecap/installer.nsi`) for a traditional installer,
but NSIS (`makensis`) is **not installed** on this machine, and building it
would write outside the repository, so no system-wide installer was produced.

## Windows-specific source changes that were required

1. `licecap/licecap.rc` included `afxres.h` (MFC-only). It now includes
   `<winres.h>` from the Windows SDK, which provides the same dialog/control
   constants (`IDC_STATIC`) used by the resource script.
2. `licecap/licecap/licecap.vcxproj`: `PlatformToolset` changed from `v120`
   (VS2013, not installed) to `v143` (VS2022).
3. `licecap/licecap.rc`: `IDD_DIALOG1` was rebuilt as the GifCam-style capture
   window (capture area + four owner-drawn toolbar buttons + status block) and
   `IDD_CUSTOMIZATION` was added; `licecap/licecap_ui.cpp` gained the owner-draw
   painting, menu, layout and record/save logic for it.

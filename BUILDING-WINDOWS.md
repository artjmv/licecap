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

It is a GUI (`/SUBSYSTEM:WINDOWS`) app: a single window titled
`LICEcap v1.32 [stopped]` appears. Close it with the window close button
(there is no console output and no `--help`).

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

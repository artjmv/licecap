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

Passing an existing GIF as the only argument opens it in the timeline editor
straight away (it is edited through a temporary copy, see `Edit` below):

```
licecap.exe "E:\capturas\anim.gif"
```

## GifCam-style interface

The Windows GUI was redesigned to look and behave like GifCam (the reference
screenshots are a 512x512 GifCam window with a vertical toolbar on the right).

Layout
------

- No menu bar. The title bar is drawn by the app (app icon, capture size and
  a close button) instead of using the system caption: the window keeps a
  region for the capture hole, and Windows draws classic light frames for
  region windows, so `WM_INITDIALOG` drops `WS_CAPTION` and the caption is
  painted with the same GDI palette as the toolbar. It shows the capture size
  (`LICEcap 512 x 512`) and, while capturing, the state
  (`LICEcap 512 x 512 - recording 0:07 (18 fps)`, `- paused 0:03`,
  `- starting in 2`, or `- not saved`).
- A flat, light side toolbar on the right with four owner-drawn buttons:
  `Rec` (with drop-down arrow), `Settings`, `Save` (with drop-down arrow)
  and `Presets`. Hover/press/focus states are drawn with GDI (accent colour
  `#2F7FD1`), the column ends with a small status block
  (`404 x 469 / 16 fps / Click Rec`). On very small windows the buttons shrink
  and the status block hides itself, so even a 128 x 128 capture stays usable.
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
- `Rec` drop-down: New, Full Screen. Every preference lives under
  `Settings`, so this menu does not repeat it.
- `Settings` drop-down (the old `Edit` button): 10/16/30 FPS (check-marked),
  Custom FPS..., Capture Cursor, Highlight Mouse Clicks, Show Elapsed Time,
  Dark Theme, Always on Top, Reset Window Size (512 x 512), Full Screen,
  Settings... and About LICEcap.
- `Edit` is a split button: the label trims the pending recording, or opens a
  GIF from disk when nothing is pending; the arrow drops `Edit Last
  Recording` and `Open GIF to Edit...` (Ctrl+O). An opened GIF is copied to
  `%TEMP%\LICEcap_edit_*.gif` and that copy becomes the pending recording, so
  `Apply` never rewrites the file that was opened: `Save As` proposes
  `<name>-edit.gif` in the folder the GIF came from. A file that is not a GIF
  is refused before anything pending is replaced.
- `Save` drop-down: Save As..., Show in Folder, Bookmarks..., Record to File...
  With a recording waiting to be saved and at least one bookmark, the arrow of
  the split button opens the bookmark list itself as a drop-down under the
  button (see below) instead of this menu.
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
/                  presets at half size (toggle; Shift+7 on es-* layouts)
Ctrl+N             new (discard pending capture)
Ctrl+S             Save As
Ctrl+O             Open GIF to Edit (picker; the file is edited through a copy)
Ctrl+U             Settings dialog
Ctrl+Alt+P         start / stop capture (existing global hotkey pref)
Esc                close when idle
```

Capture presets (`Presets` button)
----------------------------------

The space under `Save` holds a `Presets` button (dimmed while a capture is
running, because the window cannot be resized then). One click opens the
social-media sizes; the active size is check-marked and its name is shown in
the status block:

```
Instagram post            1080 x 1080
Instagram portrait        1080 x 1350
Instagram / TikTok        1080 x 1920      (also Reels / Shorts)
X / Twitter post          1200 x 675
X / YouTube HD            1280 x 720
YouTube / web             1920 x 1080
Facebook / LinkedIn       1200 x 630
LinkedIn post             1200 x 627
Discord / Slack emoji     128 x 128
Sticker / avatar          320 x 320
Classic screen            640 x 480
Classic screen            800 x 600
Square                    1024 x 1024
Half Size (/)             toggle: same presets at half dimensions
Settings... / Full Screen
```

Selecting a preset resizes the window so that the *capture area* is exactly
that size (the 88px toolbar and window frame are excluded); if the window
would leave the visible desktop it is anchored to the nearest screen edge.
`Settings...` opens the Settings dialog, which also validates sizes
(down to the 96 x 96 minimum). The preset is not stored separately: the window
rectangle is persisted as usual in `wnd_r`.

`Half Size` (also the `/` key; numpad `/` works and on Spanish layouts it is
`Shift+7`, resolved with `VkKeyScanA('/')` for the active layout) switches the
whole preset list to half dimensions, e.g. `Instagram post` becomes 540 x 540,
so a scaled desktop can still capture the intended pixel count. While the mode
is on the menu relabels every size, `Half Size` stays check-marked, the
`Presets` button reads `Presets /2`, the title bar appends `(/2)` and the
status block says e.g. `Square (/2)`. Presets whose half would fall below the
96 x 96 minimum (e.g. `Discord / Slack emoji`) are greyed out, and toggling off
restores the previous full size. The mode is per session and is ignored while a
capture is running.

The shortcuts are handled by a WH_GETMESSAGE hook installed on the UI thread
(a DialogBox has no message loop of its own to attach an accelerator table to).

Save bookmarks (Blender-style output folders)
---------------------------------------------

The `Save` arrow is the way to the output folders: with a recording waiting to
be saved and at least one bookmark, clicking it drops the folder list right
under the button, and one click on a row writes the GIF there with an automatic
name. That skips the usual "Save As -> pick folder -> type name -> OK" round
trip for the folders you use every day. With nothing pending (or with no
bookmarks yet) the arrow opens the command menu instead, so a row click always
does something:

```
[ Save ]  <- arrow opens the folder list, right under the button
+-----------------------------+
| Bookmarks               [+] |  add the last save folder
|  [] gifs                [v] |  browse for a folder
|  [] capturas            [-] |  remove the selected row
|                         [^] |  move the selected row up
|                         [v] |  move the selected row down
+-----------------------------+
```

The command menu (also on right click) keeps the actions and reopens the same
list:

```
Save As...                            Ctrl+S
Show in Folder
------------------------------------
Bookmarks...                         (opens the folder list)
------------------------------------
Record to File...
```

The list is a small popup under the `Save` button, laid out like the Blender
bookmark box: the folder list on the left, the action column on the right.
Right under `Save` there is a permanent drop-down row (`IDC_BOOKMARKDROP`, a
compact owner-drawn field: folder glyph, folder name, chevron) that shows the
folder used last and opens that same list, with the last used row already
selected and scrolled into view.

- The row is visible all the time, not only with a recording waiting: it makes
the destination explicit and doubles as the way into the list. It is disabled
while recording or while the timeline editor is open, like `Presets`.
- The last used folder is stored by **path** in the ini (`[bookmarks] last`) and
resolved back to an index on load, so reordering or removing other entries
never turns it into a different folder; removing the selected one moves the
selection to its neighbour, and a list from an older build (no `last` yet)
falls back to the first entry.
- A row click with a recording pending saves there (as before); with nothing
pending it only picks the destination (and closes the list), which is what the
row then shows. `Save As...` starts in that folder when there is no last save
path yet, and saving through the dialog into a bookmarked folder also updates
the selection.

- Clicking a row saves the pending recording into that folder. The row also
  becomes the selected one, which is what `-`, `^` and `v` act on.
- Keys: `Up`/`Down` select, `Enter` saves, `Insert` adds, `Delete` removes and
  `Esc` closes. A click outside the panel closes it too, but it stays open while
  you add, remove or reorder, like Blender's sidebar.
- `+` is only enabled when there is a last save folder (a temporary recording in
  `%TEMP%` does not count); the folder button always opens the picker.
- A bookmark is only usable while a recording is waiting to be saved; the file
  name is `gifcam_YYYY-MM-DD_HH-MM-SS.gif`, suffixed with `-2`, `-3`... if that
  name already exists in the folder. After saving, the folder becomes the new
  "last save" one, so `Save As...` and `Show in Folder` follow it.
- The folder button opens the modern shell picker (`IFileOpenDialog` +
  `FOS_PICKFOLDERS`, starting in the last save folder, with
  `SHBrowseForFolder` as a fallback).
- Duplicates are ignored (an explicit "already in the bookmarks" message is
  shown, so the `+` button never looks dead), paths are stored without trailing
  slashes and up to `LICECAP_MAX_BOOKMARKS` (24) entries are kept; a missing
  folder is reported when used and can be dropped from the list right there.
- Bookmarks persist in their own `[bookmarks]` section of `licecap.ini`
  (`count` plus `b1`...`bN`), so the existing `[licecap]` keys written by
  `SaveConfig` are untouched. The section is flushed to disk right after every
  change, and loading tolerates gaps in the `bN` keys (a half-written section
  no longer truncates the list). The first bookmark is also used as the initial
  folder of `Save As...` when there is no last save yet.
  `[bookmarks] last` (the path of the folder used last, i.e. what the
drop-down row shows) is written by the same `Bookmarks_Save()` call.

Verification: `_uicheck5.ps1` copies the Release x64 to `_bmtest\bin` (with its
own `licecap.ini`, so the real configuration is never touched), seeds two
bookmarks and checks the whole flow: menu and drop-down screenshots
(`20_main.png`, `21_save_menu.png`, `22_recorded.png`, `22b_save_dropdown.png`,
`24_after_restart.png`, `25_menu_after_restart.png`), the arrow of `Save`
opening that drop-down when a recording is pending (`22b_save_dropdown.png`, and
the geometry: it starts right below the button and its right edge lines up with
the button's), a real Rec/Stop capture saved with a **click on the first row** of
the drop-down, no duplication on `IDM_BOOKMARK_ADD` (the warning box is checked
and dismissed), reorder/remove persisted in the ini, the list surviving a
close/reopen (`WM_CLOSE` + `SaveConfig`), and the list itself (`27_panel.png`,
`28_panel_empty.png`): it opens from `IDM_BOOKMARKS_PANEL`, does not close by
itself, the `-` button removes the selected row and `Esc` closes it. The folder
picker is exercised too (`26_folder_picker.png`). `_bmtest\` is deleted
afterwards.

`_uicheck11.ps1` covers the drop-down row under `Save` (copy of the Release x64
in `_dropchk\bin` with its own ini, three bookmarks): the row sits right under
`Save` (same right edge, 18px high), a real click opens the list under it
aligned to its right edge (`61_dropdown_open.png`), picking a row with nothing
pending stores `last` and the row shows it (`62_after_pick.png`), the selection
survives a restart and the list reopens on it (`63_after_restart.png`,
`63b_restart_selected.png`), `Enter` with a pending recording saves into the
selected folder, a row click saves into another one, removing the selected
bookmark moves the selection to its neighbour, and the arrow of `Save` still
opens the same list. `_dropchk\` is deleted afterwards.

Edit: timeline editor with several ranges
-----------------------------------------

The `Edit` button opens a trim editor over the pending recording: the capture
hole becomes a preview of the GIF and a strip appears at the bottom with
play/pause, `+`, `-`, `Apply` and `Close`, plus a timeline with one tick per
frame.

The recording can be cut into several kept ranges that `Apply` writes back as
one GIF, concatenated in timeline order: `[A,B] + [C,D] + ...`.

- Each range has draggable `A`/`B` handles. Only the selected range shows the
  `A`/`B` flags; the others are drawn dimmer with plain bars, and a click on a
  range selects it.
- Ranges are kept sorted and never overlap: dragging a handle into a neighbour
  pulls the neighbour back, and a range left with no frames is dropped.
- `+` (or `N` / `Insert`) adds a range over the dropped frames starting at the
  playhead (the longest free run from there); when every frame is still kept it
  splits the range under the playhead instead, so the new one can be shaped.
- `-` (or `Delete` / `Backspace`) removes the selected range; with a single
  range it goes back to "keep everything", which is the only way out of a fully
  trimmed recording.
- Up to 12 ranges; `Tab` cycles the selection, `A`/`B` set the handle of the
  selected range at the playhead, arrows step one frame, `Space` plays only the
  kept frames, `Enter` applies and `Esc` closes without touching the file.
- The strip shows `A`/`B` times of the selected range, `range i/n`, the total
  kept duration and the playhead/frame readout; both label halves are
  ellipsized so a long range list never overlaps the readout.

Only the pending GIF can be edited, which is either the recording just made
or a GIF opened from disk that has not been saved yet.
Verification: `_uicheck8.ps1` covers the single-range path end to end,
`_uicheck9.ps1` records, trims to two ranges (`[2,4] + [10..end]`) by keyboard,
exercises `+` and `-` with real mouse clicks (plus `Esc` leaving the file
untouched) and then trims to three ranges (`[0,1] + [2,4] + [8..end]`); each
result is compared frame by frame against the frames it should have kept
(`_edittest/mrange_*.gif`, `_uicheck/51..58_*.png`). `_uicheck13.ps1` opens a
Pillow-made GIF (disposal 2, local palettes) as a command-line argument, from
the `Edit` menu, with Ctrl+O and with real mouse clicks on the split button
(label = edit, arrow = menu); trims it by keyboard (4 of 6 frames), checks the
working copy and the file `Save As` writes with Pillow, and checks that the
original is byte-identical (md5), that a non-GIF file and a missing path are
refused without leaving anything pending (`_opentest/`,
`_uicheck/70..75_*.png`).

Settings dialog (`IDD_CUSTOMIZATION`)

- Frame rate: 10 / 16 / 30 / Custom (1-60 FPS).
- Capture: mouse cursor, mouse-click highlight, elapsed-time overlay,
  stop automatically after N seconds.
- GIF output: loop count, transparency, title frame (text, seconds, big font).
- Window size: width/height of the capture area, applied on OK.
- Appearance: dark theme and always on top (same live switches as the
  `Settings` menu, so the whole configuration sits in one dialog).

New preferences stored in `licecap.ini`: `cursor` (capture mouse cursor) and
`ontop` (always on top), plus `lastsave` (last Save As path). Save bookmarks
live in the `[bookmarks]` section of the same file. Everything else
reuses the existing keys (`maxfps`, `prefs`, `titlems`, `gifloopcnt`,
`stopafter`, `wnd_r`, `title`).

## Dark theme

The UI ships with a dark palette by default; `Settings > Dark Theme` (or the
same switch in the Settings dialog) toggles back
to the original light one. The choice is stored in `licecap.ini` as `dark`
(`1` = dark, `0` = light) and is read before the window is created.

- Both palettes live in `licecap_ui.cpp` (`s_colors_dark` / `s_colors_light`);
  the rest of the drawing code only knows the `COL_*` macros, which point to
  the active palette.
- The window, owner-drawn buttons and the status block are painted by the app,
  so they follow the palette directly.
- Native pieces are opted into Windows dark mode: `DwmSetWindowAttribute`
  (`DWMWA_USE_IMMERSIVE_DARK_MODE`) for the title bars of the modal dialogs and
  `uxtheme` ordinals 133/135 (`AllowDarkModeForWindow`,
  `SetPreferredAppMode`) plus `SetWindowTheme("DarkMode_Explorer")` for the
  controls, the popup menus and the system Save dialog. All of it is resolved
  with `GetProcAddress`, so the build does not depend on `uxtheme.lib` or
  `dwmapi.lib` and older Windows versions simply keep the light look.
- The dialogs (`Settings`, `Insert text frame`, WEBM options, the save
  options panel) paint their background and answer `WM_CTLCOLOR*` so the text
  and edit fields are readable on dark.
- `licecap/licecap/licecap.manifest` adds the common controls v6 dependency
  (required for the themed dark controls) and the supportedOS list.

Verification: `_uicheck4.ps1` launches the Release x64 with a temporary
`licecap.ini` and writes `_uicheck/10_dark_main.png`,
`11_dark_menu.png`, `12_dark_customization.png` and `13_light_main.png`
(switching theme through the `Settings` menu). The user configuration in
`%APPDATA%\licecap.ini` is not touched. `_uicheck7.ps1` covers the unified
`Settings` button: label, menu contents, the dialog (`Dark theme` /
`Always on top` switches, `Defaults`) and the `dark` / topmost state going back
to the ini (`30_main_settings_button.png` .. `35_topmost.png`).

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

@echo off
rem Lanza LICEcap compilado en E:\Proyectos\licecap (Windows).
rem Funciona igual desde este repositorio o desde el escritorio: usa la ruta
rem absoluta del proyecto y, si no, la carpeta relativa al propio .bat.
setlocal
set "PROJ=E:\Proyectos\licecap"
if not exist "%PROJ%\dist\LICEcap-1.32-win64\licecap.exe" set "PROJ=%~dp0.."
if not exist "%PROJ%\dist\LICEcap-1.32-win64\licecap.exe" set "PROJ=%~dp0"

set "EXE=%PROJ%\dist\LICEcap-1.32-win64\licecap.exe"
if not exist "%EXE%" set "EXE=%PROJ%\licecap\licecap\x64\Release\licecap.exe"
if not exist "%EXE%" set "EXE=%PROJ%\licecap\licecap\x64\Debug\licecap.exe"
if not exist "%EXE%" set "EXE=%PROJ%\licecap\licecap\Debug_new\licecap.exe"

if not exist "%EXE%" (
  echo [ERROR] No se encontro licecap.exe.
  echo Compila primero con:
  echo   cd /d "%PROJ%\licecap\licecap"
  echo   "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" licecap.vcxproj -p:Configuration=Release -p:Platform=x64 -m
  pause
  exit /b 1
)

echo Lanzando "%EXE%"
start "" "%EXE%"
endlocal

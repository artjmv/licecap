@echo off
setlocal
set "EXE=E:\Proyectos\licecap\licecap\licecap\x64\Release\licecap.exe"
if not exist "%EXE%" (
  echo [ERROR] No se encontro la compilacion Release de LICEcap.
  pause
  exit /b 1
)
rem Usa siempre el Release actualizado; acepta tambien un GIF como argumento.
start "" "%EXE%" %*
endlocal

@echo off
REM Lanceur Oxford (fork Ardour, build MSYS2/MinGW) — DEV (DLL via PATH mingw64)
REM Fork SEPARE de Trident : n'utilise rien de C:\Ardour.
set PATH=C:\msys64\mingw64\bin;%PATH%
REM stderr/stdout capturés (crash + messages [Oxford]) dans un log pour debug.
"D:\Oxford\install\lib\ardour9\oxford-9.7.0.exe" > "%LOCALAPPDATA%\Oxford9\oxford_stderr.log" 2>&1

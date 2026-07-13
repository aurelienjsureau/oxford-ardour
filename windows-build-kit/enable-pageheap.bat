@echo off
REM ============================================================================
REM  Active Full PageHeap CIBLE (allocations 1 a 128 octets) pour oxford-9.7.0.exe
REM  -> toute ecriture qui deborde une petite alloc provoque une FAUTE IMMEDIATE
REM     (access violation) attrapable par gdb, avec la pile exacte du coupable.
REM  Leger : seules les petites allocs recoivent une page de garde.
REM  Necessite les droits admin (UAC) car ecrit dans HKLM.
REM  Pour annuler : disable-pageheap.bat
REM ============================================================================
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Demande d'elevation (UAC)...
  powershell -NoProfile -Command "Start-Process '%~f0' -Verb RunAs"
  exit /b
)
set "KEY=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\oxford-9.7.0.exe"
reg add "%KEY%" /v GlobalFlag            /t REG_DWORD /d 0x02000000 /f
reg add "%KEY%" /v PageHeapFlags         /t REG_DWORD /d 0x3        /f
reg add "%KEY%" /v PageHeapSizeRangeStart /t REG_DWORD /d 1         /f
reg add "%KEY%" /v PageHeapSizeRangeEnd   /t REG_DWORD /d 128       /f
echo.
echo === Full PageHeap CIBLE (1-128 octets) ACTIVE pour oxford-9.7.0.exe ===
echo Lance maintenant Lancer-Oxford-DEBUG.bat, reproduis le crash,
echo puis donne-moi "ok" (le backtrace sera dans oxford_gdb.log).
echo.
pause

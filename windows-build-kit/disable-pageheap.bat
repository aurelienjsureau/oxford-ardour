@echo off
REM Annule PageHeap pour oxford-9.7.0.exe (a relancer quand le debug est fini).
net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process '%~f0' -Verb RunAs"
  exit /b
)
reg delete "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\oxford-9.7.0.exe" /f
echo === PageHeap DESACTIVE pour oxford-9.7.0.exe ===
pause

@echo off
REM Lance Oxford sous gdb. Avec PageHeap actif (enable-pageheap.bat), le debordement
REM provoque une access violation (SIGSEGV) -> gdb ecrit la pile exacte du coupable.
REM On laisse passer les SIGTRAP (checks de tas benins) pour atteindre le vrai crash.
set PATH=C:\msys64\mingw64\bin;%PATH%
gdb --batch -ex "set pagination off" -ex "handle SIGTRAP nostop noprint pass" -ex run -ex "echo === BACKTRACE ===\n" -ex "bt 60" -ex "echo === ALL THREADS ===\n" -ex "thread apply all bt 15" -ex quit --args "D:\Oxford\install\lib\ardour9\oxford-9.7.0.exe" > "%LOCALAPPDATA%\Oxford9\oxford_gdb.log" 2>&1
echo Backtrace ecrit dans %LOCALAPPDATA%\Oxford9\oxford_gdb.log
pause

@echo off
setlocal
rem Use g++/windres from PATH when available, otherwise fall back to the WinGet install.
set "GCCDIR="
for /f "delims=" %%I in ('where g++ 2^>nul') do if not defined GXX set "GXX=%%I"
if defined GXX for %%I in ("%GXX%") do set "GCCDIR=%%~dpI"
if not defined GCCDIR set "GCCDIR=C:\Users\adstr\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\"
"%GCCDIR%windres.exe" perch.rc -O coff -o perch_res.o || exit /b 1
"%GCCDIR%g++.exe" -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++ main.cpp perch_res.o -o Perch.exe -lgdiplus -ldwmapi -liphlpapi -lshell32 -luser32 -lgdi32 -lole32 -ladvapi32 -luuid || exit /b 1
del perch_res.o
echo Built Perch.exe

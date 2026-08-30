@echo off
setlocal
set BIN=C:\Users\adstr\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
"%BIN%\windres.exe" perch.rc -O coff -o perch_res.o || exit /b 1
"%BIN%\g++.exe" -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++ main.cpp perch_res.o -o Perch.exe -lgdiplus -ldwmapi -liphlpapi -lshell32 -luser32 -lgdi32 -lole32 -ladvapi32 || exit /b 1
del perch_res.o
echo Built Perch.exe
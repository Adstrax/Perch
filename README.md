# Perch

> **v3.0.0** &mdash; the memory indicator now starts from cyan-blue and only reaches amber at 70% and red at 90%, and the tray menu follows the Windows light / dark theme (the widget itself stays as it is). Run `build.bat` to rebuild.

A tiny, native Windows network-speed monitor widget (C++ / Win32, no .NET).

- Translucent Acrylic background + Win11 rounded corners
- Floating & edge-docking states (memory ring vs. thin capsule bar)
- Live download / upload speed, CPU, memory %
- System tray icon + context menu
- Extremely light: ~5-20 MB working set, no runtime dependency

## Build

Requires MinGW-w64 (GCC) + windres.

```
build.bat
```

## Stack

C++17, Win32 / GDI+ (`SetWindowCompositionAttribute` Acrylic), `GetIfTable2`, `GlobalMemoryStatusEx`, `GetSystemTimes`.
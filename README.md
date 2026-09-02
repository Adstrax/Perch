# Perch

> **v2.0.0** &mdash; adds an in-app **About** dialog (tray menu &rarr; "About Perch") that shows the version. Run `build.bat` to rebuild.

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

C++17, Win32 / GDI+ (`SetWindowCompositionAttribute` Acrylic), `GetIfTable`, `GlobalMemoryStatusEx`, `GetSystemTimes`.
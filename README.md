# Perch

> **v3.2.0** &mdash; Perch is now a single-instance app: starting it again brings the existing widget to the front and tells you it is already running instead of stacking a second copy. Run `build.bat` to rebuild.

A tiny, native Windows network-speed monitor widget (C++ / Win32, no .NET).

- Translucent Acrylic background + Win11 rounded corners
- Floating & edge-docking states (memory ring vs. thin capsule bar)
- Live download / upload speed, CPU, memory %
- Context menu with tray icon and widget right-click entry (the tray icon can be turned off)
- Single instance: launching Perch again brings the existing widget forward and shows a notice
- Extremely light: ~5-20 MB working set, no runtime dependency

## Build

Requires MinGW-w64 (GCC) + windres.

```
build.bat
```

## Stack

C++17, Win32 / GDI+ (`SetWindowCompositionAttribute` Acrylic), `GetIfTable2`, `GlobalMemoryStatusEx`, `GetSystemTimes`.
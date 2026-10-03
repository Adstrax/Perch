# Perch

> **v3.1.0** &mdash; the tray icon can now be turned off from the menu, and the very same menu is reachable by right-clicking the widget (so hiding the tray icon never locks you out). Run `build.bat` to rebuild.

A tiny, native Windows network-speed monitor widget (C++ / Win32, no .NET).

- Translucent Acrylic background + Win11 rounded corners
- Floating & edge-docking states (memory ring vs. thin capsule bar)
- Live download / upload speed, CPU, memory %
- Context menu with tray icon and widget right-click entry (the tray icon can be turned off)
- Extremely light: ~5-20 MB working set, no runtime dependency

## Build

Requires MinGW-w64 (GCC) + windres.

```
build.bat
```

## Stack

C++17, Win32 / GDI+ (`SetWindowCompositionAttribute` Acrylic), `GetIfTable2`, `GlobalMemoryStatusEx`, `GetSystemTimes`.
# Perch

> **v3.3.1** &mdash; the context menu now has a correct top-dock icon, opens About from the clickable header, and keeps a simpler action list.

A tiny, native Windows network-speed monitor widget (C++ / Win32, no .NET).

- Translucent Acrylic background + Win11 rounded corners
- Floating & edge-docking states (memory ring, vertical capsule, or horizontal bar)
- Live download / upload speed, CPU, memory %
- WCA Acrylic context menu with Segoe Fluent Icons and the same entry from the widget or tray
- Single instance: launching Perch again brings the existing widget forward and shows a notice
- Extremely light: ~5-20 MB working set, no runtime dependency

## Build

Requires MinGW-w64 (GCC) + windres.

```
build.bat
```

## Stack

C++17, Win32 / GDI+ (`SetWindowCompositionAttribute` Acrylic), `GetIfTable2`, `GlobalMemoryStatusEx`, `GetSystemTimes`.

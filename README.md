# Perch

> **v3.5.0** &mdash; Perch now keeps a single Windows startup entry: the registry is authoritative and legacy Startup-folder shortcuts are migrated or removed automatically.

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

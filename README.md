# Perch

> **v2.0.2** &mdash; fixes the speed readout so the decimal digit is never clipped (the number now shrinks to fit instead of being cut short, e.g. "50."), and makes the readings accurate: per-adapter filter-driver interfaces are no longer counted several times over, and sampling now uses 64-bit interface counters. Run `build.bat` to rebuild.

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
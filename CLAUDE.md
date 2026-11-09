# CLAUDE.md — working rules for GoldenGate for Windows

This file governs how to work on this project. Read it before changing anything.

## What this is
A standalone Windows 11 desktop layer that gives Windows the look and feel of
Apple's macOS "Golden Gate": a top menu bar/panel, a floating magnifying Dock,
Control Center, Notification Center, Mission Control, Launchpad, Spotlight,
theming, wallpaper, and a Settings app. It runs **next to** Explorer.

## Non-negotiable constraints
- **No third-party customization apps** (Windhawk, StartAllBack, ExplorerPatcher,
  SecureUxTheme, Rainmeter, Seelen, MyDockFinder…). Reusing open-source **code or
  assets** under compatible licenses is fine (Bibata cursors GPL-3.0, MacTahoe
  icons GPL-3.0, Inter OFL).
- **No system patching, no DLL/code injection, no undocumented process tampering.**
  Only documented Windows APIs, plus the single long-stable
  `SetWindowCompositionAttribute` acrylic call (see KNOWN_LIMITATIONS.md).
- **Fully reversible.** Never permanently disable the taskbar or Explorer. The
  uninstaller and `GoldenGate.exe --restore` must put Windows back.
- **Runs without admin** for normal operation.
- **No Apple assets or trademarks shipped.** Don't use "macOS" in the product name.

## Tech stack (this supersedes the C#/.NET/WPF brief)
C++17 · Win32 · Direct2D/DirectWrite/WIC · DWM · Core Audio · WMI. Built as a
single native `GoldenGate.exe`. The decision to keep C++/Direct2D over C#/WPF is
recorded in `docs/ARCHITECTURE.md` and justified in `docs/PERFORMANCE_AUDIT.md`
(better custom glass, lower idle cost, no airspace issues).

## Build / test / package / deliver
```bash
cd ggwin
./build.sh /tmp/claude-0/build           # cross-compiles with mingw-w64 → GoldenGate.exe
```
- Cross-compiler: `x86_64-w64-mingw32-g++-posix`, C++17. Sources listed in `build.sh`.
- **Smoke test** under Wine+Xvfb (no DWM/GPU, so visuals can't be verified — only
  that it initialises, writes `settings.ini`, and runs without crashing):
  `DISPLAY=:99 wine GoldenGate.exe` after starting `Xvfb :99`.
- **Installer:** `cd installer && makensis -V1 goldengate.nsi` → per-user
  `dist/GoldenGate-Setup.exe` (NSIS).
- **Package + deliver:** copy the fresh exe into `dist/`, rebuild the installer,
  refresh the package tree under the `GoldenGate-Windows/` folder, zip it.

## Delivery rule (standing)
**Always send the full package (`GoldenGate-Windows.zip`), never just changed
files — the user deletes old files when an update arrives.**

## Verification reality
DWM glass/acrylic, real GPU rendering, and the Dock's live feel **cannot** be
verified in this Linux/Wine environment. Confirm visuals with user screenshots;
do not claim a visual result you haven't seen. Confirm performance with on-device
profiling (see `docs/PERFORMANCE_AUDIT.md`), never with invented numbers.

## MinGW gotchas
- In wide `swprintf`, `%s` reads wide strings as **narrow** (truncates at the
  first char). Always use `%ls` for `wchar_t*`. This bit the clock ("S 8 N" vs
  "Sun 8 Nov").
- AppBar: call `SHAppBarMessage(ABM_NEW)` **before** `ABM_SETPOS`, or space isn't
  reserved and maximized windows slide under the bar.

## Source map (src/)
`main.cpp` entry/lifecycle/timers · `gfx.cpp` Direct2D + `Surface` window class ·
`theme.cpp` light/dark/accent/cursors/wallpaper · `menubar.cpp` top panel ·
`dock.cpp` Dock · `controlcenter.cpp` Control Center · `calendar.cpp` Notification
Center · `overlays.cpp` Mission Control · `launchpad.cpp` Launchpad · `extras.cpp`
Spotlight/cursor-finder/shake · `fan.cpp` Downloads stack · `apps.cpp` app/icon
enumeration · `menu.cpp` popup menus · `taskbar.cpp` taskbar hide/restore ·
`settings.cpp` Settings app · `util.cpp` INI/registry/helpers.

## Current status
See `docs/IMPLEMENTATION_PLAN.md` (done/pending/blocked) and `docs/CHANGELOG.md`.
Never describe an unimplemented feature as complete.

## Skills
Don't create a skill unless the user approves it.

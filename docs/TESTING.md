# Testing

## What is automated here
The project cross-compiles on Linux (mingw-w64) and is **smoke-tested under
Wine + Xvfb**. The smoke test confirms the binary:
- starts without crashing and enters its message loop,
- initialises Direct2D/DWM calls without faulting,
- writes `settings.ini` and the pre-install backup,
- loads/saves all settings keys (incl. `glass`, `tint`, `glasspreset`).

```bash
Xvfb :99 -screen 0 1920x1080x24 &
DISPLAY=:99 WINEDEBUG=-all wine /tmp/claude-0/build/GoldenGate.exe
```

## What CANNOT be verified off-device
Wine has no real DWM compositor or GPU, so **none of these can be trusted from the
build environment** and must be checked on real Windows 11 (ideally with
screenshots / profiling):
- Acrylic glass, blur, tint, rounded corners, dark-mode chrome.
- Dock magnification feel, intellihide timing, per-pixel alpha edges.
- Mission Control live thumbnails; multi-monitor behaviour; DPI scaling.
- Real CPU/GPU/memory/frame-pacing (see `PERFORMANCE_AUDIT.md` for the on-device
  profiling plan and tools — WPA/ETW, PresentMon, GPUView, VMMap).

## On-device manual checklist
1. Install per-user; first-run opens Settings; starts at login.
2. Panel: clock formats, battery %, Control Center, Notification Center from the clock.
3. Glass: switch Clear/Balanced/Tinted and move Opacity/Tint — bar updates live.
4. Dock: magnify, launch, focus-cycle, pin/unpin, Trash, Downloads; Off/Intellihide/
   Auto-hide; maximize a window (intellihide should hide; reserved mode should not
   cover it).
5. Snipping Tool can capture the panel and Dock.
6. Fullscreen app (game/video) — bar and Dock stay out of the way.
7. Explorer restart (`taskkill /f /im explorer.exe` then relaunch) — surfaces recover.
8. Uninstall / `--restore` — theme, cursors, wallpaper, and taskbar return.

## Regression focus after the Dock refactor
- Icon hit-testing still aligns with the drawn icons (content is centred in a fixed
  window now).
- Clicks in the Dock's transparent side margins fall through to the desktop.
- Right-click menus and the Downloads fan anchor under the correct icon.

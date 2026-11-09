# Architecture

## Stack decision
Built in **C++17 / Win32 / Direct2D**, not C#/.NET/WPF. The product needs a
custom-tinted translucent "Liquid Glass" material, a per-pixel-alpha floating Dock
with pointer magnification, and low idle cost while running continuously next to
Explorer. Direct2D immediate-mode rendering with DWM backdrops gives tighter
control of the glass and lighter idle behaviour than WPF's retained-mode tree, and
avoids WPF/airspace compositing issues. See `PERFORMANCE_AUDIT.md`.

Single native process `GoldenGate.exe`. No injection, no system-file changes.

## Process & threads
- One UI thread, a single-threaded Direct2D factory (`gfx.cpp`), a hidden
  message-only window (`GoldenGateMain`) owning app-wide timers and hooks
  (`main.cpp`).
- Hooks: `WH_KEYBOARD_LL` (shortcuts/Spotlight), `SetWinEventHook`
  (`EVENT_SYSTEM_FOREGROUND`) to track the active app, clipboard listener.

## Rendering model (`gfx.cpp`, `Surface`)
Every surface is a `WS_POPUP` top-level window drawn with Direct2D in two modes:
- **Glass** — `CreateHwndRenderTarget` with `TYPE_DEFAULT` (GPU when available) over
  a DWM backdrop (`DWMSBT_TRANSIENTWINDOW`) + acrylic. Used for the menu bar,
  popup menus, Control Center, Notification Center, Mission Control, Launchpad,
  Settings. Rounded corners and dark mode via `DwmSetWindowAttribute`.
- **Layered** — `CreateDCRenderTarget` (software) rendered into a DIB and pushed
  with `UpdateLayeredWindow` for true per-pixel alpha. Used for the Dock, the
  Downloads fan, and the cursor finder (shapes that need soft rounded alpha edges).

Drawing helpers (Fill/Stroke/Text/Line/Circle/Bitmap) reuse one brush and cache
text formats and bitmaps. `D2DERR_RECREATE_TARGET` is handled by dropping and
rebuilding the target.

## Space, z-order, and staying visible
- **Menu bar** reserves its strip with the AppBar API (`SHAppBarMessage`,
  `ABM_NEW` before `ABM_SETPOS`). The **Dock** reserves the bottom strip only when
  set to never hide; with intellihide/auto-hide it floats.
- A 300 ms reassert timer re-tops the bar and Dock after focus changes, skipping
  true-fullscreen apps (`ForegroundIsTrueFullscreen`). The foreground WinEvent
  hook also refreshes them immediately.

## Dock specifics (`dock.cpp`)
Fixed-size layered window sized to the fully-magnified bound; content is drawn
centred so the window width never changes during magnification (no per-frame
target/cache teardown — see `PERFORMANCE_AUDIT.md` P0). `WM_NCHITTEST` returns
`HTTRANSPARENT` in the empty side margins so clicks fall through. Intellihide polls
overlap (`EnumWindows`) at ~4 Hz, not every tick (P1); reveal/slide animates on a
30 ms timer only while moving.

## Theme engine (`theme.cpp`)
Light/dark/auto (by time), accent color, cursor scheme (Bibata), and wallpaper.
Previous Windows state is backed up once (`ThemeBackupOnce`) and restored by
`ThemeRestoreAll` / `--restore`.

## Settings (`settings.cpp`)
Own Direct2D Glass window with a sidebar of pages and rows of controls
(switch / slider / segmented / header). Setters apply live and persist to INI.

## Persistence (`util.cpp`)
INI at `%APPDATA%\GoldenGate\settings.ini`; a one-time backup of prior Windows
settings beside it. Small registry helpers for theme/accent.

## Lifecycle & recovery
Handles Explorer restart (`TaskbarCreated`), DPI changes, monitor changes,
sleep/resume, and theme changes; recovers surfaces and re-hides the taskbar if the
user chose that. Clean exit leaves the user's chosen theme in place and only tears
down Golden Gate's own UI.

## Module map
`main` lifecycle/timers/hooks · `gfx` rendering + `Surface` · `theme` appearance ·
`menubar` panel · `dock` Dock · `controlcenter` · `calendar` Notification Center ·
`overlays` Mission Control · `launchpad` · `extras` Spotlight/finder/shake · `fan`
Downloads · `apps` app+icon enumeration · `menu` popups · `taskbar` hide/restore ·
`settings` · `util` INI/registry/helpers.

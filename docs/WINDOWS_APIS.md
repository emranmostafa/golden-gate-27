# Windows APIs & Capability Matrix

All documented Windows APIs, except the single long-stable
`SetWindowCompositionAttribute` acrylic call (see KNOWN_LIMITATIONS.md). No
injection, no system-file changes.

## APIs in use
| Area | API | Used for |
|------|-----|----------|
| Rendering | Direct2D, DirectWrite, WIC | all custom UI, text, image/icon decode |
| Window effects | DWM (`DwmSetWindowAttribute`, `DwmExtendFrameIntoClientArea`) | rounded corners, dark mode, system backdrop, border color |
| Glass | `SetWindowCompositionAttribute` (acrylic) | custom-tinted blur behind panels |
| Space reservation | Shell AppBar (`SHAppBarMessage`) | menu bar + Dock strip, taskbar state |
| Window/app enumeration | `EnumWindows`, `GetWindow*`, `DwmGetWindowAttribute(CLOAKED)` | Dock running apps, overlap/intellihide, switcher |
| App list & icons | `shell:AppsFolder` (IShellFolder), `IShellItemImageFactory`, `WM_GETICON`/`GCLP_HICON` | Dock, Launchpad, Spotlight icons |
| Audio | Core Audio (`IAudioEndpointVolume`) | real system volume/mute |
| Brightness | WMI (`WmiMonitorBrightness` / `…Methods`) | laptop backlight |
| Input | `WH_KEYBOARD_LL`, `RegisterHotKey`, `SendInput`/media keys | shortcuts, Spotlight, Now Playing |
| Events | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` | active-app tracking |
| Power/session | `LockWorkStation`, `SetSuspendState`, `shutdown.exe`, `SetThreadExecutionState` | Control Center power, Caffeine |
| Thumbnails | `DwmRegisterThumbnail` | Mission Control live previews |
| Persistence | INI (`%APPDATA%`), registry helpers | settings, theme/accent, backup |

## Capability matrix
**Fully supported (native, reliable)**
- Top panel with real glass, clock, battery/volume/network readouts, AppBar reservation.
- Floating Dock: app discovery, launch/focus/minimize, running state, pinning,
  right-click menus, magnification, Downloads stack, Trash, intellihide/auto-hide.
- Control Center (Wi-Fi/BT quick links, Focus, volume, power), Notification Center
  with calendar, Mission Control, Launchpad, Spotlight, clipboard history.
- Theme (light/dark/auto), accent, Bibata cursors, wallpaper, reversible taskbar
  hiding, rounded corners + acrylic on our own windows.

**Partially supported**
- **Menu-bar app menus:** real per-app File/Edit/… menus aren't universally exposed
  by Windows apps; we show the app name + a sensible standard fallback. (UI
  Automation for richer menus is a possible future enhancement.)
- **System brightness:** only on displays that expose the WMI interface (most
  laptops; many desktop monitors don't).
- **Volume/Now Playing:** master volume is exact; media transport uses global media
  keys (works with most players; no per-app routing).

**Not supported (by design — would require injection/patching)**
- Restyling other apps' title bars, sidebars, or window chrome.
- A system-wide File Explorer replacement (an original Shell-API file manager could
  be added, clearly labelled, but is out of scope).
- Theming the Windows lock screen / secure desktop / UAC.

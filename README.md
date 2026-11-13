# GoldenGate for Windows

Give Windows 11 the look and feel of macOS "Golden Gate" — a top menu bar, a
floating magnifying Dock, Control Center, Notification Center, Mission Control,
Launchpad, Spotlight, theming and wallpaper — as **one native app that runs next
to Explorer**. No system files are patched and nothing is injected into other
programs.

## Requirements
- Windows 11 (24H2 / 25H2 recommended; 22H2+ for acrylic glass and rounded corners).
- No administrator rights for normal use.

## Install
1. Download `GoldenGate-Setup.exe` from the
   [latest release](https://github.com/emranmostafa/golden-gate-windows/releases/latest)
   and run it (per-user install; SmartScreen may warn for an unsigned build —
   choose **More info → Run anyway**). To update, run the newer installer; your
   settings are kept.
2. Golden Gate starts and the Settings window opens on first run.
3. It starts automatically at login (toggle in Settings → Advanced).

## First things to try
- **Dock:** hover to magnify; click to launch/focus; right-click for app options,
  pinning, and Trash/Downloads actions.
- **Panel:** click the clock for Notification Center; the Control Center icon for
  Wi-Fi/Bluetooth, brightness, volume, Focus, and power.
- **Spotlight:** the search glyph (or the shortcut) to launch apps and search.
- **Settings → Panel:** choose a **Glass style** (Clear / Balanced / Tinted) and
  fine-tune **Opacity** and **Tint**; set panel height and clock options.
- **Settings → Dock:** icon size, magnification, and hiding (Off / Intellihide /
  Auto-hide).

## Uninstall / restore
- Uninstall from **Settings → Apps**, or run `GoldenGate.exe --restore`.
- This restores your previous theme, accent, cursors, wallpaper, and the Windows
  taskbar. Golden Gate never permanently disables the taskbar or Explorer.

## Licenses
Bundled open-source assets: **Bibata** cursors (GPL-3.0), **MacTahoe**-style icons
(GPL-3.0), **Inter** font (OFL). See `LICENSE*.txt`. No Apple assets or trademarks
are included.

## Documentation
Developer and design docs are in `docs/` and `PROJECT.md` / `CLAUDE.md`.

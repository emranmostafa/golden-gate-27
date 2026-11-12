# Changelog

## v2.4.2 — Panel space, full screen, Dock reordering
- **Panel keeps its space:** when Windows rearranges the screen edges (the
  taskbar switching to auto-hide at start-up, Explorer restarting, another
  docked bar appearing) the panel now claims its strip again, so maximized
  windows stay below it instead of sliding underneath. A check every clock
  tick re-reserves the strip if Windows ever drops it.
- **Full screen means full screen:** a video in full screen (YouTube, any
  player), a browser in F11 or a game now covers the whole monitor. The panel
  and the Dock hide on that monitor and return as soon as it leaves full screen
  or another window comes to the front. Small always-on-top windows such as
  picture-in-picture don't bring them back.
- **Reorder the Dock by dragging:** dragging a pinned app along the Dock
  moves it again, and the new order is kept. The drag no longer drops when the
  pointer drifts just above the icons, isn't undone by a refresh halfway
  through, and the icon no longer jumps back and forth between two magnified
  neighbours.

## v2.4.1 — Window previews, dot colour, icons for every app
- **Window previews:** right-clicking a running app in the Dock shows a live
  preview of each of its windows above the icon. Click one to switch to it, or
  the × to close it; New Window, Show All Windows, Keep in Dock and Quit sit
  underneath. Minimised windows show the app icon.
- **Running dot colour:** Settings → Dock → Running dot colour: automatic grey,
  your accent colour, or white, blue, green, orange, red or purple.
- **Icons for every app:** programs without a 256 px icon (RustDesk ships only
  48 px) now show their own icon instead of Windows' framed placeholder, also
  when the Start-menu entry is recorded under a folder ID.
- **One icon per app:** windows of the same app that report different app IDs
  (SessionX) all appear under its one Dock icon, in its window list and in
  click-to-cycle.

## v2.4 — Panel popups at the top, Windows-key Spotlight, a smoother Dock
- **Real app icons in the Dock:** apps are matched to their Start-menu item by
  app ID, then by what the shortcut runs, so VS Code, Snipping Tool and Store
  apps show their own icons; the bundled set is only a last resort. Icons share
  one margin, keep their alpha edges, and scale straight to their exact size.
  Programs with a large non-256 px icon (e.g. SessionX) show it at full size.
- **Dock auto-hide:** stays up while the pointer is anywhere over it (including
  the gap below it), waits a moment before hiding, and slides on a timed 60 fps
  ease. Clicking Downloads again closes the stack.
- **Panel on every monitor:** each panel names the top app on its own monitor,
  and its menus act on that app. Dragging the panel moves that app's window
  (maximised windows restore; drop on a panel to maximise there).
- **Panel popups at the top:** the volume and Wi-Fi icons open Windows' own
  sound and Wi-Fi panels, placed just under the panel. Scrolling over the volume
  icon changes the volume. The menu adds Hibernate; About This PC opens
  Settings > About.
- **Spotlight:** tap the Windows key to open it (Win+A opens Start; other Win
  shortcuts are unchanged). It takes focus reliably, shows a typing cursor, sits
  in the centre of the screen, and closes on a click elsewhere. Fixed a crash
  when closing Spotlight, Launchpad or Mission Control with Esc.
- **Pointer:** Style, Colour and Size (32-96 px) controls for the Bibata
  cursors. Shake to grow, ported from KDE Plasma's Shake Cursor: keep shaking to
  grow it more; it shrinks 2 s after you stop.
- **Settings:** a standard Windows title bar; sliders with labelled stops that
  the knob snaps to; descriptions wrap instead of overlapping.
- **Startup and updates:** Golden Gate starts from a per-user sign-in task (no
  admin) and hides the Windows taskbar first thing. Updating no longer brings
  the taskbar back; the new version starts by itself. If Golden Gate can't
  restart after a crash, the taskbar is restored.
- **Alt+Tab** is Windows' own again by default (Cover-flow is in Settings).
- **License:** Golden Gate is now GPL-3.0-or-later; the full GPL text ships with
  it.

## v2.3 — Taskbar-accurate icons, crash fixes, updates that apply
- **Icons chosen the way the taskbar does:** apps are matched to their Start-menu
  item, and the icon comes straight from that item (its shortcut icon, which is
  what a pinned taskbar button shows) instead of re-parsing its name, which failed
  for desktop apps (`{GUID}\Folder\app.exe` names) and silently fell back to the
  macOS set. Running apps also honour a window's own `RelaunchIconResource`, then
  their app ID, the program's icon and the window icon. Finder and Trash keep
  their icons. Apps also launch from the same Start-menu item.
- **Crash fixed: confirm dialogs** (Empty Trash, Restart, Shut Down, Restore
  Windows) deleted themselves twice on OK/Cancel. Empty Trash no longer asks twice.
- **Updates now actually install:** the installer and uninstaller couldn't find the
  running Golden Gate (its main window is message-only), so the old exe stayed in
  use and updates didn't apply. They now run `GoldenGate.exe --quit`, then make
  sure. A second launch also finds the running copy again (opens Settings).
- **Crash auto-restart works:** the restarted copy used to see the crashed one's
  single-instance lock and quit; the lock is now released and the new copy waits
  for it.
- **Quitting restores the Windows taskbar** instead of leaving neither taskbar nor
  Dock.

## v2.2 — Original, crisp icons
- **Original app icons by default:** the Dock, Launchpad and Spotlight now show
  each app's own icon from the shell (the same source the taskbar uses). The
  bundled macOS-style replacement set is an option: Settings → Dock → App icons.
- **Crisp icons:** large icons were shrunk 2–3× with plain linear filtering, which
  aliases into rough, jagged edges. Images are now pre-scaled with WIC's
  high-quality Fant filter to just above their on-screen size (cached in 16 px
  steps so magnification reuses a few sizes), applied everywhere images are drawn.

## v2.1 — Multi-monitor, Dock icons, Downloads fan
- **Secondary monitors:** every monitor's panel now reserves its strip (AppBar), so
  maximized windows stop below it on all screens, not just the primary.
- **Dock stays up while its menu is open:** right-clicking an icon (or opening the
  Downloads stack) no longer makes intellihide/auto-hide slide the Dock away.
- **Letter-placeholder icons fixed:** bare app IDs (Store apps, Chrome/Edge web apps
  such as YouTube) now resolve via `shell:AppsFolder`; running apps fall back to
  the exe's hi-res icon, then the window icon. The same fix lets those apps launch
  from the Dock.
- **Stability:** the icon fallback used to create a new bitmap on every Dock refresh
  (now once a second) — a steady memory/GPU leak — and used a blocking
  `SendMessage` that could freeze on a hung app. Icons are now cached per app and
  the window query has a timeout. Downloads stack icons are released on close.
- **Downloads stack:** real macOS-style **Fan** (newest files arc up out of the
  Dock, leaning as they rise, name pills on the left, "Open in Explorer" on top),
  plus **Grid** and **Automatic**. Switch via right-click on Downloads or
  Settings → Dock. Exe/zip/folder icons no longer sit on a white matte; images,
  video and PDFs still show previews.

## v2.0 — Dock drag, icons, corners
- **Drag to arrange the Dock:** pinned icons can now be dragged to reorder; the new
  order is saved and persists.
- **Rounded corners fixed:** Control Center, Notification Center and menus now use
  the documented DWM acrylic backdrop (not the undocumented square-cornered
  acrylic), and panel backgrounds are filled flush so DWM's rounding isn't
  double-rounded. The menu bar keeps its tunable tint.
- **Icon "white box" fixed:** app icons are fetched with `SIIGBF_ICONONLY`, so they
  keep their transparency instead of coming back as an opaque thumbnail.
- **Dock shows more open apps:** enumeration now matches the taskbar more closely
  (tool windows only excluded unless they opt in; no longer dropping no-activate
  windows), and the Dock refreshes every second to catch apps opened/closed
  silently.

## v1.9 — Stability & fixes
- **Crash resilience:** an unhandled-exception handler now logs the faulting
  module+offset to `%APPDATA%\GoldenGate\crash.log` and auto-relaunches Golden Gate
  (with a startup-loop guard), so a stray periodic fault no longer takes it down.
  Build keeps symbols so the offset pinpoints the fault.
- **Spotlight:** fixed the result icon being stretched into a giant blur (the icon
  rect used the row's Y as its right edge); tightened the selected-row highlight to
  an inset rounded pill.
- **Dock:** now shows apps opened/closed without a foreground change (periodic
  refresh when idle), and shows Golden Gate's own Settings window while it's open.
- **Taskbar:** the auto-hidden Windows taskbar is re-hidden on the fast Dock tick so
  it can't flash up over the Dock at the bottom edge.
- **Settings window:** the green traffic-light now zooms (toggles work-area size) /
  restores; it previously did nothing.

## v1.8 — Performance & design-system pass
- **Dock P0:** fixed-size window; content centred; eliminates the per-frame render-
  target / DIB / icon-cache teardown that happened on every magnify frame.
  `WM_NCHITTEST` click-through for the transparent side margins; smoother
  magnification ease-out.
- **Dock P1:** intellihide window-overlap scan (`EnumWindows`) throttled to ~4 Hz
  instead of every 30 ms tick; reveal stays instant.
- **Glass presets:** three presets (Clear / Balanced / Tinted) with independent
  **Opacity** and **Tint** sliders in Settings → Panel; tint blends the glass toward
  the accent color. New INI keys `tint`, `glasspreset`.
- **Documentation set** added: PROJECT, CLAUDE, README, ARCHITECTURE, DESIGN_SYSTEM,
  WINDOWS_APIS, IMPLEMENTATION_PLAN, TESTING, KNOWN_LIMITATIONS, PERFORMANCE_AUDIT,
  research/*.

## v1.7 — Control Center redesign
- Control Center rebuilt to the macOS Tahoe grid: Wi-Fi tile + Bluetooth/AirDrop
  tiles + Now Playing card; Focus + Mission/Awake; full-width Display/Sound pill
  sliders; round quick buttons (Appearance, Settings, Screenshot, Power).
- Notification Center drops from the clock centre.
- Panel settings section with Glass and Height controls.

## v1.6 and earlier (summary)
- Notification Center (calendar + Windows notifications), Panel glass/height,
  macOS-style Control Center first pass.
- Dock intellihide, auto-hide, running-app icons, Downloads/Trash.
- Panel with clock (fixed `%ls` truncation, AM/PM), AppBar reservation, foreground
  refresh, topmost reassert (fixes Alt-Tab / maximize hiding the bar & Dock).
- Settings window with traffic-light controls and live-applying pages.
- Taskbar hide/restore; reversible theme/accent/cursors/wallpaper; `--restore`.
- Spotlight, Mission Control, Launchpad, clipboard history, Caffeine.
- NSIS per-user installer; cross-compiled with mingw-w64.

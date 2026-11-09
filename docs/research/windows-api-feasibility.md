# Research — Windows API feasibility

Can each macOS-style capability be built natively on Windows 11 with documented
APIs and no injection/patching? Summary; full inventory in `../WINDOWS_APIS.md`.

| Goal | Feasible natively? | How |
|------|--------------------|-----|
| Top panel reserving space | ✅ | Shell AppBar (`SHAppBarMessage`) |
| Translucent glass panels | ✅ (1 undocumented-but-stable call) | DWM backdrop + `SetWindowCompositionAttribute`; `DWMSBT_TRANSIENTWINDOW` fallback |
| Rounded corners / dark chrome | ✅ | `DwmSetWindowAttribute` |
| Floating magnifying Dock | ✅ | layered window + Direct2D + `UpdateLayeredWindow` |
| App discovery & icons | ✅ | `shell:AppsFolder`, `IShellItemImageFactory` |
| Running windows / focus / switch | ✅ | `EnumWindows`, WinEvent hooks |
| System volume | ✅ | Core Audio |
| Display brightness | 🟡 | WMI (laptops / capable panels only) |
| Media transport (Now Playing) | 🟡 | global media keys (no per-app routing/metadata yet) |
| Per-app menu bar menus | 🟡 | not universally exposed; standard fallback (UI Automation could extend) |
| Mission Control thumbnails | ✅ | `DwmRegisterThumbnail` |
| Spotlight app search | ✅ | AppsFolder; deep file-content search 🟡 via Windows Search |
| Theme / accent / cursors / wallpaper | ✅ | registry + `SystemParametersInfo` |
| Reversible taskbar hide | ✅ | AppBar state + `ShowWindow`, restored on exit |
| Caffeine / power actions | ✅ | `SetThreadExecutionState`, `LockWorkStation`, `SetSuspendState`, `shutdown.exe` |
| Restyle other apps' chrome | ⛔ | would require injection — excluded |
| Replace File Explorer | ⛔ | out of scope (optional labelled file manager possible) |
| Theme lock screen / UAC | ⛔ | protected; never touched |

**Conclusion.** The large majority of the macOS Golden Gate experience is
achievable with documented, reversible, non-invasive techniques. The gaps
(other-app chrome, universal app menus, some brightness/media cases) are inherent
to not injecting into other processes, and are handled with honest fallbacks rather
than fake behaviour.

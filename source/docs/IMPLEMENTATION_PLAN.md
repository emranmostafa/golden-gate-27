# Implementation Plan & Status

Honest status. Never mark an unimplemented feature complete.
Legend: ✅ done · 🟡 partial · ⬜ planned · ⛔ blocked/out-of-scope.

## By phase
**Phase 0 — Research & architecture** ✅
Research docs (`docs/research/*`), architecture, design tokens, API feasibility.

**Phase 1 — Dock** ✅
App discovery, running-window tracking, floating layered Dock, magnification,
launch/focus, pinning, persistence, Downloads stack, Trash, intellihide/auto-hide,
DPI/multi-monitor placement. Perf: fixed-size window (P0) + throttled overlap scan
(P1) done.

**Phase 2 — Menu bar / Panel** ✅ (app menus 🟡)
Panel with glass, clock (12/24h, date, seconds, centered), battery %, volume/wifi
status, Control Center + Notification Center entry points, AppBar reservation,
adjustable height. App-specific menus are a standard fallback (🟡).

**Phase 3 — Liquid Glass engine** ✅ (presets done)
Acrylic + Direct2D tint, light/dark, three presets (Clear/Balanced/Tinted),
independent opacity & tint. GPU on Glass surfaces; software on layered (documented
in PERFORMANCE_AUDIT.md). Blur-radius control ⬜.

**Phase 4 — Spotlight** ✅ (file-content search 🟡)
Launcher UI, app search, keyboard nav, launch actions. Deep file-content indexing
via Windows Search is 🟡/⬜.

**Phase 5 — Theme & wallpaper** ✅
Light/dark/auto, accent, Bibata cursors, wallpaper presets, auto appearance switch,
restore of prior Windows state.

**Phase 6 — Settings & polish** ✅ (ongoing)
Settings app with live-applying pages (Appearance, Panel, Dock, Features, Advanced),
window controls, NSIS packaging, startup, Explorer-restart recovery, `--restore`.

## Component checklist
| Component | Status | Notes |
|-----------|--------|-------|
| Dock | ✅ | perf P0/P1 landed; P2 (DirectComposition) ⬜ gated on profiling |
| Menu bar / Panel | ✅ | app menus 🟡 fallback |
| Control Center | ✅ | macOS card grid; brightness needs WMI-capable display |
| Notification Center | ✅ | drops from the clock; opens Windows notifications |
| Mission Control | ✅ | live thumbnails |
| Launchpad | ✅ | |
| Spotlight | ✅ | file-content search 🟡 |
| Theme / accent / cursors | ✅ | reversible |
| Wallpaper | ✅ | per-monitor 🟡 |
| Settings app | ✅ | import/export ⬜ |
| Reversibility / `--restore` | ✅ | |
| Reduced-motion / anim-speed | ⬜ | planned |
| Window chrome of other apps | ⛔ | out of scope (no injection) |
| File Explorer replacement | ⛔ | out of scope |

## Next engineering steps
1. On-device profiling to validate P0/P1 (PERFORMANCE_AUDIT.md §5).
2. Reduced-motion + animation-speed controls.
3. Settings import/export; per-monitor wallpaper.
4. Evaluate P2 (DirectComposition for the Dock) only if profiling warrants it.

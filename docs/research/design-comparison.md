# Research — Design comparison (macOS Golden Gate vs our Windows build)

Per-component comparison and how close we get. Fidelity: ●●● close · ●●○ good ·
●○○ approximate. macOS specifics post-cutoff are design targets (see
`golden-gate-ui.md`).

| Component | macOS Golden Gate | Our Windows build | Fidelity |
|-----------|-------------------|-------------------|----------|
| Menu bar | translucent; Apple menu, bold app name, app menus, trailing extras + clock | glass panel, app name, trailing extras + clock, Notification Center from clock; app menus are a standard fallback | ●●○ |
| Dock | centred floating slab, magnification, running dots, Trash | same, + Downloads stack; fixed-size window internally for perf | ●●● |
| Control Center | frosted card grid, sliders, Now Playing | same card grid + brightness/volume sliders, power quick action | ●●○ |
| Notification Center | drops from clock; time/date, calendar, notifications | same; hands off to Windows notifications | ●●● |
| Mission Control | live window exposé | live thumbnails via DWM | ●●○ |
| Launchpad | full-screen app grid | same | ●●● |
| Spotlight | centred search; apps/files/calc | app search + launch; file-content partial | ●●○ |
| Liquid Glass | adaptive translucency + tint | acrylic + Direct2D tint, Clear/Balanced/Tinted presets, opacity+tint | ●●○ |
| Appearance | light/dark + accent + wallpapers | light/dark/auto + accent + wallpaper pairs | ●●● |
| Cursors | Apple cursors | Bibata (GPL-3.0) | ●●○ |
| Window chrome of apps | system-wide | our own windows only (no injection) | ●○○ |

## Where we match closely
Dock behaviour, Notification Center placement, Launchpad, appearance switching.

## Where we approximate
Glass material (our own, tunable), Control Center/Mission Control styling, cursors.

## Where we deliberately stop
Other apps' window chrome and a File Explorer replacement — both would require the
injection/patching this project forbids. Handled with honest fallbacks, not fakes.

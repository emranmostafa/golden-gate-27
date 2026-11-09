# Research — macOS Golden Gate UI

**Status / honesty note.** macOS 27 "Golden Gate" post-dates this project's
knowledge baseline, so the Golden-Gate-*specific* refinements below are **design
targets to verify** against Apple's live references, not confirmed measurements.
Where a detail is long-standing macOS behaviour (menu bar structure, Dock
magnification, light/dark), it's marked *observed*; Golden-Gate-27 specifics are
marked *to verify*. Our Windows values live in `../DESIGN_SYSTEM.md`.

**References to verify against**
- apple.com/os/macos/ · developer.apple.com/documentation/technologyoverviews/liquid-glass
- support.apple.com/en-us/122868
- macrumors.com/2026/06/08/apple-announces-macos-golden-gate/
- macworld.com Golden Gate design piece

## Menu bar (*observed*, Apple HIG)
- Thin translucent bar across the top. Left: Apple menu → **bold app name** →
  File/Edit/… app menus → Window/Help. Right (trailing): menu-bar extras →
  Spotlight → Control Center → clock (far right). Clicking the clock opens
  Notification Center. No power button in the bar (power is in the Apple menu).
- Extras are ~16 pt glyphs in a ~22 pt bar.
- **Our approximation:** same left/right structure; per-app menus are a standard
  fallback because Windows apps don't expose them universally.

## Dock (*observed*)
- Centred, floating above the bottom edge, translucent rounded slab; width grows
  with app count; pointer-proximity magnification scales neighbours smoothly;
  running indicator under active apps; Trash at the end.
- **Our approximation:** matches; adds a Downloads stack. Magnification curve and
  spacing in DESIGN_SYSTEM.md.

## Control Center (*observed* + *to verify* for Tahoe/Golden Gate styling)
- A grid of frosted cards: connectivity (Wi-Fi/Bluetooth/AirDrop), Now Playing,
  Focus, Display brightness, Sound, plus quick toggles. Opened from the trailing
  bar icon.
- **Our approximation:** same card grid; power added as a quick action (Windows
  has no equivalent trailing power elsewhere).

## Notification Center (*observed*)
- Drops from the clock; big time + date, a calendar widget, notifications list.
- **Our approximation:** matches; "Open Notification Center" hands off to Windows'
  own notifications.

## Spotlight (*observed*)
- Centred floating rounded search panel; type-to-search apps/files/calculator;
  keyboard-driven.
- **Our approximation:** app search + launch today; deep file-content search is
  partial (Windows Search).

## Appearance (*observed*)
- System-wide light/dark with matching wallpapers; a user accent color.
- **Our approximation:** light/dark/auto + accent + wallpaper pairs.

## To verify (Golden-Gate-27 specifics)
Exact bar height and corner radii, the latest Liquid Glass tint/opacity
behaviour, any Golden-Gate-only layout changes vs Tahoe 26. Do not state these as
fact until checked against the references above.

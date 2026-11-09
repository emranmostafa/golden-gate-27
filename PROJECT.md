# GoldenGate for Windows

A standalone desktop application that recreates the visual language and practical
desktop experience of Apple's macOS 27 "Golden Gate" on Windows 11 — **without
replacing Windows, patching system files, or depending on third-party
customization apps.**

> **Stack note.** An earlier brief specified C#/.NET 10/WPF. That is **superseded**:
> the product is built in **C++/Win32/Direct2D**, which is better suited to the
> primary goals (native integration, custom Liquid Glass rendering, smooth Dock
> animation, low resource use). Rationale: `docs/ARCHITECTURE.md` and
> `docs/PERFORMANCE_AUDIT.md`. Do not rewrite or migrate it.

## Primary objective
Make Windows 11 look and feel as close as reasonably possible to macOS Golden Gate
while remaining stable, safe, performant, reversible, and independently
maintainable. This is a real native Windows app — not a website, Electron wrapper,
or static mockup.

## Principles
- **Native and self-contained.** Documented Windows APIs only; runs next to
  Explorer; no injection, no system-file changes, no third-party transformation packs.
- **Reversible.** Everything it changes (theme, accent, cursors, wallpaper, taskbar
  visibility) is backed up and restored by the uninstaller and `--restore`.
- **Honest.** Features are marked done/pending/blocked (`docs/IMPLEMENTATION_PLAN.md`);
  a capability matrix (`docs/WINDOWS_APIS.md`) separates what Windows actually
  supports from our approximations. No fake system data, no placeholder buttons.
- **Original.** Our own rendering, icons where needed, and wallpapers; no Apple
  assets or trademarks shipped.

## Components
Menu bar / panel · floating magnifying Dock · Control Center · Notification Center
· Mission Control · Launchpad · Spotlight · theme + icon engine · wallpaper engine
· Settings app · Windows integration (startup, Explorer-restart recovery, DPI,
multi-monitor, fullscreen handling, reversible taskbar hiding).

## Design system
Liquid Glass material with three presets (**Clear / Balanced / Tinted**) plus
independent opacity and tint, light/dark/auto appearance, an accent color, and a
token set for radii/spacing/animation. See `docs/DESIGN_SYSTEM.md`.

## Documentation
`docs/ARCHITECTURE.md` · `docs/DESIGN_SYSTEM.md` · `docs/WINDOWS_APIS.md` ·
`docs/IMPLEMENTATION_PLAN.md` · `docs/TESTING.md` · `docs/KNOWN_LIMITATIONS.md` ·
`docs/PERFORMANCE_AUDIT.md` · `docs/CHANGELOG.md` · `docs/research/*` · `CLAUDE.md`
(working rules) · `README.md` (install & use).

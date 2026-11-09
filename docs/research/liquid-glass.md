# Research — Liquid Glass material

**Honesty note.** "Liquid Glass" is Apple's translucent material language; the
precise Golden-Gate-27 rendering is proprietary and post-cutoff. We do **not**
reproduce Apple's algorithm — we build an original material that reads as glass.
Specifics of the latest tint/opacity behaviour are *to verify* against
developer.apple.com/documentation/technologyoverviews/liquid-glass.

## Observed characteristics (public design language)
- Background translucency: content/wallpaper behind shows through a blur.
- Adaptive tint so foreground text stays legible over varied backdrops.
- Soft edge highlights and subtle depth; rounded geometry; soft shadows.
- Light and dark variants; tint strength varies by surface and context.

## Our implementation (original)
- **Real blur:** DWM transient-window backdrop + acrylic blur
  (`SetWindowCompositionAttribute`) so the live wallpaper/content behind shows
  through. Documented `DWMSBT_TRANSIENTWINDOW` is the fallback.
- **Tint:** a Direct2D fill + an ABGR acrylic tint computed by `PanelTint()` — a
  neutral grey blended toward the user's accent.
- **Two independent controls:** Opacity (tint alpha) and Tint (accent blend),
  surfaced as three presets — **Clear / Balanced / Tinted** — plus sliders.
- **Contrast:** palette provides adaptive text tiers (text/dim/faint) and
  panel/panelStrong layers so labels stay readable on glass.
- **Highlights/shadow:** a 1 px top stroke and layered soft shadows approximate
  edge light and depth (e.g. the Dock slab).

## Fidelity & limits
- We don't claim Apple's exact look; we expose knobs so users tune toward it.
- GPU-composed on Glass surfaces; the layered Dock is software today (see
  `../PERFORMANCE_AUDIT.md`). A `CompositionBackdropBrush` path would make the
  whole material documented and GPU-composed with custom tint.

## To verify
Latest Liquid Glass opacity/tint ranges and any Golden-Gate-27 changes to edge
highlights/reflections, against the Apple technology overview.

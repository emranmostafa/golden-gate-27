# Design System

Inspired by macOS Golden Gate. Values below are the ones the app actually uses
(`gfx.cpp` palette, `menubar.cpp`, `dock.cpp`); treat them as the single source of
truth and avoid hardcoding new ones elsewhere.

## Appearance
- **Modes:** Light, Dark, Auto (switches by time — `lightHour`/`darkHour`).
- **Accent:** user color (default `#3478F6`), used for highlights, selection, the
  active-icon feel, and the glass tint.

## Palette (`UpdatePalette`)
| Token | Light | Dark |
|-------|-------|------|
| text | `#1D1D1F` | `#F5F5F7` |
| textDim | 62% text | 62% text |
| textFaint | 36% text | 38% text |
| panel | `#F6F6F8` @55% | `#1E1E24` @58% |
| panelStrong | `#F7F7F9` @94% | `#26262C` @92% |
| separator | black @10% | white @12% |
| shadow | black @22% | black @45% |

## Liquid Glass material
Real translucency via a DWM transient-window backdrop + an acrylic blur tint
(`SetWindowCompositionAttribute`), with a Direct2D tint fill on top for contrast.
The tint is **ABGR**: alpha = opacity, color = a neutral grey blended toward the
accent by the tint amount (`PanelTint`).

**Two independent controls**
- **Opacity** (`panelGlass`, 0–100) → tint alpha `0x18…0xE0` (clearer → more solid).
- **Tint** (`panelTint`, 0–100) → how far the grey blends toward the accent (capped
  at 60% influence).

**Three presets** (Settings → Panel → *Glass style*)
| Preset | Opacity | Tint |
|--------|---------|------|
| Clear | 20 | 0 |
| Balanced (default) | 45 | 15 |
| Tinted | 72 | 55 |

Presets just set the two sliders; users can fine-tune either independently.

## Typography
UI font **Inter** when installed, else **Segoe UI Variable Text**, else Segoe UI.
Icon font **Segoe Fluent Icons**, falling back to Segoe MDL2 Assets. Text formats
are cached by (size, weight, align, icon); grayscale AA.

## Geometry & spacing
- Corner radii: panels/cards ~14–20; Dock slab 20; sliders fully rounded.
- **Panel:** height configurable 22–40 (default 26).
- **Dock:** base icon 32–84 (default 52), magnified default 84; gap 6, padX 12,
  padY 10; running-dot under active apps; window height = maxIcon + 30 to allow
  magnified spill above the slab.
- **Control Center:** 326-wide grid of frosted cards; 16 px gutters, 10 px gaps.

## Motion
- Dock magnification eases at 0.35/frame toward target; reveal/slide at 0.28/frame;
  launch bounce is a sine arc.
- Transient open/close animations (Launchpad, Mission Control, fan, finder) run on
  16 ms timers that stop when the animation completes (no idle rendering).
- Respect for reduced-motion and a global animation-speed control are **planned**
  (see IMPLEMENTATION_PLAN.md).

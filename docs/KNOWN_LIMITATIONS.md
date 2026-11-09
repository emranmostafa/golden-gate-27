# Known Limitations

Honest list of what Golden Gate does **not** or **cannot** do, and why.

## By design (no injection / no patching)
- **Other apps' window chrome** (title bars, buttons, sidebars) cannot be restyled.
  Doing so safely across arbitrary apps isn't possible without injection, which this
  project forbids. Our macOS-style traffic-light buttons appear only on
  Golden Gate's own windows.
- **No system-wide File Explorer replacement.** An optional original file manager
  built on Shell APIs could be added later, clearly labelled — it would not replace
  Explorer.
- **Lock screen, secure desktop, UAC** are never themed or touched.

## Platform constraints
- **Glass/acrylic & rounded corners** need Windows 11 (22H2+ for best results). On
  older builds the panels fall back to a tinted fill.
- **Display brightness** works only on panels exposing the WMI brightness interface
  (most laptops; many external monitors don't).
- **Menu-bar app menus**: Windows apps don't universally expose macOS-style menu
  structures, so the panel shows the active app's name with a standard fallback
  menu rather than each app's real File/Edit/… menus.
- **Now Playing** uses global media keys — works with most players, but there's no
  per-app media routing or (yet) track metadata.

## Implementation / undocumented surface
- **Acrylic tint** uses `SetWindowCompositionAttribute`, which is undocumented but
  long-stable. The documented `DWMSBT_TRANSIENTWINDOW` backdrop is wired as a
  fallback but doesn't allow a custom tint (which the Glass presets need). A future
  move to a `CompositionBackdropBrush` would make this fully documented
  (PERFORMANCE_AUDIT.md §4).

## Performance (to validate on device)
- The Dock renders in software (layered per-pixel path); P0/P1 removed the worst
  costs (per-frame target rebuild; 30 ms window scans). A DirectComposition Dock
  (P2) is deferred until on-device profiling shows it's needed. See
  PERFORMANCE_AUDIT.md.

## Verification
Visual and performance results can't be confirmed from the Linux/Wine build
environment; they require real Windows 11 hardware. Claims about appearance or
speed should come from screenshots or profiling, not the build.

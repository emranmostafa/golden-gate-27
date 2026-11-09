# GoldenGate for Windows — Technical Rendering & Performance Audit

**Scope:** rendering pipeline, layered-window upload cost, DirectComposition evaluation, `SetWindowCompositionAttribute` usage, idle/animation cost, timers/redraws/allocations/sync.
**Rule followed:** findings and evidence only. **No code was modified.** Every recommendation is a hypothesis to confirm with on-device profiling before any change.
**Build audited:** v1.7, `src/*` as of 2026-11-09. Evidence is cited as `file:line`.

---

## 0. Executive summary

The architecture is sound for the stated goals and does **not** need migrating. Two concrete, evidence-backed hot paths are worth fixing, both in the **Dock**:

| # | Finding | Where | Severity | Type |
|---|---------|-------|----------|------|
| F1 | Dock renders in **software** (`ID2D1DCRenderTarget` is CPU-only), unlike the Glass surfaces which are GPU | `gfx.cpp:260-262` | Medium | Rendering |
| F2 | Magnification **destroys and rebuilds the render target + DIB + entire icon-bitmap cache every frame** because the Dock window width changes per mouse-move | `gfx.cpp:229,283-290`, `dock.cpp:200-214` | **High** | Allocation/sync |
| F3 | `UpdateLayeredWindow` uploads the **whole** Dock surface every frame (no dirty region) | `gfx.cpp:306-313` | Medium | Upload |
| F4 | Dock `intelliTimer` runs **every 30 ms for the app's whole life**, and in the default intellihide mode each tick runs **`EnumWindows` + a per-window DWM query** | `dock.cpp:267-280,444,464` | **High** | Idle CPU |
| F5 | `SetWindowCompositionAttribute` acrylic is undocumented; a documented path is already wired but disabled | `gfx.cpp:50-59,197-198` | Low | Compatibility |
| F6 | Animation timers are USER `WM_TIMER` (~15.6 ms, not vsync-aligned); magnification has **no clock at all** (mouse-move driven, stalls when the cursor stops) | `dock.cpp:338-345`, `*:16ms timers` | Low–Med | Frame pacing |

**The single highest-value fix is F2+F4 (the Dock), not a rendering-engine rewrite.** F1/F3 are real but secondary; DirectComposition would address F1/F2/F3/F6 together if profiling justifies the effort.

---

## 1. Direct2D pipeline: hardware vs software

**Finding: mixed — Glass surfaces are hardware, Layered surfaces (incl. the Dock) are software.**

The D2D factory is single-threaded (`gfx.cpp:35`). Render targets are created two ways in `Surface::MakeTarget()`:

- **Glass** surfaces → `CreateHwndRenderTarget` with `D2D1_RENDER_TARGET_TYPE_DEFAULT` (`gfx.cpp:252-257`). `DEFAULT` lets D2D pick the GPU when a Direct3D device is available, so on real hardware these are **hardware-accelerated**. Glass surfaces are: menu bar, menus/popups, Control Center, Notification Center, Mission Control, Launchpad, Settings.
- **Layered** surfaces → `CreateDCRenderTarget` (`gfx.cpp:260-262`). **`ID2D1DCRenderTarget` has no hardware path — it always rasterizes on the CPU (WARP/software).** Layered surfaces are: **the Dock**, the Downloads fan, the cursor finder.

**Consequence:** the most frequently animated surface in the whole product — the Dock, redrawn on every mouse-move during magnification — is the one surface that renders on the CPU. That is the opposite of what you'd want and is the root of F1.

> Evidence it's intentional, not accidental: the layered branch sets `D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE` (`gfx.cpp:261`) specifically so the result can be fed to GDI's `UpdateLayeredWindow`. Per-pixel-alpha rounded Dock edges were the reason; `DCRenderTarget`+ULW was the simplest way to get them.

---

## 2. Does `UpdateLayeredWindow` upload the whole Dock surface each frame?

**Finding: yes — and worse, the surface is also rebuilt each magnify frame.**

`Surface::Render()` for a Layered window (`gfx.cpp:292-313`):

1. `MakeTarget()` (recreates the target if it was dropped — see F2),
2. `BindDC` → `BeginDraw` → `Clear` the **entire** surface → `OnPaint()` redraws **everything** → `EndDraw`,
3. `UpdateLayeredWindow(hwnd, nullptr, &dst, &sz, memDC, &src, 0, &bf, ULW_ALPHA)` with `sz = {width, height}` and **no dirty rectangle** (`gfx.cpp:312`).

`UpdateLayeredWindow` has no partial-update parameter, so the complete premultiplied DIB is handed to the compositor every call. For the Dock that surface is `dockWidth × (maxIconSize+30)` at DPI scale — i.e. a wide bitmap — re-uploaded on every magnify frame.

**The compounding problem (F2):** magnification changes the Dock's **window width** every frame. `Relayout()` recomputes total width from the animated `curSize` values and writes a new `shownRect` (`dock.cpp:200-212`), then `PositionWindow()` → `Move()`. `Move()` compares integer width/height and, on any change, calls **`DropTarget()`** (`gfx.cpp:229`), which releases the render target, the brush, the DIB **and the entire `bmpCache` of icon bitmaps** (`gfx.cpp:283-290`). The next `Render()` recreates the DIB + DC target and re-creates every icon `ID2D1Bitmap` from its cached WIC source (`gfx.cpp:343-352`).

So, **per magnify frame**, at mouse-move frequency (often 100–125 Hz): free D2D target + DIB + all icon bitmaps → allocate DIB + target → software-raster the slab, its 6-pass soft shadow (`dock.cpp:290-296`), every icon, dots and the hover label → full-surface `UpdateLayeredWindow`. This is the dominant active-use cost and is pure overhead — the content barely changes frame to frame.

---

## 3. Would DirectComposition help?

**Finding: yes, materially — and it is the common fix for F1, F2, F3 and F6.** It is also the largest change, so it is gated on profiling.

Moving the **layered surfaces (Dock first)** to a DirectComposition visual backed by a D3D11 + `ID2D1DeviceContext` swap-chain/surface would:

- **Make the Dock hardware-rendered** (resolves F1).
- **Make magnification and the slide-in GPU transforms on the compositor**, not CPU re-rasters. Scaling icons and sliding the Dock become `IDCompositionVisual` transform animations; the pixels are drawn once and the compositor animates them. This removes the per-frame re-raster **and** removes the width-driven target teardown (resolves F2).
- **Eliminate `UpdateLayeredWindow`** — DirectComposition owns the surface; only genuinely changed regions are redrawn via `BeginDraw(updateRect)` (resolves F3).
- **Run animations on the compositor clock**, vsync-aligned and high-refresh-capable (resolves F6).

**Cost / risk:** a new D3D11 device + `IDCompositionDevice` + visual tree + per-window swap-chain, plus device-lost handling, DPI, and z-order integration with the existing `HWND_TOPMOST` reassertion. Medium-high effort; adds complexity. **Recommendation:** prototype DComp for the **Dock only**, measure against the current path, and keep the Glass `HwndRenderTarget` surfaces as-is (they're already GPU and not hot).

**Lower-effort alternative that captures most of F2/F3 without DComp:** give the Dock a **fixed-size window** sized to the fully-magnified bounds and draw the bar centered inside it. Width stops changing during magnification, so `Move()` never triggers `DropTarget()` — the target and icon cache live for the Dock's lifetime, and you can switch to `UpdateLayeredWindowIndirect` with a `prcDirty`. This is a contained change that removes the worst cost (F2) while staying on the current ULW architecture. Strongly recommended as the first step.

---

## 4. `SetWindowCompositionAttribute` usage and alternatives

**Finding: one use site, deliberately chosen over an already-wired documented path, for tint control.**

`SetWindowCompositionAttribute` is used only in `EnableAcrylic()` (`gfx.cpp:50-59`), called from the menu bar, Control Center, Notification Center and popup menus. It sets the undocumented `ACCENT_ENABLE_ACRYLICBLURBEHIND` with a custom ABGR tint.

Notably, `Surface::Create()` **already sets the documented** `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW` for Glass windows (`gfx.cpp:197-198`), and `EnableAcrylic()` then **turns it back off** (`DWMSBT_NONE`, `gfx.cpp:54-55`) so the two don't fight. The undocumented path is kept for one reason: it accepts a **custom tint** (the Panel "Glass" slider), which the documented backdrop does not expose.

**Alternatives, by preference:**

1. **Windows.UI.Composition `CompositionBackdropBrush`** (+ Gaussian blur + tint layer), composed via DirectComposition interop. Fully documented, GPU-composed, and gives **custom tint *and* blur radius** — the only option that keeps the tint feature without an undocumented call. Ties into the §3 DComp work.
2. **`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`** — already present, documented, stable on Win11 22H2+. Trade-off: **loses the custom tint knob** (tint follows the system). Acceptable only if we drop user-tunable tint.
3. **`DwmEnableBlurBehind`** — legacy, blur without tint control. Not recommended.

**Risk of staying on SWCA:** undocumented and version-fragile; historically associated with laggy window dragging on some builds when acrylic-blur-behind is active. Low severity today (it has been stable for years), but it's the one compatibility liability in the codebase. **Recommendation:** keep it for now; migrate to option 1 if/when the Dock DComp work lands, so the whole app shares one documented GPU compositor path.

---

## 5. Profiling (idle CPU, animation CPU, GPU, memory, frame pacing)

**Honest limitation:** this audit environment is **Linux + Wine**, which does not reproduce Windows DWM, the real GPU driver, or `UpdateLayeredWindow` compositor cost. **No real CPU/GPU/FPS numbers can be produced here, and I will not invent any.** What follows is the measurement plan plus predictions derived from the code, to be confirmed on a real Windows 11 machine.

**Tools (all free, on-device):**
- **CPU / wakeups:** Windows Performance Recorder/Analyzer (WPR/WPA, ETW) — CPU Usage (Sampled) + "Timer Resolution" and context-switch graphs. Task Manager → Details → per-process CPU for a quick read.
- **GPU / frame pacing:** **PresentMon** (frame times, present mode) and **GPUView**; Task Manager → Performance → GPU for a coarse read.
- **Memory / GDI handles:** VMMap, RAMMap, and Task Manager "GDI objects" / "USER objects" columns (watch for churn during magnification — F2 predicts GDI object thrash).
- **D2D/D3D specifics:** Visual Studio Graphics Diagnostics or PIX if deeper GPU timing is wanted.

**What to measure, and the prediction for each:**

| Scenario | How | Prediction (to confirm) |
|----------|-----|-------------------------|
| **Idle**, cursor away from Dock, intellihide on | WPA CPU over 60 s; count wakeups | Small but non-trivial steady CPU from the **30 ms `EnumWindows`+DWM scan** (F4); 300 ms/1 s/2 s timers negligible. Expect the Dock scan to dominate idle. |
| **Dock magnify**, sweep cursor across Dock | PresentMon + WPA during the sweep | High CPU spike and **GDI-object churn** from target/DIB/bitmap rebuild (F2); frame times irregular (F6, no animation clock). This is the worst case. |
| **Mission Control / Launchpad open** | PresentMon during open/close | Full-screen Glass surfaces are **GPU** (fine); verify no software fallback. |
| **Menu bar clock** | WPA, 60 s | One full-bar HW repaint/second (`menubar.cpp:405`) — cheap; confirm it's not invalidating more than needed. |
| **Memory** over 30 min idle | VMMap snapshots | Should be flat; confirm icon caches aren't growing. |

**Acceptance thresholds to adopt (from the brief):** 60 FPS during animations, low idle CPU, no unnecessary continuous rendering. Treat the magnify sweep and idle-with-intellihide as the two gating benchmarks.

---

## 6. Unnecessary timers, redraws, allocations, and sync

**Timers (all enumerated):**

| Timer | Interval | Lifetime | Per-tick work | Assessment |
|-------|----------|----------|---------------|------------|
| Dock `intelliTimer` (`dock.cpp:464`) | **30 ms** | **always** | `IntelliTick`: cursor/edge check; in intellihide (default) **`EnumWindows` + per-window `DWMWA_CLOAKED`** (`dock.cpp:249-253`) | **F4 — highest idle cost.** 33×/s full window-tree scan when cursor is away. |
| `ReassertTimer` (`main.cpp:252`) | 300 ms | always | `TaskbarKeepHidden` (visibility check) + fullscreen guard + a few `SetWindowPos` topmost (`main.cpp:119-125`) | Low cost, but continuous; candidate to slow down / make event-driven. |
| `TickTimer` (`main.cpp:251`) | 1000 ms | always | clock/theme-time check | Negligible. |
| Menu-bar clock (`menubar.cpp:423`) | 1000/2000 ms | always | `Invalidate` (full-bar HW repaint) + `SetWindowPos` topmost (`menubar.cpp:405-407`) | Low (HW). Could repaint only the clock region. |
| Anim timers — fan/launchpad/mission/finder (`*:16 ms`) | 16 ms | **transient** (killed at `t>=1`) | one animation step + `Invalidate` | **Good pattern** — only run during an animation. |

**Redraws:**
- **F2 dominates:** full software re-raster + target/cache teardown per magnify frame (§2).
- Dock soft shadow is **6 rounded-rect fills every Dock paint** (`dock.cpp:290-296`) — on the CPU path that's 6 extra rasters/frame. Minor, but it rides on the worst path; a cached shadow bitmap would remove it.
- Menu-bar clock repaints the **whole bar** each second rather than the clock rectangle (`menubar.cpp:405`). Cheap on GPU; still avoidable.

**Allocations:**
- **Per magnify frame:** DIB + render target + every icon `ID2D1Bitmap` recreated (F2). This is the one real allocation hot path and it also churns GDI/GPU objects.
- `Font()` builds a `wchar_t[96]` key + `std::wstring` map lookup per `Text()` call (`gfx.cpp:61-83`). Tiny, but it's per-glyph-run per frame; negligible next to F2.
- `CCenter::OnPaint` rebuilds a `hits` vector each paint — trivial, Control Center isn't a hot surface.

**GPU/CPU sync:**
- Glass `HwndRenderTarget` `EndDraw` presents via DXGI (vsync blt) independently per window — fine.
- Layered path's sync point is `UpdateLayeredWindow` (CPU→DWM), once per frame (F3). No explicit `Flush`/stalls elsewhere; single-threaded factory means all drawing is on the UI thread, which is acceptable given the surfaces are small — **except** the Dock under F2.

---

## 7. Functionality to preserve (constraint on any later change)

Any fix must keep, pixel- and behavior-identical: the Dock's rounded per-pixel-alpha glass edges and soft shadow; magnification curve (`dock.cpp:162-170`) and hover name tag; intellihide/auto-hide/reserve behavior and the bottom-edge reveal; AppBar space reservation when "never hide"; the Panel glass **tint slider** (depends on the custom-tint acrylic, §4); all existing settings keys; multi-monitor placement; and the `--restore`/uninstall path. No change below should touch these outcomes.

---

## 8. Prioritized recommendations

Each is a **proposal to validate by profiling first** (per the brief); none has been applied.

**P0 — Dock: stop the per-frame target/cache teardown (F2).**
Make the Dock window a **fixed size** (fully-magnified bounds) and center the bar inside it, so width stops changing during magnification and `Move()` no longer triggers `DropTarget()`. Keep ULW but switch to `UpdateLayeredWindowIndirect` with a dirty rect.
*Expected:* removes the dominant active-use cost and GDI churn. *Effort:* low–medium, contained to `dock.cpp` + a small `gfx.cpp` layered tweak. *Risk:* must re-check hit-testing and the AppBar rect against the new fixed window. *Confirm with:* magnify-sweep WPA + GDI-object count before/after.

**P1 — Dock: make intellihide event-driven (F4).**
Replace the unconditional 30 ms `EnumWindows` scan with: run the slide at 30 ms **only while a reveal animation is in progress**; drive the overlap decision from `SetWinEventHook` (`EVENT_OBJECT_LOCATIONCHANGE`/foreground, already used elsewhere) or a throttled ~250 ms check when the cursor is away. Cache the window list.
*Expected:* large idle-CPU reduction. *Effort:* medium. *Risk:* get the hide/reveal latency to still feel instant. *Confirm with:* 60 s idle WPA before/after.

**P2 — Dock → DirectComposition (F1, F3, F6), only if P0/P1 don't hit the FPS/idle targets.**
Prototype the Dock on a DComp visual (D3D11 + `ID2D1DeviceContext`), magnification/slide as compositor transforms, no ULW. *Expected:* GPU rendering, vsync-aligned animation, smallest redraws. *Effort:* high. *Risk:* device-lost/DPI/z-order; keep Glass surfaces unchanged. *Confirm with:* PresentMon frame pacing vs the P0/P1 build.

**P3 — Low-risk cleanups.** Cache the Dock shadow as a bitmap; repaint only the clock rectangle; optionally slow `ReassertTimer` to ~500 ms or gate it on the foreground hook. Small, independent wins.

**P4 — Compatibility (F5).** Keep `SetWindowCompositionAttribute` for now; if P2 lands, move acrylic to a `CompositionBackdropBrush` so the app has one documented GPU compositor path and keeps the custom tint.

**Suggested order:** P0 → P1 → re-profile → decide on P2. Do P3/P4 opportunistically. **No code changes until you've reviewed this and agreed the direction.**

---

*Prepared from static analysis of the v1.7 source. On-device profiling (Windows 11, real GPU) is required to confirm the predicted magnitudes before implementing P0–P2.*

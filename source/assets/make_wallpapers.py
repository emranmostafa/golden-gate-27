#!/usr/bin/env python3
"""Golden Gate wallpapers (original artwork, made for this theme).

Soft silk waves over a deep gradient, one for light mode (dawn) and one for
dark mode (night), plus the dial-clock screensaver background. Rendered at
3840x2160 from code, so they are ours to ship.

usage: make_wallpapers.py OUTDIR
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

W, H = 3840, 2160


def lerp(a, b, t):
    return a + (b - a) * t


def grad(stops, t):
    """stops: [(pos, (r,g,b)), ...], t: array 0..1 -> rgb array"""
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        m = (t >= p0) & (t <= p1)
        u = ((t - p0) / max(p1 - p0, 1e-6))[m][:, None]
        u = u * u * (3 - 2 * u)
        out[m] = lerp(np.array(c0, np.float32), np.array(c1, np.float32), u)
    return out


def render(stops, waves, glow, line_col, out):
    """waves: back to front, each (amp, freq, phase, base, top_rgb, bottom_rgb, alpha)."""
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    xn, yn = x / W, y / H
    img = grad(stops, yn * 0.85 + xn * 0.15)
    gx, gy, gr, gc = glow
    g = np.exp(-(((xn - gx) * 1.6) ** 2 + ((yn - gy) * 1.0) ** 2) / gr)[..., None]
    img = img + np.array(gc, np.float32) * g
    for amp, freq, phase, base, top, bottom, alpha in waves:
        centre = base + amp * np.sin(2 * np.pi * (freq * xn + phase)) + 0.025 * np.sin(2 * np.pi * (1.7 * xn + phase * 2.1))
        dpx = (yn - centre) * H                      # pixels below the crest
        inside = np.clip(dpx / 2.0 + 0.5, 0, 1)      # anti-aliased edge
        t = np.clip(dpx / (H * 0.35), 0, 1)[..., None]
        t = t * t * (3 - 2 * t)
        fill = lerp(np.array(top, np.float32), np.array(bottom, np.float32), t)
        a = (alpha * inside)[..., None]
        img = img * (1 - a) + fill * a
        # silk sheen: a crisp highlight on the crest that fades into the band
        sheen = (np.exp(-(dpx / 2.2) ** 2) * 0.55 + np.exp(-(np.maximum(dpx, 0) / 60.0) ** 2) * (dpx >= 0) * 0.22)[..., None]
        img = img + np.array(line_col, np.float32) * sheen * alpha
        # soft shadow cast above the band on what is behind it
        shadow = (np.exp(-(np.minimum(dpx, 0) / 90.0) ** 2) * (dpx < 0) * 0.18)[..., None]
        img = img * (1 - shadow)
    rng = np.random.default_rng(7)
    img = img + rng.normal(0, 1.1, img.shape).astype(np.float32)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB')
    im.save(out, quality=94, subsampling=0)


def main(outdir):
    night = [(0.0, (14, 10, 52)), (0.45, (46, 28, 150)), (1.0, (24, 40, 150))]
    render(night,
           [(0.07, 0.45, 0.10, 0.46, (58, 60, 210), (24, 30, 120), 0.95),
            (0.08, 0.45, 0.22, 0.60, (40, 76, 220), (16, 26, 100), 0.95),
            (0.07, 0.45, 0.34, 0.76, (30, 92, 226), (10, 22, 82), 0.95)],
           (0.78, 0.22, 0.10, (46, 32, 100)), (190, 205, 255),
           f'{outdir}/GoldenGate-Night.jpg')
    dawn = [(0.0, (255, 226, 200)), (0.45, (244, 178, 190)), (1.0, (196, 176, 232))]
    render(dawn,
           [(0.07, 0.45, 0.10, 0.46, (248, 190, 204), (214, 150, 190), 0.95),
            (0.08, 0.45, 0.22, 0.60, (196, 186, 244), (150, 140, 222), 0.95),
            (0.07, 0.45, 0.34, 0.76, (150, 170, 240), (100, 120, 210), 0.95)],
           (0.25, 0.18, 0.12, (30, 24, 6)), (255, 252, 248),
           f'{outdir}/GoldenGate-Dawn.jpg')
    lock = [(0.0, (7, 9, 14)), (0.6, (12, 17, 28)), (1.0, (18, 26, 42))]
    render(lock, [(0.04, 0.4, 0.2, 0.84, (22, 32, 52), (10, 14, 24), 0.9)],
           (0.5, 1.0, 0.18, (52, 72, 100)), (80, 100, 140),
           f'{outdir}/GoldenGate-Lock.jpg')


if __name__ == '__main__':
    main(sys.argv[1])

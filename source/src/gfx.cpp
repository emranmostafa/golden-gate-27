// Direct2D / DirectWrite / WIC setup and the Surface window class.
#include "common.h"

ID2D1Factory *g_d2d;
IDWriteFactory *g_dw;
IWICImagingFactory *g_wic;
std::wstring g_font = L"Segoe UI Variable Text";
std::wstring g_iconFont = L"Segoe Fluent Icons";
Palette g_pal;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define GG_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define GG_DWMWA_BORDER_COLOR 34
#define GG_DWMWA_SYSTEMBACKDROP_TYPE 38
#define GG_DWMSBT_TRANSIENTWINDOW 3
#define GG_DWMWCP_ROUND 2
#define GG_DWMWCP_DONOTROUND 1

static bool FontInstalled(const wchar_t *family) {
    IDWriteFontCollection *col = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&col, TRUE))) {
        UINT32 idx;
        BOOL exists = FALSE;
        col->FindFamilyName(family, &idx, &exists);
        ok = exists;
        col->Release();
    }
    return ok;
}

bool GfxInit() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&g_dw))) return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic)))) return false;
    if (FontInstalled(L"Inter")) g_font = L"Inter";
    else if (!FontInstalled(L"Segoe UI Variable Text")) g_font = L"Segoe UI";
    if (!FontInstalled(L"Segoe Fluent Icons")) g_iconFont = L"Segoe MDL2 Assets";
    return true;
}

// --- real acrylic blur via the (undocumented but long-stable) composition API
enum GG_ACCENT_STATE { GG_ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };
struct GG_ACCENT_POLICY { DWORD state; DWORD flags; DWORD gradient; DWORD anim; };
struct GG_WINCOMPATTRDATA { DWORD attrib; PVOID data; SIZE_T size; };
typedef BOOL(WINAPI *SetWCA)(HWND, GG_WINCOMPATTRDATA *);

void EnableAcrylic(HWND hwnd, unsigned abgrTint) {
    static SetWCA set = (SetWCA)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (!set) return;
    // drop the DWM system backdrop so the two don't fight
    int none = 1;   // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &none, sizeof(none));
    GG_ACCENT_POLICY accent = {GG_ACCENT_ENABLE_ACRYLICBLURBEHIND, 0, abgrTint, 0};
    GG_WINCOMPATTRDATA data = {19 /*WCA_ACCENT_POLICY*/, &accent, sizeof(accent)};
    set(hwnd, &data);
}

IDWriteTextFormat *Font(float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT a, bool icon) {
    static std::map<std::wstring, IDWriteTextFormat *> cache;
    wchar_t key[96];
    swprintf(key, 96, L"%.2f|%d|%d|%d", size, (int)w, (int)a, (int)icon);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat *f = nullptr;
    g_dw->CreateTextFormat(icon ? g_iconFont.c_str() : g_font.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                           DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f) {
        f->SetTextAlignment(a);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject *ell = nullptr;
        DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(f, &ell))) {
            f->SetTrimming(&trim, ell);
            ell->Release();
        }
    }
    cache[key] = f;
    return f;
}

float TextWidth(const std::wstring &s, float size, DWRITE_FONT_WEIGHT w, bool icon) {
    IDWriteTextLayout *l = nullptr;
    float r = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), Font(size, w, DWRITE_TEXT_ALIGNMENT_LEADING, icon), 4000, 100, &l))) {
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        r = m.widthIncludingTrailingWhitespace;
        l->Release();
    }
    return r;
}
// height of text wrapped to maxWidth
float TextHeight(const std::wstring &s, float size, float maxWidth, DWRITE_FONT_WEIGHT w) {
    IDWriteTextLayout *l = nullptr;
    float r = 0;
    if (SUCCEEDED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), Font(size, w, DWRITE_TEXT_ALIGNMENT_LEADING, false),
                                         std::max(1.f, maxWidth), 4000, &l))) {
        l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);   // the shared formats don't wrap
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        r = m.height;
        l->Release();
    }
    return r;
}

D2D1_COLOR_F Rgb(unsigned rgb, float a) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
}
D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }
D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

void UpdatePalette() {
    Palette &p = g_pal;
    p.dark = IsDarkNow();
    D2D1_COLOR_F acc = Rgb(g_set.accent);
    p.accent = acc;
    p.accentText = D2D1::ColorF(1, 1, 1, 1);
    if (p.dark) {
        p.text = Rgb(0xF5F5F7);
        p.textDim = Rgb(0xF5F5F7, 0.62f);
        p.textFaint = Rgb(0xF5F5F7, 0.38f);
        p.panel = Rgb(0x1E1E24, 0.58f);
        p.panelStrong = Rgb(0x26262C, 0.92f);
        p.hover = Rgb(0xFFFFFF, 0.12f);
        p.separator = Rgb(0xFFFFFF, 0.12f);
        p.shadow = Rgb(0x000000, 0.45f);
    } else {
        p.text = Rgb(0x1D1D1F);
        p.textDim = Rgb(0x1D1D1F, 0.62f);
        p.textFaint = Rgb(0x1D1D1F, 0.36f);
        p.panel = Rgb(0xF6F6F8, 0.55f);
        p.panelStrong = Rgb(0xF7F7F9, 0.94f);
        p.hover = Rgb(0x000000, 0.08f);
        p.separator = Rgb(0x000000, 0.10f);
        p.shadow = Rgb(0x000000, 0.22f);
    }
}

// ------------------------------------------------------------------ WIC ---
static IWICBitmapSource *ToPbgra(IWICBitmapSource *src) {
    if (!src) return nullptr;
    IWICBitmapSource *conv = nullptr;
    WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, src, &conv);
    src->Release();
    if (!conv) return nullptr;
    // keep the pixels in memory so the source stays valid after the file closes
    IWICBitmap *mem = nullptr;
    g_wic->CreateBitmapFromSource(conv, WICBitmapCacheOnLoad, &mem);
    conv->Release();
    return mem;
}
IWICBitmapSource *LoadWicFile(const std::wstring &path) {
    IWICBitmapDecoder *dec = nullptr;
    if (FAILED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
    IWICBitmapFrameDecode *fr = nullptr;
    dec->GetFrame(0, &fr);
    dec->Release();
    return ToPbgra(fr);
}
IWICBitmapSource *WicFromHBitmap(HBITMAP hb) {
    if (!hb) return nullptr;
    IWICBitmap *b = nullptr;
    // shell icons are straight alpha (colour channels exceed alpha on their edges).
    // Reading them as premultiplied drew corner pixels far too bright: a jagged fringe.
    g_wic->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapUseAlpha, &b);
    return ToPbgra(b);
}
IWICBitmapSource *WicFromHIcon(HICON hi) {
    if (!hi) return nullptr;
    IWICBitmap *b = nullptr;
    g_wic->CreateBitmapFromHICON(hi, &b);
    return ToPbgra(b);
}

// Windows icons have no shared safe area: some fill their canvas, others (the
// bundled set) leave 10-25% empty, so equal-sized squares look like unequal icons.
// Trim each to its visible artwork and re-pad so the longest side is the same share
// of a square canvas. Pixels are only moved, never resampled, so nothing softens.
IWICBitmapSource *NormalizeIcon(IWICBitmapSource *src) {
    static std::map<IWICBitmapSource *, IWICBitmapSource *> cache;
    if (!src) return nullptr;
    auto f = cache.find(src);
    if (f != cache.end()) return f->second;
    IWICBitmapSource *out = src;
    UINT uw = 0, uh = 0;
    src->GetSize(&uw, &uh);
    int w = (int)uw, h = (int)uh;
    std::vector<BYTE> px((size_t)w * h * 4);
    if (w && h && SUCCEEDED(src->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) {
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (px[(size_t)y * w * 4 + (size_t)x * 4 + 3] <= 32) continue;   // visible artwork only
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
        if (x1 >= 0) {
            int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
            int canvas = (int)ceilf(std::max(cw, ch) / 0.86f);   // artwork = 86%, as the bundled set
            std::vector<BYTE> dst((size_t)canvas * canvas * 4, 0);
            int ox = (canvas - cw) / 2, oy = (canvas - ch) / 2;
            for (int y = 0; y < ch; y++)
                memcpy(&dst[(size_t)(y + oy) * canvas * 4 + (size_t)ox * 4],
                       &px[(size_t)(y + y0) * w * 4 + (size_t)x0 * 4], (size_t)cw * 4);
            IWICBitmap *b = nullptr;
            if (SUCCEEDED(g_wic->CreateBitmapFromMemory(canvas, canvas, GUID_WICPixelFormat32bppPBGRA, canvas * 4,
                                                        (UINT)dst.size(), dst.data(), &b)) && b)
                out = b;
        }
    }
    cache[src] = out;
    return out;
}

// --------------------------------------------------------------- Surface ---
Surface::~Surface() { Destroy(); }

bool Surface::Create(Kind k, const wchar_t *title, DWORD exStyle, RECT r, HWND owner, bool roundCorners) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"GoldenGateSurface";
        wc.style = CS_DBLCLKS;
        RegisterClassExW(&wc);
        registered = true;
    }
    kind = k;
    DWORD ex = exStyle | WS_EX_TOOLWINDOW | (k == Layered ? WS_EX_LAYERED : 0);
    hwnd = CreateWindowExW(ex, L"GoldenGateSurface", title, WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                           owner, nullptr, g_inst, this);
    if (!hwnd) return false;
    scale = DpiScaleForMonitor(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST));
    width = r.right - r.left;
    height = r.bottom - r.top;
    if (k == Glass) {
        MARGINS m = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &m);
        BOOL dark = g_pal.dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        int corner = roundCorners ? GG_DWMWCP_ROUND : GG_DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, GG_DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        int backdrop = GG_DWMSBT_TRANSIENTWINDOW;
        glassOk = SUCCEEDED(DwmSetWindowAttribute(hwnd, GG_DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
        if (!roundCorners) {
            COLORREF none = 0xFFFFFFFE;   // DWMWA_COLOR_NONE: no border line
            DwmSetWindowAttribute(hwnd, GG_DWMWA_BORDER_COLOR, &none, sizeof(none));
        }
    }
    return true;
}

void Surface::Destroy() {
    DropTarget();
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
}

void Surface::Invalidate() {
    if (!hwnd) return;
    if (kind == Layered) Render();
    else InvalidateRect(hwnd, nullptr, FALSE);
}

void Surface::Move(RECT r) {
    if (!hwnd) return;
    float ns = DpiScaleForMonitor(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST));
    bool sizeChanged = (r.right - r.left != width) || (r.bottom - r.top != height) || ns != scale;
    width = r.right - r.left;
    height = r.bottom - r.top;
    scale = ns;
    if (sizeChanged) DropTarget();
    if (kind == Layered) {
        SetWindowPos(hwnd, nullptr, r.left, r.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        Render();
    } else {
        SetWindowPos(hwnd, nullptr, r.left, r.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void Surface::Show(bool show, bool activate) {
    if (!hwnd) return;
    if (show) {
        if (kind == Layered) Render();
        ShowWindow(hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
        if (activate) SetForegroundWindow(hwnd);
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }
}

void Surface::MakeTarget() {
    if (rt) return;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.f * scale, 96.f * scale);
    if (kind == Glass) {
        ID2D1HwndRenderTarget *h = nullptr;
        g_d2d->CreateHwndRenderTarget(p, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)), &h);
        rt = h;
    } else {
        ID2D1DCRenderTarget *dc = nullptr;
        p.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
        g_d2d->CreateDCRenderTarget(&p, &dc);
        rt = dc;
        HDC screen = GetDC(nullptr);
        memDC = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = std::max(1, width);
        bi.bmiHeader.biHeight = -std::max(1, height);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void *bits = nullptr;
        dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        SelectObject(memDC, dib);
    }
    if (rt) {
        rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
}

void Surface::DropTarget() {
    for (auto &kv : bmpCache) SafeRelease(kv.second);
    bmpCache.clear();
    SafeRelease(brush);
    SafeRelease(rt);
    if (memDC) { DeleteDC(memDC); memDC = nullptr; }
    if (dib) { DeleteObject(dib); dib = nullptr; }
}

void Surface::Render() {
    if (!hwnd || width <= 0 || height <= 0) return;
    MakeTarget();
    if (!rt) return;
    if (kind == Layered) {
        RECT rc = {0, 0, width, height};
        ((ID2D1DCRenderTarget *)rt)->BindDC(memDC, &rc);
    }
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    OnPaint();
    HRESULT hr = rt->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) { DropTarget(); return; }
    if (kind == Layered) {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        POINT dst = {wr.left, wr.top}, src = {0, 0};
        SIZE sz = {width, height};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, nullptr, &dst, &sz, memDC, &src, 0, &bf, ULW_ALPHA);
    }
}

// drawing helpers (coordinates in DIPs)
void Surface::Fill(D2D1_RECT_F r, D2D1_COLOR_F c, float radius) {
    brush->SetColor(c);
    if (radius > 0) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush);
    else rt->FillRectangle(r, brush);
}
void Surface::Stroke(D2D1_RECT_F r, D2D1_COLOR_F c, float radius, float w) {
    brush->SetColor(c);
    if (radius > 0) rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush, w);
    else rt->DrawRectangle(r, brush, w);
}
void Surface::Text(const std::wstring &s, D2D1_RECT_F r, D2D1_COLOR_F c, float size, DWRITE_FONT_WEIGHT w,
                   DWRITE_TEXT_ALIGNMENT a, bool icon) {
    if (s.empty()) return;
    brush->SetColor(c);
    rt->DrawTextW(s.c_str(), (UINT32)s.size(), Font(size, w, a, icon), r, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Surface::TextWrap(const std::wstring &s, D2D1_RECT_F r, D2D1_COLOR_F c, float size) {
    if (s.empty()) return;
    IDWriteTextLayout *l = nullptr;
    if (FAILED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), Font(size, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false),
                                      std::max(1.f, r.right - r.left), std::max(1.f, r.bottom - r.top), &l)))
        return;
    l->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    brush->SetColor(c);
    rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), l, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    l->Release();
}
void Surface::Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float w) {
    brush->SetColor(c);
    rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, w);
}
void Surface::Circle(float cx, float cy, float r, D2D1_COLOR_F c, bool fill, float w) {
    brush->SetColor(c);
    D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
    if (fill) rt->FillEllipse(e, brush);
    else rt->DrawEllipse(e, brush, w);
}
// Draw an image. Linear filtering alone aliases badly when shrinking a large
// icon 2-3x (jagged, "rough" edges), so the source is first downscaled with
// WIC's high-quality cubic filter to just above the on-screen pixel size, rounded
// up to a 16px bucket (so magnification reuses a few cached sizes instead of
// one per frame). Linear then only covers the last <=16px step, which is clean.
void Surface::Bitmap(IWICBitmapSource *src, D2D1_RECT_F r, float opacity) {
    if (!src) return;
    UINT sw = 0, sh = 0;
    src->GetSize(&sw, &sh);
    float dw = (r.right - r.left) * scale, dh = (r.bottom - r.top) * scale;
    int bucket = 0;   // 0 = use the source as-is
    if (sw && sh && dw > 0 && dh > 0) {
        float big = std::max(dw, dh);
        int b16 = ((int)ceilf(big) + 15) / 16 * 16;
        int whole = (int)lroundf(big);
        if (fabsf(big - whole) < 0.05f && whole < (int)std::max(sw, sh)) {
            // at rest: scale straight to the exact pixel size and land on whole
            // pixels, so the final linear step copies 1:1 and adds no blur
            bucket = whole;
            float l = roundf(r.left * scale) / scale, t = roundf(r.top * scale) / scale;
            r = D2D1::RectF(l, t, l + (r.right - r.left), t + (r.bottom - r.top));
        } else if (b16 < (int)std::max(sw, sh)) bucket = b16;
    }
    auto key = std::make_pair(src, bucket);
    ID2D1Bitmap *b = nullptr;
    auto it = bmpCache.find(key);
    if (it != bmpCache.end()) b = it->second;
    else {
        if (bucket) {
            float f = (float)bucket / (float)std::max(sw, sh);
            UINT tw = std::max(1u, (UINT)(sw * f + 0.5f)), th = std::max(1u, (UINT)(sh * f + 0.5f));
            IWICBitmapScaler *sc = nullptr;
            if (SUCCEEDED(g_wic->CreateBitmapScaler(&sc)) &&
                SUCCEEDED(sc->Initialize(src, tw, th, (WICBitmapInterpolationMode)4 /* HighQualityCubic */)))
                rt->CreateBitmapFromWicBitmap(sc, nullptr, &b);
            SafeRelease(sc);
        }
        if (!b) rt->CreateBitmapFromWicBitmap(src, nullptr, &b);
        bmpCache[key] = b;
    }
    if (b) rt->DrawBitmap(b, r, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

LRESULT CALLBACK Surface::Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
        return DefWindowProcW(h, m, wp, lp);
    }
    Surface *s = (Surface *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!s) return DefWindowProcW(h, m, wp, lp);
    bool handled = false;
    LRESULT r = s->OnMessage(m, wp, lp, handled);
    if (handled) return r;
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (s->kind == Glass) s->Render();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (s->kind == Glass && s->rt) {
            s->width = LOWORD(lp);
            s->height = HIWORD(lp);
            ((ID2D1HwndRenderTarget *)s->rt)->Resize(D2D1::SizeU(s->width, s->height));
        }
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lp;
        s->Move(*r);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!s->tracking) {
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            s->tracking = true;
        }
        // fall through
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_LBUTTONDBLCLK: case WM_MBUTTONUP:
        s->OnMouse(m, GET_X_LPARAM(lp) / s->scale, GET_Y_LPARAM(lp) / s->scale, wp);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        s->OnMouse(m, p.x / s->scale, p.y / s->scale, wp);
        return 0;
    }
    case WM_MOUSELEAVE:
        s->tracking = false;
        s->OnMouseLeave();
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        s->OnKey((UINT)wp);
        return 0;
    case WM_ACTIVATE:
        s->OnActivate(LOWORD(wp) != WA_INACTIVE);
        return 0;
    case WM_MOUSEACTIVATE:
        if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_NOACTIVATE) return MA_NOACTIVATE;
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// The Downloads stack, like macOS.
//  - Fan: the newest files arc up out of the Dock in a curved column that leans
//    right as it rises; each has its icon and a name pill on the left, with
//    "Open in Explorer" at the top. Drawn on a per-pixel-alpha (layered) window,
//    so everything but the icons and pills is see-through and click-through.
//  - Grid: a glass panel of tiles with a header.
//  - Automatic: Fan for a few files, Grid for many.
// Right-click Downloads in the Dock to switch; also in Settings -> Dock.
#include "common.h"

static bool IsPreviewExt(const std::wstring &p) {
    std::wstring e = Lower(p);
    size_t d = e.rfind(L'.');
    if (d == std::wstring::npos) return false;
    e = e.substr(d + 1);
    static const wchar_t *media[] = {L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"webp", L"heic", L"tif", L"tiff",
                                     L"mp4", L"mov", L"mkv", L"avi", L"webm", L"pdf", L"svg"};
    for (auto m : media) if (e == m) return true;
    return false;
}

// a preview for images/video/PDF; otherwise the file's real icon, with its alpha
// (no white matte behind exe / zip / folder icons)
static IWICBitmapSource *FileThumb(const std::wstring &path, int size) {
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    IShellItemImageFactory *fac = nullptr;
    HBITMAP hb = nullptr;
    if (SUCCEEDED(item->QueryInterface(&fac))) {
        SIZE s = {size, size};
        SIIGBF flags = IsPreviewExt(path) ? (SIIGBF)0x0 : (SIIGBF)(0x1 | 0x4);   // ICONONLY | BIGGERSIZEOK
        if (FAILED(fac->GetImage(s, flags, &hb)) || !hb) fac->GetImage(s, (SIIGBF)0x0, &hb);
        fac->Release();
    }
    item->Release();
    IWICBitmapSource *src = WicFromHBitmap(hb);
    if (hb) DeleteObject(hb);
    return src;
}

static ULONGLONG g_fanClosedAt = 0;   // see DownloadsFanDismiss

struct Fan : Surface {
    struct Tile {
        std::wstring path, name;
        IWICBitmapSource *icon = nullptr;
        D2D1_POINT_2F pos = {};             // grid: tile centre
        D2D1_RECT_F hitIcon = {}, hitLabel = {};
    };
    std::vector<Tile> tiles;
    bool fanMode = true;
    int hot = -1;                 // tile index, -2 = "Open in Explorer"
    float t = 0;                  // 0..1 open animation
    UINT_PTR anim = 0;
    // fan geometry (DIPs)
    float baseX = 0, step = 66, iconS = 56;
    // grid geometry
    float tileS = 64, colW = 98;
    D2D1_RECT_F openHit = {};

    ~Fan() override { for (auto &tl : tiles) if (tl.icon) tl.icon->Release(); }
    void OnPaint() override { if (fanMode) PaintFan(); else PaintGrid(); }
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnMouseLeave() override { if (hot != -1) { hot = -1; Invalidate(); } }
    void OnActivate(bool a) override { if (!a) Close(); }
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Close(); }
    void Close() {
        if (anim) { KillTimer(nullptr, anim); anim = 0; }
        if (hwnd) g_fanClosedAt = GetTickCount64();
        Destroy();
    }
    void PaintFan();
    void PaintGrid();
    int HitAt(float x, float y);
    static float Dx(int i) { return 2.2f * i + 1.1f * i * i; }   // rightward curve as it rises
};

static Fan *g_fan = nullptr;
bool DownloadsFanOpen() { return g_fan && g_fan->hwnd && IsWindowVisible(g_fan->hwnd); }

// The Dock's Downloads button is a toggle: a click while the stack is open closes
// it. If that same click already closed it (by taking focus away), don't reopen.
bool DownloadsFanDismiss() {
    if (DownloadsFanOpen()) { g_fan->Close(); return true; }
    return GetTickCount64() - g_fanClosedAt < 300;
}

static float Ease(float p) { p = std::max(0.f, std::min(1.f, p)); return 1 - (1 - p) * (1 - p) * (1 - p); }

void Fan::PaintFan() {
    float h = H(), hs = iconS / 2;
    int n = (int)tiles.size();
    D2D1_COLOR_F white = Rgb(0xFFFFFF);
    for (int i = 0; i <= n; i++) {          // i == n is the "Open in Explorer" button on top
        float e = Ease(t * 1.6f - i * 0.06f);
        D2D1_RECT_F none = D2D1::RectF(0, 0, 0, 0);
        if (e <= 0.01f) { if (i < n) tiles[i].hitIcon = tiles[i].hitLabel = none; else openHit = none; continue; }
        float tcy = h - 14 - hs - i * step;
        float y = tcy + (1 - e) * (h - tcy);   // rises out of the Dock
        float x = baseX + Dx(i) * e;
        bool isHot = (i < n) ? (hot == i) : (hot == -2);
        std::wstring label;
        if (i < n) {
            auto &tl = tiles[i];
            // icon, leaning slightly more the higher it sits
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(1.6f * i * e, D2D1::Point2F(x, y)));
            if (isHot) Fill(D2D1::RectF(x - hs - 5, y - hs - 5, x + hs + 5, y + hs + 5), WithAlpha(white, 0.22f * e), 12);
            if (tl.icon) Bitmap(tl.icon, D2D1::RectF(x - hs, y - hs, x + hs, y + hs), e);
            else Text(L"", D2D1::RectF(x - hs, y - hs, x + hs, y + hs), WithAlpha(white, e), 34,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
            rt->SetTransform(D2D1::Matrix3x2F::Identity());
            tl.hitIcon = D2D1::RectF(x - hs, y - hs, x + hs, y + hs);
            label = tl.name;
        } else {
            Circle(x, y, 22, isHot ? WithAlpha(g_pal.accent, e) : Rgb(0x1C1C1E, 0.85f * e), true);
            Text(L"", D2D1::RectF(x - 22, y - 22, x + 22, y + 22), WithAlpha(white, e), 16,
                 DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
            label = L"Open in Explorer";
        }
        // name pill to the left of the icon
        if (label.size() > 28) label = label.substr(0, 27) + L"…";
        float tw = std::min(200.f, TextWidth(label, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM));
        float pr = x - hs - 10, pl = pr - tw - 22;
        D2D1_RECT_F pill = D2D1::RectF(pl, y - 12, pr, y + 12);
        Fill(pill, isHot ? WithAlpha(g_pal.accent, 0.95f * e) : Rgb(0x1C1C1E, 0.82f * e), 12);
        Text(label, D2D1::RectF(pl + 11, y - 12, pr - 11, y + 12), WithAlpha(white, e), 12.5f,
             DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (i < n) tiles[i].hitLabel = pill;
        else openHit = D2D1::RectF(pl, y - 22, x + 22, y + 22);
    }
}

void Fan::PaintGrid() {
    float w = W(), h = H(), a = t;
    Fill(D2D1::RectF(0, 0, w, h), g_pal.dark ? Rgb(0x1C1C1E, 0.45f) : Rgb(0xF2F2F5, 0.45f), 0);
    Text(L"Downloads", D2D1::RectF(18, 8, w - 170, 36), WithAlpha(g_pal.text, a), 15, DWRITE_FONT_WEIGHT_BOLD);
    openHit = D2D1::RectF(w - 170, 8, w - 16, 36);
    Text(L"Open in Explorer ›", openHit, WithAlpha(hot == -2 ? g_pal.accent : g_pal.textDim, a), 12.5f,
         DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_TRAILING);
    Line(14, 42, w - 14, 42, g_pal.separator);
    float s = tileS * (0.85f + 0.15f * a);
    for (int i = 0; i < (int)tiles.size(); i++) {
        auto &tl = tiles[i];
        D2D1_RECT_F cell = D2D1::RectF(tl.pos.x - colW / 2 + 4, tl.pos.y - tileS / 2 - 6,
                                       tl.pos.x + colW / 2 - 4, tl.pos.y + tileS / 2 + 26);
        if (i == hot) Fill(cell, g_pal.hover, 10);
        D2D1_RECT_F ir = D2D1::RectF(tl.pos.x - s / 2, tl.pos.y - s / 2, tl.pos.x + s / 2, tl.pos.y + s / 2);
        if (tl.icon) Bitmap(tl.icon, ir, a);
        else Text(L"", ir, WithAlpha(g_pal.textDim, a), 32, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
        Text(tl.name, D2D1::RectF(cell.left + 2, tl.pos.y + tileS / 2 + 4, cell.right - 2, tl.pos.y + tileS / 2 + 22),
             WithAlpha(g_pal.text, a), 11, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        tl.hitIcon = tl.hitLabel = cell;
    }
}

int Fan::HitAt(float x, float y) {
    auto in = [&](D2D1_RECT_F r) { return r.right > r.left && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    for (int i = (int)tiles.size() - 1; i >= 0; i--)
        if (in(tiles[i].hitIcon) || in(tiles[i].hitLabel)) return i;
    if (in(openHit)) return -2;
    return -1;
}

void Fan::OnMouse(UINT msg, float x, float y, WPARAM) {
    int h = HitAt(x, y);
    if (h != hot) { hot = h; Invalidate(); }
    if (msg == WM_LBUTTONUP) {
        if (h >= 0) ShellOpen(tiles[h].path);
        else if (h == -2) ShellOpen(KnownFolder(FOLDERID_Downloads));
        Close();
    }
}

void CALLBACK FanAnim(HWND, UINT, UINT_PTR id, DWORD) {
    if (!g_fan || !g_fan->hwnd) { KillTimer(nullptr, id); if (g_fan) g_fan->anim = 0; return; }
    g_fan->t += 0.09f;
    if (g_fan->t >= 1) { g_fan->t = 1; KillTimer(nullptr, id); g_fan->anim = 0; }
    g_fan->Invalidate();
}

void DownloadsFanShow(RECT anchor, const std::vector<std::wstring> &files) {
    if (g_fan) { g_fan->Close(); delete g_fan; g_fan = nullptr; }
    if (files.empty()) { ShellOpen(KnownFolder(FOLDERID_Downloads)); return; }
    HMONITOR mon = MonitorFromPoint(POINT{anchor.left, anchor.top}, MONITOR_DEFAULTTONEAREST);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    bool fan = g_set.dlStyle == 0 || (g_set.dlStyle == 2 && files.size() <= 8);
    g_fan = new Fan();
    g_fan->fanMode = fan;
    int bottom = anchor.top - (int)(4 * sc);
    RECT r;
    int n;
    if (fan) {
        Fan *f = g_fan;
        // as many as fit above the Dock (max 9), plus the Open button
        float avail = (bottom - mi.rcWork.top) / sc - 20;
        n = std::min<int>((int)files.size(), 9);
        while (n > 1 && (n + 1) * f->step + 24 > avail) n--;
        f->baseX = 250 + 10 + f->iconS / 2;
        float wd = f->baseX + f->iconS / 2 + Fan::Dx(n) + 30, hd = (n + 1) * f->step + 24;
        int pw = (int)(wd * sc), ph = (int)(hd * sc);
        int left = anchor.left - (int)(f->baseX * sc);   // bottom icon centred on the Downloads icon
        r = {left, bottom - ph, left + pw, bottom};
    } else {
        n = (int)files.size();
        int cols = std::min(n, 6), rows = (n + cols - 1) / cols;
        float wd = cols * g_fan->colW + 30, hd = 44 + rows * (g_fan->tileS + 40) + 16;
        int pw = (int)(wd * sc), ph = (int)(hd * sc);
        r = {anchor.left - pw / 2, bottom - ph, anchor.left + pw / 2, bottom};
    }
    if (r.right > mi.rcWork.right) { r.left -= r.right - mi.rcWork.right; r.right = mi.rcWork.right; }
    if (r.left < mi.rcWork.left) { r.right += mi.rcWork.left - r.left; r.left = mi.rcWork.left; }
    if (r.top < mi.rcWork.top) r.top = mi.rcWork.top;
    if (fan) g_fan->Create(Surface::Layered, L"GoldenGateFan", WS_EX_TOPMOST, r, nullptr, false);
    else g_fan->Create(Surface::Glass, L"GoldenGateFan", WS_EX_TOPMOST, r, nullptr, true);
    for (int i = 0; i < n; i++) {
        Fan::Tile tl;
        tl.path = files[i];
        tl.name = FileNameOf(files[i]);
        tl.icon = FileThumb(files[i], 128);
        if (!fan) {   // newest first, left to right, top to bottom
            int col = i % std::min(n, 6), row = i / std::min(n, 6);
            tl.pos = D2D1::Point2F(15 + g_fan->colW * col + g_fan->colW / 2, 44 + 10 + g_fan->tileS / 2 + row * (g_fan->tileS + 40));
        }
        g_fan->tiles.push_back(tl);
    }
    g_fan->Show(true, true);
    SetForegroundWindow(g_fan->hwnd);
    g_fan->anim = SetTimer(nullptr, 0, 16, FanAnim);
}

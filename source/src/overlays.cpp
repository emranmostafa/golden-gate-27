// Mission Control (window overview with live DWM thumbnails), the cover-flow
// Alt+Tab switcher, Launchpad (full-screen app grid), and Spotlight (search).
// All use documented APIs: DwmRegisterThumbnail for live previews, a low-level
// keyboard hook for the switcher, shell:AppsFolder for the app grid.
#include "common.h"
#include <dwmapi.h>

void ScreenToClientRect(HWND h, RECT &r);
void SwitcherAction(int what, bool shift);

// ================================================================ Mission ==
struct MCTile { HWND src; RECT target; RECT cur; HTHUMBNAIL thumb; SIZE srcSize; std::wstring title; };

struct MissionControl : Surface {
    std::vector<MCTile> tiles;
    int hot = -1;
    float t = 0;
    UINT_PTR anim = 0;

    void Build();
    void Layout();
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Close(); }
    void OnActivate(bool a) override { if (!a) Close(); }
    void Close();
    bool closing = false;   // hiding us re-enters Close via OnActivate
    void UpdateThumbs();
};
static MissionControl *g_mc = nullptr;

void MissionControl::Build() {
    for (auto &t : tiles) if (t.thumb) DwmUnregisterThumbnail(t.thumb);
    tiles.clear();
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    for (auto &w : ListAppWindows()) {
        if (w.hwnd == hwnd) continue;
        MCTile t = {};
        t.src = w.hwnd;
        t.title = w.title;
        HTHUMBNAIL th = nullptr;
        if (SUCCEEDED(DwmRegisterThumbnail(hwnd, w.hwnd, &th))) {
            t.thumb = th;
            SIZE sz = {0, 0};
            DwmQueryThumbnailSourceSize(th, &sz);
            t.srcSize = sz;
        }
        RECT wr;
        GetWindowRect(w.hwnd, &wr);
        t.cur = wr;   // start from the real position for a zoom-in feel
        ScreenToClientRect(hwnd, t.cur);
        (void)mon;
        tiles.push_back(t);
    }
    Layout();
}

void ScreenToClientRect(HWND h, RECT &r) {
    POINT a = {r.left, r.top}, b = {r.right, r.bottom};
    ScreenToClient(h, &a);
    ScreenToClient(h, &b);
    r = {a.x, a.y, b.x, b.y};
}

void MissionControl::Layout() {
    int n = (int)tiles.size();
    if (n == 0) return;
    int cols = (int)ceilf(sqrtf((float)n));
    int rows = (n + cols - 1) / cols;
    float pad = 40 * scale, gap = 24 * scale;
    float topPad = 60 * scale;
    float cw = (width - pad * 2 - gap * (cols - 1)) / cols;
    float ch = (height - topPad - pad - gap * (rows - 1)) / rows;
    for (int i = 0; i < n; i++) {
        int c = i % cols, r = i / cols;
        float cellX = pad + c * (cw + gap), cellY = topPad + r * (ch + gap);
        // keep aspect of the source window
        float aw = tiles[i].srcSize.cx ? (float)tiles[i].srcSize.cx : 16;
        float ah = tiles[i].srcSize.cy ? (float)tiles[i].srcSize.cy : 9;
        float scale2 = std::min(cw / aw, ch / ah);
        float tw = aw * scale2, th = ah * scale2;
        RECT tgt = {(LONG)(cellX + (cw - tw) / 2), (LONG)(cellY + (ch - th) / 2), 0, 0};
        tgt.right = tgt.left + (LONG)tw;
        tgt.bottom = tgt.top + (LONG)th;
        tiles[i].target = tgt;
    }
}

void MissionControl::UpdateThumbs() {
    for (int i = 0; i < (int)tiles.size(); i++) {
        auto &tl = tiles[i];
        if (!tl.thumb) continue;
        RECT d;
        d.left = (LONG)(tl.cur.left + (tl.target.left - tl.cur.left) * t);
        d.top = (LONG)(tl.cur.top + (tl.target.top - tl.cur.top) * t);
        d.right = (LONG)(tl.cur.right + (tl.target.right - tl.cur.right) * t);
        d.bottom = (LONG)(tl.cur.bottom + (tl.target.bottom - tl.cur.bottom) * t);
        DWM_THUMBNAIL_PROPERTIES p = {};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
        p.rcDestination = d;
        p.opacity = (BYTE)(255 * std::min(1.f, t * 1.5f));
        p.fVisible = TRUE;
        DwmUpdateThumbnailProperties(tl.thumb, &p);
    }
}

void MissionControl::OnPaint() {
    Fill(D2D1::RectF(0, 0, W(), H()), WithAlpha(g_pal.dark ? Rgb(0x0A0A0F) : Rgb(0x1a1a22), 0.55f * t), 0);
    Text(L"Mission Control", D2D1::RectF(0, 16, W(), 44), WithAlpha(Rgb(0xFFFFFF), 0.85f * t), 15,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    // highlight + titles drawn over the DWM thumbnails (which the compositor draws)
    for (int i = 0; i < (int)tiles.size(); i++) {
        RECT d = tiles[i].target;
        D2D1_RECT_F r = D2D1::RectF(d.left / scale, d.top / scale, d.right / scale, d.bottom / scale);
        if (i == hot) Stroke(D2D1::RectF(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4), WithAlpha(g_pal.accent, t), 10, 3);
    }
}

void MissionControl::OnMouse(UINT msg, float x, float y, WPARAM) {
    int h = -1;
    for (int i = 0; i < (int)tiles.size(); i++) {
        RECT d = tiles[i].target;
        if (x * scale >= d.left && x * scale <= d.right && y * scale >= d.top && y * scale <= d.bottom) { h = i; break; }
    }
    if (h != hot) { hot = h; Invalidate(); }
    if (msg == WM_LBUTTONUP && h >= 0) {
        HWND w = tiles[h].src;
        Close();
        ActivateWindow(w);
    } else if (msg == WM_LBUTTONUP) Close();
}

void CALLBACK MCAnim(HWND, UINT, UINT_PTR id, DWORD) {
    if (!g_mc) { KillTimer(nullptr, id); return; }
    g_mc->t += 0.14f;
    if (g_mc->t >= 1) { g_mc->t = 1; KillTimer(nullptr, id); g_mc->anim = 0; }
    g_mc->UpdateThumbs();
    g_mc->Invalidate();
}

void MissionControl::Close() {
    if (closing) return;
    closing = true;
    for (auto &t : tiles) if (t.thumb) DwmUnregisterThumbnail(t.thumb);
    tiles.clear();
    if (anim) { KillTimer(nullptr, anim); anim = 0; }
    Show(false);
    Destroy();
    g_mc = nullptr;
    delete this;
}

void MissionControlToggle() {
    if (g_mc) { g_mc->Close(); return; }
    g_mc = new MissionControl();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    RECT r = mi.rcMonitor;
    g_mc->Create(Surface::Glass, L"GoldenGateMissionControl", WS_EX_TOPMOST, r, nullptr, false);
    g_mc->t = 0;
    g_mc->Build();
    g_mc->Show(true, true);
    SetForegroundWindow(g_mc->hwnd);
    g_mc->anim = SetTimer(nullptr, 0, 16, MCAnim);
}

// ================================================================ switcher ==
// Cover-flow Alt+Tab: a low-level keyboard hook swallows Alt+Tab and shows our
// own switcher. The hook must answer within ~1s or Windows drops it, so it only
// posts to the main thread.
struct SwTile { HWND hwnd; IWICBitmapSource *icon; std::wstring title; };
struct Switcher : Surface {
    std::vector<SwTile> tiles;
    int sel = 0;
    void Build();
    void OnPaint() override;
    void Commit();
    void Cancel();
};
static Switcher *g_sw = nullptr;
static HHOOK g_kb = nullptr;
static bool g_altDown = false;

void Switcher::Build() {
    tiles.clear();
    // most-recently-used order: Z-order already gives that
    for (auto &w : ListAppWindows()) {
        SwTile t;
        t.hwnd = w.hwnd;
        t.title = w.title;
        HWND real = RealAppWindow(w.hwnd);
        t.icon = AppIcon(w.exe.empty() ? w.aumid : w.exe, 128);
        if (!t.icon) t.icon = IconForExe(w.exe, WindowAumid(real), 128);
        if (!t.icon) {
            HICON hi = (HICON)SendMessageW(w.hwnd, WM_GETICON, ICON_BIG, 0);
            if (!hi) hi = (HICON)GetClassLongPtrW(w.hwnd, GCLP_HICON);
            if (hi) t.icon = WicFromHIcon(hi);
        }
        tiles.push_back(t);
    }
    sel = tiles.size() > 1 ? 1 : 0;
}

void Switcher::OnPaint() {
    Fill(D2D1::RectF(0, 0, W(), H()), g_pal.panel, 22);
    Fill(D2D1::RectF(0, 0, W(), H()), WithAlpha(g_pal.panelStrong, 0.25f), 22);
    int n = (int)tiles.size();
    if (n == 0) return;
    float icon = 96, gap = 22, cx = W() / 2;
    // cover-flow: selected big in centre, neighbours smaller to the sides
    for (int off = -(int)n; off <= (int)n; off++) {
        int i = sel + off;
        if (i < 0 || i >= n) continue;
        float dist = fabsf((float)off);
        float sc = std::max(0.5f, 1.f - dist * 0.18f);
        float x = cx + off * (icon * 0.72f + gap);
        if (x < -icon || x > W() + icon) continue;
        float s = icon * sc;
        float y = H() / 2 - s / 2 - 10;
        if (off == 0) {
            Fill(D2D1::RectF(x - s / 2 - 10, y - 10, x + s / 2 + 10, y + s + 10), g_pal.hover, 16);
        }
        if (tiles[i].icon) Bitmap(tiles[i].icon, D2D1::RectF(x - s / 2, y, x + s / 2, y + s), off == 0 ? 1.f : 0.55f);
    }
    std::wstring t = tiles[sel].title;
    if (t.size() > 60) t = t.substr(0, 59) + L"…";
    Text(t, D2D1::RectF(20, H() - 44, W() - 20, H() - 16), g_pal.text, 14, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Switcher::Commit() {
    HWND w = (sel >= 0 && sel < (int)tiles.size()) ? tiles[sel].hwnd : nullptr;
    Cancel();
    if (w) ActivateWindow(w);
}
void Switcher::Cancel() {
    Show(false);
    Destroy();
    g_sw = nullptr;
    delete this;
}

static void OpenSwitcher(bool shift) {
    if (!g_sw) {
        g_sw = new Switcher();
        HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(mon, &mi);
        float sc = DpiScaleForMonitor(mon);
        int w = (int)std::min((float)(mi.rcWork.right - mi.rcWork.left) - 80 * sc, 900.f * sc);
        int h = (int)(260 * sc);
        int cx = (mi.rcMonitor.left + mi.rcMonitor.right) / 2, cy = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
        RECT r = {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
        g_sw->Create(Surface::Glass, L"GoldenGateSwitcher", WS_EX_TOPMOST | WS_EX_NOACTIVATE, r, nullptr, true);
        g_sw->Build();
        g_sw->Show(true, false);
    } else {
        int n = (int)g_sw->tiles.size();
        if (n) g_sw->sel = (g_sw->sel + (shift ? -1 : 1) + n) % n;
    }
    g_sw->Invalidate();
}

static LRESULT CALLBACK KbHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        bool up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
        if (k->vkCode == VK_LMENU || k->vkCode == VK_RMENU) {
            if (up && g_altDown) {
                g_altDown = false;
                if (g_sw) PostMessageW(g_main, WM_APP + 60, 1, 0);   // commit
            }
        }
        if (k->vkCode == VK_TAB && down && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            g_altDown = true;
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            PostMessageW(g_main, WM_APP + 60, 0, shift ? 1 : 0);     // step
            return 1;   // swallow so Windows' own switcher doesn't show
        }
        if (k->vkCode == VK_ESCAPE && down && g_sw) {
            PostMessageW(g_main, WM_APP + 60, 2, 0);                 // cancel
            return 1;
        }
    }
    return CallNextHookEx(g_kb, code, wp, lp);
}

void SwitcherStep(bool start) {   // called by main for WM_APP+60 and to install the hook
    if (start) {
        if (!g_kb && g_set.switcher) g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, KbHook, g_inst, 0);
    } else {
        if (g_kb) { UnhookWindowsHookEx(g_kb); g_kb = nullptr; }
    }
}

// handle the posted switcher actions on the main thread
void SwitcherAction(int what, bool shift) {
    switch (what) {
    case 0: OpenSwitcher(shift); break;
    case 1: if (g_sw) g_sw->Commit(); break;
    case 2: if (g_sw) g_sw->Cancel(); break;
    }
}

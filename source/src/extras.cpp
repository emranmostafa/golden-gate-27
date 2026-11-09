// Clipboard history, Caffeine (keep-awake), shake-to-find-cursor and hot
// corners. All documented APIs. The clipboard history is kept by our own
// process (the WinRT history API only works for the focused app), via a
// clipboard-format listener.
#include "common.h"

// ================================================================ Caffeine =
static bool g_caffeine = false;
void CaffeineSet(bool on) {
    g_caffeine = on;
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
}

// =============================================================== Clipboard =
struct ClipEntry { std::wstring text; bool isText; };
static std::vector<ClipEntry> g_clips;
static bool g_ownClipChange = false;

void ClipboardOnChange() {
    if (g_ownClipChange) { g_ownClipChange = false; return; }
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    if (!OpenClipboard(g_main)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t *p = (wchar_t *)GlobalLock(h);
        if (p) {
            std::wstring t = p;
            GlobalUnlock(h);
            if (!t.empty()) {
                g_clips.erase(std::remove_if(g_clips.begin(), g_clips.end(),
                              [&](const ClipEntry &e) { return e.text == t; }), g_clips.end());
                g_clips.insert(g_clips.begin(), {t, true});
                if (g_clips.size() > 40) g_clips.resize(40);
            }
        }
    }
    CloseClipboard();
}

void ClipboardStart() {
    if (g_set.clipboard) AddClipboardFormatListener(g_main);
}
void ClipboardStop() {
    RemoveClipboardFormatListener(g_main);
}

static void PutClipboard(const std::wstring &t) {
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    if (g) {
        memcpy(GlobalLock(g), t.c_str(), (t.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        g_ownClipChange = true;
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

struct ClipWin : Surface {
    int hot = -1;
    float scroll = 0;
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    void OnActivate(bool a) override { if (!a) Show(false); }
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Show(false); }
};
static ClipWin *g_clipWin = nullptr;

void ClipWin::OnPaint() {
    float w = W(), h = H();
    Fill(D2D1::RectF(0, 0, w, h), g_pal.panel, 16);
    Fill(D2D1::RectF(0, 0, w, h), WithAlpha(g_pal.panelStrong, 0.3f), 16);
    Text(L"Clipboard", D2D1::RectF(16, 10, w - 16, 34), g_pal.textDim, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float y = 40 - scroll;
    for (int i = 0; i < (int)g_clips.size(); i++) {
        float rowH = 46;
        if (y + rowH > 36 && y < h) {
            if (i == hot) Fill(D2D1::RectF(8, y, w - 8, y + rowH - 4), g_pal.hover, 9);
            std::wstring t = g_clips[i].text;
            for (auto &c : t) if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
            if (t.size() > 90) t = t.substr(0, 89) + L"…";
            Text(t, D2D1::RectF(16, y + 4, w - 16, y + rowH - 8), g_pal.text, 12.5f);
        }
        y += 46;
    }
    if (g_clips.empty())
        Text(L"Nothing copied yet", D2D1::RectF(16, 50, w - 16, 80), g_pal.textFaint, 12.5f);
}

void ClipWin::OnMouse(UINT msg, float x, float y, WPARAM wp) {
    if (msg == WM_MOUSEWHEEL) {
        scroll = std::max(0.f, scroll - GET_WHEEL_DELTA_WPARAM(wp) / 4.f);
        Invalidate();
        return;
    }
    int i = (int)((y - 40 + scroll) / 46);
    if (i >= 0 && i < (int)g_clips.size()) {
        if (i != hot) { hot = i; Invalidate(); }
        if (msg == WM_LBUTTONUP) { PutClipboard(g_clips[i].text); Show(false); }
    }
}

void ClipboardShow(RECT anchor) {
    if (g_clipWin && g_clipWin->Visible()) { g_clipWin->Show(false); return; }
    if (!g_clipWin) g_clipWin = new ClipWin();
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    int w = (int)(340 * sc), h = (int)(420 * sc);
    int x = anchor.left - w / 2;
    if (x + w > mi.rcWork.right - 6) x = mi.rcWork.right - 6 - w;
    if (x < mi.rcWork.left + 6) x = mi.rcWork.left + 6;
    RECT r = {x, anchor.bottom + (int)(4 * sc), x + w, anchor.bottom + (int)(4 * sc) + h};
    if (!g_clipWin->hwnd) g_clipWin->Create(Surface::Glass, L"GoldenGateClipboard", WS_EX_TOPMOST, r, nullptr, true);
    else g_clipWin->Move(r);
    g_clipWin->scroll = 0;
    g_clipWin->Show(true, true);
    SetForegroundWindow(g_clipWin->hwnd);
    g_clipWin->Invalidate();
}

// ====================================================== shake to find =====
// Grow a ring around the pointer when the mouse is shaken quickly back and
// forth. A layered overlay that fades out.
struct FindCursor : Surface {
    float t = 1;        // 1 = fully shown, fades to 0
    POINT at = {};
    void OnPaint() override {
        float cx = W() / 2, cy = H() / 2;
        float r = 20 + 60 * (1 - t);
        Circle(cx, cy, r, WithAlpha(g_pal.accent, 0.25f * t), true);
        Circle(cx, cy, r, WithAlpha(g_pal.accent, 0.7f * t), false, 3);
    }
};
static FindCursor *g_find = nullptr;
static UINT_PTR g_findTimer = 0;

static void ShowFindCursor() {
    POINT p; GetCursorPos(&p);
    if (!g_find) g_find = new FindCursor();
    int s = 220;
    RECT r = {p.x - s / 2, p.y - s / 2, p.x + s / 2, p.y + s / 2};
    if (!g_find->hwnd)
        g_find->Create(Surface::Layered, L"GoldenGateFind", WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, r, nullptr);
    else g_find->Move(r);
    g_find->t = 1;
    g_find->Show(true, false);
    SetWindowPos(g_find->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (g_findTimer) KillTimer(nullptr, g_findTimer);
    g_findTimer = SetTimer(nullptr, 0, 16, [](HWND, UINT, UINT_PTR id, DWORD) {
        if (!g_find) { KillTimer(nullptr, id); return; }
        g_find->t -= 0.05f;
        if (g_find->t <= 0) { g_find->Show(false); KillTimer(nullptr, id); g_findTimer = 0; }
        else {
            POINT p; GetCursorPos(&p);
            int s = g_find->width;
            g_find->Move(RECT{p.x - s / 2, p.y - s / 2, p.x + s / 2, p.y + s / 2});
        }
    });
}

// shake detection: watch direction reversals in a short window
static void ShakeTick() {
    static POINT last = {};
    static int lastDir = 0;
    static int reversals = 0;
    static DWORD windowStart = 0;
    POINT p; GetCursorPos(&p);
    int dx = p.x - last.x;
    last = p;
    if (abs(dx) < 20) return;   // per 33ms sample: only quick moves, not normal pointing
    int dir = dx > 0 ? 1 : -1;
    DWORD now = GetTickCount();
    if (dir != lastDir && lastDir != 0) {
        if (now - windowStart > 800) { reversals = 0; windowStart = now; }
        reversals++;
        if (reversals >= 4) {
            reversals = 0;
            ShowFindCursor();
        }
    }
    lastDir = dir;
    if (windowStart == 0) windowStart = now;
}

// ============================================================ hot corners ==
static DWORD g_cornerSince = 0;
void HotCornersTick() {
    if (!g_set.hotCorner) return;
    POINT p; GetCursorPos(&p);
    HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    bool topLeft = (p.x <= mi.rcMonitor.left + 3 && p.y <= mi.rcMonitor.top + 3);
    if (topLeft) {
        if (g_cornerSince == 0) g_cornerSince = GetTickCount();
        else if (GetTickCount() - g_cornerSince > 120) {
            g_cornerSince = 0;
            MissionControlToggle();
        }
    } else {
        g_cornerSince = 0;
    }
}

void ShakeTimerTick() {
    if (g_set.shakeFind) ShakeTick();
}

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

// ============================================================ pointer grow ==
// Shake to grow, ported from KDE Plasma's Shake Cursor effect (KWin
// src/plugins/shakecursor, GPL-2.0-or-later), which is where the Linux behaviour
// comes from: Bibata itself only supplies the artwork.
//  - detection: within the last 1000ms, the pointer's path is more than 4x the
//    diagonal of the box it covered, and that box is at least 100px across; no
//    mouse button held
//  - the first shake grows the pointer to 3x, each further shake adds 1x
//  - every change animates over 200ms, InOutCubic
//  - 2s after the last shake it shrinks back the same way
//  - while enlarged the pointer is always the arrow (KWin draws Qt::ArrowCursor),
//    so it grows over links and text too instead of staying a small hand/I-beam
// Windows has no compositor hook for this, so the documented equivalent is used:
// SetSystemCursor puts an enlarged Bibata arrow in every pointer role, and
// SPI_SETCURSORS reloads the user's scheme from the registry to put it all back.
// Bibata's .cur files carry 32-128px frames, so the big arrow is real artwork.
static const DWORD kAllCursorRoles[] = {
    32512 /*NORMAL*/, 32513 /*IBEAM*/, 32514 /*WAIT*/, 32515 /*CROSS*/, 32516 /*UP*/,
    32642 /*SIZENWSE*/, 32643 /*SIZENESW*/, 32644 /*SIZEWE*/, 32645 /*SIZENS*/, 32646 /*SIZEALL*/,
    32648 /*NO*/, 32649 /*HAND*/, 32650 /*APPSTARTING*/, 32651 /*HELP*/, 32671 /*PIN*/, 32672 /*PERSON*/,
};
static UINT_PTR g_growTimer = 0;
static int g_growPx = 0;                       // arrow size currently set (0 = the normal scheme)
static double g_mag = 1, g_magFrom = 1, g_magTarget = 1;
static ULONGLONG g_animT0 = 0, g_lastShake = 0;

static int CursorBasePx() { return (int)(g_set.cursorSize * GetDpiForSystem() / 96.0 + 0.5); }

static void SetAllCursorsPx(int px) {
    if (px == g_growPx) return;
    std::wstring f = JoinPath(JoinPath(JoinPath(g_dir, L"cursors"), g_set.cursor), L"Pointer.cur");
    HCURSOR src = (HCURSOR)LoadImageW(nullptr, f.c_str(), IMAGE_CURSOR, px, px, LR_LOADFROMFILE);
    if (!src) return;
    for (DWORD id : kAllCursorRoles) {
        HCURSOR c = (HCURSOR)CopyImage(src, IMAGE_CURSOR, 0, 0, 0);
        if (c && !SetSystemCursor(c, id)) DestroyCursor(c);   // on success Windows owns it
    }
    DestroyCursor(src);
    g_growPx = px;
}

void PointerGrowStop() {
    if (g_growTimer) { KillTimer(nullptr, g_growTimer); g_growTimer = 0; }
    g_mag = g_magFrom = g_magTarget = 1;
    if (g_growPx) { g_growPx = 0; SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0); }
}

static double InOutCubic(double p) { return p < 0.5 ? 4 * p * p * p : 1 - pow(-2 * p + 2, 3) / 2; }

static void CALLBACK GrowTick(HWND, UINT, UINT_PTR, DWORD);
static void AnimateTo(double m) {
    if (m == g_magTarget) return;
    g_magFrom = g_mag;
    g_magTarget = m;
    g_animT0 = GetTickCount64();
    if (!g_growTimer) g_growTimer = SetTimer(nullptr, 0, 15, GrowTick);
}

static void CALLBACK GrowTick(HWND, UINT, UINT_PTR, DWORD) {
    ULONGLONG now = GetTickCount64();
    if (g_magTarget > 1 && now - g_lastShake >= 2000) AnimateTo(1);   // deflate timer
    double p = std::min(1.0, (now - g_animT0) / 200.0);
    g_mag = g_magFrom + (g_magTarget - g_magFrom) * InOutCubic(p);
    if (p >= 1 && g_magTarget == 1) { PointerGrowStop(); return; }
    int base = CursorBasePx();
    SetAllCursorsPx(std::max(base, (int)lround(base * g_mag / 2) * 2));
}

// a shake was detected
static void ShakeFound() {
    if (!g_set.applyCursors) { ShowFindCursor(); return; }   // not our cursors: just the ring
    double maxMag = 256.0 / CursorBasePx();                 // Windows' cursor size limit
    AnimateTo(std::min(maxMag, g_magTarget == 1 ? 3.0 : g_magTarget + 1.0));
    g_lastShake = GetTickCount64();
}

// KWin's ShakeDetector, fed by polling the pointer (Windows gives no global
// motion events without a hook). Positions are in logical px like KWin's.
struct ShakePt { double x, y; ULONGLONG t; };
static std::vector<ShakePt> g_shakeHist;

static bool SameSign(double a, double b) {
    const double tolerance = 1;   // tiny moves count as either direction
    return (a >= -tolerance && b >= -tolerance) || (a <= tolerance && b <= tolerance);
}

static bool ShakeDetect(double x, double y, ULONGLONG now) {
    auto &h = g_shakeHist;
    size_t k = 0;
    while (k < h.size() && now - h[k].t >= 1000) k++;
    h.erase(h.begin(), h.begin() + k);
    if (h.size() >= 2) {
        ShakePt &last = h[h.size() - 1];
        const ShakePt &prev = h[h.size() - 2];
        // still heading the same way: extend the last segment instead of adding one
        if (SameSign(last.x - prev.x, x - last.x) && SameSign(last.y - prev.y, y - last.y)) {
            last = {x, y, now};
            return false;
        }
    }
    h.push_back({x, y, now});
    double left = h[0].x, top = h[0].y, right = h[0].x, bottom = h[0].y, distance = 0;
    for (size_t i = 1; i < h.size(); i++) {
        double dx = h[i].x - h[i - 1].x, dy = h[i].y - h[i - 1].y;
        distance += sqrt(dx * dx + dy * dy);
        left = std::min(left, h[i].x); top = std::min(top, h[i].y);
        right = std::max(right, h[i].x); bottom = std::max(bottom, h[i].y);
    }
    double bw = right - left, bh = bottom - top, diagonal = sqrt(bw * bw + bh * bh);
    if (diagonal < 100) return false;
    if (distance / diagonal > 4) { h.clear(); return true; }
    return false;
}

static void ShakeTick() {
    static POINT last = {LONG_MIN, LONG_MIN};
    POINT p; GetCursorPos(&p);
    if ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) {
        g_shakeHist.clear();   // dragging isn't shaking
        last = p;
        return;
    }
    if (p.x == last.x && p.y == last.y) return;   // only motion counts, as with KWin's events
    last = p;
    double sc = GetDpiForSystem() / 96.0;
    if (ShakeDetect(p.x / sc, p.y / sc, GetTickCount64())) ShakeFound();
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

// ============================================= Windows flyouts at the top ==
// The panel's volume and Wi-Fi icons open Windows' own Quick Settings pages
// (sound output, Wi-Fi list). Windows draws those next to the taskbar: one tall,
// transparent shell window (ShellHost, class ControlCenterWindow) down the right
// edge, with the content at its bottom. Windows gives that window a region
// (SetWindowRgn) covering exactly the visible content, so GetWindowRgnBox says
// where the content is, and moving the window with SetWindowPos (documented; the
// shell keeps working) puts the content's top edge just under the panel.
//  - No flash at the bottom: Windows keeps that window (hidden) between uses, so
//    before opening it we move it to where the content sat last time for the same
//    page. It appears under the panel straight away. A WinEvent hook on it being
//    shown/uncloaked corrects it the moment it appears, if the height changed.
//  - While it's open the region is re-read ~60 times a second: the Wi-Fi list
//    growing as it scans, or moving between pages, keeps the top edge put.
//  - This applies however the popup was opened (our icons, Win+Ctrl+V...).
static HWND g_fly = nullptr;
static UINT_PTR g_flyTimer = 0;
static ULONGLONG g_flyStart = 0, g_flyGoneSince = 0;
static int g_flyWhich = -1;                 // page we opened (-1: opened some other way)
static int g_flyBoxTop[2] = {-1, -1};       // content top within the window, last seen per page
static int g_flyBoxTopAny = -1;             // ...and for whatever was shown last
static HWINEVENTHOOK g_flyShowHook = nullptr, g_flyUncloakHook = nullptr;

static void FlyoutStop() {
    if (g_flyTimer) { KillTimer(nullptr, g_flyTimer); g_flyTimer = 0; }
    g_fly = nullptr;
    g_flyWhich = -1;
}

static bool FlyoutShowing(HWND h) {
    if (!h || !IsWindow(h) || !IsWindowVisible(h)) return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
    return !cloaked;
}

// the window y that puts content starting `boxTop` px into the window just under the panel
static int FlyoutWindowY(HWND h, int boxTop) {
    RECT wr;
    GetWindowRect(h, &wr);
    HMONITOR mon = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    return mi.rcMonitor.top + (int)((g_set.panelHeight + 6) * DpiScaleForMonitor(mon)) - boxTop;
}

static void FlyoutMoveTo(HWND h, int y) {
    RECT wr;
    GetWindowRect(h, &wr);
    if (abs(wr.top - y) > 1)
        SetWindowPos(h, nullptr, wr.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// place by the region Windows gave the content; false if there isn't one yet
static bool FlyoutPlace(HWND h) {
    RECT box;   // the content, relative to the window
    if (GetWindowRgnBox(h, &box) == ERROR || box.bottom - box.top < 40) return false;
    if (g_flyWhich >= 0) g_flyBoxTop[g_flyWhich] = box.top;
    g_flyBoxTopAny = box.top;
    FlyoutMoveTo(h, FlyoutWindowY(h, box.top));
    return true;
}

static void CALLBACK FlyoutTick(HWND, UINT, UINT_PTR, DWORD) {
    ULONGLONG now = GetTickCount64();
    if (!g_fly) {   // wait for it to open
        HWND f = FindWindowW(L"ControlCenterWindow", nullptr);
        if (FlyoutShowing(f)) { g_fly = f; g_flyGoneSince = 0; }
        else if (now - g_flyStart > 5000) FlyoutStop();   // Windows can be slow to open it
        if (!g_fly) return;
    }
    if (!FlyoutShowing(g_fly)) {   // closed (it's hidden/cloaked, not destroyed)
        if (!g_flyGoneSince) g_flyGoneSince = now;
        else if (now - g_flyGoneSince > 400) FlyoutStop();
        return;
    }
    g_flyGoneSince = 0;
    FlyoutPlace(g_fly);
}

// start keeping the Quick Settings popup under the panel while it's open
void FlyoutWatch() {
    g_flyStart = GetTickCount64();
    if (!g_flyTimer) g_flyTimer = SetTimer(nullptr, 0, 15, FlyoutTick);
}

// the popup is being shown/uncloaked: place it right now, before it's seen
static void CALLBACK FlyoutShownEvent(HWINEVENTHOOK, DWORD, HWND h, LONG idObject, LONG, DWORD, DWORD) {
    if (!h || idObject != OBJID_WINDOW) return;
    wchar_t cls[32] = {};
    GetClassNameW(h, cls, 32);
    if (wcscmp(cls, L"ControlCenterWindow")) return;
    if (!FlyoutPlace(h)) {
        int top = g_flyWhich >= 0 && g_flyBoxTop[g_flyWhich] >= 0 ? g_flyBoxTop[g_flyWhich] : g_flyBoxTopAny;
        if (top >= 0) FlyoutMoveTo(h, FlyoutWindowY(h, top));
    }
    FlyoutWatch();
}

void FlyoutHooksStart() {
    // only ShellHost's events are wanted, but its process id isn't known up front;
    // the class check in the callback filters, and these events are infrequent
    g_flyShowHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, FlyoutShownEvent, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_flyUncloakHook = SetWinEventHook(EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, FlyoutShownEvent, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}
void FlyoutHooksStop() {
    if (g_flyShowHook) { UnhookWinEvent(g_flyShowHook); g_flyShowHook = nullptr; }
    if (g_flyUncloakHook) { UnhookWinEvent(g_flyUncloakHook); g_flyUncloakHook = nullptr; }
    FlyoutStop();
}

// which: 0 sound output (Win+Ctrl+V), 1 Wi-Fi list
void OpenWindowsFlyout(int which) {
    g_flyWhich = which;
    // move the hidden popup window to where this page's content sat last time, so
    // it appears under the panel instead of sliding up from the bottom first
    HWND f = FindWindowW(L"ControlCenterWindow", nullptr);
    int top = g_flyBoxTop[which];
    if (f && top >= 0 && !FlyoutShowing(f)) FlyoutMoveTo(f, FlyoutWindowY(f, top));
    if (which == 0) SendKeys({VK_LWIN, VK_CONTROL, 'V'});
    else ShellOpen(L"ms-availablenetworks:");
    FlyoutWatch();
}

// The top menu bar: one per monitor, reserving space with the AppBar API.
// Left: logo menu, focused app name, that app's menus (real HMENU when the app
// exposes one, otherwise a standard set that drives the app with shortcuts).
// Centre: clock. Right: status items (Control Center, clipboard, battery,
// volume, network), each a small glyph with its own menu/flyout.
#include "common.h"
#include <powrprof.h>

static float BarH() { return (float)g_set.panelHeight; }   // logical height
// acrylic tint in ABGR: alpha from the opacity setting (panelGlass, 0 clearest
// .. 100 solid), and the colour blended from a neutral grey toward the accent
// by the tint setting (panelTint, 0 neutral .. 100 strongly accent-tinted).
static unsigned PanelTint() {
    unsigned a = (unsigned)(0x18 + g_set.panelGlass * 0xB0 / 100);   // 24..200 alpha
    if (a > 0xE0) a = 0xE0;
    int baseV = g_pal.dark ? 0x14 : 0xF2;                 // neutral grey per channel
    int aR = (g_set.accent >> 16) & 0xFF, aG = (g_set.accent >> 8) & 0xFF, aB = g_set.accent & 0xFF;
    float t = g_set.panelTint / 100.f * 0.6f;             // accent influence, capped at 60%
    int R = baseV + (int)((aR - baseV) * t);
    int G = baseV + (int)((aG - baseV) * t);
    int B = baseV + (int)((aB - baseV) * t);
    unsigned rgb = ((unsigned)R << 16) | ((unsigned)G << 8) | (unsigned)B;   // 0xRRGGBB
    return (a << 24) | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

struct AppMenuSpec { std::wstring title; std::vector<MenuItem> items; };

// standard menus for an app that doesn't give us its own; items drive the
// panel's window (the top app on that monitor) with the usual Windows shortcuts.
// It may not be the focused one, so it's brought forward before the keys go.
static std::vector<AppMenuSpec> StandardMenus(const std::wstring &appName, HWND target) {
    // keys are copied into a vector: a captured initializer_list doesn't own its array
    auto key = [target](std::initializer_list<WORD> k) {
        std::vector<WORD> keys(k);
        return [keys, target]() {
            if (target && IsWindow(target) && GetForegroundWindow() != target) SetForegroundWindow(target);
            if (keys.size() == 1) SendKeys({keys[0]});
            else if (keys.size() == 2) SendKeys({keys[0], keys[1]});
        };
    };
    std::vector<AppMenuSpec> r;
    r.push_back({L"File", {
        {L"New Window", L"Ctrl+N", key({VK_CONTROL, 'N'})},
        {L"New Tab", L"Ctrl+T", key({VK_CONTROL, 'T'})},
        {L"Open…", L"Ctrl+O", key({VK_CONTROL, 'O'})},
        MenuItem::Sep(),
        {L"Print…", L"Ctrl+P", key({VK_CONTROL, 'P'})},
        {L"Save", L"Ctrl+S", key({VK_CONTROL, 'S'})},
        MenuItem::Sep(),
        {L"Close Tab", L"Ctrl+W", key({VK_CONTROL, 'W'})},
        {L"Close Window", L"Alt+F4", key({VK_MENU, VK_F4})},
    }});
    r.push_back({L"Edit", {
        {L"Undo", L"Ctrl+Z", key({VK_CONTROL, 'Z'})},
        {L"Redo", L"Ctrl+Y", key({VK_CONTROL, 'Y'})},
        MenuItem::Sep(),
        {L"Cut", L"Ctrl+X", key({VK_CONTROL, 'X'})},
        {L"Copy", L"Ctrl+C", key({VK_CONTROL, 'C'})},
        {L"Paste", L"Ctrl+V", key({VK_CONTROL, 'V'})},
        {L"Select All", L"Ctrl+A", key({VK_CONTROL, 'A'})},
        MenuItem::Sep(),
        {L"Find…", L"Ctrl+F", key({VK_CONTROL, 'F'})},
    }});
    r.push_back({L"View", {
        {L"Zoom In", L"Ctrl++", key({VK_CONTROL, VK_OEM_PLUS})},
        {L"Zoom Out", L"Ctrl+-", key({VK_CONTROL, VK_OEM_MINUS})},
        {L"Actual Size", L"Ctrl+0", key({VK_CONTROL, '0'})},
        MenuItem::Sep(),
        {L"Full Screen", L"F11", key({VK_F11})},
    }});
    MenuItem minimize{L"Minimize", L"", [target]() { if (target && IsWindow(target)) ShowWindow(target, SW_MINIMIZE); }};
    MenuItem zoom{L"Zoom", L"", [target]() { if (target && IsWindow(target)) ShowWindow(target, IsZoomed(target) ? SW_RESTORE : SW_MAXIMIZE); }};
    r.push_back({L"Window", {minimize, zoom,
        MenuItem::Sep(),
        {L"Bring All to Front", L"", []() { MissionControlToggle(); }}}});
    return r;
}

// try to read the focused window's own top-level menu (classic Win32 apps)
static bool RealMenus(HWND h, std::vector<AppMenuSpec> &out) {
    HMENU bar = GetMenu(h);
    if (!bar) return false;
    int n = GetMenuItemCount(bar);
    if (n <= 0) return false;
    for (int i = 0; i < n; i++) {
        wchar_t buf[128] = {};
        MENUITEMINFOW mi = {sizeof(mi)};
        mi.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_STATE;
        mi.dwTypeData = buf;
        mi.cch = 128;
        if (!GetMenuItemInfoW(bar, i, TRUE, &mi)) continue;
        if (!buf[0]) continue;
        AppMenuSpec spec;
        spec.title = buf;
        // remove the & accelerators for display
        spec.title.erase(std::remove(spec.title.begin(), spec.title.end(), L'&'), spec.title.end());
        HMENU sub = mi.hSubMenu;
        if (sub) {
            int sn = GetMenuItemCount(sub);
            for (int j = 0; j < sn; j++) {
                wchar_t sb[160] = {};
                MENUITEMINFOW si = {sizeof(si)};
                si.fMask = MIIM_STRING | MIIM_STATE | MIIM_ID | MIIM_FTYPE;
                si.dwTypeData = sb;
                si.cch = 160;
                if (!GetMenuItemInfoW(sub, j, TRUE, &si)) continue;
                if (si.fType & MFT_SEPARATOR) { spec.items.push_back(MenuItem::Sep()); continue; }
                if (!sb[0]) continue;
                std::wstring label = sb, shortcut;
                size_t tab = label.find(L'\t');
                if (tab != std::wstring::npos) { shortcut = label.substr(tab + 1); label = label.substr(0, tab); }
                label.erase(std::remove(label.begin(), label.end(), L'&'), label.end());
                MenuItem it;
                it.text = label;
                it.shortcut = shortcut;
                it.disabled = (si.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;
                it.checked = (si.fState & MFS_CHECKED) != 0;
                UINT id = si.wID;
                it.action = [h, id]() { PostMessageW(h, WM_COMMAND, id, 0); };
                spec.items.push_back(it);
            }
        }
        if (!spec.items.empty()) out.push_back(spec);
    }
    return !out.empty();
}

// ------------------------------------------------------------- the bar ----
struct Bar : Surface {
    HMONITOR mon;
    bool primary;
    // hit zones on the left (logo + app name + menu titles) and right (status)
    struct Zone { float x0, x1; int id; };
    std::vector<Zone> left, right;
    std::wstring appName = L"Finder";
    HWND target = nullptr;     // the app window this panel names (top one on its monitor)
    // dragging the panel moves that window, like dragging its title bar
    HWND dragWin = nullptr;
    bool dragging = false, wasZoomed = false;
    POINT pressPt = {}, grab = {};
    void DragWindowTo(POINT c);
    void EndWindowDrag(POINT c);
    std::vector<AppMenuSpec> menus;
    bool menusAreReal = false;
    int hot = -1;          // index into a flattened zone list (left first, then right)
    float clockW = 0;
    std::wstring clockText;
    float clockZoneX0 = 0, clockZoneX1 = 0;

    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnMouseLeave() override { if (hot != -1) { hot = -1; Invalidate(); } }
    void Rebuild();
    void OpenLogoMenu(float x);
    void OpenAppMenu(int i, float x);
    void OpenStatusMenu(int id, float x);
    std::wstring Clock();
    int BatteryPercent(bool &charging);
    float right_x0_for(int id);
    RECT anchorAt(float x);
};

static std::vector<Bar *> g_bars;
static UINT_PTR g_timer = 0;

std::wstring Bar::Clock() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[128];
    const wchar_t *days[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    const wchar_t *mon3[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    std::wstring s;
    if (g_set.clockDate) {
        wchar_t d[64];
        swprintf(d, 64, L"%ls %d %ls  ", days[st.wDayOfWeek], st.wDay, mon3[st.wMonth - 1]);
        s += d;
    }
    int h = st.wHour;
    const wchar_t *ap = L"";
    if (!g_set.clock24) { ap = h < 12 ? L" AM" : L" PM"; h = h % 12; if (h == 0) h = 12; }
    if (g_set.clockSeconds) swprintf(buf, 128, L"%d:%02d:%02d%ls", h, st.wMinute, st.wSecond, ap);
    else swprintf(buf, 128, L"%d:%02d%ls", h, st.wMinute, ap);
    return s + buf;
}

int Bar::BatteryPercent(bool &charging) {
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps) && ps.BatteryLifePercent <= 100) {
        charging = (ps.ACLineStatus == 1);
        return ps.BatteryLifePercent;
    }
    charging = false;
    return -1;
}

// The app each monitor's panel names: the focused window if it's on that monitor,
// otherwise the topmost app window there (EnumWindows walks top to bottom).
static HWND TopAppWindowOn(HMONITOR mon) {
    HWND fg = GetForegroundWindow();
    if (fg && IsAppWindow(fg) && !IsIconic(fg) && MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) == mon) return fg;
    struct Scan { HMONITOR mon; HWND hit; } s = {mon, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto *s = (Scan *)lp;
        if (IsIconic(h) || !IsAppWindow(h)) return TRUE;
        if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != s->mon) return TRUE;
        s->hit = h;
        return FALSE;
    }, (LPARAM)&s);
    return s.hit;
}

void Bar::Rebuild() {
    target = TopAppWindowOn(mon);
    menus.clear();
    menusAreReal = false;
    if (target) {
        appName = AppNameFor(target);
        std::vector<AppMenuSpec> real;
        if (RealMenus(target, real)) { menus = real; menusAreReal = true; }
        else menus = StandardMenus(appName, target);
    } else {
        appName = L"Desktop";
        menus = StandardMenus(appName, nullptr);
    }
    Invalidate();
}

// catch changes no event told us about (a window closed in the background, etc.):
// rebuild only the panels whose top window actually changed
void MenuBarTrack() {
    for (auto *b : g_bars)
        if (TopAppWindowOn(b->mon) != b->target) b->Rebuild();
}

void Bar::OnPaint() {
    float h = H();
    // the bar itself is the acrylic backdrop; a faint hairline under it
    Line(0, h - 0.5f, W(), h - 0.5f, g_pal.separator);
    left.clear();
    right.clear();
    int zi = 0;
    float x = 8;
    // logo
    float logoW = 30;
    {
        bool on = hot == zi;
        if (on) Fill(D2D1::RectF(x - 3, 2, x + 20, h - 2), g_pal.hover, 6);
        IWICBitmapSource *logo = ThemedIcon(L"..\\GoldenGate");   // not used; draw vector instead
        (void)logo;
        // draw the circle-in-ring mark
        float cy = h / 2, cx = x + 8, r = 8;
        Circle(cx, cy, r, g_pal.text, true);
        Circle(cx, cy, r * 0.78f, g_pal.panelStrong, true);
        Circle(cx, cy, r * 0.56f, g_pal.text, true);
        left.push_back({x - 3, x + 20, zi++});
        x += logoW;
    }
    // app name (bold)
    {
        bool on = hot == zi;
        float tw = TextWidth(appName, 13.5f, DWRITE_FONT_WEIGHT_BOLD);
        if (on) Fill(D2D1::RectF(x - 4, 2, x + tw + 6, h - 2), g_pal.hover, 6);
        Text(appName, D2D1::RectF(x, 0, x + tw + 4, h), g_pal.text, 13.5f, DWRITE_FONT_WEIGHT_BOLD);
        left.push_back({x - 4, x + tw + 6, zi++});
        x += tw + 16;
    }
    // app menu titles
    for (size_t i = 0; i < menus.size(); i++) {
        bool on = hot == zi;
        float tw = TextWidth(menus[i].title, 13, DWRITE_FONT_WEIGHT_NORMAL);
        if (on) Fill(D2D1::RectF(x - 6, 2, x + tw + 6, h - 2), g_pal.hover, 6);
        Text(menus[i].title, D2D1::RectF(x, 0, x + tw + 4, h), g_pal.text, 13);
        left.push_back({x - 6, x + tw + 6, zi++});
        x += tw + 16;
    }
    // ---- clock (far right, like macOS) unless centred
    clockText = Clock();
    float cw = TextWidth(clockText, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
    float rx = W() - 12;
    if (!g_set.clockCenter) {
        float cx0 = rx - cw;
        bool chot = hot == 200;
        if (chot) Fill(D2D1::RectF(cx0 - 6, 2, rx + 6, h - 2), g_pal.hover, 6);
        Text(clockText, D2D1::RectF(cx0, 0, rx + 2, h), g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        clockZoneX0 = cx0 - 6; clockZoneX1 = rx + 6;
        rx = cx0 - 16;
    }
    // status items, right-to-left: Control Center, Spotlight, battery, volume, Wi-Fi, clipboard, caffeine
    auto addRight = [&](const std::wstring &glyph, int id, float size, DWRITE_FONT_WEIGHT wt, bool iconFont) {
        float tw = TextWidth(glyph, size, wt, iconFont);
        float x0 = rx - tw;
        bool on = hot == id;
        if (on) Fill(D2D1::RectF(x0 - 6, 2, rx + 6, h - 2), g_pal.hover, 6);
        Text(glyph, D2D1::RectF(x0, 0, rx + 1, h), g_pal.text, size, wt, DWRITE_TEXT_ALIGNMENT_LEADING, iconFont);
        right.push_back({x0 - 6, rx + 6, id});
        rx = x0 - 16;
    };
    addRight(L"", 100, 15, DWRITE_FONT_WEIGHT_NORMAL, true);        // Control Center
    if (g_set.spotlight) addRight(L"\uE721", 106, 14, DWRITE_FONT_WEIGHT_NORMAL, true); // Spotlight
    bool charging = false;
    int bat = g_set.showBattery ? BatteryPercent(charging) : -1;
    if (bat >= 0) {
        wchar_t b[32];
        swprintf(b, 32, L"%d%%%ls", bat, charging ? L" \uE945" : L"");
        addRight(b, 103, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM, false);
    }
    addRight(L"", 104, 14, DWRITE_FONT_WEIGHT_NORMAL, true);       // volume
    addRight(L"", 105, 14, DWRITE_FONT_WEIGHT_NORMAL, true);      // Wi-Fi
    if (g_set.clipboard) addRight(L"", 101, 15, DWRITE_FONT_WEIGHT_NORMAL, true);
    if (g_set.caffeine) addRight(L"", 102, 15, DWRITE_FONT_WEIGHT_NORMAL, true);

    // ---- centred clock sits between the app menus and the status items
    if (g_set.clockCenter) {
        float cx = (W() - cw) / 2;
        if (cx < x + 10) cx = x + 10;
        if (cx + cw > rx - 10) cx = rx - 10 - cw;
        bool chot = hot == 200;
        if (chot) Fill(D2D1::RectF(cx - 6, 2, cx + cw + 6, h - 2), g_pal.hover, 6);
        Text(clockText, D2D1::RectF(cx, 0, cx + cw + 2, h), g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        clockZoneX0 = cx - 6; clockZoneX1 = cx + cw + 6;
    }
}

// Panel drag. Past a few pixels the panel's window follows the pointer (we hold
// the mouse capture). A maximised window first restores to its normal size under
// the pointer, as a title-bar drag does; dropping it back on any panel maximises
// it on that monitor. A plain click just focuses the monitor's app.
void Bar::DragWindowTo(POINT c) {
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) { EndWindowDrag(c); return; }   // capture lost
    if (!dragWin || !IsWindow(dragWin)) return;
    if (!dragging) {
        if (abs(c.x - pressPt.x) < 6 && abs(c.y - pressPt.y) < 6) return;
        dragging = true;
        RECT wr; GetWindowRect(dragWin, &wr);
        wasZoomed = IsZoomed(dragWin) != FALSE;
        if (wasZoomed) {
            WINDOWPLACEMENT wp = {sizeof(wp)};
            GetWindowPlacement(dragWin, &wp);
            int nw = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            int nh = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            double fx = (double)(pressPt.x - wr.left) / std::max(1L, wr.right - wr.left);
            ShowWindow(dragWin, SW_RESTORE);
            grab = {(LONG)(fx * nw), (LONG)(16 * scale)};   // pointer stays on its title bar
            SetWindowPos(dragWin, nullptr, c.x - grab.x, c.y - grab.y, nw, nh, SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        grab = {pressPt.x - wr.left, pressPt.y - wr.top};
    }
    SetWindowPos(dragWin, nullptr, c.x - grab.x, c.y - grab.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Bar::EndWindowDrag(POINT c) {
    HWND w = dragWin;
    bool moved = dragging;
    dragWin = nullptr;
    dragging = false;
    if (GetCapture() == hwnd) ReleaseCapture();
    if (!w || !IsWindow(w)) return;
    if (moved && wasZoomed) {
        HMONITOR m = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(m, &mi);
        int panelPx = (int)(g_set.panelHeight * DpiScaleForMonitor(m)) + 2;
        if (c.y < mi.rcMonitor.top + panelPx) {   // dropped back on a panel: maximise there
            SetWindowPos(w, nullptr, mi.rcWork.left + 40, mi.rcWork.top + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(w, SW_MAXIMIZE);
        }
    }
    SetForegroundWindow(w);
}

void Bar::OnMouse(UINT msg, float x, float y, WPARAM wp) {
    if (dragWin) {   // a panel drag is in progress (mouse captured)
        POINT c; GetCursorPos(&c);
        if (msg == WM_MOUSEMOVE) DragWindowTo(c);
        else if (msg == WM_LBUTTONUP) EndWindowDrag(c);
        return;
    }
    int z = -1;
    for (size_t i = 0; i < left.size(); i++) if (x >= left[i].x0 && x < left[i].x1) { z = (int)i; break; }
    int statusId = -1;
    if (z < 0) for (auto &r : right) if (x >= r.x0 && x < r.x1) { statusId = r.id; break; }
    bool onClock = x >= clockZoneX0 && x < clockZoneX1;
    // scrolling over the volume icon changes the volume
    if (msg == WM_MOUSEWHEEL) { if (statusId == 104) VolumeNudge(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1); return; }
    int newHot = z >= 0 ? z : (statusId >= 0 ? statusId : (onClock ? 200 : -1));
    if (newHot != hot) { hot = newHot; Invalidate(); }
    // press on the app name or an empty part of the panel: start a window drag
    bool emptyOrName = (z == 1 || z < 0) && statusId < 0 && !onClock;
    if (msg == WM_LBUTTONDOWN && emptyOrName && target && IsWindow(target)) {
        dragWin = target;
        dragging = false;
        GetCursorPos(&pressPt);
        SetCapture(hwnd);
        return;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
        if (z == 0) OpenLogoMenu(left[0].x0);
        else if (z == 1) { /* app name with no window: nothing to do */ }
        else if (z >= 2 && z - 2 < (int)menus.size()) OpenAppMenu(z - 2, left[z].x0);
        else if (statusId >= 100) OpenStatusMenu(statusId, right_x0_for(statusId));
        else if (onClock) { RECT a = anchorAt((clockZoneX0 + clockZoneX1) / 2); extern void CalendarToggle(RECT); CalendarToggle(a); }
    }
}

float Bar::right_x0_for(int id) {
    for (auto &r : right) if (r.id == id) return r.x0;
    return W() - 40;
}

RECT Bar::anchorAt(float x) {
    RECT wr;
    GetWindowRect(hwnd, &wr);
    RECT a = {wr.left + (int)(x * scale), wr.top, wr.left + (int)(x * scale) + 10, wr.bottom};
    return a;
}

void Bar::OpenLogoMenu(float x) {
    std::vector<MenuItem> m;
    m.push_back({L"About This PC", L"", []() { ShellOpen(L"ms-settings:about"); }});   // System > About
    m.push_back(MenuItem::Sep());
    m.push_back({L"System Settings…", L"", []() { ShellOpen(L"ms-settings:"); }});
    m.push_back({L"Task Manager…", L"", []() { ShellOpen(L"taskmgr.exe"); }});
    m.push_back(MenuItem::Sep());
    m.push_back({L"Sleep", L"", []() { SetSuspendState(FALSE, FALSE, FALSE); }});
    if (IsPwrHibernateAllowed())
        m.push_back({L"Hibernate", L"", []() { SetSuspendState(TRUE, FALSE, FALSE); }});
    m.push_back({L"Restart…", L"", []() { ShowConfirm(L"Restart", L"Close all apps and restart this PC?", L"Restart",
        []() { ShellOpen(L"shutdown.exe", L"/r /t 0"); }); }});
    m.push_back({L"Shut Down…", L"", []() { ShowConfirm(L"Shut Down", L"Close all apps and shut down this PC?", L"Shut Down",
        []() { ShellOpen(L"shutdown.exe", L"/s /t 0"); }); }});
    m.push_back(MenuItem::Sep());
    m.push_back({L"Lock Screen", L"Win+L", []() { LockWorkStation(); }});
    m.push_back({L"Sign Out…", L"", []() { ShowConfirm(L"Sign Out", L"Sign out of Windows?", L"Sign Out",
        []() { ExitWindowsEx(EWX_LOGOFF, 0); }); }});
    RECT a = anchorAt(x);
    PopupMenu(m, a.left, a.bottom, hwnd, 200);
}

void Bar::OpenAppMenu(int i, float x) {
    RECT a = anchorAt(x);
    PopupMenu(menus[i].items, a.left, a.bottom, hwnd, 180);
}

void Bar::OpenStatusMenu(int id, float x) {
    RECT a = anchorAt(x);
    switch (id) {
    case 100: ControlCenterToggle(a); break;
    case 101: ClipboardShow(a); break;
    case 102: {   // caffeine toggle
        g_set.caffeine = !g_set.caffeine;
        CaffeineSet(g_set.caffeine);
        SaveSettings();
        MenuBarRefresh();
        break;
    }
    case 104: OpenWindowsFlyout(0); break;   // Windows' sound output panel, moved under the panel
    case 105: OpenWindowsFlyout(1); break;   // Windows' Wi-Fi list, moved under the panel
    case 106: SpotlightToggle(); break;
    default: ControlCenterToggle(a); break;
    }
}

// ----------------------------------------------------- appbar placement ---
static void RegisterAppBar(Bar *b, bool add) {
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = b->hwnd;
    abd.uCallbackMessage = WM_GG_TRAY;
    abd.uEdge = ABE_TOP;
    SHAppBarMessage(add ? ABM_NEW : ABM_REMOVE, &abd);
}

// position the bar window across the top of its monitor, and reserve that strip
// on EVERY monitor so maximized windows stop below it (the AppBar API picks the
// monitor from the rect). The bar must already be registered with ABM_NEW
// before this, or SETPOS reserves nothing.
static void PlaceBar(Bar *b, MONITORINFO &mi) {
    float sc = DpiScaleForMonitor(b->mon);
    int h = (int)(BarH() * sc + 0.5f);
    RECT r = mi.rcMonitor;
    r.bottom = r.top + h;
    if (g_set.menubar) {
        APPBARDATA abd = {sizeof(abd)};
        abd.hWnd = b->hwnd;
        abd.uEdge = ABE_TOP;
        abd.rc = mi.rcMonitor;
        abd.rc.bottom = mi.rcMonitor.top + h;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.bottom = abd.rc.top + h;
        SHAppBarMessage(ABM_SETPOS, &abd);
        r = abd.rc;
    }
    b->Move(r);
    SetWindowPos(b->hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static BOOL CALLBACK MonCb(HMONITOR mon, HDC, LPRECT, LPARAM) {
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    Bar *b = new Bar();
    b->mon = mon;
    b->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    float sc = DpiScaleForMonitor(mon);
    RECT r = mi.rcMonitor;
    r.bottom = r.top + (int)(BarH() * sc + 0.5f);
    b->Create(Surface::Glass, L"GoldenGateMenuBar", WS_EX_TOPMOST | WS_EX_NOACTIVATE, r, nullptr, false);
    // glassy translucent bar: low-alpha tint over a live blur of what's behind
    EnableAcrylic(b->hwnd, PanelTint());
    g_bars.push_back(b);
    // register first (so the later SETPOS actually reserves space), then place
    RegisterAppBar(b, true);
    PlaceBar(b, mi);
    b->Rebuild();
    b->Show(true, false);
    return TRUE;
}

void CALLBACK MenuBarTimer(HWND, UINT, UINT_PTR, DWORD) {
    for (auto *b : g_bars) {
        b->Invalidate();   // clock tick
        // stay above a newly-activated or maximized window
        SetWindowPos(b->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void MenuBarRetint() {
    for (auto *b : g_bars) EnableAcrylic(b->hwnd, PanelTint());
}

void MenuBarReassert() {
    for (auto *b : g_bars)
        SetWindowPos(b->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void MenuBarStart() {
    if (!g_bars.empty() || !g_set.menubar) return;
    EnumDisplayMonitors(nullptr, nullptr, MonCb, 0);
    g_timer = SetTimer(nullptr, 0, g_set.clockSeconds ? 1000 : 2000, MenuBarTimer);
}

void MenuBarStop() {
    if (g_timer) { KillTimer(nullptr, g_timer); g_timer = 0; }
    for (auto *b : g_bars) {
        RegisterAppBar(b, false);
        b->Destroy();
        delete b;
    }
    g_bars.clear();
    // release the reserved strip
    APPBARDATA abd = {sizeof(abd)};
    SHAppBarMessage(ABM_REMOVE, &abd);
}

void MenuBarRefresh() {
    for (auto *b : g_bars) b->Rebuild();
}

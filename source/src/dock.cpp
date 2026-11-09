// The Dock: a translucent rounded bar at the bottom centre of the primary
// monitor. Pinned apps + running apps, a running dot under each, magnification
// on hover, a Downloads stack and the Trash. Reserves space with the AppBar
// API when not in auto-hide. Layered (per-pixel alpha) so the rounded glass
// edges look right.
#include "common.h"

struct DockItem {
    std::wstring id;        // app id, or "::downloads" / "::trash"
    std::wstring label;
    bool pinned = false;
    bool running = false;
    bool separatorBefore = false;
    std::vector<HWND> windows;
    IWICBitmapSource *icon = nullptr;
    float curSize = 0;      // animated size
    float bounce = 0;       // launch bounce phase
};

struct Dock : Surface {
    std::vector<DockItem> items;
    int hot = -1;
    float hoverX = -1;
    int dragIndex = -1;      // pinned icon being dragged (-1 = none)
    bool dragging = false;   // moved past the drag threshold
    float dragGrabX = 0;
    int base, maxs;         // base and magnified icon size (px of art, logical)
    float gap = 6, padX = 12, padY = 10;
    UINT_PTR anim = 0;
    bool reserved = false;
    RECT fanAnchor = {};

    float reveal = 1.f, revealTarget = 1.f;   // 1 shown, 0 hidden (slid down)
    RECT shownRect = {};                        // the dock's rect when fully shown
    float contentW = 0;                         // current (magnified) content width, logical px
    float winW = 0;                             // fixed window width (fully-magnified bound)
    UINT_PTR intelliTimer = 0;
    DWORD lastOverlapCheck = 0;                 // throttle the EnumWindows overlap scan
    bool lastOverlapped = false;
    void Rebuild();
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool &handled) override;
    void Relayout(bool animate);
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnMouseLeave() override { hoverX = -1; hot = -1; Relayout(true); }
    float TargetSize(float center, float cursor);
    int HitItem(float x);
    void Activate(int i, bool rightClick);
    void Downloads();
    float x_of(int i);
    float x_of_downloads();
    void PositionWindow();
    bool Overlapped();
    bool CursorAtEdge();
    void IntelliTick();
};

static Dock *g_dock = nullptr;
bool DownloadsFanOpen();

// Icon for a running app that has no app-list icon. Tries, in order: its AppsFolder
// entry by app ID (Store apps, Chrome/Edge web apps), the themed icon, the exe's
// own hi-res icon, then the window's icon. Cached per app so the once-a-second
// Dock refresh never re-extracts (the old path leaked a new bitmap every rebuild)
// and never blocks on a hung app (timeout instead of a plain SendMessage).
static IWICBitmapSource *RunningIcon(const std::wstring &key, HWND w) {
    static std::map<std::wstring, IWICBitmapSource *> cache;
    std::wstring ck = (g_set.iconStyle ? L"t:" : L"o:") + key;
    auto f = cache.find(ck);
    if (f != cache.end()) return f->second;
    IWICBitmapSource *icon = nullptr;
    HWND real = RealAppWindow(w);
    std::wstring aumid = WindowAumid(real);
    std::wstring exe = WindowExe(w);
    if (g_set.iconStyle) icon = IconForExe(exe, aumid, 128);   // macOS-style set first
    if (!icon && !aumid.empty()) icon = ShellItemIcon(L"shell:AppsFolder\\" + aumid, 128);
    if (!icon && exe != L"applicationframehost.exe") {
        DWORD pid = 0;
        GetWindowThreadProcessId(real, &pid);
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (p) {
            wchar_t path[MAX_PATH];
            DWORD n = MAX_PATH;
            if (QueryFullProcessImageNameW(p, 0, path, &n)) icon = ShellItemIcon(path, 128);
            CloseHandle(p);
        }
    }
    if (!icon) {
        DWORD_PTR r = 0;
        if (SendMessageTimeoutW(w, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &r) && r)
            icon = WicFromHIcon((HICON)r);
        if (!icon) { HICON hi = (HICON)GetClassLongPtrW(w, GCLP_HICON); if (hi) icon = WicFromHIcon(hi); }
    }
    if (!icon) icon = IconForExe(exe, aumid, 128);
    cache[ck] = icon;
    return icon;
}

// recent files in the Downloads folder, newest first
static std::vector<std::wstring> RecentDownloads(int max) {
    std::vector<std::pair<FILETIME, std::wstring>> v;
    std::wstring dir = KnownFolder(FOLDERID_Downloads);
    if (dir.empty()) return {};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        if (fd.cFileName[0] == L'.') continue;
        std::wstring name = fd.cFileName;
        if (name.size() > 9 && Lower(name).substr(name.size() - 9) == L".crdownload") continue;
        v.push_back({fd.ftLastWriteTime, JoinPath(dir, name)});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return CompareFileTime(&a.first, &b.first) > 0; });
    std::vector<std::wstring> r;
    for (auto &p : v) { if ((int)r.size() >= max) break; r.push_back(p.second); }
    return r;
}

void Dock::Rebuild() {
    std::vector<DockItem> old = std::move(items);
    items.clear();
    auto prevSize = [&](const std::wstring &id) -> float {
        for (auto &o : old) if (o.id == id) return o.curSize;
        return (float)base;
    };
    // pinned first
    for (auto &pid : g_set.pinned) {
        DockItem it;
        it.id = pid;
        it.pinned = true;
        const AppEntry *a = FindApp(pid);
        it.label = a ? a->name : FileNameOf(pid);
        it.icon = AppIcon(pid, 128);
        it.curSize = prevSize(pid);
        items.push_back(it);
    }
    // running apps not already pinned, grouped by exe/aumid
    auto wins = ListAppWindows();
    std::map<std::wstring, std::vector<HWND>> groups;
    std::map<std::wstring, std::wstring> groupId;
    for (auto &w : wins) {
        std::wstring key = !w.aumid.empty() ? w.aumid : w.exe;
        if (key.empty()) continue;
        groups[key].push_back(w.hwnd);
        if (!groupId.count(key)) {
            // find a matching app id for launching/icon
            std::wstring id;
            for (auto &a : AllApps())
                if ((!w.aumid.empty() && a.aumid == w.aumid) || (!a.exe.empty() && a.exe == w.exe)) { id = a.id; break; }
            if (id.empty()) id = !w.aumid.empty() ? w.aumid : w.exe;
            groupId[key] = id;
        }
    }
    auto markRunning = [&](DockItem &it) {
        for (auto &g : groups) {
            std::wstring id = groupId[g.first];
            if (id == it.id) { it.running = true; it.windows = g.second; }
        }
    };
    for (auto &it : items) markRunning(it);
    for (auto &g : groups) {
        std::wstring id = groupId[g.first];
        bool have = false;
        for (auto &it : items) if (it.id == id) { have = true; break; }
        if (have) continue;
        DockItem it;
        it.id = id;
        it.running = true;
        it.windows = g.second;
        const AppEntry *a = FindApp(id);
        it.label = a ? a->name : AppNameFor(g.second[0]);
        it.icon = AppIcon(id, 128);
        if (!it.icon && !g.second.empty()) it.icon = RunningIcon(g.first, g.second[0]);
        it.curSize = prevSize(id);
        items.push_back(it);
    }
    // pinned apps whose app-list icon didn't resolve: use the running window's
    for (auto &it : items)
        if (!it.icon && !it.windows.empty()) it.icon = RunningIcon(it.id, it.windows[0]);
    // Golden Gate's own Settings window, shown as a running app while it's open
    HWND gs = SettingsHwnd();
    if (gs) {
        static IWICBitmapSource *ggIcon = nullptr;
        if (!ggIcon) ggIcon = LoadWicFile(JoinPath(g_dir, L"assets\\GoldenGate.png"));
        DockItem it;
        it.id = L"::ggsettings";
        it.label = L"Golden Gate";
        it.running = true;
        it.windows = {gs};
        it.icon = ggIcon;
        it.curSize = prevSize(it.id);
        items.push_back(it);
    }
    // Downloads + Trash at the end
    DockItem dl;
    dl.id = L"::downloads";
    dl.label = L"Downloads";
    dl.separatorBefore = true;
    dl.icon = ThemedIcon(L"blue-folder-download");
    dl.curSize = prevSize(dl.id);
    items.push_back(dl);
    DockItem tr;
    tr.id = L"::trash";
    tr.label = L"Trash";
    SHQUERYRBINFO rb = {sizeof(rb)};
    SHQueryRecycleBinW(nullptr, &rb);
    tr.icon = ThemedIcon(rb.i64NumItems > 0 ? L"user-trash-full" : L"user-trash");
    tr.curSize = prevSize(tr.id);
    items.push_back(tr);
    Relayout(false);
}

float Dock::TargetSize(float center, float cursor) {
    if (!g_set.dockMagnify || cursor < 0) return (float)base;
    float d = fabsf(center - cursor);
    float reach = (maxs) * 2.2f;
    if (d > reach) return (float)base;
    float t = 1 - d / reach;
    t = t * t * (3 - 2 * t);
    return base + (maxs - base) * t;
}

void Dock::Relayout(bool animate) {
    // compute target sizes along the row given the hovered x
    int n = (int)items.size();
    if (n == 0) return;
    // first pass with current sizes to find centres, then size with magnification
    std::vector<float> sz(n);
    float total = padX * 2;
    for (int i = 0; i < n; i++) { sz[i] = (float)base; total += sz[i] + (i ? gap : 0); if (items[i].separatorBefore) total += 10; }
    // place, measure centres relative to the dock's left, then magnify by cursor
    float cursorX = hoverX;
    // iterate twice for stable magnification
    for (int pass = 0; pass < 2; pass++) {
        float x = padX;
        total = padX * 2;
        for (int i = 0; i < n; i++) {
            if (items[i].separatorBefore) { x += 10; total += 10; }
            float center = x + sz[i] / 2;
            float t = TargetSize(center, cursorX);
            sz[i] = t;
            x += sz[i] + gap;
            total += sz[i] + (i ? gap : 0);
        }
    }
    // animate current sizes toward targets
    for (int i = 0; i < n; i++) {
        if (animate) items[i].curSize += (sz[i] - items[i].curSize) * 0.35f;
        else items[i].curSize = sz[i];
    }
    // current (magnified) content width
    float w = padX * 2;
    for (int i = 0; i < n; i++) { w += items[i].curSize + (i ? gap : 0); if (items[i].separatorBefore) w += 10; }
    contentW = w;
    // The window is a FIXED size, sized to the fully-magnified bound, so its
    // width never changes as icons magnify. That means Move() never sees a size
    // change, so the render target / DIB / icon-bitmap cache are never torn down
    // and rebuilt per frame (see PERFORMANCE_AUDIT.md, P0). Content is centred.
    float maxW = padX * 2;
    for (int i = 0; i < n; i++) { maxW += (float)maxs + (i ? gap : 0); if (items[i].separatorBefore) maxW += 10; }
    float dockH = (float)maxs + 30;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    float scl = scale;
    float workW = (mi.rcWork.right - mi.rcWork.left) / scl - 16;
    winW = std::min(maxW, workW);
    winW = std::max(winW, contentW);                 // never clip the content
    int pw = (int)(winW * scl + 0.5f), ph = (int)(dockH * scl + 0.5f);
    int cx = (mi.rcWork.left + mi.rcWork.right) / 2;
    int bottom = mi.rcMonitor.bottom;
    int margin = (int)(6 * scl);
    shownRect = {cx - pw / 2, bottom - ph - margin, cx + pw / 2, bottom - margin};
    PositionWindow();
    Invalidate();
}

// slide the window between shown (reveal 1) and hidden just off the bottom
// (reveal 0, a 3px sliver left to catch the edge-reveal).
void Dock::PositionWindow() {
    if (shownRect.right == shownRect.left) return;
    int ph = shownRect.bottom - shownRect.top;
    int hiddenTop = shownRect.bottom + (int)(4 * scale);   // fully off the bottom
    int top = (int)(hiddenTop + (shownRect.top - hiddenTop) * reveal);
    RECT r = {shownRect.left, top, shownRect.right, top + ph};
    Move(r);
}

// a top-level window on the dock's monitor overlaps the shown dock area
struct OverlapScan { RECT dock; HMONITOR mon; bool hit; };
static BOOL CALLBACK OverlapCb(HWND h, LPARAM lp) {
    auto *s = (OverlapScan *)lp;
    if (!IsWindowVisible(h) || IsIconic(h) || GetWindow(h, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    wchar_t cls[80];
    GetClassNameW(h, cls, 80);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcsncmp(cls, L"GoldenGate", 10) ||
        !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return TRUE;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != s->mon) return TRUE;
    RECT wr, is;
    GetWindowRect(h, &wr);
    if (IntersectRect(&is, &wr, &s->dock)) { s->hit = true; return FALSE; }
    return TRUE;
}
bool Dock::Overlapped() {
    OverlapScan sc = {shownRect, MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), false};
    EnumWindows(OverlapCb, (LPARAM)&sc);
    return sc.hit;
}

// pointer pushed to the bottom edge within the dock's horizontal reach
bool Dock::CursorAtEdge() {
    POINT p;
    GetCursorPos(&p);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    if (p.y < mi.rcMonitor.bottom - (int)(2 * scale)) return false;
    int reach = (int)(contentW * scale) / 2 + (int)(60 * scale);
    int cx = (shownRect.left + shownRect.right) / 2;
    return abs(p.x - cx) <= reach;
}

void Dock::IntelliTick() {
    // keep the auto-hidden Windows taskbar from revealing over the Dock at the
    // bottom edge: re-hide fast (every 30ms tick), not just every 300ms
    TaskbarKeepHidden();
    // clear stale magnification if the pointer left the Dock content (it can sit
    // in the fixed window's transparent side margins without a mouse message)
    if (hoverX >= 0) {
        POINT cp; GetCursorPos(&cp);
        RECT wr; GetWindowRect(hwnd, &wr);
        float left = (W() - contentW) / 2; if (left < 0) left = 0;
        int cL = wr.left + (int)(left * scale), cR = wr.left + (int)((left + contentW) * scale);
        if (cp.x < cL || cp.x > cR || cp.y < wr.top || cp.y > wr.bottom) { hoverX = -1; hot = -1; }
    }
    // ease magnification back to rest when not hovering
    if (hoverX < 0) {
        for (auto &it : items) if (fabsf(it.curSize - (float)base) > 0.5f) { Relayout(true); break; }
    }
    // a Dock menu or the Downloads stack is open: the pointer is on that, not the
    // Dock, but the Dock must stay up (like macOS) instead of sliding away
    bool hovering = hot >= 0 || hoverX >= 0 || PopupMenuOpen() || DownloadsFanOpen();
    bool show;
    if (CursorAtEdge() || hovering) show = true;
    else if (g_set.dockHide == 2) show = false;            // always auto-hide
    else if (g_set.dockHide == 1) {                        // intellihide
        // the EnumWindows overlap scan is expensive; run it ~4x/sec instead of
        // every 30ms tick (see PERFORMANCE_AUDIT.md, P1)
        DWORD now = GetTickCount();
        if (now - lastOverlapCheck >= 250) { lastOverlapped = Overlapped(); lastOverlapCheck = now; }
        show = !lastOverlapped;
    }
    else show = true;                                       // never hide
    revealTarget = show ? 1.f : 0.f;
    if (fabsf(reveal - revealTarget) > 0.005f) {
        reveal += (revealTarget - reveal) * 0.28f;
        if (fabsf(reveal - revealTarget) <= 0.005f) reveal = revealTarget;
        PositionWindow();
    }
}

LRESULT Dock::OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool &handled) {
    if (msg == WM_NCHITTEST) {
        // let clicks in the empty side margins of the fixed-width window fall
        // through to whatever is beneath (see PERFORMANCE_AUDIT.md, P0)
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        float lx = p.x / scale;
        float left = (W() - contentW) / 2; if (left < 0) left = 0;
        if (lx < left || lx > left + contentW) { handled = true; return HTTRANSPARENT; }
    }
    return 0;
}

void Dock::OnPaint() {
    float w = W(), h = H();
    // content is centred within the fixed-width window; the side margins are
    // transparent (and click-through, see OnMessage)
    float left = (w - contentW) / 2;
    if (left < 0) left = 0;
    float slabH = (float)base + padY * 2;
    float slabTop = h - slabH;
    float baseline = h - padY - 4;      // icon bottoms sit on this line
    D2D1_RECT_F slab = D2D1::RectF(left + 1, slabTop, left + contentW - 1, h - 1);
    for (int i = 6; i > 0; i--)
        Fill(D2D1::RectF(slab.left - i * 0.5f, slab.top - i * 0.3f + 2, slab.right + i * 0.5f, slab.bottom + i * 0.5f),
             WithAlpha(g_pal.shadow, 0.045f), 20 + i);
    Fill(slab, g_pal.panel, 20);
    Fill(slab, WithAlpha(g_pal.panelStrong, g_pal.dark ? 0.25f : 0.35f), 20);
    Stroke(D2D1::RectF(slab.left + 0.5f, slab.top + 0.5f, slab.right - 0.5f, slab.bottom - 0.5f),
           WithAlpha(Rgb(0xFFFFFF), g_pal.dark ? 0.10f : 0.55f), 20, 1);

    float x = left + padX;
    for (size_t i = 0; i < items.size(); i++) {
        auto &it = items[i];
        if (it.separatorBefore) {
            Line(x + 2, baseline - base * 0.68f, x + 2, baseline, g_pal.separator);
            x += 10;
        }
        float s = it.curSize;
        float iconY = baseline - s;
        float bounce = it.bounce > 0 ? sinf(it.bounce * 3.14159f) * 18 : 0;
        D2D1_RECT_F ir = D2D1::RectF(x, iconY - bounce, x + s, iconY + s - bounce);
        if (it.icon) Bitmap(it.icon, ir);
        else { Fill(ir, g_pal.hover, s * 0.22f); Text(it.label.substr(0, 1), ir, g_pal.text, s * 0.4f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER); }
        if (it.running)
            Circle(x + s / 2, h - 5, 2.2f, g_pal.textDim, true);
        x += s + gap;
    }
    // name tag above the hovered item
    if (hot >= 0 && hot < (int)items.size() && hoverX >= 0) {
        auto &it = items[hot];
        float cx = 0, acc = left + padX;
        for (int i = 0; i <= hot; i++) { if (items[i].separatorBefore) acc += 10; if (i == hot) cx = acc + items[i].curSize / 2; acc += items[i].curSize + gap; }
        float tw = TextWidth(it.label, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        float lx = cx - tw / 2 - 8, ly = std::max(1.f, baseline - it.curSize - 30);
        Fill(D2D1::RectF(lx, ly, lx + tw + 16, ly + 24), g_pal.panelStrong, 7);
        Stroke(D2D1::RectF(lx + 0.5f, ly + 0.5f, lx + tw + 15.5f, ly + 23.5f), g_pal.separator, 7, 1);
        Text(it.label, D2D1::RectF(lx, ly, lx + tw + 16, ly + 24), g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

int Dock::HitItem(float x) {
    float acc = padX;
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].separatorBefore) acc += 10;
        if (x >= acc - gap / 2 && x < acc + items[i].curSize + gap / 2) return (int)i;
        acc += items[i].curSize + gap;
    }
    return -1;
}

void Dock::OnMouse(UINT msg, float x, float y, WPARAM) {
    float left = (W() - contentW) / 2;
    if (left < 0) left = 0;
    float cx = x - left;               // content-space coordinate

    if (msg == WM_LBUTTONDOWN) {
        int h = HitItem(cx);
        dragIndex = (h >= 0 && items[h].pinned) ? h : -1;   // only pinned icons reorder
        dragGrabX = cx;
        dragging = false;
        hoverX = cx; hot = h; Relayout(true);
        return;
    }
    if (msg == WM_MOUSEMOVE) {
        hoverX = cx;
        if (dragIndex >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000)) {
            if (!dragging && fabsf(cx - dragGrabX) > 8) dragging = true;
            if (dragging) {
                int target = HitItem(cx);
                if (target >= 0 && target < (int)items.size() && items[target].pinned && target != dragIndex) {
                    DockItem moved = items[dragIndex];
                    items.erase(items.begin() + dragIndex);
                    items.insert(items.begin() + target, moved);
                    dragIndex = target;
                }
                hot = dragIndex;
                Relayout(true);
                return;
            }
        }
        hot = HitItem(cx);
        Relayout(true);
        return;
    }
    if (msg == WM_LBUTTONUP) {
        if (dragging) {   // persist the new pinned order
            std::vector<std::wstring> order;
            for (auto &it : items) if (it.pinned) order.push_back(it.id);
            g_set.pinned = order;
            SaveSettings();
            dragging = false; dragIndex = -1;
            Rebuild();
            return;
        }
        int h = HitItem(cx);
        dragIndex = -1;
        Activate(h, false);
        return;
    }
    if (msg == WM_RBUTTONUP) { Activate(HitItem(cx), true); return; }
}

void Dock::Activate(int i, bool rightClick) {
    if (i < 0 || i >= (int)items.size()) return;
    auto &it = items[i];
    if (it.id == L"::ggsettings") { SettingsOpen(); return; }
    if (it.id == L"::downloads") {
        if (!rightClick) { Downloads(); return; }
        // macOS-style stack options
        std::vector<MenuItem> m;
        MenuItem hdr; hdr.text = L"Display as"; hdr.header = true; m.push_back(hdr);
        const wchar_t *names[] = {L"Fan", L"Grid", L"Automatic"};
        for (int k = 0; k < 3; k++) {
            MenuItem mi; mi.text = names[k]; mi.checked = (g_set.dlStyle == k);
            mi.action = [k]() { g_set.dlStyle = k; SaveSettings(); };
            m.push_back(mi);
        }
        m.push_back(MenuItem::Sep());
        m.push_back({L"Open Downloads", L"", []() { ShellOpen(KnownFolder(FOLDERID_Downloads)); }});
        RECT a; GetWindowRect(hwnd, &a);
        PopupMenu(m, a.left + (int)(x_of(i) * scale), a.top, hwnd);
        return;
    }
    if (it.id == L"::trash") {
        if (rightClick) {
            std::vector<MenuItem> m = {
                {L"Open Recycle Bin", L"", []() { ShellOpen(L"shell:RecycleBinFolder"); }},
                {L"Empty Recycle Bin…", L"", []() { ShowConfirm(L"Empty Trash", L"Permanently delete everything in the Recycle Bin?", L"Empty",
                    []() { SHEmptyRecycleBinW(nullptr, nullptr, 0); if (g_dock) g_dock->Rebuild(); }); }},
            };
            RECT a; GetWindowRect(hwnd, &a);
            PopupMenu(m, a.left + (int)(x_of(i) * scale), a.top, hwnd);
        } else ShellOpen(L"shell:RecycleBinFolder");
        return;
    }
    if (rightClick) {
        std::vector<MenuItem> m;
        if (it.running) {
            m.push_back({L"Show All Windows", L"", [this, i]() { for (HWND w : items[i].windows) ActivateWindow(w); }});
            for (HWND w : it.windows) {
                int n = GetWindowTextLengthW(w);
                std::wstring t(n + 1, 0);
                GetWindowTextW(w, &t[0], n + 1);
                t.resize(n);
                if (t.size() > 40) t = t.substr(0, 39) + L"…";
                m.push_back({t, L"", [w]() { ActivateWindow(w); }});
            }
            m.push_back(MenuItem::Sep());
            m.push_back({L"Quit", L"", [this, i]() { for (HWND w : items[i].windows) PostMessageW(w, WM_CLOSE, 0, 0); }});
            m.push_back(MenuItem::Sep());
        }
        std::wstring id = it.id;
        bool pinned = it.pinned;
        m.push_back({pinned ? L"Remove from Dock" : L"Keep in Dock", L"", [id, pinned]() {
            if (pinned) g_set.pinned.erase(std::remove(g_set.pinned.begin(), g_set.pinned.end(), id), g_set.pinned.end());
            else g_set.pinned.push_back(id);
            SaveSettings();
            if (g_dock) g_dock->Rebuild();
        }});
        m.push_back({L"Open", L"", [id]() { LaunchApp(id); }});
        RECT a; GetWindowRect(hwnd, &a);
        PopupMenu(m, a.left + (int)(x_of(i) * scale), a.top, hwnd);
        return;
    }
    // left click: focus if running (cycle windows), else launch with a bounce
    if (it.running && !it.windows.empty()) {
        HWND fg = GetForegroundWindow();
        HWND next = it.windows[0];
        for (size_t k = 0; k < it.windows.size(); k++)
            if (it.windows[k] == fg) { next = it.windows[(k + 1) % it.windows.size()]; break; }
        ActivateWindow(next);
    } else {
        LaunchApp(it.id);
        it.bounce = 0.001f;
    }
}

float Dock::x_of(int i) {
    float left = (W() - contentW) / 2;
    if (left < 0) left = 0;
    float acc = left + padX;
    for (int k = 0; k < i; k++) { if (items[k].separatorBefore) acc += 10; acc += items[k].curSize + gap; }
    return acc + items[i].curSize / 2;
}

void Dock::Downloads() {
    RECT a;
    GetWindowRect(hwnd, &a);
    RECT anchor = {a.left + (int)(x_of_downloads() * scale), a.top, a.left + (int)(x_of_downloads() * scale), a.top};
    extern void DownloadsFanShow(RECT, const std::vector<std::wstring> &);
    DownloadsFanShow(anchor, RecentDownloads(12));
}
float Dock::x_of_downloads() {
    for (size_t i = 0; i < items.size(); i++) if (items[i].id == L"::downloads") return x_of((int)i);
    return W() / 2;
}

// --------------------------------------------------- appbar reservation ---
// Reserve the bottom strip only when the Dock never hides; with intellihide or
// auto-hide the Dock floats so maximized windows get the full height.
static void ReserveDock(Dock *d, bool add) {
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = d->hwnd;
    abd.uCallbackMessage = WM_GG_TRAY;
    if (!add) { SHAppBarMessage(ABM_REMOVE, &abd); return; }
    if (g_set.dockHide != 0) return;
    SHAppBarMessage(ABM_NEW, &abd);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(MonitorFromWindow(d->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT wr; GetWindowRect(d->hwnd, &wr);
    abd.uEdge = ABE_BOTTOM;
    abd.rc = mi.rcMonitor;
    abd.rc.top = wr.top - (int)(6 * d->scale);
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc.top = abd.rc.bottom - (wr.bottom - wr.top) - (int)(12 * d->scale);
    SHAppBarMessage(ABM_SETPOS, &abd);
}

void CALLBACK DockIntelliTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (g_dock) g_dock->IntelliTick();
}

void DockStart() {
    if (g_dock || !g_set.dock) return;
    g_dock = new Dock();
    g_dock->base = g_set.dockSize;
    g_dock->maxs = g_set.dockMagnify ? g_set.dockMagnifySize : g_set.dockSize;
    MONITORINFO mi = {sizeof(mi)};
    HMONITOR pm = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfo(pm, &mi);
    RECT r = {mi.rcWork.left, mi.rcWork.bottom - 100, mi.rcWork.left + 400, mi.rcWork.bottom};
    g_dock->Create(Surface::Layered, L"GoldenGateDock", WS_EX_TOPMOST | WS_EX_NOACTIVATE, r, nullptr);
    // start hidden if auto-hide, else shown; intellihide decides each tick
    g_dock->reveal = g_dock->revealTarget = (g_set.dockHide == 2) ? 0.f : 1.f;
    g_dock->Rebuild();
    g_dock->Show(true, false);
    SetWindowPos(g_dock->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ReserveDock(g_dock, true);
    g_dock->intelliTimer = SetTimer(nullptr, 0, 30, DockIntelliTimer);
}

void DockStop() {
    if (!g_dock) return;
    if (g_dock->intelliTimer) KillTimer(nullptr, g_dock->intelliTimer);
    ReserveDock(g_dock, false);
    g_dock->Destroy();
    delete g_dock;
    g_dock = nullptr;
}

void DockRefresh() {
    if (g_dock) g_dock->Rebuild();
}

// Rebuild to pick up apps opened/closed without a foreground change, but not
// while the user is interacting (hovering, or a Dock menu is open).
void DockPeriodicRefresh() {
    if (!g_dock || !g_dock->hwnd) return;
    if (g_dock->hoverX >= 0 || PopupMenuOpen()) return;
    g_dock->Rebuild();
}

void DockReassert() {
    if (g_dock && g_dock->hwnd)
        SetWindowPos(g_dock->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

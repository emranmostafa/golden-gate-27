// Launchpad (full-screen app grid with search) and Spotlight (a search box
// that finds apps, files via the shell, and offers a web search). Both list
// apps from shell:AppsFolder.
#include "common.h"

// ================================================================ Launchpad =
struct LPItem { std::wstring id, name; IWICBitmapSource *icon; D2D1_RECT_F r; };
struct Launchpad : Surface {
    std::vector<LPItem> all, shown;
    std::wstring query;
    int hot = -1;
    float t = 0;
    UINT_PTR anim = 0;
    void Build();
    void Filter();
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnKey(UINT vk) override;
    void OnActivate(bool a) override { if (!a) Close(); }
    LRESULT OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) override;
    void Close();
    bool closing = false;   // hiding us re-enters Close via OnActivate
};
static Launchpad *g_lp = nullptr;

void Launchpad::Build() {
    all.clear();
    for (auto &a : AllApps()) {
        if (a.name.empty()) continue;
        // skip obvious non-apps
        std::wstring ln = Lower(a.name);
        if (ln.find(L"uninstall") != std::wstring::npos) continue;
        LPItem it;
        it.id = a.id;
        it.name = a.name;
        it.icon = AppIcon(a.id, 128);
        all.push_back(it);
    }
    std::sort(all.begin(), all.end(), [](const LPItem &a, const LPItem &b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    Filter();
}

void Launchpad::Filter() {
    shown.clear();
    std::wstring q = Lower(query);
    for (auto &it : all)
        if (q.empty() || Lower(it.name).find(q) != std::wstring::npos) shown.push_back(it);
    hot = shown.empty() ? -1 : 0;
}

void Launchpad::OnPaint() {
    Fill(D2D1::RectF(0, 0, W(), H()), WithAlpha(g_pal.dark ? Rgb(0x0A0A0F) : Rgb(0xECECF2), 0.4f + 0.45f * t), 0);
    float w = W();
    // search box
    float boxW = 360, boxH = 36, bx = (w - boxW) / 2, by = 50;
    Fill(D2D1::RectF(bx, by, bx + boxW, by + boxH), g_pal.panelStrong, boxH / 2);
    Text(L"", D2D1::RectF(bx + 12, by, bx + 40, by + boxH), g_pal.textDim, 14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    Text(query.empty() ? L"Search" : query, D2D1::RectF(bx + 42, by, bx + boxW - 12, by + boxH),
         query.empty() ? g_pal.textFaint : g_pal.text, 14);
    // grid
    int cols = 7;
    float cellW = 130 * scale / scale, cellH = 118;
    float gridW = cols * cellW;
    float startX = (w - gridW) / 2, startY = by + boxH + 30;
    int n = (int)shown.size();
    for (int i = 0; i < n; i++) {
        int c = i % cols, r = i / cols;
        float cx = startX + c * cellW + cellW / 2;
        float cy = startY + r * cellH;
        if (cy + cellH > H()) break;
        float is = 60;
        D2D1_RECT_F ir = D2D1::RectF(cx - is / 2, cy, cx + is / 2, cy + is);
        if (i == hot) Fill(D2D1::RectF(cx - cellW / 2 + 6, cy - 8, cx + cellW / 2 - 6, cy + cellH - 18), g_pal.hover, 14);
        if (shown[i].icon) Bitmap(shown[i].icon, ir, t);
        std::wstring nm = shown[i].name;
        if (nm.size() > 16) nm = nm.substr(0, 15) + L"…";
        Text(nm, D2D1::RectF(cx - cellW / 2 + 2, cy + is + 4, cx + cellW / 2 - 2, cy + is + 26),
             WithAlpha(g_pal.text, t), 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        shown[i].r = D2D1::RectF(cx - cellW / 2 + 6, cy - 8, cx + cellW / 2 - 6, cy + cellH - 18);
    }
}

void Launchpad::OnMouse(UINT msg, float x, float y, WPARAM) {
    int h = -1;
    for (int i = 0; i < (int)shown.size(); i++)
        if (x >= shown[i].r.left && x <= shown[i].r.right && y >= shown[i].r.top && y <= shown[i].r.bottom) { h = i; break; }
    if (h != hot) { hot = h; Invalidate(); }
    if (msg == WM_LBUTTONUP) {
        if (h >= 0) { std::wstring id = shown[h].id; Close(); LaunchApp(id); }
        else Close();
    }
}

void Launchpad::OnKey(UINT vk) {
    if (vk == VK_ESCAPE) { Close(); return; }
    if (vk == VK_RETURN) { if (hot >= 0 && hot < (int)shown.size()) { std::wstring id = shown[hot].id; Close(); LaunchApp(id); } return; }
    if (vk == VK_BACK) { if (!query.empty()) { query.pop_back(); Filter(); Invalidate(); } return; }
    if (vk == VK_LEFT && hot > 0) { hot--; Invalidate(); }
    if (vk == VK_RIGHT && hot < (int)shown.size() - 1) { hot++; Invalidate(); }
    if (vk == VK_DOWN && hot + 7 < (int)shown.size()) { hot += 7; Invalidate(); }
    if (vk == VK_UP && hot - 7 >= 0) { hot -= 7; Invalidate(); }
}

LRESULT Launchpad::OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) {
    if (m == WM_CHAR) {
        wchar_t c = (wchar_t)wp;
        if (c >= L' ') { query += c; Filter(); Invalidate(); handled = true; return 0; }
    }
    handled = false;
    return 0;
}

void CALLBACK LPAnim(HWND, UINT, UINT_PTR id, DWORD) {
    if (!g_lp) { KillTimer(nullptr, id); return; }
    g_lp->t += 0.16f;
    if (g_lp->t >= 1) { g_lp->t = 1; KillTimer(nullptr, id); g_lp->anim = 0; }
    g_lp->Invalidate();
}

void Launchpad::Close() {
    if (closing) return;
    closing = true;
    if (anim) { KillTimer(nullptr, anim); anim = 0; }
    Show(false);
    Destroy();
    g_lp = nullptr;
    delete this;
}

void LaunchpadToggle() {
    if (g_lp) { g_lp->Close(); return; }
    AppsRefreshAsync();   // newly installed apps appear next time it opens
    g_lp = new Launchpad();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    g_lp->Create(Surface::Glass, L"GoldenGateLaunchpad", WS_EX_TOPMOST, mi.rcMonitor, nullptr, false);
    g_lp->Build();
    g_lp->Show(true, true);
    ForceForeground(g_lp->hwnd);
    g_lp->anim = SetTimer(nullptr, 0, 16, LPAnim);
}

// ================================================================ Spotlight =
struct Result { std::wstring title, sub, action; int kind; IWICBitmapSource *icon; };
struct Spotlight : Surface {
    std::wstring query;
    std::vector<Result> results;
    int sel = 0;
    bool closing = false;
    int openTop = 0;        // where the search box sits (screen px)
    // typing cursor, blinking at the system rate; the same timer closes Spotlight
    // when another window takes focus (in case it never got focus to lose)
    bool caretOn = true;
    UINT_PTR tick = 0;
    ULONGLONG lastBlink = 0;
    HWND fgAtOpen = nullptr;
    void Search();
    void OnPaint() override;
    void OnKey(UINT vk) override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnActivate(bool a) override { if (!a) Close(); }
    LRESULT OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) override;
    void Run(int i);
    void Close();
    void Resize();
    void ShowMenu(int i, int x, int y);
    bool menuOpen = false;   // we opened a right-click menu (close it with us)
};
static Spotlight *g_sl = nullptr;

void SpotlightAppsChanged() {
    if (g_sl && !g_sl->closing && !(g_sl->menuOpen && PopupMenuOpen())) g_sl->Search();
}

// How well an app matches what was typed: lower is better, -1 is no match.
// Like Start's search it looks at the name (whole, start, start of any word, the
// initials) and at the program's file name, so "code", "vsc", "cmd", "regedit"
// and "studio code" all find their app.
static int MatchScore(const AppEntry &a, const std::wstring &q) {
    std::wstring n = Lower(a.name);
    if (n == q) return 0;
    std::wstring stem = a.exe;   // "cmd.exe" -> "cmd"
    if (stem.size() > 4) stem.resize(stem.size() - 4);
    // typing the exact program name ("cmd", "regedit") finds that program first,
    // as Start does: Command Prompt before Git CMD
    if (!stem.empty() && stem == q) return 1;
    if (n.compare(0, q.size(), q) == 0) return 1;
    for (size_t i = 1; i + q.size() <= n.size(); i++)
        if (!iswalnum(n[i - 1]) && n.compare(i, q.size(), q) == 0) return 2;
    if (!stem.empty() && stem.compare(0, q.size(), q) == 0) return 3;
    if (q.size() >= 2) {
        std::wstring initials;
        bool start = true;
        for (wchar_t c : n) {
            if (iswalnum(c)) { if (start) initials += c; start = false; }
            else start = true;
        }
        if (initials.compare(0, q.size(), q) == 0) return 3;
    }
    if (n.find(q) != std::wstring::npos) return 4;
    // several words, each somewhere in the name: "studio code"
    if (q.find(L' ') != std::wstring::npos) {
        bool all = true, any = false;
        size_t p = 0;
        while (p < q.size() && all) {
            size_t e = q.find(L' ', p);
            if (e == std::wstring::npos) e = q.size();
            if (e > p) { any = true; all = n.find(q.substr(p, e - p)) != std::wstring::npos; }
            p = e + 1;
        }
        if (all && any) return 5;
    }
    if (q.size() >= 3 && !stem.empty() && stem.find(q) != std::wstring::npos) return 6;
    return -1;
}

void Spotlight::Search() {
    results.clear();
    std::wstring q = Lower(query);
    while (!q.empty() && q.back() == L' ') q.pop_back();
    while (!q.empty() && q.front() == L' ') q.erase(q.begin());
    if (!q.empty()) {
        // rank every match, best first; the old search took the first 6 in
        // alphabetical order, so a good match late in the alphabet never showed
        struct Hit { int score; const AppEntry *a; };
        std::vector<Hit> hits;
        std::set<std::wstring> seen;   // one row per name (the shell can list an app twice)
        for (auto &a : AllApps()) {
            int s = MatchScore(a, q);
            if (s >= 0) hits.push_back({s, &a});
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit &x, const Hit &y) {
            if (x.score != y.score) return x.score < y.score;
            if (x.a->name.size() != y.a->name.size()) return x.a->name.size() < y.a->name.size();
            return _wcsicmp(x.a->name.c_str(), y.a->name.c_str()) < 0;
        });
        for (auto &h : hits) {
            if (!seen.insert(Lower(h.a->name)).second) continue;
            Result r;
            r.title = h.a->name;
            r.sub = L"Application";
            r.action = h.a->id;
            r.kind = 0;
            r.icon = AppIcon(h.a->id, 64);
            results.push_back(r);
            if (results.size() >= 7) break;   // 7 apps + the web row fill the 8 rows
        }
        // a web search and a direct "open" fallback
        Result web;
        web.title = L"Search the web for “" + query + L"”";
        web.sub = L"Web";
        web.action = L"https://www.bing.com/search?q=" + query;
        web.kind = 1;
        web.icon = nullptr;
        results.push_back(web);
    }
    sel = 0;
    Resize();
}

void Spotlight::Resize() {
    float rowH = 52, headH = 56;
    int rows = std::min((int)results.size(), 8);
    float h = headH + (rows > 0 ? rows * rowH + 8 : 0);
    RECT wr; GetWindowRect(hwnd, &wr);
    float sc = scale;
    int ph = (int)(h * sc);
    // the box stays put and results open below it, unless they'd run off the screen
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    int top = std::max((int)mi.rcWork.top, std::min(openTop, (int)mi.rcWork.bottom - ph));
    SetWindowPos(hwnd, HWND_TOPMOST, wr.left, top, wr.right - wr.left, ph, SWP_NOACTIVATE);
    Invalidate();
}

void Spotlight::OnPaint() {
    float w = W();
    Fill(D2D1::RectF(0, 0, w, H()), g_pal.panel, 0);
    Fill(D2D1::RectF(0, 0, w, H()), WithAlpha(g_pal.panelStrong, 0.3f), 0);
    Text(L"", D2D1::RectF(18, 0, 50, 56), g_pal.textDim, 20, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    Text(query.empty() ? L"Spotlight Search" : query, D2D1::RectF(54, 0, w - 16, 56),
         query.empty() ? g_pal.textFaint : g_pal.text, 20, DWRITE_FONT_WEIGHT_NORMAL);
    if (caretOn) {   // the typing cursor: after the text (before the placeholder)
        float cx = std::min(w - 18, 54 + (query.empty() ? 0.f : TextWidth(query, 20)) + 1);
        Fill(D2D1::RectF(cx, 16, cx + 2, 40), g_pal.accent, 1);
    }
    float y = 56;
    for (int i = 0; i < (int)results.size() && i < 8; i++) {
        float rowH = 52;
        if (i == sel) Fill(D2D1::RectF(8, y + 3, w - 8, y + rowH - 3), g_pal.accent, 10);
        D2D1_COLOR_F fg = i == sel ? g_pal.accentText : g_pal.text;
        if (results[i].icon) Bitmap(results[i].icon, D2D1::RectF(16, y + 8, 52, y + 44));
        else { Text(results[i].kind == 1 ? L"" : L"", D2D1::RectF(18, y, 54, y + rowH), fg, 18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true); }
        Text(results[i].title, D2D1::RectF(60, y + 7, w - 16, y + 30), fg, 14.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        Text(results[i].sub, D2D1::RectF(60, y + 28, w - 16, y + 48), WithAlpha(fg, 0.65f), 11.5f);
        y += rowH;
    }
}

void Spotlight::OnMouse(UINT msg, float x, float y, WPARAM) {
    if (y < 56) return;
    int i = (int)((y - 56) / 52);
    if (i >= 0 && i < (int)results.size()) {
        if (i != sel) { sel = i; Invalidate(); }
        if (msg == WM_LBUTTONUP) Run(i);
        else if (msg == WM_RBUTTONUP) {
            POINT p;
            GetCursorPos(&p);
            ShowMenu(i, p.x, p.y);
        }
    }
}

// Right-click on an app: Windows' own menu for it (Run as administrator, Open
// file location, Uninstall, Pin to Start...), plus Keep in Dock. The menu never
// takes focus, so Spotlight stays open behind it until a command is chosen.
void Spotlight::ShowMenu(int i, int x, int y) {
    if (i < 0 || i >= (int)results.size() || results[i].kind != 0) return;
    std::wstring id = results[i].action;
    auto items = AppShellMenu(id, []() { if (g_sl) g_sl->Close(); });
    if (items.empty()) {   // the shell had no menu for it: at least offer Open
        MenuItem open;
        open.text = L"Open";
        open.action = [id]() { if (g_sl) g_sl->Close(); LaunchApp(id); };
        items.push_back(open);
    }
    bool pinned = std::find(g_set.pinned.begin(), g_set.pinned.end(), id) != g_set.pinned.end();
    items.push_back(MenuItem::Sep());
    MenuItem dock;
    dock.text = pinned ? L"Remove from Dock" : L"Keep in Dock";
    dock.action = [id, pinned]() {
        if (pinned) g_set.pinned.erase(std::remove(g_set.pinned.begin(), g_set.pinned.end(), id), g_set.pinned.end());
        else g_set.pinned.push_back(id);
        SaveSettings();
        DockRefresh();
    };
    items.push_back(dock);
    menuOpen = true;
    PopupMenu(items, x, y, hwnd, 200);
}

void Spotlight::OnKey(UINT vk) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (menuOpen && PopupMenuOpen()) {   // a key while the menu is up closes it first
        ClosePopupMenus();
        menuOpen = false;
        if (vk == VK_ESCAPE) return;
    }
    if (vk == VK_ESCAPE) { Close(); return; }
    // Ctrl+Shift+Enter runs the app as administrator, as in Start
    if (vk == VK_RETURN && ctrl && shift) {
        if (sel >= 0 && sel < (int)results.size() && results[sel].kind == 0) {
            std::wstring id = results[sel].action;
            Close();
            if (!InvokeAppVerb(id, L"runas")) MessageBeep(MB_ICONWARNING);   // a Store app can't be
        }
        return;
    }
    if (vk == VK_RETURN) { Run(sel); return; }
    // the menu key or Shift+F10 opens the selected app's menu under its row
    if (vk == VK_APPS || (vk == VK_F10 && shift)) {
        RECT wr;
        GetWindowRect(hwnd, &wr);
        ShowMenu(sel, wr.left + (int)(60 * scale), wr.top + (int)((56 + (sel + 1) * 52) * scale));
        return;
    }
    if (vk == VK_BACK) { if (!query.empty()) { query.pop_back(); caretOn = true; lastBlink = GetTickCount64(); Search(); } return; }
    if (vk == VK_DOWN && sel < (int)results.size() - 1) { sel++; Invalidate(); }
    if (vk == VK_UP && sel > 0) { sel--; Invalidate(); }
}

LRESULT Spotlight::OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) {
    if (m == WM_CHAR) {
        wchar_t c = (wchar_t)wp;
        if (c >= L' ') { query += c; caretOn = true; lastBlink = GetTickCount64(); Search(); handled = true; return 0; }
    }
    handled = false;
    return 0;
}

void Spotlight::Run(int i) {
    if (i < 0 || i >= (int)results.size()) return;
    std::wstring a = results[i].action;
    int kind = results[i].kind;
    Close();
    if (kind == 0) LaunchApp(a);
    else ShellOpen(a);
}

// Close once. Hiding the focused window makes Windows send "deactivated" straight
// back into OnActivate -> Close, which deleted Spotlight twice and crashed on Esc.
void Spotlight::Close() {
    if (closing) return;
    closing = true;
    if (menuOpen) ClosePopupMenus();   // don't leave its right-click menu floating
    if (tick) { KillTimer(nullptr, tick); tick = 0; }
    Destroy();          // detaches us from the window first, so no re-entry
    g_sl = nullptr;
    delete this;
}

void SpotlightToggle() {
    if (g_sl) { g_sl->Close(); return; }
    // pick up apps installed or removed since the list was last read; this runs
    // in the background and the results refresh themselves when it lands
    AppsRefreshAsync();
    g_sl = new Spotlight();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    float sc = DpiScaleForMonitor(mon);
    int w = (int)(620 * sc), h = (int)(56 * sc);
    int cx = (mi.rcWork.left + mi.rcWork.right) / 2;
    int top = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;   // dead centre of the screen
    g_sl->openTop = top;
    RECT r = {cx - w / 2, top, cx + w / 2, top + h};
    g_sl->Create(Surface::Glass, L"GoldenGateSpotlight", WS_EX_TOPMOST, r, nullptr, true);
    g_sl->Show(true, true);
    ForceForeground(g_sl->hwnd);
    g_sl->fgAtOpen = GetForegroundWindow();
    g_sl->lastBlink = GetTickCount64();
    g_sl->tick = SetTimer(nullptr, 0, 50, [](HWND, UINT, UINT_PTR, DWORD) {
        if (!g_sl) return;
        // another window took focus (a click elsewhere): close, even if Spotlight
        // itself never got focus and so never sees "deactivated"
        HWND fg = GetForegroundWindow();
        if (fg && fg != g_sl->hwnd && fg != g_sl->fgAtOpen) { g_sl->Close(); return; }
        ULONGLONG now = GetTickCount64();
        UINT blink = GetCaretBlinkTime();
        if (blink != INFINITE && now - g_sl->lastBlink >= blink) {
            g_sl->caretOn = !g_sl->caretOn;
            g_sl->lastBlink = now;
            g_sl->Invalidate();
        }
    });
    g_sl->Invalidate();
}

// The Golden Gate settings window (one window for everything) and the small
// confirm / about dialogs. Drawn with our own toolkit so it matches the theme.
// Left: a sidebar of pages. Right: rows of controls (switches, segmented
// choices, sliders, dropdowns).
#include "common.h"

enum Ctl { C_SWITCH, C_SEG, C_SLIDER, C_CHOICE, C_COLOR, C_BUTTON, C_LABEL, C_HEADER };

struct Row {
    Ctl type;
    std::wstring label, sub;
    std::function<int()> get;              // current value
    std::function<void(int)> set;          // on change
    std::vector<std::wstring> options;     // for seg/choice
    int min = 0, max = 100;                // for slider
    std::function<void()> click;           // for button
    D2D1_RECT_F rect;                      // filled at paint
    D2D1_RECT_F ctlRect;
};

struct Page { std::wstring name, glyph; std::vector<Row> rows; };

struct SettingsWin : Surface {
    std::vector<Page> pages;
    int page = 0;
    int hotRow = -1;
    float scroll = 0;
    bool choiceOpen = false;
    int choiceRow = -1;
    void BuildPages();
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Show(false); }
    LRESULT OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) override;
    float sidebarW = 180;
    int trafficHot = -1;   // 0 close, 1 min, 2 zoom
    bool zoomed = false;
    RECT restoreRect = {};
};
static SettingsWin *g_settings = nullptr;
HWND SettingsHwnd() { return (g_settings && g_settings->Visible()) ? g_settings->hwnd : nullptr; }

// Apply settings without tearing down the whole UI on every click (that stole
// focus and made the window feel dead). Only restart what the change needs.
static void ApplyLive() {
    SaveSettings();
    UpdatePalette();
    MenuBarRefresh();
    DockRefresh();
    TaskbarApply();
    CaffeineSet(g_set.caffeine);
    if (g_settings && g_settings->hwnd) {
        g_settings->Invalidate();
        SetForegroundWindow(g_settings->hwnd);   // keep the window interactive
    }
}

// The Bibata folders are named Bibata-<shape>-<tint>-Regular; Settings offers the
// parts as separate controls.
static const int kCursorSizes[] = {32, 48, 64, 96};
static const wchar_t *const kCursorShapes[] = {L"Modern", L"Original"};
static const wchar_t *const kCursorTints[] = {L"Classic", L"Ice", L"Amber"};
static int CursorPart(const wchar_t *const *names, int n) {
    for (int i = 0; i < n; i++) if (g_set.cursor.find(names[i]) != std::wstring::npos) return i;
    return 0;
}
static void SetCursorParts(int shape, int tint) {
    if (shape < 0 || shape > 1 || tint < 0 || tint > 2) return;
    g_set.cursor = std::wstring(L"Bibata-") + kCursorShapes[shape] + L"-" + kCursorTints[tint] + L"-Regular";
    ThemeApplyCursors();
    ApplyLive();
}

void SettingsWin::BuildPages() {
    pages.clear();
    // -------- Appearance
    Page ap; ap.name = L"Appearance"; ap.glyph = L"";
    ap.rows.push_back({C_SEG, L"Theme", L"Light, Dark, or follow the time of day",
        []() { return g_set.style; }, [](int v) { g_set.style = v; ThemeApply(true); ApplyLive(); },
        {L"Light", L"Dark", L"Auto"}});
    ap.rows.push_back({C_COLOR, L"Accent colour", L"Buttons, highlights and selections",
        []() { return (int)g_set.accent; }, [](int v) { g_set.accent = (unsigned)v; ThemeApply(true); ApplyLive(); }});
    ap.rows.push_back({C_SWITCH, L"Golden Gate wallpaper", L"Change the desktop to match light and dark",
        []() { return g_set.applyWallpaper; }, [](int v) { g_set.applyWallpaper = v; if (v) ThemeApply(true); ApplyLive(); }});
    ap.rows.push_back({C_HEADER, L"Pointer"});
    ap.rows.push_back({C_SEG, L"Style", L"Bibata Modern has rounded tips; Original has sharp ones",
        []() { return CursorPart(kCursorShapes, 2); },
        [](int v) { SetCursorParts(v, CursorPart(kCursorTints, 3)); },
        {L"Modern", L"Original"}});
    ap.rows.push_back({C_SEG, L"Colour", L"Classic is black, Ice is white, Amber is gold",
        []() { return CursorPart(kCursorTints, 3); },
        [](int v) { SetCursorParts(CursorPart(kCursorShapes, 2), v); },
        {L"Classic", L"Ice", L"Amber"}});
    ap.rows.push_back({C_SEG, L"Size", L"32, 48, 64 or 96 pixels, all drawn from Bibata's own artwork",
        []() { for (int i = 0; i < 4; i++) if (kCursorSizes[i] == g_set.cursorSize) return i; return 0; },
        [](int v) { if (v >= 0 && v < 4) { g_set.cursorSize = kCursorSizes[v]; ThemeApplyCursors(); ApplyLive(); } },
        {L"Small", L"Medium", L"Large", L"Huge"}});
    pages.push_back(ap);

    // -------- Menu Bar
    Page mb; mb.name = L"Panel"; mb.glyph = L"";
    mb.rows.push_back({C_SWITCH, L"Show the panel", L"The macOS-style menu bar at the top",
        []() { return g_set.menubar; }, [](int v) { g_set.menubar = v; MenuBarStop(); if (v) MenuBarStart(); ApplyLive(); }});
    mb.rows.push_back({C_SEG, L"Glass style", L"Clear, Balanced or Tinted; the sliders below fine-tune it",
        []() { return g_set.glassPreset; }, [](int v) {
            g_set.glassPreset = v;
            if (v == 0) { g_set.panelGlass = 20; g_set.panelTint = 0; }
            else if (v == 1) { g_set.panelGlass = 45; g_set.panelTint = 15; }
            else { g_set.panelGlass = 72; g_set.panelTint = 55; }
            MenuBarRetint(); ApplyLive();
        }, {L"Clear", L"Balanced", L"Tinted"}});
    mb.rows.push_back({C_SLIDER, L"Opacity", L"Lower is clearer; higher is more solid",
        []() { return g_set.panelGlass; }, [](int v) { g_set.panelGlass = v; MenuBarRetint(); ApplyLive(); }, {}, 0, 100});
    mb.rows.push_back({C_SLIDER, L"Tint", L"How much the glass takes on your accent colour",
        []() { return g_set.panelTint; }, [](int v) { g_set.panelTint = v; MenuBarRetint(); ApplyLive(); }, {}, 0, 100});
    mb.rows.push_back({C_SLIDER, L"Height", L"Thickness of the panel",
        []() { return g_set.panelHeight; }, [](int v) { g_set.panelHeight = v; MenuBarStop(); MenuBarStart(); ApplyLive(); }, {}, 22, 40});
    mb.rows.push_back({C_HEADER, L"Clock"});
    mb.rows.push_back({C_SWITCH, L"24-hour clock", L"",
        []() { return g_set.clock24; }, [](int v) { g_set.clock24 = v; MenuBarRefresh(); ApplyLive(); }});
    mb.rows.push_back({C_SWITCH, L"Show the date", L"",
        []() { return g_set.clockDate; }, [](int v) { g_set.clockDate = v; MenuBarRefresh(); ApplyLive(); }});
    mb.rows.push_back({C_SWITCH, L"Show seconds", L"",
        []() { return g_set.clockSeconds; }, [](int v) { g_set.clockSeconds = v; ApplyLive(); }});
    mb.rows.push_back({C_SWITCH, L"Centre the clock", L"Off: the clock sits on the right. The calendar drops from the clock either way",
        []() { return g_set.clockCenter; }, [](int v) { g_set.clockCenter = v; MenuBarRefresh(); ApplyLive(); }});
    mb.rows.push_back({C_SWITCH, L"Battery percentage", L"",
        []() { return g_set.showBattery; }, [](int v) { g_set.showBattery = v; MenuBarRefresh(); ApplyLive(); }});
    pages.push_back(mb);

    // -------- Dock
    Page dk; dk.name = L"Dock"; dk.glyph = L"";
    dk.rows.push_back({C_SWITCH, L"Show the Dock", L"",
        []() { return g_set.dock; }, [](int v) { g_set.dock = v; DockStop(); if (v) DockStart(); ApplyLive(); }});
    dk.rows.push_back({C_SLIDER, L"Icon size", L"",
        []() { return g_set.dockSize; }, [](int v) { g_set.dockSize = v; g_set.dockMagnifySize = std::max(g_set.dockMagnifySize, v + 10); DockStop(); DockStart(); ApplyLive(); }, {}, 32, 84});
    dk.rows.push_back({C_SWITCH, L"Magnify on hover", L"",
        []() { return g_set.dockMagnify; }, [](int v) { g_set.dockMagnify = v; DockStop(); DockStart(); ApplyLive(); }});
    dk.rows.push_back({C_SEG, L"Hiding", L"Intellihide hides the Dock only when a window overlaps it; push the pointer to the bottom edge to show it",
        []() { return g_set.dockHide; }, [](int v) { g_set.dockHide = v; DockStop(); DockStart(); ApplyLive(); },
        {L"Off", L"Intellihide", L"Auto-hide"}});
    dk.rows.push_back({C_SEG, L"App icons", L"Original shows each app's own icon, like the taskbar; macOS style uses the bundled replacement set",
        []() { return g_set.iconStyle; }, [](int v) { g_set.iconStyle = v; DockStop(); DockStart(); ApplyLive(); },
        {L"Original", L"macOS style"}});
    dk.rows.push_back({C_SEG, L"Downloads stack", L"Fan arcs your newest files up out of the Dock; Automatic switches to a grid when there are many",
        []() { return g_set.dlStyle; }, [](int v) { g_set.dlStyle = v; ApplyLive(); },
        {L"Fan", L"Grid", L"Automatic"}});
    pages.push_back(dk);

    // -------- Features
    Page ft; ft.name = L"Features"; ft.glyph = L"";
    ft.rows.push_back({C_SWITCH, L"Mission Control", L"Top-left hot corner, or Ctrl+Up, shows all windows",
        []() { return g_set.missionControl; }, [](int v) { g_set.missionControl = v; ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Hot corner", L"Push into the top-left corner for Mission Control",
        []() { return g_set.hotCorner; }, [](int v) { g_set.hotCorner = v; ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Cover-flow Alt+Tab", L"Replace the Windows switcher",
        []() { return g_set.switcher; }, [](int v) { g_set.switcher = v; SwitcherStep(false); if (v) SwitcherStep(true); ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Launchpad", L"A full-screen grid of all apps (Ctrl+Space, or F4)",
        []() { return g_set.launchpad; }, [](int v) { g_set.launchpad = v; ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Spotlight search", L"A search box (double-tap Ctrl)",
        []() { return g_set.spotlight; }, [](int v) { g_set.spotlight = v; ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Clipboard history", L"In the menu bar",
        []() { return g_set.clipboard; }, [](int v) { g_set.clipboard = v; ClipboardStop(); if (v) ClipboardStart(); MenuBarRefresh(); ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Shake to find the pointer", L"Shake the mouse to highlight it",
        []() { return g_set.shakeFind; }, [](int v) { g_set.shakeFind = v; ApplyLive(); }});
    ft.rows.push_back({C_SWITCH, L"Caffeine", L"Keep the screen awake",
        []() { return g_set.caffeine; }, [](int v) { g_set.caffeine = v; CaffeineSet(v); MenuBarRefresh(); ApplyLive(); }});
    pages.push_back(ft);

    // -------- Windows
    Page wn; wn.name = L"Windows"; wn.glyph = L"";
    wn.rows.push_back({C_SEG, L"Windows taskbar", L"Hidden gives the most macOS-like desktop",
        []() { return g_set.taskbar; }, [](int v) { g_set.taskbar = v; TaskbarApply(); ApplyLive(); },
        {L"Shown", L"Auto-hide", L"Hidden"}});
    wn.rows.push_back({C_SWITCH, L"Start Golden Gate at login", L"",
        []() { return g_set.startAtLogin; }, [](int v) { g_set.startAtLogin = v; SaveSettings(); }});
    wn.rows.push_back({C_HEADER, L"About"});
    wn.rows.push_back({C_BUTTON, L"About Golden Gate", L"", nullptr, nullptr, {}, 0, 100, []() { ShowAbout(); }});
    wn.rows.push_back({C_BUTTON, L"Restore Windows look", L"Undo every change Golden Gate made", nullptr, nullptr, {}, 0, 100,
        []() { ShowConfirm(L"Restore Windows", L"Put the cursor, colours, wallpaper and taskbar back the way Windows had them? Golden Gate keeps running.", L"Restore",
            []() { ThemeRestoreAll(); TaskbarRestore(); }); }});
    pages.push_back(wn);
}

void SettingsWin::OnPaint() {
    float w = W(), h = H();
    Fill(D2D1::RectF(0, 0, w, h), g_pal.panelStrong, 0);
    // sidebar
    Fill(D2D1::RectF(0, 0, sidebarW, h), g_pal.panel, 0);
    Line(sidebarW, 0, sidebarW, h, g_pal.separator);
    // macOS traffic-light window buttons, top-left
    const unsigned tl[3] = {0xFF5F57, 0xFEBC2E, 0x28C840};
    const wchar_t *tlg[3] = {L"✕", L"−", L"↗"};
    for (int i = 0; i < 3; i++) {
        float cx = 20 + i * 20, cy = 22;
        Circle(cx, cy, 6.5f, Rgb(tl[i]), true);
        if (trafficHot >= 0)   // show glyphs when hovering the group
            Text(tlg[i], D2D1::RectF(cx - 7, cy - 7, cx + 7, cy + 7), Rgb(0x4D0000, 0.6f), 8.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    Text(L"Golden Gate", D2D1::RectF(18, 44, sidebarW - 10, 70), g_pal.text, 15, DWRITE_FONT_WEIGHT_BOLD);
    float y = 84;
    for (int i = 0; i < (int)pages.size(); i++) {
        D2D1_RECT_F r = D2D1::RectF(10, y, sidebarW - 10, y + 36);
        if (i == page) Fill(r, g_pal.accent, 8);
        D2D1_COLOR_F fg = i == page ? g_pal.accentText : g_pal.text;
        Text(pages[i].glyph, D2D1::RectF(20, y, 44, y + 36), fg, 14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        Text(pages[i].name, D2D1::RectF(48, y, sidebarW - 10, y + 36), fg, 13.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        y += 40;
    }
    // page content
    float cx = sidebarW + 28, cw = w - sidebarW - 56;
    y = 28 - scroll;
    Text(pages[page].name, D2D1::RectF(cx, y, w - 20, y + 36), g_pal.text, 22, DWRITE_FONT_WEIGHT_BOLD);
    y += 52;
    auto &rows = pages[page].rows;
    for (int i = 0; i < (int)rows.size(); i++) {
        Row &row = rows[i];
        if (row.type == C_HEADER) {
            Text(row.label, D2D1::RectF(cx, y + 10, w - 20, y + 34), g_pal.textFaint, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            y += 40;
            continue;
        }
        float rh = row.sub.empty() ? 52 : 60;
        row.rect = D2D1::RectF(cx, y, cx + cw, y + rh);
        Fill(D2D1::RectF(cx, y, cx + cw, y + rh - 6), g_pal.panel, 12);
        Text(row.label, D2D1::RectF(cx + 16, y + (row.sub.empty() ? 0 : 8), cx + cw - 220, y + (row.sub.empty() ? rh - 6 : 32)),
             g_pal.text, 13.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        if (!row.sub.empty())
            Text(row.sub, D2D1::RectF(cx + 16, y + 30, cx + cw - 180, y + rh - 8), g_pal.textDim, 11.5f);
        // control on the right
        float ctlR = cx + cw - 16;
        if (row.type == C_SWITCH) {
            bool on = row.get() != 0;
            float sw = 44, sh = 26, sx = ctlR - sw, sy = y + (rh - 6 - sh) / 2;
            Fill(D2D1::RectF(sx, sy, sx + sw, sy + sh), on ? g_pal.accent : g_pal.hover, sh / 2);
            Circle(on ? sx + sw - sh / 2 : sx + sh / 2, sy + sh / 2, sh / 2 - 3, Rgb(0xFFFFFF), true);
            row.ctlRect = D2D1::RectF(sx, sy, sx + sw, sy + sh);
        } else if (row.type == C_SEG) {
            float segW = 72, totW = segW * row.options.size(), sx = ctlR - totW, sy = y + (rh - 6 - 28) / 2;
            Fill(D2D1::RectF(sx, sy, sx + totW, sy + 28), g_pal.hover, 8);
            int cur = row.get();
            for (int o = 0; o < (int)row.options.size(); o++) {
                D2D1_RECT_F sr = D2D1::RectF(sx + o * segW, sy, sx + (o + 1) * segW, sy + 28);
                if (o == cur) Fill(D2D1::RectF(sr.left + 2, sr.top + 2, sr.right - 2, sr.bottom - 2), g_pal.panelStrong, 7);
                Text(row.options[o], sr, g_pal.text, 12, o == cur ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            row.ctlRect = D2D1::RectF(sx, sy, sx + totW, sy + 28);
        } else if (row.type == C_SLIDER) {
            float sw = 160, sx = ctlR - sw, sy = y + (rh - 6) / 2;
            Fill(D2D1::RectF(sx, sy - 3, sx + sw, sy + 3), g_pal.hover, 3);
            float frac = (float)(row.get() - row.min) / (row.max - row.min);
            Fill(D2D1::RectF(sx, sy - 3, sx + sw * frac, sy + 3), g_pal.accent, 3);
            Circle(sx + sw * frac, sy, 9, Rgb(0xFFFFFF), true);
            Circle(sx + sw * frac, sy, 9, g_pal.separator, false, 1);
            row.ctlRect = D2D1::RectF(sx, sy - 12, sx + sw, sy + 12);
        } else if (row.type == C_CHOICE) {
            float cwid = 180, sx = ctlR - cwid, sy = y + (rh - 6 - 28) / 2;
            Fill(D2D1::RectF(sx, sy, sx + cwid, sy + 28), g_pal.hover, 7);
            int cur = row.get();
            std::wstring val = (cur >= 0 && cur < (int)row.options.size()) ? row.options[cur] : L"";
            Text(val, D2D1::RectF(sx + 10, sy, sx + cwid - 24, sy + 28), g_pal.text, 12.5f);
            Text(L"", D2D1::RectF(sx, sy, sx + cwid - 8, sy + 28), g_pal.textDim, 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, true);
            row.ctlRect = D2D1::RectF(sx, sy, sx + cwid, sy + 28);
        } else if (row.type == C_COLOR) {
            static unsigned presets[] = {0x3478F6, 0x30D158, 0xFF9F0A, 0xFF375F, 0xBF5AF2, 0x64D2FF, 0xFFD60A, 0x8E8E93};
            float sw = 24, gap = 8, totW = (sw + gap) * 8 - gap, sx = ctlR - totW, sy = y + (rh - 6 - sw) / 2;
            for (int o = 0; o < 8; o++) {
                float px = sx + o * (sw + gap);
                Circle(px + sw / 2, sy + sw / 2, sw / 2, Rgb(presets[o]), true);
                if ((g_set.accent & 0xFFFFFF) == presets[o]) Circle(px + sw / 2, sy + sw / 2, sw / 2 + 3, g_pal.text, false, 2);
            }
            row.ctlRect = D2D1::RectF(sx, sy, sx + totW, sy + sw);
        } else if (row.type == C_BUTTON) {
            float bw = TextWidth(row.label, 13, DWRITE_FONT_WEIGHT_MEDIUM) + 32, sx = ctlR - bw, sy = y + (rh - 6 - 30) / 2;
            Fill(D2D1::RectF(sx, sy, sx + bw, sy + 30), g_pal.hover, 8);
            Text(row.label, D2D1::RectF(sx, sy, sx + bw, sy + 30), g_pal.text, 13, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
            // the button's label already shows; hide the left label for buttons
            Fill(D2D1::RectF(cx, y, cx + 200, y + rh - 6), g_pal.panel, 12);   // repaint over the left label area done above
            Text(row.label, D2D1::RectF(sx, sy, sx + bw, sy + 30), g_pal.text, 13, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
            row.ctlRect = D2D1::RectF(sx, sy, sx + bw, sy + 30);
            row.rect = D2D1::RectF(cx, y, cx + 10, y + 10);   // left label not clickable
        }
        y += rh;
    }
}

void SettingsWin::OnMouse(UINT msg, float x, float y, WPARAM wp) {
    if (msg == WM_MOUSEWHEEL) {
        scroll = std::max(0.f, scroll - GET_WHEEL_DELTA_WPARAM(wp) / 3.f);
        Invalidate();
        return;
    }
    bool down = (msg == WM_LBUTTONDOWN);
    bool drag = (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON));
    bool up = (msg == WM_LBUTTONUP);
    // traffic-light buttons (top-left): close / minimize / zoom
    int th = -1;
    for (int i = 0; i < 3; i++) { float cx = 20 + i * 20; if (fabsf(x - cx) < 9 && fabsf(y - 22) < 9) th = i; }
    if (th != trafficHot && y < 44) { trafficHot = th; Invalidate(); }
    if (up && th >= 0) {
        if (th == 0) Show(false);
        else if (th == 1) ShowWindow(hwnd, SW_MINIMIZE);
        else if (th == 2) {   // zoom: toggle between the window size and the work area
            MONITORINFO mi = {sizeof(mi)};
            GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            if (!zoomed) { GetWindowRect(hwnd, &restoreRect); zoomed = true; Move(mi.rcWork); }
            else { zoomed = false; Move(restoreRect); }
        }
        return;
    }
    if (down && x < sidebarW && y >= 72) {
        int i = (int)((y - 84) / 40);
        if (i >= 0 && i < (int)pages.size()) { page = i; scroll = 0; Invalidate(); }
        return;
    }
    auto &rows = pages[page].rows;
    for (auto &row : rows) {
        D2D1_RECT_F c = row.ctlRect;
        bool in = x >= c.left - 4 && x <= c.right + 4 && y >= c.top - 6 && y <= c.bottom + 6;
        if (!in) continue;
        if (row.type == C_SWITCH && up) { row.set(row.get() ? 0 : 1); Invalidate(); return; }
        if (row.type == C_SEG && up) {
            int o = (int)((x - c.left) / ((c.right - c.left) / row.options.size()));
            o = std::max(0, std::min((int)row.options.size() - 1, o));
            row.set(o); Invalidate(); return;
        }
        if (row.type == C_SLIDER && (down || drag)) {
            float frac = (x - c.left) / (c.right - c.left);
            frac = std::max(0.f, std::min(1.f, frac));
            row.set(row.min + (int)(frac * (row.max - row.min) + 0.5f));
            Invalidate(); return;
        }
        if (row.type == C_COLOR && up) {
            static unsigned presets[] = {0x3478F6, 0x30D158, 0xFF9F0A, 0xFF375F, 0xBF5AF2, 0x64D2FF, 0xFFD60A, 0x8E8E93};
            float sw = 24, gap = 8;
            int o = (int)((x - c.left) / (sw + gap));
            if (o >= 0 && o < 8) { row.set((int)presets[o]); Invalidate(); }
            return;
        }
        if (row.type == C_CHOICE && up) {
            std::vector<MenuItem> m;
            int cur = row.get();
            for (int o = 0; o < (int)row.options.size(); o++) {
                MenuItem it;
                it.text = row.options[o];
                it.checked = (o == cur);
                int oo = o;
                Row *rp = &row;
                it.action = [rp, oo]() { rp->set(oo); if (g_settings) g_settings->Invalidate(); };
                m.push_back(it);
            }
            RECT wr; GetWindowRect(hwnd, &wr);
            PopupMenu(m, wr.left + (int)(c.left * scale), wr.top + (int)(c.bottom * scale), hwnd, (c.right - c.left) / 1);
            return;
        }
        if (row.type == C_BUTTON && up) { if (row.click) row.click(); return; }
    }
}

LRESULT SettingsWin::OnMessage(UINT m, WPARAM wp, LPARAM lp, bool &handled) {
    if (m == WM_NCHITTEST) {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        float fx = pt.x / scale, fy = pt.y / scale;
        // drag by the top strip (right of the traffic lights), and the sidebar header
        bool onLight = fy < 44 && fx < 80;
        if (!onLight && ((fy < 44) || (fx < sidebarW && fy < 72))) { handled = true; return HTCAPTION; }
    }
    handled = false;
    return 0;
}

void SettingsOpen() {
    if (g_settings) { g_settings->Show(true, true); SetForegroundWindow(g_settings->hwnd); return; }
    g_settings = new SettingsWin();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    int w = (int)(760 * sc), h = (int)(560 * sc);
    int cx = (mi.rcWork.left + mi.rcWork.right) / 2, cy = (mi.rcWork.top + mi.rcWork.bottom) / 2;
    RECT r = {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
    g_settings->BuildPages();
    g_settings->Create(Surface::Glass, L"Golden Gate Settings", WS_EX_APPWINDOW, r, nullptr, true);
    // give it a normal resizable frame feel: caption drag via hit-test
    g_settings->Show(true, true);
    SetForegroundWindow(g_settings->hwnd);
}

// ----------------------------------------------------- confirm / about ----
struct Dialog : Surface {
    std::wstring title, body, ok;
    std::function<void()> onOk;
    bool isAbout = false;
    D2D1_RECT_F okR, cancelR;
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Close(); if (vk == VK_RETURN) { auto f = onOk; Close(); if (f) f(); } }
    void OnActivate(bool a) override { if (!a) Close(); }
    // Close once. Hiding/destroying the focused window makes Windows send a
    // "deactivated" message straight back into OnActivate -> Close; without the
    // guard that deleted the dialog twice and crashed (Empty Trash, Restart...).
    bool closing = false;
    void Close() {
        if (closing) return;
        closing = true;
        Destroy();          // detaches us from the window first, so no re-entry
        delete this;
    }
};

void Dialog::OnPaint() {
    float w = W(), h = H();
    Fill(D2D1::RectF(0, 0, w, h), g_pal.panelStrong, 14);
    float y = 28;
    if (isAbout) {
        // logo
        float cx = w / 2, cy = 70;
        Circle(cx, cy, 34, g_pal.text, true);
        Circle(cx, cy, 26, g_pal.panelStrong, true);
        Circle(cx, cy, 18, g_pal.text, true);
        Text(L"Golden Gate", D2D1::RectF(0, 118, w, 148), g_pal.text, 20, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(L"macOS-style desktop for Windows 11", D2D1::RectF(0, 148, w, 170), g_pal.textDim, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(body, D2D1::RectF(24, 184, w - 24, h - 60), g_pal.textDim, 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        float bw = 100, bx = (w - bw) / 2, by = h - 48;
        Fill(D2D1::RectF(bx, by, bx + bw, by + 32), g_pal.accent, 8);
        Text(L"OK", D2D1::RectF(bx, by, bx + bw, by + 32), g_pal.accentText, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        okR = D2D1::RectF(bx, by, bx + bw, by + 32);
        return;
    }
    Text(title, D2D1::RectF(24, y, w - 24, y + 30), g_pal.text, 16, DWRITE_FONT_WEIGHT_BOLD);
    y += 40;
    Text(body, D2D1::RectF(24, y, w - 24, h - 70), g_pal.textDim, 13, DWRITE_FONT_WEIGHT_NORMAL);
    float bw = 110, by = h - 52;
    cancelR = D2D1::RectF(w - bw * 2 - 32, by, w - bw - 24, by + 34);
    okR = D2D1::RectF(w - bw - 20, by, w - 20, by + 34);
    Fill(cancelR, g_pal.hover, 8);
    Text(L"Cancel", cancelR, g_pal.text, 13, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
    Fill(okR, g_pal.accent, 8);
    Text(ok, okR, g_pal.accentText, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Dialog::OnMouse(UINT msg, float x, float y, WPARAM) {
    if (msg != WM_LBUTTONUP) return;
    if (x >= okR.left && x <= okR.right && y >= okR.top && y <= okR.bottom) {
        auto f = onOk; Close(); if (f) f(); return;
    }
    if (!isAbout && x >= cancelR.left && x <= cancelR.right && y >= cancelR.top && y <= cancelR.bottom) { Close(); return; }
}

static void OpenDialog(const std::wstring &title, const std::wstring &body, const std::wstring &ok,
                       std::function<void()> onOk, bool about) {
    Dialog *d = new Dialog();
    d->title = title; d->body = body; d->ok = ok; d->onOk = onOk; d->isAbout = about;
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    int w = (int)((about ? 380 : 420) * sc), h = (int)((about ? 300 : 210) * sc);
    int cx = (mi.rcWork.left + mi.rcWork.right) / 2, cy = (mi.rcWork.top + mi.rcWork.bottom) / 2;
    RECT r = {cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
    d->Create(Surface::Glass, L"Golden Gate", WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, r, nullptr, true);
    d->Show(true, true);
    SetForegroundWindow(d->hwnd);
}

void ShowConfirm(const std::wstring &title, const std::wstring &body, const std::wstring &okText, std::function<void()> ok) {
    OpenDialog(title, body, okText, ok, false);
}
void ShowAbout() {
    OpenDialog(L"About", L"Version 2.3\n\nA desktop layer that runs beside Windows. Nothing in Windows is patched.\n\nIncludes Bibata cursors and MacTahoe icons (GPL-3.0) and the Inter font (OFL). Golden Gate is not affiliated with Apple.",
               L"OK", nullptr, true);
}

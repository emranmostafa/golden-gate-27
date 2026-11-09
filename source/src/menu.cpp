// The Golden Gate popup menu, drawn in our own style (used by the logo menu,
// app menus, status menus and right-click menus). One level of submenus.
#include "common.h"

struct MenuWin;
static std::vector<MenuWin *> g_open;
static HHOOK g_mouseHook = nullptr;

struct MenuWin : Surface {
    std::vector<MenuItem> items;
    std::vector<float> rowY;     // top of each row
    std::vector<float> rowH;
    float w = 0, rowHeight = 30, pad = 6, sep = 9;
    int hot = -1;
    MenuWin *parent = nullptr, *child = nullptr;
    int parentRow = -1;
    HWND returnTo = nullptr;

    void Build(std::vector<MenuItem> it, float minWidth) {
        items = std::move(it);
        float maxw = minWidth;
        for (auto &m : items) {
            if (m.separator) continue;
            float tw = TextWidth(m.text, 13.5f, m.header ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL) +
                       (m.shortcut.empty() ? 0 : TextWidth(m.shortcut, 12, DWRITE_FONT_WEIGHT_NORMAL) + 28) +
                       (m.sub.empty() ? 0 : 20) + 24 + (m.checked ? 20 : 0);
            maxw = std::max(maxw, tw + 28);
        }
        w = std::min(maxw, 420.f);
        float y = pad;
        rowY.clear();
        rowH.clear();
        for (auto &m : items) {
            rowY.push_back(y);
            float h = m.separator ? sep : (m.header ? 24 : rowHeight);
            rowH.push_back(h);
            y += h;
        }
        float h = y + pad;
        POINT pt;
        GetCursorPos(&pt);
        (void)pt;
        // caller positions with Open()
        pendingH = h;
    }
    float pendingH = 0;

    void OpenAt(int x, int y, HWND owner) {
        float sc = DpiScaleForMonitor(MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST));
        int pw = (int)(w * sc + 0.5f), ph = (int)(pendingH * sc + 0.5f);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST), &mi);
        if (x + pw > mi.rcWork.right) x = (parent ? parent->ScreenLeft() - pw : mi.rcWork.right - pw);
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y + ph > mi.rcWork.bottom) y = mi.rcWork.bottom - ph;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        RECT r = {x, y, x + pw, y + ph};
        Create(Glass, L"GoldenGateMenu", WS_EX_TOPMOST | WS_EX_NOACTIVATE, r, owner);
        Show(true, false);
        Invalidate();
    }
    int ScreenLeft() { RECT r; GetWindowRect(hwnd, &r); return r.left; }

    int RowAt(float y) {
        for (size_t i = 0; i < rowY.size(); i++)
            if (!items[i].separator && !items[i].header && y >= rowY[i] && y < rowY[i] + rowH[i]) return (int)i;
        return -1;
    }

    void OnPaint() override {
        Fill(D2D1::RectF(0, 0, W(), H()), g_pal.panelStrong, 0);
        Stroke(D2D1::RectF(0.5f, 0.5f, W() - 0.5f, H() - 0.5f), g_pal.separator, 0, 1);
        for (size_t i = 0; i < items.size(); i++) {
            auto &m = items[i];
            float y = rowY[i], h = rowH[i];
            if (m.separator) {
                Line(10, y + h / 2, W() - 10, y + h / 2, g_pal.separator);
                continue;
            }
            if (m.header) {
                Text(m.text, D2D1::RectF(14, y, W() - 10, y + h), g_pal.textFaint, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                continue;
            }
            D2D1_COLOR_F fg = m.disabled ? g_pal.textFaint : g_pal.text;
            if ((int)i == hot && !m.disabled) {
                Fill(D2D1::RectF(5, y + 1, W() - 5, y + h - 1), g_pal.accent, 7);
                fg = g_pal.accentText;
            }
            float tx = 14;
            if (m.checked) {
                Text(L"✓", D2D1::RectF(14, y, 34, y + h), fg, 13);
                tx = 36;
            }
            Text(m.text, D2D1::RectF(tx, y, W() - 40, y + h), fg, 13.5f);
            if (!m.shortcut.empty())
                Text(m.shortcut, D2D1::RectF(0, y, W() - 16, y + h),
                     (int)i == hot ? WithAlpha(fg, 0.8f) : g_pal.textFaint, 12, DWRITE_FONT_WEIGHT_NORMAL,
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
            if (!m.sub.empty())
                Text(L"›", D2D1::RectF(0, y, W() - 14, y + h), fg, 15, DWRITE_FONT_WEIGHT_NORMAL,
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }

    void OpenSub(int row) {
        if (child) { child->CloseTree(); delete child; child = nullptr; }
        if (row < 0 || items[row].sub.empty()) return;
        child = new MenuWin();
        child->parent = this;
        child->parentRow = row;
        child->Build(items[row].sub, 0);
        RECT r;
        GetWindowRect(hwnd, &r);
        child->OpenAt(r.right - 4, r.top + (int)(rowY[row] * scale), hwnd);
        g_open.push_back(child);
    }

    void OnMouse(UINT msg, float x, float y, WPARAM) override {
        int row = RowAt(y);
        if (row != hot) {
            hot = row;
            Invalidate();
            if (row >= 0 && !items[row].sub.empty()) OpenSub(row);
            else if (child) { child->CloseTree(); delete child; child = nullptr; }
        }
        if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
            if (row >= 0 && !items[row].disabled && items[row].sub.empty()) {
                auto act = items[row].action;
                CloseAll();
                if (act) act();
            }
        }
    }
    void OnMouseLeave() override {
        if (hot >= 0 && (!child)) { hot = -1; Invalidate(); }
    }

    void CloseTree() {
        if (child) { child->CloseTree(); delete child; child = nullptr; }
        g_open.erase(std::remove(g_open.begin(), g_open.end(), this), g_open.end());
        Destroy();
    }
    void CloseAll();
};

static MenuWin *g_root = nullptr;

void MenuWin::CloseAll() { ClosePopupMenus(); }

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN ||
                              wp == WM_NCLBUTTONDOWN || wp == WM_NCRBUTTONDOWN)) {
        POINT pt = ((MSLLHOOKSTRUCT *)lp)->pt;
        bool inside = false;
        for (auto *m : g_open) {
            RECT r;
            if (m->hwnd && GetWindowRect(m->hwnd, &r) && PtInRect(&r, pt)) { inside = true; break; }
        }
        if (!inside) {
            PostMessageW(g_main, WM_APP + 50, 0, 0);   // close on next loop
        }
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

bool PopupMenuOpen() { return !g_open.empty(); }

void ClosePopupMenus() {
    if (g_root) { g_root->CloseTree(); delete g_root; g_root = nullptr; }
    g_open.clear();
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
}

void PopupMenu(std::vector<MenuItem> items, int x, int y, HWND owner, float minWidth) {
    ClosePopupMenus();
    if (items.empty()) return;
    g_root = new MenuWin();
    g_root->Build(std::move(items), minWidth);
    g_root->OpenAt(x, y, owner);
    g_open.push_back(g_root);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, g_inst, 0);
}

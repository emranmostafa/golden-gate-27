// Clicking the clock/date opens this: Notification Center, like macOS — a
// right-side glass panel with a big clock + date header, a calendar widget
// card (today highlighted), and a notifications area with a button that opens
// Windows' own notification centre. Anchored top-right, slides under the bar.
#include "common.h"

struct NotifCenter : Surface {
    int month, year;    // month shown in the calendar widget
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnActivate(bool a) override { if (!a) Show(false); }
    void OnKey(UINT vk) override { if (vk == VK_ESCAPE) Show(false); }
    D2D1_RECT_F prevR, nextR, notifBtn;
};
static NotifCenter *g_nc = nullptr;

static int DaysInMonth(int m, int y) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m];
}

// a rounded frosted "widget" card
void NotifCenter::OnPaint() {
    float w = W(), pad = 16;
    // frosted backing, square-cornered so the DWM-rounded window isn't double-rounded
    Fill(D2D1::RectF(0, 0, w, H()), g_pal.dark ? Rgb(0x1C1C1E, 0.4f) : Rgb(0xF2F2F5, 0.4f), 0);
    SYSTEMTIME now;
    GetLocalTime(&now);
    const wchar_t *months[] = {L"January", L"February", L"March", L"April", L"May", L"June",
                              L"July", L"August", L"September", L"October", L"November", L"December"};
    const wchar_t *monShort[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    const wchar_t *dayNames[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};

    // ---- header: big time + full date
    int h12 = now.wHour % 12; if (h12 == 0) h12 = 12;
    wchar_t timeStr[32];
    swprintf(timeStr, 32, L"%d:%02d %ls", h12, now.wMinute, now.wHour < 12 ? L"AM" : L"PM");
    wchar_t dateStr[64];
    swprintf(dateStr, 64, L"%ls, %ls %d", dayNames[now.wDayOfWeek], months[now.wMonth - 1], now.wDay);
    float y = pad;
    Text(timeStr, D2D1::RectF(pad, y, w - pad, y + 34), g_pal.text, 28, DWRITE_FONT_WEIGHT_BOLD);
    y += 36;
    Text(dateStr, D2D1::RectF(pad, y, w - pad, y + 22), g_pal.textDim, 13.5f, DWRITE_FONT_WEIGHT_MEDIUM);
    y += 34;

    // ---- calendar widget card
    float cardX = pad, cardW = w - pad * 2;
    float calTop = y;
    // month title + arrows
    wchar_t mt[32];
    swprintf(mt, 32, L"%ls %d", monShort[month], year);
    Fill(D2D1::RectF(cardX, calTop, cardX + cardW, calTop + 232), g_pal.panelStrong, 16);
    float ix = cardX + 14;
    Text(mt, D2D1::RectF(ix, calTop + 12, cardX + cardW - 70, calTop + 36), g_pal.text, 15, DWRITE_FONT_WEIGHT_BOLD);
    prevR = D2D1::RectF(cardX + cardW - 64, calTop + 10, cardX + cardW - 40, calTop + 34);
    nextR = D2D1::RectF(cardX + cardW - 34, calTop + 10, cardX + cardW - 12, calTop + 34);
    Text(L"‹", prevR, g_pal.textDim, 19, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(L"›", nextR, g_pal.textDim, 19, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    // weekday row
    float gridTop = calTop + 44;
    float cellW = (cardW - 20) / 7, gx = cardX + 10;
    const wchar_t *dow[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
    for (int i = 0; i < 7; i++)
        Text(dow[i], D2D1::RectF(gx + i * cellW, gridTop, gx + (i + 1) * cellW, gridTop + 18),
             g_pal.textFaint, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    // days
    SYSTEMTIME first = {};
    first.wYear = year; first.wMonth = month + 1; first.wDay = 1;
    FILETIME ft; SystemTimeToFileTime(&first, &ft); FileTimeToSystemTime(&ft, &first);
    int startDow = first.wDayOfWeek, days = DaysInMonth(month, year);
    float rowTop = gridTop + 22, cellH = 25;
    bool thisMonth = (month == now.wMonth - 1 && year == now.wYear);
    for (int d = 1; d <= days; d++) {
        int idx = startDow + d - 1, col = idx % 7, row = idx / 7;
        float ccx = gx + col * cellW + cellW / 2, ccy = rowTop + row * cellH + cellH / 2;
        wchar_t ds[4]; swprintf(ds, 4, L"%d", d);
        if (thisMonth && d == now.wDay) {
            Circle(ccx, ccy, 12, g_pal.accent, true);
            Text(ds, D2D1::RectF(ccx - cellW / 2, ccy - cellH / 2, ccx + cellW / 2, ccy + cellH / 2), g_pal.accentText, 12.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            Text(ds, D2D1::RectF(ccx - cellW / 2, ccy - cellH / 2, ccx + cellW / 2, ccy + cellH / 2),
                 (col == 0 || col == 6) ? g_pal.textDim : g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    y = calTop + 232 + 14;

    // ---- notifications card
    Text(L"Notifications", D2D1::RectF(pad + 2, y, w - pad, y + 20), g_pal.textDim, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    y += 26;
    notifBtn = D2D1::RectF(pad, y, w - pad, y + 54);
    Fill(notifBtn, g_pal.panelStrong, 14);
    Text(L"", D2D1::RectF(pad + 14, y, pad + 46, y + 54), g_pal.accent, 18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    Text(L"Open Notification Center", D2D1::RectF(pad + 52, y + 9, w - pad - 10, y + 31), g_pal.text, 13.5f, DWRITE_FONT_WEIGHT_MEDIUM);
    Text(L"Your Windows alerts and quick settings", D2D1::RectF(pad + 52, y + 29, w - pad - 10, y + 48), g_pal.textFaint, 11.5f);
}

void NotifCenter::OnMouse(UINT msg, float x, float y, WPARAM) {
    if (msg != WM_LBUTTONUP) return;
    auto in = [&](D2D1_RECT_F r) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    if (in(prevR)) { if (--month < 0) { month = 11; year--; } Invalidate(); return; }
    if (in(nextR)) { if (++month > 11) { month = 0; year++; } Invalidate(); return; }
    if (in(notifBtn)) { Show(false); SendKeys({VK_LWIN, 'N'}); return; }
}

void CalendarToggle(RECT anchor) {
    if (g_nc && g_nc->Visible()) { g_nc->Show(false); return; }
    if (!g_nc) g_nc = new NotifCenter();
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    SYSTEMTIME st; GetLocalTime(&st);
    g_nc->month = st.wMonth - 1;
    g_nc->year = st.wYear;
    int w = (int)(340 * sc), h = (int)(470 * sc);
    // drop down from the clock (centred on the anchor), kept on screen
    int x = anchor.left - w / 2;
    if (x + w > mi.rcWork.right - 8) x = mi.rcWork.right - 8 - w;
    if (x < mi.rcWork.left + 8) x = mi.rcWork.left + 8;
    int top = anchor.bottom + (int)(4 * sc);
    RECT r = {x, top, x + w, top + h};
    if (!g_nc->hwnd) { g_nc->Create(Surface::Glass, L"GoldenGateNotifications", WS_EX_TOPMOST, r, nullptr, true); }
    else g_nc->Move(r);
    g_nc->Show(true, true);
    SetForegroundWindow(g_nc->hwnd);
    g_nc->Invalidate();
}

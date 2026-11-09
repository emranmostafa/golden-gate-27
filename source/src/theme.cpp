// Theme engine: light/dark, accent colour, wallpaper and cursors through the
// same settings Windows' own Personalization page writes, so every app that
// follows Windows follows Golden Gate. Everything it changes is saved first
// and put back by "GoldenGate.exe --restore" (run by the uninstaller).
#include "common.h"

static const wchar_t *K_PERS = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static const wchar_t *K_DWM = L"Software\\Microsoft\\Windows\\DWM";
static const wchar_t *K_ACCENT = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
static const wchar_t *K_CURSORS = L"Control Panel\\Cursors";
static const wchar_t *K_DESKTOP = L"Control Panel\\Desktop";

// registry value name -> Bibata file (from Bibata's install.inf)
static const struct { const wchar_t *reg, *file; } kCursorMap[] = {
    {L"Arrow", L"Pointer.cur"}, {L"Help", L"Help.cur"}, {L"AppStarting", L"Work.ani"}, {L"Wait", L"Busy.ani"},
    {L"Crosshair", L"Cross.cur"}, {L"precisionhair", L"Cross.cur"}, {L"IBeam", L"Text.cur"},
    {L"NWPen", L"Handwriting.cur"}, {L"No", L"Unavailable.cur"}, {L"SizeNS", L"Vert.cur"}, {L"SizeWE", L"Horz.cur"},
    {L"SizeNWSE", L"Dgn1.cur"}, {L"SizeNESW", L"Dgn2.cur"}, {L"SizeAll", L"Move.cur"}, {L"UpArrow", L"Alternate.cur"},
    {L"Hand", L"Link.cur"}, {L"Pin", L"Pin.cur"}, {L"Person", L"Person.cur"},
};

static std::wstring BackupPath() { return JoinPath(g_dataDir, L"windows-before-goldengate.ini"); }
static void BPut(const wchar_t *sec, const wchar_t *key, const std::wstring &v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), BackupPath().c_str());
}
static std::wstring BGet(const wchar_t *sec, const wchar_t *key, bool *found = nullptr) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(sec, key, L"\x01", buf, 2048, BackupPath().c_str());
    bool f = !(buf[0] == 1 && buf[1] == 0);
    if (found) *found = f;
    return f ? buf : L"";
}

bool IsDarkNow() {
    if (g_set.style == 0) return false;
    if (g_set.style == 1) return true;
    SYSTEMTIME st;
    GetLocalTime(&st);
    int h = st.wHour;
    if (g_set.lightHour < g_set.darkHour) return h < g_set.lightHour || h >= g_set.darkHour;
    return h >= g_set.darkHour && h < g_set.lightHour;
}

void ThemeBackupOnce() {
    if (FileExists(BackupPath())) return;
    DWORD v;
    if (RegGetDword(HKEY_CURRENT_USER, K_PERS, L"AppsUseLightTheme", v)) BPut(L"personalize", L"AppsUseLightTheme", std::to_wstring(v));
    if (RegGetDword(HKEY_CURRENT_USER, K_PERS, L"SystemUsesLightTheme", v)) BPut(L"personalize", L"SystemUsesLightTheme", std::to_wstring(v));
    if (RegGetDword(HKEY_CURRENT_USER, K_DWM, L"AccentColor", v)) BPut(L"dwm", L"AccentColor", std::to_wstring(v));
    if (RegGetDword(HKEY_CURRENT_USER, K_DWM, L"ColorizationColor", v)) BPut(L"dwm", L"ColorizationColor", std::to_wstring(v));
    if (RegGetDword(HKEY_CURRENT_USER, K_ACCENT, L"AccentColorMenu", v)) BPut(L"accent", L"AccentColorMenu", std::to_wstring(v));
    if (RegGetDword(HKEY_CURRENT_USER, K_ACCENT, L"StartColorMenu", v)) BPut(L"accent", L"StartColorMenu", std::to_wstring(v));
    BYTE pal[32];
    DWORD sz = sizeof(pal);
    if (RegGetValueW(HKEY_CURRENT_USER, K_ACCENT, L"AccentPalette", RRF_RT_REG_BINARY, nullptr, pal, &sz) == ERROR_SUCCESS) {
        std::wstring hex;
        wchar_t b[4];
        for (DWORD i = 0; i < sz; i++) { swprintf(b, 4, L"%02X", pal[i]); hex += b; }
        BPut(L"accent", L"AccentPalette", hex);
    }
    std::wstring s;
    if (RegGetString(HKEY_CURRENT_USER, K_DESKTOP, L"WallPaper", s)) BPut(L"desktop", L"WallPaper", s);
    if (RegGetString(HKEY_CURRENT_USER, K_CURSORS, nullptr, s)) BPut(L"cursors", L"(scheme)", s);
    for (auto &c : kCursorMap) {
        if (RegGetString(HKEY_CURRENT_USER, K_CURSORS, c.reg, s)) BPut(L"cursors", c.reg, s.empty() ? L"(empty)" : s);
    }
    APPBARDATA abd = {sizeof(abd)};
    UINT state = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
    BPut(L"taskbar", L"state", std::to_wstring(state));
    BPut(L"backup", L"made", L"1");
}

static void Broadcast(const wchar_t *what) {
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)what, SMTO_ABORTIFHUNG, 3000, &r);
}

static DWORD Abgr(unsigned rgb, unsigned a = 0xFF) {
    return (a << 24) | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}
static unsigned Shade(unsigned rgb, float f) {   // f > 0 lighter, < 0 darker
    int c[3] = {(int)(rgb >> 16) & 255, (int)(rgb >> 8) & 255, (int)rgb & 255};
    for (int &x : c) x = f >= 0 ? (int)(x + (255 - x) * f) : (int)(x * (1 + f));
    return (c[0] << 16) | (c[1] << 8) | c[2];
}

static void ApplyAccent() {
    unsigned a = g_set.accent & 0xFFFFFF;
    RegSetDword(HKEY_CURRENT_USER, K_PERS, L"EnableTransparency", 1);
    RegSetDword(HKEY_CURRENT_USER, K_DWM, L"AccentColor", Abgr(a));
    RegSetDword(HKEY_CURRENT_USER, K_DWM, L"ColorizationColor", 0xC4000000 | a);
    RegSetDword(HKEY_CURRENT_USER, K_DWM, L"ColorizationAfterglow", 0xC4000000 | a);
    RegSetDword(HKEY_CURRENT_USER, K_ACCENT, L"AccentColorMenu", Abgr(Shade(a, -0.15f)));
    RegSetDword(HKEY_CURRENT_USER, K_ACCENT, L"StartColorMenu", Abgr(Shade(a, -0.35f)));
    const float steps[8] = {0.55f, 0.35f, 0.18f, 0.f, -0.18f, -0.35f, -0.55f, -0.7f};
    BYTE pal[32];
    for (int i = 0; i < 8; i++) {
        unsigned c = Shade(a, steps[i]);
        pal[i * 4] = (c >> 16) & 255;
        pal[i * 4 + 1] = (c >> 8) & 255;
        pal[i * 4 + 2] = c & 255;
        pal[i * 4 + 3] = 0;
    }
    RegSetBinary(HKEY_CURRENT_USER, K_ACCENT, L"AccentPalette", pal, sizeof(pal));
    RegSetDword(HKEY_CURRENT_USER, K_PERS, L"ColorPrevalence", 0);
}

static void ApplyWallpaper(bool dark) {
    std::wstring wp = JoinPath(g_dir, dark ? L"wallpapers\\GoldenGate-Night.jpg" : L"wallpapers\\GoldenGate-Dawn.jpg");
    if (!FileExists(wp)) return;
    IDesktopWallpaper *dw = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dw)))) {
        dw->SetPosition(DWPOS_FILL);
        dw->SetWallpaper(nullptr, wp.c_str());
        dw->Release();
    } else {
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)wp.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
}

void ThemeApplyCursors() {
    if (!g_set.applyCursors) return;
    std::wstring dir = JoinPath(JoinPath(g_dir, L"cursors"), g_set.cursor);
    if (!FileExists(JoinPath(dir, L"Pointer.cur"))) return;
    std::wstring scheme = L"Golden Gate (" + g_set.cursor + L")";
    std::wstring list;
    for (auto &c : kCursorMap) {
        std::wstring f = JoinPath(dir, c.file);
        RegSetString(HKEY_CURRENT_USER, K_CURSORS, c.reg, f, REG_EXPAND_SZ);
        if (!list.empty()) list += L",";
        list += f;
    }
    RegSetString(HKEY_CURRENT_USER, K_CURSORS, nullptr, scheme);
    RegSetString(HKEY_CURRENT_USER, L"Control Panel\\Cursors\\Schemes", scheme.c_str(), list, REG_EXPAND_SZ);
    RegSetDword(HKEY_CURRENT_USER, K_CURSORS, L"Scheme Source", 1);
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

static int g_applied = -1;
void ThemeApply(bool force) {
    ThemeBackupOnce();
    bool dark = IsDarkNow();
    if (!force && g_applied == (int)dark) return;
    bool changed = g_applied != (int)dark;
    g_applied = dark;
    RegSetDword(HKEY_CURRENT_USER, K_PERS, L"AppsUseLightTheme", dark ? 0 : 1);
    RegSetDword(HKEY_CURRENT_USER, K_PERS, L"SystemUsesLightTheme", dark ? 0 : 1);
    if (g_set.applyAccent) ApplyAccent();
    Broadcast(L"ImmersiveColorSet");
    if (g_set.applyWallpaper && (changed || force)) ApplyWallpaper(dark);
    if (force) ThemeApplyCursors();
    UpdatePalette();
    if (g_main) PostMessageW(g_main, WM_GG_THEME, 0, 0);
}

void ThemeRestoreAll() {
    bool f;
    std::wstring v;
    v = BGet(L"personalize", L"AppsUseLightTheme", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_PERS, L"AppsUseLightTheme", _wtoi(v.c_str()));
    v = BGet(L"personalize", L"SystemUsesLightTheme", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_PERS, L"SystemUsesLightTheme", _wtoi(v.c_str()));
    v = BGet(L"dwm", L"AccentColor", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_DWM, L"AccentColor", (DWORD)wcstoull(v.c_str(), nullptr, 10));
    v = BGet(L"dwm", L"ColorizationColor", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_DWM, L"ColorizationColor", (DWORD)wcstoull(v.c_str(), nullptr, 10));
    v = BGet(L"accent", L"AccentColorMenu", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_ACCENT, L"AccentColorMenu", (DWORD)wcstoull(v.c_str(), nullptr, 10));
    v = BGet(L"accent", L"StartColorMenu", &f);
    if (f) RegSetDword(HKEY_CURRENT_USER, K_ACCENT, L"StartColorMenu", (DWORD)wcstoull(v.c_str(), nullptr, 10));
    v = BGet(L"accent", L"AccentPalette", &f);
    if (f && v.size() >= 64) {
        BYTE pal[32];
        for (int i = 0; i < 32; i++) pal[i] = (BYTE)wcstoul(v.substr(i * 2, 2).c_str(), nullptr, 16);
        RegSetBinary(HKEY_CURRENT_USER, K_ACCENT, L"AccentPalette", pal, 32);
    }
    v = BGet(L"desktop", L"WallPaper", &f);
    if (f && !v.empty() && FileExists(v))
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)v.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    // cursors: back to the saved scheme, or Windows' defaults
    v = BGet(L"cursors", L"(scheme)", &f);
    RegSetString(HKEY_CURRENT_USER, K_CURSORS, nullptr, f ? v : L"");
    for (auto &c : kCursorMap) {
        v = BGet(L"cursors", c.reg, &f);
        if (f && v != L"(empty)") RegSetString(HKEY_CURRENT_USER, K_CURSORS, c.reg, v, REG_EXPAND_SZ);
        else RegSetString(HKEY_CURRENT_USER, K_CURSORS, c.reg, L"", REG_EXPAND_SZ);
    }
    std::wstring scheme = L"Golden Gate (" + g_set.cursor + L")";
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors\\Schemes", scheme.c_str());
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    Broadcast(L"ImmersiveColorSet");
    // taskbar
    v = BGet(L"taskbar", L"state", &f);
    APPBARDATA abd = {sizeof(abd)};
    abd.lParam = f ? (LPARAM)_wtoi(v.c_str()) : ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &abd);
    for (const wchar_t *cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        HWND t = nullptr;
        while ((t = FindWindowExW(nullptr, t, cls, nullptr))) ShowWindow(t, SW_SHOW);
    }
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"GoldenGate");
}

// Small helpers: strings, paths, registry, DPI, settings file.
#include "common.h"
#include <cstdarg>
#include <cstdio>

HINSTANCE g_inst;
std::wstring g_dir, g_dataDir;
Settings g_set;

std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}
std::wstring FileNameOf(const std::wstring &p) {
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}
std::wstring JoinPath(const std::wstring &a, const std::wstring &b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
bool FileExists(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}
std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}
float DpiScaleFor(HWND hwnd) {
    UINT d = GetDpiForWindow(hwnd);
    return d ? d / 96.f : 1.f;
}
typedef HRESULT(WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT *, UINT *);
float DpiScaleForMonitor(HMONITOR mon) {
    static GetDpiForMonitorFn fn = (GetDpiForMonitorFn)GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor");
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(mon, 0, &x, &y))) return x / 96.f;
    return 1.f;
}
void ShellOpen(const std::wstring &target, const std::wstring &args) {
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = target.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}
void Log(const wchar_t *fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    OutputDebugStringW(buf);
    if (g_dataDir.empty()) return;
    FILE *f = _wfopen(JoinPath(g_dataDir, L"goldengate.log").c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02d:%02d:%02d %ls\n", st.wHour, st.wMinute, st.wSecond, buf);
    fclose(f);
}

// --------------------------------------------------------------- registry --
bool RegGetDword(HKEY root, const wchar_t *key, const wchar_t *name, DWORD &out) {
    DWORD sz = sizeof(DWORD);
    return RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &out, &sz) == ERROR_SUCCESS;
}
bool RegSetDword(HKEY root, const wchar_t *key, const wchar_t *name, DWORD v) {
    return RegSetKeyValueW(root, key, name, REG_DWORD, &v, sizeof(v)) == ERROR_SUCCESS;
}
bool RegGetString(HKEY root, const wchar_t *key, const wchar_t *name, std::wstring &out) {
    DWORD sz = 0;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, nullptr, &sz) != ERROR_SUCCESS) return false;
    std::wstring s(sz / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, &s[0], &sz) != ERROR_SUCCESS) return false;
    s.resize(wcslen(s.c_str()));
    out = s;
    return true;
}
bool RegSetString(HKEY root, const wchar_t *key, const wchar_t *name, const std::wstring &v, DWORD type) {
    return RegSetKeyValueW(root, key, name, type, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
bool RegSetBinary(HKEY root, const wchar_t *key, const wchar_t *name, const void *data, DWORD size) {
    return RegSetKeyValueW(root, key, name, REG_BINARY, data, size) == ERROR_SUCCESS;
}

// ------------------------------------------------------- input / windows --
void SendKeys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> in;
    for (WORD k : keys) {
        INPUT i = {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k;
        if (k == VK_LEFT || k == VK_RIGHT || k == VK_UP || k == VK_DOWN || k == VK_DELETE ||
            k == VK_HOME || k == VK_END || k == VK_PRIOR || k == VK_NEXT || k == VK_INSERT)
            i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        in.push_back(i);
    }
    std::vector<WORD> rev(keys);
    std::reverse(rev.begin(), rev.end());
    for (WORD k : rev) {
        INPUT i = {};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = k;
        i.ki.dwFlags = KEYEVENTF_KEYUP;
        if (k == VK_LEFT || k == VK_RIGHT || k == VK_UP || k == VK_DOWN || k == VK_DELETE ||
            k == VK_HOME || k == VK_END || k == VK_PRIOR || k == VK_NEXT || k == VK_INSERT)
            i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        in.push_back(i);
    }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

void ActivateWindow(HWND h) {
    if (!h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    // a harmless Alt tap lets a background process hand focus to a window
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_MENU;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    SetForegroundWindow(h);
    BringWindowToTop(h);
}

// --------------------------------------------------------------- settings --
static std::wstring IniPath() { return JoinPath(g_dataDir, L"settings.ini"); }
static int IniInt(const wchar_t *sec, const wchar_t *key, int def) {
    return GetPrivateProfileIntW(sec, key, def, IniPath().c_str());
}
static std::wstring IniStr(const wchar_t *sec, const wchar_t *key, const std::wstring &def) {
    wchar_t buf[4096];
    GetPrivateProfileStringW(sec, key, def.c_str(), buf, 4096, IniPath().c_str());
    return buf;
}
static void IniPut(const wchar_t *sec, const wchar_t *key, const std::wstring &v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), IniPath().c_str());
}
static void IniPutInt(const wchar_t *sec, const wchar_t *key, int v) { IniPut(sec, key, std::to_wstring(v)); }

void LoadSettings() {
    Settings d;
    Settings &s = g_set;
    s.style = IniInt(L"theme", L"style", d.style);
    s.lightHour = IniInt(L"theme", L"lightHour", d.lightHour);
    s.darkHour = IniInt(L"theme", L"darkHour", d.darkHour);
    s.accent = (unsigned)wcstoul(IniStr(L"theme", L"accent", L"3478F6").c_str(), nullptr, 16);
    s.cursor = IniStr(L"theme", L"cursor", d.cursor);
    s.cursorSize = IniInt(L"theme", L"cursorSize", d.cursorSize);
    if (s.cursorSize != 32 && s.cursorSize != 48 && s.cursorSize != 64 && s.cursorSize != 96) s.cursorSize = d.cursorSize;
    s.applyCursors = IniInt(L"theme", L"applyCursors", d.applyCursors);
    s.applyWallpaper = IniInt(L"theme", L"applyWallpaper", d.applyWallpaper);
    s.applyAccent = IniInt(L"theme", L"applyAccent", d.applyAccent);
    s.menubar = IniInt(L"menubar", L"enabled", d.menubar);
    s.clock24 = IniInt(L"menubar", L"clock24", d.clock24);
    s.clockSeconds = IniInt(L"menubar", L"seconds", d.clockSeconds);
    s.clockDate = IniInt(L"menubar", L"date", d.clockDate);
    s.clockCenter = IniInt(L"menubar", L"center", d.clockCenter);
    s.showBattery = IniInt(L"menubar", L"battery", d.showBattery);
    s.panelHeight = std::max(22, std::min(40, IniInt(L"menubar", L"height", d.panelHeight)));
    s.panelGlass = std::max(0, std::min(100, IniInt(L"menubar", L"glass", d.panelGlass)));
    s.panelTint = std::max(0, std::min(100, IniInt(L"menubar", L"tint", d.panelTint)));
    s.glassPreset = std::max(0, std::min(2, IniInt(L"menubar", L"glasspreset", d.glassPreset)));
    s.dock = IniInt(L"dock", L"enabled", d.dock);
    s.dlStyle = std::max(0, std::min(2, IniInt(L"dock", L"downloads", d.dlStyle)));
    s.iconStyle = std::max(0, std::min(1, IniInt(L"dock", L"iconstyle", d.iconStyle)));
    s.dockMagnify = IniInt(L"dock", L"magnify", d.dockMagnify);
    s.dockAutohide = IniInt(L"dock", L"autohide", d.dockAutohide);
    s.dockHide = IniInt(L"dock", L"hide", d.dockHide);
    s.dockReserve = IniInt(L"dock", L"reserve", d.dockReserve);
    s.dockSize = std::max(32, std::min(96, IniInt(L"dock", L"size", d.dockSize)));
    s.dockMagnifySize = std::max(s.dockSize, std::min(140, IniInt(L"dock", L"magnifySize", d.dockMagnifySize)));
    s.pinned.clear();
    std::wstring pins = IniStr(L"dock", L"pinned", L"");
    size_t a = 0;
    while (a < pins.size()) {
        size_t b = pins.find(L'|', a);
        if (b == std::wstring::npos) b = pins.size();
        if (b > a) s.pinned.push_back(pins.substr(a, b - a));
        a = b + 1;
    }
    s.missionControl = IniInt(L"features", L"missionControl", d.missionControl);
    s.hotCorner = IniInt(L"features", L"hotCorner", d.hotCorner);
    // new key: the old "switcher" defaulted on, so existing installs return to Windows' Alt+Tab
    s.switcher = IniInt(L"features", L"coverflowAltTab", d.switcher);
    s.spotlight = IniInt(L"features", L"spotlight", d.spotlight);
    s.launchpad = IniInt(L"features", L"launchpad", d.launchpad);
    s.clipboard = IniInt(L"features", L"clipboard", d.clipboard);
    s.shakeFind = IniInt(L"features", L"shakeFind", d.shakeFind);
    s.caffeine = IniInt(L"features", L"caffeine", d.caffeine);
    s.taskbar = IniInt(L"windows", L"taskbar", d.taskbar);
    s.startAtLogin = IniInt(L"windows", L"startAtLogin", d.startAtLogin);
}

void SaveSettings() {
    const Settings &s = g_set;
    wchar_t hex[16];
    swprintf(hex, 16, L"%06X", s.accent & 0xFFFFFF);
    IniPutInt(L"theme", L"style", s.style);
    IniPutInt(L"theme", L"lightHour", s.lightHour);
    IniPutInt(L"theme", L"darkHour", s.darkHour);
    IniPut(L"theme", L"accent", hex);
    IniPut(L"theme", L"cursor", s.cursor);
    IniPutInt(L"theme", L"cursorSize", s.cursorSize);
    IniPutInt(L"theme", L"applyCursors", s.applyCursors);
    IniPutInt(L"theme", L"applyWallpaper", s.applyWallpaper);
    IniPutInt(L"theme", L"applyAccent", s.applyAccent);
    IniPutInt(L"menubar", L"enabled", s.menubar);
    IniPutInt(L"menubar", L"clock24", s.clock24);
    IniPutInt(L"menubar", L"seconds", s.clockSeconds);
    IniPutInt(L"menubar", L"date", s.clockDate);
    IniPutInt(L"menubar", L"center", s.clockCenter);
    IniPutInt(L"menubar", L"battery", s.showBattery);
    IniPutInt(L"menubar", L"height", s.panelHeight);
    IniPutInt(L"menubar", L"glass", s.panelGlass);
    IniPutInt(L"menubar", L"tint", s.panelTint);
    IniPutInt(L"menubar", L"glasspreset", s.glassPreset);
    IniPutInt(L"dock", L"enabled", s.dock);
    IniPutInt(L"dock", L"downloads", s.dlStyle);
    IniPutInt(L"dock", L"iconstyle", s.iconStyle);
    IniPutInt(L"dock", L"magnify", s.dockMagnify);
    IniPutInt(L"dock", L"autohide", s.dockAutohide);
    IniPutInt(L"dock", L"hide", s.dockHide);
    IniPutInt(L"dock", L"reserve", s.dockReserve);
    IniPutInt(L"dock", L"size", s.dockSize);
    IniPutInt(L"dock", L"magnifySize", s.dockMagnifySize);
    std::wstring pins;
    for (auto &p : s.pinned) { if (!pins.empty()) pins += L"|"; pins += p; }
    IniPut(L"dock", L"pinned", pins);
    IniPutInt(L"features", L"missionControl", s.missionControl);
    IniPutInt(L"features", L"hotCorner", s.hotCorner);
    IniPutInt(L"features", L"coverflowAltTab", s.switcher);
    IniPutInt(L"features", L"spotlight", s.spotlight);
    IniPutInt(L"features", L"launchpad", s.launchpad);
    IniPutInt(L"features", L"clipboard", s.clipboard);
    IniPutInt(L"features", L"shakeFind", s.shakeFind);
    IniPutInt(L"features", L"caffeine", s.caffeine);
    IniPutInt(L"windows", L"taskbar", s.taskbar);
    IniPutInt(L"windows", L"startAtLogin", s.startAtLogin);
    // start at login: per-user Run key
    const wchar_t *run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (s.startAtLogin) {
        RegSetString(HKEY_CURRENT_USER, run, L"GoldenGate", L"\"" + JoinPath(g_dir, L"GoldenGate.exe") + L"\"");
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, run, L"GoldenGate");
    }
}

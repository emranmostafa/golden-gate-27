// Enumerating apps and windows, launching, and icons (themed with MacTahoe
// where we recognise the app, otherwise the app's own icon).
#include "common.h"
#include <uiautomation.h>

// --------------------------------------------------------------- windows ---
std::wstring WindowExe(HWND h, DWORD *pidOut) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pidOut) *pidOut = pid;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) r = Lower(FileNameOf(buf));
    CloseHandle(p);
    return r;
}

std::wstring WindowAumid(HWND h) {
    std::wstring r;
    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR) r = Lower(pv.pwszVal);
        PropVariantClear(&pv);
        ps->Release();
    }
    if (r.empty()) {
        // packaged apps often carry their app ID on the process, not the window
        typedef LONG (WINAPI *GetAumidFn)(HANDLE, UINT32 *, PWSTR);
        static GetAumidFn getAumid = (GetAumidFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetApplicationUserModelId");
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE p = getAumid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
        if (p) {
            wchar_t buf[256];
            UINT32 n = 256;
            if (getAumid(p, &n, buf) == ERROR_SUCCESS) r = Lower(buf);
            CloseHandle(p);
        }
    }
    return r;
}

// Store apps host their real window inside ApplicationFrameHost; find it so we
// read the right AUMID and icon.
static HWND CoreWindowOf(HWND frame) {
    HWND found = nullptr;
    EnumChildWindows(frame, [](HWND c, LPARAM lp) -> BOOL {
        wchar_t cls[64];
        GetClassNameW(c, cls, 64);
        if (!wcscmp(cls, L"Windows.UI.Core.CoreWindow")) { *(HWND *)lp = c; return FALSE; }
        return TRUE;
    }, (LPARAM)&found);
    return found;
}
// A full-screen window covers its whole monitor: a browser playing video full
// screen or in F11, a media player, a game. A maximized window with a title bar
// is not one (without a reserved panel strip it can also reach the screen edges).
// Our own windows (Launchpad, Mission Control) don't count.
bool IsFullscreenWindow(HWND h) {
    if (!h || !IsWindowVisible(h) || IsIconic(h)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return false;
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if (IsZoomed(h) && (st & WS_CAPTION) == WS_CAPTION) return false;
    RECT wr;
    GetWindowRect(h, &wr);
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfo(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

// Is the window on top of this monitor full screen? The focused window answers
// it directly when it's on that monitor (the common case, no window scan);
// otherwise walk the z-order to the top app window there. Small always-on-top
// windows (picture-in-picture, sticky notes) float over a full-screen video, so
// they're looked past.
bool MonitorHasFullscreen(HMONITOR mon) {
    HWND fg = GetForegroundWindow();
    if (fg && MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) == mon) {
        if (IsFullscreenWindow(fg)) return true;
        if (IsAppWindow(fg) && !(GetWindowLongW(fg, GWL_EXSTYLE) & WS_EX_TOPMOST)) return false;
    }
    struct Scan { HMONITOR mon; bool fs; } s = {mon, false};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto *s = (Scan *)lp;
        if (IsIconic(h) || !IsWindowVisible(h)) return TRUE;
        if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != s->mon) return TRUE;
        if (IsFullscreenWindow(h)) { s->fs = true; return FALSE; }
        if (!IsAppWindow(h) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;
        return FALSE;   // a normal app window is on top: not full screen
    }, (LPARAM)&s);
    return s.fs;
}

HWND RealAppWindow(HWND h) {
    std::wstring exe = WindowExe(h);
    if (exe == L"applicationframehost.exe") {
        HWND core = CoreWindowOf(h);
        if (core) return core;
    }
    return h;
}

bool IsAppWindow(HWND h) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    LONG st = GetWindowLongW(h, GWL_STYLE);
    // a tool window isn't a taskbar app unless it explicitly opts in with APPWINDOW
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    if (!(st & WS_VISIBLE)) return false;
    wchar_t title[4];
    if (GetWindowTextLengthW(h) == 0) return false;
    GetWindowTextW(h, title, 4);
    // skip cloaked windows (Store apps parked in the background, virtual desktops)
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    wchar_t cls[128];
    GetClassNameW(h, cls, 128);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"GoldenGateSurface") || !wcscmp(cls, L"Windows.UI.Core.CoreWindow"))
        return false;
    return true;
}

static BOOL CALLBACK EnumWin(HWND h, LPARAM lp) {
    auto *out = (std::vector<WinInfo> *)lp;
    if (!IsAppWindow(h)) return TRUE;
    WinInfo wi;
    wi.hwnd = h;
    int n = GetWindowTextLengthW(h);
    std::wstring t(n + 1, 0);
    GetWindowTextW(h, &t[0], n + 1);
    t.resize(n);
    wi.title = t;
    HWND real = RealAppWindow(h);
    wi.exe = WindowExe(h, &wi.pid);
    wi.aumid = WindowAumid(real);
    out->push_back(wi);
    return TRUE;
}
std::vector<WinInfo> ListAppWindows(HMONITOR onlyOn) {
    std::vector<WinInfo> v;
    EnumWindows(EnumWin, (LPARAM)&v);
    if (onlyOn) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const WinInfo &w) {
            return MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST) != onlyOn;
        }), v.end());
    }
    return v;
}

std::wstring AppNameFor(HWND h) {
    HWND real = RealAppWindow(h);
    std::wstring aumid = WindowAumid(real);
    std::wstring exe = WindowExe(h);
    if (const AppEntry *a = FindAppForWindow(aumid, exe)) return a->name;
    // fall back to the product name in the exe's version info
    DWORD pid;
    WindowExe(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[MAX_PATH];
    DWORD n = MAX_PATH;
    std::wstring name;
    if (p && QueryFullProcessImageNameW(p, 0, path, &n)) {
        DWORD h2, sz = GetFileVersionInfoSizeW(path, &h2);
        if (sz) {
            std::vector<BYTE> buf(sz);
            if (GetFileVersionInfoW(path, 0, sz, buf.data())) {
                void *val = nullptr;
                UINT len = 0;
                if (VerQueryValueW(buf.data(), L"\\StringFileInfo\\040904B0\\FileDescription", &val, &len) && len > 1)
                    name = (wchar_t *)val;
            }
        }
    }
    if (p) CloseHandle(p);
    if (name.empty()) {
        name = exe;
        size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        if (!name.empty()) name[0] = towupper(name[0]);
    }
    return name;
}

// --------------------------------------------------------------- app list ---
static std::vector<AppEntry> g_apps;
std::vector<AppEntry> &AllApps(bool refresh) {
    if (!g_apps.empty() && !refresh) return g_apps;
    for (auto &a : g_apps) if (a.pidl) CoTaskMemFree(a.pidl);
    g_apps.clear();
    IShellItem *appsFolder = nullptr;
    // shell:AppsFolder enumerates desktop + Store apps that show in the app list
    if (FAILED(SHCreateItemFromParsingName(L"shell:AppsFolder", nullptr, IID_PPV_ARGS(&appsFolder)))) return g_apps;
    IEnumShellItems *en = nullptr;
    if (SUCCEEDED(appsFolder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en))) && en) {
        IShellItem *item;
        while (en->Next(1, &item, nullptr) == S_OK) {
            AppEntry e;
            PWSTR name = nullptr, parse = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name))) e.name = name;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parse))) e.id = parse;
            // keep the item itself: its icon is exactly the shortcut icon the
            // taskbar/Start show, and it launches without re-parsing the name
            SHGetIDListFromObject(item, &e.pidl);
            CoTaskMemFree(name);
            CoTaskMemFree(parse);
            // classify: a filesystem path is a desktop app, otherwise an AUMID
            if (e.id.find(L"\\") != std::wstring::npos && (e.id.size() < 2 || e.id[1] != L':')) {
                // AppsFolder\<aumid>
            }
            IShellItem2 *i2 = nullptr;
            if (SUCCEEDED(item->QueryInterface(&i2))) {
                PWSTR aumid = nullptr;
                if (SUCCEEDED(i2->GetString(PKEY_AppUserModel_ID, &aumid)) && aumid) e.aumid = Lower(aumid);
                CoTaskMemFree(aumid);
                // what the shortcut runs, so a window with no app ID can still be
                // matched to its Start-menu item (VS Code's id isn't a path).
                // explorer.exe is skipped: many shell entries point at it.
                PWSTR target = nullptr;
                static const PROPERTYKEY GG_PKEY_Link_TargetParsingPath =
                    {{0xB9B4B3FC, 0x2B51, 0x4A42, {0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25}}, 2};
                if (SUCCEEDED(i2->GetString(GG_PKEY_Link_TargetParsingPath, &target)) && target) {
                    std::wstring t = Lower(FileNameOf(target));
                    if (t.size() > 4 && t.substr(t.size() - 4) == L".exe" && t != L"explorer.exe") { e.exe = t; e.target = target; }
                }
                CoTaskMemFree(target);
                i2->Release();
            }
            // a path-style id may start with a known-folder GUID ({6D809377-...}\RustDesk\RustDesk.exe):
            // only a real drive path can stand in for the shortcut's resolved target
            if (e.id.size() > 3 && Lower(e.id).substr(e.id.size() - 4) == L".exe") {
                e.exe = Lower(FileNameOf(e.id));
                if (e.target.empty() && e.id.find(L":\\") != std::wstring::npos) e.target = e.id;
            }
            if (!e.name.empty()) g_apps.push_back(e);
            else if (e.pidl) CoTaskMemFree(e.pidl);
            item->Release();
        }
        en->Release();
    }
    appsFolder->Release();
    return g_apps;
}

const AppEntry *FindApp(const std::wstring &id) {
    for (auto &a : g_apps) if (a.id == id) return &a;
    return nullptr;
}

// The Start-menu item for a window. Every app ID is checked before any exe, or a
// Chrome web app (its own ID, same chrome.exe) would be taken for Chrome itself.
const AppEntry *FindAppForWindow(const std::wstring &aumid, const std::wstring &exe) {
    auto &apps = AllApps();
    if (!aumid.empty()) for (auto &a : apps) if (a.aumid == aumid) return &a;
    if (!exe.empty()) for (auto &a : apps) if (a.exe == exe) return &a;
    return nullptr;
}

// Older builds pinned running apps by bare exe name ("code.exe"), which matches no
// Start-menu item. Resolve by the shortcut's target, then for packaged apps (no
// target) by display name: snippingtool.exe -> "Snipping Tool".
std::wstring CanonicalAppId(const std::wstring &id) {
    auto &apps = AllApps();
    std::wstring l = Lower(id);
    if (FindApp(id) || l.size() <= 4 || l.substr(l.size() - 4) != L".exe" ||
        l.find(L'\\') != std::wstring::npos || l == L"explorer.exe")
        return id;
    for (auto &a : apps) if (a.exe == l) return a.id;
    std::wstring stem = l.substr(0, l.size() - 4);
    for (auto &a : apps) {
        std::wstring n = Lower(a.name);
        n.erase(std::remove(n.begin(), n.end(), L' '), n.end());
        if (n == stem) return a.id;
    }
    return id;
}

void LaunchApp(const std::wstring &id) {
    // launch the Start-menu item directly when we have it (works for desktop,
    // Store and web apps alike)
    if (const AppEntry *a = FindApp(id)) {
        if (a->pidl) {
            SHELLEXECUTEINFOW sei = {sizeof(sei)};
            sei.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
            sei.lpIDList = a->pidl;
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&sei)) return;
        }
    }
    IShellItem *item = nullptr;
    std::wstring parse = id;
    if (id.find(L":\\") == std::wstring::npos && id.find(L"AppsFolder") != 0 && id.find(L"\\") != std::wstring::npos)
        parse = L"shell:AppsFolder\\" + id;
    HRESULT hr = SHCreateItemFromParsingName(parse.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(hr) && parse == id)   // bare app IDs (Store / web apps) live under AppsFolder
        hr = SHCreateItemFromParsingName((L"shell:AppsFolder\\" + id).c_str(), nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(hr)) {
        SHELLEXECUTEINFOW sei = {sizeof(sei)};
        sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
        sei.lpIDList = nullptr;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(item, &pidl))) {
            sei.lpIDList = pidl;
            sei.fMask |= SEE_MASK_IDLIST;
            sei.nShow = SW_SHOWNORMAL;
            ShellExecuteExW(&sei);
            CoTaskMemFree(pidl);
        }
        item->Release();
    } else {
        ShellOpen(id);
    }
}

// ----------------------------------------------------------------- icons ---
static std::map<std::wstring, IWICBitmapSource *> g_iconCache;
static std::map<std::wstring, std::wstring> g_iconMap;   // match -> themed key

static void LoadIconMap() {
    if (!g_iconMap.empty()) return;
    FILE *f = _wfopen(JoinPath(JoinPath(g_dir, L"icons"), L"icons.map").c_str(), L"r, ccs=UTF-8");
    if (!f) { g_iconMap[L"\x01"] = L""; return; }
    wchar_t line[512];
    while (fgetws(line, 512, f)) {
        std::wstring s = line;
        if (s.empty() || s[0] == L'#') continue;
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Lower(s.substr(0, eq)), v = s.substr(eq + 1);
        while (!v.empty() && (v.back() == L'\n' || v.back() == L'\r' || v.back() == L' ')) v.pop_back();
        if (!k.empty() && !v.empty()) g_iconMap[k] = v;
    }
    fclose(f);
}

IWICBitmapSource *ThemedIcon(const std::wstring &key) {
    std::wstring ck = L"theme:" + key;
    auto it = g_iconCache.find(ck);
    if (it != g_iconCache.end()) return it->second;
    IWICBitmapSource *b = LoadWicFile(JoinPath(JoinPath(g_dir, L"icons"), key + L".png"));
    g_iconCache[ck] = b;
    return b;
}

IWICBitmapSource *IconForExe(const std::wstring &exe, const std::wstring &aumid, int size) {
    LoadIconMap();
    std::wstring k = Lower(exe);
    auto it = g_iconMap.find(k);
    if (it == g_iconMap.end() && !aumid.empty()) {
        // try the AUMID and its prefix before '!'
        std::wstring a = Lower(aumid);
        it = g_iconMap.find(a);
        if (it == g_iconMap.end()) {
            size_t bang = a.find(L'!');
            if (bang != std::wstring::npos) it = g_iconMap.find(a.substr(0, bang));
        }
        if (it == g_iconMap.end()) {
            for (auto &m : g_iconMap)
                if (m.first.size() > 4 && a.find(m.first) != std::wstring::npos) { it = g_iconMap.find(m.first); break; }
        }
    }
    if (it != g_iconMap.end()) {
        IWICBitmapSource *b = ThemedIcon(it->second);
        if (b) return b;
    }
    return nullptr;
}

// the shell's icon for any parsing name (a file path, or shell:AppsFolder\<id>),
// icon-only so it keeps its alpha instead of coming back as a white-matte thumbnail
static IWICBitmapSource *IconFromItem(IShellItem *item, int size) {
    IShellItemImageFactory *fac = nullptr;
    HBITMAP hb = nullptr;
    if (SUCCEEDED(item->QueryInterface(&fac))) {
        // always the largest the shell has: everything downstream scales down from it
        SIZE s = {std::max(size, 256), std::max(size, 256)};
        fac->GetImage(s,(SIIGBF)(0x1 | 0x4) /* BIGGERSIZEOK | ICONONLY */, &hb);
        fac->Release();
    }
    IWICBitmapSource *src = WicFromHBitmap(hb);
    if (hb) DeleteObject(hb);
    return src;
}

IWICBitmapSource *ShellItemIcon(const std::wstring &parse, int size) {
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(parse.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    IWICBitmapSource *r = IconFromItem(item, size);
    item->Release();
    return r;
}

// the shell's own icon for an app id (desktop or Store). Bare app IDs (Store apps,
// Chrome/Edge web apps) only resolve under shell:AppsFolder, so try that too.
// The shell only uses an exe's icon as "large" when it has a 256px image. Without
// one (RustDesk ships only 48px, SessionX a single 240px) it draws the small icon
// on a faint placeholder frame instead. In that case load the exe's own largest
// image. Returns null when the shell's icon is already right (256px present).
IWICBitmapSource *ExeNativeIcon(const std::wstring &exePath) {
    if (exePath.empty()) return nullptr;
    HMODULE m = LoadLibraryExW(exePath.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!m) return nullptr;
    int biggest = 0;
    // the first icon group is the program's main icon
    EnumResourceNamesW(m, RT_GROUP_ICON, [](HMODULE mod, LPCWSTR type, LPWSTR name, LONG_PTR lp) -> BOOL {
        HRSRC r = FindResourceW(mod, name, type);
        BYTE *d = r ? (BYTE *)LockResource(LoadResource(mod, r)) : nullptr;
        if (d && SizeofResource(mod, r) >= 6) {
            int count = *(WORD *)(d + 4);
            for (int i = 0; i < count && 6 + 14 * (i + 1) <= (int)SizeofResource(mod, r); i++) {
                int w = d[6 + 14 * i];
                *(int *)lp = std::max(*(int *)lp, w == 0 ? 256 : w);
            }
        }
        return FALSE;
    }, (LONG_PTR)&biggest);
    FreeLibrary(m);
    // Any exe without a 256px image gets the framed placeholder from the shell
    // (RustDesk ships only 48px, SessionX one 240px), so use its own largest
    // image whatever the size; a 48px icon is about the Dock's own size anyway.
    if (biggest >= 256 || biggest < 16) return nullptr;
    HICON big = nullptr;
    if (SHDefExtractIconW(exePath.c_str(), 0, 0, &big, nullptr, MAKELONG(biggest, 16)) != S_OK || !big) return nullptr;
    IWICBitmapSource *b = WicFromHIcon(big);
    DestroyIcon(big);
    return b;
}

static IWICBitmapSource *ShellIcon(const std::wstring &id, int size) {
    // an exe whose large icon the shell would turn into a framed placeholder
    {
        const AppEntry *a = FindApp(id);
        std::wstring path = a ? a->target : (id.find(L":\\") != std::wstring::npos ? id : L"");
        if (IWICBitmapSource *n = ExeNativeIcon(path)) return n;
    }
    // best: the Start-menu item itself (its shortcut icon is what the taskbar shows)
    if (const AppEntry *a = FindApp(id)) {
        if (a->pidl) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromIDList(a->pidl, IID_PPV_ARGS(&item)))) {
                IWICBitmapSource *b = IconFromItem(item, size);
                item->Release();
                if (b) return b;
            }
        }
    }
    std::wstring parse = id;
    if (id.find(L":\\") == std::wstring::npos && id.find(L"\\") != std::wstring::npos && id.find(L"AppsFolder") != 0)
        parse = L"shell:AppsFolder\\" + id;
    IWICBitmapSource *b = ShellItemIcon(parse, size);
    if (!b && parse == id) b = ShellItemIcon(L"shell:AppsFolder\\" + id, size);
    return b;
}

// An app's icon. Original style (default) = the app's own icon from the shell,
// exactly what the taskbar shows; macOS style = the bundled replacement set when
// one exists. Each falls back to the other so nothing ends up blank.
IWICBitmapSource *AppIcon(const std::wstring &id, int size, bool bundledFallback) {
    std::wstring ck = (g_set.iconStyle ? L"t:" : bundledFallback ? L"o:" : L"n:") + id;
    auto it = g_iconCache.find(ck);
    if (it != g_iconCache.end()) return it->second;
    const AppEntry *a = FindApp(id);
    std::wstring exe = a ? a->exe : (id.size() > 4 && Lower(id).substr(id.size() - 4) == L".exe" ? Lower(FileNameOf(id)) : L"");
    std::wstring aumid = a ? a->aumid : L"";
    IWICBitmapSource *b = nullptr;
    // File Explorer keeps its Finder icon in every style
    std::wstring lid = Lower(id);
    bool finder = exe == L"explorer.exe" || aumid == L"microsoft.windows.explorer" ||
                  lid == L"microsoft.windows.explorer" || (lid.size() >= 12 && lid.substr(lid.size() - 12) == L"explorer.exe");
    if (g_set.iconStyle || finder) b = IconForExe(finder ? L"explorer.exe" : exe, aumid, size);
    if (!b) b = ShellIcon(id, size);
    if (!b && !g_set.iconStyle && bundledFallback) b = IconForExe(exe, aumid, size);
    g_iconCache[ck] = b;
    return b;
}

// ------------------------------------------------------- icon diagnostic ---
// GoldenGate.exe --test-icons=<file.png>: runs every Start-menu app through the
// Dock's own icon path and saves one contact sheet (64px each, on a dark tile,
// in Start-menu order) plus <file.png>.txt naming each tile, so a wrong icon (a
// framed placeholder, the bundled set, nothing) is easy to spot for any app.
int RunIconTest(const std::wstring &outPng) {
    auto &apps = AllApps(true);
    const int cell = 72, icon = 64, cols = 16;
    int n = (int)apps.size(), rows = (n + cols - 1) / cols;
    int W = cols * cell, H = std::max(1, rows) * cell;
    std::vector<DWORD> sheet((size_t)W * H, 0xFF202020);
    std::wstring index;
    for (int i = 0; i < n; i++) {
        int cx = (i % cols) * cell, cy = (i / cols) * cell;
        for (int y = 2; y < cell - 2; y++)   // a slightly lighter tile behind each icon
            for (int x = 2; x < cell - 2; x++) sheet[(size_t)(cy + y) * W + cx + x] = 0xFF2C2C2C;
        IWICBitmapSource *b = AppIcon(apps[i].id, 128, false);   // as the Dock asks first
        bool bundled = false;
        if (!b) { b = AppIcon(apps[i].id, 128); bundled = b != nullptr; }   // its last resort
        wchar_t line[64];
        swprintf(line, 64, L"%3d  r%d c%d  %ls  ", i, i / cols, i % cols, b ? (bundled ? L"[bundled]" : L"") : L"[NONE]");
        index += line + apps[i].name + L"  |  " + apps[i].id + L"\r\n";
        if (!b) continue;
        IWICBitmapSource *nb = NormalizeIcon(b);
        IWICBitmapScaler *sc = nullptr;
        if (FAILED(g_wic->CreateBitmapScaler(&sc))) continue;
        std::vector<DWORD> px((size_t)icon * icon);
        if (SUCCEEDED(sc->Initialize(nb, icon, icon, (WICBitmapInterpolationMode)4)) &&
            SUCCEEDED(sc->CopyPixels(nullptr, icon * 4, (UINT)px.size() * 4, (BYTE *)px.data()))) {
            int ox = cx + (cell - icon) / 2, oy = cy + (cell - icon) / 2;
            for (int y = 0; y < icon; y++)
                for (int x = 0; x < icon; x++) {   // premultiplied "over" the tile
                    DWORD s = px[(size_t)y * icon + x], &d = sheet[(size_t)(oy + y) * W + ox + x];
                    int a = s >> 24, inv = 255 - a;
                    int r = ((s >> 16) & 255) + (((d >> 16) & 255) * inv) / 255;
                    int g = ((s >> 8) & 255) + (((d >> 8) & 255) * inv) / 255;
                    int bl = (s & 255) + ((d & 255) * inv) / 255;
                    d = 0xFF000000 | (std::min(r, 255) << 16) | (std::min(g, 255) << 8) | std::min(bl, 255);
                }
        }
        sc->Release();
    }
    // save the sheet as PNG
    IWICStream *st = nullptr;
    IWICBitmapEncoder *enc = nullptr;
    IWICBitmapFrameEncode *fr = nullptr;
    bool ok = SUCCEEDED(g_wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(outPng.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
              SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(W, H));
    if (ok) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(fr->SetPixelFormat(&fmt)) &&
             SUCCEEDED(fr->WritePixels(H, W * 4, (UINT)sheet.size() * 4, (BYTE *)sheet.data())) &&
             SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    FILE *f = _wfopen((outPng + L".txt").c_str(), L"w, ccs=UTF-8");
    if (f) { fputws(index.c_str(), f); fclose(f); }
    return ok ? 0 : 1;
}

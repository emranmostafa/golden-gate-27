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
    for (auto &a : AllApps())
        if ((!aumid.empty() && a.aumid == aumid) || (!a.exe.empty() && a.exe == exe))
            return a.name;
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
                i2->Release();
            }
            if (e.id.size() > 3 && Lower(e.id).substr(e.id.size() - 4) == L".exe") e.exe = Lower(FileNameOf(e.id));
            if (!e.name.empty()) g_apps.push_back(e);
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

void LaunchApp(const std::wstring &id) {
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
IWICBitmapSource *ShellItemIcon(const std::wstring &parse, int size) {
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(parse.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
    IShellItemImageFactory *fac = nullptr;
    HBITMAP hb = nullptr;
    if (SUCCEEDED(item->QueryInterface(&fac))) {
        SIZE s = {size, size};
        fac->GetImage(s, (SIIGBF)(0x1 | 0x4) /* BIGGERSIZEOK | ICONONLY */, &hb);
        fac->Release();
    }
    item->Release();
    IWICBitmapSource *src = WicFromHBitmap(hb);
    if (hb) DeleteObject(hb);
    return src;
}

// the shell's own icon for an app id (desktop or Store). Bare app IDs (Store apps,
// Chrome/Edge web apps) only resolve under shell:AppsFolder, so try that too.
static IWICBitmapSource *ShellIcon(const std::wstring &id, int size) {
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
IWICBitmapSource *AppIcon(const std::wstring &id, int size) {
    std::wstring ck = (g_set.iconStyle ? L"t:" : L"o:") + id;
    auto it = g_iconCache.find(ck);
    if (it != g_iconCache.end()) return it->second;
    const AppEntry *a = FindApp(id);
    std::wstring exe = a ? a->exe : (id.size() > 4 && Lower(id).substr(id.size() - 4) == L".exe" ? Lower(FileNameOf(id)) : L"");
    std::wstring aumid = a ? a->aumid : L"";
    IWICBitmapSource *b = nullptr;
    if (g_set.iconStyle) b = IconForExe(exe, aumid, size);
    if (!b) b = ShellIcon(id, size);
    if (!b && !g_set.iconStyle) b = IconForExe(exe, aumid, size);
    g_iconCache[ck] = b;
    return b;
}

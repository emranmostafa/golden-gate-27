// Golden Gate for Windows 11 - entry point.
// Runs as a single background process: a hidden message window owns the global
// hotkeys and timers and starts the menu bar, Dock and features. The tray icon
// (a Windows notification icon) gives quick access to settings and quitting.
// Command-line:  --restore  undoes all theme changes (used by the uninstaller).
#include "common.h"

HWND g_main = nullptr;
UINT g_msgTaskbarCreated = 0;
static NOTIFYICONDATAW g_nid = {};
static UINT_PTR g_tickTimer = 0;
static HICON g_trayIcon = nullptr;

void SwitcherAction(int what, bool shift);
void SwitcherStep(bool start);
void ClipboardOnChange();

// The Windows key, while Spotlight is on (Ctrl+Space -> Launchpad is a hotkey):
//  - tapped on its own: Spotlight instead of the Start menu
//  - Win+A: the Start menu (in place of Windows' Quick Settings shortcut)
//  - every other Win shortcut (Win+E, Win+L, Win+Shift+S...) works as usual
// A low-level keyboard hook (documented). Windows opens Start when the Win key is
// released with nothing pressed in between, so:
//  - for a tap, the release is held back and replayed after a harmless dummy key
//    (0xFF, no meaning), which makes it a "shortcut" rather than a Start press
//  - for Win+A the A is swallowed, so the release looks like a lone tap to
//    Windows and opens Start the normal way
// Our own injected keys (and every other program's) pass straight through.
static HHOOK g_tapHook = nullptr;
static bool g_winCombo = false;   // another key went down while Win was held
static bool g_winStart = false;   // Win+A: let the release open Start
static LRESULT CALLBACK TapHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_set.spotlight) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        if (!(k->flags & LLKHF_INJECTED)) {
            bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN), up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
            bool isWin = (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN);
            // async state isn't updated for this event yet: "held" means before it
            bool winHeld = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
            if (isWin && down) {
                if (!(GetAsyncKeyState(k->vkCode) & 0x8000)) { g_winCombo = false; g_winStart = false; }   // not auto-repeat
            } else if (isWin && up) {
                bool otherWin = (GetAsyncKeyState(k->vkCode == VK_LWIN ? VK_RWIN : VK_LWIN) & 0x8000) != 0;
                if (!g_winCombo && !g_winStart && !otherWin) {
                    // a lone tap: replay the release after a dummy key so Start stays shut
                    INPUT in[3] = {};
                    in[0].type = in[1].type = in[2].type = INPUT_KEYBOARD;
                    in[0].ki.wVk = 0xFF;
                    in[1].ki.wVk = 0xFF; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    in[2].ki.wVk = (WORD)k->vkCode; in[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
                    SendInput(3, in, sizeof(INPUT));
                    PostMessageW(g_main, WM_APP + 70, 0, 0);   // Spotlight
                    return 1;
                }
            } else if (winHeld) {
                bool otherMod = (GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU)) & 0x8000;
                if (k->vkCode == 'A' && !otherMod) {
                    if (down) g_winStart = true;
                    return 1;   // swallow A (down and up): Windows sees a lone Win press
                }
                if (down) g_winCombo = true;
            }
        }
    }
    return CallNextHookEx(g_tapHook, code, wp, lp);
}

static void AddTray() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_GG_TRAY;
    g_trayIcon = (HICON)LoadImageW(nullptr, JoinPath(g_dir, L"GoldenGate.ico").c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!g_trayIcon) g_trayIcon = LoadIcon(nullptr, IDI_APPLICATION);
    g_nid.hIcon = g_trayIcon;
    wcscpy(g_nid.szTip, L"Golden Gate");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayMenu() {
    std::vector<MenuItem> m;
    m.push_back({L"Golden Gate Settings…", L"", []() { SettingsOpen(); }});
    m.push_back({L"Mission Control", L"", []() { MissionControlToggle(); }});
    m.push_back({L"Launchpad", L"", []() { LaunchpadToggle(); }});
    m.push_back(MenuItem::Sep());
    MenuItem light{L"Light", L"", []() { g_set.style = 0; SaveSettings(); ThemeApply(true); }};
    light.checked = g_set.style == 0;
    MenuItem dark{L"Dark", L"", []() { g_set.style = 1; SaveSettings(); ThemeApply(true); }};
    dark.checked = g_set.style == 1;
    MenuItem autoT{L"Automatic", L"", []() { g_set.style = 2; SaveSettings(); ThemeApply(true); }};
    autoT.checked = g_set.style == 2;
    MenuItem appearance{L"Appearance"};
    appearance.sub = {light, dark, autoT};
    m.push_back(appearance);
    m.push_back(MenuItem::Sep());
    m.push_back({L"About Golden Gate", L"", []() { ShowAbout(); }});
    m.push_back({L"Quit Golden Gate", L"", []() { PostMessageW(g_main, WM_CLOSE, 0, 0); }});
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(g_main);
    PopupMenu(m, p.x, p.y, g_main, 190);
}

static void RegisterHotkeys() {
    // Mission Control: Ctrl+Up (like macOS uses Ctrl+Up). Launchpad: Ctrl+Space.
    if (g_set.missionControl) RegisterHotKey(g_main, HK_MISSION, MOD_CONTROL | MOD_NOREPEAT, VK_UP);
    if (g_set.launchpad) RegisterHotKey(g_main, HK_LAUNCHPAD, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE);
}
static void UnregisterHotkeys() {
    UnregisterHotKey(g_main, HK_MISSION);
    UnregisterHotKey(g_main, HK_LAUNCHPAD);
}

void CALLBACK TickTimer(HWND, UINT, UINT_PTR, DWORD) {
    ThemeApply(false);          // follow the time of day in Auto mode
    HotCornersTick();
    MenuBarTrack();
    TaskbarKeepHidden();
    DockPeriodicRefresh();   // catch apps opened/closed without a foreground change
}

// ---- crash safety: log the faulting module+offset and relaunch, so a stray
// periodic fault doesn't take Golden Gate down for good. crash.log lives in
// %APPDATA%\GoldenGate and gives the offset to pinpoint the fault.
static ULONGLONG g_launchTick = 0;
static HANDLE g_mtx = nullptr;   // single-instance lock

// our main window is message-only (HWND_MESSAGE), which plain FindWindow can't see
static HWND FindRunningMain() { return FindWindowExW(HWND_MESSAGE, nullptr, L"GoldenGateMain", nullptr); }

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep) {
    void *addr = ep ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    HMODULE mod = nullptr;
    wchar_t modname[MAX_PATH] = L"";
    if (addr) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &mod);
        if (mod) GetModuleFileNameW(mod, modname, MAX_PATH);
    }
    UINT_PTR off = (addr && mod) ? (UINT_PTR)addr - (UINT_PTR)mod : 0;
    std::wstring logp = JoinPath(g_dataDir, L"crash.log");
    HANDLE fh = CreateFileW(logp.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t line[600];
        int n = swprintf(line, 600,
            L"[%04d-%02d-%02d %02d:%02d:%02d] code=0x%08lX addr=%p module=%ls off=0x%IX uptime=%llums\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            (unsigned long)(ep ? ep->ExceptionRecord->ExceptionCode : 0), addr, modname, off,
            (unsigned long long)(GetTickCount64() - g_launchTick));
        if (n > 0) { DWORD wr; WriteFile(fh, line, (DWORD)n * (DWORD)sizeof(wchar_t), &wr, nullptr); }
        CloseHandle(fh);
    }
    if (GetTickCount64() - g_launchTick > 8000) {   // relaunch, unless it crashed at startup
        // drop the single-instance lock first, or the new copy sees "already
        // running" and quits; it also waits for the lock (--relaunched) to be safe
        if (g_mtx) { ReleaseMutex(g_mtx); CloseHandle(g_mtx); g_mtx = nullptr; }
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cl = L"\"" + std::wstring(self) + L"\" --relaunched";
        std::vector<wchar_t> buf(cl.begin(), cl.end());
        buf.push_back(0);
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(self, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return EXCEPTION_EXECUTE_HANDLER;   // the new copy keeps the taskbar hidden
        }
    }
    // nothing will take over (it crashed at startup, or the relaunch failed): give
    // the Windows taskbar back so you're never left with no taskbar and no Dock
    TaskbarRestore();
    return EXCEPTION_EXECUTE_HANDLER;
}

// keep the menu bar and Dock above the active window (Windows otherwise hides
// registered app-bars when a maximized window is focused, or buries them on
// Alt+Tab). Runs often, and from the foreground WinEvent for an instant reaction.
// A full-screen app (borderless, filling its monitor) is left alone: the panel
// and Dock on that monitor hide until it leaves full screen.
void CALLBACK ReassertTimer(HWND, UINT, UINT_PTR, DWORD) {
    TaskbarKeepHidden();        // keep the Windows taskbar down so it can't
                                // cover the Dock's bottom reveal edge
    MenuBarFullscreenSync();
    DockFullscreenSync();
    MenuBarReassert();
    DockReassert();
}

static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (hwnd && ev == EVENT_SYSTEM_FOREGROUND) {   // Windows' Quick Settings popup: keep it under the panel
        wchar_t cls[32] = {};
        GetClassNameW(hwnd, cls, 32);
        if (!wcscmp(cls, L"ControlCenterWindow")) FlyoutWatch();
    }
    // the focused app changed: update the menu bar, the Dock's running marks,
    // and keep our surfaces on top
    PostMessageW(g_main, WM_APP + 72, 0, 0);
}
static HWINEVENTHOOK g_fgHook = nullptr;
static HWINEVENTHOOK g_moveHook = nullptr, g_minHook = nullptr;   // windows moved / minimised

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == g_msgTaskbarCreated) {
        // Explorer restarted: re-add our tray icon and re-assert the layout
        AddTray();
        MenuBarStop(); if (g_set.menubar) MenuBarStart();
        DockStop(); if (g_set.dock) DockStart();
        TaskbarApply();
        return 0;
    }
    switch (m) {
    case WM_GG_TRAY:
        if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP) TrayMenu();
        return 0;
    case WM_HOTKEY:
        if (wp == HK_MISSION && g_set.missionControl) MissionControlToggle();
        else if (wp == HK_LAUNCHPAD && g_set.launchpad) LaunchpadToggle();
        return 0;
    case WM_APP + 50:   // close popup menus (from the menu mouse hook)
        ClosePopupMenus();
        return 0;
    case WM_APP + 60:   // switcher action: wp 0 step / 1 commit / 2 cancel, lp shift
        SwitcherAction((int)wp, lp != 0);
        return 0;
    case WM_APP + 70:   // spotlight (double-tap Ctrl)
        SpotlightToggle();
        return 0;
    case WM_APP + 71:   // a second launch: open Settings
        SettingsOpen();
        return 0;
    case WM_APP + 72:   // foreground app changed
        MenuBarRefresh();
        DockRefresh();
        MenuBarFullscreenSync();
        DockFullscreenSync();
        MenuBarReassert();
        DockReassert();
        return 0;
    case WM_GG_THEME:
        MenuBarRefresh();
        DockRefresh();
        return 0;
    case WM_GG_REFRESH:
        DockRefresh();
        MenuBarRefresh();
        return 0;
    case WM_GG_APPS:   // a background re-read of the app list finished
        if (AppsSwap(lp)) { SpotlightAppsChanged(); DockRefresh(); }
        return 0;
    case WM_CLIPBOARDUPDATE:
        ClipboardOnChange();
        return 0;
    case WM_SETTINGCHANGE:
        // display change / DPI / work area: rebuild bars and dock
        PostMessageW(h, WM_GG_REFRESH, 0, 0);
        return 0;
    case WM_DISPLAYCHANGE:
        MenuBarStop(); if (g_set.menubar) MenuBarStart();
        DockStop(); if (g_set.dock) DockStart();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static int RunRestore() {
    // undo everything, for the uninstaller: GoldenGate.exe --restore
    ThemeRestoreAll();
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dir = exe;
    g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/"));
    g_dataDir = JoinPath(KnownFolder(FOLDERID_RoamingAppData), L"GoldenGate");
    CreateDirectoryW(g_dataDir.c_str(), nullptr);

    LoadSettings();

    if (cmd && wcsstr(cmd, L"--restore")) return RunRestore();

    // --quit: shut a running copy down cleanly and wait for it (used by the
    // installer before replacing files, so updates actually take effect)
    if (cmd && wcsstr(cmd, L"--quit")) {
        HWND prev = FindRunningMain();
        if (prev) {
            DWORD pid = 0;
            GetWindowThreadProcessId(prev, &pid);
            HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
            PostMessageW(prev, WM_CLOSE, 0, 0);
            if (p) { WaitForSingleObject(p, 6000); CloseHandle(p); }
        }
        return 0;
    }

    // diagnostic: open the sound popup under the panel exactly as the volume icon does,
    // then exit after a few seconds. Runs beside a running copy (no UI, no lock).
    if (cmd && wcsstr(cmd, L"--test-flyout")) {
        OpenWindowsFlyout(wcsstr(cmd, L"--test-flyout=wifi") ? 1 : 0);
        SetTimer(nullptr, 0, 4000, [](HWND, UINT, UINT_PTR, DWORD) { PostQuitMessage(0); });
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
        return 0;
    }
    // diagnostic: every Start-menu app's Dock icon on one contact sheet (no UI)
    if (cmd && wcsstr(cmd, L"--test-icons=")) {
        extern int RunIconTest(const std::wstring &outPng);
        if (!GfxInit()) return 1;
        return RunIconTest(wcsstr(cmd, L"--test-icons=") + 13);
    }

    // single instance
    g_mtx = CreateMutexW(nullptr, TRUE, L"GoldenGate_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // after a crash the old copy may still be exiting: wait for its lock
        bool relaunched = cmd && wcsstr(cmd, L"--relaunched");
        DWORD w = relaunched ? WaitForSingleObject(g_mtx, 10000) : WAIT_TIMEOUT;
        if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
            // a second launch opens Settings in the running copy, except the
            // automatic one at sign-in (the logon task and the Run key fallback)
            HWND prev = FindRunningMain();
            if (prev && !(cmd && wcsstr(cmd, L"--autostart"))) PostMessageW(prev, WM_APP + 71, 0, 0);
            return 0;
        }
    }

    g_launchTick = GetTickCount64();
    SetUnhandledExceptionFilter(CrashFilter);
    // hide the Windows taskbar before anything slow (reading the whole Start menu,
    // wallpaper, building the UI), so at sign-in it's gone as soon as we start
    TaskbarApply();

    if (!GfxInit()) { MessageBoxW(nullptr, L"Direct2D is required.", L"Golden Gate", MB_ICONERROR); return 1; }
    UpdatePalette();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"GoldenGateMain";
    RegisterClassExW(&wc);
    g_main = CreateWindowExW(0, L"GoldenGateMain", L"Golden Gate", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);

    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    AllApps(true);
    ThemeApply(true);           // apply the look on first run
    AddTray();

    if (g_set.menubar) MenuBarStart();
    if (g_set.dock) DockStart();
    TaskbarApply();
    AutostartSync();   // make sure the sign-in task exists (runs in the background)
    CaffeineSet(g_set.caffeine);
    ClipboardStart();
    SwitcherStep(true);
    RegisterHotkeys();
    g_tapHook = SetWindowsHookExW(WH_KEYBOARD_LL, TapHook, inst, 0);
    g_tickTimer = SetTimer(g_main, 1, 1000, TickTimer);
    SetTimer(g_main, 2, 300, ReassertTimer);   // keep bar + Dock on top
    // a shake reverses direction several times in half a second; the 1s tick can't see it
    SetTimer(g_main, 3, 16, [](HWND, UINT, UINT_PTR, DWORD) { ShakeTimerTick(); });
    g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                               ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // each monitor's panel names the top app there, which also changes when a
    // window is dragged to another monitor or minimised without a focus change
    g_moveHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr,
                                 ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    FlyoutHooksStart();
    g_minHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
                                ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // welcome: open settings on the very first run
    if (!FileExists(JoinPath(g_dataDir, L"settings.ini"))) {
        SaveSettings();
        SettingsOpen();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // shut down cleanly (leave the theme as the user set it; only stop our UI)
    KillTimer(g_main, 1);
    KillTimer(g_main, 2);
    if (g_fgHook) UnhookWinEvent(g_fgHook);
    if (g_moveHook) UnhookWinEvent(g_moveHook);
    if (g_minHook) UnhookWinEvent(g_minHook);
    FlyoutHooksStop();
    if (g_tapHook) UnhookWindowsHookEx(g_tapHook);
    SwitcherStep(false);
    UnregisterHotkeys();
    PointerGrowStop();   // never leave a shake-enlarged arrow behind
    ClipboardStop();
    MenuBarStop();
    DockStop();
    TaskbarRestore();   // quitting must never leave you with no taskbar and no Dock
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    CoUninitialize();
    if (g_mtx) { ReleaseMutex(g_mtx); CloseHandle(g_mtx); }
    return 0;
}

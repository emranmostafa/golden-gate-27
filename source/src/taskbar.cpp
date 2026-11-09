// Hiding the Windows taskbar, safely: Explorer keeps running, so Start, the
// Action Center, toasts and Quick Settings still work. We only auto-hide it
// (documented AppBar state) or hide its window (ShowWindow), and the original
// state is restored by ThemeRestoreAll on uninstall and by TaskbarRestore.
#include "common.h"

static void ForEachTaskbar(std::function<void(HWND)> fn) {
    for (const wchar_t *cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        HWND t = nullptr;
        while ((t = FindWindowExW(nullptr, t, cls, nullptr))) fn(t);
    }
}

void TaskbarApply() {
    if (g_set.taskbar == 0) { TaskbarRestore(); return; }
    // Auto-hide first: this shrinks the reserved taskbar strip to ~1px so the
    // work area grows and maximized windows fill the screen (the Dock then
    // reserves its own strip at the very bottom).
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = ABS_AUTOHIDE | ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &abd);
    if (g_set.taskbar == 2) {
        // fully hidden: also hide the reveal strip on every monitor
        ForEachTaskbar([](HWND t) { ShowWindow(t, SW_HIDE); });
    } else {
        ForEachTaskbar([](HWND t) { ShowWindow(t, SW_SHOW); });
    }
}

// Explorer re-shows the taskbar on various events; keep it hidden while mode 2.
void TaskbarKeepHidden() {
    if (g_set.taskbar != 2) return;
    ForEachTaskbar([](HWND t) { if (IsWindowVisible(t)) ShowWindow(t, SW_HIDE); });
}

void TaskbarRestore() {
    APPBARDATA abd = {sizeof(abd)};
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &abd);
    ForEachTaskbar([](HWND t) { ShowWindow(t, SW_SHOW); });
}

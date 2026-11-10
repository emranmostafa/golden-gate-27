// Golden Gate for Windows 11 - shared declarations.
// A macOS-style desktop layer (menu bar, Dock, Control Center, Mission
// Control, Launchpad, Spotlight...) that runs next to Explorer and uses only
// documented Windows APIs: nothing is injected into other processes and no
// system file is changed.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- basics ---
template <class T> void SafeRelease(T *&p) { if (p) { p->Release(); p = nullptr; } }

extern HINSTANCE g_inst;
extern std::wstring g_dir;          // folder of GoldenGate.exe (the install folder)
extern std::wstring g_dataDir;      // %APPDATA%\GoldenGate

std::wstring Utf8ToWide(const std::string &s);
std::string WideToUtf8(const std::wstring &s);
std::wstring Lower(std::wstring s);
std::wstring FileNameOf(const std::wstring &path);
std::wstring JoinPath(const std::wstring &a, const std::wstring &b);
bool FileExists(const std::wstring &p);
std::wstring KnownFolder(REFKNOWNFOLDERID id);
float DpiScaleFor(HWND hwnd);
float DpiScaleForMonitor(HMONITOR mon);
void ShellOpen(const std::wstring &target, const std::wstring &args = L"");
void Log(const wchar_t *fmt, ...);

// registry helpers (HKCU unless stated)
bool RegGetDword(HKEY root, const wchar_t *key, const wchar_t *name, DWORD &out);
bool RegSetDword(HKEY root, const wchar_t *key, const wchar_t *name, DWORD v);
bool RegGetString(HKEY root, const wchar_t *key, const wchar_t *name, std::wstring &out);
bool RegSetString(HKEY root, const wchar_t *key, const wchar_t *name, const std::wstring &v, DWORD type = REG_SZ);
bool RegSetBinary(HKEY root, const wchar_t *key, const wchar_t *name, const void *data, DWORD size);

// ------------------------------------------------------------ settings ---
struct Settings {
    int style = 2;              // 0 light, 1 dark, 2 automatic (by time of day)
    int lightHour = 7, darkHour = 19;
    unsigned accent = 0x3478F6; // RGB
    std::wstring cursor = L"Bibata-Modern-Classic-Regular";
    int cursorSize = 32;        // px: 32, 48, 64 or 96 (sizes every Bibata file has art for)
    bool applyCursors = true, applyWallpaper = true, applyAccent = true;
    // menu bar / panel
    bool menubar = true, clock24 = false, clockSeconds = false, clockDate = true, clockCenter = true;
    bool showBattery = true;
    int panelHeight = 26;    // logical px
    int panelGlass = 45;     // 0 = clearest glass, 100 = solid (opacity)
    int panelTint = 15;      // 0 = neutral grey glass, 100 = strongly accent-tinted
    int glassPreset = 1;     // 0 Clear, 1 Balanced, 2 Tinted (quick presets)
    // dock
    bool dock = true, dockMagnify = true, dockAutohide = false, dockReserve = true;
    // how the Dock hides: 0 never (reserves space), 1 intellihide (hides only
    // when a window overlaps it, like macOS/Dash-to-Dock), 2 always auto-hide
    int dockHide = 1;
    int dockSize = 52, dockMagnifySize = 84;
    unsigned dockDotColor = 0;   // running dot: 0 automatic grey, 1 accent, else RGB
    int dlStyle = 0;            // Downloads stack: 0 fan, 1 grid, 2 automatic
    int iconStyle = 0;          // app icons: 0 original (as the taskbar shows), 1 macOS-style set
    std::vector<std::wstring> pinned;   // AppsFolder parsing names
    // features
    bool missionControl = true, hotCorner = true, switcher = false, spotlight = true;
    bool launchpad = true, clipboard = true, shakeFind = true, caffeine = false;
    // Windows taskbar: 0 leave as is, 1 auto-hide, 2 hide completely
    int taskbar = 2;
    bool startAtLogin = true;
};
extern Settings g_set;
void LoadSettings();
void AutostartSync();   // start at login: per-user sign-in task (Run key fallback)
void SaveSettings();

// ---------------------------------------------------------------- theme ---
struct Palette {
    bool dark;
    D2D1_COLOR_F text, textDim, textFaint, panel, panelStrong, hover, separator, accent, accentText, shadow;
};
extern Palette g_pal;
bool IsDarkNow();                     // what the theme engine wants right now
void ThemeApply(bool force = false);  // apply style, wallpaper, accent, cursors
void ThemeApplyCursors();
void ThemeRestoreAll();               // put back what Windows had before Golden Gate
void ThemeBackupOnce();
void UpdatePalette();

// ------------------------------------------------------------ graphics ---
extern ID2D1Factory *g_d2d;
extern IDWriteFactory *g_dw;
extern IWICImagingFactory *g_wic;
extern std::wstring g_font;           // UI font family ("Inter" or "Segoe UI Variable Text")
extern std::wstring g_iconFont;       // "Segoe Fluent Icons" or "Segoe MDL2 Assets"
bool GfxInit();
// real translucent acrylic blur (wallpaper/content behind shows through), tuned
// by an ABGR tint whose alpha sets how glassy it is. Used for the menu bar and
// the popup panels so they read as glass, not a solid bar.
void EnableAcrylic(HWND hwnd, unsigned abgrTint);
IDWriteTextFormat *Font(float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, bool icon = false);
float TextWidth(const std::wstring &s, float size, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL, bool icon = false);
float TextHeight(const std::wstring &s, float size, float maxWidth, DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL);
D2D1_COLOR_F Rgb(unsigned rgb, float a = 1.f);
D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a);
D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t);
IWICBitmapSource *LoadWicFile(const std::wstring &path);
IWICBitmapSource *WicFromHBitmap(HBITMAP hb);
IWICBitmapSource *WicFromHIcon(HICON hi);
IWICBitmapSource *NormalizeIcon(IWICBitmapSource *src);   // cached: same optical size for every icon

// A top-level window drawn with Direct2D.
//  - Glass: Windows 11 acrylic backdrop behind our drawing (menu bar, menus,
//    Control Center, Mission Control, settings). Falls back to a tinted fill.
//  - Layered: per-pixel transparent window (Dock, cursor finder, Downloads fan).
class Surface {
public:
    enum Kind { Glass, Layered };
    virtual ~Surface();
    bool Create(Kind kind, const wchar_t *title, DWORD exStyle, RECT r, HWND owner = nullptr, bool roundCorners = true);
    void Destroy();
    void Invalidate();
    void Move(RECT r);
    void Show(bool show, bool activate = false);
    bool Visible() const { return hwnd && IsWindowVisible(hwnd); }
    HWND hwnd = nullptr;
    float scale = 1.f;          // DPI scale (1.0 = 96 DPI)
    int width = 0, height = 0;  // pixels
    Kind kind = Glass;
    bool glassOk = false;
    // drawing in DIPs (logical pixels, divided by scale)
    ID2D1RenderTarget *rt = nullptr;
    ID2D1SolidColorBrush *brush = nullptr;
    void Fill(D2D1_RECT_F r, D2D1_COLOR_F c, float radius = 0);
    void Stroke(D2D1_RECT_F r, D2D1_COLOR_F c, float radius = 0, float w = 1.f);
    void Text(const std::wstring &s, D2D1_RECT_F r, D2D1_COLOR_F c, float size,
              DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL,
              DWRITE_TEXT_ALIGNMENT a = DWRITE_TEXT_ALIGNMENT_LEADING, bool icon = false);
    void TextWrap(const std::wstring &s, D2D1_RECT_F r, D2D1_COLOR_F c, float size);   // wraps onto more lines
    void Line(float x1, float y1, float x2, float y2, D2D1_COLOR_F c, float w = 1.f);
    void Circle(float cx, float cy, float r, D2D1_COLOR_F c, bool fill = true, float w = 1.f);
    void Bitmap(IWICBitmapSource *src, D2D1_RECT_F r, float opacity = 1.f);
    float W() const { return width / scale; }
    float H() const { return height / scale; }
protected:
    virtual void OnPaint() = 0;
    virtual LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp, bool &handled) { handled = false; return 0; }
    virtual void OnMouse(UINT msg, float x, float y, WPARAM wp) {}
    virtual void OnMouseLeave() {}
    virtual void OnKey(UINT vk) {}
    virtual void OnActivate(bool active) {}
    void Render();
    // D2D bitmaps per (source image, pre-scaled pixel size bucket)
    std::map<std::pair<IWICBitmapSource *, int>, ID2D1Bitmap *> bmpCache;
private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void MakeTarget();
    void DropTarget();
    bool tracking = false;
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
};

// ---------------------------------------------------------------- apps ---
struct AppEntry {
    std::wstring id;        // AppsFolder parsing name (AUMID or path)
    std::wstring name;      // display name
    std::wstring exe;       // target exe (desktop apps), lower-case
    std::wstring target;    // full path of that exe, when known
    std::wstring aumid;     // lower-case
    PIDLIST_ABSOLUTE pidl = nullptr;   // the Start-menu item itself (icon + launch), owned by the app list
};
std::vector<AppEntry> &AllApps(bool refresh = false);
const AppEntry *FindApp(const std::wstring &id);
const AppEntry *FindAppForWindow(const std::wstring &aumid, const std::wstring &exe);   // app ID wins over exe
std::wstring CanonicalAppId(const std::wstring &id);   // bare "app.exe" pins -> their Start-menu id
void LaunchApp(const std::wstring &id);
// cached. bundledFallback=false leaves it null when the shell has no icon, so a
// running app can try its real window icon before the bundled set.
IWICBitmapSource *AppIcon(const std::wstring &id, int size = 128, bool bundledFallback = true);
IWICBitmapSource *IconForExe(const std::wstring &exe, const std::wstring &aumid, int size = 128);
IWICBitmapSource *ThemedIcon(const std::wstring &key);                // assets/icons/<key>.png
IWICBitmapSource *ShellItemIcon(const std::wstring &parseName, int size = 128);   // file path or shell:AppsFolder\id
IWICBitmapSource *ExeNativeIcon(const std::wstring &exePath);   // an exe's own large icon when the shell won't use it

struct WinInfo { HWND hwnd; std::wstring title, exe, aumid; DWORD pid; };
std::vector<WinInfo> ListAppWindows(HMONITOR onlyOn = nullptr);
bool IsAppWindow(HWND h);
std::wstring WindowExe(HWND h, DWORD *pidOut = nullptr);
std::wstring WindowAumid(HWND h);
HWND RealAppWindow(HWND h);           // for ApplicationFrameHost (Store apps), the app inside
std::wstring AppNameFor(HWND h);
void ActivateWindow(HWND h);
void SendKeys(std::initializer_list<WORD> keys);
void ForceForeground(HWND h);   // focus one of our windows even when Windows' focus lock would refuse

// -------------------------------------------------------------- modules ---
void MenuBarStart();   void MenuBarStop();   void MenuBarRefresh();   void MenuBarReassert();   void MenuBarRetint();
void DockStart();      void DockStop();      void DockRefresh();      void DockReassert();      void DockPeriodicRefresh();
void TaskbarKeepHidden();
void ControlCenterToggle(RECT anchor);
void VolumeNudge(int steps);              // +/- 2% per step
void OpenWindowsFlyout(int which);       // 0 sound output, 1 Wi-Fi: Windows' own popup, under the panel
void FlyoutWatch();                       // keep Windows' Quick Settings popup under the panel while it's open
void FlyoutHooksStart(); void FlyoutHooksStop();   // place that popup the moment it's shown
void MissionControlToggle();
void SwitcherStep(bool start);
void LaunchpadToggle();
void SpotlightToggle();
void ClipboardStart(); void ClipboardStop(); void ClipboardShow(RECT anchor);
void CaffeineSet(bool on);
void ShakeTimerTick();
void PointerGrowStop();   // put the arrow back if a shake left it enlarged
void MenuBarTrack();   // per-monitor panels: rebuild any whose top app changed
void HotCornersTick();
void TaskbarApply();   void TaskbarRestore();
void SettingsOpen();
HWND SettingsHwnd();
void ShowConfirm(const std::wstring &title, const std::wstring &body, const std::wstring &okText, std::function<void()> ok);
void ShowAbout();

// popup menus drawn in the Golden Gate style
struct MenuItem {
    std::wstring text, shortcut;
    std::function<void()> action;
    std::vector<MenuItem> sub;
    bool separator = false, disabled = false, checked = false, header = false;
    static MenuItem Sep() { MenuItem m; m.separator = true; return m; }
};
void PopupMenu(std::vector<MenuItem> items, int x, int y, HWND owner = nullptr, float minWidth = 0);
void ClosePopupMenus();
bool PopupMenuOpen();
// Dock right-click on a running app: live previews of its windows + its actions
void ShowWindowPreviews(const std::vector<HWND> &windows, std::vector<MenuItem> actions, IWICBitmapSource *icon,
                        int anchorX, int anchorBottom);
bool WindowPreviewsOpen();

// messages and hotkeys
#define WM_GG_TRAY        (WM_APP + 1)
#define WM_GG_THEME       (WM_APP + 2)
#define WM_GG_REFRESH     (WM_APP + 3)
enum { HK_MISSION = 1, HK_SWITCH, HK_SPOTLIGHT, HK_LAUNCHPAD };
extern HWND g_main;                  // hidden message window
extern UINT g_msgTaskbarCreated;

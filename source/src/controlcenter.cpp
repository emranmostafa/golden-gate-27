// Control Center, designed like macOS Tahoe: a 2-column grid of frosted cards.
// Top row groups connectivity (Wi-Fi tile + Bluetooth / AirDrop square tiles)
// beside a Now Playing card; then Focus + two square tiles; full-width Display
// and Sound pill sliders with a bulging knob; and a row of round quick buttons
// (Appearance, Settings, Screenshot, Power). Opens at the top-right, like macOS.
// Uses documented APIs only (Core Audio for volume, WMI for brightness).
#include "common.h"
#include <powrprof.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wbemidl.h>

// ---- system volume via Core Audio ----
static IAudioEndpointVolume *VolEndpoint() {
    IMMDeviceEnumerator *en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return nullptr;
    IMMDevice *dev = nullptr;
    en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    en->Release();
    if (!dev) return nullptr;
    IAudioEndpointVolume *ep = nullptr;
    dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&ep);
    dev->Release();
    return ep;
}
static float GetVolume() { IAudioEndpointVolume *e = VolEndpoint(); float v = 0; if (e) { e->GetMasterVolumeLevelScalar(&v); e->Release(); } return v; }
static void SetVolume(float v) { IAudioEndpointVolume *e = VolEndpoint(); if (e) { e->SetMasterVolumeLevelScalar(std::max(0.f, std::min(1.f, v)), nullptr); e->Release(); } }
static bool GetMute() { IAudioEndpointVolume *e = VolEndpoint(); BOOL m = FALSE; if (e) { e->GetMute(&m); e->Release(); } return m; }
static void SetMute(bool m) { IAudioEndpointVolume *e = VolEndpoint(); if (e) { e->SetMute(m, nullptr); e->Release(); } }

// ---- display brightness via WMI (laptops); -1 when unavailable ----
static int GetBrightness() {
    int pct = -1;
    IWbemLocator *loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return -1;
    IWbemServices *svc = nullptr;
    BSTR ns = SysAllocString(L"root\\WMI");
    if (SUCCEEDED(loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc)) && svc) {
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        IEnumWbemClassObject *en = nullptr;
        BSTR q = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
        BSTR wql = SysAllocString(L"WQL");
        if (SUCCEEDED(svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY, nullptr, &en)) && en) {
            IWbemClassObject *o = nullptr; ULONG n = 0;
            if (en->Next(2000, 1, &o, &n) == S_OK && o) {
                VARIANT v; VariantInit(&v);
                if (SUCCEEDED(o->Get(L"CurrentBrightness", 0, &v, nullptr, nullptr))) pct = v.uintVal;
                VariantClear(&v); o->Release();
            }
            en->Release();
        }
        SysFreeString(q); SysFreeString(wql);
        svc->Release();
    }
    SysFreeString(ns);
    loc->Release();
    return pct;
}
static void SetBrightness(int pct) {
    IWbemLocator *loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&loc)))) return;
    IWbemServices *svc = nullptr;
    BSTR ns = SysAllocString(L"root\\WMI");
    if (SUCCEEDED(loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc)) && svc) {
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        IEnumWbemClassObject *en = nullptr;
        BSTR q = SysAllocString(L"SELECT * FROM WmiMonitorBrightnessMethods");
        BSTR wql = SysAllocString(L"WQL");
        if (SUCCEEDED(svc->ExecQuery(wql, q, WBEM_FLAG_FORWARD_ONLY, nullptr, &en)) && en) {
            IWbemClassObject *o = nullptr; ULONG n = 0;
            if (en->Next(2000, 1, &o, &n) == S_OK && o) {
                VARIANT path; VariantInit(&path);
                o->Get(L"__PATH", 0, &path, nullptr, nullptr);
                IWbemClassObject *in = nullptr, *sig = nullptr;
                BSTR cls = SysAllocString(L"WmiMonitorBrightnessMethods"), meth = SysAllocString(L"WmiSetBrightness");
                svc->GetObject(cls, 0, nullptr, &sig, nullptr);
                if (sig) {
                    IWbemClassObject *proto = nullptr;
                    sig->GetMethod(L"WmiSetBrightness", 0, &proto, nullptr);
                    if (proto) {
                        proto->SpawnInstance(0, &in);
                        if (in) {
                            VARIANT t, b; VariantInit(&t); VariantInit(&b);
                            t.vt = VT_UI4; t.uintVal = 1;
                            b.vt = VT_UI1; b.bVal = (BYTE)std::max(0, std::min(100, pct));
                            in->Put(L"Timeout", 0, &t, 0);
                            in->Put(L"Brightness", 0, &b, 0);
                            svc->ExecMethod(path.bstrVal, meth, 0, nullptr, in, nullptr, nullptr);
                            in->Release();
                        }
                        proto->Release();
                    }
                    sig->Release();
                }
                SysFreeString(cls); SysFreeString(meth);
                VariantClear(&path); o->Release();
            }
            en->Release();
        }
        SysFreeString(q); SysFreeString(wql);
        svc->Release();
    }
    SysFreeString(ns);
    loc->Release();
}

struct Hit { D2D1_RECT_F r; int id; };

struct CCenter : Surface {
    float volume = 0.5f;
    bool mute = false;
    int brightness = -1;
    std::vector<Hit> hits;

    void Refresh() { volume = GetVolume(); mute = GetMute(); brightness = GetBrightness(); }
    void OnPaint() override;
    void OnMouse(UINT msg, float x, float y, WPARAM) override;
    void OnActivate(bool a) override { if (!a) Show(false); }
    // building blocks, all drawn like macOS Control Center
    void Card(D2D1_RECT_F r, float rad = 16) {
        Fill(r, g_pal.dark ? Rgb(0x2C2C2E, 0.62f) : Rgb(0xFFFFFF, 0.58f), rad);
    }
    void IconCircle(float cx, float cy, float r, const wchar_t *glyph, bool on, D2D1_COLOR_F onColor);
    void TileBtn(D2D1_RECT_F r, const wchar_t *glyph, bool on, D2D1_COLOR_F col, int id, float gsize = 19);
    void SliderCard(float x, float y, float w, const wchar_t *label, const wchar_t *glyph, float val, int id);
    void QuickCircle(float cx, float cy, const wchar_t *glyph, int id, bool on = false);
};
static CCenter *g_cc = nullptr;

// layout constants (logical px); panel width matches macOS Control Center
static const float CW = 326, PAD = 14, GAP = 10;
static const float GROUP_H = 56 + GAP + 50;   // Wi-Fi tile + BT/AirDrop row
static const float ROWB_H = 62;               // Focus + square tiles
static const float SLIDER_H = 64;
static float CCHeight() {
    float y = PAD;
    y += GROUP_H + GAP;          // connectivity + Now Playing
    y += ROWB_H + GAP;           // Focus + 2 square tiles
    y += SLIDER_H + GAP;         // Display
    y += SLIDER_H + GAP;         // Sound
    y += 48;                     // bottom quick circles (radius 24)
    return y + PAD;
}

void CCenter::IconCircle(float cx, float cy, float r, const wchar_t *glyph, bool on, D2D1_COLOR_F onColor) {
    Circle(cx, cy, r, on ? onColor : (g_pal.dark ? Rgb(0x636366) : Rgb(0xDFDFE3)), true);
    Text(glyph, D2D1::RectF(cx - r, cy - r, cx + r, cy + r), on ? D2D1::ColorF(1, 1, 1, 1) : g_pal.text,
         r * 1.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
}

// a rounded-square icon tile (Bluetooth / AirDrop / Mission / Awake). When on,
// it fills with its accent colour and the glyph turns white, like macOS.
void CCenter::TileBtn(D2D1_RECT_F r, const wchar_t *glyph, bool on, D2D1_COLOR_F col, int id, float gsize) {
    if (on) Fill(r, col, 16);
    else Card(r);
    float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    Text(glyph, D2D1::RectF(cx - 20, cy - 20, cx + 20, cy + 20),
         on ? D2D1::ColorF(1, 1, 1, 1) : g_pal.text, gsize,
         DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    hits.push_back({r, id});
}

// a full-width slider card (Display / Sound): label on top, then a tall pill
// track with a bulging white knob and the glyph inside the left end.
void CCenter::SliderCard(float x, float y, float w, const wchar_t *label, const wchar_t *glyph, float val, int id) {
    D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + SLIDER_H);
    Card(r);
    Text(label, D2D1::RectF(x + 16, y + 8, x + w - 16, y + 26), g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float px = x + 14, pw = w - 28, py = y + 30, ph = 24;
    val = std::max(0.f, std::min(1.f, val));
    // track
    Fill(D2D1::RectF(px, py, px + pw, py + ph), g_pal.dark ? Rgb(0x48484A, 0.95f) : Rgb(0xE4E4E8, 0.98f), ph / 2);
    float knobX = px + ph / 2 + (pw - ph) * val;
    // filled portion
    Fill(D2D1::RectF(px, py, knobX, py + ph), g_pal.dark ? Rgb(0xD8D8DC) : Rgb(0xFFFFFF), ph / 2);
    // glyph inside the left end
    Text(glyph, D2D1::RectF(px + 9, py, px + 9 + ph, py + ph), Rgb(0x8A8A8E), ph * 0.52f,
         DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    // bulging knob
    Circle(knobX, py + ph / 2, ph / 2 + 2, g_pal.shadow, true);         // soft base
    Circle(knobX, py + ph / 2, ph / 2 + 1.5f, Rgb(0xFFFFFF), true);
    hits.push_back({D2D1::RectF(px, y, px + pw, y + SLIDER_H), id});
}

void CCenter::QuickCircle(float cx, float cy, const wchar_t *glyph, int id, bool on) {
    float r = 24;
    D2D1_COLOR_F blue = Rgb(0x0A84FF);
    Circle(cx, cy, r, on ? blue : WithAlpha(blue, g_pal.dark ? 0.35f : 0.20f), true);
    Text(glyph, D2D1::RectF(cx - r, cy - r, cx + r, cy + r),
         on ? D2D1::ColorF(1, 1, 1, 1) : blue, 18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    hits.push_back({D2D1::RectF(cx - r, cy - r, cx + r, cy + r), id});
}

void CCenter::OnPaint() {
    hits.clear();
    float w = W();
    // whole-panel frosted backing (over the acrylic blur)
    Fill(D2D1::RectF(0, 0, w, H()), g_pal.dark ? Rgb(0x1C1C1E, 0.42f) : Rgb(0xF2F2F5, 0.42f), 0);
    float x = PAD, y = PAD, inner = w - PAD * 2, halfW = (inner - GAP) / 2;
    D2D1_COLOR_F blue = Rgb(0x0A84FF), orange = Rgb(0xFF9F0A), purple = Rgb(0xBF5AF2);

    // ---- Row A: connectivity group (left) + Now Playing (right)
    // Wi-Fi tile
    D2D1_RECT_F wifi = D2D1::RectF(x, y, x + halfW, y + 56);
    Card(wifi);
    IconCircle(x + 30, y + 28, 16, L"", true, blue);
    Text(L"Wi-Fi", D2D1::RectF(x + 54, y + 11, x + halfW - 8, y + 31), g_pal.text, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(L"Connected", D2D1::RectF(x + 54, y + 29, x + halfW - 8, y + 47), g_pal.textDim, 11);
    hits.push_back({wifi, 10});
    // Bluetooth + AirDrop square tiles under Wi-Fi
    float subW = (halfW - GAP) / 2, subY = y + 56 + GAP, subH = 50;
    TileBtn(D2D1::RectF(x, subY, x + subW, subY + subH), L"", true, blue, 11);
    TileBtn(D2D1::RectF(x + subW + GAP, subY, x + halfW, subY + subH), L"", false, blue, 12);

    // Now Playing card (right)
    float npx = x + halfW + GAP;
    D2D1_RECT_F np = D2D1::RectF(npx, y, npx + halfW, y + GROUP_H);
    Card(np);
    Fill(D2D1::RectF(npx + 14, y + 13, npx + 54, y + 53), g_pal.dark ? Rgb(0x48484A, 0.85f) : Rgb(0xD8D8DC, 0.95f), 9);
    Text(L"", D2D1::RectF(npx + 14, y + 13, npx + 54, y + 53), g_pal.textFaint, 18,
         DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    Text(L"Now Playing", D2D1::RectF(npx + 14, y + 60, npx + halfW - 10, y + 80), g_pal.text, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float ty = y + GROUP_H - 26;
    float t1 = npx + halfW * 0.24f, t2 = npx + halfW * 0.5f, t3 = npx + halfW * 0.76f;
    Text(L"", D2D1::RectF(t1 - 16, ty - 14, t1 + 16, ty + 14), g_pal.text, 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    Text(L"", D2D1::RectF(t2 - 16, ty - 14, t2 + 16, ty + 14), g_pal.text, 18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    Text(L"", D2D1::RectF(t3 - 16, ty - 14, t3 + 16, ty + 14), g_pal.text, 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true);
    hits.push_back({D2D1::RectF(t1 - 18, ty - 16, t1 + 18, ty + 16), 50});
    hits.push_back({D2D1::RectF(t2 - 18, ty - 16, t2 + 18, ty + 16), 51});
    hits.push_back({D2D1::RectF(t3 - 18, ty - 16, t3 + 18, ty + 16), 52});
    y += GROUP_H + GAP;

    // ---- Row B: Focus (left) + Mission / Awake square tiles (right)
    D2D1_RECT_F focus = D2D1::RectF(x, y, x + halfW, y + ROWB_H);
    Card(focus);
    IconCircle(x + 30, y + 31, 16, L"", false, purple);
    Text(L"Focus", D2D1::RectF(x + 54, y + 14, x + halfW - 8, y + 34), g_pal.text, 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(L"Do Not Disturb", D2D1::RectF(x + 54, y + 32, x + halfW - 8, y + 50), g_pal.textDim, 10.5f);
    hits.push_back({focus, 21});
    float sqW = (halfW - GAP) / 2, sqx1 = x + halfW + GAP, sqx2 = sqx1 + sqW + GAP;
    TileBtn(D2D1::RectF(sqx1, y, sqx1 + sqW, y + ROWB_H), L"", false, blue, 23);
    TileBtn(D2D1::RectF(sqx2, y, sqx2 + sqW, y + ROWB_H), L"", g_set.caffeine, orange, 22);
    y += ROWB_H + GAP;

    // ---- Display + Sound sliders
    SliderCard(x, y, inner, L"Display", L"", brightness >= 0 ? brightness / 100.f : 0.75f, 30);
    y += SLIDER_H + GAP;
    SliderCard(x, y, inner, L"Sound", mute ? L"" : L"", mute ? 0 : volume, 31);
    y += SLIDER_H + GAP;

    // ---- bottom quick circles: appearance, settings, screenshot, power
    float qcy = y + 24;
    const wchar_t *qg[] = {L"", L"", L"", L""};
    int qid[] = {20, 40, 42, 41};
    for (int i = 0; i < 4; i++)
        QuickCircle(x + inner * (i + 0.5f) / 4, qcy, qg[i], qid[i]);
}

void CCenter::OnMouse(UINT msg, float x, float y, WPARAM) {
    // slider drag (brightness / volume)
    if ((msg == WM_MOUSEMOVE && (GetKeyState(VK_LBUTTON) & 0x8000)) || msg == WM_LBUTTONDOWN) {
        for (auto &h : hits) {
            if ((h.id == 30 || h.id == 31) && y >= h.r.top - 6 && y <= h.r.bottom + 6 && x >= h.r.left - 10 && x <= h.r.right + 10) {
                float v = (x - h.r.left) / (h.r.right - h.r.left);
                v = std::max(0.f, std::min(1.f, v));
                if (h.id == 31) { if (mute && v > 0) { mute = false; SetMute(false); } volume = v; SetVolume(v); }
                else { brightness = (int)(v * 100 + 0.5f); SetBrightness(brightness); }
                Invalidate();
                return;
            }
        }
    }
    if (msg != WM_LBUTTONUP) return;
    for (auto &h : hits) {
        if (x < h.r.left || x > h.r.right || y < h.r.top || y > h.r.bottom) continue;
        // controls that keep the panel open
        bool keepOpen = (h.id == 20 || h.id == 22 || h.id == 50 || h.id == 51 || h.id == 52);
        switch (h.id) {
        case 10: ShellOpen(L"ms-availablenetworks:"); break;                                   // Wi-Fi
        case 11: ShellOpen(L"ms-settings:bluetooth"); break;                                   // Bluetooth
        case 12: ShellOpen(L"ms-settings:crossdevice"); break;                                 // AirDrop/Nearby
        case 20: g_set.style = g_pal.dark ? 0 : 1; SaveSettings(); ThemeApply(true); break;    // appearance
        case 21: ShellOpen(L"ms-settings:quiethours"); break;                                  // focus
        case 22: g_set.caffeine = !g_set.caffeine; CaffeineSet(g_set.caffeine); SaveSettings(); MenuBarRefresh(); break; // awake
        case 23: Show(false); MissionControlToggle(); break;                                   // mission control
        case 40: Show(false); ShellOpen(L"ms-settings:"); break;                               // settings
        case 42: Show(false); SendKeys({VK_LWIN, VK_SHIFT, 'S'}); return;                       // screenshot
        case 50: SendKeys({VK_MEDIA_PREV_TRACK}); return;                                       // previous
        case 51: SendKeys({VK_MEDIA_PLAY_PAUSE}); return;                                       // play/pause
        case 52: SendKeys({VK_MEDIA_NEXT_TRACK}); return;                                       // next
        case 41: {                                                                             // power
            RECT wr; GetWindowRect(hwnd, &wr);
            int px = wr.left + (int)(h.r.left * scale), py = wr.top + (int)(h.r.bottom * scale);
            Show(false);
            std::vector<MenuItem> m = {
                {L"Lock Screen", L"", []() { LockWorkStation(); }},
                {L"Sleep", L"", []() { SetSuspendState(FALSE, FALSE, FALSE); }},
                MenuItem::Sep(),
                {L"Restart…", L"", []() { ShowConfirm(L"Restart", L"Close all apps and restart this PC?", L"Restart",
                    []() { ShellOpen(L"shutdown.exe", L"/r /t 0"); }); }},
                {L"Shut Down…", L"", []() { ShowConfirm(L"Shut Down", L"Close all apps and shut down this PC?", L"Shut Down",
                    []() { ShellOpen(L"shutdown.exe", L"/s /t 0"); }); }},
            };
            PopupMenu(m, px, py, nullptr, 170);
            return;
        }
        }
        Invalidate();
        if (!keepOpen) Show(false);
        return;
    }
}

void ControlCenterToggle(RECT anchor) {
    if (g_cc && g_cc->Visible()) { g_cc->Show(false); return; }
    if (!g_cc) g_cc = new CCenter();
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    float sc = DpiScaleForMonitor(mon);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);
    int w = (int)(CW * sc), h = (int)(CCHeight() * sc + 0.5f);
    int x = mi.rcWork.right - w - (int)(8 * sc);
    int top = anchor.bottom + (int)(4 * sc);
    RECT r = {x, top, x + w, top + h};
    if (!g_cc->hwnd) { g_cc->Create(Surface::Glass, L"GoldenGateControlCenter", WS_EX_TOPMOST, r, nullptr, true); }
    else g_cc->Move(r);
    g_cc->Refresh();
    g_cc->Show(true, true);
    SetForegroundWindow(g_cc->hwnd);
    g_cc->Invalidate();
}

// scrolling over the panel's volume icon: +/- 2% per notch (clicking it opens
// Windows' own audio panel, see menubar.cpp)
void VolumeNudge(int steps) {
    if (steps > 0 && GetMute()) SetMute(false);
    SetVolume(GetVolume() + steps * 0.02f);
}

// main.cpp - window, layer editing, presets, tray icon, settings
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <math.h>
#include <stdio.h>
#include "audio.h"
#include "resource.h"
#include "i18n.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

#define APP_CLASS   L"DeskNoiseWnd"
#define APP_TITLE   L"DeskNoise"
#define APP_MUTEX   L"DeskNoise.SingleInstance.v1"
#define RUN_VALUE   L"DeskNoise"
#define CONFIG_VER  4

// Title bar text. Debug builds are tagged so the running build is obvious.
#ifdef _DEBUG
#define APP_CAPTION APP_TITLE L" v" APP_VERSION_STRW L" (Debug)"
#else
#define APP_CAPTION APP_TITLE L" v" APP_VERSION_STRW
#endif

#define WM_TRAY        (WM_APP + 1)
#define WM_SHOWME      (WM_APP + 2)
#define HOTKEY_TOGGLE  1

enum {
    IDC_PRESET_COMBO = 1001, IDC_PRESET_SAVE, IDC_PRESET_DEL,
    IDC_MODE, IDC_FREQ_SLIDER, IDC_FREQ_EDIT, IDC_BW_SLIDER, IDC_VOL_SLIDER, IDC_BAL_SLIDER,
    IDC_BEAT_SLIDER, IDC_CHK_BEAT,
    IDC_PLAYMIN, IDC_RESTMIN, IDC_FADEMIN, IDC_PLAY, IDC_HIDE, IDC_EXIT, IDC_STATUS,
    IDC_MASTER_SLIDER, IDC_LBL_MASTER,
    IDC_LBL_FREQ, IDC_LBL_MID, IDC_LBL_VOL, IDC_LBL_BAL,
    IDC_STATUS_TIME,
    IDC_LAYER_EN = 1100,    // +0..kMaxLayers
    IDC_LAYER_SEL = 1110,   // +0..kMaxLayers
    IDM_TRAY_TOGGLE = 2001, IDM_TRAY_SHOW, IDM_TRAY_EXIT,
    IDM_TRAY_ABOUT, IDM_TRAY_RESTART
};

// Layout grid. The left gutter holds a checkbox now and can hold a small icon
// later, so every title starts at kTextX and lines up whether or not the row
// has a checkbox of its own.
static const int kGutterX = 24;    // checkbox / icon column
static const int kTextX = 48;      // every title and label starts here
static const int kCtrlX = 44;      // sliders
static const int kCtrlW = 314;     // sliders stop at 358, inside the box border
static const int kRightX = 378;    // right edge of content
static const int kWinW = 402;
static const int kWinH = 668;

// Group boxes span the full content width; what they hold is inset from both
// sides by the same amount, which is why the sliders stop short of kRightX.
static const int kBoxX = kGutterX;
static const int kBoxW = kRightX - kBoxX;
static const int kBoxATop = 183, kBoxABottom = 446;
static const int kBoxBTop = 464, kBoxBBottom = 574;
static const int kCaptionX = 40;

// Play and rest durations in minutes. 0 means "continuous" and "none".
static const int  kPlayMin[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90 };
static const int  kRestMin[] = { 0, 1, 2, 3, 5, 10, 15, 20, 30 };
// Length of the ramp down to silence at the end of the play phase. 0 is off.
static const int  kFadeMin[] = { 0, 5, 10, 20, 30 };
static const int  kPlayCount = 9;
static const int  kRestCount = 9;
static const int  kFadeCount = 5;
static const UINT_PTR kTickTimer = 1;

enum { PHASE_PLAY = 0, PHASE_REST = 1 };
static const int  kMaxPresets = 24;

// Mode order matches audio.h, so the mode value indexes both string blocks.
static const WCHAR* ModeName(int mode)  { return T(S_MODE_SINE + mode); }
static const WCHAR* ModeShort(int mode) { return T(S_MODES_SINE + mode); }

// UI-side values for one layer.
// Frequency and bandwidth are kept as real numbers. Storing them as slider
// steps would snap a typed 4000Hz to the nearest step, 3990Hz.
struct LayerCfg {
    bool   on;
    int    mode;
    double freq;      // Hz
    double bw;        // octaves
    int    vol;       // 0..100
    int    bal;       // 0..100, 50 is center
    bool   beat;      // pure-tone beat enabled
    double beatHz;    // 0.2 ~ 12.0
};

static HINSTANCE   g_inst;
static HWND        g_hwnd;
static HFONT       g_font;
static HFONT       g_fontBold;
static AudioEngine g_audio;
static NOTIFYICONDATAW g_nid = {};
static bool        g_trayAdded = false;
static bool        g_trayV4 = false;      // NIM_SETVERSION accepted NOTIFYICON_VERSION_4
static bool        g_updating = false;   // guards against control-update feedback
static bool        g_realExit = false;
static int         g_dpi = 96;
static bool        g_session = false;    // user turned it on; stays true during rest
static int         g_phase = PHASE_PLAY;
static ULONGLONG   g_phaseEnd = 0;       // when the current phase ends (0 = open ended)
static ULONGLONG   g_playedMs = 0;       // playing time so far in this session, rest excluded
static ULONGLONG   g_playSince = 0;      // start of the current play phase, 0 while not playing
static WCHAR       g_iniPath[MAX_PATH] = {};
static int         g_fileVer = CONFIG_VER;   // config version as found on disk
static HANDLE      g_mutex = nullptr;        // single-instance guard, released on restart
static WCHAR       g_cmdLine[256] = {};      // kept so a restart can pass the same switches
static LayerCfg    g_layer[kMaxLayers] = {};
static int         g_sel = 0;            // layer being edited
static WCHAR       g_nameBuf[64] = {};

static HWND hPresetCombo, hPresetSave, hPresetDel;
static HWND hLayerEn[kMaxLayers], hLayerSel[kMaxLayers];
static HWND hMode, hFreqSlider, hFreqEdit, hBwSlider, hBeatSlider, hChkBeat;
static HWND hVolSlider, hBalSlider;
static HWND hPlayMin, hRestMin, hFadeMin, hMaster, hLblMaster;
// 1.0 is the master volume as set; the fade walks it down to 0 without moving
// the slider, so the next session starts back at the level the user chose.
static double g_fadeScale = 1.0;
static HWND hPlay, hHide, hExit, hStatus, hStatusTime, hLblFreq, hLblMid, hLblVol, hLblBal;
static HWND hLblBoxA, hLblBoxB;

// Defined with the other frame drawing, but needed by the label updates above it.
static void FitCaption(HWND h, LPCWSTR text, int x, int boxTop);

// ---------------------------------------------------------------- conversions

static const double kFreqMin = 100.0, kFreqMax = 14000.0;
// The top of the bandwidth range means "no band filter", so the slider reaches
// past any useful band width. kBwFull mirrors kBwOpen on the audio side.
static const double kBwMin = 0.05, kBwMax = 4.0;
static const double kBwFull = (double)kBwOpen;
static const double kBeatMin = 0.2, kBeatMax = 12.0;

static double PosToFreq(int pos)
{
    return kFreqMin * pow(kFreqMax / kFreqMin, pos / 1000.0);
}
static int FreqToPos(double hz)
{
    if (hz < kFreqMin) hz = kFreqMin;
    if (hz > kFreqMax) hz = kFreqMax;
    int p = (int)(1000.0 * log(hz / kFreqMin) / log(kFreqMax / kFreqMin) + 0.5);
    return p < 0 ? 0 : (p > 1000 ? 1000 : p);
}
static double PosToBw(int pos)
{
    return kBwMin * pow(kBwMax / kBwMin, pos / 1000.0);
}
static int BwToPos(double oct)
{
    if (oct < kBwMin) oct = kBwMin;
    if (oct > kBwMax) oct = kBwMax;
    return (int)(1000.0 * log(oct / kBwMin) / log(kBwMax / kBwMin) + 0.5);
}
static double PosToBeat(int pos) { return pos / 10.0; }              // slider steps are 0.1Hz
static int    BeatToPos(double hz) { return (int)(hz * 10.0 + 0.5); }
// Percent sliders run 0..1000 so the thumb glides like the frequency slider
// instead of snapping every 3.5 pixels. The value they carry is still whole
// percent; only the travel is finer.
static const int kPctSteps = 1000;
static int PctFromPos(int pos) { return (pos + 5) / 10; }
static int PctToPos(int pct)   { return pct * 10; }

static double VolToDb(int pos) { return -50.0 + pos * 0.50; }        // 0..100 -> -50..0 dB
static float VolToGain(int pos)
{
    if (pos <= 0) return 0.0f;
    return (float)pow(10.0, VolToDb(pos) / 20.0);
}
// Every mode except a pure tone can be band-filtered, and the filter is what
// gives a noise a frequency. A fully open band means the frequency is unused.
static bool ModeUsesBw(int m)          { return m != MODE_SINE; }
static bool BandOpen(const LayerCfg& L) { return ModeUsesBw(L.mode) && L.bw >= kBwFull; }
static bool UsesFreq(const LayerCfg& L) { return !BandOpen(L); }
static double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- DPI

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void QueryDpi(HWND hwnd)
{
    typedef UINT(WINAPI* PFN)(HWND);
    static PFN pGetDpiForWindow = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) pGetDpiForWindow = (PFN)GetProcAddress(u, "GetDpiForWindow");
    }
    if (pGetDpiForWindow && hwnd) {
        UINT d = pGetDpiForWindow(hwnd);
        if (d >= 72) { g_dpi = (int)d; return; }
    }
    HDC dc = GetDC(nullptr);
    if (dc) { g_dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    if (g_dpi < 72) g_dpi = 96;
}

static void MakeFont()
{
    if (g_font) { DeleteObject(g_font); g_font = nullptr; }
    if (g_fontBold) { DeleteObject(g_fontBold); g_fontBold = nullptr; }
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        LOGFONTW lf = ncm.lfMessageFont;
        lf.lfHeight = -MulDiv(9, g_dpi, 72);
        g_font = CreateFontIndirectW(&lf);
        // Same face and size, heavier weight: only the top-level titles use it.
        lf.lfWeight = FW_BOLD;
        g_fontBold = CreateFontIndirectW(&lf);
    }
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_fontBold) g_fontBold = g_font;
}

// ---------------------------------------------------------------- drawing colours

// Darkens the window background by a percentage, so every drawn line tracks the
// theme instead of being a fixed grey.
static COLORREF ShadeFace(int pct)
{
    const COLORREF f = GetSysColor(COLOR_BTNFACE);
    return RGB(GetRValue(f) * pct / 100, GetGValue(f) * pct / 100, GetBValue(f) * pct / 100);
}

static COLORREF RuleColor()   { return ShadeFace(88); }
static COLORREF FieldBorder() { return ShadeFace(62); }

// One colour per layer, carried by that layer's checkbox, its row marker and,
// while it is the layer being edited, the sliders below.
static COLORREF LayerColor(int i)
{
    // Hues about 230, 185 and 78 degrees. The first was moved towards violet and
    // the last towards yellow, because blue and teal sat too close to tell apart
    // at checkbox size.
    static const COLORREF kAccent[] = {
        RGB(0x3A, 0x55, 0xD6),   // blue
        RGB(0x0F, 0xA8, 0xB4),   // teal
        RGB(0x74, 0xA0, 0x14),   // yellow-green
    };
    return kAccent[(i < 0 || i >= kMaxLayers) ? 0 : i];
}

static void FillRound(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

// ---------------------------------------------------------------- settings file

static void IniSetInt(LPCWSTR sec, LPCWSTR key, int v)
{
    WCHAR buf[32]; wsprintfW(buf, L"%d", v);
    WritePrivateProfileStringW(sec, key, buf, g_iniPath);
}
static int IniGetInt(LPCWSTR sec, LPCWSTR key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, g_iniPath);
}
static void IniSetStr(LPCWSTR sec, LPCWSTR key, LPCWSTR v)
{
    WritePrivateProfileStringW(sec, key, v, g_iniPath);
}
static void IniGetStr(LPCWSTR sec, LPCWSTR key, LPWSTR out, int cch)
{
    GetPrivateProfileStringW(sec, key, L"", out, cch, g_iniPath);
}

static void InitIniPath()
{
    WCHAR dir[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, dir))) return;
    lstrcatW(dir, L"\\DeskNoise");
    CreateDirectoryW(dir, nullptr);
    wsprintfW(g_iniPath, L"%s\\config.ini", dir);

    if (GetFileAttributesW(g_iniPath) != INVALID_FILE_ATTRIBUTES) {
        const int have = IniGetInt(L"main", L"ver", 0);
        g_fileVer = have;
        if (have == 0) {
            // Version 1.0 file. The layout is different enough to be unusable.
            DeleteFileW(g_iniPath);
            g_fileVer = CONFIG_VER;
        }
        else if (have < 3) {
            // Default volume changed, so reset the live settings but keep presets.
            WritePrivateProfileStringW(L"cur", nullptr, nullptr, g_iniPath);
        }
    }

    // A UTF-16 BOM makes the profile API read and write Unicode. Without it,
    // Korean preset names break on systems with a different code page.
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_iniPath, GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD wr = 0;
            WriteFile(f, bom, 2, &wr, nullptr);
            CloseHandle(f);
        }
    }
    IniSetInt(L"main", L"ver", CONFIG_VER);
}

static void DefaultLayers(LayerCfg L[kMaxLayers])
{
    for (int i = 0; i < kMaxLayers; i++) {
        L[i].on = false;
        L[i].mode = MODE_SINE;
        L[i].freq = 4000.0;
        L[i].bw = kBwMax;      // no band filter unless the layer asks for one
        L[i].vol = 70;
        L[i].bal = 50;
        L[i].beat = false;
        L[i].beatHz = 3.0;
    }
    L[0].on = true;  L[0].mode = MODE_SINE;       L[0].freq = 4000.0; L[0].vol = 85;
    L[1].mode = MODE_NARROWBAND; L[1].freq = 6000.0; L[1].vol = 70; L[1].bw = 0.5;
    L[2].mode = MODE_PINK;  L[2].vol = 60;
}

static void LayersToIni(LPCWSTR sec, const LayerCfg L[kMaxLayers])
{
    WCHAR key[24];
    for (int i = 0; i < kMaxLayers; i++) {
        wsprintfW(key, L"l%don", i);    IniSetInt(sec, key, L[i].on ? 1 : 0);
        wsprintfW(key, L"l%dmode", i);  IniSetInt(sec, key, L[i].mode);
        wsprintfW(key, L"l%dfreq", i);  IniSetInt(sec, key, (int)(L[i].freq + 0.5));
        wsprintfW(key, L"l%dbw", i);    IniSetInt(sec, key, (int)(L[i].bw * 100.0 + 0.5));
        wsprintfW(key, L"l%dvol", i);   IniSetInt(sec, key, L[i].vol);
        wsprintfW(key, L"l%dbal", i);   IniSetInt(sec, key, L[i].bal);
        wsprintfW(key, L"l%dbeat", i);  IniSetInt(sec, key, L[i].beat ? 1 : 0);
        wsprintfW(key, L"l%dbeatr", i); IniSetInt(sec, key, (int)(L[i].beatHz * 10.0 + 0.5));
    }
}

static void LayersFromIni(LPCWSTR sec, LayerCfg L[kMaxLayers])
{
    LayerCfg def[kMaxLayers];
    DefaultLayers(def);
    WCHAR key[24];
    for (int i = 0; i < kMaxLayers; i++) {
        wsprintfW(key, L"l%don", i);
        L[i].on = IniGetInt(sec, key, def[i].on ? 1 : 0) != 0;
        wsprintfW(key, L"l%dmode", i);
        L[i].mode = IniGetInt(sec, key, def[i].mode);
        if (L[i].mode < 0 || L[i].mode > MODE_BROWN) L[i].mode = MODE_SINE;
        wsprintfW(key, L"l%dfreq", i);
        L[i].freq = Clamp(IniGetInt(sec, key, (int)(def[i].freq + 0.5)), kFreqMin, kFreqMax);
        wsprintfW(key, L"l%dbw", i);
        L[i].bw = Clamp(IniGetInt(sec, key, (int)(def[i].bw * 100.0 + 0.5)) / 100.0, kBwMin, kBwMax);
        wsprintfW(key, L"l%dvol", i);
        L[i].vol = (int)Clamp(IniGetInt(sec, key, def[i].vol), 0, 100);
        wsprintfW(key, L"l%dbal", i);
        L[i].bal = (int)Clamp(IniGetInt(sec, key, def[i].bal), 0, 100);
        wsprintfW(key, L"l%dbeat", i);
        L[i].beat = IniGetInt(sec, key, def[i].beat ? 1 : 0) != 0;
        wsprintfW(key, L"l%dbeatr", i);
        L[i].beatHz = Clamp(IniGetInt(sec, key, (int)(def[i].beatHz * 10.0 + 0.5)) / 10.0, kBeatMin, kBeatMax);

        // Before version 4 the band filter reached only narrow-band noise, so a
        // stored bandwidth on a broadband layer never applied. Open it fully,
        // otherwise the layer would suddenly come back band-passed.
        if (g_fileVer < 4 && L[i].mode >= MODE_WHITE) L[i].bw = kBwMax;
    }
}

// ---------------------------------------------------------------- run at startup

// "Run at Windows startup" is gone, but an earlier build may have written the
// Run value. Without this an old user would keep launching at boot with nothing
// in the app to turn it off. Runs once per start and costs nothing when absent.
static void ClearStartupEntry()
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(k, RUN_VALUE);
    RegCloseKey(k);
}

// ---------------------------------------------------------------- tray icon

static HICON LoadAppIcon(int cx, int cy)
{
    return (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

static void TrayAdd()
{
    if (g_trayAdded) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    lstrcpynW(g_nid.szTip, APP_TITLE, ARRAYSIZE(g_nid.szTip));
    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_nid.uVersion = NOTIFYICON_VERSION_4;
        g_trayV4 = (Shell_NotifyIconW(NIM_SETVERSION, &g_nid) != FALSE);
        g_trayAdded = true;
    }
}

static void TrayRemove()
{
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
    g_trayAdded = false;
}

static void TrayTip(LPCWSTR text)
{
    if (!g_trayAdded) return;
    g_nid.uFlags = NIF_TIP;
    lstrcpynW(g_nid.szTip, text, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ---------------------------------------------------------------- pushing to audio

static void PushLayer(int i)
{
    LayerParams& p = g_audio.params.layer[i];
    const LayerCfg& L = g_layer[i];
    p.enabled.store(L.on);
    p.mode.store(L.mode);
    p.freqHz.store((float)L.freq);
    p.bandwidthOct.store((float)L.bw);
    p.gain.store(VolToGain(L.vol));
    p.balance.store((float)(L.bal - 50) / 50.0f);
    p.beatHz.store((L.mode == MODE_SINE && L.beat) ? (float)L.beatHz : 0.0f);
}

static void PushAllLayers() { for (int i = 0; i < kMaxLayers; i++) PushLayer(i); }

static int EnabledCount()
{
    int n = 0;
    for (int i = 0; i < kMaxLayers; i++) if (g_layer[i].on) n++;
    return n;
}

static void UpdateLayerRow(int i)
{
    // The row text is drawn by DrawLayerRow so the name, frequency and volume
    // keep their own columns. Nothing to set here but the checkbox.
    InvalidateRect(hLayerSel[i], nullptr, TRUE);
    SendMessageW(hLayerEn[i], BM_SETCHECK, g_layer[i].on ? BST_CHECKED : BST_UNCHECKED, 0);
}

// Layer row columns, in logical units from the left edge of the row. Name,
// frequency and volume each own a fixed cell so the values line up down the
// list instead of drifting with the text in front of them.
struct RowCol { int x, w; UINT align; };
static const RowCol kRowNum   = {   4, 12, DT_RIGHT };
static const RowCol kRowName  = {  24, 74, DT_LEFT };
static const RowCol kRowFreq  = { 102, 72, DT_RIGHT };
static const RowCol kRowVol   = { 180, 44, DT_RIGHT };
static const RowCol kRowExtra = { 232, 92, DT_LEFT };

static void DrawRowCell(HDC dc, const RECT& row, const RowCol& c, LPCWSTR text)
{
    RECT r = row;
    r.left = row.left + S(c.x);
    r.right = r.left + S(c.w);
    DrawTextW(dc, text, -1, &r, c.align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

// The checkbox is drawn here rather than by the theme, because the theme has no
// way to say "this one is the teal layer".
static void DrawLayerCheck(int i, const NMCUSTOMDRAW* cd)
{
    FillRect(cd->hdc, &cd->rc, GetSysColorBrush(COLOR_BTNFACE));

    const int side = S(16);
    RECT r;
    r.left = cd->rc.left + S(2);
    r.top = cd->rc.top + ((cd->rc.bottom - cd->rc.top) - side) / 2;
    r.right = r.left + side;
    r.bottom = r.top + side;

    const COLORREF accent = LayerColor(i);
    // Button custom draw does not report the check state in uItemState, so ask
    // the control. Reading it from the flags leaves the box always empty.
    const bool checked =
        (SendMessageW(cd->hdr.hwndFrom, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool hot = (cd->uItemState & CDIS_HOT) != 0;

    if (!checked) {
        FillRound(cd->hdc, r, S(4), GetSysColor(COLOR_WINDOW), hot ? accent : FieldBorder());
        return;
    }

    FillRound(cd->hdc, r, S(4), accent, accent);
    HPEN pen = CreatePen(PS_SOLID, S(2), RGB(255, 255, 255));
    HGDIOBJ op = SelectObject(cd->hdc, pen);
    const POINT tick[3] = {
        { r.left + side * 24 / 100, r.top + side * 52 / 100 },
        { r.left + side * 42 / 100, r.top + side * 72 / 100 },
        { r.left + side * 78 / 100, r.top + side * 30 / 100 },
    };
    Polyline(cd->hdc, tick, 3);
    SelectObject(cd->hdc, op);
    DeleteObject(pen);
}

// Trackbars hand over one item at a time; only the thumb is taken, so the
// channel and the background stay native.
static COLORREF Blend(COLORREF a, COLORREF b, int pctA)
{
    const int q = 100 - pctA;
    return RGB((GetRValue(a) * pctA + GetRValue(b) * q) / 100,
               (GetGValue(a) * pctA + GetGValue(b) * q) / 100,
               (GetBValue(a) * pctA + GetBValue(b) * q) / 100);
}

static void FillPoly(HDC dc, const POINT* pts, int n, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    Polygon(dc, pts, n);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void DrawSliderThumb(HDC dc, HWND slider, RECT r, COLORREF accent)
{
    // Trackbar custom draw does not report the disabled state, so ask the
    // control. Reading it from the flags leaves a disabled slider looking live.
    const bool disabled = !IsWindowEnabled(slider);
    const COLORREF c = disabled ? ShadeFace(72) : accent;

    const int w = S(11);
    const int cx = (r.left + r.right) / 2;
    r.left = cx - w / 2;
    r.right = r.left + w;

    // The classic trackbar pointer: square shoulders, a short point at the
    // bottom. GDI has no anti-aliasing, so the shape is laid down twice: once a
    // pixel larger in a half-tone, then at size. The outer pass fills the steps
    // along the diagonals with a mid colour and the edge stops looking sawn.
    // Everything stays inside the rect the control gave us. A pixel outside it
    // is never erased when the thumb moves on, and leaves a trail down the bar.
    const int shoulder = r.top + (r.bottom - r.top) * 74 / 100;
    const POINT halo[5] = {
        { r.left,      r.top },
        { r.right - 1, r.top },
        { r.right - 1, shoulder },
        { cx,          r.bottom - 1 },
        { r.left,      shoulder },
    };
    const POINT body[5] = {
        { r.left + 1,  r.top + 1 },
        { r.right - 2, r.top + 1 },
        { r.right - 2, shoulder },
        { cx,          r.bottom - 3 },
        { r.left + 1,  shoulder },
    };

    FillPoly(dc, halo, ARRAYSIZE(halo), Blend(c, GetSysColor(COLOR_BTNFACE), 55));
    FillPoly(dc, body, ARRAYSIZE(body), c);
}

// The whole control, not just the thumb. Intercepting TBCD_THUMB alone left the
// colour a layer behind: the trackbar sends the item stages only for the parts
// it decides need repainting, so a slider whose value did not change kept the
// previous layer's thumb no matter how hard the window was invalidated. The
// prepaint stage always arrives, so everything is drawn from here instead.
static void PaintSlider(HDC dc, HWND h, const RECT& client, COLORREF accent)
{
    // Nothing erases behind us once the control's own drawing is skipped.
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));

    // The control knows where its parts sit; asking beats recomputing the
    // layout and drifting from where the mouse thinks the thumb is.
    RECT ch = {};
    SendMessageW(h, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
    const int mid = (ch.top + ch.bottom) / 2;
    RECT bar = { ch.left, mid - S(2), ch.right, mid + S(2) };
    FillRound(dc, bar, S(4), ShadeFace(93), ShadeFace(83));

    RECT tr = {};
    SendMessageW(h, TBM_GETTHUMBRECT, 0, (LPARAM)&tr);
    DrawSliderThumb(dc, h, tr, accent);
}

static void DrawSliderAll(const NMCUSTOMDRAW* cd, COLORREF accent)
{
    const HWND h = cd->hdr.hwndFrom;

    // Not cd->rc: at the prepaint stage the trackbar leaves it empty, and an
    // empty rect here means an empty off screen bitmap and a slider that draws
    // nothing at all. The control's own client rect is what we are covering.
    RECT client = {};
    GetClientRect(h, &client);
    const int w = client.right - client.left;
    const int ht = client.bottom - client.top;
    if (w <= 0 || ht <= 0) return;

    // Off screen first, then one blit. Background, channel and thumb laid
    // straight onto the window would be three visible steps, and dragging a
    // thumb repaints fast enough for that to read as flicker.
    HDC mem = CreateCompatibleDC(cd->hdc);
    HBITMAP bmp = mem ? CreateCompatibleBitmap(cd->hdc, w, ht) : nullptr;
    if (!bmp) {
        if (mem) DeleteDC(mem);
        PaintSlider(cd->hdc, h, client, accent);
        return;
    }

    HGDIOBJ old = SelectObject(mem, bmp);
    SetViewportOrgEx(mem, -client.left, -client.top, nullptr);
    PaintSlider(mem, h, client, accent);
    SetViewportOrgEx(mem, 0, 0, nullptr);

    BitBlt(cd->hdc, client.left, client.top, w, ht, mem, 0, 0, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void DrawLayerRow(int i, const NMCUSTOMDRAW* cd)
{
    const LayerCfg& L = g_layer[i];
    const int saved = SaveDC(cd->hdc);

    SelectObject(cd->hdc, g_font);
    SetBkMode(cd->hdc, TRANSPARENT);
    SetTextColor(cd->hdc, GetSysColor(COLOR_BTNTEXT));

    WCHAR buf[64];
    wsprintfW(buf, L"%d", i + 1);
    DrawRowCell(cd->hdc, cd->rc, kRowNum, buf);
    DrawRowCell(cd->hdc, cd->rc, kRowName, ModeShort(L.mode));
    if (UsesFreq(L)) {
        wsprintfW(buf, L"%d Hz", (int)(L.freq + 0.5));
        DrawRowCell(cd->hdc, cd->rc, kRowFreq, buf);
    }
    wsprintfW(buf, L"%d%%", L.vol);
    DrawRowCell(cd->hdc, cd->rc, kRowVol, buf);
    if (L.mode == MODE_SINE && L.beat) {
        swprintf_s(buf, T(S_FMT_LAYER_BEAT), L.beatHz);
        DrawRowCell(cd->hdc, cd->rc, kRowExtra, buf);
    }
    RestoreDC(cd->hdc, saved);
}

static void UpdateAllLayerRows() { for (int i = 0; i < kMaxLayers; i++) UpdateLayerRow(i); }

// A window property rather than GWLP_USERDATA: common controls are free to use
// the user data slot themselves, and a colour that gets overwritten is worse
// than no colour at all.
static const WCHAR kAccentProp[] = L"DeskNoise.Accent";

static COLORREF LayerSliderColor(HWND s)
{
    return (COLORREF)(UINT_PTR)GetPropW(s, kAccentProp);
}

static void SetLayerSliderColor()
{
    // Only the sliders whose colour actually changes are invalidated. This runs
    // on every label update, so repainting unconditionally made all five flash
    // while one of them was being dragged.
    const COLORREF c = LayerColor(g_sel);
    const HWND sliders[] = { hFreqSlider, hBwSlider, hBeatSlider, hVolSlider, hBalSlider };
    for (HWND s : sliders) {
        if (LayerSliderColor(s) == c) continue;
        SetPropW(s, kAccentProp, (HANDLE)(UINT_PTR)c);
        // A trackbar repaints itself only when its value moves, so one that
        // keeps its position still has to be told the colour is stale.
        InvalidateRect(s, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------- status text

static int SelPlayMin()
{
    const int i = (int)SendMessageW(hPlayMin, CB_GETCURSEL, 0, 0);
    return (i > 0 && i < kPlayCount) ? kPlayMin[i] : 0;
}

static int SelRestMin()
{
    if (SelPlayMin() == 0) return 0;   // continuous playback leaves no room for a rest phase
    const int i = (int)SendMessageW(hRestMin, CB_GETCURSEL, 0, 0);
    return (i > 0 && i < kRestCount) ? kRestMin[i] : 0;
}

// Fading has to reach real silence, and it moves in fractions of a percent, so
// this takes the slider position through the volume curve as a real number
// rather than reusing the integer VolToGain.
static void PushMasterGain()
{
    const int pos = PctFromPos((int)SendMessageW(hMaster, TBM_GETPOS, 0, 0));
    const double p = pos * g_fadeScale;
    g_audio.params.masterVol.store(
        p <= 0.0 ? 0.0f : (float)pow(10.0, (-50.0 + p * 0.5) / 20.0));
}

static int SelFadeMin()
{
    // A fade needs a fixed end to run up to, and a rest cycle would bring the
    // volume back up right after, which defeats the point.
    if (SelPlayMin() == 0 || SelRestMin() > 0) return 0;
    const int i = (int)SendMessageW(hFadeMin, CB_GETCURSEL, 0, 0);
    return (i > 0 && i < kFadeCount) ? kFadeMin[i] : 0;
}

static void UpdateTimerEnables()
{
    EnableWindow(hRestMin, SelPlayMin() != 0);
    EnableWindow(hFadeMin, SelPlayMin() != 0 && SelRestMin() == 0);
}

// Master volume does not belong to a layer, and is deliberately not stored in presets.
static void ApplyMaster()
{
    const int pos = PctFromPos((int)SendMessageW(hMaster, TBM_GETPOS, 0, 0));
    PushMasterGain();
    WCHAR buf[80];
    // With a fade armed the dB figure stops describing what will be heard, so
    // the note replaces it and stays up for as long as the option is set.
    if (SelFadeMin() > 0) swprintf_s(buf, T(S_FMT_MASTER_FADE), pos);
    else if (pos <= 0)    lstrcpynW(buf, T(S_MASTER_MUTE), ARRAYSIZE(buf));
    else                  swprintf_s(buf, T(S_FMT_MASTER), pos, VolToDb(pos));
    SetWindowTextW(hLblMaster, buf);
}

static void EnsureTick()
{
    const bool need = g_session || IsWindowVisible(g_hwnd);
    static bool on = false;
    if (need && !on) { SetTimer(g_hwnd, kTickTimer, 500, nullptr); on = true; }
    else if (!need && on) { KillTimer(g_hwnd, kTickTimer); on = false; }
}

// Closes the open play stretch, if any, and folds it into the session total.
static void ClosePlayStretch()
{
    if (!g_playSince) return;
    const ULONGLONG now = GetTickCount64();
    if (now > g_playSince) g_playedMs += now - g_playSince;
    g_playSince = 0;
}

// The clock runs only while something is actually audible. A session with every
// layer unchecked is not playing time.
static void SyncPlayClock()
{
    const bool audible = g_session && g_phase == PHASE_PLAY && EnabledCount() > 0;
    if (audible) { if (!g_playSince) g_playSince = GetTickCount64(); }
    else         ClosePlayStretch();
}

static ULONGLONG PlayedMs()
{
    ULONGLONG ms = g_playedMs;
    if (g_playSince) {
        const ULONGLONG now = GetTickCount64();
        if (now > g_playSince) ms += now - g_playSince;
    }
    return ms;
}

static int SecondsLeft()
{
    if (!g_phaseEnd) return -1;
    const ULONGLONG now = GetTickCount64();
    return (g_phaseEnd > now) ? (int)((g_phaseEnd - now) / 1000) : 0;
}

// Walks the master gain down over the last stretch of the play phase. Runs off
// the same half-second tick as the status line, which is finer than the ear can
// follow, so the drop sounds continuous rather than stepped.
static void UpdateFade()
{
    double scale = 1.0;
    const int fadeSec = SelFadeMin() * 60;
    if (g_session && g_phase == PHASE_PLAY && fadeSec > 0) {
        const int left = SecondsLeft();
        if (left >= 0 && left < fadeSec) scale = (double)left / (double)fadeSec;
    }
    if (scale != g_fadeScale) {
        g_fadeScale = scale;
        PushMasterGain();
    }
}

static void UpdateStatus()
{
    WCHAR buf[320] = {};
    WCHAR seg[128];
    WCHAR when[64] = {};     // countdown, appended to the status message
    const bool resting = (g_session && g_phase == PHASE_REST);
    const int left = SecondsLeft();

    // Every path that changes layers, phase or session ends up here, so this is
    // where the play clock and the fade catch up with the current state.
    SyncPlayClock();
    UpdateFade();

    if (!g_session) {
        lstrcpynW(buf, T(S_STOPPED), 320);
    }
    else if (resting) {
        lstrcpynW(buf, T(S_RESTING), 320);
        if (left >= 0) swprintf_s(when, T(S_FMT_TIME_PLAY), left / 60, left % 60);
    }
    else if (!g_audio.isDeviceOk()) {
        lstrcpynW(buf, T(S_NO_DEVICE), 320);
    }
    else {
        const int n = EnabledCount();
        if (n == 0) lstrcpynW(buf, T(S_NO_LAYER), 320);
        else { swprintf_s(seg, T(S_FMT_PLAYING_N), n); lstrcpynW(buf, seg, 320); }

        if (g_audio.isLimiting())
            lstrcatW(buf, T(S_LIMITING));

        if (left >= 0) {
            if (SelRestMin() > 0) swprintf_s(when, T(S_FMT_TIME_REST), left / 60, left % 60);
            else                  swprintf_s(when, T(S_FMT_TIME_STOP), left / 60, left % 60);
        }
    }

    // Message and countdown sit together on the right; the left cell carries
    // how long this session has been playing.
    if (when[0]) lstrcatW(buf, when);
    SetWindowTextW(hStatus, buf);

    const int mins = (int)(PlayedMs() / 60000ULL);
    if (mins >= 60) swprintf_s(seg, T(S_FMT_PLAYED_HOUR), mins / 60, mins % 60);
    else            swprintf_s(seg, T(S_FMT_PLAYED_MIN), mins);
    SetWindowTextW(hStatusTime, seg);

    SetWindowTextW(hPlay, T(g_session ? S_STOP : S_PLAY));
    // Focus moving to another button hands the default look over to it, and the
    // play button loses the accent border that marks it as the main action.
    // Claim it back, but only when it is actually gone, or the button would
    // repaint on every tick.
    if ((GetWindowLongW(hPlay, GWL_STYLE) & BS_TYPEMASK) != BS_DEFPUSHBUTTON)
        SendMessageW(hPlay, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);

    WCHAR tip[128];
    wsprintfW(tip, L"%s · %s", APP_TITLE,
        !g_session ? T(S_STOPPED) : (resting ? T(S_RESTING) : T(S_PLAYING)));
    TrayTip(tip);
}

// Bandwidth and beat apply to different sources, so they share one row.
static void UpdateMidRow()
{
    const LayerCfg& L = g_layer[g_sel];
    const bool isSine = (L.mode == MODE_SINE);
    WCHAR buf[80];

    // The wobble checkbox carries its own title text, so for a pure tone it
    // replaces the bandwidth label instead of sitting next to it.
    ShowWindow(hChkBeat, isSine ? SW_SHOW : SW_HIDE);
    ShowWindow(hLblMid, isSine ? SW_HIDE : SW_SHOW);
    ShowWindow(hBeatSlider, isSine ? SW_SHOW : SW_HIDE);
    ShowWindow(hBwSlider, isSine ? SW_HIDE : SW_SHOW);

    if (isSine) {
        if (L.beat) swprintf_s(buf, T(S_FMT_BEAT_RATE), L.beatHz);
        else        lstrcpynW(buf, T(S_BEAT_OFF), ARRAYSIZE(buf));
        SetWindowTextW(hChkBeat, buf);
        EnableWindow(hBeatSlider, L.beat);
    }
    else {
        if (L.bw >= kBwFull) lstrcpynW(buf, T(S_BW_FULL), ARRAYSIZE(buf));
        else                 swprintf_s(buf, T(S_FMT_BW), L.bw);
        SetWindowTextW(hLblMid, buf);
        EnableWindow(hBwSlider, TRUE);
        EnableWindow(hLblMid, TRUE);
    }
}

static void UpdateLabels()
{
    // First thing, before any call below can make a slider repaint: hand each
    // one the colour of the layer now being edited. A slider that painted before
    // this ran would show the layer selected a moment ago.
    SetLayerSliderColor();

    WCHAR buf[96];
    const LayerCfg& L = g_layer[g_sel];

    // The box caption names the layer, so the frequency label no longer has to.
    swprintf_s(buf, T(S_FMT_BOX_LAYER), g_sel + 1);
    FitCaption(hLblBoxA, buf, kCaptionX, kBoxATop);
    SetWindowTextW(hLblFreq, T(S_FREQ));

    UpdateMidRow();

    if (L.vol <= 0) lstrcpynW(buf, T(S_VOL_MUTE), ARRAYSIZE(buf));
    else            swprintf_s(buf, T(S_FMT_VOL), L.vol, VolToDb(L.vol));
    SetWindowTextW(hLblVol, buf);

    if (L.bal == 50)     lstrcpynW(buf, T(S_BAL_CENTER), ARRAYSIZE(buf));
    else if (L.bal < 50) swprintf_s(buf, T(S_FMT_BAL_LEFT), 100 - L.bal * 2);
    else                 swprintf_s(buf, T(S_FMT_BAL_RIGHT), (L.bal - 50) * 2);
    SetWindowTextW(hLblBal, buf);

    const BOOL fUsable = UsesFreq(L);
    EnableWindow(hFreqSlider, fUsable);
    EnableWindow(hFreqEdit, fUsable);
    EnableWindow(hLblFreq, fUsable);

}

static void SyncFreqEdit()
{
    WCHAR buf[32];
    wsprintfW(buf, L"%d", (int)(g_layer[g_sel].freq + 0.5));
    const bool prev = g_updating;
    g_updating = true;
    SetWindowTextW(hFreqEdit, buf);
    g_updating = prev;
}

static void MarkDirty()   // values no longer match the preset, so clear the selection
{
    if (SendMessageW(hPresetCombo, CB_GETCURSEL, 0, 0) != 0) {
        const bool prev = g_updating;
        g_updating = true;
        SendMessageW(hPresetCombo, CB_SETCURSEL, 0, 0);
        g_updating = prev;
        EnableWindow(hPresetDel, FALSE);
    }
}

static void CommitLayerEdit()
{
    PushLayer(g_sel);
    UpdateLayerRow(g_sel);
    UpdateLabels();
    MarkDirty();
    UpdateStatus();
}

// Fill the controls from the layer being edited.
static void WriteLayerToControls()
{
    const LayerCfg& L = g_layer[g_sel];
    g_updating = true;
    SendMessageW(hMode, CB_SETCURSEL, L.mode, 0);
    SendMessageW(hFreqSlider, TBM_SETPOS, TRUE, FreqToPos(L.freq));
    SendMessageW(hBwSlider, TBM_SETPOS, TRUE, BwToPos(L.bw));
    SendMessageW(hBeatSlider, TBM_SETPOS, TRUE, BeatToPos(L.beatHz));
    SendMessageW(hChkBeat, BM_SETCHECK, L.beat ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(hVolSlider, TBM_SETPOS, TRUE, PctToPos(L.vol));
    SendMessageW(hBalSlider, TBM_SETPOS, TRUE, PctToPos(L.bal));
    for (int i = 0; i < kMaxLayers; i++)
        SendMessageW(hLayerSel[i], BM_SETCHECK, (i == g_sel) ? BST_CHECKED : BST_UNCHECKED, 0);
    g_updating = false;
    SyncFreqEdit();
    UpdateLabels();
}

static void SelectLayer(int i)
{
    if (i < 0 || i >= kMaxLayers) return;
    g_sel = i;
    // Before the controls take their new values. Setting a trackbar's position
    // repaints it on the spot, and that paint has to already know the colour;
    // UpdateLabels at the end of the fill would have been a paint too late.
    SetLayerSliderColor();
    WriteLayerToControls();
}

static void ApplyFreqFromEdit()
{
    WCHAR buf[32] = {};
    GetWindowTextW(hFreqEdit, buf, 32);
    const int hz = _wtoi(buf);
    LayerCfg& L = g_layer[g_sel];

    if (hz <= 0) { SyncFreqEdit(); return; }
    // Do not snap to an integer when the display already matches the slider value.
    if ((int)(L.freq + 0.5) == hz) { SyncFreqEdit(); return; }

    L.freq = Clamp(hz, kFreqMin, kFreqMax);
    g_updating = true;
    SendMessageW(hFreqSlider, TBM_SETPOS, TRUE, FreqToPos(L.freq));
    g_updating = false;
    CommitLayerEdit();
    SyncFreqEdit();
}

// ---------------------------------------------------------------- presets

static int PresetCount()
{
    int n = IniGetInt(L"presets", L"count", 0);
    if (n < 0) n = 0;
    if (n > kMaxPresets) n = kMaxPresets;
    return n;
}

static void PresetSection(int i, LPWSTR out) { wsprintfW(out, L"preset%d", i); }

static void PresetGetName(int i, LPWSTR out, int cch)
{
    WCHAR sec[24]; PresetSection(i, sec);
    IniGetStr(sec, L"name", out, cch);
}

static int PresetFindByName(LPCWSTR name)
{
    const int n = PresetCount();
    WCHAR cur[64];
    for (int i = 0; i < n; i++) {
        PresetGetName(i, cur, 64);
        if (lstrcmpiW(cur, name) == 0) return i;
    }
    return -1;
}

static void RefreshPresetCombo(int selectIndex)
{
    const bool prev = g_updating;
    g_updating = true;
    SendMessageW(hPresetCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)T(S_PRESET_CUSTOM));
    const int n = PresetCount();
    WCHAR name[64];
    for (int i = 0; i < n; i++) {
        PresetGetName(i, name, 64);
        if (!name[0]) lstrcpynW(name, T(S_PRESET_UNNAMED), 64);
        SendMessageW(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)name);
    }
    if (selectIndex < 0 || selectIndex > n) selectIndex = 0;
    SendMessageW(hPresetCombo, CB_SETCURSEL, selectIndex, 0);
    g_updating = prev;
    EnableWindow(hPresetDel, selectIndex > 0);
}

static void PresetApply(int i)
{
    WCHAR sec[24]; PresetSection(i, sec);
    LayersFromIni(sec, g_layer);
    PushAllLayers();
    UpdateAllLayerRows();
    WriteLayerToControls();
    UpdateStatus();
}

static void PresetStore(int i, LPCWSTR name)
{
    WCHAR sec[24]; PresetSection(i, sec);
    IniSetStr(sec, L"name", name);
    LayersToIni(sec, g_layer);
}

static void PresetDeleteAt(int idx)
{
    const int n = PresetCount();
    if (idx < 0 || idx >= n) return;
    // Shift the later presets down one slot.
    for (int i = idx; i < n - 1; i++) {
        WCHAR from[24], to[24], name[64];
        PresetSection(i + 1, from);
        PresetSection(i, to);
        LayerCfg tmp[kMaxLayers];
        LayersFromIni(from, tmp);
        IniGetStr(from, L"name", name, 64);
        IniSetStr(to, L"name", name);
        LayersToIni(to, tmp);
    }
    WCHAR last[24]; PresetSection(n - 1, last);
    WritePrivateProfileStringW(last, nullptr, nullptr, g_iniPath);   // delete the section
    IniSetInt(L"presets", L"count", n - 1);
}

static INT_PTR CALLBACK PresetNameProc(HWND h, UINT m, WPARAM w, LPARAM)
{
    switch (m) {
    case WM_INITDIALOG:
        // The dialog resource carries the Korean text; retranslate it here.
        SetWindowTextW(h, T(S_DLG_SAVE_PRESET));
        SetDlgItemTextW(h, IDC_PRESET_NAME_LBL, T(S_NAME));
        SetDlgItemTextW(h, IDOK, T(S_SAVE));
        SetDlgItemTextW(h, IDCANCEL, T(S_CANCEL));
        SendDlgItemMessageW(h, IDC_PRESET_NAME, EM_LIMITTEXT, 40, 0);
        SetDlgItemTextW(h, IDC_PRESET_NAME, g_nameBuf);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            GetDlgItemTextW(h, IDC_PRESET_NAME, g_nameBuf, ARRAYSIZE(g_nameBuf));
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(h, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void TrimInPlace(LPWSTR s)
{
    int a = 0;
    while (s[a] == L' ' || s[a] == L'\t') a++;
    if (a) { int k = 0; while (s[a]) s[k++] = s[a++]; s[k] = 0; }
    int e = lstrlenW(s);
    while (e > 0 && (s[e - 1] == L' ' || s[e - 1] == L'\t')) s[--e] = 0;
}

static void OnPresetSave()
{
    const int cur = (int)SendMessageW(hPresetCombo, CB_GETCURSEL, 0, 0);
    if (cur > 0) PresetGetName(cur - 1, g_nameBuf, ARRAYSIZE(g_nameBuf));
    else         g_nameBuf[0] = 0;

    if (DialogBoxW(g_inst, MAKEINTRESOURCEW(IDD_PRESETNAME), g_hwnd, PresetNameProc) != IDOK)
        return;
    TrimInPlace(g_nameBuf);
    if (!g_nameBuf[0]) return;

    int idx = PresetFindByName(g_nameBuf);
    if (idx >= 0) {
        WCHAR q[160];
        swprintf_s(q, T(S_FMT_PRESET_REPLACE), g_nameBuf);
        if (MessageBoxW(g_hwnd, q, APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    else {
        const int n = PresetCount();
        if (n >= kMaxPresets) {
            MessageBoxW(g_hwnd, T(S_PRESET_FULL), APP_TITLE, MB_OK | MB_ICONINFORMATION);
            return;
        }
        idx = n;
        IniSetInt(L"presets", L"count", n + 1);
    }
    PresetStore(idx, g_nameBuf);
    RefreshPresetCombo(idx + 1);
}

static void OnPresetDelete()
{
    const int cur = (int)SendMessageW(hPresetCombo, CB_GETCURSEL, 0, 0);
    if (cur <= 0) return;
    WCHAR name[64], q[160];
    PresetGetName(cur - 1, name, 64);
    swprintf_s(q, T(S_FMT_PRESET_DELETE), name);
    if (MessageBoxW(g_hwnd, q, APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    PresetDeleteAt(cur - 1);
    RefreshPresetCombo(0);
}

// ---------------------------------------------------------------- playback control

static void EnterPhase(int phase)
{
    g_phase = phase;
    const int mins = (phase == PHASE_PLAY) ? SelPlayMin() : SelRestMin();
    g_phaseEnd = mins ? GetTickCount64() + (ULONGLONG)mins * 60000ULL : 0;
    SyncPlayClock();
    g_audio.setPlaying(phase == PHASE_PLAY);
}

// Settings used to reach the disk only on a clean exit, which lost everything
// changed in the session if the app was killed. Written at the two points the
// user has finished setting up: putting the window away, and starting to play.
static void SaveSettings();

static void SetSession(bool on)
{
    g_session = on;
    if (on) {
        SaveSettings();
        // A new session starts the count over. Stopping keeps the total on
        // screen so the last run stays readable.
        g_playedMs = 0;
        g_playSince = 0;
        EnterPhase(PHASE_PLAY);
    }
    else {
        g_phase = PHASE_PLAY;
        g_phaseEnd = 0;
        SyncPlayClock();
        g_audio.setPlaying(false);
    }
    EnsureTick();
    UpdateStatus();
}

static void ToggleSession() { SetSession(!g_session); }

static void ShowMainWindow()
{
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    EnsureTick();
}

static void HideMainWindow()
{
    ShowWindow(g_hwnd, SW_HIDE);
    EnsureTick();
    SaveSettings();
}

// A minimized window counts as visible, so restore it instead of hiding it.
static void ToggleMainWindow()
{
    if (IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) {
        HideMainWindow();
    }
    else {
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        ShowMainWindow();
    }
}

// ---------------------------------------------------------------- control creation

static HWND MkStatic(HWND p, int id, LPCWSTR text, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        S(x), S(y), S(w), S(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

// A trackbar pages towards a click on the bar and leaves the thumb where it was.
// If the pointer happened to land near the thumb it looks as though the thumb
// was grabbed and then slipped. This takes the click to mean "put the thumb
// here and start dragging".
static LRESULT CALLBACK SliderProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    // Never let a trackbar turn its focus rectangle on. It is drawn by inverting
    // pixels, and the custom thumb repaints often enough that the outline ends
    // up doubled or half erased. Tab still lands here, it just does not outline.
    if (m == WM_UPDATEUISTATE) wp = MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS);

    // The custom draw lays the background down itself, so erasing first would
    // only be a flash of grey ahead of every repaint.
    if (m == WM_ERASEBKGND) return 1;

    if (m == WM_LBUTTONDOWN) {
        RECT ch = {};
        SendMessageW(h, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
        const int thumb = (int)SendMessageW(h, TBM_GETTHUMBLENGTH, 0, 0);
        const int lo = (int)SendMessageW(h, TBM_GETRANGEMIN, 0, 0);
        const int hi = (int)SendMessageW(h, TBM_GETRANGEMAX, 0, 0);
        const int span = (ch.right - ch.left) - thumb;
        if (span > 0 && hi > lo) {
            int x = (int)(short)LOWORD(lp) - ch.left - thumb / 2;
            if (x < 0) x = 0;
            if (x > span) x = span;
            const int value = lo + (int)(((LONGLONG)(hi - lo) * x + span / 2) / span);
            SendMessageW(h, TBM_SETPOS, TRUE, value);
            SendMessageW(GetParent(h), WM_HSCROLL,
                MAKEWPARAM(TB_THUMBPOSITION, value), (LPARAM)h);
            // Re-aim the click at the thumb's new home so the control starts a
            // drag rather than another page step.
            RECT tr = {};
            SendMessageW(h, TBM_GETTHUMBRECT, 0, (LPARAM)&tr);
            lp = MAKELPARAM((tr.left + tr.right) / 2, (tr.top + tr.bottom) / 2);
        }
    }
    return DefSubclassProc(h, m, wp, lp);
}

static HWND MkSlider(HWND p, int id, int x, int y, int w, int h, int lo, int hi, int page)
{
    HWND c = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        S(x), S(y), S(w), S(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(c, TBM_SETPAGESIZE, 0, page);
    // One arrow key should be a visible move, not a fraction of the value's
    // own resolution, so scale the line size with the range.
    const int line = (hi - lo) / 100;
    SendMessageW(c, TBM_SETLINESIZE, 0, line > 1 ? line : 1);
    SetWindowSubclass(c, SliderProc, 1, 0);
    return c;
}

static HWND MkButton(HWND p, int id, LPCWSTR text, int x, int y, int w, int h, DWORD extra)
{
    HWND c = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra,
        S(x), S(y), S(w), S(h), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

// Rules are painted by the window rather than made of SS_ETCHEDHORZ statics.
// The etched style draws a two-pixel groove that reads far too heavy next to
// this much light grey, so these are single hairlines in a colour derived from
// the window background.
struct Rule { int x, y, w; };
static Rule g_rules[8];
static int  g_ruleCount = 0;

// Group boxes are drawn the same way, so their weight matches the rules.
struct Box { int x, y, w, h; };
static Box g_boxes[6];
static int g_boxCount = 0;

static void MkRule(HWND, int x, int y, int w)
{
    if (g_ruleCount < ARRAYSIZE(g_rules)) g_rules[g_ruleCount++] = { x, y, w };
}

static void MkBox(int x, int y, int w, int h)
{
    if (g_boxCount < ARRAYSIZE(g_boxes)) g_boxes[g_boxCount++] = { x, y, w, h };
}


// Status bar text sits back from the controls above it. Mixing towards the
// background rather than using COLOR_GRAYTEXT keeps it from reading as disabled.
// Raise the percentage to fade it further.
static COLORREF DimTextColor()
{
    const int kFade = 45;
    const COLORREF t = GetSysColor(COLOR_BTNTEXT);
    const COLORREF f = GetSysColor(COLOR_BTNFACE);
    return RGB((GetRValue(t) * (100 - kFade) + GetRValue(f) * kFade) / 100,
               (GetGValue(t) * (100 - kFade) + GetGValue(f) * kFade) / 100,
               (GetBValue(t) * (100 - kFade) + GetBValue(f) * kFade) / 100);
}

static void PaintFrames(HDC dc)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RuleColor());
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    for (int i = 0; i < g_ruleCount; i++) {
        MoveToEx(dc, S(g_rules[i].x), S(g_rules[i].y), nullptr);
        LineTo(dc, S(g_rules[i].x + g_rules[i].w), S(g_rules[i].y));
    }
    for (int i = 0; i < g_boxCount; i++) {
        const Box& b = g_boxes[i];
        // A field-sized box wants a tighter corner than a section-sized one.
        const int r = S(b.h < 60 ? 4 : 8);
        RoundRect(dc, S(b.x), S(b.y), S(b.x + b.w), S(b.y + b.h), r, r);
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Width of a string in the UI font, given back in the same logical units the
// layout constants use. Lets a row follow its own text instead of a fixed column,
// which matters because a translated label is rarely the same length.
static int TextWidth(HWND p, LPCWSTR text, HFONT font)
{
    HDC dc = GetDC(p);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(p, dc);
    return MulDiv(sz.cx, 96, g_dpi);
}

static int TextHeight(HWND p)
{
    HDC dc = GetDC(p);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(p, dc);
    return MulDiv(tm.tmHeight, 96, g_dpi);
}

// A box caption sits on the border and is sized to its own text, so it punches
// a gap exactly as wide as the words. The static erases its background with the
// window colour, which is what breaks the line behind it.
static void FitCaption(HWND h, LPCWSTR text, int x, int boxTop)
{
    SetWindowTextW(h, text);
    HDC dc = GetDC(h);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(h, dc);

    const int pad = S(5), height = S(18);
    SetWindowPos(h, nullptr, S(x) - pad, S(boxTop) - height / 2,
        sz.cx + pad * 2, height, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(h, nullptr, TRUE);
}

// Text inset for combo boxes. A drop-down list combo has no edit control, so
// there is no text margin to set: the only way to move its text off the frame
// is to draw the text here and let the system keep the frame and the arrow.
static const int kComboPad = 8;

static int ComboItemHeight()
{
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_font);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return tm.tmHeight + S(5);
}

static void DrawComboItem(const DRAWITEMSTRUCT* di)
{
    if ((int)di->itemID < 0) return;              // nothing selected yet

    WCHAR text[128] = {};
    if (SendMessageW(di->hwndItem, CB_GETLBTEXTLEN, di->itemID, 0) < ARRAYSIZE(text))
        SendMessageW(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)text);

    const bool onEdit = (di->itemState & ODS_COMBOBOXEDIT) != 0;
    const bool selected = (di->itemState & ODS_SELECTED) != 0;
    const bool disabled = (di->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;

    COLORREF bg = GetSysColor(disabled ? COLOR_BTNFACE : COLOR_WINDOW);
    COLORREF fg = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT);
    if (selected && !onEdit) {
        bg = GetSysColor(COLOR_HIGHLIGHT);
        fg = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(di->hDC, &di->rcItem, br);
    DeleteObject(br);

    RECT r = di->rcItem;
    r.left += S(kComboPad);
    const int saved = SaveDC(di->hDC);
    SelectObject(di->hDC, g_font);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, fg);
    DrawTextW(di->hDC, text, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RestoreDC(di->hDC, saved);
    // No focus rectangle here: the themed frame already shows focus, and drawing
    // one on top of it reads as an error state.
}

static HWND MkCombo(HWND p, int id, int x, int y, int w, int dropH)
{
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL
        | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        S(x), S(y), S(w), S(dropH), p, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, ComboItemHeight());
    SendMessageW(c, CB_SETITEMHEIGHT, 0, ComboItemHeight());
    return c;
}

static void CreateControls(HWND p)
{
    // Top-level titles sit on the checkbox column, one step left of anything
    // inside a box.
    const int presetLblW = TextWidth(p, T(S_PRESET), g_fontBold);
    const int presetComboX = kGutterX + presetLblW + 12;
    HWND lblPreset = MkStatic(p, 0, T(S_PRESET), kGutterX, 16, presetLblW, 20);
    SendMessageW(lblPreset, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    hPresetCombo = MkCombo(p, IDC_PRESET_COMBO, presetComboX, 12, 258 - presetComboX, 260);
    hPresetSave = MkButton(p, IDC_PRESET_SAVE, T(S_SAVE), 264, 12, 54, 24, 0);
    hPresetDel = MkButton(p, IDC_PRESET_DEL, T(S_DELETE), 324, 12, 54, 24, 0);

    // The two buttons stand beside the preset combo, so they take their height
    // from it rather than a fixed one. A flat button next to a taller box reads
    // as squashed, and the combo grows with the font on its own.
    const int presetH = ComboItemHeight() + S(6);
    SetWindowPos(hPresetSave, nullptr, 0, 0, S(54), presetH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hPresetDel, nullptr, 0, 0, S(54), presetH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    MkRule(p, kGutterX, 48, kRightX - kGutterX);

    HWND lblLayers = MkStatic(p, 0, T(S_LAYERS_HINT), kGutterX, 60, kRightX - kGutterX, 18);
    SendMessageW(lblLayers, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

    // Create every enable checkbox first, then every select button. An auto-radio
    // group only spans controls that are adjacent in z-order.
    for (int i = 0; i < kMaxLayers; i++)
        hLayerEn[i] = MkButton(p, IDC_LAYER_EN + i, L"", kGutterX, 82 + i * 28, 22, 26, BS_AUTOCHECKBOX);
    for (int i = 0; i < kMaxLayers; i++) {
        hLayerSel[i] = MkButton(p, IDC_LAYER_SEL + i, L"", kTextX, 82 + i * 28, kRightX - kTextX, 26,
            BS_AUTORADIOBUTTON | BS_PUSHLIKE | (i == 0 ? WS_GROUP : 0));
    }

    // Two group boxes. Anything inside the first belongs to the selected layer;
    // anything inside the second applies no matter which layer is selected.
    MkBox(kBoxX, kBoxATop, kBoxW, kBoxABottom - kBoxATop);
    MkBox(kBoxX, kBoxBTop, kBoxW, kBoxBBottom - kBoxBTop);

    // editor for the selected layer
    MkStatic(p, 0, T(S_SOURCE), kTextX, 206, 70, 20);
    hMode = MkCombo(p, IDC_MODE, 120, 202, kCtrlX + kCtrlW - 120, 240);
    SetWindowLongPtrW(hMode, GWL_STYLE, GetWindowLongPtrW(hMode, GWL_STYLE) | WS_GROUP);
    for (int i = 0; i < 5; i++) SendMessageW(hMode, CB_ADDSTRING, 0, (LPARAM)ModeName(i));

    // Label at Y, slider at Y + 20, next label 50 lower.
    // The edit box sits right after the title, the way the other rows carry
    // their value next to the title text.
    const int freqLblW = TextWidth(p, T(S_FREQ), g_font);
    const int freqEditX = kTextX + freqLblW + 8;
    hLblFreq = MkStatic(p, IDC_LBL_FREQ, T(S_FREQ), kTextX, 238, freqLblW, 18);
    // Drawn frame instead of WS_EX_CLIENTEDGE: the sunken 3D edge reads as a
    // hole next to the flat combo boxes.
    // A single-line edit draws its text at the top and leaves any spare height
    // below it, so the field is cut to the height of one line. The frame then
    // sits on the pixel next to the edit; any gap would show window grey
    // between the white field and its border.
    const int freqH = TextHeight(p) + 4;
    hFreqEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT,
        S(freqEditX), S(236), S(58), S(freqH), p, (HMENU)IDC_FREQ_EDIT, g_inst, nullptr);
    SendMessageW(hFreqEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(hFreqEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(S(4), S(4)));
    MkBox(freqEditX - 1, 235, 60, freqH + 2);
    MkStatic(p, 0, L"Hz", freqEditX + 58 + 6, 238, 24, 18);
    hFreqSlider = MkSlider(p, IDC_FREQ_SLIDER, kCtrlX, 258, kCtrlW, 26, 0, 1000, 20);

    // Bandwidth and wobble share the same row: one static, one checkbox, and
    // whichever is showing owns the title text.
    hLblMid = MkStatic(p, IDC_LBL_MID, T(S_BANDWIDTH), kTextX, 288, 300, 18);
    // Aligned to the title column, not to the layer checkboxes above: this row
    // is a section title that happens to carry a checkbox.
    hChkBeat = MkButton(p, IDC_CHK_BEAT, T(S_BEAT), kTextX, 284, kRightX - kTextX, 22, BS_AUTOCHECKBOX);
    hBwSlider = MkSlider(p, IDC_BW_SLIDER, kCtrlX, 308, kCtrlW, 26, 0, 1000, 20);
    hBeatSlider = MkSlider(p, IDC_BEAT_SLIDER, kCtrlX, 308, kCtrlW, 26,
        BeatToPos(kBeatMin), BeatToPos(kBeatMax), 5);

    hLblVol = MkStatic(p, IDC_LBL_VOL, T(S_VOLUME), kTextX, 338, 300, 18);
    hVolSlider = MkSlider(p, IDC_VOL_SLIDER, kCtrlX, 358, kCtrlW, 26, 0, kPctSteps, 50);

    hLblBal = MkStatic(p, IDC_LBL_BAL, T(S_BALANCE), kTextX, 388, 300, 18);
    hBalSlider = MkSlider(p, IDC_BAL_SLIDER, kCtrlX, 408, kCtrlW, 26, 0, kPctSteps, 50);

    // Give the layer sliders a colour before their first paint; UpdateLabels
    // keeps them in step from then on.
    SetLayerSliderColor();

    // Everything below here is global rather than per-layer.
    hLblMaster = MkStatic(p, IDC_LBL_MASTER, T(S_MASTER), kTextX, 478, 300, 18);
    hMaster = MkSlider(p, IDC_MASTER_SLIDER, kCtrlX, 498, kCtrlW, 26, 0, kPctSteps, 50);

    // Three label-and-combo pairs share one row. The labels are measured and the
    // leftover width is split between the combos, so a longer translation makes
    // the combos narrower instead of pushing the row off the box.
    const int kLblGap = 5, kPairGap = 8;
    const int rowRight = kCtrlX + kCtrlW;
    const int wPlay = TextWidth(p, T(S_PLAY), g_font);
    const int wRest = TextWidth(p, T(S_REST), g_font);
    const int wFade = TextWidth(p, T(S_FADE), g_font);
    int comboW = (rowRight - kTextX - (wPlay + wRest + wFade)
                  - kLblGap * 3 - kPairGap * 2) / 3;
    if (comboW < 54) comboW = 54;

    int x = kTextX;
    MkStatic(p, 0, T(S_PLAY), x, 540, wPlay, 20);
    hPlayMin = MkCombo(p, IDC_PLAYMIN, x + wPlay + kLblGap, 536, comboW, 260);
    x += wPlay + kLblGap + comboW + kPairGap;

    MkStatic(p, 0, T(S_REST), x, 540, wRest, 20);
    hRestMin = MkCombo(p, IDC_RESTMIN, x + wRest + kLblGap, 536, comboW, 260);
    x += wRest + kLblGap + comboW + kPairGap;

    MkStatic(p, 0, T(S_FADE), x, 540, wFade, 20);
    hFadeMin = MkCombo(p, IDC_FADEMIN, x + wFade + kLblGap, 536, comboW, 260);
    // Slot 0 is the open-ended entry; the rest are built from the minute values.
    WCHAR item[32];
    for (int i = 0; i < kPlayCount; i++) {
        if (i == 0) lstrcpynW(item, T(S_CONTINUOUS), ARRAYSIZE(item));
        else        swprintf_s(item, T(S_FMT_MINUTES), kPlayMin[i]);
        SendMessageW(hPlayMin, CB_ADDSTRING, 0, (LPARAM)item);
    }
    for (int i = 0; i < kRestCount; i++) {
        if (i == 0) lstrcpynW(item, T(S_NONE), ARRAYSIZE(item));
        else        swprintf_s(item, T(S_FMT_MINUTES), kRestMin[i]);
        SendMessageW(hRestMin, CB_ADDSTRING, 0, (LPARAM)item);
    }

    for (int i = 0; i < kFadeCount; i++) {
        if (i == 0) lstrcpynW(item, T(S_NONE), ARRAYSIZE(item));
        else        swprintf_s(item, T(S_FMT_MINUTES), kFadeMin[i]);
        SendMessageW(hFadeMin, CB_ADDSTRING, 0, (LPARAM)item);
    }

    // Play takes the width it deserves; exit is the narrowest so a stray click
    // is least likely to land on it.
    hPlay = MkButton(p, IDC_PLAY, T(S_PLAY), kGutterX, 586, 186, 38, BS_DEFPUSHBUTTON);
    hHide = MkButton(p, IDC_HIDE, T(S_HIDE_TRAY), 220, 586, 84, 38, 0);
    hExit = MkButton(p, IDC_EXIT, T(S_TRAY_EXIT), 314, 586, 64, 38, 0);

    // Status bar. Its rule spans the whole window, unlike the boxes above.
    // Playing time on the left, message and countdown right-aligned.
    MkRule(p, 0, 636, kWinW);
    hStatusTime = MkStatic(p, IDC_STATUS_TIME, L"", kGutterX, 644, 120, 18);
    hStatus = MkStatic(p, IDC_STATUS, T(S_STOPPED), 148, 644, kRightX - 148, 18);
    SetWindowLongPtrW(hStatus, GWL_STYLE,
        GetWindowLongPtrW(hStatus, GWL_STYLE) | SS_RIGHT | SS_ENDELLIPSIS);

    // Captions last so they sit above the box borders in z-order and break the
    // line where the words are. Their size is set by FitCaption.
    hLblBoxA = MkStatic(p, 0, L"", kCaptionX, kBoxATop, 10, 18);
    hLblBoxB = MkStatic(p, 0, L"", kCaptionX, kBoxBTop, 10, 18);
    FitCaption(hLblBoxB, T(S_BOX_COMMON), kCaptionX, kBoxBTop);
}

// ---------------------------------------------------------------- applying settings

static void LoadSettings()
{
    LayersFromIni(L"cur", g_layer);
    PushAllLayers();

    g_updating = true;
    int pi = IniGetInt(L"main", L"playmin", 0);
    if (pi < 0 || pi >= kPlayCount) pi = 0;
    SendMessageW(hPlayMin, CB_SETCURSEL, pi, 0);
    int ri = IniGetInt(L"main", L"restmin", 0);
    if (ri < 0 || ri >= kRestCount) ri = 0;
    SendMessageW(hRestMin, CB_SETCURSEL, ri, 0);
    int fi = IniGetInt(L"main", L"fademin", 0);
    if (fi < 0 || fi >= kFadeCount) fi = 0;
    SendMessageW(hFadeMin, CB_SETCURSEL, fi, 0);
    SendMessageW(hMaster, TBM_SETPOS, TRUE,
        PctToPos((int)Clamp(IniGetInt(L"main", L"master", 100), 0, 100)));
    g_updating = false;

    UpdateTimerEnables();
    ApplyMaster();

    g_sel = IniGetInt(L"main", L"sel", 0);
    if (g_sel < 0 || g_sel >= kMaxLayers) g_sel = 0;

    RefreshPresetCombo(0);
    UpdateAllLayerRows();
    WriteLayerToControls();
}

static void SaveSettings()
{
    LayersToIni(L"cur", g_layer);
    IniSetInt(L"main", L"playmin", (int)SendMessageW(hPlayMin, CB_GETCURSEL, 0, 0));
    IniSetInt(L"main", L"restmin", (int)SendMessageW(hRestMin, CB_GETCURSEL, 0, 0));
    IniSetInt(L"main", L"fademin", (int)SendMessageW(hFadeMin, CB_GETCURSEL, 0, 0));
    IniSetInt(L"main", L"master", PctFromPos((int)SendMessageW(hMaster, TBM_GETPOS, 0, 0)));
    IniSetInt(L"main", L"sel", g_sel);
}

// ---------------------------------------------------------------- window procedure

// ---------------------------------------------------------------- about box

static void CopyToClipboard(HWND owner, LPCWSTR text)
{
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const SIZE_T bytes = ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* p = GlobalLock(mem);
        if (p) {
            memcpy(p, text, bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);   // clipboard owns it now
        }
        else GlobalFree(mem);
    }
    CloseClipboard();
}

static INT_PTR CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM lp)
{
    switch (m) {
    case WM_INITDIALOG: {
        SetWindowTextW(h, T(S_ABOUT));
        SendDlgItemMessageW(h, IDC_AB_ICON, STM_SETICON,
            (WPARAM)LoadAppIcon(S(32), S(32)), 0);
        SetDlgItemTextW(h, IDC_AB_TITLE, APP_TITLE);

        SetDlgItemTextW(h, IDC_AB_VER_L, T(S_AB_VERSION));
        SetDlgItemTextW(h, IDC_AB_VER, APP_VERSION_STRW);
        SetDlgItemTextW(h, IDC_AB_AUTHOR_L, T(S_AB_AUTHOR));
        SetDlgItemTextW(h, IDC_AB_AUTHOR, APP_AUTHOR_MAIL);
        SetDlgItemTextW(h, IDC_AB_DATE_L, T(S_AB_DATE));
        SetDlgItemTextW(h, IDC_AB_DATE, APP_RELEASE_DATE_W);

        WCHAR link[256];
        SetDlgItemTextW(h, IDC_AB_GITHUB_L, T(S_AB_GITHUB));
        swprintf_s(link, L"<a href=\"%s\">%s</a>", APP_REPO_URL, APP_REPO_TEXT);
        SetDlgItemTextW(h, IDC_AB_GITHUB, link);

        SetDlgItemTextW(h, IDC_AB_TOOLS_L, T(S_AB_TOOLS));
        swprintf_s(link, L"<a href=\"%s\">%s</a>", APP_DEV_URL, APP_DEV_TOOLS);
        SetDlgItemTextW(h, IDC_AB_TOOLS, link);

        SetDlgItemTextW(h, IDC_AB_LIC_L, T(S_AB_LICENSE));
        SetDlgItemTextW(h, IDC_AB_LIC, T(S_AB_LICENSE_TEXT));
        SetDlgItemTextW(h, IDC_AB_NOTE_L, T(S_AB_NOTE));
        SetDlgItemTextW(h, IDC_AB_NOTE, T(S_AB_NOTE_TEXT));

        SetDlgItemTextW(h, IDC_AB_COPY, T(S_AB_COPY_MAIL));
        SetDlgItemTextW(h, IDOK, T(S_CLOSE));
        return TRUE;
    }

    case WM_NOTIFY: {
        // Both links carry their target in the markup, so open whatever was hit.
        const NMHDR* nh = (const NMHDR*)lp;
        if (nh->code == NM_CLICK || nh->code == NM_RETURN) {
            const NMLINK* nl = (const NMLINK*)lp;
            if (nl->item.szUrl[0])
                ShellExecuteW(h, L"open", nl->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(w) == IDC_AB_COPY) {
            CopyToClipboard(h, APP_AUTHOR_MAIL);
            SetDlgItemTextW(h, IDC_AB_COPY, T(S_AB_COPIED));
            return TRUE;
        }
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { EndDialog(h, IDOK); return TRUE; }
        break;

    case WM_CLOSE:
        EndDialog(h, IDOK);
        return TRUE;
    }
    return FALSE;
}

// Relaunches this executable and lets this instance go. Used after changes that
// are easier to apply from a clean start than to rewire while running.
static void RestartApp()
{
    WCHAR path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, ARRAYSIZE(path))) return;

    WCHAR args[64] = {};
    if (!IsWindowVisible(g_hwnd)) lstrcatW(args, L"/tray ");
    if (wcsstr(g_cmdLine, L"/lang=ko")) lstrcatW(args, L"/lang=ko");
    else if (wcsstr(g_cmdLine, L"/lang=en")) lstrcatW(args, L"/lang=en");

    SaveSettings();
    TrayRemove();
    // The new process tests the same single-instance mutex. Release it first,
    // or the new instance would just wake this one and quit.
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = nullptr; }
    ShellExecuteW(nullptr, L"open", path, args[0] ? args : nullptr, nullptr, SW_SHOWNORMAL);

    g_realExit = true;
    DestroyWindow(g_hwnd);
}

static void ShowTrayMenu()
{
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TRAY_TOGGLE, T(g_session ? S_STOP : S_PLAY));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, T(S_TRAY_SHOW));
    AppendMenuW(m, MF_STRING, IDM_TRAY_ABOUT, T(S_ABOUT));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_TRAY_RESTART, T(S_TRAY_RESTART));
    AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, T(S_TRAY_EXIT));
    SetMenuDefaultItem(m, IDM_TRAY_TOGGLE, FALSE);

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static UINT s_taskbarCreated = 0;

    switch (msg) {
    case WM_CREATE:
        s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        return 0;

    case WM_HSCROLL: {
        if (g_updating) return 0;
        HWND s = (HWND)lp;
        LayerCfg& L = g_layer[g_sel];
        const int pos = (int)SendMessageW(s, TBM_GETPOS, 0, 0);
        if (s == hFreqSlider)      { L.freq = PosToFreq(pos); CommitLayerEdit(); SyncFreqEdit(); }
        else if (s == hBwSlider)   { L.bw = PosToBw(pos);     CommitLayerEdit(); }
        else if (s == hBeatSlider) { L.beatHz = PosToBeat(pos); CommitLayerEdit(); }
        else if (s == hVolSlider)  { L.vol = PctFromPos(pos); CommitLayerEdit(); }
        else if (s == hBalSlider)  { L.bal = PctFromPos(pos); CommitLayerEdit(); }
        else if (s == hMaster)     { ApplyMaster(); }
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wp);
        const int code = HIWORD(wp);

        if (id >= IDC_LAYER_EN && id < IDC_LAYER_EN + kMaxLayers) {
            const int i = id - IDC_LAYER_EN;
            g_layer[i].on = (SendMessageW(hLayerEn[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
            PushLayer(i);
            UpdateLayerRow(i);
            MarkDirty();
            // Turning a layer on moves the editor to it, since that is what the
            // next action will be about. Turning one off leaves the editor where
            // it was: that is usually a quick A/B while working on another layer.
            if (g_layer[i].on) SelectLayer(i);
            UpdateStatus();
            return 0;
        }
        if (id >= IDC_LAYER_SEL && id < IDC_LAYER_SEL + kMaxLayers) {
            SelectLayer(id - IDC_LAYER_SEL);
            return 0;
        }
        if (id == IDC_PRESET_COMBO && code == CBN_SELCHANGE) {
            if (g_updating) return 0;
            const int cur = (int)SendMessageW(hPresetCombo, CB_GETCURSEL, 0, 0);
            EnableWindow(hPresetDel, cur > 0);
            if (cur > 0) PresetApply(cur - 1);
            return 0;
        }
        if (id == IDC_PRESET_SAVE) { OnPresetSave(); return 0; }
        if (id == IDC_PRESET_DEL) { OnPresetDelete(); return 0; }
        if (id == IDC_MODE && code == CBN_SELCHANGE) {
            if (g_updating) return 0;
            const int m = (int)SendMessageW(hMode, CB_GETCURSEL, 0, 0);
            g_layer[g_sel].mode = (m < 0) ? MODE_SINE : m;
            CommitLayerEdit();
            return 0;
        }
        if (id == IDC_CHK_BEAT) {
            g_layer[g_sel].beat = (SendMessageW(hChkBeat, BM_GETCHECK, 0, 0) == BST_CHECKED);
            CommitLayerEdit();
            return 0;
        }
        if ((id == IDC_PLAYMIN || id == IDC_RESTMIN || id == IDC_FADEMIN)
            && code == CBN_SELCHANGE) {
            UpdateTimerEnables();
            ApplyMaster();   // the master label carries the fade note
            // While a session runs, re-arm the remaining time with the new values.
            // Continuous playback has no rest phase, so fall back to the play phase.
            if (g_session) EnterPhase(SelPlayMin() == 0 ? PHASE_PLAY : g_phase);
            UpdateStatus();
            return 0;
        }
        if (id == IDC_FREQ_EDIT && code == EN_KILLFOCUS) { ApplyFreqFromEdit(); return 0; }
        if (id == IDC_PLAY) { ToggleSession(); return 0; }
        if (id == IDC_HIDE) { HideMainWindow(); return 0; }
        if (id == IDC_EXIT) { g_realExit = true; DestroyWindow(hwnd); return 0; }
        if (id == IDM_TRAY_TOGGLE) { ToggleSession(); return 0; }
        if (id == IDM_TRAY_SHOW) { ShowMainWindow(); return 0; }
        if (id == IDM_TRAY_ABOUT) {
            DialogBoxW(g_inst, MAKEINTRESOURCEW(IDD_ABOUT), g_hwnd, AboutProc);
            return 0;
        }
        if (id == IDM_TRAY_RESTART) { RestartApp(); return 0; }
        if (id == IDM_TRAY_EXIT) { g_realExit = true; DestroyWindow(hwnd); return 0; }
        return 0;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
        if (mi->CtlType == ODT_COMBOBOX) { mi->itemHeight = ComboItemHeight(); return TRUE; }
        break;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_COMBOBOX) { DrawComboItem(di); return TRUE; }
        break;
    }

    case WM_NOTIFY: {
        // The layer rows keep the system's button background and selection, and
        // only their text is drawn here, in fixed columns.
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->code != NM_CUSTOMDRAW) break;
        NMCUSTOMDRAW* cd = (NMCUSTOMDRAW*)lp;

        if (nh->idFrom >= IDC_LAYER_SEL && nh->idFrom < IDC_LAYER_SEL + kMaxLayers) {
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYPOSTPAINT;
            if (cd->dwDrawStage == CDDS_POSTPAINT) {
                DrawLayerRow((int)(nh->idFrom - IDC_LAYER_SEL), cd);
                return CDRF_DODEFAULT;
            }
            break;
        }
        if (nh->idFrom >= IDC_LAYER_EN && nh->idFrom < IDC_LAYER_EN + kMaxLayers) {
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                DrawLayerCheck((int)(nh->idFrom - IDC_LAYER_EN), cd);
                return CDRF_SKIPDEFAULT;
            }
            break;
        }
        // Sliders belonging to the selected layer take its colour; the master
        // volume is not a layer, so it stays neutral.
        {
            const HWND s = nh->hwndFrom;
            const bool layerSlider = (s == hFreqSlider || s == hBwSlider || s == hBeatSlider
                                      || s == hVolSlider || s == hBalSlider);
            if (layerSlider || s == hMaster) {
                if (cd->dwDrawStage == CDDS_PREPAINT) {
                    // A layer slider was told its colour when the layer was
                    // selected. The master volume is not a layer, so it borrows
                    // the system accent, the blue the title bar already uses.
                    DrawSliderAll(cd, layerSlider ? LayerSliderColor(s)
                                                  : GetSysColor(COLOR_HIGHLIGHT));
                    return CDRF_SKIPDEFAULT;
                }
                return CDRF_DODEFAULT;
            }
        }
        break;
    }

    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE) ToggleSession();
        return 0;

    case WM_TRAY: {
        // Under NOTIFYICON_VERSION_4 one click delivers both the plain mouse
        // message and the NIN_* notification. Acting on both toggles the window
        // twice, so only the pair member that matches the active version counts.
        const UINT ev = LOWORD(lp);
        if (ev == (g_trayV4 ? (UINT)NIN_SELECT : (UINT)WM_LBUTTONUP)) {
            ToggleMainWindow();
            return 0;
        }
        if (ev == (g_trayV4 ? (UINT)WM_CONTEXTMENU : (UINT)WM_RBUTTONUP)) {
            ShowTrayMenu();
            return 0;
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == kTickTimer) {
            if (g_session && g_phaseEnd && GetTickCount64() >= g_phaseEnd) {
                if (g_phase == PHASE_PLAY) {
                    if (SelRestMin() > 0) EnterPhase(PHASE_REST);
                    else { SetSession(false); return 0; }
                }
                else {
                    EnterPhase(PHASE_PLAY);
                }
            }
            UpdateStatus();
        }
        return 0;

    case WM_CLOSE:
        if (!g_realExit) { HideMainWindow(); return 0; }
        break;

    case WM_QUERYENDSESSION:
        SaveSettings();
        return TRUE;

    case WM_DESTROY:
        SaveSettings();
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        KillTimer(hwnd, kTickTimer);
        TrayRemove();
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PaintFrames(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        const HWND c = (HWND)lp;
        if (c == hStatus || c == hStatusTime || c == hLblBoxA || c == hLblBoxB)
            SetTextColor(dc, DimTextColor());
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    }

    if (s_taskbarCreated && msg == s_taskbarCreated) {
        // Re-register the tray icon after Explorer restarts.
        g_trayAdded = false;
        TrayAdd();
        UpdateStatus();
        return 0;
    }
    if (msg == WM_SHOWME) { ShowMainWindow(); return 0; }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- entry point

// Window-level shortcuts, taken before IsDialogMessage so they work wherever
// focus happens to sit. The exceptions matter more than the keys themselves:
// a control that already owns the key keeps it.
// Windows hides focus rectangles while the mouse is in charge and shows them
// once the keyboard is used. Play/stop and volume are not navigation, so they
// should not turn the dotted outlines on; without this they appear and, because
// they are drawn by inverting pixels, double up wherever something repaints.
static void KeepFocusRectsHidden()
{
    SendMessageW(g_hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

static bool HandleAppKey(const MSG& msg)
{
    if (msg.message != WM_KEYDOWN) return false;
    const HWND f = GetFocus();
    if (f == hFreqEdit) return false;          // the edit is taking a number

    WCHAR cls[16] = {};
    GetClassNameW(f, cls, ARRAYSIZE(cls));
    const bool onCombo = (lstrcmpiW(cls, L"ComboBox") == 0);

    if (msg.wParam == VK_SPACE) {
        // Space still checks a layer on and off, and still picks from an open list.
        for (int i = 0; i < kMaxLayers; i++) if (f == hLayerEn[i]) return false;
        if (onCombo && SendMessageW(f, CB_GETDROPPEDSTATE, 0, 0)) return false;
        ToggleSession();
        KeepFocusRectsHidden();
        return true;
    }

    const bool up = (msg.wParam == VK_OEM_PLUS || msg.wParam == VK_ADD);
    const bool down = (msg.wParam == VK_OEM_MINUS || msg.wParam == VK_SUBTRACT);
    if (up || down) {
        if (onCombo && SendMessageW(f, CB_GETDROPPEDSTATE, 0, 0)) return false;
        const int now = PctFromPos((int)SendMessageW(hMaster, TBM_GETPOS, 0, 0));
        const int next = (int)Clamp(now + (up ? 5 : -5), 0, 100);
        SendMessageW(hMaster, TBM_SETPOS, TRUE, PctToPos(next));
        ApplyMaster();
        KeepFocusRectsHidden();
        return true;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int)
{
    g_inst = inst;
    I18nInit(cmdLine);

    if (cmdLine) lstrcpynW(g_cmdLine, cmdLine, ARRAYSIZE(g_cmdLine));

    HANDLE mtx = g_mutex = CreateMutexW(nullptr, FALSE, APP_MUTEX);
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(APP_CLASS, nullptr);
        if (prev) PostMessageW(prev, WM_SHOWME, 0, 0);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = { sizeof(ic),
        ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&ic);
    InitIniPath();
    ClearStartupEntry();
    DefaultLayers(g_layer);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_CLASS;
    wc.hIcon = LoadAppIcon(0, 0);
    wc.hIconSm = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    RegisterClassExW(&wc);

    QueryDpi(nullptr);
    MakeFont();

    RECT rc = { 0, 0, S(kWinW), S(kWinH) };
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    g_hwnd = CreateWindowExW(0, APP_CLASS, APP_CAPTION,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    QueryDpi(g_hwnd);
    MakeFont();
    CreateControls(g_hwnd);

    KeepFocusRectsHidden();
    g_audio.start();
    LoadSettings();
    TrayAdd();

    RegisterHotKey(g_hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M');

    const bool startHidden = (cmdLine && wcsstr(cmdLine, L"/tray") != nullptr);
    if (!startHidden) ShowMainWindow();

    UpdateStatus();
    EnsureTick();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // IsDialogMessage steals Enter for the default button, so handle the edit first.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetFocus() == hFreqEdit) {
            ApplyFreqFromEdit();
            continue;
        }
        if (HandleAppKey(msg)) continue;
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    g_audio.stop();
    if (g_font) DeleteObject(g_font);
    CoUninitialize();
    if (mtx) CloseHandle(mtx);
    return 0;
}

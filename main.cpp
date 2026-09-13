#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <gdiplus.h>
#include <mmsystem.h>
#include "res.h"

using namespace Gdiplus;

// ---------------- 亚克力(WCA) ----------------
#pragma pack(push,1)
struct ACCENTPOLICY { int nAccentState; int nFlags; unsigned int nColor; int nAnimationId; };
struct WINCOMPATTRDATA { int nAttribute; void* pData; unsigned long ulDataSize; };
#pragma pack(pop)

static int (WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*) = nullptr;

static void LoadAcrylicApi()
{
    static bool tried = false;
    if (tried) return;
    tried = true;
    HMODULE h = GetModuleHandleW(L"user32.dll");
    if (!h) h = LoadLibraryW(L"user32.dll");
    if (h) pSetWindowCompositionAttribute = (int(WINAPI*)(HWND,WINCOMPATTRDATA*))GetProcAddress(h, "SetWindowCompositionAttribute");
}

// 给无边框窗口开启真亚克力背景模糊。颜色为 AABBGGRR。
static void EnableAcrylic(HWND hwnd, unsigned int color)
{
    LoadAcrylicApi();
    if (!pSetWindowCompositionAttribute) return;
    ACCENTPOLICY ap{};
    ap.nAccentState = 4;
    ap.nFlags = 2;
    ap.nColor = color;
    ap.nAnimationId = 0;
    WINCOMPATTRDATA wc{};
    wc.nAttribute = 19;
    wc.pData = &ap;
    wc.ulDataSize = sizeof(ap);
    pSetWindowCompositionAttribute(hwnd, &wc);
}

// ---------------- 深色菜单(uxtheme 未公开接口) ----------------
// 菜单跟随系统深浅色:AllowDark 模式 —— 系统深色就画深色菜单,系统浅色就画浅色菜单
typedef int  (WINAPI* PFN_SetPreferredAppMode)(int);
typedef BOOL (WINAPI* PFN_AllowDarkModeForWindow)(HWND, BOOL);
typedef void (WINAPI* PFN_FlushMenuThemes)();

static PFN_SetPreferredAppMode    pSetPreferredAppMode    = nullptr;
static PFN_AllowDarkModeForWindow pAllowDarkModeForWindow = nullptr;
static PFN_FlushMenuThemes        pFlushMenuThemes        = nullptr;
static bool g_darkApiTried = false;

// 这三个接口在 uxtheme 里只按序号导出,只能按序号取;取不到就静默跳过(菜单保持系统默认)
static void LoadDarkMenuApi()
{
    if (g_darkApiTried) return;
    g_darkApiTried = true;
    HMODULE h = LoadLibraryW(L"uxtheme.dll");
    if (!h) return;
    pSetPreferredAppMode    = (PFN_SetPreferredAppMode)   GetProcAddress(h, MAKEINTRESOURCEA(135));
    pAllowDarkModeForWindow = (PFN_AllowDarkModeForWindow)GetProcAddress(h, MAKEINTRESOURCEA(133));
    pFlushMenuThemes        = (PFN_FlushMenuThemes)       GetProcAddress(h, MAKEINTRESOURCEA(136));
}

static void ApplyMenuTheme(HWND hwnd)
{
    LoadDarkMenuApi();
    if (pSetPreferredAppMode) pSetPreferredAppMode(1);            // 1 = AllowDark(跟随系统)
    if (hwnd && pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, TRUE);
    if (pFlushMenuThemes) pFlushMenuThemes();                     // 主题切换后立即生效,不用重启
}

// ---------------- 统计 ----------------
static unsigned long long ftToU64(FILETIME f){ return ((unsigned long long)f.dwHighDateTime << 32) | (unsigned long long)f.dwLowDateTime; }

struct NetCounters { unsigned long long rx=0, tx=0; };

static bool IsVirtualNic(const wchar_t* d)
{
    static const wchar_t* keys[] = {
        L"virtual", L"hyper-v", L"vpn", L"tap", L"wan miniport", L"bluetooth", L"vmware",
        L"virtualbox", L"vethernet", L"wsl", L"tunnel", L"wwan", L"hyperv", L"loopback",
        L"teredo", L"isatap", L"pseudo", L"docker", L"ras ", L"npcap", L"wintun",
        L"虚拟", L"隧道", L"回环"
    };
    if (!d) return false;
    std::wstring s(d);
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    for (const wchar_t* k : keys) if (s.find(k) != std::wstring::npos) return true;
    return false;
}

static NetCounters SumNet()
{
    NetCounters c{};
    PMIB_IF_TABLE2 tbl = nullptr;
    if (GetIfTable2(&tbl) != NO_ERROR || !tbl) return c;
    for (ULONG i = 0; i < tbl->NumEntries; ++i)
    {
        MIB_IF_ROW2& row = tbl->Table[i];
        if (row.InterfaceAndOperStatusFlags.FilterInterface) continue; // 过滤驱动接口与真实网卡重复计数
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;   // 回环不计入真实网速
        if (row.Type == IF_TYPE_TUNNEL) continue;              // 隧道类虚拟接口
        if (row.OperStatus != IfOperStatusUp) continue;        // 只统计已连接网卡
        if (IsVirtualNic(row.Description)) continue;           // 虚拟网卡
        c.rx += row.InOctets;                                  // 64 位计数器,无回绕问题
        c.tx += row.OutOctets;
    }
    FreeMibTable(tbl);
    return c;
}

class StatsSampler
{
public:
    void Init()
    {
        QueryPerformanceFrequency(&_freq);
        _last = SumNet();
        _lastTick = QpcNow();
        if (GetSystemTimes(&_idle, &_kern, &_user))
        {
            _lastIdle = ftToU64(_idle);
            _lastKern = ftToU64(_kern);
            _lastUser = ftToU64(_user);
        }
    }

    void Sample(double& downBps, double& upBps, unsigned int& memLoad, unsigned long long& memTotal, unsigned long long& memAvail, double& cpuPct)
    {
        auto now = SumNet();
        long long nowTick = QpcNow();
        double dt = (nowTick - _lastTick) / 1000000000.0;
        if (dt <= 0) dt = 1.0;

        // 64 位计数器不会回绕;网卡增删导致累计值变小时忽略本次,避免出现虚假峰值
        double drx = (now.rx >= _last.rx) ? (double)(now.rx - _last.rx) : 0.0;
        double dtx = (now.tx >= _last.tx) ? (double)(now.tx - _last.tx) : 0.0;
        downBps = drx / dt;
        upBps   = dtx / dt;

        _last.rx = now.rx; _last.tx = now.tx; _lastTick = nowTick;

        MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
        memLoad = 0; memTotal = 0; memAvail = 0;
        if (GlobalMemoryStatusEx(&ms)) { memLoad = ms.dwMemoryLoad; memTotal = ms.ullTotalPhys; memAvail = ms.ullAvailPhys; }

        cpuPct = SampleCpu();
    }

private:
    long long QpcNow()
    {
        LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
        return (long long)((double)c.QuadPart * 1000000000.0 / (double)_freq.QuadPart);
    }

    double SampleCpu()
    {
        if (!GetSystemTimes(&_idle, &_kern, &_user)) return 0;
        unsigned long long idle = ftToU64(_idle), kern = ftToU64(_kern), user = ftToU64(_user);
        unsigned long long idleD = idle - _lastIdle;
        unsigned long long kernD = kern - _lastKern;
        unsigned long long userD = user - _lastUser;
        _lastIdle = idle; _lastKern = kern; _lastUser = user;
        unsigned long long total = kernD + userD;
        if (total == 0) return 0;
        long long busy = (long long)total - (long long)idleD;
        if (busy < 0) busy = 0;
        return std::min(100.0, std::max(0.0, busy * 100.0 / (double)total));
    }

    LARGE_INTEGER _freq{};
    long long _lastTick = 0;
    unsigned long long _lastIdle=0,_lastKern=0,_lastUser=0;
    FILETIME _idle{}, _kern{}, _user{};
    NetCounters _last{};
};

// ---------------- 格式化 ----------------
static void FormatSpeed(double bps, std::wstring& value, std::wstring& unit)
{
    wchar_t vb[40];
    if (bps < 1024.0*1024.0) { swprintf(vb,40,L"%.1f", bps/1024.0); unit = L"K/s"; }
    else if (bps < 1024.0*1024.0*1024.0) { swprintf(vb,40,L"%.1f", bps/(1024.0*1024.0)); unit = L"M/s"; }
    else { swprintf(vb,40,L"%.2f", bps/(1024.0*1024.0*1024.0)); unit = L"G/s"; }
    value = vb;
}

static std::wstring FormatPct(unsigned int pct){ wchar_t b[16]; swprintf(b,16,L"%u%%",pct); return b; }

// ---------------- 全局状态 ----------------
enum class Mode { Float, DockLeft, DockRight, DockTop, DockBottom };

static HWND g_hwnd = nullptr;
static StatsSampler g_stats;
static Mode g_mode = Mode::Float;
static unsigned int g_memLoad = 0;
static unsigned long long g_memTotal = 0, g_memAvail = 0;
static double g_cpu = 0;
static double g_downBps = 0, g_upBps = 0;
static ULONG_PTR g_gdiplusToken = 0;
static float g_dpi = 96.0f;

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"Perch";
static const wchar_t* kStateKey = L"Software\\Perch";
static const wchar_t* kStateEdge = L"DockEdge";
static const wchar_t* kStateX = L"DockX";
static const wchar_t* kStateY = L"DockY";

static const int CW_FLOAT = 40, CW_DOCK = 36, CH = 192;
static const int RING = 26, RING_TH = 5, DOT = 9;
static const int CAP_W = 11, CAP_H = 30;

static int Px(float v){ return (int)std::lround(v); }

static BYTE AccentR(){ return 0x4C; } static BYTE AccentG(){ return 0xC9; } static BYTE AccentB(){ return 0xF0; }
static BYTE AmberR(){ return 0xFF; } static BYTE AmberG(){ return 0xB8; } static BYTE AmberB(){ return 0x4C; }
static BYTE RedR(){ return 0xFF; } static BYTE RedG(){ return 0x5C; } static BYTE RedB(){ return 0x6C; }
static BYTE CyanR(){ return 0x4C; } static BYTE CyanG(){ return 0xC9; } static BYTE CyanB(){ return 0xF0; }

// 颜色锚点:内存到这两个百分比时,颜色分别正好是原来的"橙"和"红"
static const double kMemWarn = 70.0;
static const double kMemCrit = 90.0;
// 长条/圆环渐变的跨度:底部取"低这么多百分点"时的颜色
static const double kMemGradSpan = 25.0;

static void RgbToHsv(float r, float g, float b, float& h, float& s, float& v)
{
    float mx = std::max(r, std::max(g, b));
    float mn = std::min(r, std::min(g, b));
    float d = mx - mn;
    v = mx;
    s = (mx <= 0.0f) ? 0.0f : d / mx;
    if (d <= 0.0f) { h = 0.0f; return; }
    if (mx == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else              h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    if (s <= 0.0f) { r = g = b = v; return; }
    float hh = h * 6.0f;
    int i = (int)std::floor(hh);
    float f = hh - i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i % 6)
    {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

// 沿色相插值(不走 RGB 直线,否则中间会发灰、出橄榄色)
static Color LerpHsv(const Color& c1, const Color& c2, float t)
{
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    float h1, s1, v1, h2, s2, v2;
    RgbToHsv(c1.GetR()/255.0f, c1.GetG()/255.0f, c1.GetB()/255.0f, h1, s1, v1);
    RgbToHsv(c2.GetR()/255.0f, c2.GetG()/255.0f, c2.GetB()/255.0f, h2, s2, v2);
    float dh = h2 - h1;
    if (dh > 0.5f) dh -= 1.0f; else if (dh < -0.5f) dh += 1.0f;
    float h = h1 + dh * t;
    if (h < 0.0f) h += 1.0f; else if (h >= 1.0f) h -= 1.0f;
    float r, g, b;
    HsvToRgb(h, s1 + (s2 - s1) * t, v1 + (v2 - v1) * t, r, g, b);
    return Color(255, (BYTE)std::lround(r*255.0f), (BYTE)std::lround(g*255.0f), (BYTE)std::lround(b*255.0f));
}

// 内存占用 -> 状态色:青蓝 --(平滑)--> 橙(70%) --(平滑)--> 红(90%)
static Color MemColor(double mem)
{
    Color g(255, AccentR(), AccentG(), AccentB());
    Color a(255, AmberR(), AmberG(), AmberB());
    Color r(255, RedR(), RedG(), RedB());
    if (mem <= 0.0) return g;
    if (mem >= kMemCrit) return r;
    if (mem < kMemWarn) return LerpHsv(g, a, (float)(mem / kMemWarn));
    return LerpHsv(a, r, (float)((mem - kMemWarn) / (kMemCrit - kMemWarn)));
}

// 贴靠边为直角,其它三边圆角
static void ApplyRegion()
{
    RECT rc{}; GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    float rr = 12.0f * (g_dpi / 96.0f);
    int r = Px(rr);
    HRGN rg = CreateRoundRectRgn(0, 0, w+1, h+1, r*2, r*2);

    HRGN add = CreateRectRgn(0,0,0,0);
    HRGN a=nullptr, b=nullptr;
    switch (g_mode)
    {
        case Mode::DockRight:
            a = CreateRectRgn(w-r, 0, w+1, r+1); b = CreateRectRgn(w-r, h-r, w+1, h+1);
            CombineRgn(add,a,b,RGN_OR); DeleteObject(a); DeleteObject(b);
            CombineRgn(rg,rg,add,RGN_OR); break;
        case Mode::DockLeft:
            a = CreateRectRgn(0, 0, r+1, r+1); b = CreateRectRgn(0, h-r, r+1, h+1);
            CombineRgn(add,a,b,RGN_OR); DeleteObject(a); DeleteObject(b);
            CombineRgn(rg,rg,add,RGN_OR); break;
        case Mode::DockTop:
            a = CreateRectRgn(0, 0, r+1, r+1); b = CreateRectRgn(w-r, 0, w+1, r+1);
            CombineRgn(add,a,b,RGN_OR); DeleteObject(a); DeleteObject(b);
            CombineRgn(rg,rg,add,RGN_OR); break;
        case Mode::DockBottom:
            a = CreateRectRgn(0, h-r, r+1, h+1); b = CreateRectRgn(w-r, h-r, w+1, h+1);
            CombineRgn(add,a,b,RGN_OR); DeleteObject(a); DeleteObject(b);
            CombineRgn(rg,rg,add,RGN_OR); break;
        default: break;
    }
    DeleteObject(add);
    SetWindowRgn(g_hwnd, rg, TRUE);
}

static void ApplyModeSize()
{
    float sc = g_dpi/96.0f;
    int w = Px((g_mode==Mode::Float ? CW_FLOAT : CW_DOCK)*sc);
    int h = Px(CH*sc);
    SetWindowPos(g_hwnd, nullptr, 0,0, w, h, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}

// 数值串在给定宽度内必须完整显示(否则小数位会被裁掉):按需自动缩小字号
static float FitValueFont(Graphics& g, const wchar_t* a, const wchar_t* b, float availW, float basePx)
{
    Font probe(L"Segoe UI", basePx, FontStyleBold, UnitPixel, nullptr);
    RectF box;
    float wMax = 0.0f;
    g.MeasureString(a, -1, &probe, PointF(0,0), &box); wMax = std::max(wMax, box.Width);
    g.MeasureString(b, -1, &probe, PointF(0,0), &box); wMax = std::max(wMax, box.Width);
    if (wMax <= availW || wMax <= 0.0f) return basePx;
    float px = basePx * (availW / wMax) * 0.98f;   // 0.98 留出安全余量
    if (px < 6.0f) px = 6.0f;
    return px;
}

// 画一行 "↑ 值 单位"(单行,紧凑)
static void DrawSpeedRow(Graphics& g, float cx, float y, float rowW, const std::wstring& value, const std::wstring& unit, bool up, BYTE arrR, BYTE arrG, BYTE arrB, float valuePx)
{
    float sc = g_dpi/96.0f;
    Font fValue(L"Segoe UI", valuePx, FontStyleBold, UnitPixel, nullptr);
    Font fUnit(L"Segoe UI", 11.0f*sc, FontStyleBold, UnitPixel, nullptr);
    SolidBrush aBrush(Color(255,arrR,arrG,arrB));
    SolidBrush vBrush(Color(255,0xFD,0xFD,0xFD));
    SolidBrush uBrush(Color(255,0xB4,0xC2,0xCE));
    float lineH = Px(16.0f*sc);
    float gap = Px(3.0f*sc);
    // 第一行: 数值,占满,居中
    StringFormat center; center.SetAlignment(StringAlignmentCenter); center.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(value.c_str(), -1, &fValue, RectF(cx - rowW/2.0f, y, rowW, lineH), &center, &vBrush);
    // 第二行: 箭头(实心三角) + 单位,一起居中
    float unitY = y + lineH;
    RectF boxU; g.MeasureString(unit.c_str(), -1, &fUnit, PointF(0,0), &boxU);
    float aw = Px(6.0f*sc);
    float total = aw + gap + boxU.Width;
    float x = cx - total/2.0f;
    float triCx = x + aw/2.0f;
    float midY = unitY + Px(6.5f*sc);
    float ah = Px(7.0f*sc);
    PointF pts[3];
    if (up) { pts[0]=PointF(triCx-aw/2.0f, midY+ah/2.0f); pts[1]=PointF(triCx+aw/2.0f, midY+ah/2.0f); pts[2]=PointF(triCx, midY-ah/2.0f); }
    else    { pts[0]=PointF(triCx-aw/2.0f, midY-ah/2.0f); pts[1]=PointF(triCx+aw/2.0f, midY-ah/2.0f); pts[2]=PointF(triCx, midY+ah/2.0f); }
    g.FillPolygon(&aBrush, pts, 3);
    StringFormat left; left.SetAlignment(StringAlignmentNear); left.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(unit.c_str(), -1, &fUnit, RectF(x + aw + gap, unitY, boxU.Width, Px(13*sc)), &left, &uBrush);
}

static void FillRoundedPanel(Graphics& g, float x, float y, float w, float h, float rTL, float rTR, float rBR, float rBL, const Brush& br)
{
    GraphicsPath p;
    float dTL=rTL*2, dTR=rTR*2, dBR=rBR*2, dBL=rBL*2;
    p.StartFigure();
    p.AddLine(x + std::max(rTL,0.1f), y, x + w - std::max(rTR,0.1f), y);
    if (rTR>0.01f) p.AddArc(x+w-dTR, y, dTR, dTR, 270, 90);
    p.AddLine(x+w, y + std::max(rTR,0.1f), x+w, y + h - std::max(rBR,0.1f));
    if (rBR>0.01f) p.AddArc(x+w-dBR, y+h-dBR, dBR, dBR, 0, 90);
    p.AddLine(x+w - std::max(rBR,0.1f), y+h, x + std::max(rBL,0.1f), y+h);
    if (rBL>0.01f) p.AddArc(x, y+h-dBL, dBL, dBL, 90, 90);
    p.AddLine(x, y + h - std::max(rBL,0.1f), x, y + std::max(rTL,0.1f));
    if (rTL>0.01f) p.AddArc(x, y, dTL, dTL, 180, 90);
    p.CloseFigure();
    g.FillPath(&br, &p);
}



static void StrokeRoundedPanel(Graphics& g, float x, float y, float w, float h, float rTL, float rTR, float rBR, float rBL, const Pen& pen)
{
    GraphicsPath p;
    float dTL=rTL*2, dTR=rTR*2, dBR=rBR*2, dBL=rBL*2;
    p.StartFigure();
    p.AddLine(x + std::max(rTL,0.1f), y, x + w - std::max(rTR,0.1f), y);
    if (rTR>0.01f) p.AddArc(x+w-dTR, y, dTR, dTR, 270, 90);
    p.AddLine(x+w, y + std::max(rTR,0.1f), x+w, y + h - std::max(rBR,0.1f));
    if (rBR>0.01f) p.AddArc(x+w-dBR, y+h-dBR, dBR, dBR, 0, 90);
    p.AddLine(x+w - std::max(rBR,0.1f), y+h, x + std::max(rBL,0.1f), y+h);
    if (rBL>0.01f) p.AddArc(x, y+h-dBL, dBL, dBL, 90, 90);
    p.AddLine(x, y + h - std::max(rBL,0.1f), x, y + std::max(rTL,0.1f));
    if (rTL>0.01f) p.AddArc(x, y, dTL, dTL, 180, 90);
    p.CloseFigure();
    g.DrawPath(&pen, &p);
}

static void FillShield(Graphics& g, float cx, float top, float w, float h, const Brush& br)
{
    GraphicsPath p;
    float y = top;
    float r = w*0.18f;
    p.StartFigure();
    p.AddArc(cx - w/2.0f, y, r*2, r*2, 180, 90);
    p.AddArc(cx + w/2.0f - r*2, y, r*2, r*2, 270, 90);
    p.AddLine(cx + w/2.0f, y + r, cx + w/2.0f, y + h*0.60f);
    p.AddBezier(cx + w/2.0f, y + h*0.60f, cx + w*0.28f, y + h*0.82f, cx + w*0.18f, y + h*0.95f, cx, y + h);
    p.AddBezier(cx, y + h, cx - w*0.18f, y + h*0.95f, cx - w*0.28f, y + h*0.82f, cx - w/2.0f, y + h*0.60f);
    p.AddLine(cx - w/2.0f, y + h*0.60f, cx - w/2.0f, y + r);
    p.CloseFigure();
    g.FillPath(&br, &p);
}



static void FillBird(Graphics& g, float cx, float cy, float size, const Brush& br)
{
    float s = size;
    float hw = s/2.0f;
    float hh = s*0.62f;
    GraphicsPath p;
    p.StartFigure();
    p.AddBezier(cx-hw, cy-hh*0.62f, cx-hw*0.60f, cy-hh*0.10f, cx-hw*0.34f, cy+hh*0.02f, cx, cy+hh*0.24f);
    p.AddBezier(cx, cy+hh*0.24f, cx+hw*0.34f, cy+hh*0.02f, cx+hw*0.60f, cy-hh*0.10f, cx+hw, cy-hh*0.62f);
    p.AddBezier(cx+hw, cy-hh*0.62f, cx+hw*0.44f, cy-hh*0.10f, cx+hw*0.18f, cy-hh*0.02f, cx, cy+hh*0.16f);
    p.AddBezier(cx, cy+hh*0.16f, cx-hw*0.18f, cy-hh*0.02f, cx-hw*0.44f, cy-hh*0.10f, cx-hw, cy-hh*0.62f);
    p.CloseFigure();
    g.FillPath(&br, &p);
}

static void DrawContent(Graphics& g, int w, int h)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    float sc = g_dpi/96.0f;
    float cx = w/2.0f;

    // 亚克力面板:圆角(贴靠边直角),半透明深色,透出背景模糊
    float rr = 12.0f*sc;
    float rTL = (g_mode==Mode::DockLeft || g_mode==Mode::DockTop) ? 0 : rr;
    float rTR = (g_mode==Mode::DockRight || g_mode==Mode::DockTop) ? 0 : rr;
    float rBR = (g_mode==Mode::DockRight || g_mode==Mode::DockBottom) ? 0 : rr;
    float rBL = (g_mode==Mode::DockLeft || g_mode==Mode::DockBottom) ? 0 : rr;
    SolidBrush panelBr(Color(0x26, 0x1E, 0x1E, 0x28));
    FillRoundedPanel(g, 0, 0, (float)w, (float)h, rTL, rTR, rBR, rBL, panelBr);

    Color accent = MemColor(g_memLoad);                                    // 当前状态色
    Color accentLow = MemColor(std::max(0.0, g_memLoad - kMemGradSpan));   // 渐变底部(略冷)色
    BYTE ar = accent.GetR(), ag = accent.GetG(), ab = accent.GetB();
    Color text(255,0xFD,0xFD,0xFD);
    Color dim(255,0xB4,0xC2,0xCE);

    // 顶部状态指示:小盾牌
    float sw = Px(11.0f*sc), sh = Px(13.0f*sc);
    float y = Px(6.0f*sc);
    SolidBrush shBr(accent);
    FillShield(g, cx, y, sw, sh, shBr);
    y += sh + Px(10.0f*sc);

    if (g_mode == Mode::Float)
    {
        float rs = RING*sc;
        float ringCx = cx, ringCy = y + rs/2.0f;
        SolidBrush track(Color(0x30,0xFF,0xFF,0xFF));
        g.FillEllipse(&track, ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs);
        float sweep = (float)(g_memLoad/100.0*360.0);
        if (sweep > 0.5f)
        {
            // 沿弧线扫掠渐变:弧尾偏冷,弧头为当前状态色(分段绘制 + 轻微重叠消除缝隙)
            const int SEG = 64;
            for (int i = 0; i < SEG; ++i)
            {
                float t0 = (float)i / SEG, t1 = (float)(i + 1) / SEG;
                float a0 = sweep * t0;
                float dA = sweep * (t1 - t0);
                bool last = (i == SEG - 1);
                if (!last) dA += 0.4f;
                Pen seg(LerpHsv(accentLow, accent, (t0 + t1) * 0.5f), RING_TH*sc);
                if (i == 0) seg.SetStartCap(LineCapRound);
                if (last)   seg.SetEndCap(LineCapRound);
                g.DrawArc(&seg, ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs, -90.0f + a0, dA);
            }
        }
        // 中心百分比
        Font fp(L"Segoe UI", 10.0f*sc, FontStyleBold, UnitPixel, nullptr);
        SolidBrush tb(text);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(FormatPct(g_memLoad).c_str(), -1, &fp, RectF(ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs), &sf, &tb);
        y = ringCy + rs/2.0f + Px(6*sc);
    }
    else
    {
        float capW = CAP_W*sc, capH = CAP_H*sc;
        float cy = y;
        float capX = cx - capW/2.0f;
        SolidBrush capBg(Color(0x30,0xFF,0xFF,0xFF));
        FillRoundedPanel(g, capX, cy, capW, capH, capW/2.0f, capW/2.0f, capW/2.0f, capW/2.0f, capBg);
        float fh = capH * (float)(g_memLoad/100.0);
        if (fh > 0.5f)
        {
            float fr = std::min(capW/2.0f, fh/2.0f);
            // 纵向渐变:底部偏冷(低占用率对应的颜色),顶部是当前状态色
            LinearGradientBrush lg(PointF(capX, cy+capH), PointF(capX, cy+capH-fh), accentLow, accent);
            lg.SetWrapMode(WrapModeClamp);
            FillRoundedPanel(g, capX, cy+capH-fh, capW, fh, fr, fr, fr, fr, lg);
        }

        y = cy + capH + Px(6*sc);
    }

    float rowW = (g_mode==Mode::Float ? (w - Px(8*sc)) : (w - Px(6*sc)));
    // divider
    SolidBrush sep(Color(0x24,0xFF,0xFF,0xFF));
    g.FillRectangle(&sep, (REAL)Px(8*sc), (REAL)y, (REAL)(w - Px(16*sc)), (REAL)(1.0f*sc));
    y += Px(9*sc);

    std::wstring uv, uu; FormatSpeed(g_upBps, uv, uu);
    std::wstring dv, du; FormatSpeed(g_downBps, dv, du);
    float vPx = FitValueFont(g, uv.c_str(), dv.c_str(), rowW, 11.0f*sc);
    DrawSpeedRow(g, cx, y, rowW, uv, uu, true, CyanR(), CyanG(), CyanB(), vPx);
    y += Px(36*sc);
    DrawSpeedRow(g, cx, y, rowW, dv, du, false, ar, ag, ab, vPx);
    y += Px(36*sc);

    g.FillRectangle(&sep, (REAL)Px(8*sc), (REAL)y, (REAL)(w - Px(16*sc)), (REAL)(1.0f*sc));
    y += Px(9*sc);

    // CPU badge
    Font fBadge(L"Segoe UI", 11.0f*sc, FontStyleBold, UnitPixel, nullptr);
    Font fCpu(L"Segoe UI", 11.0f*sc, FontStyleBold, UnitPixel, nullptr);
    StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
    RectF badge(cx - Px(17*sc), y, Px(34*sc), Px(14*sc));
    SolidBrush cpuText(Color(255,CyanR(),CyanG(),CyanB()));
    SolidBrush cpuVal(text);
    g.DrawString(L"CPU", -1, &fBadge, badge, &cf, &cpuText);
    RectF cpuV(cx - rowW/2.0f, badge.Y + badge.Height + Px(2*sc), rowW, Px(16*sc));
    g.DrawString(FormatPct((unsigned int)g_cpu).c_str(), -1, &fCpu, cpuV, &cf, &cpuVal);
}

static void Render()
{
    RECT rc{}; GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w<=0||h<=0) return;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) { if(dib) DeleteObject(dib); ReleaseDC(nullptr, screen); return; }
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP oldb = (HBITMAP)SelectObject(mem, dib);
    {
        Graphics g(mem);
        g.Clear(Color(0,0,0,0));
        DrawContent(g, w, h);
    }
    BYTE* q = (BYTE*)bits;
    for (int i=0;i<w*h;i++){ BYTE a=q[3]; if(a<255){ q[0]=(BYTE)(q[0]*a/255); q[1]=(BYTE)(q[1]*a/255); q[2]=(BYTE)(q[2]*a/255); } q+=4; }
    POINT src{0,0}, dst{0,0}; SIZE sz{w,h};
    BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, oldb); DeleteDC(mem); DeleteObject(dib); ReleaseDC(nullptr, screen);
}
// ---------------- 贴靠/菜单/WndProc ----------------
#define WM_TRAYICON (WM_APP + 1)
#define TRAY_UID 1
static bool g_topmost = false;

static int clamp(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

static void UpdateStats()
{
    double d,u,pct; unsigned int ml; unsigned long long mt,ma;
    g_stats.Sample(d,u,ml,mt,ma,pct);
    g_downBps=d; g_upBps=u; g_memLoad=ml; g_memTotal=mt; g_memAvail=ma; g_cpu=pct;
}

static void ApplyAcrylicToWindow()
{
    // 分层窗口上启用亚克力背景模糊
    MARGINS mg{-1,-1,-1,-1};
    DwmExtendFrameIntoClientArea(g_hwnd, &mg);
    EnableAcrylic(g_hwnd, 0x991E1E28u); // AABBGGRR
}

static int ReadDockEdge()
{
    DWORD v = 2; HKEY k; DWORD sz = sizeof(v); DWORD type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStateKey, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(k, kStateEdge, nullptr, &type, (BYTE*)&v, &sz) != ERROR_SUCCESS) v = 2;
        RegCloseKey(k);
    }
    if (v < 1 || v > 4) v = 2;
    return (int)v;
}

static void SaveDockEdge(int edge)
{
    HKEY k; DWORD disp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kStateKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, &disp) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)edge;
        RegSetValueExW(k, kStateEdge, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}
static bool ReadDockPos(bool& ok, int& x, int& y)
{
    ok = false; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStateKey, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD type = 0, sx = sizeof(x), sy = sizeof(y);
        if (RegQueryValueExW(k, kStateX, nullptr, &type, (BYTE*)&x, &sx) == ERROR_SUCCESS &&
            RegQueryValueExW(k, kStateY, nullptr, &type, (BYTE*)&y, &sy) == ERROR_SUCCESS) ok = true;
        RegCloseKey(k);
    }
    return ok;
}

static void SaveDockPos(int x, int y)
{
    HKEY k; DWORD disp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kStateKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, &disp) == ERROR_SUCCESS)
    {
        DWORD vx = (DWORD)x, vy = (DWORD)y;
        RegSetValueExW(k, kStateX, 0, REG_DWORD, (const BYTE*)&vx, sizeof(vx));
        RegSetValueExW(k, kStateY, 0, REG_DWORD, (const BYTE*)&vy, sizeof(vy));
        RegCloseKey(k);
    }
}

static void RestoreDockState()
{
    int edge = ReadDockEdge();
    g_mode = (Mode)edge;
    ApplyModeSize();
    RECT cr{}; GetClientRect(g_hwnd, &cr); int cw = cr.right, ch = cr.bottom;
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfo(mon, &mi);
    RECT wa = mi.rcWork;
    int x, y; bool have;
    if (ReadDockPos(have, x, y) && have) { }
    else { x = wa.right - cw; y = wa.top + (wa.bottom - wa.top - ch) / 2; }
    SetWindowPos(g_hwnd, nullptr, x, y, cw, ch, SWP_NOZORDER|SWP_NOACTIVATE);
    SaveDockEdge((int)g_mode);
    SaveDockPos(x, y);
    Render();
}
static void DockTo(Mode m)
{
    // 记住当前窗口位置用于夹取
    RECT wr{}; GetWindowRect(g_hwnd, &wr);
    g_mode = m;
    ApplyModeSize();
    RECT cr{}; GetClientRect(g_hwnd, &cr); int cw=cr.right, ch=cr.bottom;
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize=sizeof(mi); GetMonitorInfo(mon,&mi);
    RECT wa = mi.rcWork;
    int x=wr.left, y=wr.top;
    switch(m)
    {
        case Mode::DockLeft:    x=wa.left; y=clamp(wr.top,wa.top,wa.bottom-ch); break;
        case Mode::DockRight:   x=wa.right-cw; y=clamp(wr.top,wa.top,wa.bottom-ch); break;
        case Mode::DockTop:     y=wa.top; x=clamp(wr.left,wa.left,wa.right-cw); break;
        case Mode::DockBottom:  y=wa.bottom-ch; x=clamp(wr.left,wa.left,wa.right-cw); break;
        default:                x=clamp(wr.left,wa.left,wa.right-cw); y=clamp(wr.top,wa.top,wa.bottom-ch); break;
    }

    SetWindowPos(g_hwnd,nullptr,x,y,cw,ch,SWP_NOZORDER|SWP_NOACTIVATE);
    if (m != Mode::Float) { SaveDockEdge((int)m); SaveDockPos(x, y); }
    Render();
}

static void HandleDragEnd()
{
    RECT r{}; GetWindowRect(g_hwnd, &r);
    int w=r.right-r.left, h=r.bottom-r.top;
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize=sizeof(mi); GetMonitorInfo(mon,&mi);
    RECT wa = mi.rcWork;
    int dL=abs(r.left-wa.left), dR=abs(wa.right-r.right), dT=abs(r.top-wa.top), dB=abs(wa.bottom-r.bottom);
    int thr = Px(46*(g_dpi/96.0f));
    int mn = std::min({dL,dR,dT,dB});
    Mode m = Mode::Float;
    if (dL<=thr && dL==mn) m=Mode::DockLeft;
    else if (dR<=thr && dR==mn) m=Mode::DockRight;
    else if (dT<=thr && dT==mn) m=Mode::DockTop;
    else if (dB<=thr && dB==mn) m=Mode::DockBottom;
    // 用 DockTo 统一处理位置与区域
    LRESULT pos = 0; (void)pos; (void)w; (void)h;
    DockTo(m);
}

static void AddTray(HWND hwnd)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_UID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!nid.hIcon) { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); nid.hIcon = ExtractIconW(GetModuleHandleW(nullptr), exe, 0); }
    wcscpy_s(nid.szTip, L"Perch");
    Shell_NotifyIconW(NIM_ADD, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
}

static void ToggleVisible()
{
    if (IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_HIDE);
    else { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); }
}

static void EnsureStartupShortcut(bool create)
{
    wchar_t startDir[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, startDir);
    if (FAILED(hr)) return;
    wchar_t lnk[MAX_PATH + 16];
    int l = (int)wcslen(startDir);
    for (int i = 0; i < l; ++i) lnk[i] = startDir[i];
    lnk[l] = L'\\';
    wcscpy(lnk + l + 1, L"Perch.lnk");

    if (!create) { DeleteFileW(lnk); return; }

    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    IShellLinkW* psl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl))) return;
    psl->SetPath(exe);
    psl->SetDescription(L"Perch");
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&pf))) { pf->Save(lnk, TRUE); pf->Release(); }
    psl->Release();
}
static bool IsAutoStart()
{
    wchar_t buf[MAX_PATH]; DWORD sz = sizeof(buf);
    LONG r = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz);
    return r == ERROR_SUCCESS;
}

static void SetAutoStart(bool on)
{
    EnsureStartupShortcut(on);
    HKEY k; DWORD disp;
    if (on)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, &disp) == ERROR_SUCCESS)
        {
            const int kPathMax = 2048;
            wchar_t exe[kPathMax]; GetModuleFileNameW(nullptr, exe, kPathMax);
            wchar_t val[kPathMax+4];
            int exl = (int)wcslen(exe);
            val[0] = L'"';
            for (int i = 0; i < exl; ++i) val[i+1] = exe[i];
            val[exl+1] = L'"';
            val[exl+2] = 0;
            RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val)+1)*sizeof(wchar_t)));
            RegCloseKey(k);
        }
    }
    else
    {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS)
        {
            RegDeleteValueW(k, kRunValue);
            RegCloseKey(k);
        }
    }
}

static const wchar_t* kAppVersion = L"3.0.0";

static void ShowAbout()
{
    std::wstring text;
    text += L"Perch  v";
    text += kAppVersion;
    text += L"\n\n";
    text += L"Tiny native Windows network-speed monitor widget.\n";
    text += L"Translucent Acrylic UI, Win11 rounded corners, floating & docking.\n";
    text += L"C++17 / Win32 (GDI+, Acrylic), no .NET, no runtime dependency.\n\n";
    text += L"(c) 2026 Adstrax";
    MessageBoxW(g_hwnd, text.c_str(), L"About Perch", MB_OK | MB_ICONINFORMATION);
}

static void HandleMenuCommand(int cmd)
{
    switch (cmd)
    {
        case 10: DockTo(Mode::DockLeft); break;
        case 11: DockTo(Mode::DockRight); break;
        case 12: DockTo(Mode::DockTop); break;
        case 13: DockTo(Mode::DockBottom); break;
        case 20:
            g_topmost = !g_topmost;
            SetWindowPos(g_hwnd, g_topmost?HWND_TOPMOST:HWND_NOTOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            break;
        case 21: SetAutoStart(!IsAutoStart()); break;
        case 22: ShowWindow(g_hwnd, SW_HIDE); break;
        case 23: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
        case 24: ShowAbout(); break;
    }
}

static void ShowMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 10, L"Dock left");
    AppendMenuW(menu, MF_STRING, 11, L"Dock right");
    AppendMenuW(menu, MF_STRING, 12, L"Dock top");
    AppendMenuW(menu, MF_STRING, 13, L"Dock bottom");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_topmost ? MF_CHECKED : 0), 20, L"Always on top");
    AppendMenuW(menu, MF_STRING | (IsAutoStart() ? MF_CHECKED : 0), 21, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 22, L"Hide to tray");
    AppendMenuW(menu, MF_STRING, 23, L"Exit");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 24, L"About Perch");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd) HandleMenuCommand(cmd);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_CREATE:
            g_stats.Init();
            ApplyAcrylicToWindow();
            ApplyMenuTheme(hwnd);
            SetTimer(hwnd, 1, 1000, nullptr);
            return 0;
        case WM_TIMER:
            if (wp == 1) { UpdateStats(); Render(); }
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            Render();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // 让亚克力透出,不擦背景
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_LBUTTONDOWN:
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        case WM_EXITSIZEMOVE:
            HandleDragEnd();
            return 0;
        case WM_DPICHANGED:
            g_dpi = (float)HIWORD(wp);
            {
                RECT* r = (RECT*)lp;
                SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER|SWP_NOACTIVATE);
            }
            ApplyModeSize();
            Render();
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            // 系统切深色/浅色后菜单立即跟上(其它设置变更交给默认处理)
            if (msg != WM_SETTINGCHANGE || (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0)) { ApplyMenuTheme(hwnd); return 0; }
            break;
        case WM_TRAYICON:
        {
            UINT ev = LOWORD(lp);
            if (ev == WM_LBUTTONDBLCLK || ev == WM_LBUTTONUP) ToggleVisible();
            else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) ShowMenu();
            return 0;
        }
        case WM_COMMAND:
            HandleMenuCommand(LOWORD(wp));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            { NOTIFYICONDATAW nid{}; nid.cbSize=sizeof(nid); nid.hWnd=hwnd; nid.uID=TRAY_UID; Shell_NotifyIconW(NIM_DELETE,&nid); }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ApplyMenuTheme(nullptr);   // 尽早声明跟随系统主题
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gsi; GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    HDC sdc = GetDC(nullptr);
    g_dpi = (float)GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"PerchWnd";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"PerchWnd", L"Perch", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_FLOAT, CH, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ApplyAcrylicToWindow();
    ApplyModeSize();

    AddTray(g_hwnd);
    ShowWindow(g_hwnd, SW_SHOW);
    if (IsAutoStart()) EnsureStartupShortcut(true);
    // 开机默认贴靠到上次用的边(默认右侧)
    RestoreDockState();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    return 0;
}

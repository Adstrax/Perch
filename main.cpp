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
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// ---------------- 统计 ----------------
static unsigned long long ftToU64(FILETIME f){ return ((unsigned long long)f.dwHighDateTime << 32) | (unsigned long long)f.dwLowDateTime; }

struct NetCounters { unsigned long long rx=0, tx=0; };

static NetCounters SumNet()
{
    NetCounters c{};
    ULONG size = 0;
    if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER) return c;
    std::vector<BYTE> buf(size);
    PMIB_IFTABLE tbl = (PMIB_IFTABLE)buf.data();
    if (GetIfTable(tbl, &size, FALSE) == NO_ERROR && tbl)
    {
        for (DWORD i = 0; i < tbl->dwNumEntries; ++i)
        {
            MIB_IFROW& row = tbl->table[i];
            if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL) continue;
            c.rx += row.dwInOctets;
            c.tx += row.dwOutOctets;
        }
    }
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

        long long drx = (now.rx >= _last.rx) ? (long long)(now.rx - _last.rx) : 0;
        long long dtx = (now.tx >= _last.tx) ? (long long)(now.tx - _last.tx) : 0;
        downBps = std::max(0.0, (double)drx / dt);
        upBps   = std::max(0.0, (double)dtx / dt);

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
static std::wstring FormatSpeed(double bps)
{
    wchar_t buf[40];
    if (bps < 1024.0*1024.0)
        swprintf(buf, 40, L"%.1f  K/s", bps/1024.0);
    else if (bps < 1024.0*1024.0*1024.0)
        swprintf(buf, 40, L"%.1f  M/s", bps/(1024.0*1024.0));
    else
        swprintf(buf, 40, L"%.2f  G/s", bps/(1024.0*1024.0*1024.0));
    return buf;
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

static const int CW_FLOAT = 104, CW_DOCK = 80, CH = 186;
static const int RING = 29, RING_TH = 4, DOT = 10;
static const int CAP_W = 11, CAP_H = 34;

static int Px(float v){ return (int)std::lround(v); }

static BYTE AccentR(){ return 0x3B; } static BYTE AccentG(){ return 0xD9; } static BYTE AccentB(){ return 0xA3; }
static BYTE AmberR(){ return 0xFF; } static BYTE AmberG(){ return 0xB8; } static BYTE AmberB(){ return 0x4C; }
static BYTE RedR(){ return 0xFF; } static BYTE RedG(){ return 0x5C; } static BYTE RedB(){ return 0x6C; }
static BYTE CyanR(){ return 0x4C; } static BYTE CyanG(){ return 0xC9; } static BYTE CyanB(){ return 0xF0; }

static COLORREF StateColor(double mem, BYTE& r, BYTE& g, BYTE& b)
{
    if (mem < 60)  { r=AccentR(); g=AccentG(); b=AccentB(); }
    else if (mem < 85) { r=AmberR(); g=AmberG(); b=AmberB(); }
    else           { r=RedR(); g=RedG(); b=RedB(); }
    return RGB(r,g,b);
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

// 画一行"值 单位"
static void DrawSpeedRow(Graphics& g, float colCx, float y, float rowW, const std::wstring& value, BYTE arrR, BYTE arrG, BYTE arrB)
{
    Font fValue(L"Segoe UI", 14.0f*(g_dpi/96.0f), FontStyleBold, UnitPixel, nullptr);
    Font fUnit(L"Segoe UI", 11.0f*(g_dpi/96.0f), FontStyleRegular, UnitPixel, nullptr);
    SolidBrush vBrush(Color(255,0xFD,0xFD,0xFD));
    SolidBrush uBrush(Color(255,0xB4,0xC2,0xCE));

    // 第一行: 箭头 + 数值
    Font fArrow(L"Segoe UI Symbol", 11.0f*(g_dpi/96.0f), FontStyleRegular, UnitPixel, nullptr);
    SolidBrush aBrush(Color(255,arrR,arrG,arrB));
    wchar_t arrow[] = { L'\u2191', L' ', L'\0' };
    PointF pos(colCx - rowW/2.0f, y);
    g.DrawString(arrow, -1, &fArrow, pos, &aBrush);
    // value
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter);
    RectF vr(colCx - rowW/2.0f, y + Px(13*(g_dpi/96.0f)), rowW, Px(18*(g_dpi/96.0f)));
    g.DrawString(value.c_str(), -1, &fValue, vr, &sf, &vBrush);
    // unit (下方, 居中)
    RectF ur(colCx - rowW/2.0f, y + Px(30*(g_dpi/96.0f)), rowW, Px(16*(g_dpi/96.0f)));
    g.DrawString(L"K/s", -1, &fUnit, ur, &sf, &uBrush);
    (void)pos;
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

static void DrawContent(Graphics& g, int w, int h)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    float sc = g_dpi/96.0f;
    float cx = w/2.0f;

    // 亚克力面板:圆角(贴靠边直角),半透明深色,透出背景模糊
    float rr = 12.0f*sc;
    float rTL = (g_mode==Mode::DockLeft || g_mode==Mode::DockTop) ? 0 : rr;
    float rTR = (g_mode==Mode::DockRight || g_mode==Mode::DockTop) ? 0 : rr;
    float rBR = (g_mode==Mode::DockRight || g_mode==Mode::DockBottom) ? 0 : rr;
    float rBL = (g_mode==Mode::DockLeft || g_mode==Mode::DockBottom) ? 0 : rr;
    SolidBrush panelBr(Color(0xB4, 0x1E, 0x1E, 0x28));
    FillRoundedPanel(g, 0, 0, (float)w, (float)h, rTL, rTR, rBR, rBL, panelBr);

    BYTE ar, ag, ab; StateColor(g_memLoad, ar, ag, ab);
    Color accent(255, ar, ag, ab);
    Color text(255,0xFD,0xFD,0xFD);
    Color dim(255,0xB4,0xC2,0xCE);

    // 状态点
    float dotY = Px(8*sc);
    SolidBrush dotBr(accent);
    g.FillEllipse(&dotBr, (REAL)(cx - Px(DOT/2*sc)), (REAL)dotY, (REAL)Px(DOT*sc), (REAL)Px(DOT*sc));
    float y = dotY + Px(DOT*sc) + Px(8*sc);

    if (g_mode == Mode::Float)
    {
        float rs = RING*sc;
        float ringCx = cx, ringCy = y + rs/2.0f;
        SolidBrush track(Color(0x30,0xFF,0xFF,0xFF));
        g.FillEllipse(&track, ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs);
        Pen arcPen(accent, RING_TH*sc);
        arcPen.SetStartCap(LineCapRound); arcPen.SetEndCap(LineCapRound);
        float sweep = (float)(g_memLoad/100.0*360.0);
        if (sweep > 0.5f)
        {
            Pen mask(Color(255, ar,ag,ab));
            // 先画暗弧背景底(把整环填成 track)后,再画亮弧
            g.DrawArc(&mask, ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs, -90.0f, sweep);
        }
        // 中心百分比
        Font fp(L"Segoe UI", 9.0f*sc, FontStyleBold, UnitPixel, nullptr);
        SolidBrush tb(text);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(FormatPct(g_memLoad).c_str(), -1, &fp, RectF(ringCx-rs/2.0f, ringCy-rs/2.0f, rs, rs), &sf, &tb);
        y = ringCy + rs/2.0f + Px(6*sc);
    }
    else
    {
        float capW = CAP_W*sc, capH = CAP_H*sc;
        float cy = y;
        SolidBrush capBg(Color(0x30,0xFF,0xFF,0xFF));
        g.FillRectangle(&capBg, cx-capW/2.0f, cy, capW, capH);
        float fh = capH * (float)(g_memLoad/100.0);
        if (fh > 0.5f)
        {
            SolidBrush fs(accent);
            g.FillRectangle(&fs, cx-capW/2.0f, cy+capH-fh, capW, fh);
        }
        y = cy + capH + Px(6*sc);
    }

    float rowW = (g_mode==Mode::Float ? (w - Px(16*sc)) : (w - Px(10*sc)));
    // divider
    SolidBrush sep(Color(0x24,0xFF,0xFF,0xFF));
    g.FillRectangle(&sep, (REAL)Px(8*sc), (REAL)y, (REAL)(w - Px(16*sc)), (REAL)(1.0f*sc));
    y += Px(8*sc);

    DrawSpeedRow(g, cx, y, rowW, ::FormatSpeed(g_downBps), CyanR(), CyanG(), CyanB());
    y += Px(46*sc);
    DrawSpeedRow(g, cx, y, rowW, ::FormatSpeed(g_upBps), ar, ag, ab);
    y += Px(46*sc);

    g.FillRectangle(&sep, (REAL)Px(8*sc), (REAL)y, (REAL)(w - Px(16*sc)), (REAL)(1.0f*sc));
    y += Px(8*sc);

    // CPU badge
    Font fBadge(L"Segoe UI", 9.0f*sc, FontStyleBold, UnitPixel, nullptr);
    SolidBrush badgeBg(Color(0x2E, 0x4C, 0xC9, 0xF0));
    Font fCpu(L"Segoe UI", 13.0f*sc, FontStyleBold, UnitPixel, nullptr);
    StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
    RectF badge(cx - Px(20*sc), y, Px(40*sc), Px(16*sc));
    SolidBrush cpuText(Color(255,CyanR(),CyanG(),CyanB()));
    SolidBrush cpuVal(text);
    g.FillRectangle(&badgeBg, badge);
    g.DrawString(L"CPU", -1, &fBadge, badge, &cf, &cpuText);
    RectF cpuV(cx - rowW/2.0f, badge.Y + badge.Height + Px(2*sc), rowW, Px(18*sc));
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
    EnableAcrylic(g_hwnd, 0x991E1E28u); // AABBGGRR
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
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
    wcscpy_s(nid.szTip, L"Perch");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void ToggleVisible()
{
    if (IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_HIDE);
    else { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); }
}

static bool IsAutoStart()
{
    wchar_t buf[MAX_PATH]; DWORD sz = sizeof(buf);
    LONG r = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz);
    return r == ERROR_SUCCESS;
}

static void SetAutoStart(bool on)
{
    HKEY k; DWORD disp;
    if (on)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, &disp) == ERROR_SUCCESS)
        {
            wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
            wchar_t val[MAX_PATH+8]; swprintf(val, MAX_PATH+8, L"\"%s\"", exe);
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

    // 初始位置:屏幕工作区右侧居中
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize=sizeof(mi); GetMonitorInfo(mon,&mi);
    RECT wa = mi.rcWork; RECT cr{}; GetClientRect(g_hwnd,&cr);
    int cw=cr.right, ch=cr.bottom;
    SetWindowPos(g_hwnd, nullptr, wa.right-cw-Px(40*(g_dpi/96.0f)), wa.top+(wa.bottom-wa.top-ch)/2, cw, ch, SWP_NOZORDER|SWP_NOACTIVATE);

    AddTray(g_hwnd);
    ShowWindow(g_hwnd, SW_SHOW);
    Render();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0))
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    GdiplusShutdown(g_gdiplusToken);
    return 0;
}
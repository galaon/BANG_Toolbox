// BANG_Picker.exe — 화면 어디서나 픽셀 색을 집는 스포이드 도우미
//
//  CEP 패널(Chromium 99)에서는 화면 픽셀을 읽을 방법이 없다 — 브라우저 EyeDropper API 는
//  존재하지만 CEF 가 오버레이를 못 띄워 2ms 만에 AbortError 로 끝나고, getDisplayMedia 는
//  Permission denied 다. 그래서 화면 집기는 이 작은 네이티브 도우미가 맡는다.
//  (PowerToys Color Picker · Just Color Picker 와 같은 구조: 화면을 한 번 캡처해 두고
//   그 정지 화면 위에 전체화면 오버레이를 띄운 뒤 확대 루페로 픽셀을 고르게 한다.)
//
//  사용법:  BANG_Picker.exe <결과파일>
//    고르면  <결과파일> 에 "#RRGGBB" 를 쓰고 0 으로 종료
//    취소하면 파일을 만들지 않고 1 로 종료 (Esc · 오른쪽 클릭 · 포커스 상실)
//
//  조작:  이동=마우스 / 방향키(Shift=10px) · 고르기=왼쪽 클릭·Enter·Space
//         확대=휠 (4~32배) · 취소=Esc·오른쪽 클릭
//
//  색은 **화면이 아니라 캡처 버퍼**에서 읽는다. 그래야 우리가 그린 루페·격자가
//  결과 색에 섞이지 않는다 (PowerToys 도 같은 이유로 버퍼에서 읽는다).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>

static const wchar_t* kClass = L"BANG_PickerOverlay";
#define WM_APP_PICK (WM_APP + 1)

struct Shot {
    HDC      dc   = nullptr;     // 캡처 화면을 담은 메모리 DC
    HBITMAP  bmp  = nullptr;
    HBITMAP  old  = nullptr;
    uint32_t* px  = nullptr;     // top-down BGRX
    int x0 = 0, y0 = 0, w = 0, h = 0;   // 가상 화면 원점·크기 (원점은 음수일 수 있다)
};

static Shot   g_shot;
static HWND   g_wnd    = nullptr;
static int    g_zoom   = 12;     // 확대 배율
static int    g_loupe  = 192;    // 루페 한 변 (화면 픽셀)
static POINT  g_cur    = { 0, 0 };
static POINT  g_prev   = { 0, 0 };
static bool   g_picked = false;
static bool   g_armed  = false;   // 스포이드를 부른 클릭이 떨어질 때까지 기다린다
static COLORREF g_pickedColor = 0;

// ── 캡처 ────────────────────────────────────────────────────
static bool CaptureScreen(Shot& s)
{
    s.x0 = GetSystemMetrics(SM_XVIRTUALSCREEN);
    s.y0 = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s.w  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s.h  = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (s.w <= 0 || s.h <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    s.dc = CreateCompatibleDC(screen);

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = s.w;
    bi.bmiHeader.biHeight      = -s.h;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    s.bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!s.bmp) { DeleteDC(s.dc); ReleaseDC(nullptr, screen); return false; }
    s.px  = (uint32_t*)bits;
    s.old = (HBITMAP)SelectObject(s.dc, s.bmp);

    BitBlt(s.dc, 0, 0, s.w, s.h, screen, s.x0, s.y0, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    ReleaseDC(nullptr, screen);
    return true;
}

static void FreeShot(Shot& s)
{
    if (s.dc)  { if (s.old) SelectObject(s.dc, s.old); DeleteDC(s.dc); s.dc = nullptr; }
    if (s.bmp) { DeleteObject(s.bmp); s.bmp = nullptr; }
    s.px = nullptr;
}

// 캡처 버퍼에서 화면 좌표의 색을 읽는다 (우리가 그린 루페가 섞이지 않는다)
static COLORREF SampleAt(int sx, int sy)
{
    int x = sx - g_shot.x0, y = sy - g_shot.y0;
    if (x < 0) x = 0; if (x >= g_shot.w) x = g_shot.w - 1;
    if (y < 0) y = 0; if (y >= g_shot.h) y = g_shot.h - 1;
    const uint32_t v = g_shot.px[(size_t)y * g_shot.w + x];
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// ── 루페 배치 ───────────────────────────────────────────────
//  커서 오른쪽 아래가 기본, 화면 밖으로 나가면 반대쪽으로 접는다.
static const int kLabelH = 30;
static const int kPad    = 22;

static RECT LoupeRect(POINT c)
{
    const int side = g_loupe;
    const int total = side + kLabelH;
    int left = c.x + kPad, top = c.y + kPad;
    const int rx = g_shot.x0, ry = g_shot.y0;
    if (left + side  > rx + g_shot.w) left = c.x - kPad - side;
    if (top  + total > ry + g_shot.h) top  = c.y - kPad - total;
    if (left < rx) left = rx;
    if (top  < ry) top  = ry;
    RECT r = { left, top, left + side, top + total };
    return r;
}

// ── 그리기 ──────────────────────────────────────────────────
static void DrawLoupe(HDC dc, POINT c)
{
    const RECT lr = LoupeRect(c);
    const int  left = lr.left, top = lr.top, side = g_loupe;

    // 홀수 개의 픽셀이 들어가야 정가운데가 생긴다
    int n = side / g_zoom; if (n < 3) n = 3; if ((n & 1) == 0) n--;
    const int half = n / 2;
    const int cell = side / n;               // 실제 그려지는 한 픽셀 크기
    const int draw = cell * n;               // 반올림 오차 제거

    const int sx = c.x - g_shot.x0 - half;
    const int sy = c.y - g_shot.y0 - half;

    SetStretchBltMode(dc, COLORONCOLOR);     // 최근접 — 픽셀이 뭉개지면 안 된다
    StretchBlt(dc, left, top, draw, draw, g_shot.dc, sx, sy, n, n, SRCCOPY);

    // 격자 (충분히 확대됐을 때만)
    if (cell >= 8) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HGDIOBJ op = SelectObject(dc, pen);
        for (int i = 1; i < n; i++) {
            MoveToEx(dc, left + i * cell, top, nullptr);        LineTo(dc, left + i * cell, top + draw);
            MoveToEx(dc, left, top + i * cell, nullptr);        LineTo(dc, left + draw, top + i * cell);
        }
        SelectObject(dc, op); DeleteObject(pen);
    }

    // 가운데 픽셀 — 흰 테두리 안에 검은 테두리라 어떤 배경에서도 보인다
    const int cx = left + half * cell, cy = top + half * cell;
    for (int i = 0; i < 2; i++) {
        HPEN pen = CreatePen(PS_SOLID, 1, i ? RGB(0, 0, 0) : RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, cx - i, cy - i, cx + cell + i + 1, cy + cell + i + 1);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pen);
    }

    // 바깥 테두리
    {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(240, 240, 240));
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, left - 1, top - 1, left + draw + 1, top + draw + 1);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pen);
    }

    // 라벨: 고른 색 견본 + #RRGGBB + R G B
    const COLORREF col = SampleAt(c.x, c.y);
    const int r = GetRValue(col), g = GetGValue(col), b = GetBValue(col);

    RECT lab = { left - 1, top + draw + 1, left + draw + 1, top + draw + 1 + kLabelH };
    HBRUSH bg = CreateSolidBrush(RGB(24, 24, 24));
    FillRect(dc, &lab, bg);
    DeleteObject(bg);

    RECT sw = { lab.left + 5, lab.top + 5, lab.left + 5 + (kLabelH - 10), lab.bottom - 5 };
    HBRUSH sb = CreateSolidBrush(col);
    FillRect(dc, &sw, sb);
    DeleteObject(sb);
    FrameRect(dc, &sw, (HBRUSH)GetStockObject(GRAY_BRUSH));

    char txt[64];
    sprintf_s(txt, "#%02X%02X%02X   %d %d %d", r, g, b, r, g, b);
    HFONT font = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HGDIOBJ of = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 235, 235));
    RECT tr = { sw.right + 8, lab.top, lab.right - 4, lab.bottom };
    DrawTextA(dc, txt, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    SelectObject(dc, of); DeleteObject(font);
}

// 루페가 차지하는(또는 차지했던) 영역만 다시 그린다 — 4K 전체를 매번 칠하면 느리다
static void InvalidateLoupe(POINT c)
{
    RECT r = LoupeRect(c);
    r.left -= 4; r.top -= 4; r.right += 4; r.bottom += 4;
    RECT w = { r.left - g_shot.x0, r.top - g_shot.y0, r.right - g_shot.x0, r.bottom - g_shot.y0 };
    InvalidateRect(g_wnd, &w, FALSE);
}

static void Finish(bool picked)
{
    g_picked = picked;
    if (picked) g_pickedColor = SampleAt(g_cur.x, g_cur.y);
    DestroyWindow(g_wnd);
}

// ── 저수준 입력 후크 ────────────────────────
//  포그라운드를 못 가져오면 창 메시지(WM_LBUTTONDOWN · WM_KEYDOWN)가 오지 않는다.
//  그래서 입력은 WH_MOUSE_LL / WH_KEYBOARD_LL 로 직접 받는다 (PowerToys Color Picker 와 같은 방식).
//  동시에 **그 입력을 삼켜서**(return 1) 밑에 깔린 AE 로 클릭이 새지 않게 한다 —
//  안 그러면 색을 고를 때마다 레이어가 선택되거나 끌려다니게 된다.
static HHOOK g_mouseHook = nullptr;
static HHOOK g_keyHook   = nullptr;

static void ZoomBy(int d)
{
    int z = g_zoom + d;
    if (z < 4) z = 4;
    if (z > 32) z = 32;
    if (z != g_zoom) { g_zoom = z; InvalidateLoupe(g_cur); }
}

static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lp;
        switch (wp) {
        case WM_MOUSEMOVE:
            if (m->pt.x != g_cur.x || m->pt.y != g_cur.y) {
                g_prev = g_cur; g_cur = m->pt;
                InvalidateLoupe(g_prev);
                InvalidateLoupe(g_cur);
            }
            break;
        case WM_LBUTTONDOWN:
            if (!g_armed) return 1;                 // 스포이드를 부른 바로 그 클릭
            g_cur = m->pt;
            PostMessageW(g_wnd, WM_APP_PICK, 1, 0);
            return 1;
        case WM_LBUTTONUP:
            if (!g_armed) { g_armed = true; }       // 불러온 클릭의 떼기까지 삼킨 다음부터 센다
            return 1;
        case WM_RBUTTONDOWN:
            PostMessageW(g_wnd, WM_APP_PICK, 0, 0);
            return 1;
        case WM_RBUTTONUP:
            return 1;
        case WM_MOUSEWHEEL:
            ZoomBy(((short)HIWORD(m->mouseData) > 0) ? 2 : -2);
            return 1;
        default: break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK KeyHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
        const int step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
        switch (k->vkCode) {
        case VK_ESCAPE: PostMessageW(g_wnd, WM_APP_PICK, 0, 0); return 1;
        case VK_RETURN:
        case VK_SPACE:  PostMessageW(g_wnd, WM_APP_PICK, 1, 0); return 1;
        case VK_LEFT:   SetCursorPos(g_cur.x - step, g_cur.y); return 1;
        case VK_RIGHT:  SetCursorPos(g_cur.x + step, g_cur.y); return 1;
        case VK_UP:     SetCursorPos(g_cur.x, g_cur.y - step); return 1;
        case VK_DOWN:   SetCursorPos(g_cur.x, g_cur.y + step); return 1;
        case VK_OEM_PLUS:  case VK_ADD:      ZoomBy(2);  return 1;
        case VK_OEM_MINUS: case VK_SUBTRACT: ZoomBy(-2); return 1;
        default: break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        // 요청된 영역만 캡처 화면으로 되돌린 뒤 루페를 얹는다
        const RECT& r = ps.rcPaint;
        BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, g_shot.dc, r.left, r.top, SRCCOPY);
        SetViewportOrgEx(dc, -g_shot.x0, -g_shot.y0, nullptr);   // 화면 좌표로 그리기
        DrawLoupe(dc, g_cur);
        SetViewportOrgEx(dc, 0, 0, nullptr);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_MOUSEMOVE: {
        POINT p; GetCursorPos(&p);
        if (p.x == g_cur.x && p.y == g_cur.y) return 0;
        g_prev = g_cur; g_cur = p;
        InvalidateLoupe(g_prev);
        InvalidateLoupe(g_cur);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 2 : -2;
        int z = g_zoom + d; if (z < 4) z = 4; if (z > 32) z = 32;
        if (z != g_zoom) { g_zoom = z; InvalidateLoupe(g_cur); }
        return 0;
    }
    case WM_LBUTTONDOWN: Finish(true);  return 0;
    case WM_RBUTTONDOWN: Finish(false); return 0;

    // ⚠ AE(CEP)가 띄우면 이 창은 포그라운드를 못 가져온다 — Windows 가 백그라운드 프로세스의
    //   포그라운드 탈취를 막기 때문이다. 그러면 SetCapture 도 듣지 않아 WM_MOUSEMOVE·WM_KEYDOWN 이
    //   아예 안 온다. 그래서 커서와 키 상태를 타이머로 직접 읽는다 (창을 직접 띄웠을 때는
    //   위의 메시지 경로가 그대로 동작하고, 아래 폴링은 같은 결과라 중복돼도 무해하다).
    // 안전망 — 후크가 막혔을 때도 커서를 따라가고, 다른 창이 위로 올라오면 다시 맨 앞으로
    case WM_TIMER: {
        POINT p;
        if (GetCursorPos(&p) && (p.x != g_cur.x || p.y != g_cur.y)) {
            g_prev = g_cur; g_cur = p;
            InvalidateLoupe(g_prev);
            InvalidateLoupe(g_cur);
        }
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }

    // 후크에서 올라오는 결정 (wp: 1 = 고름, 0 = 취소)
    case WM_APP_PICK: Finish(wp != 0); return 0;

    case WM_KEYDOWN: {
        const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
        POINT p = g_cur;
        switch (wp) {
        case VK_ESCAPE: Finish(false); return 0;
        case VK_RETURN: case VK_SPACE: Finish(true); return 0;
        case VK_LEFT:  p.x -= step; break;
        case VK_RIGHT: p.x += step; break;
        case VK_UP:    p.y -= step; break;
        case VK_DOWN:  p.y += step; break;
        default: return 0;
        }
        SetCursorPos(p.x, p.y);
        g_prev = g_cur; g_cur = p;
        InvalidateLoupe(g_prev); InvalidateLoupe(g_cur);
        return 0;
    }
    case WM_DESTROY:   PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) return 2;
    const wchar_t* outPath = argv[1];

    // 물리 픽셀 기준으로 좌표를 다루려면 per-monitor v2 가 필요하다 (안 그러면 고DPI 에서 어긋난다)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (!CaptureScreen(g_shot)) return 3;

    // -probe: 화면 계측값만 파일에 적고 끝낸다 (고DPI·다중 모니터 진단용)
    if (argc >= 3 && lstrcmpW(argv[2], L"-probe") == 0) {
        FILE* pf = nullptr;
        if (_wfopen_s(&pf, outPath, L"wb") == 0 && pf) {
            fprintf(pf, "virtual=%d,%d %dx%d  primary=%dx%d",
                    g_shot.x0, g_shot.y0, g_shot.w, g_shot.h,
                    GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
            fclose(pf);
        }
        FreeShot(g_shot);
        return 0;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = nullptr;               // 커서는 숨기고 루페만 보여준다
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                            kClass, L"BANG Picker", WS_POPUP,
                            g_shot.x0, g_shot.y0, g_shot.w, g_shot.h,
                            nullptr, nullptr, inst, nullptr);
    if (!g_wnd) { FreeShot(g_shot); return 4; }

    GetCursorPos(&g_cur);
    g_prev = g_cur;

    // ⚠ CEP 의 window.cep.process.createProcess 는 자식을 STARTUPINFO.wShowWindow = SW_HIDE 로 띄운다.
    //   Windows 는 프로세스의 **첫 ShowWindow 호출**을 그 값으로 가로채기 때문에 한 번만 부르면
    //   창이 영영 안 뜨고 사용자는 ‘아무 일도 안 일어난다’ 고 느낀다. 두 번 불러야 한다.
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
    SetWindowPos(g_wnd, HWND_TOPMOST, g_shot.x0, g_shot.y0, g_shot.w, g_shot.h, SWP_SHOWWINDOW);
    SetForegroundWindow(g_wnd);
    SetFocus(g_wnd);

    if (argc >= 3 && lstrcmpW(argv[2], L"-probe2") == 0) {
        Sleep(400);
        RECT wr; GetWindowRect(g_wnd, &wr);
        FILE* pf = nullptr;
        if (_wfopen_s(&pf, outPath, L"wb") == 0 && pf) {
            fprintf(pf, "rect=%ld,%ld-%ld,%ld vis=%d iconic=%d style=%08lX ex=%08lX fg=%d cap=%d",
                    wr.left, wr.top, wr.right, wr.bottom,
                    (int)IsWindowVisible(g_wnd), (int)IsIconic(g_wnd),
                    (unsigned long)GetWindowLongW(g_wnd, GWL_STYLE),
                    (unsigned long)GetWindowLongW(g_wnd, GWL_EXSTYLE),
                    (int)(GetForegroundWindow() == g_wnd), (int)(GetCapture() == g_wnd));
            fclose(pf);
        }
        return 0;
    }
    SetCapture(g_wnd);
    SetTimer(g_wnd, 1, 100, nullptr);     // 안전망용 느린 폴링
    // 불러온 클릭이 아직 눌려 있으면 그걸 고르기로 오해하지 않도록 떼는 것까지 기다린다
    g_armed = !(GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, inst, 0);
    g_keyHook   = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, inst, 0);
    while (ShowCursor(FALSE) >= 0) {}

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    while (ShowCursor(TRUE) < 0) {}
    KillTimer(g_wnd, 1);
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    if (g_keyHook)   UnhookWindowsHookEx(g_keyHook);
    ReleaseCapture();
    FreeShot(g_shot);

    if (!g_picked) return 1;

    FILE* f = nullptr;
    if (_wfopen_s(&f, outPath, L"wb") != 0 || !f) return 5;
    fprintf(f, "#%02X%02X%02X", GetRValue(g_pickedColor), GetGValue(g_pickedColor), GetBValue(g_pickedColor));
    fclose(f);
    return 0;
}

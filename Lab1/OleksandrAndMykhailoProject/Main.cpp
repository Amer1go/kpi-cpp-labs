#include <windows.h>
#include <string>

static const wchar_t szClassName[] = L"Alex";
static const wchar_t szTitle[] = L"LabWork #1, variant 6, Group 6";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int CALLBACK CountFontsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam)
{
    (*reinterpret_cast<int*>(lParam))++;
    return 1;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_BYTEALIGNCLIENT | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_NO);
    wcex.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szClassName;
    wcex.hIconSm = LoadIcon(nullptr, IDI_INFORMATION);
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    const int width = 400;
    const int height = 500;

    int cx = GetSystemMetrics(SM_CXSCREEN);
    int x = (cx - width) / 2;
    int y = 0;

    DWORD style = WS_OVERLAPPEDWINDOW;
#ifdef START_MINIMIZED
    style |= WS_MINIMIZE;
#endif

    HWND hWnd = CreateWindowW(szClassName, szTitle, style,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr);
    if (!hWnd)
        return FALSE;

#ifdef START_MINIMIZED
    ShowWindow(hWnd, SW_SHOWMINIMIZED);
#else
    ShowWindow(hWnd, nCmdShow);
#endif
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect;
        GetClientRect(hWnd, &rect);

        int numFonts = GetDeviceCaps(hdc, NUMFONTS);

        int installedFonts = 0;
        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(hdc, &lf, CountFontsProc, (LPARAM)&installedFonts, 0);

        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);

        std::wstring text =
            L"Кількість шрифтів (пристрій): " + std::to_wstring(numFonts) + L"\n" +
            L"Встановлених шрифтів: " + std::to_wstring(installedFonts) + L"\n" +
            L"Висота шрифту: " + std::to_wstring(tm.tmHeight) + L" px";

        RECT calc = rect;
        DrawTextW(hdc, text.c_str(), -1, &calc, DT_CENTER | DT_CALCRECT);
        int textH = calc.bottom - calc.top;
        rect.top += (rect.bottom - rect.top - textH) / 2;

        DrawTextW(hdc, text.c_str(), -1, &rect, DT_CENTER);

        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
        InvalidateRect(hWnd, nullptr, TRUE);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
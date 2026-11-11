
#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")  
#endif
#ifndef CLIENT_NO
#define CLIENT_NO 1
#endif

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <string>
#pragma comment(lib, "ws2_32.lib")

#define SERV_PORT 5000
#define ID_CONNECT 1001
#define ID_SEND    1002
#define ID_INPUT   1003

HWND   hEdit, hInput;
SOCKET sock = INVALID_SOCKET;

void Log(const std::wstring& s)
{
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)(s + L"\r\n").c_str());
}

bool Connect(HWND hWnd)
{
    if (sock != INVALID_SOCKET) { Log(L"Вже підключено"); return true; }
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);                        
    if (sock == INVALID_SOCKET) { Log(L"Помилка створення сокета"); return false; }

    SOCKADDR_IN dst = {};
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(SERV_PORT);
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");                            

    if (connect(sock, (PSOCKADDR)&dst, sizeof(dst)) == SOCKET_ERROR)         
    {
        Log(L"Помилка з'єднання (сервер запущено?)");
        closesocket(sock); sock = INVALID_SOCKET;
        return false;
    }
    Log(L"Зв'язок встановлено");
    return true;
}

bool SendText(const std::wstring& w)                                        
{
    if (sock == INVALID_SOCKET) { Log(L"Спочатку встановіть з'єднання"); return false; }
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, nullptr, nullptr);
    if (send(sock, u.c_str(), (int)u.size(), 0) == SOCKET_ERROR) { Log(L"Помилка надсилання"); return false; }
    Log(L"Надіслано серверу:\r\n" + w);
    return true;
}

struct EnumCtx { int count; };
BOOL CALLBACK CountWin(HWND h, LPARAM lp)                                   
{
    if (IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && GetWindowTextLengthW(h) > 0)
        ++((EnumCtx*)lp)->count;
    return TRUE;
}

std::wstring BuildMessage(HWND hWnd)
{
    wchar_t b[256];
#if CLIENT_NO == 1
    SYSTEM_POWER_STATUS ps = {};
    GetSystemPowerStatus(&ps);
    std::wstring bat;
    if (ps.BatteryFlag == 128)           bat = L"батареї немає";
    else {
        wsprintfW(b, L"%u%%, живлення від %s", ps.BatteryLifePercent == 255 ? 0u : (unsigned)ps.BatteryLifePercent,
                  ps.ACLineStatus == 1 ? L"мережі" : L"батареї");
        bat = b;
    }
    DEVMODEW dm = { };  dm.dmSize = sizeof(dm);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);              
    HDC hdc = GetDC(hWnd);
    int bpp = GetDeviceCaps(hdc, BITSPIXEL);                                
    ReleaseDC(hWnd, hdc);
    wsprintfW(b, L"Частота оновлення екрану: %lu Гц\r\nГлибина кольору: %d біт", dm.dmDisplayFrequency, bpp);
    return L"Стан батареї: " + bat + L"\r\n" + b;
#elif CLIENT_NO == 2
    EnumCtx ctx = { 0 };
    EnumWindows(CountWin, (LPARAM)&ctx);                                     
    int frame = GetSystemMetrics(SM_CXFRAME);                                
    HDC hdc = GetDC(hWnd);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);                               
    ReleaseDC(hWnd, hdc);
    wsprintfW(b, L"Кількість відкритих вікон: %d\r\nШирина рамки вікна (SM_CXFRAME): %d px\r\nDPI вертикально: %d",
              ctx.count, frame, dpiY);
    return b;
#else
    wchar_t t[1024];
    GetWindowTextW(hInput, t, 1024);                                        
    return t;
#endif
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMENU bar = CreateMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_CONNECT, L"Set connection");
        AppendMenuW(sub, MF_STRING, ID_SEND,    L"Send message");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, L"Client");
        SetMenu(hWnd, bar);

        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                              0, 0, 100, 100, hWnd, nullptr, nullptr, nullptr);
#if CLIENT_NO == 3
        hInput = CreateWindowW(L"EDIT", L"Привіт, сервере!", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               0, 0, 100, 24, hWnd, (HMENU)ID_INPUT, nullptr, nullptr);
#endif
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) { MessageBoxW(hWnd, L"WSAStartup error", L"Error", MB_OK); return -1; }
        return 0;
    }
    case WM_SIZE:
    {
        int w = LOWORD(lParam), h = HIWORD(lParam);
#if CLIENT_NO == 3
        MoveWindow(hEdit,  0, 0, w, h - 26, TRUE);
        MoveWindow(hInput, 0, h - 26, w, 26, TRUE);
#else
        MoveWindow(hEdit, 0, 0, w, h, TRUE);
#endif
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_CONNECT) Connect(hWnd);
        if (LOWORD(wParam) == ID_SEND)    SendText(BuildMessage(hWnd));
        return 0;
    case WM_DESTROY:
        if (sock != INVALID_SOCKET) closesocket(sock);
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AlexClient";
    RegisterClassExW(&wc);
    wchar_t title[32];
    wsprintfW(title, L"Client %d", CLIENT_NO);
    HWND hWnd = CreateWindowW(L"AlexClient", title, WS_OVERLAPPEDWINDOW, 600, 100 + CLIENT_NO * 80, 420, 260, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

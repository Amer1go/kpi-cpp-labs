#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")   
#endif
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <string>
#pragma comment(lib, "ws2_32.lib")

#define SERV_PORT   5000
#define MAX_CLIENTS 3
#define WSA_ACCEPT  (WM_USER + 0)
#define WSA_NETEVENT (WM_USER + 1)
#define ID_START 1001
#define ID_STOP  1002

HWND   hEdit;
SOCKET srv = INVALID_SOCKET;
SOCKET cl[MAX_CLIENTS] = { INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET };

void Log(const std::wstring& s)                      
{
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)(s + L"\r\n").c_str());
}

void ServerStart(HWND hWnd)
{
    if (srv != INVALID_SOCKET) { Log(L"Сервер вже запущено"); return; }

    srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);                
    if (srv == INVALID_SOCKET) { Log(L"Помилка створення сокета"); return; }

    SOCKADDR_IN addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(SERV_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (LPSOCKADDR)&addr, sizeof(addr)) == SOCKET_ERROR)  
    { Log(L"Помилка bind (порт зайнятий?)"); closesocket(srv); srv = INVALID_SOCKET; return; }

    if (listen(srv, SOMAXCONN) == SOCKET_ERROR)
    { Log(L"Помилка listen"); closesocket(srv); srv = INVALID_SOCKET; return; }

    WSAAsyncSelect(srv, hWnd, WSA_ACCEPT, FD_ACCEPT);                
    Log(L"Сервер запущено, порт 5000");
}

void ServerStop(HWND hWnd)
{
    if (srv == INVALID_SOCKET) return;
    WSAAsyncSelect(srv, hWnd, 0, 0);
    closesocket(srv);
    srv = INVALID_SOCKET;
    for (int i = 0; i < MAX_CLIENTS; ++i)
        if (cl[i] != INVALID_SOCKET) { closesocket(cl[i]); cl[i] = INVALID_SOCKET; }
    Log(L"Сервер зупинено");
}

void OnAccept(HWND hWnd, LPARAM lParam)
{
    if (WSAGETSELECTERROR(lParam)) { Log(L"accept error"); return; }

    SOCKADDR_IN from = {};
    int fl = sizeof(from);
    SOCKET s = accept(srv, (LPSOCKADDR)&from, &fl);
    if (s == INVALID_SOCKET) return;

    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) if (cl[i] == INVALID_SOCKET) { idx = i; break; }
    if (idx < 0) { Log(L"Забагато клієнтів, з'єднання відхилено"); closesocket(s); return; }

    cl[idx] = s;
    WSAAsyncSelect(s, hWnd, WSA_NETEVENT, FD_READ | FD_CLOSE);

    wchar_t line[128];
    wsprintfW(line, L"Підключено клієнта %d  (%S:%u)", idx + 1, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
    Log(line);
}

void OnNetEvent(WPARAM wParam, LPARAM lParam)
{
    SOCKET s = (SOCKET)wParam;
    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) if (cl[i] == s) idx = i;
    if (idx < 0) return;

    if (WSAGETSELECTEVENT(lParam) == FD_READ)
    {
        char buf[2048];
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n > 0)
        {
            std::wstring w(n + 1, L'\0');
            int wl = MultiByteToWideChar(CP_UTF8, 0, buf, n, &w[0], n + 1);
            w.resize(wl);
            std::wstring norm;
            for (wchar_t ch : w) { if (ch == L'\n' && (norm.empty() || norm.back() != L'\r')) norm += L'\r'; norm += ch; }
            wchar_t head[64];
            wsprintfW(head, L"--- Дані від Клієнта %d ---\r\n", idx + 1);
            Log(std::wstring(head) + norm);
        }
    }
    if (WSAGETSELECTEVENT(lParam) == FD_CLOSE)
    {
        WSAAsyncSelect(s, GetParent(hEdit), 0, 0);
        closesocket(s);
        cl[idx] = INVALID_SOCKET;
        wchar_t line[64];
        wsprintfW(line, L"Клієнт %d відключився", idx + 1);
        Log(line);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMENU bar = CreateMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_START, L"Start");
        AppendMenuW(sub, MF_STRING, ID_STOP,  L"Stop");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, L"Server");
        SetMenu(hWnd, bar);

        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                              0, 0, 100, 100, hWnd, nullptr, nullptr, nullptr);
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa)) { MessageBoxW(hWnd, L"WSAStartup error", L"Error", MB_OK); return -1; }
        wchar_t line[128];
        wsprintfW(line, L"Winsock %d.%d готовий", LOBYTE(wsa.wVersion), HIBYTE(wsa.wVersion));
        Log(line);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_START) ServerStart(hWnd);
        if (LOWORD(wParam) == ID_STOP)  ServerStop(hWnd);
        return 0;
    case WSA_ACCEPT:    OnAccept(hWnd, lParam);            return 0;
    case WSA_NETEVENT:  OnNetEvent(wParam, lParam);        return 0;
    case WM_DESTROY:
        ServerStop(hWnd);
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
    wc.lpszClassName = L"AlexServer";
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowW(L"AlexServer", L"Server", WS_OVERLAPPEDWINDOW, 100, 100, 480, 420, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

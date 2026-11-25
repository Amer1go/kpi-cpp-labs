#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_NAME    L"lab6_FileMapping"
#define EVENT_NAME  L"lab6_NewDataEvent"
#define MUTEX_NAME  L"lab6_Mutex"
#define MAP_CAP     2000         

struct SharedBuf { DWORD used; wchar_t data[MAP_CAP]; };

#define ID_START 101
#define ID_READ  102
#define ID_STOP  103
#define ID_SEND  101
#define ID_FILL  102
#define WM_NEWDATA (WM_APP + 1)

static void AppendText(HWND hEdit, const wchar_t* s)
{
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)s);
}

static void Spawn(const wchar_t* args)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf(cmd, MAX_PATH + 64, L"\"%ls\" %ls", exe, args);
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

static int ParseClientId(PCWSTR cmd)
{
    const wchar_t* p = wcsstr(cmd, L"client");
    if (!p) return 0;
    p += 6; while (*p == L' ') p++;
    int n = _wtoi(p);
    return n > 0 ? n : 1;
}

static int MessageLoop()
{
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return (int)m.wParam;
}

// ======================= СЕРВЕР =======================
namespace srv {

static HWND hEdit;
static HANDLE hMap, hMutex, hEvent, hThread;
static SharedBuf* pBuf;
static volatile bool running;

static DWORD WINAPI Watcher(LPVOID p)     
{
    HWND hWnd = (HWND)p;
    while (running)
        if (WaitForSingleObject(hEvent, 200) == WAIT_OBJECT_0)
            PostMessageW(hWnd, WM_NEWDATA, 0, 0);
    return 0;
}

static void ReadAll()
{
    if (!pBuf) return;
    wchar_t* tmp = NULL;
    WaitForSingleObject(hMutex, INFINITE);
    DWORD n = pBuf->used;
    if (n)
    {
        tmp = (wchar_t*)calloc(n + 1, sizeof(wchar_t));
        if (tmp) memcpy(tmp, pBuf->data, n * sizeof(wchar_t));
        pBuf->used = 0;                    
    }
    ReleaseMutex(hMutex);
    if (tmp) { AppendText(hEdit, tmp); free(tmp); }
}

static void StartServer(HWND hWnd)
{
    if (hMap) return;
    hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedBuf), MAP_NAME);
    if (!hMap)
    {
        MessageBoxW(hWnd, L"Не можливо створити відображений у пам'яті об'єкт", L"Map", MB_OK);
        return;
    }
    pBuf   = (SharedBuf*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    hMutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    hEvent = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME);  
    if (!pBuf || !hMutex || !hEvent)
    {
        MessageBoxW(hWnd, L"Подання проектованого файлу неможливе", L"Map", MB_OK);
        return;
    }
    running = true;
    hThread = CreateThread(NULL, 0, Watcher, hWnd, 0, NULL);
    SetWindowTextW(hEdit, L"Сервер FileMapping запущено\r\n");
}

static void StopServer(bool say)
{
    if (!hMap) return;
    running = false;
    if (hThread) { WaitForSingleObject(hThread, 2000); CloseHandle(hThread); hThread = NULL; }
    UnmapViewOfFile(pBuf); pBuf = NULL;
    CloseHandle(hEvent); CloseHandle(hMutex); CloseHandle(hMap);
    hEvent = hMutex = hMap = NULL;
    if (say) SetWindowTextW(hEdit, L"Сервер FileMapping зупинено\r\n");
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMENU bar = CreateMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_START, L"Start");
        AppendMenuW(sub, MF_STRING, ID_READ, L"Read");
        AppendMenuW(sub, MF_STRING, ID_STOP, L"Stop");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, L"FileMapping");
        SetMenu(hWnd, bar);
        hEdit = CreateWindowW(L"EDIT", L"Лабораторна робота - Відображення (проекція) файлу в пам'ять.\r\n",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, hWnd, NULL, NULL, NULL);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_START: StartServer(hWnd); break;
        case ID_READ:  ReadAll();         break;   
        case ID_STOP:  StopServer(true);  break;
        }
        return 0;
    case WM_NEWDATA:                                
        ReadAll();
        return 0;
    case WM_DESTROY:
        StopServer(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static int Run(HINSTANCE hi, int nShow)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Lab6Server";
    RegisterClassW(&wc);
    HWND h = CreateWindowW(wc.lpszClassName, L"Сервер", WS_OVERLAPPEDWINDOW,
        0, 0, 440, 500, NULL, NULL, hi, NULL);
    ShowWindow(h, nShow);
    return MessageLoop();
}
}

// ======================= КЛІЄНТ =======================
namespace cli {

static HWND hEdit;
static int g_id = 1;

// >>> ТУТ формується повідомлення за замовчуванням. Для іншого варіанта міняти лише цю функцію. <<<
static void BuildDefault(wchar_t* out, size_t n)
{
    if (g_id == 1)
        swprintf(out, n, L"Дані від Клієнта №1:\r\nКількість кнопок миші = %d\r\nКолесо миші = %ls",
                 GetSystemMetrics(SM_CMOUSEBUTTONS), GetSystemMetrics(SM_MOUSEWHEELPRESENT) ? L"Так" : L"Ні");
    else
    {
        MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        swprintf(out, n, L"Дані від Клієнта №2:\r\nРозмір файлу підкачки: %llu байт\r\nВільно у файлі підкачки: %llu байт",
                 (unsigned long long)ms.ullTotalPageFile, (unsigned long long)ms.ullAvailPageFile);
    }
}

static void Send(HWND hWnd)
{
    wchar_t text[1024];
    GetWindowTextW(hEdit, text, 1000);
    wcscat(text, L"\r\n\r\n");
    size_t len = wcslen(text);

    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_NAME);
    if (!hMap)
    {
        MessageBoxW(hWnd, L"Не можливо відкрити відображений у пам'яті об'єкт\r\n(сервер не запущено? FileMapping -> Start)", L"Map", MB_OK);
        return;
    }
    SharedBuf* p = (SharedBuf*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuf));
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MUTEX_NAME);
    HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
    if (!p || !hMutex || !hEvent)
        MessageBoxW(hWnd, L"Подання проектованого файлу неможливе", L"Map", MB_OK);
    else
    {
        WaitForSingleObject(hMutex, INFINITE);
        bool ok = p->used + len < MAP_CAP;
        if (ok)
        {
            memcpy(p->data + p->used, text, len * sizeof(wchar_t));   
            p->used += (DWORD)len;
        }
        ReleaseMutex(hMutex);
        if (ok) SetEvent(hEvent);        
        else MessageBoxW(hWnd, L"Буфер заповнений, зачекайте поки сервер прочитає", L"Map", MB_OK);
    }
    if (p) UnmapViewOfFile(p);
    if (hEvent) CloseHandle(hEvent);
    if (hMutex) CloseHandle(hMutex);
    CloseHandle(hMap);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HMENU bar = CreateMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_SEND, L"Send");
        AppendMenuW(sub, MF_STRING, ID_FILL, L"Default data");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, L"File");
        SetMenu(hWnd, bar);
        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                              0, 0, 10, 10, hWnd, NULL, NULL, NULL);
        wchar_t t[1024]; BuildDefault(t, 1024); SetWindowTextW(hEdit, t);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_SEND) Send(hWnd);
        else if (LOWORD(wParam) == ID_FILL)
        { wchar_t t[1024]; BuildDefault(t, 1024); SetWindowTextW(hEdit, t); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static int Run(HINSTANCE hi, int nShow, int id)
{
    g_id = id;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"Lab6Client";
    RegisterClassW(&wc);
    wchar_t title[32]; swprintf(title, 32, L"Клієнт %d", id);
    HWND h = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
        450, (id - 1) * 270, 420, 265, NULL, NULL, hi, NULL);
    ShowWindow(h, nShow);
    return MessageLoop();
}
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int nShow)
{
    int id = ParseClientId(cmd);
    if (id > 0) return cli::Run(hi, nShow, id);

    if (!wcsstr(cmd, L"server"))                
        for (int i = 1; i <= 2; i++)
        {
            wchar_t a[32]; swprintf(a, 32, L"client %d", i);
            Spawn(a);
        }
    return srv::Run(hi, nShow);
}

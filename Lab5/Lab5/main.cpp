#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t* MAILSLOT_NAME = L"\\\\.\\mailslot\\lab5";
#define ID_CREATE 101
#define ID_CLOSE  102
#define ID_SEND   101
#define ID_FILL   102
#define IDT_TIMER 1001

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

static HWND hMain, hEdit;
static HANDLE hMailslot = INVALID_HANDLE_VALUE;

static void CloseBox(bool say)
{
    if (hMailslot == INVALID_HANDLE_VALUE) return;
    KillTimer(hMain, IDT_TIMER);
    CloseHandle(hMailslot);
    hMailslot = INVALID_HANDLE_VALUE;
    if (say) AppendText(hEdit, L"\r\nПоштовий сервер закрито\r\n");
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hMain = hWnd;
        HMENU bar = CreateMenu(), sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_CREATE, L"Create");
        AppendMenuW(sub, MF_STRING, ID_CLOSE, L"Close");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, L"MailSlot");
        SetMenu(hWnd, bar);
        hEdit = CreateWindowW(L"EDIT", L"Лабораторна робота - Поштова скриня.\r\n",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, hWnd, NULL, NULL, NULL);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_CREATE)
        {
            if (hMailslot != INVALID_HANDLE_VALUE) return 0;
            hMailslot = CreateMailslotW(MAILSLOT_NAME, 0, 0, NULL);
            if (hMailslot == INVALID_HANDLE_VALUE)
            {
                wchar_t e[128];
                swprintf(e, 128, L"\r\nПоштовий сервер. Error: %lu\r\n", GetLastError());
                AppendText(hEdit, e);
                return 0;
            }
            AppendText(hEdit, L"\r\nСервер запущено\r\n");
            SetTimer(hWnd, IDT_TIMER, 1000, NULL);
        }
        else if (LOWORD(wParam) == ID_CLOSE)
            CloseBox(true);
        return 0;
    case WM_TIMER:
        if (wParam == IDT_TIMER && hMailslot != INVALID_HANDLE_VALUE)
        {
            DWORD cbNext, cMsg;
            while (GetMailslotInfo(hMailslot, NULL, &cbNext, &cMsg, NULL)
                   && cMsg > 0 && cbNext != MAILSLOT_NO_MESSAGE)
            {
                wchar_t* buf = (wchar_t*)calloc(cbNext + 2, 1);
                DWORD cbRead = 0;
                if (buf && ReadFile(hMailslot, buf, cbNext, &cbRead, NULL))
                {
                    AppendText(hEdit, L"\r\n");
                    AppendText(hEdit, buf);
                    AppendText(hEdit, L"\r\n");
                }
                free(buf);
                if (!cbRead) break;
            }
        }
        return 0;
    case WM_DESTROY:
        CloseBox(false);
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
    wc.lpszClassName = L"Lab5Server";
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
static void BuildDefault(HWND hWnd, wchar_t* out, size_t n)
{
    switch (g_id)
    {
    case 1:
    {
        RECT r; GetWindowRect(hWnd, &r);
        swprintf(out, n, L"Дані Клієнта #1:\r\n - ширина екрану = %d\r\n - висота екрану = %d\r\n"
                         L" - ширина вікна = %ld\r\n - висота вікна = %ld",
                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                 (long)(r.right - r.left), (long)(r.bottom - r.top));
        break;
    }
    case 2:
    {
        HDC dc = GetDC(hWnd);
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
        ReleaseDC(hWnd, dc);
        swprintf(out, n, L"Дані Клієнта #2:\r\n - міжрядковий інтервал = %ld\r\n"
                         L" - ширина піктограми = %d\r\n - висота піктограми = %d",
                 (long)tm.tmExternalLeading, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
        break;
    }
    default:
    {
        srand(GetTickCount());
        int a = rand() % 90 + 10, b = rand() % 90 + 10;
        swprintf(out, n, L"Дані Клієнта #3:\r\n - сторона a = %d\r\n - сторона b = %d\r\n - площа S = a*b = %d",
                 a, b, a * b);
    }
    }
}

static void Send(HWND hWnd)
{
    wchar_t buf[1024];
    int len = GetWindowTextW(hEdit, buf, 1024);
    HANDLE h = CreateFileW(MAILSLOT_NAME, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        wchar_t e[128];
        swprintf(e, 128, L"Помилка поштового серверу: %lu\r\n(сервер не запущено? MailSlot -> Create)", GetLastError());
        MessageBoxW(hWnd, e, L"Mailslot", MB_OK | MB_ICONERROR);
        return;
    }
    DWORD wr;
    WriteFile(h, buf, (len + 1) * sizeof(wchar_t), &wr, NULL);
    CloseHandle(h);
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
        wchar_t t[1024]; BuildDefault(hWnd, t, 1024); SetWindowTextW(hEdit, t);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_SEND) Send(hWnd);
        else if (LOWORD(wParam) == ID_FILL)
        { wchar_t t[1024]; BuildDefault(hWnd, t, 1024); SetWindowTextW(hEdit, t); }
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
    wc.lpszClassName = L"Lab5Client";
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
        for (int i = 1; i <= 3; i++)
        {
            wchar_t a[32]; swprintf(a, 32, L"client %d", i);
            Spawn(a);
        }
    return srv::Run(hi, nShow);
}

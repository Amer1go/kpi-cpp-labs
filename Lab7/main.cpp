
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#define PIPE_NAME L"\\\\.\\pipe\\lab7"

static void Spawn(const wchar_t* args)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf(cmd, MAX_PATH + 64, L"\"%ls\" %ls", exe, args);
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
    { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}

// ======================= СЕРВЕР =======================
static CRITICAL_SECTION cs;

static DWORD WINAPI ClientThread(LPVOID p)   
{
    HANDLE hPipe = (HANDLE)p;
    wchar_t buf[2048];
    DWORD cbRead, cbWritten, id = GetCurrentThreadId();

    for (;;)
    {
        BOOL ok = ReadFile(hPipe, buf, sizeof(buf) - sizeof(wchar_t), &cbRead, NULL);
        if (!ok && GetLastError() != ERROR_MORE_DATA) break;      // клієнт відключився
        buf[cbRead / sizeof(wchar_t)] = 0;

        EnterCriticalSection(&cs);
        wprintf(L"[клієнт %lu] Received: <%ls>\n", id, buf);
        LeaveCriticalSection(&cs);

        if (!WriteFile(hPipe, buf, cbRead, &cbWritten, NULL)) break;  
        if (wcscmp(buf, L"exit") == 0) break;
    }
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);                                           // закриття екземпляра каналу
    EnterCriticalSection(&cs);
    wprintf(L"[клієнт %lu] Disconnected\n", id);
    LeaveCriticalSection(&cs);
    return 0;
}

static int ServerMain(bool spawnClients)
{
    SetConsoleTitleW(L"Pipe server");
    InitializeCriticalSection(&cs);
    wprintf(L"Pipe server started. Ctrl+C - вихід.\n");

    for (;;)
    {
        HANDLE hPipe = CreateNamedPipeW(PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 5000, NULL);
        if (hPipe == INVALID_HANDLE_VALUE)
        {
            wprintf(L"CreateNamedPipe: Error %lu\n", GetLastError());
            break;
        }
        if (spawnClients)            
        {
            spawnClients = false;
            Spawn(L"client 1");
            Spawn(L"client 2");
        }
        wprintf(L"Waiting for connect ...\n");
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected)
        {
            wprintf(L"Connected. Waiting for command ...\n");
            HANDLE t = CreateThread(NULL, 0, ClientThread, hPipe, 0, NULL);
            if (t) CloseHandle(t); else CloseHandle(hPipe);
        }
        else
        {
            wprintf(L"ConnectNamedPipe: Error %lu\n", GetLastError());
            CloseHandle(hPipe);
        }
    }
    DeleteCriticalSection(&cs);
    return 0;
}

// ======================= КЛІЄНТ =======================
// >>> ТУТ формується повідомлення за замовчуванням. Для іншого варіанта міняти лише цю функцію. <<<
static BOOL CALLBACK CountWin(HWND h, LPARAM lp)
{
    if (IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && GetWindowTextLengthW(h) > 0)
        ++*(int*)lp;
    return TRUE;
}

static void BuildDefault(int id, wchar_t* out, size_t n)
{
    HDC hdc = GetDC(NULL);
    if (id == 1)
    {
        SYSTEM_POWER_STATUS ps = {};
        GetSystemPowerStatus(&ps);
        wchar_t bat[64];
        if (ps.BatteryFlag == 128) swprintf(bat, 64, L"no battery");
        else swprintf(bat, 64, L"%u%%, %ls", ps.BatteryLifePercent == 255 ? 0u : (unsigned)ps.BatteryLifePercent,
                      ps.ACLineStatus == 1 ? L"AC power" : L"on battery");
        DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
        EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
        swprintf(out, n, L"Client 1: battery=%ls, refresh rate=%lu Hz, color depth=%d bit",
                 bat, dm.dmDisplayFrequency, GetDeviceCaps(hdc, BITSPIXEL));
    }
    else
    {
        int count = 0;
        EnumWindows(CountWin, (LPARAM)&count);
        swprintf(out, n, L"Client 2: open windows=%d, frame width (SM_CXFRAME)=%d px, DPI Y=%d",
                 count, GetSystemMetrics(SM_CXFRAME), GetDeviceCaps(hdc, LOGPIXELSY));
    }
    ReleaseDC(NULL, hdc);
}

static bool Exchange(HANDLE h, const wchar_t* msg, wchar_t* reply, size_t replyCap)
{
    DWORD wr, rd;
    if (!WriteFile(h, msg, (DWORD)((wcslen(msg) + 1) * sizeof(wchar_t)), &wr, NULL)) return false;
    if (!ReadFile(h, reply, (DWORD)(replyCap * sizeof(wchar_t)), &rd, NULL)) return false;
    return true;
}

static void WaitEnter()
{
    wchar_t t[16];
    wprintf(L"\nPress Enter to close...\n");
    fgetws(t, 16, stdin);
}

static int ClientMain(int id)
{
    wchar_t title[32]; swprintf(title, 32, L"Pipe client %d", id);
    SetConsoleTitleW(title);

    HANDLE h = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 25; tries++)
    {
        h = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) break;
        DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY) WaitNamedPipeW(PIPE_NAME, 5000);
        else if (e == ERROR_FILE_NOT_FOUND) Sleep(200);      // сервер ще не створив канал
        else { wprintf(L"CreateFile: Error %lu\n", e); WaitEnter(); return 1; }
    }
    if (h == INVALID_HANDLE_VALUE) { wprintf(L"Server not found\n"); WaitEnter(); return 1; }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, NULL, NULL);
    wprintf(L"Connected. Type 'exit' to terminate\n");

    wchar_t msg[512], reply[2048];
    BuildDefault(id, msg, 512);
    wprintf(L"Sending default data: %ls\n", msg);
    if (!Exchange(h, msg, reply, 2047)) { wprintf(L"Pipe error %lu\n", GetLastError()); WaitEnter(); return 1; }
    wprintf(L"Received back: <%ls>\n", reply);

    for (;;)
    {
        wprintf(L"cmd> ");
        if (!fgetws(msg, 512, stdin)) break;
        size_t l = wcslen(msg);
        while (l && (msg[l - 1] == L'\n' || msg[l - 1] == L'\r')) msg[--l] = 0;
        if (!l) continue;
        if (!Exchange(h, msg, reply, 2047)) { wprintf(L"Pipe error %lu\n", GetLastError()); break; }
        wprintf(L"Received back: <%ls>\n", reply);
        if (wcscmp(msg, L"exit") == 0) break;
    }
    CloseHandle(h);
    WaitEnter();
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);

    if (argc >= 2 && wcscmp(argv[1], L"client") == 0)
        return ClientMain(argc >= 3 && _wtoi(argv[2]) > 0 ? _wtoi(argv[2]) : 1);
    bool spawn = !(argc >= 2 && wcscmp(argv[1], L"server") == 0);
    return ServerMain(spawn);
}

#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#pragma comment(lib, "comdlg32.lib")

#define WM_BENCH (WM_APP + 1)
enum { ID_FILE = 101, ID_ARGS, ID_BROWSE, ID_RUN, ID_SHOW, ID_BENCH, ID_STATUS, ID_OUT };

struct Proc { std::wstring name; DWORD pid; HANDLE h; HANDLE watcher; bool finished; DWORD exitCode; };
std::vector<Proc> procs;                 
std::wstring gTable;                     
HANDLE hProcMutex, hSumMutex;            
HANDLE hStopEvent, hStartEvent;          
HANDLE hBenchThread = nullptr;
volatile LONG gActive = 0;               
uint64_t gSum = 0;                       
HWND hMain, hFile, hArgs, hStatus, hOut;

const size_t DATA_SIZE = 64u << 20;      
const int    ROUNDS    = 16;             

ULONGLONG FT(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

DWORD WINAPI Watcher(LPVOID param)
{
    size_t idx = (size_t)param;
    DWORD tid = GetCurrentThreadId();
    InterlockedIncrement(&gActive);

    WaitForSingleObject(hProcMutex, INFINITE);
    HANDLE hp = procs[idx].h;  DWORD pid = procs[idx].pid;
    ReleaseMutex(hProcMutex);
    printf("[TID %lu] watcher started, PID %lu\n", tid, pid);

    HANDLE w[2] = { hp, hStopEvent };
    DWORD r = WaitForMultipleObjects(2, w, FALSE, INFINITE);      
    if (r == WAIT_OBJECT_0)
    {
        DWORD code = 0;
        GetExitCodeProcess(hp, &code);
        WaitForSingleObject(hProcMutex, INFINITE);
        printf("[TID %lu] access to process table: PID %lu exited, code %lu\n", tid, pid, code);
        procs[idx].finished = true;
        procs[idx].exitCode = code;
        ReleaseMutex(hProcMutex);
    }
    printf("[TID %lu] watcher finished\n", tid);
    InterlockedDecrement(&gActive);
    ExitThread(0);
    return 0;
}

struct BenchArg { const uint8_t* d; size_t from, to; };

DWORD WINAPI BenchWorker(LPVOID param)
{
    BenchArg* a = (BenchArg*)param;
    DWORD tid = GetCurrentThreadId();
    InterlockedIncrement(&gActive);
    printf("[TID %lu] bench worker started, range %llu..%llu\n", tid, (unsigned long long)a->from, (unsigned long long)a->to);

    WaitForSingleObject(hStartEvent, INFINITE);                  

    uint64_t sum = 0;
    DWORD code = 0;
    for (int r = 0; r < ROUNDS && !code; ++r)
    {
        size_t i = a->from;
        while (i < a->to && !code)
        {
            size_t end = i + (1u << 20);
            if (end > a->to) end = a->to;
            for (; i < end; ++i)
                sum += (uint64_t)(a->d[i] ^ r) * ((i & 15) + 1);   
            if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) code = 2;   
        }
    }
    if (!code)
    {
        WaitForSingleObject(hSumMutex, INFINITE);
        printf("[TID %lu] access to shared checksum\n", tid);
        gSum += sum;
        ReleaseMutex(hSumMutex);
    }
    printf("[TID %lu] bench worker finished, code %lu\n", tid, code);
    InterlockedDecrement(&gActive);
    ExitThread(code);
    return 0;
}

DWORD WINAPI BenchMain(LPVOID)
{
    DWORD tid = GetCurrentThreadId();
    InterlockedIncrement(&gActive);
    printf("[TID %lu] benchmark started\n", tid);

    std::vector<uint8_t> data(DATA_SIZE);
    for (size_t i = 0; i < DATA_SIZE; ++i) data[i] = (uint8_t)(i * 31 + 7);

    LARGE_INTEGER freq;  QueryPerformanceFrequency(&freq);
    const int counts[4] = { 2, 4, 8, 16 };
    uint64_t reference = 0;
    double bestMs = 1e18;  int bestN = 0;

    WaitForSingleObject(hProcMutex, INFINITE);
    gTable = L"Потоків | Час, мс | CPU, % | Коментар\r\n";
    ReleaseMutex(hProcMutex);

    for (int k = 0; k < 4; ++k)
    {
        int n = counts[k];
        gSum = 0;
        ResetEvent(hStartEvent);                                  

        std::vector<BenchArg> args(n);
        std::vector<HANDLE> th;
        for (int i = 0; i < n; ++i)
        {
            args[i] = { data.data(), DATA_SIZE * i / n, DATA_SIZE * (i + 1) / n };
            HANDLE h = CreateThread(nullptr, 0, BenchWorker, &args[i], 0, nullptr);
            if (h) th.push_back(h);
        }

        FILETIME i0, k0, u0, i1, k1, u1;
        GetSystemTimes(&i0, &k0, &u0);
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        SetEvent(hStartEvent);                                    
        WaitForMultipleObjects((DWORD)th.size(), th.data(), TRUE, INFINITE);   
        QueryPerformanceCounter(&t1);
        GetSystemTimes(&i1, &k1, &u1);

        bool failed = (th.size() != (size_t)n);
        for (HANDLE h : th)
        {
            DWORD ec = 0;  GetExitCodeThread(h, &ec);
            if (ec != 0) failed = true;
            CloseHandle(h);
        }

        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        double dIdle = (double)(FT(i1) - FT(i0));
        double dTotal = (double)((FT(k1) - FT(k0)) + (FT(u1) - FT(u0)));   
        double cpu = dTotal > 0 ? 100.0 * (1.0 - dIdle / dTotal) : 0;

        const wchar_t* note = L"OK";
        if (failed) note = L"потік завершився аварійно / не створився";
        else if (k == 0) reference = gSum;
        else if (gSum != reference) note = L"контрольна сума не збіглася!";
        if (!failed && ms < bestMs) { bestMs = ms; bestN = n; }

        wchar_t line[256];
        swprintf(line, 256, L"%7d | %7.0f | %6.1f | %s\r\n", n, ms, cpu, note);
        WaitForSingleObject(hProcMutex, INFINITE);
        gTable += line;
        ReleaseMutex(hProcMutex);
        PostMessageW(hMain, WM_BENCH, 0, 0);
        if (WaitForSingleObject(hStopEvent, 0) == WAIT_OBJECT_0) break;
    }

    wchar_t fin[160];
    swprintf(fin, 160, L"\r\nНайшвидше: %d потоків (%.0f мс)", bestN, bestMs);
    WaitForSingleObject(hProcMutex, INFINITE);
    gTable += fin;
    ReleaseMutex(hProcMutex);
    PostMessageW(hMain, WM_BENCH, 0, 0);

    printf("[TID %lu] benchmark finished\n", tid);
    InterlockedDecrement(&gActive);
    ExitThread(0);
    return 0;
}

void Browse(HWND hWnd)
{
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Програми (*.exe)\0*.exe\0Усі файли\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(hFile, path);
}

void Run(HWND hWnd)
{
    wchar_t file[MAX_PATH], args[512];
    GetWindowTextW(hFile, file, MAX_PATH);
    GetWindowTextW(hArgs, args, 512);
    if (!file[0]) { MessageBoxW(hWnd, L"Вкажіть ім'я файлу", L"Помилка", MB_OK | MB_ICONWARNING); return; }

    std::wstring cmd = L"\"" + std::wstring(file) + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(file, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        wchar_t err[128];
        wsprintfW(err, L"CreateProcess не вдалося, код помилки: %lu", GetLastError());
        MessageBoxW(hWnd, err, L"Помилка", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);

    std::wstring name = file;
    size_t p = name.find_last_of(L"\\/");
    if (p != std::wstring::npos) name = name.substr(p + 1);

    WaitForSingleObject(hProcMutex, INFINITE);
    procs.push_back({ name, pi.dwProcessId, pi.hProcess, nullptr, false, 0 });
    size_t idx = procs.size() - 1;
    procs[idx].watcher = CreateThread(nullptr, 0, Watcher, (LPVOID)idx, 0, nullptr);  
    ReleaseMutex(hProcMutex);
}

void Show()
{
    std::wstring out;
    WaitForSingleObject(hProcMutex, INFINITE);
    if (procs.empty()) out = L"Процесів ще не запущено.\r\n";
    wchar_t line[512];
    for (size_t i = 0; i < procs.size(); ++i)
    {
        FILETIME c, e, k, u, now;
        GetProcessTimes(procs[i].h, &c, &e, &k, &u);
        GetSystemTimeAsFileTime(&now);
        ULONGLONG end = procs[i].finished ? FT(e) : FT(now);
        double sec = (double)(end - FT(c)) / 1e7;

        wsprintfW(line, L"#%u  Ім'я: %s\r\n    PID: %lu   HANDLE: 0x%p\r\n    Стан: %s",
                  (unsigned)(i + 1), procs[i].name.c_str(), procs[i].pid, procs[i].h,
                  procs[i].finished ? L"завершено" : L"виконується");
        out += line;
        if (procs[i].finished) { wsprintfW(line, L" (код %lu)", procs[i].exitCode); out += line; }
        swprintf(line, 512, L"\r\n    Час виконання: %.2f с\r\n\r\n", sec);
        out += line;
    }
    ReleaseMutex(hProcMutex);
    SetWindowTextW(hOut, out.c_str());
}

void StartBench(HWND hWnd)
{
    if (hBenchThread && WaitForSingleObject(hBenchThread, 0) == WAIT_TIMEOUT)
    { MessageBoxW(hWnd, L"Бенчмарк вже виконується", L"Інфо", MB_OK); return; }
    if (hBenchThread) CloseHandle(hBenchThread);
    SetWindowTextW(hOut, L"Бенчмарк запущено, зачекайте...\r\n");
    hBenchThread = CreateThread(nullptr, 0, BenchMain, nullptr, 0, nullptr);
}

void Shutdown()
{
    SetEvent(hStopEvent);                                 
    std::vector<HANDLE> hs;
    WaitForSingleObject(hProcMutex, INFINITE);
    for (auto& p : procs) if (p.watcher) hs.push_back(p.watcher);
    ReleaseMutex(hProcMutex);
    if (hBenchThread) hs.push_back(hBenchThread);

    for (size_t i = 0; i < hs.size(); i += MAXIMUM_WAIT_OBJECTS)       
    {
        DWORD n = (DWORD)std::min((size_t)MAXIMUM_WAIT_OBJECTS, hs.size() - i);
        WaitForMultipleObjects(n, &hs[i], TRUE, 10000);
    }
    for (HANDLE h : hs)
    {
        DWORD ec = 0;  GetExitCodeThread(h, &ec);
        if (ec != 0) printf("thread exited abnormally, code %lu\n", ec);
        CloseHandle(h);
    }
    for (auto& p : procs) CloseHandle(p.h);
    CloseHandle(hProcMutex); CloseHandle(hSumMutex); CloseHandle(hStopEvent); CloseHandle(hStartEvent);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        hMain = hWnd;
        AllocConsole();                                        
        freopen("CONOUT$", "w", stdout);
        setvbuf(stdout, nullptr, _IONBF, 0);
        hProcMutex  = CreateMutexW(nullptr, FALSE, nullptr);
        hSumMutex   = CreateMutexW(nullptr, FALSE, nullptr);
        hStopEvent  = CreateEventW(nullptr, TRUE, FALSE, nullptr);  
        hStartEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        CreateWindowW(L"STATIC", L"Файл:", WS_CHILD | WS_VISIBLE, 10, 14, 70, 20, hWnd, nullptr, nullptr, nullptr);
        hFile = CreateWindowW(L"EDIT", L"C:\\Windows\\System32\\notepad.exe", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              85, 10, 400, 24, hWnd, (HMENU)ID_FILE, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Огляд...", WS_CHILD | WS_VISIBLE, 495, 10, 90, 24, hWnd, (HMENU)ID_BROWSE, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Аргументи:", WS_CHILD | WS_VISIBLE, 10, 48, 75, 20, hWnd, nullptr, nullptr, nullptr);
        hArgs = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                              85, 44, 500, 24, hWnd, (HMENU)ID_ARGS, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Запустити", WS_CHILD | WS_VISIBLE, 85, 80, 130, 28, hWnd, (HMENU)ID_RUN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Показати процеси", WS_CHILD | WS_VISIBLE, 225, 80, 150, 28, hWnd, (HMENU)ID_SHOW, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Бенчмарк 2/4/8/16", WS_CHILD | WS_VISIBLE, 385, 80, 150, 28, hWnd, (HMENU)ID_BENCH, nullptr, nullptr);
        hStatus = CreateWindowW(L"STATIC", L"Активних потоків: 0", WS_CHILD | WS_VISIBLE, 10, 118, 300, 20, hWnd, (HMENU)ID_STATUS, nullptr, nullptr);
        hOut = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                             10, 145, 575, 270, hWnd, (HMENU)ID_OUT, nullptr, nullptr);
        SetTimer(hWnd, 1, 300, nullptr);
        return 0;

    case WM_TIMER:
    {
        wchar_t t[64];
        wsprintfW(t, L"Активних потоків: %ld", gActive);
        SetWindowTextW(hStatus, t);
        return 0;
    }
    case WM_BENCH:
    {
        WaitForSingleObject(hProcMutex, INFINITE);
        std::wstring copy = gTable;
        ReleaseMutex(hProcMutex);
        SetWindowTextW(hOut, copy.c_str());
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_BROWSE: Browse(hWnd);     break;
        case ID_RUN:    Run(hWnd);        break;
        case ID_SHOW:   Show();           break;
        case ID_BENCH:  StartBench(hWnd); break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        Shutdown();
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"Alex";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(L"Alex", L"ЛБ_03*: процеси і потоки (варіант 6)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                              CW_USEDEFAULT, 0, 615, 480, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

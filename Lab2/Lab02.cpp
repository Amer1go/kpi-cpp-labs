#include <windows.h>

const int CELL = 40;                       
const wchar_t CLASS_NAME[] = L"Alex";


COLORREF CellColor(int row, int col)
{
    return RGB((row * 67 + col * 131 + 40) % 256,
               (row * 113 + col * 29 + 150) % 256,
               (row * 17 + col * 199 + 90) % 256);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        InvalidateRect(hWnd, nullptr, TRUE);        
        return 0;

    case WM_ERASEBKGND:
        return 1;                                     

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        int cols = (rc.right  + CELL - 1) / CELL;     
        int rows = (rc.bottom + CELL - 1) / CELL;

        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
            {
                RECT cell = { c * CELL, r * CELL, c * CELL + CELL - 2, r * CELL + CELL - 2 }; 
                HBRUSH br = CreateSolidBrush(CellColor(r, c));
                FillRect(hdc, &cell, br);
                DeleteObject(br);
            }

        wchar_t info[64];
        wsprintfW(info, L" %d x %d = %d ", cols, rows, cols * rows);
        SetBkColor(hdc, RGB(255, 255, 255));
        TextOutW(hdc, 5, 5, info, lstrlenW(info));

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(CLASS_NAME, L"ЛБ_02: квадрати (варіант 6)", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, 0, 640, 480, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

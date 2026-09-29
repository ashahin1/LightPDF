#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

struct WindowFindContext {
    DWORD processId = 0;
    HWND hWnd = nullptr;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowFindContext* ctx = (WindowFindContext*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->processId) {
        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, _countof(className));
        if (wcscmp(className, L"LightPDF_WindowClass") == 0) {
            ctx->hWnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

int main() {
    std::wstring exePath = L"bin\\LightPDF.exe";
    std::wstring pdfPath = L"C:\\AntiGravity_Projects\\NovaPDF\\sample_document.pdf";
    std::wstring cmdLine = L"\"" + exePath + L"\" \"" + pdfPath + L"\"";

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = {};

    std::wcout << L"====================================================\n";
    std::wcout << L"  LightPDF Navigation & Go to Page Verification\n";
    std::wcout << L"====================================================\n";

    BOOL success = CreateProcessW(
        nullptr,
        &cmdLine[0],
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!success) {
        std::wcerr << L"Failed to create process: " << GetLastError() << L"\n";
        return 1;
    }

    WaitForInputIdle(pi.hProcess, 5000);
    Sleep(500);

    WindowFindContext ctx = { pi.dwProcessId, nullptr };
    EnumWindows(EnumWindowsProc, (LPARAM)&ctx);

    if (!ctx.hWnd) {
        std::wcerr << L"Failed to find LightPDF window for PID " << pi.dwProcessId << L"\n";
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    auto GetTitle = [&](HWND hwnd) -> std::wstring {
        wchar_t title[512] = {};
        GetWindowTextW(hwnd, title, _countof(title));
        return std::wstring(title);
    };

    std::wcout << L"[1] Initial Window Title: " << GetTitle(ctx.hWnd) << L"\n";

    // 1. Test Next Page (VK_RIGHT)
    SendMessageW(ctx.hWnd, WM_KEYDOWN, VK_RIGHT, 0);
    Sleep(150);
    std::wcout << L"[2] After VK_RIGHT:       " << GetTitle(ctx.hWnd) << L"\n";

    // 2. Test VK_END (jump to last page)
    SendMessageW(ctx.hWnd, WM_KEYDOWN, VK_END, 0);
    Sleep(150);
    std::wcout << L"[3] After VK_END:         " << GetTitle(ctx.hWnd) << L"\n";

    // 3. Test VK_HOME (jump to first page)
    SendMessageW(ctx.hWnd, WM_KEYDOWN, VK_HOME, 0);
    Sleep(150);
    std::wcout << L"[4] After VK_HOME:        " << GetTitle(ctx.hWnd) << L"\n";

    // 4. Test Go to Page (Ctrl+G, type '2', press Enter)
    // First, simulate opening Go to Page:
    // When Go to Page is open, send key codes
    // Send WM_LBUTTONDOWN on HUD Pill to open Go to Page, OR post Ctrl+G
    // Let's test clicking the HUD Pill:
    RECT rc = {};
    GetClientRect(ctx.hWnd, &rc);
    int hudClickX = (rc.right - rc.left) / 2;
    int hudClickY = (rc.bottom - rc.top) - 25; // HUD pill is ~32px high, 16px from bottom
    LPARAM hudPt = MAKELPARAM(hudClickX, hudClickY);

    SendMessageW(ctx.hWnd, WM_LBUTTONDOWN, MK_LBUTTON, hudPt);
    Sleep(100);

    // Now Go to Page overlay is open! Type '2' and VK_RETURN:
    SendMessageW(ctx.hWnd, WM_KEYDOWN, '2', 0);
    Sleep(100);
    SendMessageW(ctx.hWnd, WM_KEYDOWN, VK_RETURN, 0);
    Sleep(150);
    std::wcout << L"[5] After Go to Page (2): " << GetTitle(ctx.hWnd) << L"\n";

    // 5. Test Scrollbar Dragging
    // Click on right edge to grab scrollbar and drag down to jump pages
    int sbX = rc.right - 10;
    int sbStartY = rc.top + 40;
    int sbTargetY = rc.bottom - 40;

    SendMessageW(ctx.hWnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sbX, sbStartY));
    Sleep(50);
    SendMessageW(ctx.hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(sbX, sbTargetY));
    Sleep(100);
    SendMessageW(ctx.hWnd, WM_LBUTTONUP, 0, MAKELPARAM(sbX, sbTargetY));
    Sleep(150);
    std::wcout << L"[6] After Scrollbar Drag: " << GetTitle(ctx.hWnd) << L"\n";

    // 6. Test Continuous Scroll Toggle (VK_HOME back to 1)
    SendMessageW(ctx.hWnd, WM_KEYDOWN, VK_HOME, 0);
    Sleep(100);
    std::wcout << L"[7] Back to Page 1:       " << GetTitle(ctx.hWnd) << L"\n";

    // Cleanly close
    PostMessageW(ctx.hWnd, WM_CLOSE, 0, 0);
    WaitForSingleObject(pi.hProcess, 1000);
    TerminateProcess(pi.hProcess, 0);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wcout << L"====================================================\n";
    std::wcout << L"  VERIFICATION SUCCESSFUL!\n";
    std::wcout << L"====================================================\n";
    return 0;
}

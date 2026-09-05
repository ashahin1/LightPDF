#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <string>
#include "app_window.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nCmdShow) {
    // 1. Initialize High-DPI Awareness for crisp rendering on 4K/retina displays
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2. Initialize COM & WinRT Apartment
    winrt::init_apartment();

    // 3. Parse optional command line argument for direct file invocation
    std::wstring initialFile;
    if (lpCmdLine && lpCmdLine[0] != L'\0') {
        std::wstring cmd = lpCmdLine;
        // Strip leading/trailing whitespace and quotation marks
        size_t start = cmd.find_first_not_of(L" \t\"");
        size_t end = cmd.find_last_not_of(L" \t\"");
        if (start != std::wstring::npos && end != std::wstring::npos && end >= start) {
            initialFile = cmd.substr(start, end - start + 1);
        }
    }

    // 4. Create application window and enter event pump
    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, initialFile)) {
        return 1;
    }

    int exitCode = app.Run();

    // Instant exit: skip redundant heap traversal on shutdown
    ExitProcess((UINT)exitCode);
    return exitCode;
}

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <shlobj.h>
#include <string>
#include "app_window.hpp"
#include "supported_formats.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, [[maybe_unused]] PWSTR lpCmdLine, int nCmdShow) {
    // 1. Initialize High-DPI Awareness for crisp rendering on 4K/retina displays
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2. Initialize COM & WinRT Apartment
    winrt::init_apartment();

    // 3. Parse command line arguments safely with CommandLineToArgvW
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring initialFile;
    bool isSilent = false;
    bool doRegister = false;
    bool doUnregister = false;

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (_wcsicmp(arg.c_str(), L"/silent") == 0 || _wcsicmp(arg.c_str(), L"-silent") == 0) {
                isSilent = true;
            } else if (_wcsicmp(arg.c_str(), L"/register") == 0 || _wcsicmp(arg.c_str(), L"-register") == 0) {
                doRegister = true;
            } else if (_wcsicmp(arg.c_str(), L"/unregister") == 0 || _wcsicmp(arg.c_str(), L"-unregister") == 0) {
                doUnregister = true;
            } else if (initialFile.empty() && !arg.empty() && arg[0] != L'-' && arg[0] != L'/') {
                if (SupportedFormats::IsSupported(arg)) {
                    initialFile = arg;
                }
            }
        }
        LocalFree(argv);
    }

    if (doRegister) {
        AppWindow::RegisterShellAssociations();
        if (!isSilent) {
            MessageBoxW(nullptr,
                L"LightPDF successfully registered!\n\n"
                L"To make it your default PDF viewer, choose it in Windows Settings > Apps > Default apps, "
                L"or use \"Open with\" > \"Choose another app\" on any PDF document.\n\n"
                L"Use /unregister to remove the associations.",
                L"LightPDF", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    if (doUnregister) {
        AppWindow::UnregisterShellAssociations();
        if (!isSilent) {
            MessageBoxW(nullptr,
                L"LightPDF shell associations successfully removed.",
                L"LightPDF", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    // 4. Single-Instance Check with WM_COPYDATA Tab Forwarding
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"LightPDF_SingleInstance_Mutex_9A7B3E");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(WINDOW_CLASS_NAME, nullptr);
        if (hExisting) {
            if (!initialFile.empty()) {
                COPYDATASTRUCT cds = {};
                cds.dwData = 1;
                cds.cbData = (DWORD)((initialFile.length() + 1) * sizeof(wchar_t));
                cds.lpData = (PVOID)initialFile.c_str();
                SendMessageW(hExisting, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            if (IsIconic(hExisting)) {
                ShowWindow(hExisting, SW_RESTORE);
            }
            SetForegroundWindow(hExisting);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // 5. Create application window and enter event pump
    int exitCode = 0;
    {
        AppWindow app;
        if (!app.Create(hInstance, nCmdShow, initialFile)) {
            if (hMutex) CloseHandle(hMutex);
            return 1;
        }

        exitCode = app.Run();
    }

    if (hMutex) CloseHandle(hMutex);

    // Instant exit: skip redundant heap traversal on shutdown (app is cleanly destructed)
    ExitProcess((UINT)exitCode);
    return exitCode;
}

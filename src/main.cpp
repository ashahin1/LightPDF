#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <shlobj.h>
#include <string>
#include "app_window.hpp"

static bool RegisterFileAssociation() {
    wchar_t exePath[MAX_PATH * 2] = { 0 };
    GetModuleFileNameW(nullptr, exePath, _countof(exePath));

    std::wstring openCmd = L"\"" + std::wstring(exePath) + L"\" \"%1\"";
    std::wstring iconCmd = L"\"" + std::wstring(exePath) + L"\",0";

    HKEY hKey = nullptr;
    // 1. HKCU\Software\Classes\LightPDF.Document
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)L"PDF Document", (DWORD)(sizeof(L"PDF Document")));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document\\DefaultIcon", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)iconCmd.c_str(), (DWORD)((iconCmd.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document\\shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)openCmd.c_str(), (DWORD)((openCmd.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // 2. HKCU\Software\Classes\.pdf\OpenWithProgids -> LightPDF.Document
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.pdf\\OpenWithProgids", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"LightPDF.Document", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
        RegCloseKey(hKey);
    }

    // 3. HKCU\Software\Classes\Applications\LightPDF.exe
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LightPDF.exe\\SupportedTypes", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L".pdf", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
        RegCloseKey(hKey);
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LightPDF.exe\\shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)openCmd.c_str(), (DWORD)((openCmd.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

static bool UnregisterFileAssociation() {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document");
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.pdf\\OpenWithProgids", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"LightPDF.Document");
        RegCloseKey(hKey);
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LightPDF.exe");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR lpCmdLine, int nCmdShow) {
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
                initialFile = arg;
            }
        }
        LocalFree(argv);
    }

    if (doRegister) {
        RegisterFileAssociation();
        if (!isSilent) {
            MessageBoxW(nullptr, L"LightPDF has been registered as a PDF viewer in your user profile.\nYou can now select it in Windows 'Open with' or Default Apps.", L"LightPDF Registration", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    if (doUnregister) {
        UnregisterFileAssociation();
        if (!isSilent) {
            MessageBoxW(nullptr, L"LightPDF file associations removed.", L"LightPDF Registration", MB_OK | MB_ICONINFORMATION);
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
    AppWindow app;
    if (!app.Create(hInstance, nCmdShow, initialFile)) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    int exitCode = app.Run();

    if (hMutex) CloseHandle(hMutex);

    // Instant exit: skip redundant heap traversal on shutdown
    ExitProcess((UINT)exitCode);
    return exitCode;
}

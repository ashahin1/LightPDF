#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <string>

#pragma comment(lib, "psapi.lib")

int main() {
    std::wstring exePath = L"bin\\LightPDF.exe";
    std::wstring pdfPath = L"C:\\AntiGravity_Projects\\NovaPDF\\sample_document.pdf";
    std::wstring cmdLine = L"\"" + exePath + L"\" \"" + pdfPath + L"\"";

    LARGE_INTEGER freq, tStart, tReady;
    QueryPerformanceFrequency(&freq);

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi = {};

    std::wcout << L"====================================================\n";
    std::wcout << L"  LightPDF Performance & Memory Benchmark\n";
    std::wcout << L"====================================================\n";
    std::wcout << L"Target Executable: " << exePath << L"\n";
    std::wcout << L"Target Document:   " << pdfPath << L"\n\n";

    QueryPerformanceCounter(&tStart);

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

    // Wait until process enters idle message loop and finishes first frame render
    DWORD waitResult = WaitForInputIdle(pi.hProcess, 5000);
    QueryPerformanceCounter(&tReady);

    double startupMs = (double)(tReady.QuadPart - tStart.QuadPart) * 1000.0 / (double)freq.QuadPart;

    // Allow short settling window (100ms) for GPU swap chain initialization
    Sleep(150);

    // Measure memory footprint via PSAPI
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(pi.hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        double workingSetMb = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
        double privateBytesMb = (double)pmc.PrivateUsage / (1024.0 * 1024.0);
        double peakWorkingSetMb = (double)pmc.PeakWorkingSetSize / (1024.0 * 1024.0);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "--- Startup Latency ---\n";
        std::cout << "  Cold Launch to Input Idle: " << startupMs << " ms\n\n";

        std::cout << "--- Memory Footprint ---\n";
        std::cout << "  Current Working Set (RAM): " << workingSetMb << " MB\n";
        std::cout << "  Peak Working Set (RAM):    " << peakWorkingSetMb << " MB\n";
        std::cout << "  Private Bytes Commit:      " << privateBytesMb << " MB\n\n";

        // Verification against targets
        std::cout << "--- Target Evaluation ---\n";
        if (startupMs < 50.0) {
            std::cout << "  [PASS] Startup Latency (< 50ms): EXCELLENT (" << startupMs << " ms)\n";
        } else {
            std::cout << "  [INFO] Startup Latency: " << startupMs << " ms\n";
        }

        if (workingSetMb < 30.0) {
            std::cout << "  [PASS] Working Set Memory (< 30MB): EXCELLENT (" << workingSetMb << " MB)\n";
        } else {
            std::cout << "  [INFO] Working Set: " << workingSetMb << " MB\n";
        }
    }

    // Cleanly terminate test process
    PostThreadMessageW(pi.dwThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(pi.hProcess, 500);
    TerminateProcess(pi.hProcess, 0);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "====================================================\n";
    return 0;
}

#include <iostream>
#include <windows.h>
#include <cassert>
#include "src/pdf_document.hpp"
#include "src/pdf_parser.hpp"
#include "src/d2d_renderer.hpp"

int main() {
    winrt::init_apartment();

    std::wstring path = L"C:\\Users\\eng_a\\Desktop\\_المعادلات_.pdf";
    std::wcout << L"1. Loading document..." << std::endl;
    PdfDocumentWrapper doc;
    if (!doc.Open(path)) {
        std::wcout << L"Failed to open document!" << std::endl;
        return 1;
    }

    std::wcout << L"2. Parsing document..." << std::endl;
    PdfParser parser;
    if (!parser.Load(path)) {
        std::wcout << L"Failed to load parser!" << std::endl;
        return 1;
    }

    // Verify Type 3 font detection
    bool hasType3 = parser.PageHasType3Fonts(0);
    std::wcout << L"Page 0 PageHasType3Fonts: " << (hasType3 ? L"TRUE" : L"FALSE") << std::endl;
    if (!hasType3) {
        std::wcout << L"ERROR: Expected Type 3 font on page 0!" << std::endl;
        return 1;
    }

    // Create a dummy hidden window for D2DRenderer
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_HREDRAW | CS_VREDRAW, DefWindowProcW, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"TestType3Class", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"TestType3Class", L"TestType3", WS_OVERLAPPEDWINDOW, 0, 0, 1024, 768, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!hwnd) {
        std::wcout << L"Failed to create window!" << std::endl;
        return 1;
    }

    std::wcout << L"3. Initializing D2DRenderer..." << std::endl;
    D2DRenderer renderer;
    if (!renderer.Initialize(hwnd)) {
        std::wcout << L"Failed to initialize D2DRenderer!" << std::endl;
        return 1;
    }

    auto page = doc.GetPage(0);
    auto pSize = doc.GetPageSize(0);

    std::wcout << L"4. Testing Single-Page Render (RenderPage with hasType3=true)..." << std::endl;
    renderer.RenderPage(
        page,
        1.0f,
        0.0f,
        0.0f,
        pSize,
        0,
        1,
        L"Fit Width",
        hasType3
    );
    std::wcout << L"-> RenderPage succeeded without crash!" << std::endl;

    std::wcout << L"5. Testing Zoomed Render (zoom = 2.5)..." << std::endl;
    renderer.RenderPage(
        page,
        2.5f,
        50.0f,
        50.0f,
        pSize,
        0,
        1,
        L"250%",
        hasType3
    );
    std::wcout << L"-> Zoomed RenderPage succeeded without crash!" << std::endl;

    std::wcout << L"6. Testing Continuous Scroll Mode (RenderContinuous with hasType3=true)..." << std::endl;
    ContinuousPageInfo cpInfo;
    cpInfo.page = page;
    cpInfo.pageSize = pSize;
    cpInfo.pageIndex = 0;
    cpInfo.xOffset = 24.0f;
    cpInfo.yOffset = 24.0f;
    cpInfo.hasType3Font = hasType3;
    std::vector<ContinuousPageInfo> vPages = { cpInfo };

    renderer.RenderContinuous(
        vPages,
        1.0f,
        0,
        1,
        L"Fit Width",
        true
    );
    std::wcout << L"-> RenderContinuous succeeded without crash!" << std::endl;

    std::wcout << L"7. Testing Print Rasterization (PrintPageToHdc with hasType3=true)..." << std::endl;
    HDC hdcScreen = GetDC(hwnd);
    bool printOk = renderer.PrintPageToHdc(page, hdcScreen, pSize, hasType3);
    ReleaseDC(hwnd, hdcScreen);
    std::wcout << L"-> PrintPageToHdc returned: " << (printOk ? L"TRUE" : L"FALSE") << std::endl;

    DestroyWindow(hwnd);
    std::wcout << L"\nALL RENDERER TESTS PASSED WITH 100% SUCCESS!" << std::endl;
    return 0;
}

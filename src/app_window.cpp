#include "app_window.hpp"
#include <windowsx.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <vector>

static const wchar_t* CLASS_NAME = L"LightPDF_WindowClass";

AppWindow::AppWindow() = default;

AppWindow::~AppWindow() {
    m_cancelPrint = true;
    if (m_printThread.joinable()) {
        m_printThread.join();
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool AppWindow::Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile) {
    m_hInstance = hInstance;

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = AppWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    // Create window with standard modern window styling
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
        CLASS_NAME,
        L"LightPDF",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 750,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) return false;

    DragAcceptFiles(m_hwnd, TRUE);

    if (!m_renderer.Initialize(m_hwnd)) {
        return false;
    }

    if (!initialFile.empty()) {
        OpenFile(initialFile);
    } else {
        UpdateTitle();
        m_renderer.RenderBlank(L"");
    }

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    return true;
}

int AppWindow::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = (CREATESTRUCTW*)lParam;
        pThis = (AppWindow*)pCreate->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
    } else {
        pThis = (AppWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_APP_OPEN_FILE: {
        std::unique_ptr<std::wstring> pPath((std::wstring*)lParam);
        if (pPath && !pPath->empty()) {
            OpenFile(*pPath);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        Render();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        m_renderer.Resize(width, height);
        if (m_zoomMode != ZoomMode::Custom) {
            RecalculateLayout();
        }
        Render();
        return 0;
    }

    case WM_DPICHANGED: {
        float newDpi = (float)HIWORD(wParam);
        m_renderer.UpdateDpi(newDpi);
        RECT* prc = (RECT*)lParam;
        SetWindowPos(m_hwnd, nullptr, prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RecalculateLayout();
        Render();
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t droppedPath[MAX_PATH * 2] = { 0 };
        if (DragQueryFileW(hDrop, 0, droppedPath, _countof(droppedPath))) {
            OpenFile(droppedPath);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (isCtrlDown) {
            float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &pt);
            AdjustZoom(factor, pt);
        } else {
            // Check if document is taller than viewport
            D2D1_SIZE_F pSize = m_document.GetPageSize(m_currentPage);
            float dipH = m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi());
            float renderedH = pSize.height * m_zoom;

            if (renderedH > dipH) {
                // Scroll vertically within page
                float scrollStep = (float)delta * 0.6f;
                float oldOffsetY = m_offsetY;
                m_offsetY += scrollStep;

                float minOffsetY = dipH - renderedH - 20.0f;
                float maxOffsetY = 20.0f;

                // If scrolled past bottom, advance to next page
                if (oldOffsetY <= minOffsetY && delta < 0) {
                    NextPage();
                    m_offsetY = 20.0f;
                }
                // If scrolled past top, go to previous page
                else if (oldOffsetY >= maxOffsetY && delta > 0) {
                    PrevPage();
                    D2D1_SIZE_F prevSize = m_document.GetPageSize(m_currentPage);
                    m_offsetY = dipH - (prevSize.height * m_zoom) - 20.0f;
                } else {
                    m_offsetY = std::clamp(m_offsetY, minOffsetY, maxOffsetY);
                }
                m_zoomMode = ZoomMode::Custom;
                Render();
            } else {
                // Page fits vertically: mouse wheel navigates pages directly
                if (delta < 0) {
                    NextPage();
                } else {
                    PrevPage();
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (m_showHelp) {
            m_showHelp = false;
            Render();
            return 0;
        }
        m_isPanning = true;
        m_lastMousePos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(m_hwnd);
        SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (m_isPanning) {
            int curX = GET_X_LPARAM(lParam);
            int curY = GET_Y_LPARAM(lParam);
            float dipScale = 96.0f / m_renderer.GetDpi();
            m_offsetX += (curX - m_lastMousePos.x) * dipScale;
            m_offsetY += (curY - m_lastMousePos.y) * dipScale;
            m_lastMousePos = { curX, curY };
            m_zoomMode = ZoomMode::Custom;
            Render();
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        if (m_isPanning) {
            m_isPanning = false;
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (m_zoomMode == ZoomMode::FitPage) {
            SetZoomMode(ZoomMode::FitWidth);
        } else {
            SetZoomMode(ZoomMode::FitPage);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        switch (wParam) {
        case 'O':
            if (isCtrlDown) {
                PromptOpenFile();
                return 0;
            }
            break;
        case 'P':
            if (isCtrlDown) {
                PromptPrint();
                return 0;
            }
            break;
        case '0':
            if (isCtrlDown) {
                SetZoomMode(ZoomMode::FitPage);
                return 0;
            }
            break;
        case '1':
            if (isCtrlDown) {
                m_zoom = 1.0f;
                m_zoomMode = ZoomMode::Custom;
                RecalculateLayout();
                Render();
                return 0;
            }
            break;
        case '2':
            if (isCtrlDown) {
                SetZoomMode(ZoomMode::FitWidth);
                return 0;
            }
            break;
        case VK_OEM_PLUS:
        case VK_ADD: {
            POINT pt = { (LONG)(m_renderer.GetWidth() / 2), (LONG)(m_renderer.GetHeight() / 2) };
            AdjustZoom(1.15f, pt);
            return 0;
        }
        case VK_OEM_MINUS:
        case VK_SUBTRACT: {
            POINT pt = { (LONG)(m_renderer.GetWidth() / 2), (LONG)(m_renderer.GetHeight() / 2) };
            AdjustZoom(1.0f / 1.15f, pt);
            return 0;
        }
        case VK_NEXT:
            NextPage();
            return 0;
        case VK_PRIOR:
            PrevPage();
            return 0;
        case VK_SPACE:
            if (isShiftDown) PrevPage();
            else NextPage();
            return 0;
        case VK_RIGHT:
            NextPage();
            return 0;
        case VK_LEFT:
            PrevPage();
            return 0;
        case VK_DOWN:
            m_offsetY -= 40.0f;
            m_zoomMode = ZoomMode::Custom;
            Render();
            return 0;
        case VK_UP:
            m_offsetY += 40.0f;
            m_zoomMode = ZoomMode::Custom;
            Render();
            return 0;
        case VK_HOME:
            GoToPage(0);
            return 0;
        case VK_END:
            if (m_document.IsLoaded()) {
                GoToPage(m_document.GetPageCount() - 1);
            }
            return 0;
        case VK_F1:
            m_showHelp = !m_showHelp;
            Render();
            return 0;
        case VK_F11:
            ToggleFullscreen();
            return 0;
        case VK_ESCAPE:
            if (m_showHelp) {
                m_showHelp = false;
                Render();
                return 0;
            }
            if (m_isFullscreen) {
                ToggleFullscreen();
            }
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1; // Direct2D handles entire background, avoid flicker

    case WM_DESTROY:
        m_renderer.Cleanup();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void AppWindow::OpenFile(const std::wstring& path) {
    if (m_document.Open(path)) {
        m_currentPage = 0;
        m_zoomMode = ZoomMode::FitPage;
        RecalculateLayout();
        UpdateTitle();
        Render();
    } else {
        UpdateTitle();
        m_renderer.RenderBlank(L"Could not open file:\n" + path);
    }
}

void AppWindow::PromptOpenFile() {
    if (m_isDialogOpen.exchange(true)) {
        return;
    }

    HWND hwnd = m_hwnd;
    std::thread([this, hwnd]() {
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        ComPtr<IFileOpenDialog> pFileOpen;
        HRESULT hr = CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&pFileOpen)
        );

        if (SUCCEEDED(hr)) {
            COMDLG_FILTERSPEC filterSpecs[] = {
                { L"PDF Documents (*.pdf)", L"*.pdf" },
                { L"All Files (*.*)", L"*.*" }
            };
            pFileOpen->SetFileTypes(ARRAYSIZE(filterSpecs), filterSpecs);
            pFileOpen->SetTitle(L"Open PDF Document");

            hr = pFileOpen->Show(hwnd);
            if (SUCCEEDED(hr)) {
                ComPtr<IShellItem> pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr)) {
                    PWSTR pszFilePath = nullptr;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr) && pszFilePath) {
                        std::wstring* pPath = new std::wstring(pszFilePath);
                        CoTaskMemFree(pszFilePath);
                        PostMessageW(hwnd, WM_APP_OPEN_FILE, 0, (LPARAM)pPath);
                    }
                }
            }
        } else {
            // Fallback to classic GetOpenFileNameW
            wchar_t szFile[MAX_PATH * 2] = { 0 };
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = _countof(szFile);
            ofn.lpstrTitle = L"Open PDF Document";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&ofn)) {
                std::wstring* pPath = new std::wstring(szFile);
                PostMessageW(hwnd, WM_APP_OPEN_FILE, 0, (LPARAM)pPath);
            }
        }

        if (SUCCEEDED(hrCo)) {
            CoUninitialize();
        }

        m_isDialogOpen = false;
    }).detach();
}

void AppWindow::PromptPrint() {
    if (!m_document.IsLoaded() || m_document.GetPageCount() == 0) {
        return;
    }
    if (m_isPrinting.exchange(true)) {
        return;
    }

    if (m_printThread.joinable()) {
        m_printThread.join();
    }

    m_cancelPrint = false;
    HWND hwnd = m_hwnd;
    uint32_t currentPage = m_currentPage;
    uint32_t totalPages = m_document.GetPageCount();
    std::wstring docName = m_document.GetFileName();
    auto doc = m_document.GetDoc();

    m_printThread = std::thread([this, hwnd, currentPage, totalPages, docName, doc]() {
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        PRINTPAGERANGE pageRanges[32] = {};
        pageRanges[0].nFromPage = 1;
        pageRanges[0].nToPage = totalPages;

        PRINTDLGEXW pdex = {};
        pdex.lStructSize = sizeof(PRINTDLGEXW);
        pdex.hwndOwner = hwnd;
        pdex.hDevMode = nullptr;
        pdex.hDevNames = nullptr;
        pdex.hDC = nullptr;
        pdex.Flags = PD_ALLPAGES | PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION;
        pdex.Flags2 = 0;
        pdex.ExclusionFlags = 0;
        pdex.nPageRanges = 1;
        pdex.nMaxPageRanges = ARRAYSIZE(pageRanges);
        pdex.lpPageRanges = pageRanges;
        pdex.nMinPage = 1;
        pdex.nMaxPage = totalPages;
        pdex.nCopies = 1;
        pdex.nStartPage = START_PAGE_GENERAL;

        HRESULT hr = PrintDlgExW(&pdex);
        if (SUCCEEDED(hr) && pdex.dwResultAction == PD_RESULT_PRINT && pdex.hDC && !m_cancelPrint) {
            std::vector<uint32_t> pagesToPrint;

            if (pdex.Flags & PD_CURRENTPAGE) {
                pagesToPrint.push_back(currentPage);
            } else if (pdex.Flags & PD_PAGENUMS) {
                for (DWORD r = 0; r < pdex.nPageRanges; ++r) {
                    DWORD start = pdex.lpPageRanges[r].nFromPage;
                    DWORD end = pdex.lpPageRanges[r].nToPage;
                    if (start > end) std::swap(start, end);
                    if (start < 1) start = 1;
                    if (end > totalPages) end = totalPages;
                    for (DWORD p = start; p <= end; ++p) {
                        pagesToPrint.push_back(p - 1);
                    }
                }
            } else {
                for (uint32_t p = 0; p < totalPages; ++p) {
                    pagesToPrint.push_back(p);
                }
            }

            if (!pagesToPrint.empty() && !m_cancelPrint) {
                DOCINFOW di = {};
                di.cbSize = sizeof(DOCINFOW);
                di.lpszDocName = docName.c_str();

                if (StartDocW(pdex.hDC, &di) > 0) {
                    DWORD copies = (pdex.nCopies > 0) ? pdex.nCopies : 1;
                    bool aborted = false;

                    for (DWORD c = 0; c < copies && !aborted; ++c) {
                        for (uint32_t pageIndex : pagesToPrint) {
                            if (m_cancelPrint) {
                                aborted = true;
                                break;
                            }
                            if (StartPage(pdex.hDC) > 0) {
                                try {
                                    auto page = doc.GetPage(pageIndex);
                                    if (page) {
                                        auto sz = page.Size();
                                        D2D1_SIZE_F pSize = D2D1::SizeF(sz.Width, sz.Height);
                                        m_renderer.PrintPageToHdc(page, pdex.hDC, pSize);
                                    }
                                } catch (...) {}
                                EndPage(pdex.hDC);
                            }
                        }
                    }

                    if (aborted) {
                        AbortDoc(pdex.hDC);
                    } else {
                        EndDoc(pdex.hDC);
                    }
                }
            }
        }

        if (pdex.hDC) {
            DeleteDC(pdex.hDC);
        }
        if (pdex.hDevMode) {
            GlobalFree(pdex.hDevMode);
        }
        if (pdex.hDevNames) {
            GlobalFree(pdex.hDevNames);
        }

        if (SUCCEEDED(hrCo)) {
            CoUninitialize();
        }

        m_isPrinting = false;
    });
}

void AppWindow::UpdateTitle() {
    if (m_document.IsLoaded() && m_document.GetPageCount() > 0) {
        wchar_t title[512];
        swprintf_s(
            title,
            L"[%u / %u] - %s - LightPDF",
            m_currentPage + 1,
            m_document.GetPageCount(),
            m_document.GetFileName().c_str()
        );
        SetWindowTextW(m_hwnd, title);
    } else {
        SetWindowTextW(m_hwnd, L"LightPDF - Minimalist PDF Viewer");
    }
}

void AppWindow::SetZoomMode(ZoomMode mode) {
    m_zoomMode = mode;
    RecalculateLayout();
    Render();
}

void AppWindow::AdjustZoom(float factor, POINT mousePos) {
    float oldZoom = m_zoom;
    float newZoom = std::clamp(m_zoom * factor, 0.20f, 6.0f);
    if (newZoom == oldZoom) return;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float mouseX = (float)mousePos.x * dipScale;
    float mouseY = (float)mousePos.y * dipScale;

    // Zoom centered on mouse pointer
    m_offsetX = mouseX - (mouseX - m_offsetX) * (newZoom / oldZoom);
    m_offsetY = mouseY - (mouseY - m_offsetY) * (newZoom / oldZoom);
    m_zoom = newZoom;
    m_zoomMode = ZoomMode::Custom;

    Render();
}

void AppWindow::RecalculateLayout() {
    if (!m_document.IsLoaded() || m_document.GetPageCount() == 0) return;

    D2D1_SIZE_F pSize = m_document.GetPageSize(m_currentPage);
    if (pSize.width <= 0.0f || pSize.height <= 0.0f) return;

    float dipW = m_renderer.GetWidth() * (96.0f / m_renderer.GetDpi());
    float dipH = m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi());

    if (m_zoomMode == ZoomMode::FitPage) {
        float margin = 24.0f;
        float scaleX = (dipW - margin * 2.0f) / pSize.width;
        float scaleY = (dipH - margin * 2.0f) / pSize.height;
        m_zoom = std::max(0.10f, std::min(scaleX, scaleY));
        m_offsetX = (dipW - pSize.width * m_zoom) * 0.5f;
        m_offsetY = (dipH - pSize.height * m_zoom) * 0.5f;
    } else if (m_zoomMode == ZoomMode::FitWidth) {
        float margin = 24.0f;
        m_zoom = std::max(0.10f, (dipW - margin * 2.0f) / pSize.width);
        m_offsetX = margin;
        m_offsetY = margin;
    } else {
        // Custom zoom: Center if smaller than viewport
        float renderedW = pSize.width * m_zoom;
        float renderedH = pSize.height * m_zoom;
        if (renderedW < dipW) {
            m_offsetX = (dipW - renderedW) * 0.5f;
        }
        if (renderedH < dipH) {
            m_offsetY = (dipH - renderedH) * 0.5f;
        }
    }
}

void AppWindow::NextPage() {
    if (!m_document.IsLoaded()) return;
    if (m_currentPage + 1 < m_document.GetPageCount()) {
        m_currentPage++;
        if (m_zoomMode != ZoomMode::Custom) {
            RecalculateLayout();
        } else {
            // Keep horizontal offset, reset vertical to top
            m_offsetY = 24.0f;
        }
        UpdateTitle();
        Render();
    }
}

void AppWindow::PrevPage() {
    if (!m_document.IsLoaded()) return;
    if (m_currentPage > 0) {
        m_currentPage--;
        if (m_zoomMode != ZoomMode::Custom) {
            RecalculateLayout();
        } else {
            m_offsetY = 24.0f;
        }
        UpdateTitle();
        Render();
    }
}

void AppWindow::GoToPage(uint32_t pageIndex) {
    if (!m_document.IsLoaded() || pageIndex >= m_document.GetPageCount()) return;
    m_currentPage = pageIndex;
    if (m_zoomMode != ZoomMode::Custom) {
        RecalculateLayout();
    } else {
        m_offsetY = 24.0f;
    }
    UpdateTitle();
    Render();
}

void AppWindow::ToggleFullscreen() {
    DWORD dwStyle = GetWindowLongW(m_hwnd, GWL_STYLE);
    if (!m_isFullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(m_hwnd, &m_prevPlacement) &&
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(m_hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(
                m_hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED
            );
            m_isFullscreen = true;
        }
    } else {
        SetWindowLongW(m_hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_prevPlacement);
        SetWindowPos(
            m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );
        m_isFullscreen = false;
    }
    RecalculateLayout();
    Render();
}

void AppWindow::Render() {
    if (m_document.IsLoaded() && m_currentPage < m_document.GetPageCount()) {
        auto page = m_document.GetPage(m_currentPage);
        auto pSize = m_document.GetPageSize(m_currentPage);
        std::wstring modeStr = L"";
        if (m_zoomMode == ZoomMode::FitPage) modeStr = L"Fit Page";
        else if (m_zoomMode == ZoomMode::FitWidth) modeStr = L"Fit Width";

        m_renderer.RenderPage(
            page,
            m_zoom,
            m_offsetX,
            m_offsetY,
            pSize,
            m_currentPage,
            m_document.GetPageCount(),
            modeStr,
            m_showHelp
        );
    } else {
        m_renderer.RenderBlank(L"", m_showHelp);
    }
}

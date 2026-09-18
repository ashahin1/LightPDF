#include "app_window.hpp"
#include "pdf_parser.hpp"
#include <windowsx.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <vector>

const wchar_t* WINDOW_CLASS_NAME = L"LightPDF_WindowClass";

AppWindow::AppWindow() = default;

AppWindow::~AppWindow() {
    m_cancelPrint = true;
    if (m_printThread.joinable()) {
        m_printThread.join();
    }
    if (m_dialogThread.joinable()) {
        m_dialogThread.join();
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool AppWindow::Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile) {
    m_hInstance = hInstance;

    m_cursorArrow = LoadCursor(nullptr, IDC_ARROW);
    m_cursorHand = LoadCursor(nullptr, IDC_HAND);
    m_cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);
    m_cursorSizeAll = LoadCursor(nullptr, IDC_SIZEALL);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = AppWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = m_cursorArrow;
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    RegisterClassExW(&wc);

    // Create window with standard modern window styling
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_ACCEPTFILES,
        WINDOW_CLASS_NAME,
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
        OpenTab(initialFile);
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
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pCds = (PCOPYDATASTRUCT)lParam;
        if (pCds && pCds->dwData == 1 && pCds->lpData) {
            const wchar_t* pFilePath = (const wchar_t*)pCds->lpData;
            if (pFilePath && pFilePath[0] != L'\0') {
                OpenTab(pFilePath);
            }
        }
        return TRUE;
    }

    case WM_APP_OPEN_FILE: {
        std::unique_ptr<std::wstring> pPath((std::wstring*)lParam);
        if (pPath && !pPath->empty()) {
            OpenTab(*pPath);
        }
        return 0;
    }

    case WM_APP_SEARCH_UPDATE: {
        InvalidateSearchHighlights();
        int activeIdx = m_searchEngine.GetActiveMatchIndex();
        if (activeIdx >= 0 && activeIdx != m_lastJumpedMatch) {
            m_lastJumpedMatch = activeIdx;
            JumpToActiveMatch();
        }
        Render();
        return 0;
    }

    case WM_APP_PAGE_SIZES_READY: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            pTab->lastOffsetsZoom = -1.0f; // Invalidate cached continuous scroll offsets
            if (pTab->zoomMode != ZoomMode::Custom) {
                RecalculateLayout();
            }
            Render();
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == 1) {
            float targetAlpha = 0.0f;
            uint64_t now = GetTickCount64();
            if (m_isDraggingScrollbar || m_isScrollbarHovered || (now - m_lastScrollbarActiveTime < 1200)) {
                targetAlpha = 1.0f;
            }

            if (m_scrollbarAlpha != targetAlpha) {
                if (targetAlpha > m_scrollbarAlpha) {
                    m_scrollbarAlpha = std::min(targetAlpha, m_scrollbarAlpha + 0.15f);
                } else {
                    m_scrollbarAlpha = std::max(targetAlpha, m_scrollbarAlpha - 0.08f);
                }
                Render();
            } else if (targetAlpha == 0.0f) {
                KillTimer(m_hwnd, 1);
            }
        } else if (wParam == 2) {
            // Search debounce timer: User paused typing, trigger search now
            KillTimer(m_hwnd, 2);
            m_searchDebouncePending = false;
            TriggerSearch();
            Render();
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
        auto* pTab = GetActiveTab();
        if (pTab && pTab->zoomMode != ZoomMode::Custom) {
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
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < fileCount; ++i) {
            wchar_t droppedPath[MAX_PATH * 2] = { 0 };
            if (DragQueryFileW(hDrop, i, droppedPath, _countof(droppedPath))) {
                OpenTab(droppedPath);
            }
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (m_showProperties) return 0;
        if (m_isDraggingScrollbar) return 0;
        auto* pTab = GetActiveTab();
        if (!pTab || !pTab->document.IsLoaded()) return 0;

        ShowScrollbar();

        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (isCtrlDown) {
            float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &pt);
            AdjustZoom(factor, pt);
        } else if (pTab->continuousScroll) {
            ScrollContinuous((float)delta * 0.6f);
        } else {
            D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
            float topOffset = GetTopOffset();
            float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - topOffset;
            float renderedH = pSize.height * pTab->zoom;

            if (renderedH > dipH) {
                float scrollStep = (float)delta * 0.6f;
                float oldOffsetY = pTab->offsetY;
                pTab->offsetY += scrollStep;

                float minOffsetY = dipH - renderedH - 20.0f;
                float maxOffsetY = 20.0f;

                if (oldOffsetY <= minOffsetY && delta < 0) {
                    if (pTab->currentPage + 1 < pTab->document.GetPageCount()) {
                        NextPage(false);
                        pTab->offsetY = 20.0f;
                    } else {
                        pTab->offsetY = minOffsetY;
                    }
                } else if (oldOffsetY >= maxOffsetY && delta > 0) {
                    if (pTab->currentPage > 0) {
                        PrevPage(false);
                        D2D1_SIZE_F prevSize = pTab->document.GetPageSize(pTab->currentPage);
                        pTab->offsetY = dipH - (prevSize.height * pTab->zoom) - 20.0f;
                    } else {
                        pTab->offsetY = maxOffsetY;
                    }
                } else {
                    pTab->offsetY = std::clamp(pTab->offsetY, minOffsetY, maxOffsetY);
                }
                Render();
            } else {
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
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipX = (float)pt.x * dipScale;
        float dipY = (float)pt.y * dipScale;
        float topOffset = GetTopOffset();

        // 0. If Document Properties is open, handle clicks
        if (m_showProperties) {
            int propHit = m_renderer.HitTestDocumentProperties(pt);
            if (propHit == 1 || propHit == 3 || propHit == -1) {
                CloseDocumentProperties();
                return 0;
            } else if (propHit == 2) {
                CopyPropertiesToClipboard();
                return 0;
            }
            return 0;
        }

        // 1. If Go to Page overlay is open, click outside closes it
        if (m_showGoToPage) {
            float dipW = (float)m_renderer.GetWidth() * dipScale;
            float dipH = (float)m_renderer.GetHeight() * dipScale;
            float cardW = 320.0f;
            float cardH = 150.0f;
            float cardX = (dipW - cardW) * 0.5f;
            float cardY = (dipH - cardH) * 0.5f;
            if (dipX < cardX || dipX > cardX + cardW || dipY < cardY || dipY > cardY + cardH) {
                m_showGoToPage = false;
                Render();
            }
            return 0;
        }

        // 2. Tab Bar clicks
        if (m_tabs.size() > 1 && dipY < topOffset) {
            bool outClose = false;
            bool outAdd = false;
            int hit = HitTestTab(pt, outClose, outAdd);

            if (msg == WM_LBUTTONDOWN) {
                if (outAdd) {
                    PromptOpenFile();
                } else if (outClose && hit >= 0) {
                    CloseTab((size_t)hit);
                } else if (hit >= 0) {
                    SelectTab((size_t)hit);
                }
            } else if (msg == WM_MBUTTONDOWN) {
                if (hit >= 0) {
                    CloseTab((size_t)hit);
                }
            }
            return 0;
        }

        // 2b. Search Bar interaction
        if (m_showSearch) {
            int searchHit = HitTestSearchBar(pt);
            if (msg == WM_LBUTTONDOWN && searchHit > 0) {
                if (searchHit == 1) { // Prev
                    if (m_searchDebouncePending || m_searchEngine.GetCurrentQuery() != m_searchQuery) {
                        TriggerSearch();
                    } else if (m_searchEngine.PrevMatch()) {
                        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                        JumpToActiveMatch();
                        Render();
                    }
                } else if (searchHit == 2) { // Next
                    if (m_searchDebouncePending || m_searchEngine.GetCurrentQuery() != m_searchQuery) {
                        TriggerSearch();
                    } else if (m_searchEngine.NextMatch()) {
                        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                        JumpToActiveMatch();
                        Render();
                    }
                } else if (searchHit == 3) { // Match Case
                    m_searchMatchCase = !m_searchMatchCase;
                    TriggerSearch();
                    Render();
                } else if (searchHit == 4) { // OCR
                    m_searchOcrEnabled = !m_searchOcrEnabled;
                    TriggerSearch();
                    Render();
                } else if (searchHit == 5) { // Close
                    CloseSearch();
                    Render();
                }
                return 0;
            } else if (searchHit == 0) {
                // Clicked inside search bar input area
                return 0;
            }
        }

        // 3. Help Overlay dismissal
        if (m_showHelp) {
            m_showHelp = false;
            Render();
            return 0;
        }

        // 4. Scrollbar interaction (Left button only)
        if (msg == WM_LBUTTONDOWN) {
            bool outThumb = false;
            ScrollbarRenderInfo sInfo;
            if (HitTestScrollbar(pt, outThumb, &sInfo)) {
                if (outThumb) {
                    m_scrollbarDragThumbOffsetY = dipY - sInfo.thumbY;
                    m_scrollbarDragThumbY = sInfo.thumbY;
                } else {
                    m_scrollbarDragThumbOffsetY = sInfo.thumbH * 0.5f;
                    float usableH = sInfo.trackH - sInfo.thumbH;
                    m_scrollbarDragThumbY = std::clamp(dipY - m_scrollbarDragThumbOffsetY, sInfo.trackY, sInfo.trackY + usableH);
                }
                m_isDraggingScrollbar = true;
                SetCapture(m_hwnd);
                if (!outThumb) {
                    HandleScrollbarDrag(dipY);
                }
                ShowScrollbar();
                Render();
                return 0;
            }
        }

        // 5. HUD Pill interaction (click opens Go to Page)
        if (msg == WM_LBUTTONDOWN && HitTestHud(pt)) {
            m_showGoToPage = true;
            m_goToPageBuffer.clear();
            Render();
            return 0;
        }

        // 6. Canvas Panning
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            m_isPanning = true;
            m_lastMousePos = pt;
            SetCapture(m_hwnd);
            SetCursor(m_cursorSizeAll);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipX = (float)pt.x * dipScale;
        float dipY = (float)pt.y * dipScale;
        float dipW = (float)m_renderer.GetWidth() * dipScale;
        float topOffset = GetTopOffset();

        // 0. Document Properties hover detection
        if (m_showProperties) {
            int hit = m_renderer.HitTestDocumentProperties(pt);
            int newBtn = (hit > 0) ? hit : 0;
            if (newBtn != m_propsHoveredBtn) {
                m_propsHoveredBtn = newBtn;
                Render();
            }
            if (hit > 0) {
                SetCursor(m_cursorHand);
            } else {
                SetCursor(m_cursorArrow);
            }
            return 0;
        }

        // 1. Handle Scrollbar Dragging
        if (m_isDraggingScrollbar) {
            ShowScrollbar();
            HandleScrollbarDrag(dipY);
            return 0;
        }

        // 2. Scrollbar Hover detection (right 24 DIPs)
        bool nearRight = (dipX >= dipW - 24.0f && dipY >= topOffset);
        if (nearRight != m_isScrollbarHovered) {
            m_isScrollbarHovered = nearRight;
            if (m_isScrollbarHovered) {
                ShowScrollbar();
            }
            Render();
        }

        // 2b. Search Bar Hover detection
        if (m_showSearch) {
            int hit = HitTestSearchBar(pt);
            int newBtn = (hit > 0) ? hit : 0;
            if (newBtn != m_searchHoveredBtn) {
                m_searchHoveredBtn = newBtn;
                Render();
            }
            if (hit > 0) {
                SetCursor(m_cursorHand);
            } else if (hit == 0) {
                SetCursor(m_cursorIBeam);
            }
        } else if (m_searchHoveredBtn != 0) {
            m_searchHoveredBtn = 0;
        }

        // 3. Tab Bar hover
        if (m_tabs.size() > 1 && dipY < topOffset) {
            bool outClose = false;
            bool outAdd = false;
            int hit = HitTestTab(pt, outClose, outAdd);

            bool changed = (hit != m_hoveredTab) || (outClose != m_hoveredClose) || (outAdd != m_hoveredAdd);
            m_hoveredTab = hit;
            m_hoveredClose = outClose;
            m_hoveredAdd = outAdd;

            if (changed) {
                Render();
            }
            SetCursor(m_cursorArrow);
        } else {
            if (m_hoveredTab != -1 || m_hoveredClose || m_hoveredAdd) {
                m_hoveredTab = -1;
                m_hoveredClose = false;
                m_hoveredAdd = false;
                Render();
            }
        }

        // 4. Canvas Panning
        if (m_isPanning) {
            auto* pTab = GetActiveTab();
            if (pTab) {
                float dx = (pt.x - m_lastMousePos.x) * dipScale;
                float dy = (pt.y - m_lastMousePos.y) * dipScale;
                m_lastMousePos = pt;

                if (pTab->continuousScroll) {
                    pTab->offsetX += dx;
                    ScrollContinuous(dy);
                } else {
                    pTab->offsetX += dx;
                    pTab->offsetY += dy;
                    pTab->zoomMode = ZoomMode::Custom;
                    Render();
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        if (m_isDraggingScrollbar) {
            m_isDraggingScrollbar = false;
            ReleaseCapture();
            ShowScrollbar();
            Render();
            return 0;
        }
        if (m_isPanning) {
            m_isPanning = false;
            ReleaseCapture();
            SetCursor(m_cursorArrow);
        }
        return 0;
    }

    case WM_CAPTURECHANGED: {
        if (m_isDraggingScrollbar) {
            m_isDraggingScrollbar = false;
            ShowScrollbar();
            Render();
        }
        if (m_isPanning) {
            m_isPanning = false;
            SetCursor(m_cursorArrow);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (m_showProperties) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float topOffset = GetTopOffset();
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipY = (float)pt.y * dipScale;
        if (m_tabs.size() > 1 && dipY < topOffset) {
            return 0;
        }

        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            if (pTab->continuousScroll) {
                float mouseY = dipY - topOffset;
                float clickedDocY = pTab->scrollY + mouseY;

                // Find which page was double-clicked and offset within it
                uint32_t count = pTab->document.GetPageCount();
                uint32_t clickedPage = 0;
                float curY = 24.0f;
                float gap = 12.0f;
                float offsetInPage = 0.0f;

                for (uint32_t i = 0; i < count; ++i) {
                    float pageH = pTab->document.GetPageSize(i).height * pTab->zoom;
                    if (clickedDocY < curY + pageH + gap || i == count - 1) {
                        clickedPage = i;
                        offsetInPage = std::clamp(clickedDocY - curY, 0.0f, pageH);
                        break;
                    }
                    curY += pageH + gap;
                }

                float ptOffsetY = (pTab->zoom > 0.0f) ? (offsetInPage / pTab->zoom) : 0.0f;

                // Toggle zoom mode between FitWidth and FitPage
                ZoomMode newMode = (pTab->zoomMode == ZoomMode::FitPage) ? ZoomMode::FitWidth : ZoomMode::FitPage;
                pTab->zoomMode = newMode;
                pTab->currentPage = clickedPage;

                // Calculate new zoom
                float dipW = m_renderer.GetWidth() * dipScale;
                float dipH = (m_renderer.GetHeight() * dipScale) - topOffset;
                float margin = 24.0f;

                if (newMode == ZoomMode::FitWidth) {
                    float maxW = 0.0f;
                    for (uint32_t i = 0; i < count; ++i) {
                        float w = pTab->document.GetPageSize(i).width;
                        if (w > maxW) maxW = w;
                    }
                    if (maxW > 0.0f) {
                        pTab->zoom = std::max(0.10f, (dipW - margin * 2.0f) / maxW);
                    }
                } else { // FitPage
                    D2D1_SIZE_F pSize = pTab->document.GetPageSize(clickedPage);
                    if (pSize.height > 0.0f && pSize.width > 0.0f) {
                        float scaleX = (dipW - margin * 2.0f) / pSize.width;
                        float scaleY = (dipH - margin * 2.0f) / pSize.height;
                        pTab->zoom = std::max(0.10f, std::min(scaleX, scaleY));
                    }
                }
                pTab->offsetX = 0.0f;

                // Anchor clicked point directly under the mouse cursor at new zoom
                float newPageTop = GetPageYOffset(pTab, clickedPage);
                float totalH = GetTotalDocumentHeight(pTab);
                float maxScroll = std::max(0.0f, totalH - dipH);
                float newScrollY = newPageTop + (ptOffsetY * pTab->zoom) - mouseY;
                pTab->scrollY = std::clamp(newScrollY, 0.0f, maxScroll);
                pTab->currentPage = clickedPage;

                UpdateTitle();
                ShowScrollbar();
                Render();
                return 0;
            } else {
                if (pTab->zoomMode == ZoomMode::FitPage) {
                    SetZoomMode(ZoomMode::FitWidth);
                } else {
                    SetZoomMode(ZoomMode::FitPage);
                }
            }
        } else {
            PromptOpenFile();
        }
        return 0;
    }

    case WM_SYSKEYDOWN: {
        if (wParam >= '1' && wParam <= '9') {
            size_t targetIndex = (size_t)(wParam - '1');
            if (targetIndex < m_tabs.size()) {
                SelectTab(targetIndex);
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN: {
        if (m_isDraggingScrollbar) return 0;
        bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (m_showProperties) {
            if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
                CloseDocumentProperties();
                return 0;
            } else if (wParam == 'D' && isCtrlDown) {
                CloseDocumentProperties();
                return 0;
            } else if (wParam == 'C' && isCtrlDown) {
                CopyPropertiesToClipboard();
                return 0;
            }
            return 0;
        }

        if (m_showGoToPage) {
            if (wParam >= '0' && wParam <= '9') {
                if (m_goToPageBuffer.size() < 6) {
                    m_goToPageBuffer.push_back((wchar_t)wParam);
                    Render();
                }
                return 0;
            } else if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
                if (m_goToPageBuffer.size() < 6) {
                    m_goToPageBuffer.push_back(L'0' + (wchar_t)(wParam - VK_NUMPAD0));
                    Render();
                }
                return 0;
            } else if (wParam == VK_BACK) {
                if (!m_goToPageBuffer.empty()) {
                    m_goToPageBuffer.pop_back();
                    Render();
                }
                return 0;
            } else if (wParam == VK_RETURN) {
                if (!m_goToPageBuffer.empty()) {
                    try {
                        long p = std::stol(m_goToPageBuffer);
                        auto* pTab = GetActiveTab();
                        if (pTab && pTab->document.IsLoaded()) {
                            uint32_t total = pTab->document.GetPageCount();
                            if (p >= 1 && (uint32_t)p <= total) {
                                GoToPage((uint32_t)(p - 1));
                            }
                        }
                    } catch (...) {}
                }
                m_showGoToPage = false;
                m_goToPageBuffer.clear();
                Render();
                return 0;
            } else if (wParam == VK_ESCAPE) {
                m_showGoToPage = false;
                m_goToPageBuffer.clear();
                Render();
                return 0;
            }
            return 0;
        }

        switch (wParam) {
        case 'F':
            if (isCtrlDown) {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
                    m_showSearch = true;
                    m_showGoToPage = false;
                    m_showHelp = false;
                    if (!m_searchQuery.empty()) {
                        TriggerSearch();
                    }
                    Render();
                }
                return 0;
            }
            break;
        case 'V':
            if (isCtrlDown && m_showSearch) {
                if (OpenClipboard(m_hwnd)) {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData) {
                        const wchar_t* pText = (const wchar_t*)GlobalLock(hData);
                        if (pText) {
                            for (const wchar_t* p = pText; *p; ++p) {
                                if (*p >= 32 && *p != 127) {
                                    m_searchQuery.push_back(*p);
                                }
                            }
                            GlobalUnlock(hData);
                            ScheduleSearchDebounce();
                        }
                    }
                    CloseClipboard();
                }
                return 0;
            }
            break;
        case 'D':
            if (isCtrlDown) {
                if (m_showProperties) CloseDocumentProperties();
                else ShowDocumentProperties();
                return 0;
            }
            break;
        case 'G':
            if (isCtrlDown) {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
                    m_showGoToPage = true;
                    m_goToPageBuffer.clear();
                    Render();
                }
                return 0;
            }
            break;
        case VK_TAB:
            if (isCtrlDown) {
                if (isShiftDown) PrevTab();
                else NextTab();
                return 0;
            }
            break;
        case 'T':
            if (isCtrlDown) {
                PromptOpenFile();
                return 0;
            }
            break;
        case 'W':
            if (isCtrlDown) {
                if (!m_tabs.empty()) {
                    CloseTab(m_activeTab);
                }
                return 0;
            }
            break;
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
                auto* pTab = GetActiveTab();
                if (pTab) {
                    pTab->zoom = 1.0f;
                    pTab->zoomMode = ZoomMode::Custom;
                    RecalculateLayout();
                    Render();
                }
                return 0;
            }
            break;
        case '2':
            if (isCtrlDown) {
                SetZoomMode(ZoomMode::FitWidth);
                return 0;
            }
            break;
        case '3':
            if (isCtrlDown) {
                ToggleContinuousScroll();
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
        case VK_NEXT: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->continuousScroll) {
                float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
                ScrollContinuous(-(dipH * 0.85f));
            } else {
                NextPage();
            }
            return 0;
        }
        case VK_PRIOR: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->continuousScroll) {
                float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
                ScrollContinuous(dipH * 0.85f);
            } else {
                PrevPage();
            }
            return 0;
        }
        case VK_SPACE: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->continuousScroll) {
                float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
                if (isShiftDown) ScrollContinuous(dipH * 0.85f);
                else ScrollContinuous(-(dipH * 0.85f));
            } else {
                if (isShiftDown) PrevPage();
                else NextPage();
            }
            return 0;
        }
        case VK_RIGHT:
            NextPage();
            return 0;
        case VK_LEFT:
            PrevPage();
            return 0;
        case VK_DOWN: {
            auto* pTab = GetActiveTab();
            if (pTab) {
                if (pTab->continuousScroll) {
                    ScrollContinuous(-40.0f);
                } else {
                    pTab->offsetY -= 40.0f;
                    pTab->zoomMode = ZoomMode::Custom;
                    ShowScrollbar();
                    Render();
                }
            }
            return 0;
        }
        case VK_UP: {
            auto* pTab = GetActiveTab();
            if (pTab) {
                if (pTab->continuousScroll) {
                    ScrollContinuous(40.0f);
                } else {
                    pTab->offsetY += 40.0f;
                    pTab->zoomMode = ZoomMode::Custom;
                    ShowScrollbar();
                    Render();
                }
            }
            return 0;
        }
        case VK_HOME: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->continuousScroll) {
                pTab->scrollY = 0.0f;
                pTab->currentPage = 0;
                UpdateTitle();
                ShowScrollbar();
                Render();
            } else {
                GoToPage(0);
            }
            return 0;
        }
        case VK_END: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->document.IsLoaded()) {
                if (pTab->continuousScroll) {
                    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
                    float totalH = GetTotalDocumentHeight(pTab);
                    pTab->scrollY = std::max(0.0f, totalH - dipH);
                    pTab->currentPage = pTab->document.GetPageCount() - 1;
                    UpdateTitle();
                    ShowScrollbar();
                    Render();
                } else {
                    GoToPage(pTab->document.GetPageCount() - 1);
                }
            }
            return 0;
        }
        case VK_RETURN:
            if (m_showSearch) {
                if (m_searchDebouncePending || m_searchEngine.GetCurrentQuery() != m_searchQuery) {
                    TriggerSearch();
                    Render();
                    return 0;
                }
                if (isShiftDown) {
                    if (m_searchEngine.PrevMatch()) {
                        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                        JumpToActiveMatch();
                        Render();
                    }
                } else {
                    if (m_searchEngine.NextMatch()) {
                        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                        JumpToActiveMatch();
                        Render();
                    }
                }
                return 0;
            }
            break;
        case VK_F3: {
            auto* pTab = GetActiveTab();
            if (pTab && pTab->document.IsLoaded()) {
                if (!m_showSearch) {
                    m_showSearch = true;
                    if (!m_searchQuery.empty()) {
                        TriggerSearch();
                    }
                } else {
                    if (m_searchDebouncePending || m_searchEngine.GetCurrentQuery() != m_searchQuery) {
                        TriggerSearch();
                    } else if (isShiftDown) {
                        if (m_searchEngine.PrevMatch()) {
                            m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                            JumpToActiveMatch();
                            Render();
                        }
                    } else {
                        if (m_searchEngine.NextMatch()) {
                            m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
                            JumpToActiveMatch();
                            Render();
                        }
                    }
                }
                Render();
            }
            return 0;
        }
        case VK_F1:
            m_showHelp = !m_showHelp;
            Render();
            return 0;
        case VK_F11:
            ToggleFullscreen();
            return 0;
        case VK_ESCAPE:
            if (m_showProperties) {
                CloseDocumentProperties();
                return 0;
            }
            if (m_showHelp) {
                m_showHelp = false;
                Render();
                return 0;
            }
            if (m_showGoToPage) {
                m_showGoToPage = false;
                m_goToPageBuffer.clear();
                Render();
                return 0;
            }
            if (m_showSearch) {
                CloseSearch();
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

    case WM_CHAR:
        if (m_showProperties) {
            return 0;
        }
        if (m_showGoToPage) {
            return 0;
        }
        if (m_showSearch) {
            if (wParam == VK_BACK) {
                if (!m_searchQuery.empty()) {
                    m_searchQuery.pop_back();
                    ScheduleSearchDebounce();
                }
                return 0;
            } else if (wParam >= 32 && wParam != 127) {
                m_searchQuery.push_back((wchar_t)wParam);
                ScheduleSearchDebounce();
                return 0;
            } else if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
                return 0;
            }
        }
        break;

    case WM_ERASEBKGND:
        return 1; // Direct2D handles entire background, avoid flicker

    case WM_DESTROY:
        m_renderer.Cleanup();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void AppWindow::OpenTab(const std::wstring& path) {
    if (path.empty()) return;

    // Resolve to absolute canonical path for comparison
    wchar_t fullPath[MAX_PATH * 2] = { 0 };
    DWORD len = GetFullPathNameW(path.c_str(), _countof(fullPath), fullPath, nullptr);
    std::wstring resolvedPath = (len > 0) ? fullPath : path;

    // Check if this file is already open in an existing tab
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].document.IsLoaded() &&
            _wcsicmp(m_tabs[i].document.GetFilePath().c_str(), resolvedPath.c_str()) == 0) {
            SelectTab(i);
            return;
        }
    }

    // If we have a single tab that failed to load or is blank, reuse it
    if (m_tabs.size() == 1 && !m_tabs[0].document.IsLoaded()) {
        if (m_tabs[0].document.Open(resolvedPath, m_hwnd)) {
            m_tabs[0].currentPage = 0;
            m_tabs[0].zoomMode = ZoomMode::FitPage;
            m_tabs[0].continuousScroll = false;
            m_tabs[0].scrollY = 0.0f;
            m_tabs[0].textCache = std::make_shared<PageTextCache>();
            m_tabs[0].textCache->pages.resize(m_tabs[0].document.GetPageCount());
            m_renderer.InvalidatePageCache();
            m_activeTab = 0;
            if (m_showSearch && !m_searchQuery.empty()) {
                TriggerSearch();
            }
            RecalculateLayout();
            UpdateTitle();
            Render();
        }
        return;
    }

    DocumentTab newTab;
    if (newTab.document.Open(resolvedPath, m_hwnd)) {
        newTab.currentPage = 0;
        newTab.zoomMode = ZoomMode::FitPage;
        newTab.continuousScroll = false;
        newTab.scrollY = 0.0f;
        newTab.textCache = std::make_shared<PageTextCache>();
        newTab.textCache->pages.resize(newTab.document.GetPageCount());
        m_renderer.InvalidatePageCache();
        m_tabs.push_back(std::move(newTab));
        m_activeTab = m_tabs.size() - 1;
        if (m_showSearch && !m_searchQuery.empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();
        Render();
    }
}

void AppWindow::CloseTab(size_t index) {
    if (index >= m_tabs.size()) return;

    m_renderer.InvalidatePageCache();
    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_activeTab = 0;
        CloseSearch();
        CloseDocumentProperties();
        UpdateTitle();
        Render();
        return;
    }

    if (m_activeTab >= m_tabs.size()) {
        m_activeTab = m_tabs.size() - 1;
    } else if (m_activeTab > index) {
        m_activeTab--;
    }

    if (m_showProperties) {
        ShowDocumentProperties();
    }
    if (m_showSearch && !m_searchQuery.empty()) {
        TriggerSearch();
    }
    RecalculateLayout();
    UpdateTitle();
    Render();
}

void AppWindow::SelectTab(size_t index) {
    if (index >= m_tabs.size() || index == m_activeTab) return;
    m_renderer.InvalidatePageCache();
    m_activeTab = index;
    if (m_showProperties) {
        ShowDocumentProperties();
    }
    if (m_showSearch && !m_searchQuery.empty()) {
        TriggerSearch();
    }
    RecalculateLayout();
    UpdateTitle();
    Render();
}

void AppWindow::NextTab() {
    if (m_tabs.size() <= 1) return;
    m_renderer.InvalidatePageCache();
    m_activeTab = (m_activeTab + 1) % m_tabs.size();
    if (m_showProperties) {
        ShowDocumentProperties();
    }
    if (m_showSearch && !m_searchQuery.empty()) {
        TriggerSearch();
    }
    RecalculateLayout();
    UpdateTitle();
    Render();
}

void AppWindow::PrevTab() {
    if (m_tabs.size() <= 1) return;
    m_renderer.InvalidatePageCache();
    m_activeTab = (m_activeTab == 0) ? (m_tabs.size() - 1) : (m_activeTab - 1);
    if (m_showProperties) {
        ShowDocumentProperties();
    }
    if (m_showSearch && !m_searchQuery.empty()) {
        TriggerSearch();
    }
    RecalculateLayout();
    UpdateTitle();
    Render();
}

int AppWindow::HitTestTab(POINT pt, bool& outClose, bool& outAdd) const {
    outClose = false;
    outAdd = false;
    if (m_tabs.size() <= 1) return -1;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;

    if (dipY < 0.0f || dipY > 34.0f) return -1;

    float dipWidth = (float)m_renderer.GetWidth() * dipScale;
    float availW = dipWidth - 44.0f;
    float tabW = std::clamp(availW / (float)m_tabs.size(), 100.0f, 220.0f);

    // Check '+' Add Tab button
    float addX = (float)m_tabs.size() * tabW + 6.0f;
    if (dipX >= addX && dipX <= addX + 24.0f && dipY >= 5.0f && dipY <= 29.0f) {
        outAdd = true;
        return -1;
    }

    // Check tabs
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        float tx = (float)i * tabW;
        if (dipX >= tx && dipX < tx + tabW) {
            // Check close button
            if (dipX >= tx + tabW - 24.0f && dipX <= tx + tabW - 8.0f && dipY >= 8.0f && dipY <= 26.0f) {
                outClose = true;
            }
            return (int)i;
        }
    }

    return -1;
}

void AppWindow::UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const {
    if (infos.size() != m_tabs.size()) {
        infos.resize(m_tabs.size());
    }

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const std::wstring& title = m_tabs[i].document.IsLoaded() ? m_tabs[i].document.GetFileName() : L"Empty";
        if (infos[i].title != title) {
            infos[i].title = title;
        }
        infos[i].isActive = (i == m_activeTab);
        infos[i].isHovered = ((int)i == m_hoveredTab);
        infos[i].isCloseHovered = ((int)i == m_hoveredTab && m_hoveredClose);
    }
}

std::vector<TabRenderInfo> AppWindow::GetTabRenderInfos() const {
    std::vector<TabRenderInfo> infos;
    UpdateTabRenderInfos(infos);
    return infos;
}

void AppWindow::PromptOpenFile() {
    if (m_isDialogOpen.exchange(true)) {
        return;
    }

    if (m_dialogThread.joinable()) {
        m_dialogThread.join();
    }

    HWND hwnd = m_hwnd;
    m_dialogThread = std::thread([this, hwnd]() {
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
    });
}

void AppWindow::PromptPrint() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
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
    uint32_t currentPage = pTab->currentPage;
    uint32_t totalPages = pTab->document.GetPageCount();
    std::wstring docName = pTab->document.GetFileName();
    auto doc = pTab->document.GetDoc();

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
    auto* pTab = GetActiveTab();
    if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
        wchar_t title[512];
        const wchar_t* modeSuffix = pTab->continuousScroll ? L" (Continuous)" : L"";
        if (m_tabs.size() > 1) {
            swprintf_s(
                title,
                L"[Tab %zu/%zu] [%u / %u] - %s - LightPDF%s",
                m_activeTab + 1,
                m_tabs.size(),
                pTab->currentPage + 1,
                pTab->document.GetPageCount(),
                pTab->document.GetFileName().c_str(),
                modeSuffix
            );
        } else {
            swprintf_s(
                title,
                L"[%u / %u] - %s - LightPDF%s",
                pTab->currentPage + 1,
                pTab->document.GetPageCount(),
                pTab->document.GetFileName().c_str(),
                modeSuffix
            );
        }
        SetWindowTextW(m_hwnd, title);
    } else {
        SetWindowTextW(m_hwnd, L"LightPDF - Minimalist PDF Viewer");
    }
}

void AppWindow::SetZoomMode(ZoomMode mode) {
    auto* pTab = GetActiveTab();
    if (!pTab) return;
    pTab->zoomMode = mode;
    RecalculateLayout();
    Render();
}

void AppWindow::AdjustZoom(float factor, POINT mousePos) {
    auto* pTab = GetActiveTab();
    if (!pTab) return;

    float oldZoom = pTab->zoom;
    float newZoom = std::clamp(pTab->zoom * factor, 0.20f, 6.0f);
    if (newZoom == oldZoom) return;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float mouseX = (float)mousePos.x * dipScale;
    float mouseY = (float)mousePos.y * dipScale - GetTopOffset();

    if (pTab->continuousScroll) {
        float docY = pTab->scrollY + mouseY;
        pTab->scrollY = std::max(0.0f, (docY * (newZoom / oldZoom)) - mouseY);
        pTab->zoom = newZoom;
        pTab->zoomMode = ZoomMode::Custom;

        float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        pTab->scrollY = std::min(pTab->scrollY, maxScroll);
        pTab->currentPage = GetPageAtScrollOffset(pTab);
        UpdateTitle();
    } else {
        pTab->offsetX = mouseX - (mouseX - pTab->offsetX) * (newZoom / oldZoom);
        pTab->offsetY = mouseY - (mouseY - pTab->offsetY) * (newZoom / oldZoom);
        pTab->zoom = newZoom;
        pTab->zoomMode = ZoomMode::Custom;
    }

    Render();
}

void AppWindow::RecalculateLayout() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return;

    float dipW = m_renderer.GetWidth() * (96.0f / m_renderer.GetDpi());
    float topOffset = GetTopOffset();
    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - topOffset;

    if (pTab->continuousScroll) {
        float margin = 24.0f;
        float oldZoom = pTab->zoom;
        uint32_t count = pTab->document.GetPageCount();
        uint32_t anchorPage = pTab->currentPage;
        if (anchorPage >= count) anchorPage = count - 1;

        float oldPageTop = GetPageYOffset(pTab, anchorPage);

        if (pTab->zoomMode == ZoomMode::FitWidth) {
            float maxW = 0.0f;
            for (uint32_t i = 0; i < count; ++i) {
                float w = pTab->document.GetPageSize(i).width;
                if (w > maxW) maxW = w;
            }
            if (maxW > 0.0f) {
                pTab->zoom = std::max(0.10f, (dipW - margin * 2.0f) / maxW);
            }
            pTab->offsetX = 0.0f;
        } else if (pTab->zoomMode == ZoomMode::FitPage) {
            D2D1_SIZE_F pSize = pTab->document.GetPageSize(anchorPage);
            if (pSize.height > 0.0f && pSize.width > 0.0f) {
                float scaleX = (dipW - margin * 2.0f) / pSize.width;
                float scaleY = (dipH - margin * 2.0f) / pSize.height;
                pTab->zoom = std::max(0.10f, std::min(scaleX, scaleY));
            }
            pTab->offsetX = 0.0f;
        }

        float newPageTop = GetPageYOffset(pTab, anchorPage);
        float newScrollY = newPageTop;
        if (pTab->scrollY >= oldPageTop) {
            float ptOffset = (oldZoom > 0.0f) ? ((pTab->scrollY - oldPageTop) / oldZoom) : 0.0f;
            newScrollY = newPageTop + ptOffset * pTab->zoom;
        } else {
            float marginOffset = oldPageTop - pTab->scrollY;
            newScrollY = newPageTop - marginOffset;
        }

        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        pTab->scrollY = std::clamp(newScrollY, 0.0f, maxScroll);
        pTab->currentPage = anchorPage;
        return;
    }

    D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
    if (pSize.width <= 0.0f || pSize.height <= 0.0f) return;

    if (pTab->zoomMode == ZoomMode::FitPage) {
        float margin = 24.0f;
        float scaleX = (dipW - margin * 2.0f) / pSize.width;
        float scaleY = (dipH - margin * 2.0f) / pSize.height;
        pTab->zoom = std::max(0.10f, std::min(scaleX, scaleY));
        pTab->offsetX = (dipW - pSize.width * pTab->zoom) * 0.5f;
        pTab->offsetY = (dipH - pSize.height * pTab->zoom) * 0.5f;
    } else if (pTab->zoomMode == ZoomMode::FitWidth) {
        float margin = 24.0f;
        pTab->zoom = std::max(0.10f, (dipW - margin * 2.0f) / pSize.width);
        pTab->offsetX = margin;
        pTab->offsetY = margin;
    } else {
        // Custom zoom: Center if smaller than viewport
        float renderedW = pSize.width * pTab->zoom;
        float renderedH = pSize.height * pTab->zoom;
        if (renderedW < dipW) {
            pTab->offsetX = (dipW - renderedW) * 0.5f;
        }
        if (renderedH < dipH) {
            pTab->offsetY = (dipH - renderedH) * 0.5f;
        }
    }
}

void AppWindow::NextPage(bool shouldRender) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;
    if (pTab->currentPage + 1 < pTab->document.GetPageCount()) {
        pTab->currentPage++;
        if (pTab->continuousScroll) {
            float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
            float totalH = GetTotalDocumentHeight(pTab);
            float maxScroll = std::max(0.0f, totalH - dipH);
            float scrollY = GetPageYOffset(pTab, pTab->currentPage) - 24.0f;
            pTab->scrollY = std::clamp(scrollY, 0.0f, maxScroll);
        } else {
            if (pTab->zoomMode != ZoomMode::Custom) {
                RecalculateLayout();
            } else {
                pTab->offsetY = 24.0f;
            }
        }
        UpdateTitle();
        ShowScrollbar();
        if (shouldRender) {
            Render();
        }
    }
}

void AppWindow::PrevPage(bool shouldRender) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;
    if (pTab->currentPage > 0) {
        pTab->currentPage--;
        if (pTab->continuousScroll) {
            float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
            float totalH = GetTotalDocumentHeight(pTab);
            float maxScroll = std::max(0.0f, totalH - dipH);
            float scrollY = GetPageYOffset(pTab, pTab->currentPage) - 24.0f;
            pTab->scrollY = std::clamp(scrollY, 0.0f, maxScroll);
        } else {
            if (pTab->zoomMode != ZoomMode::Custom) {
                RecalculateLayout();
            } else {
                pTab->offsetY = 24.0f;
            }
        }
        UpdateTitle();
        ShowScrollbar();
        if (shouldRender) {
            Render();
        }
    }
}

void AppWindow::GoToPage(uint32_t pageIndex) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pageIndex >= pTab->document.GetPageCount()) return;
    pTab->currentPage = pageIndex;
    if (pTab->continuousScroll) {
        float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        float scrollY = GetPageYOffset(pTab, pageIndex) - 24.0f;
        pTab->scrollY = std::clamp(scrollY, 0.0f, maxScroll);
    } else {
        if (pTab->zoomMode != ZoomMode::Custom) {
            RecalculateLayout();
        } else {
            pTab->offsetY = 24.0f;
        }
    }
    UpdateTitle();
    ShowScrollbar();
    Render();
}

void AppWindow::ToggleContinuousScroll() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    pTab->continuousScroll = !pTab->continuousScroll;

    if (pTab->continuousScroll) {
        float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        float scrollY = GetPageYOffset(pTab, pTab->currentPage) - 24.0f;
        pTab->scrollY = std::clamp(scrollY, 0.0f, maxScroll);
        pTab->offsetX = 0.0f;
    } else {
        pTab->currentPage = GetPageAtScrollOffset(pTab);
        pTab->offsetY = 24.0f;
    }

    RecalculateLayout();
    UpdateTitle();
    ShowScrollbar();
    Render();
}

void AppWindow::ScrollContinuous(float deltaY) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
    float totalH = GetTotalDocumentHeight(pTab);
    float maxScroll = std::max(0.0f, totalH - dipH);

    pTab->scrollY = std::clamp(pTab->scrollY - deltaY, 0.0f, maxScroll);
    pTab->currentPage = GetPageAtScrollOffset(pTab);
    UpdateTitle();
    ShowScrollbar();
    Render();
}

void AppWindow::UpdateContinuousOffsets(DocumentTab* pTab) {
    if (!pTab || !pTab->document.IsLoaded()) return;
    uint32_t count = pTab->document.GetPageCount();
    if (count == 0) {
        pTab->pageOffsets.clear();
        pTab->totalDocHeight = 0.0f;
        pTab->lastOffsetsZoom = pTab->zoom;
        return;
    }
    if (pTab->pageOffsets.size() == count && pTab->lastOffsetsZoom == pTab->zoom) {
        return;
    }
    pTab->pageOffsets.resize(count);
    float y = 24.0f; // top margin
    float gap = 12.0f;
    for (uint32_t i = 0; i < count; ++i) {
        pTab->pageOffsets[i] = y;
        y += pTab->document.GetPageSize(i).height * pTab->zoom;
        if (i + 1 < count) {
            y += gap;
        }
    }
    pTab->totalDocHeight = y + 24.0f; // bottom margin
    pTab->lastOffsetsZoom = pTab->zoom;
}

float AppWindow::GetTotalDocumentHeight(const DocumentTab* pTab) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0.0f;
    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != pTab->document.GetPageCount()) {
        const_cast<AppWindow*>(this)->UpdateContinuousOffsets(const_cast<DocumentTab*>(pTab));
    }
    return pTab->totalDocHeight;
}

float AppWindow::GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0.0f;
    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != pTab->document.GetPageCount()) {
        const_cast<AppWindow*>(this)->UpdateContinuousOffsets(const_cast<DocumentTab*>(pTab));
    }
    if (pageIndex < pTab->pageOffsets.size()) {
        return pTab->pageOffsets[pageIndex];
    }
    return pTab->totalDocHeight;
}

uint32_t AppWindow::GetPageAtScrollOffset(const DocumentTab* pTab) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0;
    uint32_t count = pTab->document.GetPageCount();
    if (count <= 1) return 0;

    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != count) {
        const_cast<AppWindow*>(this)->UpdateContinuousOffsets(const_cast<DocumentTab*>(pTab));
    }

    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
    float targetY = pTab->scrollY + dipH * 0.45f;

    auto it = std::upper_bound(pTab->pageOffsets.begin(), pTab->pageOffsets.end(), targetY);
    if (it == pTab->pageOffsets.begin()) return 0;
    size_t idx = std::distance(pTab->pageOffsets.begin(), it) - 1;
    return (uint32_t)std::min(idx, (size_t)(count - 1));
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
    auto* pTab = GetActiveTab();
    UpdateTabRenderInfos(m_cachedTabInfos);
    const auto& tabInfos = m_cachedTabInfos;

    m_docPropsInfo.visible = m_showProperties;
    m_docPropsInfo.hoveredBtn = m_propsHoveredBtn;
    m_docPropsInfo.copyFeedback = (m_propsCopiedFeedbackTime > 0 && (GetTickCount64() - m_propsCopiedFeedbackTime < 2000));

    if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
        std::wstring modeStr = L"";
        if (pTab->zoomMode == ZoomMode::FitPage) modeStr = L"Fit Page";
        else if (pTab->zoomMode == ZoomMode::FitWidth) modeStr = L"Fit Width";

        if (pTab->continuousScroll) {
            float dipW = m_renderer.GetWidth() * (96.0f / m_renderer.GetDpi());
            float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
            float viewTop = pTab->scrollY;
            float viewBot = pTab->scrollY + dipH;
            uint32_t count = pTab->document.GetPageCount();

            UpdateContinuousOffsets(pTab);
            const auto& offsets = pTab->pageOffsets;

            // Binary search for the first page that could be visible
            auto it = std::upper_bound(offsets.begin(), offsets.end(), viewTop);
            uint32_t startIdx = 0;
            if (it != offsets.begin()) {
                startIdx = static_cast<uint32_t>(std::distance(offsets.begin(), it) - 1);
            }

            std::vector<ContinuousPageInfo> visiblePages;
            for (uint32_t i = startIdx; i < count; ++i) {
                float curY = offsets[i];
                D2D1_SIZE_F pSize = pTab->document.GetPageSize(i);
                float pageH = pSize.height * pTab->zoom;
                float pageW = pSize.width * pTab->zoom;

                if (curY + pageH >= viewTop && curY <= viewBot) {
                    ContinuousPageInfo info;
                    info.page = pTab->document.GetPage(i);
                    info.pageSize = pSize;
                    info.yOffset = curY - viewTop;
                    if (dipW > pageW) {
                        info.xOffset = (dipW - pageW) * 0.5f + pTab->offsetX;
                    } else {
                        info.xOffset = 24.0f + pTab->offsetX;
                    }
                    info.pageIndex = i;
                    visiblePages.push_back(std::move(info));
                } else if (curY > viewBot) {
                    break;
                }
            }

            m_renderer.RenderContinuous(
                visiblePages,
                pTab->zoom,
                pTab->currentPage,
                pTab->document.GetPageCount(),
                modeStr,
                true,
                m_showHelp,
                tabInfos,
                m_hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo
            );
            return;
        }

        if (pTab->currentPage < pTab->document.GetPageCount()) {
            auto page = pTab->document.GetPage(pTab->currentPage);
            auto pSize = pTab->document.GetPageSize(pTab->currentPage);

            m_renderer.RenderPage(
                page,
                pTab->zoom,
                pTab->offsetX,
                pTab->offsetY,
                pSize,
                pTab->currentPage,
                pTab->document.GetPageCount(),
                modeStr,
                m_showHelp,
                tabInfos,
                m_hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo
            );
            return;
        }
    }

    m_renderer.RenderBlank(
        L"",
        m_showHelp,
        tabInfos,
        m_hoveredAdd,
        m_showGoToPage,
        m_goToPageBuffer,
        GetSearchBarInfo(),
        m_docPropsInfo
    );
}

void AppWindow::ShowScrollbar() {
    m_lastScrollbarActiveTime = GetTickCount64();
    SetTimer(m_hwnd, 1, 16, nullptr);
}

ScrollbarRenderInfo AppWindow::GetScrollbarInfo() const {
    ScrollbarRenderInfo info;
    const auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        info.visible = false;
        return info;
    }

    uint32_t totalPages = pTab->document.GetPageCount();
    float dipScale = 96.0f / m_renderer.GetDpi();
    float topOffset = GetTopOffset();
    float dipH = (float)m_renderer.GetHeight() * dipScale;

    info.visible = true;
    info.trackY = topOffset + 4.0f;
    info.trackH = std::max(10.0f, dipH - info.trackY - 4.0f);
    info.alpha = m_scrollbarAlpha;
    info.isHovered = m_isScrollbarHovered;
    info.isDragging = m_isDraggingScrollbar;
    info.totalPages = totalPages;

    if (pTab->continuousScroll) {
        float totalDocH = GetTotalDocumentHeight(pTab);
        float viewportH = dipH - topOffset;
        if (totalDocH <= viewportH || totalDocH <= 0.0f) {
            info.thumbH = info.trackH;
            info.thumbY = info.trackY;
            info.hoverPage = pTab->currentPage;
        } else {
            float ratio = viewportH / totalDocH;
            info.thumbH = std::clamp(info.trackH * ratio, 24.0f, info.trackH);
            if (m_isDraggingScrollbar) {
                info.thumbY = std::clamp(m_scrollbarDragThumbY, info.trackY, info.trackY + info.trackH - info.thumbH);
            } else {
                float scrollFraction = pTab->scrollY / (totalDocH - viewportH);
                scrollFraction = std::clamp(scrollFraction, 0.0f, 1.0f);
                info.thumbY = info.trackY + scrollFraction * (info.trackH - info.thumbH);
                m_scrollbarDragThumbY = info.thumbY;
            }
            info.hoverPage = pTab->currentPage;
        }
    } else {
        // Single page mode
        if (totalPages <= 1) {
            info.thumbH = info.trackH;
            info.thumbY = info.trackY;
            info.hoverPage = 0;
            m_scrollbarDragThumbY = info.thumbY;
        } else {
            info.thumbH = std::max(24.0f, info.trackH / (float)totalPages);
            if (m_isDraggingScrollbar) {
                info.thumbY = std::clamp(m_scrollbarDragThumbY, info.trackY, info.trackY + info.trackH - info.thumbH);
            } else {
                float step = (info.trackH - info.thumbH) / (float)(totalPages - 1);
                info.thumbY = info.trackY + step * (float)pTab->currentPage;
                m_scrollbarDragThumbY = info.thumbY;
            }
            info.hoverPage = pTab->currentPage;
        }
    }

    return info;
}

bool AppWindow::HitTestScrollbar(POINT pt, bool& outThumb, ScrollbarRenderInfo* outInfo) const {
    outThumb = false;
    const auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return false;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = (float)m_renderer.GetWidth() * dipScale;

    ScrollbarRenderInfo info = GetScrollbarInfo();
    if (!info.visible) return false;

    // Track hit area extends to 24 DIPs from right window edge
    if (dipX >= dipW - 24.0f && dipX <= dipW && dipY >= info.trackY && dipY <= info.trackY + info.trackH) {
        if (dipY >= info.thumbY && dipY <= info.thumbY + info.thumbH) {
            outThumb = true;
        }
        if (outInfo) {
            *outInfo = info;
        }
        return true;
    }
    return false;
}

bool AppWindow::HitTestHud(POINT pt) const {
    const auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return false;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipWidth = (float)m_renderer.GetWidth() * dipScale;
    float dipHeight = (float)m_renderer.GetHeight() * dipScale;

    float pillWidth = 190.0f;
    bool hasZoomMode = (pTab->zoomMode != ZoomMode::Custom);
    if (pTab->continuousScroll) {
        pillWidth = hasZoomMode ? 330.0f : 280.0f;
    } else {
        pillWidth = hasZoomMode ? 240.0f : 190.0f;
    }

    float pillHeight = 32.0f;
    float pillLeft = (dipWidth - pillWidth) * 0.5f;
    float pillTop = dipHeight - pillHeight - 16.0f;

    return (dipX >= pillLeft && dipX <= pillLeft + pillWidth &&
            dipY >= pillTop && dipY <= pillTop + pillHeight);
}

void AppWindow::HandleScrollbarDrag(float mouseY) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return;

    ScrollbarRenderInfo info = GetScrollbarInfo();
    float usableH = info.trackH - info.thumbH;
    if (usableH <= 0.0f) return;

    m_scrollbarDragThumbY = std::clamp(mouseY - m_scrollbarDragThumbOffsetY, info.trackY, info.trackY + usableH);
    float fraction = (m_scrollbarDragThumbY - info.trackY) / usableH;
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    uint32_t totalPages = pTab->document.GetPageCount();

    if (pTab->continuousScroll) {
        float dipScale = 96.0f / m_renderer.GetDpi();
        float topOffset = GetTopOffset();
        float dipH = (float)m_renderer.GetHeight() * dipScale - topOffset;
        float totalDocH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalDocH - dipH);

        pTab->scrollY = fraction * maxScroll;
        pTab->currentPage = GetPageAtScrollOffset(pTab);
        UpdateTitle();
        Render();
    } else {
        if (totalPages > 1) {
            uint32_t targetPage = (uint32_t)std::round(fraction * (float)(totalPages - 1));
            if (targetPage != pTab->currentPage) {
                GoToPage(targetPage);
            } else {
                Render();
            }
        } else {
            Render();
        }
    }
}

SearchBarRenderInfo AppWindow::GetSearchBarInfo() const {
    SearchBarRenderInfo info;
    info.visible = m_showSearch;
    info.hasTabs = (m_tabs.size() > 1);
    info.query = m_searchQuery;
    info.matchCase = m_searchMatchCase;
    info.ocrEnabled = m_searchOcrEnabled;
    info.isSearching = m_searchEngine.IsSearching();
    info.isDebouncing = m_searchDebouncePending;
    info.hasScanned = m_searchEngine.HasScannedPages();
    info.totalMatches = m_searchEngine.GetTotalMatches();
    int activeIdx = m_searchEngine.GetActiveMatchIndex();
    info.activeMatch = (activeIdx >= 0) ? (uint32_t)(activeIdx + 1) : 0;

    info.isPrevHovered = (m_searchHoveredBtn == 1);
    info.isNextHovered = (m_searchHoveredBtn == 2);
    info.isCaseHovered = (m_searchHoveredBtn == 3);
    info.isOcrHovered = (m_searchHoveredBtn == 4);
    info.isCloseHovered = (m_searchHoveredBtn == 5);

    return info;
}

const std::vector<SearchHighlight>& AppWindow::GetSearchHighlights() const {
    if (!m_showSearch || m_searchQuery.empty()) {
        m_cachedHighlights.clear();
        m_highlightsDirty = false;
        return m_cachedHighlights;
    }

    if (m_highlightsDirty) {
        auto matches = m_searchEngine.GetAllMatches();
        int activeIdx = m_searchEngine.GetActiveMatchIndex();

        m_cachedHighlights.clear();
        m_cachedHighlights.reserve(matches.size());
        for (size_t i = 0; i < matches.size(); ++i) {
            SearchHighlight hl;
            hl.pageIndex = matches[i].pageIndex;
            hl.pageRect = matches[i].pageRect;
            hl.rects = matches[i].rects;
            hl.isActive = ((int)i == activeIdx);
            m_cachedHighlights.push_back(std::move(hl));
        }
        m_highlightsDirty = false;
    }

    return m_cachedHighlights;
}

int AppWindow::HitTestSearchBar(POINT pt) const {
    if (!m_showSearch) return -1;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = (float)m_renderer.GetWidth() * dipScale;

    D2D1_RECT_F bar = SearchBarLayout::GetBarRect(dipW, m_tabs.size() > 1);
    if (dipX < bar.left || dipX > bar.right || dipY < bar.top || dipY > bar.bottom) {
        return -1;
    }

    auto inRect = [](const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    };

    if (inRect(SearchBarLayout::GetPrevBtnRect(bar), dipX, dipY)) return 1;
    if (inRect(SearchBarLayout::GetNextBtnRect(bar), dipX, dipY)) return 2;
    if (inRect(SearchBarLayout::GetCaseBtnRect(bar), dipX, dipY)) return 3;
    if (inRect(SearchBarLayout::GetOcrBtnRect(bar), dipX, dipY)) return 4;
    if (inRect(SearchBarLayout::GetCloseBtnRect(bar), dipX, dipY)) return 5;
    if (inRect(SearchBarLayout::GetInputRect(bar), dipX, dipY)) return 0;

    return 0;
}

void AppWindow::TriggerSearch() {
    KillTimer(m_hwnd, 2);
    m_searchDebouncePending = false;

    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || m_searchQuery.empty()) {
        m_searchEngine.Cancel();
        m_searchEngine.Clear();
        m_lastJumpedMatch = -1;
        InvalidateSearchHighlights();
        return;
    }

    m_lastJumpedMatch = -1;
    InvalidateSearchHighlights();
    m_searchEngine.StartSearch(
        m_hwnd,
        pTab->document.GetFilePath(),
        pTab->document.GetPageCount(),
        m_searchQuery,
        m_searchMatchCase,
        m_searchOcrEnabled,
        pTab->document.GetDoc(),
        pTab->textCache
    );
}

void AppWindow::JumpToActiveMatch() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    InvalidateSearchHighlights();

    SearchMatch match = m_searchEngine.GetActiveMatch();
    if (match.pageIndex >= pTab->document.GetPageCount()) return;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipW = (float)m_renderer.GetWidth() * dipScale;
    float dipH = (float)m_renderer.GetHeight() * dipScale - GetTopOffset();

    if (pTab->continuousScroll) {
        float curY = GetPageYOffset(pTab, match.pageIndex);
        float matchCenterY = curY + (match.pageRect.top + (match.pageRect.bottom - match.pageRect.top) * 0.5f) * pTab->zoom;
        float targetScrollY = matchCenterY - (dipH * 0.5f);
        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        pTab->scrollY = std::clamp(targetScrollY, 0.0f, maxScroll);
        pTab->currentPage = match.pageIndex;
    } else {
        if (pTab->currentPage != match.pageIndex) {
            pTab->currentPage = match.pageIndex;
            if (pTab->zoomMode != ZoomMode::Custom) {
                RecalculateLayout();
            }
        }
        D2D1_SIZE_F pSize = pTab->document.GetPageSize(match.pageIndex);
        float pageRenderW = pSize.width * pTab->zoom;
        float pageRenderH = pSize.height * pTab->zoom;
        float matchCenterX = (match.pageRect.left + (match.pageRect.right - match.pageRect.left) * 0.5f) * pTab->zoom;
        float matchCenterY = (match.pageRect.top + (match.pageRect.bottom - match.pageRect.top) * 0.5f) * pTab->zoom;

        if (pageRenderW > dipW) {
            pTab->offsetX = (dipW * 0.5f) - matchCenterX;
        }
        if (pageRenderH > dipH) {
            pTab->offsetY = (dipH * 0.5f) - matchCenterY;
        }
    }
    UpdateTitle();
    ShowScrollbar();
}

void AppWindow::CloseSearch() {
    KillTimer(m_hwnd, 2);
    m_searchDebouncePending = false;
    m_showSearch = false;
    m_searchHoveredBtn = 0;
    m_searchEngine.Cancel();
    m_searchEngine.Clear();
    m_lastJumpedMatch = -1;
    InvalidateSearchHighlights();
}

void AppWindow::ScheduleSearchDebounce() {
    KillTimer(m_hwnd, 2);
    m_searchDebouncePending = true;
    m_searchEngine.CancelAsync();
    m_searchEngine.Clear();
    m_lastJumpedMatch = -1;
    InvalidateSearchHighlights();

    if (m_searchQuery.empty()) {
        m_searchDebouncePending = false;
        Render();
        return;
    }

    // Debounce timer: wait 350 ms after last keystroke before initiating search
    SetTimer(m_hwnd, 2, 350, nullptr);
    Render();
}

void AppWindow::ShowDocumentProperties() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        return;
    }

    m_docPropsInfo = DocumentPropertiesRenderInfo();
    m_docPropsInfo.visible = true;
    m_propsHoveredBtn = 0;
    m_propsCopiedFeedbackTime = 0;

    std::wstring filePath = pTab->document.GetFilePath();

    // 1. Extract PDF metadata (cached per tab)
    if (!pTab->metadataLoaded) {
        PdfParser parser;
        if (parser.Load(filePath)) {
            parser.ExtractMetadata(pTab->metadata);
            pTab->metadataLoaded = true;
        }
    }
    const auto& meta = pTab->metadata;

    m_docPropsInfo.title = meta.title.empty() ? L"—" : meta.title;
    m_docPropsInfo.author = meta.author.empty() ? L"—" : meta.author;
    m_docPropsInfo.subject = meta.subject.empty() ? L"—" : meta.subject;
    m_docPropsInfo.keywords = meta.keywords.empty() ? L"—" : meta.keywords;
    m_docPropsInfo.creator = meta.creator.empty() ? L"—" : meta.creator;
    m_docPropsInfo.producer = meta.producer.empty() ? L"—" : meta.producer;
    m_docPropsInfo.pdfFormat = meta.pdfFormat.empty() ? L"—" : meta.pdfFormat;
    m_docPropsInfo.created = meta.creationDate.empty() ? L"—" : meta.creationDate;
    m_docPropsInfo.modified = meta.modDate.empty() ? L"—" : meta.modDate;

    // 2. Total pages
    uint32_t totalPages = pTab->document.GetPageCount();
    m_docPropsInfo.totalPages = std::to_wstring(totalPages);

    // 3. Current page size
    D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
    float widthIn = pSize.width / 72.0f;
    float heightIn = pSize.height / 72.0f;
    float widthMm = widthIn * 25.4f;
    float heightMm = heightIn * 25.4f;

    wchar_t sizeBuf[128];
    swprintf_s(sizeBuf, L"%.2f × %.2f in (%.1f × %.1f pt / %.1f × %.1f mm)",
        widthIn, heightIn, pSize.width, pSize.height, widthMm, heightMm);
    m_docPropsInfo.pageSize = sizeBuf;

    // 4. File size on disk
    WIN32_FILE_ATTRIBUTE_DATA fileAttrData;
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fileAttrData)) {
        ULARGE_INTEGER fileSize;
        fileSize.HighPart = fileAttrData.nFileSizeHigh;
        fileSize.LowPart = fileAttrData.nFileSizeLow;
        uint64_t bytes = fileSize.QuadPart;

        wchar_t fSizeBuf[64];
        if (bytes >= 1024 * 1024) {
            double mb = (double)bytes / (1024.0 * 1024.0);
            swprintf_s(fSizeBuf, L"%.2f MB (%llu bytes)", mb, bytes);
        } else if (bytes >= 1024) {
            double kb = (double)bytes / 1024.0;
            swprintf_s(fSizeBuf, L"%.1f KB (%llu bytes)", kb, bytes);
        } else {
            swprintf_s(fSizeBuf, L"%llu bytes", bytes);
        }
        m_docPropsInfo.fileSize = fSizeBuf;
    } else {
        m_docPropsInfo.fileSize = L"—";
    }

    m_showProperties = true;
    m_showHelp = false;
    m_showGoToPage = false;
    m_showSearch = false;

    Render();
}

void AppWindow::CloseDocumentProperties() {
    if (m_showProperties) {
        m_showProperties = false;
        m_docPropsInfo.visible = false;
        m_propsHoveredBtn = 0;
        Render();
    }
}

void AppWindow::CopyPropertiesToClipboard() {
    std::wstring text;
    text += L"Document Properties\r\n";
    text += L"===================\r\n\r\n";
    text += L"Document Information:\r\n";
    text += L"  Title:     " + m_docPropsInfo.title + L"\r\n";
    text += L"  Author:    " + m_docPropsInfo.author + L"\r\n";
    text += L"  Subject:   " + m_docPropsInfo.subject + L"\r\n";
    text += L"  Keywords:  " + m_docPropsInfo.keywords + L"\r\n";
    text += L"  Creator:   " + m_docPropsInfo.creator + L"\r\n";
    text += L"  Producer:  " + m_docPropsInfo.producer + L"\r\n\r\n";
    text += L"File & Page Details:\r\n";
    text += L"  Total Pages: " + m_docPropsInfo.totalPages + L"\r\n";
    text += L"  File Size:   " + m_docPropsInfo.fileSize + L"\r\n";
    text += L"  PDF Format:  " + m_docPropsInfo.pdfFormat + L"\r\n";
    text += L"  Page Size:   " + m_docPropsInfo.pageSize + L"\r\n";
    text += L"  Created:     " + m_docPropsInfo.created + L"\r\n";
    text += L"  Modified:    " + m_docPropsInfo.modified + L"\r\n";

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }

    m_propsCopiedFeedbackTime = GetTickCount64();
    Render();
}


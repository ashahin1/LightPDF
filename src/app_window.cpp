#include "app_window.hpp"
#include "pdf_parser.hpp"
#include <windowsx.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

const wchar_t* WINDOW_CLASS_NAME = L"LightPDF_WindowClass";

AppWindow::AppWindow() = default;

AppWindow::~AppWindow() {
    m_cancelBakingPdf = true;
    if (m_bakePdfThread.joinable()) {
        m_bakePdfThread.join();
    }
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
    static HBRUSH s_hbrDarkBg = CreateSolidBrush(RGB(31, 31, 31));
    wc.hbrBackground = s_hbrDarkBg;
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

    // Initialize offline dictionary engine (non-blocking, graceful fallback if missing)
    m_dictEngine.Initialize();

    if (!initialFile.empty()) {
        OpenTab(initialFile);
    }
    if (m_tabs.empty()) {
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

    case WM_APP_BAKE_PDF_DONE: {
        m_isBakingPdf = false;
        int status = (int)wParam; // 1 = success, 0 = error, 2 = cancelled
        bool wasOverwrite = (lParam != 0);
        if (status == 1) {
            if (wasOverwrite) {
                auto* pTab = GetActiveTab();
                if (pTab) {
                    std::wstring curPath = pTab->document.GetFilePath();
                    uint32_t curPage = pTab->currentPage;
                    pTab->document.Close();
                    if (pTab->parser) pTab->parser->Close();

                    if (pTab->document.Open(curPath, m_hwnd)) {
                        pTab->parser = std::make_unique<PdfParser>();
                        pTab->parser->Load(curPath);
                        pTab->textCache = std::make_shared<PageTextCache>();
                        pTab->textCache->pages.resize(pTab->document.GetPageCount());
                        pTab->currentPage = std::min(curPage, pTab->document.GetPageCount() - 1);
                        m_renderer.InvalidatePageCache();
                        RecalculateLayout();
                        UpdateTitle();
                        ShowToast(L"Searchable PDF saved! (Original file updated)");
                    } else {
                        ShowToast(L"Searchable PDF saved, please reopen the file.");
                    }
                }
            } else {
                ShowToast(L"Searchable PDF saved successfully!");
            }
        } else if (status == 2) {
            ShowToast(L"Searchable PDF generation cancelled.");
        } else {
            ShowToast(L"Failed to generate searchable PDF.");
        }
        Render();
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
        } else if (wParam == 3) {
            // HUD toast timer: revert HUD status message
            KillTimer(m_hwnd, 3);
            m_hudToastTime = 0;
            m_hudToastText.clear();
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
        if (pTab) {
            if (pTab->zoomMode != ZoomMode::Custom) {
                RecalculateLayout();
            } else {
                ClampCanvasOffsets(pTab);
            }
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
            ScrollSinglePage((float)delta * 0.6f);
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

        // 0a. If Presenter Bar is visible in fullscreen, handle button clicks
        if (msg == WM_LBUTTONDOWN && m_isFullscreen && m_showPresenterBar) {
            int presHit = m_renderer.HitTestPresenterBar(pt);
            if (presHit >= 0 && presHit <= 4) {
                switch (presHit) {
                case 0: PrevPage(); break;
                case 1: NextPage(); break;
                case 2: ToggleLaserPointer(); break;
                case 3: CycleLaserColor(); break;
                case 4: ToggleFullscreen(); break;
                }
                return 0;
            } else if (presHit == 100) {
                return 0; // Clicked on bar body
            }
        }

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

        // 0b. If Dictionary Card is open and clicked outside, dismiss it
        if (m_dictCardInfo.visible && !HitTestDictionaryCard(pt)) {
            DismissDictionaryCard();
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

        // 3. Help Overlay interaction
        if (m_showHelp) {
            int hit = m_renderer.HitTestHelpOverlay(pt);
            if (hit == 100 || hit == -1) {
                // Close button [×] or outside card (backdrop)
                m_showHelp = false;
                Render();
                return 0;
            } else if (hit >= 0 && hit <= 4) {
                // Category tab clicked
                m_helpActiveCategory = hit;
                Render();
                return 0;
            } else if (hit == 999) {
                // Clicked inside card body - do nothing, swallow click
                return 0;
            }
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

        // 6. Canvas & Text Interaction
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            if (msg == WM_MBUTTONDOWN) {
                // Middle click: always pan
                m_isPanning = true;
                m_lastMousePos = pt;
                SetCapture(m_hwnd);
                SetCursor(m_cursorSizeAll);
                return 0;
            }

            // Left click: Check if Hand mode or holding spacebar
            bool isSpaceDown = (GetKeyState(VK_SPACE) & 0x8000) != 0;
            if (m_toolMode == ToolMode::Hand || isSpaceDown) {
                m_isPanning = true;
                m_lastMousePos = pt;
                SetCapture(m_hwnd);
                SetCursor(m_cursorSizeAll);
                return 0;
            }

            // Text Selection Mode
            uint32_t hitPage = 0;
            size_t hitChar = 0;
            bool afterChar = false;
            bool hitText = HitTestPageText(pt, hitPage, hitChar, afterChar);

            if (hitText) {
                size_t idx = afterChar ? hitChar + 1 : hitChar;
                pTab->selection.active = true;
                pTab->selection.isDragging = true;
                pTab->selection.startPage = hitPage;
                pTab->selection.startIndex = idx;
                pTab->selection.endPage = hitPage;
                pTab->selection.endIndex = idx;
                SetCapture(m_hwnd);
                Render();
                return 0;
            } else {
                // Clicked outside text on page margins / empty canvas
                if (pTab->selection.HasSelection()) {
                    pTab->selection.Clear();
                    Render();
                }
                m_isPanning = true;
                m_lastMousePos = pt;
                SetCapture(m_hwnd);
                SetCursor(m_cursorSizeAll);
                return 0;
            }
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_isLaserActive) {
            if (!m_isFullscreen || !m_showPresenterBar || m_presenterBarHoveredBtn < 0 || m_presenterBarHoveredBtn > 4) {
                SetCursor(NULL);
                return TRUE;
            }
        }
        break;

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipX = (float)pt.x * dipScale;
        float dipY = (float)pt.y * dipScale;
        float dipW = (float)m_renderer.GetWidth() * dipScale;
        float topOffset = GetTopOffset();

        // 00a. Presenter Bar hover detection in fullscreen
        if (m_isFullscreen) {
            float dipH = (float)m_renderer.GetHeight() * dipScale;
            bool nearBottom = (dipY >= dipH - 58.0f);
            if (nearBottom != m_showPresenterBar) {
                m_showPresenterBar = nearBottom;
                if (!m_showPresenterBar) m_presenterBarHoveredBtn = -1;
                Render();
            }
            if (m_showPresenterBar) {
                int hit = m_renderer.HitTestPresenterBar(pt);
                int newBtn = (hit >= 0 && hit <= 4) ? hit : -1;
                if (newBtn != m_presenterBarHoveredBtn) {
                    m_presenterBarHoveredBtn = newBtn;
                    Render();
                }
                if (newBtn >= 0) {
                    SetCursor(m_cursorHand);
                    return 0;
                }
            }
        } else if (m_showPresenterBar) {
            m_showPresenterBar = false;
            m_presenterBarHoveredBtn = -1;
        }

        // 00b. Laser pointer tracking
        if (m_isLaserActive) {
            m_laserPos = pt;
            SetCursor(NULL);
            Render();
            return 0;
        }

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

        // 0b. Help Overlay hover detection
        if (m_showHelp) {
            int hit = m_renderer.HitTestHelpOverlay(pt);
            int newCat = (hit >= 0 && hit <= 4) ? hit : -1;
            int newClose = (hit == 100) ? 1 : 0;
            if (newCat != m_helpHoveredCategory || newClose != m_helpHoveredClose) {
                m_helpHoveredCategory = newCat;
                m_helpHoveredClose = newClose;
                Render();
            }
            if (newCat >= 0 || newClose > 0) {
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

        auto* pTab = GetActiveTab();

        // 4. Handle Text Selection Dragging
        if (pTab && pTab->selection.isDragging) {
            uint32_t hitPage = 0;
            size_t hitChar = 0;
            bool afterChar = false;
            if (HitTestPageText(pt, hitPage, hitChar, afterChar)) {
                size_t newEnd = afterChar ? hitChar + 1 : hitChar;
                if (pTab->selection.endPage != hitPage || pTab->selection.endIndex != newEnd) {
                    pTab->selection.endPage = hitPage;
                    pTab->selection.endIndex = newEnd;
                    pTab->selection.active = true;
                    Render();
                }
            }

            // Auto-scroll when dragging near viewport top/bottom borders
            float dipH = (float)m_renderer.GetHeight() * dipScale;
            if (dipY < topOffset + 40.0f) {
                if (pTab->continuousScroll) ScrollContinuous(16.0f);
                else ScrollSinglePage(16.0f);
            } else if (dipY > dipH - 40.0f) {
                if (pTab->continuousScroll) ScrollContinuous(-16.0f);
                else ScrollSinglePage(-16.0f);
            }
            SetCursor(m_cursorIBeam);
            return 0;
        }

        // 5. Canvas Panning
        if (m_isPanning) {
            if (pTab) {
                float dx = (pt.x - m_lastMousePos.x) * dipScale;
                float dy = (pt.y - m_lastMousePos.y) * dipScale;
                m_lastMousePos = pt;

                if (pTab->continuousScroll) {
                    pTab->offsetX += dx;
                    pTab->scrollY = std::max(0.0f, pTab->scrollY - dy);
                    ClampCanvasOffsets(pTab);
                    ShowScrollbar();
                    Render();
                } else {
                    pTab->offsetX += dx;
                    pTab->offsetY += dy;
                    ClampCanvasOffsets(pTab);
                    D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
                    float renderedW = pSize.width * pTab->zoom;
                    float renderedH = pSize.height * pTab->zoom;
                    float dipW = (float)m_renderer.GetWidth() * dipScale;
                    float dipH = (float)m_renderer.GetHeight() * dipScale - topOffset;
                    if (renderedW > dipW - 48.0f || renderedH > dipH - 48.0f) {
                        pTab->zoomMode = ZoomMode::Custom;
                    }
                    Render();
                }
            }
            return 0;
        }

        // 6. Idle Hover Cursor Management
        if (pTab && pTab->document.IsLoaded() && dipY >= topOffset) {
            bool isSpaceDown = (GetKeyState(VK_SPACE) & 0x8000) != 0;
            if (m_toolMode == ToolMode::Hand || isSpaceDown) {
                SetCursor(m_cursorHand);
            } else {
                uint32_t hitPage = 0;
                size_t hitChar = 0;
                bool afterChar = false;
                if (HitTestPageText(pt, hitPage, hitChar, afterChar)) {
                    SetCursor(m_cursorIBeam);
                } else {
                    SetCursor(m_cursorArrow);
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->selection.isDragging) {
            pTab->selection.isDragging = false;
            ReleaseCapture();
            if (!pTab->selection.HasSelection()) {
                pTab->selection.active = false;
            }
            Render();
            return 0;
        }
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
            SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
        }
        return 0;
    }

    case WM_CAPTURECHANGED: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->selection.isDragging) {
            pTab->selection.isDragging = false;
            if (!pTab->selection.HasSelection()) {
                pTab->selection.active = false;
            }
            Render();
        }
        if (m_isDraggingScrollbar) {
            m_isDraggingScrollbar = false;
            ShowScrollbar();
            Render();
        }
        if (m_isPanning) {
            m_isPanning = false;
            SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
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
            // Double click in Text Select mode: select word if hitting text
            if (m_toolMode == ToolMode::TextSelect) {
                uint32_t hitPage = 0;
                size_t hitChar = 0;
                bool afterChar = false;
                if (HitTestPageText(pt, hitPage, hitChar, afterChar)) {
                    auto pageText = GetOrExtractPageText(pTab, hitPage);
                    if (pageText && hitChar < pageText->chars.size()) {
                        auto isWordChar = [](wchar_t c) {
                            return iswalnum(c) || c == L'_';
                        };
                        size_t wStart = hitChar;
                        while (wStart > 0 && isWordChar(pageText->chars[wStart - 1].ch)) {
                            wStart--;
                        }
                        size_t wEnd = hitChar;
                        while (wEnd < pageText->chars.size() && isWordChar(pageText->chars[wEnd].ch)) {
                            wEnd++;
                        }
                        if (wEnd > wStart) {
                            pTab->selection.active = true;
                            pTab->selection.isDragging = false;
                            pTab->selection.startPage = hitPage;
                            pTab->selection.startIndex = wStart;
                            pTab->selection.endPage = hitPage;
                            pTab->selection.endIndex = wEnd;

                            D2D1_RECT_F anchorRect = { 0, 0, 0, 0 };
                            std::wstring selectedWord = GetSelectedWordOrText(anchorRect);
                            if (!selectedWord.empty()) {
                                TriggerDictionaryLookup(selectedWord, anchorRect);
                            }
                            Render();
                            return 0;
                        }
                    }
                }
            }

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

        if (m_dictCardInfo.visible) {
            if (wParam == VK_ESCAPE) {
                DismissDictionaryCard();
                return 0;
            } else if (wParam == 'C' && isCtrlDown) {
                CopyDictionaryDefinitionToClipboard();
                return 0;
            }
        }

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

        if (m_showHelp) {
            if (wParam == VK_ESCAPE || wParam == VK_F1) {
                m_showHelp = false;
                Render();
                return 0;
            } else if (wParam >= '1' && wParam <= '5' && !isCtrlDown) {
                m_helpActiveCategory = (int)(wParam - '1');
                Render();
                return 0;
            } else if (wParam == VK_TAB) {
                if (isShiftDown) {
                    m_helpActiveCategory = (m_helpActiveCategory + 4) % 5;
                } else {
                    m_helpActiveCategory = (m_helpActiveCategory + 1) % 5;
                }
                Render();
                return 0;
            } else if (wParam == VK_LEFT) {
                m_helpActiveCategory = (m_helpActiveCategory + 4) % 5;
                Render();
                return 0;
            } else if (wParam == VK_RIGHT) {
                m_helpActiveCategory = (m_helpActiveCategory + 1) % 5;
                Render();
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
        case 'H':
            if (!isCtrlDown && !m_showSearch && !m_showGoToPage) {
                SetToolMode((m_toolMode == ToolMode::Hand) ? ToolMode::TextSelect : ToolMode::Hand);
                return 0;
            }
            break;
        case 'L':
            if (!isCtrlDown && !m_showSearch && !m_showGoToPage) {
                ToggleLaserPointer();
                return 0;
            }
            break;
        case 'S':
            if (isCtrlDown) {
                PromptSaveSearchablePdf(isShiftDown);
                return 0;
            } else if (!m_showSearch && !m_showGoToPage) {
                SetToolMode(ToolMode::TextSelect);
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
            } else if (!isCtrlDown && !m_showSearch && !m_showGoToPage) {
                SetToolMode(ToolMode::TextSelect);
                return 0;
            }
            break;
        case 'C':
            if (isCtrlDown) {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->selection.HasSelection()) {
                    CopySelectionToClipboard();
                    return 0;
                }
            } else if (m_isLaserActive && !m_showSearch && !m_showGoToPage) {
                CycleLaserColor();
                return 0;
            }
            break;
        case 'D':
            if (isCtrlDown) {
                if (m_showProperties) CloseDocumentProperties();
                else ShowDocumentProperties();
                return 0;
            } else if (!m_showSearch && !m_showGoToPage) {
                D2D1_RECT_F anchorRect = { 0, 0, 0, 0 };
                std::wstring query = GetSelectedWordOrText(anchorRect);
                if (!query.empty()) {
                    TriggerDictionaryLookup(query, anchorRect);
                } else {
                    ShowToast(L"Select text or double-click a word for dictionary lookup (D)");
                }
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
                    ScrollSinglePage(-40.0f);
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
                    ScrollSinglePage(40.0f);
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
            if (m_showHelp) {
                m_helpHoveredCategory = -1;
                m_helpHoveredClose = 0;
                m_showProperties = false;
                m_showGoToPage = false;
                m_showSearch = false;
            }
            Render();
            return 0;
        case VK_F11:
            ToggleFullscreen();
            return 0;
        case VK_ESCAPE:
            if (m_isBakingPdf) {
                CancelBakingSearchablePdf();
                ShowToast(L"Cancelling Searchable PDF generation...");
                return 0;
            }
            if (m_isLaserActive) {
                ToggleLaserPointer();
                return 0;
            }
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
            {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->selection.HasSelection()) {
                    pTab->selection.Clear();
                    Render();
                    return 0;
                }
            }
            if (m_isFullscreen) {
                ToggleFullscreen();
            }
            return 0;
        }
        break;
    }

    case WM_KEYUP:
        if (wParam == VK_SPACE && !m_isPanning) {
            SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
        }
        return 0;

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

    DocumentTab newTab;
    if (newTab.document.Open(resolvedPath, m_hwnd)) {
        newTab.currentPage = 0;
        newTab.zoomMode = ZoomMode::FitPage;
        newTab.continuousScroll = false;
        newTab.scrollY = 0.0f;
        newTab.textCache = std::make_shared<PageTextCache>();
        newTab.textCache->pages.resize(newTab.document.GetPageCount());
        newTab.selection.Clear();
        newTab.parser = std::make_unique<PdfParser>();
        newTab.parser->Load(resolvedPath);
        m_renderer.InvalidatePageCache();
        m_tabs.push_back(std::move(newTab));
        m_activeTab = m_tabs.size() - 1;
        if (m_showSearch && !m_searchQuery.empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();
        Render();
    } else {
        std::wstring fileName = resolvedPath;
        size_t lastSlash = resolvedPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            fileName = resolvedPath.substr(lastSlash + 1);
        }
        std::wstring msg = L"Unable to open \"" + fileName + L"\".\n\n"
                           L"The file may be password-protected, corrupted, or inaccessible.";
        MessageBoxW(m_hwnd, msg.c_str(), L"LightPDF - Error", MB_OK | MB_ICONWARNING);

        if (m_tabs.empty()) {
            UpdateTitle();
            Render();
        }
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

void AppWindow::CancelBakingSearchablePdf() {
    m_cancelBakingPdf = true;
}

void AppWindow::PromptSaveSearchablePdf(bool forceSaveAs) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        ShowToast(L"No document is currently loaded.");
        return;
    }

    if (m_isBakingPdf) {
        ShowToast(L"Searchable PDF generation is already in progress...");
        return;
    }

    std::wstring origPath = pTab->document.GetFilePath();
    std::wstring origName = pTab->document.GetFileName();

    bool doSaveAs = forceSaveAs;
    bool doOverwrite = false;

    if (!forceSaveAs) {
        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        tdc.hwndParent = m_hwnd;
        tdc.hInstance = m_hInstance;
        tdc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        tdc.pszWindowTitle = L"Save Searchable PDF";
        tdc.pszMainInstruction = L"How would you like to save this searchable PDF?";
        tdc.pszContent = L"Optical Character Recognition (OCR) will bake an invisible, permanent text layer (English & Arabic) into the document.";

        TASKDIALOG_BUTTON buttons[] = {
            { 101, L"Save As New File...\nChoose a new location or filename (Original file will be untouched)" },
            { 102, L"Overwrite Original File\nUpdate the current file directly (A safe backup swap will be used)" }
        };
        tdc.pButtons = buttons;
        tdc.cButtons = ARRAYSIZE(buttons);
        tdc.nDefaultButton = 101;

        int nButton = 0;
        HRESULT hrTd = TaskDialogIndirect(&tdc, &nButton, nullptr, nullptr);
        if (SUCCEEDED(hrTd)) {
            if (nButton == 101) {
                doSaveAs = true;
            } else if (nButton == 102) {
                doOverwrite = true;
            } else {
                return;
            }
        } else {
            int res = MessageBoxW(
                m_hwnd,
                L"Do you want to overwrite the original file?\n\n"
                L"Click 'Yes' to overwrite the original file.\n"
                L"Click 'No' to save as a new file.\n"
                L"Click 'Cancel' to abort.",
                L"Save Searchable PDF",
                MB_YESNOCANCEL | MB_ICONQUESTION
            );
            if (res == IDYES) {
                doOverwrite = true;
            } else if (res == IDNO) {
                doSaveAs = true;
            } else {
                return;
            }
        }
    }

    if (doOverwrite) {
        StartBakingSearchablePdf(origPath, true);
        return;
    }

    if (doSaveAs) {
        std::wstring defaultName = origName;
        size_t dotPos = defaultName.rfind(L'.');
        if (dotPos != std::string::npos) {
            defaultName = defaultName.substr(0, dotPos) + L"_searchable.pdf";
        } else {
            defaultName += L"_searchable.pdf";
        }

        ComPtr<IFileSaveDialog> pFileSave;
        HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pFileSave));
        if (SUCCEEDED(hr)) {
            COMDLG_FILTERSPEC filterSpecs[] = {
                { L"PDF Documents (*.pdf)", L"*.pdf" },
                { L"All Files (*.*)", L"*.*" }
            };
            pFileSave->SetFileTypes(ARRAYSIZE(filterSpecs), filterSpecs);
            pFileSave->SetDefaultExtension(L"pdf");
            pFileSave->SetFileName(defaultName.c_str());
            pFileSave->SetTitle(L"Save Searchable PDF As");

            hr = pFileSave->Show(m_hwnd);
            if (SUCCEEDED(hr)) {
                ComPtr<IShellItem> pItem;
                hr = pFileSave->GetResult(&pItem);
                if (SUCCEEDED(hr) && pItem) {
                    PWSTR pszFilePath = nullptr;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr) && pszFilePath) {
                        std::wstring targetPath(pszFilePath);
                        CoTaskMemFree(pszFilePath);
                        bool isSame = (_wcsicmp(targetPath.c_str(), origPath.c_str()) == 0);
                        StartBakingSearchablePdf(targetPath, isSame);
                    }
                }
            }
        } else {
            wchar_t szFile[MAX_PATH * 2] = { 0 };
            wcsncpy_s(szFile, defaultName.c_str(), _TRUNCATE);
            OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
            ofn.hwndOwner = m_hwnd;
            ofn.lpstrFilter = L"PDF Documents (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = _countof(szFile);
            ofn.lpstrDefExt = L"pdf";
            ofn.lpstrTitle = L"Save Searchable PDF As";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (GetSaveFileNameW(&ofn)) {
                std::wstring targetPath(szFile);
                bool isSame = (_wcsicmp(targetPath.c_str(), origPath.c_str()) == 0);
                StartBakingSearchablePdf(targetPath, isSame);
            }
        }
    }
}

void AppWindow::StartBakingSearchablePdf(const std::wstring& targetPath, bool overwriteOriginal) {
    if (m_isBakingPdf.exchange(true)) return;
    m_cancelBakingPdf = false;

    if (m_bakePdfThread.joinable()) {
        m_bakePdfThread.join();
    }

    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) {
        m_isBakingPdf = false;
        return;
    }

    std::wstring srcPath = pTab->document.GetFilePath();
    uint32_t totalPages = pTab->document.GetPageCount();
    auto doc = pTab->document.GetDoc();
    HWND hwnd = m_hwnd;

    ShowToast(L"Starting bilingual OCR baking...");

    m_bakePdfThread = std::thread([this, hwnd, srcPath, targetPath, overwriteOriginal, totalPages, doc]() {
        try {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
        } catch (...) {}

        winrt::Windows::Media::Ocr::OcrEngine ocrEngine{ nullptr };
        try {
            auto arLang = winrt::Windows::Globalization::Language(L"ar-SA");
            if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arLang)) {
                ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arLang);
            }
            if (!ocrEngine) {
                auto arGen = winrt::Windows::Globalization::Language(L"ar");
                if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arGen)) {
                    ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arGen);
                }
            }
            if (!ocrEngine) {
                ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
            }
            if (!ocrEngine) {
                auto enLang = winrt::Windows::Globalization::Language(L"en-US");
                if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(enLang)) {
                    ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(enLang);
                }
            }
        } catch (...) {}

        if (!ocrEngine || !doc) {
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 0, 0);
            return;
        }

        PdfParser parser;
        if (!parser.Load(srcPath)) {
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 0, 0);
            return;
        }

        std::vector<OcrPageItem> ocrPages;

        for (uint32_t p = 0; p < totalPages; ++p) {
            if (m_cancelBakingPdf) {
                PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 2, 0);
                return;
            }

            PdfPageText pageText;
            parser.ExtractPageText(p, pageText);

            if (!pageText.hasDigitalText) {
                try {
                    auto page = doc.GetPage(p);
                    if (page) {
                        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
                        page.RenderToStreamAsync(stream).get();
                        auto decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
                        auto bitmap = decoder.GetSoftwareBitmapAsync().get();
                        auto ocrResult = ocrEngine.RecognizeAsync(bitmap).get();

                        float scaleX = (bitmap.PixelWidth() > 0) ? (pageText.pageWidth / (float)bitmap.PixelWidth()) : 1.0f;
                        float scaleY = (bitmap.PixelHeight() > 0) ? (pageText.pageHeight / (float)bitmap.PixelHeight()) : 1.0f;

                        OcrPageItem pageItem;
                        pageItem.pageIndex = p;
                        pageItem.pageWidthDip = pageText.pageWidth;
                        pageItem.pageHeightDip = pageText.pageHeight;

                        for (auto line : ocrResult.Lines()) {
                            for (auto word : line.Words()) {
                                auto r = word.BoundingRect();
                                D2D1_RECT_F wRect = D2D1::RectF(
                                    r.X * scaleX,
                                    r.Y * scaleY,
                                    (r.X + r.Width) * scaleX,
                                    (r.Y + r.Height) * scaleY
                                );
                                pageItem.words.push_back({ std::wstring(word.Text()), wRect });
                            }
                        }

                        if (!pageItem.words.empty()) {
                            ocrPages.push_back(std::move(pageItem));
                        }
                    }
                } catch (...) {}
            }

            wchar_t progText[128];
            swprintf_s(progText, L"Baking Searchable PDF... Page %u of %u (Esc to cancel)", p + 1, totalPages);
            ShowToast(progText);
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        if (m_cancelBakingPdf) {
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 2, 0);
            return;
        }

        if (ocrPages.empty()) {
            ShowToast(L"Document already contains digital text on all pages.");
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 1, 0);
            return;
        }

        ShowToast(L"Writing searchable PDF to disk...");
        InvalidateRect(hwnd, nullptr, FALSE);

        bool ok = PdfSearchableWriter::WriteSearchablePdf(srcPath, targetPath, ocrPages, nullptr);
        PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, ok ? 1 : 0, overwriteOriginal ? 1 : 0);
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
    ClampCanvasOffsets(pTab);

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
        ClampCanvasOffsets(pTab);
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
    ClampCanvasOffsets(pTab);
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

void AppWindow::ScrollSinglePage(float deltaY) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return;

    D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
    float topOffset = GetTopOffset();
    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - topOffset;
    float renderedH = pSize.height * pTab->zoom;

    float margin = (pTab->zoomMode == ZoomMode::FitWidth) ? 24.0f : 20.0f;

    if (renderedH > dipH) {
        float oldOffsetY = pTab->offsetY;
        pTab->offsetY += deltaY;

        float minOffsetY = dipH - renderedH - margin;
        float maxOffsetY = margin;

        if (oldOffsetY <= minOffsetY && deltaY < 0.0f) {
            if (pTab->currentPage + 1 < pTab->document.GetPageCount()) {
                NextPage(false);
                pTab->offsetY = maxOffsetY;
            } else {
                pTab->offsetY = minOffsetY;
            }
        } else if (oldOffsetY >= maxOffsetY && deltaY > 0.0f) {
            if (pTab->currentPage > 0) {
                PrevPage(false);
                D2D1_SIZE_F prevSize = pTab->document.GetPageSize(pTab->currentPage);
                float prevRenderedH = prevSize.height * pTab->zoom;
                pTab->offsetY = dipH - prevRenderedH - margin;
            } else {
                pTab->offsetY = maxOffsetY;
            }
        } else {
            pTab->offsetY = std::clamp(pTab->offsetY, minOffsetY, maxOffsetY);
        }
        ShowScrollbar();
        Render();
    } else {
        if (deltaY < 0.0f) {
            if (pTab->currentPage + 1 < pTab->document.GetPageCount()) {
                NextPage();
            }
        } else if (deltaY > 0.0f) {
            if (pTab->currentPage > 0) {
                PrevPage();
            }
        }
    }
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
        m_showPresenterBar = false;
        m_presenterBarHoveredBtn = -1;
    }
    RecalculateLayout();
    Render();
}

void AppWindow::ToggleLaserPointer() {
    m_isLaserActive = !m_isLaserActive;
    if (m_isLaserActive) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hwnd, &pt);
        m_laserPos = pt;
        SetCursor(NULL);

        const wchar_t* colorName = L"Laser: Red";
        switch (m_laserColor) {
        case LaserColor::Red:   colorName = L"Laser: Red"; break;
        case LaserColor::Green: colorName = L"Laser: Emerald Green"; break;
        case LaserColor::Cyan:  colorName = L"Laser: Electric Cyan"; break;
        case LaserColor::Gold:  colorName = L"Laser: Amber Gold"; break;
        }
        m_hudToastText = colorName;
        m_hudToastTime = GetTickCount64();
    } else {
        m_hudToastText = L"Laser: OFF";
        m_hudToastTime = GetTickCount64();
        SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
    }
    Render();
}

void AppWindow::CycleLaserColor() {
    switch (m_laserColor) {
    case LaserColor::Red:   m_laserColor = LaserColor::Green; break;
    case LaserColor::Green: m_laserColor = LaserColor::Cyan;  break;
    case LaserColor::Cyan:  m_laserColor = LaserColor::Gold;  break;
    case LaserColor::Gold:  m_laserColor = LaserColor::Red;   break;
    }
    const wchar_t* colorName = L"Laser: Red";
    switch (m_laserColor) {
    case LaserColor::Red:   colorName = L"Laser: Red"; break;
    case LaserColor::Green: colorName = L"Laser: Emerald Green"; break;
    case LaserColor::Cyan:  colorName = L"Laser: Electric Cyan"; break;
    case LaserColor::Gold:  colorName = L"Laser: Amber Gold"; break;
    }
    m_hudToastText = colorName;
    m_hudToastTime = GetTickCount64();
    Render();
}

void AppWindow::Render() {
    auto* pTab = GetActiveTab();
    UpdateTabRenderInfos(m_cachedTabInfos);
    const auto& tabInfos = m_cachedTabInfos;

    m_docPropsInfo.visible = m_showProperties;
    m_docPropsInfo.hoveredBtn = m_propsHoveredBtn;
    m_docPropsInfo.copyFeedback = (m_propsCopiedFeedbackTime > 0 && (GetTickCount64() - m_propsCopiedFeedbackTime < 2000));

    float dipScale = 96.0f / m_renderer.GetDpi();
    LaserPointerRenderInfo laserInfo;
    laserInfo.active = m_isLaserActive;
    laserInfo.position = D2D1::Point2F((float)m_laserPos.x * dipScale, (float)m_laserPos.y * dipScale);
    laserInfo.color = m_laserColor;

    PresenterBarRenderInfo presenterBarInfo;
    presenterBarInfo.visible = m_isFullscreen && m_showPresenterBar;
    presenterBarInfo.hoveredBtn = m_presenterBarHoveredBtn;
    presenterBarInfo.currentPage = pTab ? pTab->currentPage : 0;
    presenterBarInfo.totalPages = pTab ? pTab->document.GetPageCount() : 0;
    presenterBarInfo.isLaserActive = m_isLaserActive;
    presenterBarInfo.laserColor = m_laserColor;

    if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
        std::wstring modeStr = L"";
        if (m_hudToastTime > 0 && (GetTickCount64() - m_hudToastTime < 1500)) {
            modeStr = m_hudToastText;
        } else if (pTab->zoomMode == ZoomMode::FitPage) {
            modeStr = L"Fit Page";
        } else if (pTab->zoomMode == ZoomMode::FitWidth) {
            modeStr = L"Fit Width";
        }

        auto selectionSpans = GetSelectionSpans();

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
                    float margin = 24.0f;
                    if (pageW <= dipW - margin * 2.0f) {
                        info.xOffset = (dipW - pageW) * 0.5f + pTab->offsetX;
                    } else {
                        info.xOffset = margin + pTab->offsetX;
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
                GetHelpInfo(),
                tabInfos,
                m_hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo,
                selectionSpans,
                m_dictCardInfo,
                laserInfo,
                presenterBarInfo
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
                GetHelpInfo(),
                tabInfos,
                m_hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo,
                selectionSpans,
                m_dictCardInfo,
                laserInfo,
                presenterBarInfo
            );
            return;
        }
    }

    m_renderer.RenderBlank(
        L"",
        GetHelpInfo(),
        tabInfos,
        m_hoveredAdd,
        m_showGoToPage,
        m_goToPageBuffer,
        GetSearchBarInfo(),
        m_docPropsInfo,
        m_dictCardInfo,
        laserInfo,
        presenterBarInfo
    );
}

HelpOverlayRenderInfo AppWindow::GetHelpInfo() const {
    HelpOverlayRenderInfo info;
    info.visible = m_showHelp;
    info.activeCategory = m_helpActiveCategory;
    info.hoveredCategory = m_helpHoveredCategory;
    info.hoveredClose = m_helpHoveredClose;
    return info;
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

static bool HitTestCharInPage(
    const PdfPageText& pageText,
    float pdfX,
    float pdfY,
    size_t& outCharIndex,
    bool& outAfterChar
) {
    if (pageText.chars.empty()) return false;

    // 1. Direct character bounding box containment test
    for (size_t i = 0; i < pageText.chars.size(); ++i) {
        const auto& c = pageText.chars[i];
        if (c.rect.right <= c.rect.left) continue;
        if (pdfY >= c.rect.top - 2.0f && pdfY <= c.rect.bottom + 2.0f) {
            if (pdfX >= c.rect.left && pdfX <= c.rect.right) {
                outCharIndex = i;
                outAfterChar = (pdfX > (c.rect.left + c.rect.right) * 0.5f);
                return true;
            }
        }
    }

    // 2. Line proximity test: Find line nearest to pdfY
    float closestLineDist = 1e9f;
    float bestLineY = 0.0f;
    for (const auto& c : pageText.chars) {
        if (c.rect.right <= c.rect.left) continue;
        float midY = (c.rect.top + c.rect.bottom) * 0.5f;
        float dist = std::abs(pdfY - midY);
        if (dist < closestLineDist) {
            closestLineDist = dist;
            bestLineY = midY;
        }
    }

    if (closestLineDist <= 24.0f) {
        float minX = 1e9f, maxX = -1e9f;
        size_t minIdx = 0, maxIdx = 0;
        size_t closestHorizIdx = 0;
        float closestHorizDist = 1e9f;

        for (size_t i = 0; i < pageText.chars.size(); ++i) {
            const auto& c = pageText.chars[i];
            if (c.rect.right <= c.rect.left) continue;
            float midY = (c.rect.top + c.rect.bottom) * 0.5f;
            if (std::abs(midY - bestLineY) <= 6.0f) {
                if (c.rect.left < minX) { minX = c.rect.left; minIdx = i; }
                if (c.rect.right > maxX) { maxX = c.rect.right; maxIdx = i; }

                float midX = (c.rect.left + c.rect.right) * 0.5f;
                float hDist = std::abs(pdfX - midX);
                if (hDist < closestHorizDist) {
                    closestHorizDist = hDist;
                    closestHorizIdx = i;
                }
            }
        }

        if (maxX >= minX) {
            if (pdfX <= minX) {
                outCharIndex = minIdx;
                outAfterChar = false;
                return true;
            } else if (pdfX >= maxX) {
                outCharIndex = maxIdx;
                outAfterChar = true;
                return true;
            } else {
                outCharIndex = closestHorizIdx;
                const auto& c = pageText.chars[closestHorizIdx];
                outAfterChar = (pdfX > (c.rect.left + c.rect.right) * 0.5f);
                return true;
            }
        }
    }

    return false;
}

void AppWindow::SetToolMode(ToolMode mode) {
    m_toolMode = mode;
    if (m_toolMode == ToolMode::Hand) {
        SetCursor(m_cursorHand);
        ShowToast(L"Hand Tool");
    } else {
        SetCursor(m_cursorArrow);
        ShowToast(L"Text Select Tool");
    }
    Render();
}

void AppWindow::ShowToast(const std::wstring& text) {
    m_hudToastText = text;
    m_hudToastTime = GetTickCount64();
    SetTimer(m_hwnd, 3, 1500, nullptr);
}

std::shared_ptr<PdfPageText> AppWindow::GetOrExtractPageText(DocumentTab* pTab, uint32_t pageIndex) {
    if (!pTab || !pTab->document.IsLoaded() || pageIndex >= pTab->document.GetPageCount()) {
        return nullptr;
    }
    if (!pTab->textCache) {
        pTab->textCache = std::make_shared<PageTextCache>();
        pTab->textCache->pages.resize(pTab->document.GetPageCount());
    }
    {
        std::lock_guard<std::mutex> lock(pTab->textCache->mutex);
        if (pageIndex < pTab->textCache->pages.size() && pTab->textCache->pages[pageIndex]) {
            return pTab->textCache->pages[pageIndex];
        }
    }

    if (!pTab->parser) {
        pTab->parser = std::make_unique<PdfParser>();
        pTab->parser->Load(pTab->document.GetFilePath());
    }

    PdfPageText pageText;
    if (pTab->parser->ExtractPageText(pageIndex, pageText)) {
        auto sharedPage = std::make_shared<PdfPageText>(std::move(pageText));
        std::lock_guard<std::mutex> lock(pTab->textCache->mutex);
        if (pageIndex < pTab->textCache->pages.size()) {
            pTab->textCache->pages[pageIndex] = sharedPage;
        }
        return sharedPage;
    }
    return nullptr;
}

bool AppWindow::HitTestPageText(const POINT& clientPt, uint32_t& outPage, size_t& outCharIndex, bool& outAfterChar) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return false;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipX = (float)clientPt.x * dipScale;
    float dipY = (float)clientPt.y * dipScale;
    float topOffset = GetTopOffset();
    if (dipY < topOffset) return false;

    if (pTab->continuousScroll) {
        float mouseY = dipY - topOffset;
        float docY = pTab->scrollY + mouseY;
        uint32_t count = pTab->document.GetPageCount();
        UpdateContinuousOffsets(pTab);
        const auto& offsets = pTab->pageOffsets;

        auto it = std::upper_bound(offsets.begin(), offsets.end(), docY);
        uint32_t pageIdx = 0;
        if (it != offsets.begin()) {
            pageIdx = static_cast<uint32_t>(std::distance(offsets.begin(), it) - 1);
        }
        if (pageIdx >= count) return false;

        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pageIdx);
        float pageW = pSize.width * pTab->zoom;
        float pageH = pSize.height * pTab->zoom;
        float dipW = (float)m_renderer.GetWidth() * dipScale;
        float margin = 24.0f;
        float pageX = (pageW <= dipW - margin * 2.0f) ? (dipW - pageW) * 0.5f + pTab->offsetX : margin + pTab->offsetX;
        float pageTopY = offsets[pageIdx] - pTab->scrollY;

        if (dipX < pageX - 20.0f || dipX > pageX + pageW + 20.0f) return false;
        if (mouseY < pageTopY - 10.0f || mouseY > pageTopY + pageH + 10.0f) return false;

        float pdfX = (dipX - pageX) / pTab->zoom;
        float pdfY = (mouseY - pageTopY) / pTab->zoom;
        pdfX = std::clamp(pdfX, 0.0f, pSize.width);
        pdfY = std::clamp(pdfY, 0.0f, pSize.height);

        auto pageText = GetOrExtractPageText(pTab, pageIdx);
        if (!pageText || pageText->chars.empty()) return false;

        outPage = pageIdx;
        return HitTestCharInPage(*pageText, pdfX, pdfY, outCharIndex, outAfterChar);
    } else {
        uint32_t pageIdx = pTab->currentPage;
        if (pageIdx >= pTab->document.GetPageCount()) return false;

        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pageIdx);
        float pageW = pSize.width * pTab->zoom;
        float pageH = pSize.height * pTab->zoom;
        float pageX = pTab->offsetX;
        float pageY = pTab->offsetY + topOffset;

        if (dipX < pageX - 20.0f || dipX > pageX + pageW + 20.0f) return false;
        if (dipY < pageY - 10.0f || dipY > pageY + pageH + 10.0f) return false;

        float pdfX = (dipX - pageX) / pTab->zoom;
        float pdfY = (dipY - pageY) / pTab->zoom;
        pdfX = std::clamp(pdfX, 0.0f, pSize.width);
        pdfY = std::clamp(pdfY, 0.0f, pSize.height);

        auto pageText = GetOrExtractPageText(pTab, pageIdx);
        if (!pageText || pageText->chars.empty()) return false;

        outPage = pageIdx;
        return HitTestCharInPage(*pageText, pdfX, pdfY, outCharIndex, outAfterChar);
    }
}

std::vector<SelectionHighlightSpan> AppWindow::GetSelectionSpans() const {
    std::vector<SelectionHighlightSpan> spans;
    const auto* pTab = GetActiveTab();
    if (!pTab || !pTab->selection.HasSelection()) return spans;

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = const_cast<AppWindow*>(this)->GetOrExtractPageText(const_cast<DocumentTab*>(pTab), p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        SelectionHighlightSpan span;
        span.pageIndex = p;

        D2D1_RECT_F curBand = { 0, 0, 0, 0 };
        bool hasBand = false;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.rect.right <= ch.rect.left || ch.rect.bottom <= ch.rect.top) {
                continue;
            }

            if (!hasBand) {
                curBand = ch.rect;
                hasBand = true;
            } else {
                bool sameLine = (std::abs(ch.rect.top - curBand.top) < 6.0f) &&
                                (std::abs(ch.rect.bottom - curBand.bottom) < 6.0f);
                bool adjacent = (ch.rect.left <= curBand.right + 12.0f);

                if (sameLine && adjacent) {
                    curBand.left = (std::min)(curBand.left, ch.rect.left);
                    curBand.right = (std::max)(curBand.right, ch.rect.right);
                    curBand.top = (std::min)(curBand.top, ch.rect.top);
                    curBand.bottom = (std::max)(curBand.bottom, ch.rect.bottom);
                } else {
                    span.rects.push_back(curBand);
                    curBand = ch.rect;
                }
            }
        }
        if (hasBand) {
            span.rects.push_back(curBand);
        }

        if (!span.rects.empty()) {
            spans.push_back(std::move(span));
        }
    }
    return spans;
}

void AppWindow::CopySelectionToClipboard() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->selection.HasSelection()) return;

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    std::wstring result;
    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = GetOrExtractPageText(pTab, p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        if (p > startPage && !result.empty()) {
            result += L"\r\n\r\n";
        }

        float lastY = -1.0f;
        float lastRight = -1.0f;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.ch == 0) continue;

            if (lastY >= 0.0f) {
                if (std::abs(ch.rect.top - lastY) > 8.0f) {
                    result += L"\r\n";
                    lastRight = -1.0f;
                } else if (lastRight >= 0.0f && (ch.rect.left - lastRight) > 6.0f) {
                    if (!result.empty() && result.back() != L' ') {
                        result += L' ';
                    }
                }
            }

            result += ch.ch;
            lastY = ch.rect.top;
            if (ch.rect.right > ch.rect.left) {
                lastRight = ch.rect.right;
            }
        }
    }

    if (result.empty()) return;

    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        size_t bytes = (result.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, result.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
        ShowToast(L"Copied to clipboard");
        Render();
    }
}

void AppWindow::ClampCanvasOffsets(DocumentTab* pTab) {
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipW = (float)m_renderer.GetWidth() * dipScale;
    float topOffset = GetTopOffset();
    float dipH = (float)m_renderer.GetHeight() * dipScale - topOffset;
    const float margin = 24.0f;

    if (pTab->continuousScroll) {
        // Continuous-scroll vertical clamping
        float totalH = GetTotalDocumentHeight(pTab);
        float maxScroll = std::max(0.0f, totalH - dipH);
        pTab->scrollY = std::clamp(pTab->scrollY, 0.0f, maxScroll);

        uint32_t oldPage = pTab->currentPage;
        pTab->currentPage = GetPageAtScrollOffset(pTab);
        if (pTab->currentPage != oldPage) {
            UpdateTitle();
        }

        // Continuous-scroll horizontal clamping
        float maxPageW = 0.0f;
        uint32_t count = pTab->document.GetPageCount();
        for (uint32_t i = 0; i < count; ++i) {
            float w = pTab->document.GetPageSize(i).width * pTab->zoom;
            if (w > maxPageW) maxPageW = w;
        }

        if (maxPageW <= dipW - margin * 2.0f) {
            pTab->offsetX = 0.0f;
        } else {
            float minOffsetX = dipW - maxPageW - margin * 2.0f;
            float maxOffsetX = 0.0f;
            pTab->offsetX = std::clamp(pTab->offsetX, minOffsetX, maxOffsetX);
        }
    } else {
        // Single-page clamping
        if (pTab->currentPage >= pTab->document.GetPageCount()) return;
        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
        if (pSize.width <= 0.0f || pSize.height <= 0.0f) return;

        float renderedW = pSize.width * pTab->zoom;
        float renderedH = pSize.height * pTab->zoom;

        // Horizontal clamping
        if (renderedW <= dipW - margin * 2.0f) {
            pTab->offsetX = (dipW - renderedW) * 0.5f;
        } else {
            float minOffsetX = dipW - renderedW - margin;
            float maxOffsetX = margin;
            pTab->offsetX = std::clamp(pTab->offsetX, minOffsetX, maxOffsetX);
        }

        // Vertical clamping
        if (renderedH <= dipH - margin * 2.0f) {
            pTab->offsetY = (dipH - renderedH) * 0.5f;
        } else {
            float minOffsetY = dipH - renderedH - margin;
            float maxOffsetY = margin;
            pTab->offsetY = std::clamp(pTab->offsetY, minOffsetY, maxOffsetY);
        }
    }
}

bool AppWindow::TriggerDictionaryLookup(const std::wstring& query, const D2D1_RECT_F& anchorRect) {
    if (query.empty()) return false;

    if (!m_dictEngine.IsLoaded()) {
        m_dictEngine.Initialize();
    }

    if (!m_dictEngine.IsLoaded()) {
        ShowToast(L"Dictionary not found (dict\\en-ar.dat)");
        return false;
    }

    DictionaryResult res;
    if (m_dictEngine.Lookup(query, res)) {
        m_dictCardInfo.visible = true;
        m_dictCardInfo.anchorRect = anchorRect;
        m_dictCardInfo.word = res.word;
        m_dictCardInfo.definition = res.definition;
        m_dictCardInfo.categoryTag = res.GetCategoryName();
        m_dictCardInfo.category = (uint16_t)res.category;

        // Precompute card bounds for hit-testing / click outside
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipWidth = (float)m_renderer.GetWidth() * dipScale;
        float dipHeight = (float)m_renderer.GetHeight() * dipScale;
        float topOffset = GetTopOffset();

        float approxDefHeight = (float)(res.definition.length() / 25 + 1) * 22.0f;
        approxDefHeight = (std::max)(32.0f, approxDefHeight);
        m_dictCardBounds = DictionaryCardLayout::CalculateCardRect(
            anchorRect, approxDefHeight, dipWidth, dipHeight, topOffset
        );

        Render();
        return true;
    } else {
        ShowToast(L"No dictionary entry found for \"" + query + L"\"");
        return false;
    }
}

void AppWindow::DismissDictionaryCard() {
    if (m_dictCardInfo.visible) {
        m_dictCardInfo.visible = false;
        Render();
    }
}

bool AppWindow::HitTestDictionaryCard(POINT pt) const {
    if (!m_dictCardInfo.visible) return false;
    float dipScale = 96.0f / m_renderer.GetDpi();
    float x = (float)pt.x * dipScale;
    float y = (float)pt.y * dipScale;
    return (x >= m_dictCardBounds.left && x <= m_dictCardBounds.right &&
            y >= m_dictCardBounds.top && y <= m_dictCardBounds.bottom);
}

std::wstring AppWindow::GetSelectedWordOrText(D2D1_RECT_F& outAnchorRect) {
    outAnchorRect = { 0, 0, 0, 0 };
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || !pTab->selection.HasSelection()) {
        return L"";
    }

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    std::wstring result;
    D2D1_RECT_F pageBounds = { 1e9f, 1e9f, -1e9f, -1e9f };
    bool hasBounds = false;
    uint32_t primaryPage = startPage;

    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = GetOrExtractPageText(pTab, p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.ch != 0) {
                if (!result.empty() && ch.rect.left > pageBounds.right + 4.0f && result.back() != L' ') {
                    result += L' ';
                }
                result += ch.ch;
            }
            if (ch.rect.right > ch.rect.left && ch.rect.bottom > ch.rect.top) {
                pageBounds.left = (std::min)(pageBounds.left, ch.rect.left);
                pageBounds.top = (std::min)(pageBounds.top, ch.rect.top);
                pageBounds.right = (std::max)(pageBounds.right, ch.rect.right);
                pageBounds.bottom = (std::max)(pageBounds.bottom, ch.rect.bottom);
                hasBounds = true;
            }
        }
    }

    if (result.empty() || !hasBounds) return L"";

    // Transform page bounds to screen DIPs
    float dipScale = 96.0f / m_renderer.GetDpi();
    float topOffset = GetTopOffset();

    if (pTab->continuousScroll) {
        UpdateContinuousOffsets(pTab);
        if (primaryPage < pTab->pageOffsets.size()) {
            D2D1_SIZE_F pSize = pTab->document.GetPageSize(primaryPage);
            float pageW = pSize.width * pTab->zoom;
            float dipW = (float)m_renderer.GetWidth() * dipScale;
            float margin = 24.0f;
            float pageX = (pageW <= dipW - margin * 2.0f) ? (dipW - pageW) * 0.5f + pTab->offsetX : margin + pTab->offsetX;
            float pageTopY = pTab->pageOffsets[primaryPage] - pTab->scrollY + topOffset;

            outAnchorRect.left = pageX + pageBounds.left * pTab->zoom;
            outAnchorRect.top = pageTopY + pageBounds.top * pTab->zoom;
            outAnchorRect.right = pageX + pageBounds.right * pTab->zoom;
            outAnchorRect.bottom = pageTopY + pageBounds.bottom * pTab->zoom;
        }
    } else {
        float pageX = pTab->offsetX;
        float pageY = pTab->offsetY + topOffset;
        outAnchorRect.left = pageX + pageBounds.left * pTab->zoom;
        outAnchorRect.top = pageY + pageBounds.top * pTab->zoom;
        outAnchorRect.right = pageX + pageBounds.right * pTab->zoom;
        outAnchorRect.bottom = pageY + pageBounds.bottom * pTab->zoom;
    }

    return result;
}

void AppWindow::CopyDictionaryDefinitionToClipboard() {
    if (!m_dictCardInfo.visible || m_dictCardInfo.definition.empty()) return;
    std::wstring text = m_dictCardInfo.word + L" \x2014 " + m_dictCardInfo.definition;

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
            }
        }
        CloseClipboard();
        ShowToast(L"Definition copied to clipboard");
    }
}


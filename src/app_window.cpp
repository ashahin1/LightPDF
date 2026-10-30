/**
 * @file app_window.cpp
 * @brief Implementation of top-level application window, message dispatch, tool controllers, and UI interaction pipelines.
 */

#include "app_window.hpp"
#include "pdf_parser.hpp"
#include "pdf_decryptor.hpp"
#include "common_utils.hpp"
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>
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
    if (m_settingsController.Get().keepRecentHistory) {
        for (size_t i = 0; i < m_tabController.GetTabCount(); ++i) {
            const auto* tab = m_tabController.GetTab(i);
            if (tab && tab->document.IsLoaded() && !tab->document.GetFilePath().empty()) {
                m_recentController.UpdateLastPage(tab->document.GetFilePath(), tab->currentPage);
            }
        }
        m_recentController.SaveToRegistry();
    }
    m_settingsController.SaveToRegistry();

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
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
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
    m_selectionController.GetDictEngine().Initialize();

    // Load persistent user settings and recent files history from registry
    m_settingsController.LoadFromRegistry();
    ApplySettingsToSession();
    if (m_settingsController.Get().keepRecentHistory) {
        m_recentController.LoadFromRegistry();
    }

    if (!initialFile.empty()) {
        OpenTab(initialFile);
    }
    if (m_tabController.IsEmpty()) {
        UpdateTitle();
        auto recentInfo = m_recentController.GetRenderInfo();
        m_renderer.RenderBlank(L"", {}, {}, false, false, L"", {}, {}, {}, {}, {}, {}, &recentInfo);
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
        if (pCds && pCds->dwData == 1 && pCds->lpData && pCds->cbData >= sizeof(wchar_t)) {
            size_t numChars = pCds->cbData / sizeof(wchar_t);
            const wchar_t* pRawChars = static_cast<const wchar_t*>(pCds->lpData);
            std::wstring filePath(pRawChars, numChars);
            while (!filePath.empty() && filePath.back() == L'\0') {
                filePath.pop_back();
            }
            if (!filePath.empty()) {
                OpenTab(filePath);
            }
        }
        return TRUE;
    }

    case WM_APP_OPEN_FILE: {
        std::vector<std::wstring> filesToOpen;
        {
            std::lock_guard<std::mutex> lock(m_pendingOpenFilesMutex);
            filesToOpen.swap(m_pendingOpenFiles);
        }
        for (const auto& path : filesToOpen) {
            if (!path.empty()) {
                OpenTab(path);
            }
        }
        return 0;
    }

    case WM_APP_SEARCH_UPDATE: {
        InvalidateSearchHighlights();
        int activeIdx = m_searchController.GetEngine().GetActiveMatchIndex();
        if (activeIdx >= 0 && activeIdx != m_searchController.GetLastJumpedMatch()) {
            m_searchController.SetLastJumpedMatch(activeIdx);
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

    case WM_APP_BAKE_PDF_START: {
        std::vector<PendingBakeRequest> bakesToProcess;
        {
            std::lock_guard<std::mutex> lock(m_pendingBakeMutex);
            bakesToProcess.swap(m_pendingBakes);
        }
        for (const auto& req : bakesToProcess) {
            if (!req.targetPath.empty()) {
                StartBakingSearchablePdf(req.targetPath, req.overwriteOriginal);
            }
        }
        return 0;
    }

    case WM_APP_BAKE_PDF_PROGRESS: {
        uint32_t cur = (uint32_t)wParam;
        uint32_t tot = (uint32_t)lParam;
        if (cur == 0) {
            ShowToast(L"Writing searchable PDF to disk...");
        } else {
            wchar_t progText[128];
            swprintf_s(progText, L"Baking Searchable PDF... Page %u of %u (Esc to cancel)", cur, tot);
            ShowToast(progText);
        }
        Render();
        return 0;
    }

    case WM_APP_BAKE_PDF_DONE: {
        m_isBakingPdf = false;
        int status = (int)wParam; // 1 = success, 0 = error, 2 = cancelled, 3 = already digital
        bool wasOverwrite = (lParam != 0);
        std::wstring targetPath = m_activeBakeTargetPath;
        if (status == 1) {
            if (!targetPath.empty() && ReloadOrSwitchActiveTabToFile(targetPath)) {
                if (wasOverwrite) {
                    ShowToast(L"Searchable PDF saved! (Original file updated)");
                } else {
                    ShowToast(L"Searchable PDF saved and opened!");
                }
            } else {
                ShowToast(L"Searchable PDF saved successfully!");
            }
        } else if (status == 2) {
            ShowToast(L"Searchable PDF generation cancelled.");
        } else if (status == 3) {
            if (wasOverwrite) {
                ShowToast(L"Document already contains digital text on all pages.");
            } else {
                if (!targetPath.empty()) {
                    ReloadOrSwitchActiveTabToFile(targetPath);
                }
                ShowToast(L"PDF saved successfully! (Document is already digital)");
            }
        } else {
            ShowToast(L"Failed to save searchable PDF.");
        }
        Render();
        return 0;
    }

    case WM_APP_SAVE_PDF_DONE: {
        std::vector<PendingSaveCompletion> completions;
        {
            std::lock_guard<std::mutex> lock(m_pendingSaveDoneMutex);
            completions.swap(m_pendingSaveCompletions);
        }
        for (const auto& comp : completions) {
            if (comp.success && !comp.targetPath.empty()) {
                ReloadOrSwitchActiveTabToFile(comp.targetPath, comp.password);
            }
            if (!comp.toastMessage.empty()) {
                ShowToast(comp.toastMessage);
            }
        }
        Render();
        return 0;
    }

    case WM_APP_TTS_EVENT: {
        if (m_readAloudController.OnTtsEvent()) {
            if (m_readAloudController.NeedsNextPage()) {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->document.IsLoaded()) {
                    uint32_t nextPage = m_readAloudController.GetCurrentPage() + 1;
                    if (nextPage < pTab->document.GetPageCount()) {
                        GoToPage(nextPage);
                        auto pageText = GetOrExtractPageText(pTab, nextPage);
                        if (pageText && !pageText->hasDigitalText && pageText->fullText.empty()) {
                            auto engines = CreateBilingualOcrEngines();
                            if (engines.IsValid()) {
                                auto page = pTab->document.GetPage(nextPage);
                                auto pSize = pTab->document.GetPageSize(nextPage);
                                OcrPageItem ocrItem;
                                if (ExtractBilingualPageOcr(page, nextPage, pSize.width, pSize.height, engines.arEngine, engines.enEngine, ocrItem)) {
                                    PopulatePageTextFromOcr(ocrItem, *pageText);
                                }
                            }
                        }
                        m_readAloudController.ContinueWithPage(pageText, nextPage);
                    } else {
                        m_readAloudController.Stop();
                        ShowToast(L"Read Aloud: Finished document");
                    }
                }
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
                if (SupportedFormats::IsSupported(droppedPath)) {
                    OpenTab(droppedPath);
                }
            }
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_SETCURSOR:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_LBUTTONDBLCLK:
        return HandleMouseEvent(msg, wParam, lParam);

    case WM_RBUTTONDOWN: {
        if (m_isPanning) {
            m_isPanning = false;
            ReleaseCapture();
            SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
        }
        auto* pTab = GetActiveTab();
        if (pTab && pTab->selection.isDragging) {
            pTab->selection.isDragging = false;
            ReleaseCapture();
        }
        if (m_isDraggingScrollbar) {
            m_isDraggingScrollbar = false;
            ReleaseCapture();
        }
        break;
    }

    case WM_CONTEXTMENU: {
        POINT ptScreen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (ptScreen.x == -1 && ptScreen.y == -1) {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            POINT ptClient = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
            ptScreen = ptClient;
            ClientToScreen(m_hwnd, &ptScreen);
        }
        HandleContextMenu(ptScreen);
        return 0;
    }

    case WM_XBUTTONDOWN: {
        if (HIWORD(wParam) == XBUTTON1) {
            NavigateBack();
            return TRUE;
        } else if (HIWORD(wParam) == XBUTTON2) {
            NavigateForward();
            return TRUE;
        }
        break;
    }

    case WM_SYSKEYDOWN: {
        if (wParam == VK_LEFT) {
            NavigateBack();
            return 0;
        } else if (wParam == VK_RIGHT) {
            NavigateForward();
            return 0;
        }
        if (wParam >= '1' && wParam <= '9') {
            size_t targetIndex = (size_t)(wParam - '1');
            if (targetIndex < m_tabController.GetTabCount()) {
                SelectTab(targetIndex);
                return 0;
            }
        }
        break;
    }

    case WM_KEYDOWN:
        return HandleKeyDown(wParam, lParam);

    case WM_KEYUP:
        if (wParam == VK_SPACE && !m_isPanning) {
            SetCursor((m_toolMode == ToolMode::Hand) ? m_cursorHand : m_cursorArrow);
        }
        return 0;

    case WM_CHAR:
        if (m_showPasswordOverlay) {
            if (wParam >= 32 && wParam != 127) {
                if (m_passwordInput.size() < 256) {
                    m_passwordInput.push_back((wchar_t)wParam);
                    m_passwordError.clear();
                    Render();
                }
            }
            return 0;
        }
        if (m_showProperties) {
            return 0;
        }
        if (m_showGoToPage) {
            return 0;
        }
        if (m_searchController.IsOpen()) {
            if (wParam == VK_BACK) {
                if (!m_searchController.GetQuery().empty()) {
                    m_searchController.PopQueryChar();
                    ScheduleSearchDebounce();
                }
                return 0;
            } else if (wParam >= 32 && wParam != 127) {
                m_searchController.AppendQueryChar((wchar_t)wParam);
                ScheduleSearchDebounce();
                return 0;
            } else if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
                return 0;
            }
        }
        break;

    case WM_ERASEBKGND:
        return 1; // Direct2D handles entire background, avoid flicker

    case WM_ENDSESSION:
        if (wParam) {
            if (m_settingsController.Get().keepRecentHistory) {
                for (size_t i = 0; i < m_tabController.GetTabCount(); ++i) {
                    const auto* tab = m_tabController.GetTab(i);
                    if (tab && tab->document.IsLoaded() && !tab->document.GetFilePath().empty()) {
                        m_recentController.UpdateLastPage(tab->document.GetFilePath(), tab->currentPage);
                    }
                }
                m_recentController.SaveToRegistry();
            }
            m_settingsController.SaveToRegistry();
        }
        return 0;

    case WM_DESTROY:
        if (m_settingsController.Get().keepRecentHistory) {
            for (size_t i = 0; i < m_tabController.GetTabCount(); ++i) {
                const auto* tab = m_tabController.GetTab(i);
                if (tab && tab->document.IsLoaded() && !tab->document.GetFilePath().empty()) {
                    m_recentController.UpdateLastPage(tab->document.GetFilePath(), tab->currentPage);
                }
            }
            m_recentController.SaveToRegistry();
        }
        m_settingsController.SaveToRegistry();
        m_renderer.Cleanup();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
LRESULT AppWindow::HandleMouseEvent(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEWHEEL: {
        if (m_showProperties || m_showOptions) return 0;
        if (m_isDraggingScrollbar) return 0;
        auto* pTab = GetActiveTab();
        if (!pTab || !pTab->document.IsLoaded()) return 0;

        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (isCtrlDown) {
            float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &pt);
            AdjustZoom(factor, pt);
        } else if (isShiftDown) {
            ScrollHorizontal((float)delta * 0.6f);
        } else {
            ShowScrollbar();
    

        if (pTab->continuousScroll) {
                ScrollContinuous((float)delta * 0.6f);
            } else {
                ScrollSinglePage((float)delta * 0.6f);
            }
        }
        return 0;
    }

    case WM_MOUSEHWHEEL: {
        if (m_showProperties || m_showOptions) return 0;
        if (m_isDraggingScrollbar) return 0;
        auto* pTab = GetActiveTab();
        if (!pTab || !pTab->document.IsLoaded()) return 0;

        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollHorizontal((float)delta * 0.6f);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipX = (float)pt.x * dipScale;
        float dipY = (float)pt.y * dipScale;
        float topOffset = GetTopOffset();

        // Route click to active modal overlay if any
        if (HandleModalMouseDown(msg, pt, dipScale, dipX, dipY)) {
            return 0;
        }

        // 2. Tab Bar clicks
        if (m_tabController.HasMultipleTabs() && dipY < topOffset) {
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
        if (m_searchController.IsOpen()) {
            int searchHit = HitTestSearchBar(pt);
            if (msg == WM_LBUTTONDOWN && searchHit > 0) {
                if (searchHit == 1) { // Prev
                    if (m_searchController.IsDebouncePending() || m_searchController.GetEngine().GetCurrentQuery() != m_searchController.GetQuery()) {
                        TriggerSearch();
                    } else if (m_searchController.PrevMatch()) {
                        JumpToActiveMatch();
                        Render();
                    }
                } else if (searchHit == 2) { // Next
                    if (m_searchController.IsDebouncePending() || m_searchController.GetEngine().GetCurrentQuery() != m_searchController.GetQuery()) {
                        TriggerSearch();
                    } else if (m_searchController.NextMatch()) {
                        JumpToActiveMatch();
                        Render();
                    }
                } else if (searchHit == 3) { // Match Case
                    m_searchController.ToggleMatchCase();
                    TriggerSearch();
                    Render();
                } else if (searchHit == 4) { // OCR
                    m_searchController.ToggleOcr();
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

            // Left click: Check if clicking an interactive hyperlink
            uint32_t linkPage = 0;
            PdfLink hitLink;
            bool isSpaceDown = (GetKeyState(VK_SPACE) & 0x8000) != 0;
            if (!isSpaceDown && m_selectionController.HitTestPageLink(pTab, pt, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), GetTopOffset(), linkPage, hitLink)) {
                m_isLinkMouseDown = true;
                m_linkMouseDownPt = pt;
                m_mouseDownLink = hitLink;
            }

            // Left click: Check if Hand mode or holding spacebar
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
        } else if (m_tabController.IsEmpty() && !m_showHelp && !m_showProperties && !m_showGoToPage && !m_searchController.IsOpen()) {
            if (msg == WM_LBUTTONDOWN && !m_recentController.IsEmpty()) {
                float dipW = (float)m_renderer.GetWidth() * dipScale;
                float dipH = (float)m_renderer.GetHeight() * dipScale;
                bool outRemove = false;
                int hit = RecentFilesController::HitTestDashboard(
                    D2D1::Point2F(dipX, dipY), dipW, dipH, m_recentController.GetCount(), outRemove);
                if (hit >= 0) {
                    if (outRemove) {
                        m_recentController.RemoveRecentFile(static_cast<size_t>(hit));
                        Render();
                        return 0;
                    } else {
                        m_justOpenedRecent = true;
                        OpenRecentFile(static_cast<size_t>(hit));
                        return 0;
                    }
                }
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

        // 000. Password prompt hover detection
        if (m_showPasswordOverlay) {
            int hit = m_renderer.HitTestPasswordOverlay(pt);
            int newBtn = (hit >= 1 && hit <= 4) ? hit : 0;
            if (newBtn != m_passwordHoveredBtn) {
                m_passwordHoveredBtn = newBtn;
                Render();
            }
            if (newBtn >= 1 && newBtn <= 4) {
                SetCursor(m_cursorHand);
            } else if (hit == 5) {
                SetCursor(m_cursorIBeam);
            } else {
                SetCursor(m_cursorArrow);
            }
            return 0;
        }

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

        // 00a2. Read Aloud bar hover detection
        if (m_readAloudController.IsActive()) {
            int ttsHit = m_renderer.HitTestTtsBar(pt, m_tabController.GetTabCount() > 1);
            int newBtn = (ttsHit >= 0 && ttsHit <= 4) ? ttsHit : -1;
            m_readAloudController.SetBarHoveredBtn(newBtn);
            if (newBtn >= 0) {
                SetCursor(m_cursorHand);
                Render();
                return 0;
            }
        }

        // 00b. Laser pointer tracking
        if (m_isLaserActive) {
            m_laserPos = pt;
            SetCursor(NULL);
            Render();
            return 0;
        }

        // 00c. Options Overlay hover detection
        if (m_showOptions) {
            int hit = m_renderer.HitTestOptionsOverlay(pt, m_optionsActiveCategory);
            int newHov = (hit > 0) ? hit : 0;
            if (newHov != m_optionsHoveredId) {
                m_optionsHoveredId = newHov;
                Render();
            }
            SetCursor((hit > 0) ? m_cursorHand : m_cursorArrow);
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

        // 0c. Quick Switcher hover detection
        if (m_recentController.IsSwitcherOpen()) {
            float dipH = (float)m_renderer.GetHeight() * dipScale;
            bool outCardClicked = false;
            int hit = RecentFilesController::HitTestSwitcher(
                D2D1::Point2F(dipX, dipY), dipW, dipH, m_recentController.GetCount(), outCardClicked);
            int prevHover = m_recentController.GetRenderInfo().switcherHoveredIndex;
            if (hit != prevHover) {
                m_recentController.SetSwitcherHoveredIndex(hit);
                Render();
            }
            if (hit >= 0) {
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
        if (m_searchController.IsOpen()) {
            int hit = HitTestSearchBar(pt);
            int newBtn = (hit > 0) ? hit : 0;
            if (newBtn != m_searchController.GetHoveredButton()) {
                m_searchController.SetHoveredButton(newBtn);
                Render();
            }
            if (hit > 0) {
                SetCursor(m_cursorHand);
            } else if (hit == 0) {
                SetCursor(m_cursorIBeam);
            }
        } else if (m_searchController.GetHoveredButton() != 0) {
            m_searchController.SetHoveredButton(0);
        }

        // 3. Tab Bar hover
        if (m_tabController.HasMultipleTabs() && dipY < topOffset) {
            bool outClose = false;
            bool outAdd = false;
            int hit = HitTestTab(pt, outClose, outAdd);

            bool changed = (hit != m_tabController.GetHoveredTab()) ||
                           (outClose != m_tabController.IsHoveredClose()) ||
                           (outAdd != m_tabController.IsHoveredAdd());
            m_tabController.SetHover(hit, outClose, outAdd);

            if (changed) {
                Render();
            }
            SetCursor(m_cursorArrow);
        } else {
            if (m_tabController.GetHoveredTab() != -1 || m_tabController.IsHoveredClose() || m_tabController.IsHoveredAdd()) {
                m_tabController.SetHover(-1, false, false);
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
            if (isSpaceDown) {
                if (m_hasHoveredLink) {
                    ClearHoveredLink();
                    Render();
                }
                SetCursor(m_cursorHand);
            } else {
                uint32_t linkPage = 0;
                PdfLink hitLink;
                if (m_selectionController.HitTestPageLink(pTab, pt, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), topOffset, linkPage, hitLink)) {
                    bool changed = (!m_hasHoveredLink || m_hoveredLinkPage != linkPage ||
                                    m_hoveredLink.uri != hitLink.uri ||
                                    m_hoveredLink.targetPage != hitLink.targetPage ||
                                    m_hoveredLink.rect.left != hitLink.rect.left ||
                                    m_hoveredLink.rect.top != hitLink.rect.top);
                    m_hasHoveredLink = true;
                    m_hoveredLinkPage = linkPage;
                    m_hoveredLink = hitLink;
                    SetCursor(m_cursorHand);
                    if (changed) {
                        Render();
                    }
                } else {
                    if (m_hasHoveredLink) {
                        ClearHoveredLink();
                        Render();
                    }
                    if (m_toolMode == ToolMode::Hand) {
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
            }
        } else if (m_tabController.IsEmpty() && !m_showHelp && !m_showProperties && !m_showGoToPage && !m_searchController.IsOpen()) {
            if (!m_recentController.IsEmpty()) {
                float dipH = (float)m_renderer.GetHeight() * dipScale;
                bool outRemove = false;
                int hit = RecentFilesController::HitTestDashboard(
                    D2D1::Point2F(dipX, dipY), dipW, dipH, m_recentController.GetCount(), outRemove);
                auto rInfo = m_recentController.GetRenderInfo();
                int prevHover = rInfo.dashboardHoveredIndex;
                int prevRem = rInfo.dashboardHoveredRemove;
                int newRem = outRemove ? hit : -1;
                if (hit != prevHover || newRem != prevRem) {
                    m_recentController.SetDashboardHoveredIndex(hit, newRem);
                    Render();
                }
                if (hit >= 0) {
                    SetCursor(m_cursorHand);
                } else {
                    SetCursor(m_cursorArrow);
                }
            } else {
                SetCursor(m_cursorArrow);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_MBUTTONUP: {
        auto* pTab = GetActiveTab();
        if (msg == WM_LBUTTONUP && m_isLinkMouseDown) {
            m_isLinkMouseDown = false;
            float dipScale = 96.0f / m_renderer.GetDpi();
            float dx = (float)(GET_X_LPARAM(lParam) - m_linkMouseDownPt.x) * dipScale;
            float dy = (float)(GET_Y_LPARAM(lParam) - m_linkMouseDownPt.y) * dipScale;
            if ((dx * dx + dy * dy) <= 16.0f) {
                if (pTab && !pTab->selection.HasSelection()) {
                    pTab->selection.Clear();
                }
                if (m_isPanning) {
                    m_isPanning = false;
                    ReleaseCapture();
                }
                ActivatePdfLink(m_mouseDownLink);
                return 0;
            }
        }
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
        if (m_justOpenedRecent) {
            m_justOpenedRecent = false;
            return 0;
        }
        if (m_showProperties || m_showOptions) return 0;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float topOffset = GetTopOffset();
        float dipScale = 96.0f / m_renderer.GetDpi();
        float dipY = (float)pt.y * dipScale;
        if (m_tabController.HasMultipleTabs() && dipY < topOffset) {
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
                    if (m_selectionController.SelectWordAtChar(pTab, hitPage, hitChar)) {
                        if (m_settingsController.Get().doubleClickLookup) {
                            D2D1_RECT_F anchorRect = { 0, 0, 0, 0 };
                            std::wstring selectedWord = GetSelectedWordOrText(anchorRect);
                            if (!selectedWord.empty()) {
                                TriggerDictionaryLookup(selectedWord, anchorRect);
                            }
                        }
                        Render();
                        return 0;
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
                m_settingsController.SetDefaultZoomMode((newMode == ZoomMode::FitWidth) ? 1 : 0);
                ApplySettingsToSession();

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

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

bool AppWindow::HandleModalMouseDown(UINT msg, POINT pt, float dipScale, float dipX, float dipY) {
    if (msg == WM_LBUTTONDOWN && m_showPasswordOverlay) {
        int pwdHit = m_renderer.HitTestPasswordOverlay(pt);
        if (pwdHit == 1 || pwdHit == 4 || pwdHit == -1) {
            CancelPasswordPrompt();
            return true;
        } else if (pwdHit == 2) {
            m_passwordShowPlain = !m_passwordShowPlain;
            Render();
            return true;
        } else if (pwdHit == 3) {
            SubmitPasswordPrompt();
            return true;
        }
        return true;
    }

    if (msg == WM_LBUTTONDOWN && m_readAloudController.IsActive()) {
        int ttsHit = m_renderer.HitTestTtsBar(pt, m_tabController.GetTabCount() > 1);
        if (ttsHit >= 0 && ttsHit <= 4) {
            switch (ttsHit) {
            case 0: m_readAloudController.SkipBackward(); break;
            case 1:
                m_readAloudController.TogglePause();
                ShowToast(m_readAloudController.IsPaused() ? L"⏸ Paused" : L"▶ Resumed");
                break;
            case 2: m_readAloudController.SkipForward(); break;
            case 3:
                m_readAloudController.SpeedUp();
                ShowToast(L"Speed: " + m_readAloudController.GetRateLabel());
                break;
            case 4: StopReadAloud(); break;
            }
            Render();
            return true;
        } else if (ttsHit == 100) {
            return true;
        }
    }

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
            return true;
        } else if (presHit == 100) {
            return true;
        }
    }

    if (m_showOptions) {
        if (msg == WM_LBUTTONDOWN) {
            int optHit = m_renderer.HitTestOptionsOverlay(pt, m_optionsActiveCategory);
            HandleOptionsClick(optHit);
        }
        return true;
    }

    if (m_showProperties) {
        int propHit = m_renderer.HitTestDocumentProperties(pt);
        if (propHit == 1 || propHit == 3 || propHit == -1) {
            CloseDocumentProperties();
            return true;
        } else if (propHit == 2) {
            CopyPropertiesToClipboard();
            return true;
        }
        return true;
    }

    if (m_selectionController.IsDictCardVisible() && !HitTestDictionaryCard(pt)) {
        DismissDictionaryCard();
    }

    if (m_recentController.IsSwitcherOpen()) {
        if (msg == WM_LBUTTONDOWN) {
            float dipW = (float)m_renderer.GetWidth() * dipScale;
            float dipH = (float)m_renderer.GetHeight() * dipScale;
            bool outCardClicked = false;
            int hit = RecentFilesController::HitTestSwitcher(
                D2D1::Point2F(dipX, dipY), dipW, dipH, m_recentController.GetCount(), outCardClicked);
            if (!outCardClicked) {
                m_recentController.CloseSwitcher();
                Render();
                return true;
            } else if (hit >= 0) {
                m_recentController.CloseSwitcher();
                OpenRecentFile(static_cast<size_t>(hit));
                return true;
            }
        }
        return true;
    }

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
        return true;
    }

    return false;
}

bool AppWindow::HandleModalKeyDown(WPARAM wParam, bool isCtrlDown, bool isShiftDown) {
    if (m_showPasswordOverlay) {
        if (wParam == VK_ESCAPE) {
            CancelPasswordPrompt();
            return true;
        } else if (wParam == VK_RETURN) {
            SubmitPasswordPrompt();
            return true;
        } else if (wParam == VK_BACK) {
            if (isCtrlDown) {
                if (!m_passwordInput.empty()) {
                    SecureZeroMemory(m_passwordInput.data(), m_passwordInput.size() * sizeof(wchar_t));
                    m_passwordInput.clear();
                }
                m_passwordError.clear();
                Render();
            } else if (!m_passwordInput.empty()) {
                m_passwordInput.back() = 0;
                m_passwordInput.pop_back();
                m_passwordError.clear();
                Render();
            }
            return true;
        } else if (wParam == 'V' && isCtrlDown) {
            std::wstring clip = ClipboardUtils::GetText(m_hwnd);
            if (!clip.empty()) {
                for (wchar_t ch : clip) {
                    if (m_passwordInput.size() >= 256) break;
                    if (ch != L'\r' && ch != L'\n') {
                        m_passwordInput.push_back(ch);
                    }
                }
                m_passwordError.clear();
                Render();
            }
            return true;
        }
        return true;
    }

    if (m_selectionController.IsDictCardVisible()) {
        if (wParam == VK_ESCAPE) {
            DismissDictionaryCard();
            return true;
        } else if (wParam == 'C' && isCtrlDown) {
            CopyDictionaryDefinitionToClipboard();
            return true;
        }
    }

    if (m_showOptions) {
        if (wParam == VK_ESCAPE || wParam == VK_RETURN || (wParam == VK_OEM_COMMA && isCtrlDown)) {
            CloseOptionsOverlay();
            return true;
        } else if (wParam >= '1' && wParam <= '3' && !isCtrlDown) {
            m_optionsActiveCategory = (int)(wParam - '1');
            Render();
            return true;
        } else if (wParam == VK_TAB) {
            m_optionsActiveCategory = isShiftDown
                ? ((m_optionsActiveCategory + 2) % 3)
                : ((m_optionsActiveCategory + 1) % 3);
            Render();
            return true;
        } else if (wParam == VK_LEFT) {
            m_optionsActiveCategory = (m_optionsActiveCategory + 2) % 3;
            Render();
            return true;
        } else if (wParam == VK_RIGHT) {
            m_optionsActiveCategory = (m_optionsActiveCategory + 1) % 3;
            Render();
            return true;
        }
        return true;
    }

    if (m_showProperties) {
        if (wParam == VK_ESCAPE || wParam == VK_RETURN || (wParam == 'D' && isCtrlDown)) {
            CloseDocumentProperties();
            return true;
        } else if (wParam == 'C' && isCtrlDown) {
            CopyPropertiesToClipboard();
            return true;
        }
        return true;
    }

    if (m_showHelp) {
        if (wParam == VK_ESCAPE || wParam == VK_F1) {
            m_showHelp = false;
            Render();
            return true;
        } else if (wParam >= '1' && wParam <= '5' && !isCtrlDown) {
            m_helpActiveCategory = (int)(wParam - '1');
            Render();
            return true;
        } else if (wParam == VK_TAB) {
            m_helpActiveCategory = isShiftDown ? ((m_helpActiveCategory + 4) % 5) : ((m_helpActiveCategory + 1) % 5);
            Render();
            return true;
        } else if (wParam == VK_LEFT) {
            m_helpActiveCategory = (m_helpActiveCategory + 4) % 5;
            Render();
            return true;
        } else if (wParam == VK_RIGHT) {
            m_helpActiveCategory = (m_helpActiveCategory + 1) % 5;
            Render();
            return true;
        }
        return true;
    }

    if (m_recentController.IsSwitcherOpen()) {
        if (wParam == VK_ESCAPE) {
            m_recentController.CloseSwitcher();
            Render();
            return true;
        } else if (wParam == VK_UP) {
            m_recentController.SelectPrevSwitcherItem();
            Render();
            return true;
        } else if (wParam == VK_DOWN) {
            m_recentController.SelectNextSwitcherItem();
            Render();
            return true;
        } else if (wParam == VK_RETURN) {
            int sel = m_recentController.GetSwitcherSelectedIndex();
            m_recentController.CloseSwitcher();
            if (sel >= 0) {
                OpenRecentFile(static_cast<size_t>(sel));
            } else {
                Render();
            }
            return true;
        } else if (wParam == VK_DELETE) {
            int sel = m_recentController.GetSwitcherSelectedIndex();
            if (sel >= 0) {
                m_recentController.RemoveRecentFile(static_cast<size_t>(sel));
                if (m_recentController.IsEmpty()) {
                    m_recentController.CloseSwitcher();
                } else {
                    m_recentController.SetSwitcherSelectedIndex((std::min)(sel, static_cast<int>(m_recentController.GetCount() - 1)));
                }
                Render();
            }
            return true;
        } else if (wParam >= '1' && wParam <= '9' && !isCtrlDown) {
            size_t idx = static_cast<size_t>(wParam - '1');
            m_recentController.CloseSwitcher();
            OpenRecentFile(idx);
            return true;
        }
        return true;
    }

    if (m_showGoToPage) {
        if (wParam >= '0' && wParam <= '9') {
            if (m_goToPageBuffer.size() < 6) {
                m_goToPageBuffer.push_back((wchar_t)wParam);
                Render();
            }
            return true;
        } else if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
            if (m_goToPageBuffer.size() < 6) {
                m_goToPageBuffer.push_back(L'0' + (wchar_t)(wParam - VK_NUMPAD0));
                Render();
            }
            return true;
        } else if (wParam == VK_BACK) {
            if (!m_goToPageBuffer.empty()) {
                m_goToPageBuffer.pop_back();
                Render();
            }
            return true;
        } else if (wParam == VK_RETURN) {
            if (!m_goToPageBuffer.empty()) {
                try {
                    long p = std::stol(m_goToPageBuffer);
                    auto* pTab = GetActiveTab();
                    if (pTab && pTab->document.IsLoaded()) {
                        uint32_t total = pTab->document.GetPageCount();
                        if (p >= 1 && (uint32_t)p <= total) {
                            PushNavHistory(pTab);
                            pTab->navForwardStack.clear();
                            GoToPage((uint32_t)(p - 1));
                        }
                    }
                } catch (...) {}
            }
            m_showGoToPage = false;
            m_goToPageBuffer.clear();
            Render();
            return true;
        } else if (wParam == VK_ESCAPE) {
            m_showGoToPage = false;
            m_goToPageBuffer.clear();
            Render();
            return true;
        }
        return true;
    }

    return false;
}

LRESULT AppWindow::HandleKeyDown(WPARAM wParam, [[maybe_unused]] LPARAM lParam) {
    if (m_isDraggingScrollbar) return 0;

    bool isCtrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool isShiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (HandleModalKeyDown(wParam, isCtrlDown, isShiftDown)) {
        return 0;
    }

        switch (wParam) {
        case VK_OEM_COMMA:
            if (isCtrlDown) {
                ToggleOptionsOverlay();
                return 0;
            }
            break;
        case 'I':
            if (isCtrlDown) {
                PageTheme newTheme = m_settingsController.CyclePageTheme();
                m_renderer.SetPageTheme(newTheme);
                ShowToast(std::wstring(L"Page Theme: ") + SettingsController::GetThemeName(newTheme));
                Render();
                return 0;
            }
            break;
        case 'F':
            if (isCtrlDown) {
                auto* pTab = GetActiveTab();
                if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
                    auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
                    if (pageText && !pageText->hasDigitalText && m_settingsController.Get().autoOcrSearch) {
                        m_searchController.SetOcrEnabled(true);
                    }
                    m_searchController.Open();
                    m_showGoToPage = false;
                    m_showHelp = false;
                    if (!m_searchController.GetQuery().empty()) {
                        TriggerSearch();
                    }
                    Render();
                }
                return 0;
            }
            break;
        case 'H':
            if (isCtrlDown) {
                m_recentController.ToggleSwitcher();
                Render();
                return 0;
            } else if (!m_searchController.IsOpen() && !m_showGoToPage) {
                if (m_isLaserActive) {
                    SetToolMode(ToolMode::Hand);
                } else {
                    SetToolMode((m_toolMode == ToolMode::Hand) ? ToolMode::TextSelect : ToolMode::Hand);
                }
                return 0;
            }
            break;
        case 'A':
            if (isCtrlDown) {
                SelectAll();
                return 0;
            }
            break;
        case 'L':
            if (!isCtrlDown && !m_searchController.IsOpen() && !m_showGoToPage) {
                ToggleLaserPointer();
                return 0;
            }
            break;
        case 'S':
            if (isCtrlDown) {
                auto* pTab = GetActiveTab();
                if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
                    ShowToast(L"No document is currently loaded.");
                } else if (isShiftDown) {
                    PromptSaveAsPdf();
                } else if (pTab->document.IsPasswordProtected() || (pTab->parser && pTab->parser->IsEncrypted())) {
                    PromptSaveDecryptedPdf();
                } else if (DoesDocumentNeedOcr(pTab)) {
                    PromptSaveSearchablePdf(false);
                } else {
                    PromptSaveAsPdf();
                }
                return 0;
            } else if (!m_searchController.IsOpen() && !m_showGoToPage) {
                SetToolMode(ToolMode::TextSelect);
                return 0;
            }
            break;
        case 'V':
            if (isCtrlDown && m_searchController.IsOpen()) {
                std::wstring clip = ClipboardUtils::GetText(m_hwnd);
                if (!clip.empty()) {
                    for (wchar_t ch : clip) {
                        if (ch >= 32 && ch != 127) {
                            m_searchController.AppendQueryChar(ch);
                        }
                    }
                    ScheduleSearchDebounce();
                }
                return 0;
            } else if (!isCtrlDown && !m_searchController.IsOpen() && !m_showGoToPage) {
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
            } else if (m_isLaserActive && !m_searchController.IsOpen() && !m_showGoToPage) {
                CycleLaserColor();
                return 0;
            }
            break;
        case 'D':
            if (isCtrlDown) {
                if (m_showProperties) CloseDocumentProperties();
                else ShowDocumentProperties();
                return 0;
            } else if (!m_searchController.IsOpen() && !m_showGoToPage) {
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
                if (isShiftDown) {
                    ReopenClosedTab();
                } else {
                    PromptOpenFile();
                }
                return 0;
            }
            break;
        case 'W':
            if (isCtrlDown) {
                if (!m_tabController.IsEmpty()) {
                    CloseTab(m_tabController.GetActiveIndex());
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
        case 'R':
            if (isCtrlDown) {
                ToggleReadAloud();
                return 0;
            }
            break;
        case VK_OEM_6: // ']'
            if (isCtrlDown && m_readAloudController.IsActive()) {
                m_readAloudController.SpeedUp();
                ShowToast(L"Speed: " + m_readAloudController.GetRateLabel());
                Render();
                return 0;
            }
            break;
        case VK_OEM_4: // '['
            if (isCtrlDown && m_readAloudController.IsActive()) {
                m_readAloudController.SpeedDown();
                ShowToast(L"Speed: " + m_readAloudController.GetRateLabel());
                Render();
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
                SetZoomMode(ZoomMode::Custom, 1.0f);
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
            if (m_readAloudController.IsActive()) {
                m_readAloudController.TogglePause();
                ShowToast(m_readAloudController.IsPaused() ? L"⏸ Paused" : L"▶ Resumed");
                Render();
                return 0;
            }
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
            if (m_readAloudController.IsActive()) {
                m_readAloudController.SkipForward();
                Render();
                return 0;
            }
            NextPage();
            return 0;
        case VK_LEFT:
            if (m_readAloudController.IsActive()) {
                m_readAloudController.SkipBackward();
                Render();
                return 0;
            }
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
            if (m_searchController.IsOpen()) {
                if (m_searchController.IsDebouncePending() || m_searchController.GetEngine().GetCurrentQuery() != m_searchController.GetQuery()) {
                    TriggerSearch();
                    Render();
                    return 0;
                }
                if (isShiftDown) {
                    if (m_searchController.PrevMatch()) {
                        JumpToActiveMatch();
                        Render();
                    }
                } else {
                    if (m_searchController.NextMatch()) {
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
                if (!m_searchController.IsOpen()) {
                    auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
                    if (pageText && !pageText->hasDigitalText) {
                        m_searchController.SetOcrEnabled(true);
                    }
                    m_searchController.Open();
                    if (!m_searchController.GetQuery().empty()) {
                        TriggerSearch();
                    }
                } else {
                    if (m_searchController.IsDebouncePending() || m_searchController.GetEngine().GetCurrentQuery() != m_searchController.GetQuery()) {
                        TriggerSearch();
                    } else if (isShiftDown) {
                        if (m_searchController.PrevMatch()) {
                            JumpToActiveMatch();
                            Render();
                        }
                    } else {
                        if (m_searchController.NextMatch()) {
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
                m_searchController.SetOpen(false);
            }
            Render();
            return 0;
        case VK_F11:
            ToggleFullscreen();
            return 0;
        case VK_ESCAPE:
            if (m_recentController.IsSwitcherOpen()) {
                m_recentController.CloseSwitcher();
                Render();
                return 0;
            }
            if (m_isBakingPdf) {
                CancelBakingSearchablePdf();
                ShowToast(L"Cancelling Searchable PDF generation...");
                return 0;
            }
            if (m_readAloudController.IsActive()) {
                StopReadAloud();
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
            if (m_searchController.IsOpen()) {
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

    return DefWindowProcW(m_hwnd, WM_KEYDOWN, wParam, lParam);
}


void AppWindow::EnqueueOpenFile(const std::wstring& path) {
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_pendingOpenFilesMutex);
        m_pendingOpenFiles.push_back(path);
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_APP_OPEN_FILE, 0, 0);
    }
}

void AppWindow::EnqueueBakePdf(const std::wstring& targetPath, bool overwriteOriginal) {
    if (targetPath.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_pendingBakeMutex);
        m_pendingBakes.push_back({ targetPath, overwriteOriginal });
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_APP_BAKE_PDF_START, 0, 0);
    }
}

void AppWindow::EnqueueSaveDone(bool success, const std::wstring& targetPath, const std::wstring& password, const std::wstring& toastMessage) {
    {
        std::lock_guard<std::mutex> lock(m_pendingSaveDoneMutex);
        m_pendingSaveCompletions.push_back({ success, targetPath, password, toastMessage });
    }
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_APP_SAVE_PDF_DONE, 0, 0);
    }
}

void AppWindow::OpenTab(const std::wstring& path) {
    if (path.empty()) return;

    std::wstring resolvedPath;
    bool alreadyOpen = false;
    PdfLoadStatus status = m_tabController.OpenTabWithPassword(path, m_hwnd, resolvedPath, alreadyOpen, L"");
    if (status == PdfLoadStatus::Success) {
        auto* pTab = GetActiveTab();
        if (pTab && !alreadyOpen && m_settingsController.Get().rememberLastPage) {
            for (const auto& item : m_recentController.GetItems()) {
                if (_wcsicmp(item.filePath.c_str(), resolvedPath.c_str()) == 0) {
                    if (item.lastPage > 0 && item.lastPage < pTab->document.GetPageCount()) {
                        pTab->currentPage = item.lastPage;
                    }
                    break;
                }
            }
        }
        uint32_t totalPages = pTab ? pTab->document.GetPageCount() : 0;
        uint32_t lastPage = pTab ? pTab->currentPage : 0;
        if (m_settingsController.Get().keepRecentHistory) {
            m_recentController.AddRecentFile(resolvedPath, totalPages, lastPage);
        }
        SHAddToRecentDocs(SHARD_PATHW, resolvedPath.c_str());

        m_renderer.InvalidatePageCache();
        if (m_searchController.IsOpen() && !m_searchController.GetQuery().empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();
        Render();
    } else if (status == PdfLoadStatus::PasswordRequired) {
        PendingPasswordRequest req;
        req.filePath = resolvedPath.empty() ? path : resolvedPath;
        PromptForPassword(req);
    } else {
        std::wstring fileName = resolvedPath;
        size_t lastSlash = resolvedPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            fileName = resolvedPath.substr(lastSlash + 1);
        }
        std::wstring msg = L"Unable to open \"" + fileName + L"\".\n\n"
                           L"The file may be corrupted, inaccessible, or an unsupported format.";
        MessageBoxW(m_hwnd, msg.c_str(), L"LightPDF - Error", MB_OK | MB_ICONWARNING);

        if (m_tabController.IsEmpty()) {
            UpdateTitle();
            Render();
        }
    }
}

void AppWindow::OpenRecentFile(size_t index) {
    const auto& items = m_recentController.GetItems();
    if (index >= items.size()) return;

    RecentFileItem item = items[index];
    std::wstring resolvedPath;
    bool alreadyOpen = false;

    PdfLoadStatus status = m_tabController.OpenTabWithPassword(item.filePath, m_hwnd, resolvedPath, alreadyOpen, L"");
    if (status == PdfLoadStatus::Success) {
        m_renderer.InvalidatePageCache();
        auto* pTab = GetActiveTab();
        if (pTab && !alreadyOpen && item.lastPage > 0 && m_settingsController.Get().rememberLastPage) {
            if (item.lastPage < pTab->document.GetPageCount()) {
                GoToPage(item.lastPage);
            }
        }
        uint32_t totalPages = GetActiveTab() ? GetActiveTab()->document.GetPageCount() : item.totalPages;
        if (m_settingsController.Get().keepRecentHistory) {
            m_recentController.AddRecentFile(resolvedPath, totalPages, GetActiveTab() ? GetActiveTab()->currentPage : item.lastPage);
        }
        SHAddToRecentDocs(SHARD_PATHW, resolvedPath.c_str());

        if (m_searchController.IsOpen() && !m_searchController.GetQuery().empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();
        Render();
    } else if (status == PdfLoadStatus::PasswordRequired) {
        PendingPasswordRequest req;
        req.filePath = resolvedPath.empty() ? item.filePath : resolvedPath;
        req.hasRestoreState = true;
        req.restorePage = item.lastPage;
        PromptForPassword(req);
    } else {
        m_recentController.RemoveRecentFileByPath(item.filePath);
        ShowToast(L"File not found: " + item.fileName);
        Render();
    }
}

void AppWindow::ReopenClosedTab() {
    ClosedTabInfo info;
    if (!m_recentController.PopClosedTab(info)) {
        ShowToast(L"No closed tabs to reopen");
        return;
    }

    std::wstring resolvedPath;
    bool alreadyOpen = false;
    PdfLoadStatus status = m_tabController.OpenTabWithPassword(info.filePath, m_hwnd, resolvedPath, alreadyOpen, L"");
    if (status == PdfLoadStatus::Success) {
        m_renderer.InvalidatePageCache();
        auto* pTab = GetActiveTab();
        if (pTab && !alreadyOpen) {
            pTab->zoom = info.zoom;
            pTab->zoomMode = info.zoomMode;
            pTab->continuousScroll = info.continuousScroll;
            pTab->scrollY = info.scrollY;
            if (info.currentPage < pTab->document.GetPageCount()) {
                pTab->currentPage = info.currentPage;
            }
        }
        std::wstring fileName = RecentFilesController::ExtractFileName(resolvedPath);
        ShowToast(L"Reopened: " + fileName);

        if (m_searchController.IsOpen() && !m_searchController.GetQuery().empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();
        Render();
    } else if (status == PdfLoadStatus::PasswordRequired) {
        PendingPasswordRequest req;
        req.filePath = resolvedPath.empty() ? info.filePath : resolvedPath;
        req.hasRestoreState = true;
        req.restorePage = info.currentPage;
        req.restoreZoom = info.zoom;
        req.restoreZoomMode = info.zoomMode;
        req.restoreScrollY = info.scrollY;
        req.restoreContinuous = info.continuousScroll;
        PromptForPassword(req);
    } else {
        ShowToast(L"Unable to reopen: " + RecentFilesController::ExtractFileName(info.filePath));
    }
}


void AppWindow::PromptForPassword(const PendingPasswordRequest& req) {
    if (m_showPasswordOverlay) {
        m_pendingPasswordQueue.push_back(req);
        return;
    }
    m_activePasswordReq = req;
    m_showPasswordOverlay = true;
    m_passwordInput.clear();
    m_passwordError.clear();
    m_passwordShowPlain = false;
    m_passwordHoveredBtn = 0;
    Render();
}

void AppWindow::SubmitPasswordPrompt() {
    if (m_passwordInput.empty()) {
        m_passwordError = L"Please enter a password.";
        Render();
        return;
    }

    std::wstring resolvedPath;
    bool alreadyOpen = false;
    PdfLoadStatus status = m_tabController.OpenTabWithPassword(
        m_activePasswordReq.filePath,
        m_hwnd,
        resolvedPath,
        alreadyOpen,
        m_passwordInput
    );

    SecureZeroMemory(m_passwordInput.data(), m_passwordInput.size() * sizeof(wchar_t));
    m_passwordInput.clear();

    if (status == PdfLoadStatus::Success) {
        m_showPasswordOverlay = false;
        m_passwordError.clear();

        auto* pTab = GetActiveTab();
        if (pTab && m_activePasswordReq.hasRestoreState && !alreadyOpen) {
            pTab->zoom = m_activePasswordReq.restoreZoom;
            pTab->zoomMode = m_activePasswordReq.restoreZoomMode;
            pTab->continuousScroll = m_activePasswordReq.restoreContinuous;
            pTab->scrollY = m_activePasswordReq.restoreScrollY;
            if (m_activePasswordReq.restorePage < pTab->document.GetPageCount()) {
                pTab->currentPage = m_activePasswordReq.restorePage;
            }
        }

        uint32_t totalPages = pTab ? pTab->document.GetPageCount() : 0;
        uint32_t lastPage = pTab ? pTab->currentPage : 0;
        m_recentController.AddRecentFile(resolvedPath, totalPages, lastPage);
        SHAddToRecentDocs(SHARD_PATHW, resolvedPath.c_str());

        m_renderer.InvalidatePageCache();
        if (m_searchController.IsOpen() && !m_searchController.GetQuery().empty()) {
            TriggerSearch();
        }
        RecalculateLayout();
        UpdateTitle();

        ProcessNextPasswordRequest();
        Render();
    } else if (status == PdfLoadStatus::WrongPassword || status == PdfLoadStatus::PasswordRequired) {
        m_passwordError = L"Incorrect password. Please try again.";
        Render();
    } else {
        m_showPasswordOverlay = false;
        m_passwordError.clear();
        std::wstring fileName = RecentFilesController::ExtractFileName(m_activePasswordReq.filePath);
        MessageBoxW(m_hwnd, (L"Unable to open \"" + fileName + L"\".\n\nThe file may be corrupted or inaccessible.").c_str(), L"LightPDF - Error", MB_OK | MB_ICONWARNING);
        ProcessNextPasswordRequest();
        Render();
    }
}

void AppWindow::CancelPasswordPrompt() {
    if (!m_passwordInput.empty()) {
        SecureZeroMemory(m_passwordInput.data(), m_passwordInput.size() * sizeof(wchar_t));
        m_passwordInput.clear();
    }
    m_showPasswordOverlay = false;
    m_passwordError.clear();
    ProcessNextPasswordRequest();
    Render();
}

void AppWindow::ProcessNextPasswordRequest() {
    if (m_pendingPasswordQueue.empty()) return;
    auto nextReq = m_pendingPasswordQueue.front();
    m_pendingPasswordQueue.pop_front();
    PromptForPassword(nextReq);
}

void AppWindow::RecordClosedTab(const DocumentTab& tab) {
    if (tab.document.IsLoaded() && !tab.document.GetFilePath().empty()) {
        ClosedTabInfo info;
        info.filePath = tab.document.GetFilePath();
        info.currentPage = tab.currentPage;
        info.zoom = tab.zoom;
        info.scrollY = tab.scrollY;
        info.zoomMode = tab.zoomMode;
        info.continuousScroll = tab.continuousScroll;
        m_recentController.PushClosedTab(info);
        m_recentController.UpdateLastPage(tab.document.GetFilePath(), tab.currentPage);
    }
}

void AppWindow::OnActiveTabChanged() {
    m_renderer.InvalidatePageCache();
    if (m_showProperties) {
        ShowDocumentProperties();
    }
    if (m_searchController.IsOpen() && !m_searchController.GetQuery().empty()) {
        TriggerSearch();
    }
    RecalculateLayout();
    UpdateTitle();
    Render();
}

void AppWindow::CloseTab(size_t index) {
    if (index >= m_tabController.GetTabCount()) return;

    if (const auto* pTab = m_tabController.GetTab(index)) {
        RecordClosedTab(*pTab);
    }

    m_renderer.InvalidatePageCache();
    m_tabController.CloseTab(index);

    if (m_tabController.IsEmpty()) {
        CloseSearch();
        CloseDocumentProperties();
        UpdateTitle();
        Render();
        return;
    }

    OnActiveTabChanged();
}

void AppWindow::CloseOtherTabs(size_t keepIndex) {
    if (keepIndex >= m_tabController.GetTabCount()) return;

    m_renderer.InvalidatePageCache();
    m_tabController.CloseOtherTabs(keepIndex, [this](const DocumentTab& tab) { RecordClosedTab(tab); });
    OnActiveTabChanged();
}

void AppWindow::CloseTabsToTheRight(size_t index) {
    if (index >= m_tabController.GetTabCount()) return;

    m_renderer.InvalidatePageCache();
    m_tabController.CloseTabsToTheRight(index, [this](const DocumentTab& tab) { RecordClosedTab(tab); });
    OnActiveTabChanged();
}

void AppWindow::SelectTab(size_t index) {
    if (m_tabController.SelectTab(index)) {
        OnActiveTabChanged();
    }
}

void AppWindow::NextTab() {
    if (m_tabController.NextTab()) {
        OnActiveTabChanged();
    }
}

void AppWindow::PrevTab() {
    if (m_tabController.PrevTab()) {
        OnActiveTabChanged();
    }
}

int AppWindow::HitTestTab(POINT pt, bool& outClose, bool& outAdd) const {
    return m_tabController.HitTestTab(pt, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), outClose, outAdd);
}

void AppWindow::UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const {
    m_tabController.UpdateTabRenderInfos(infos);
}

std::vector<TabRenderInfo> AppWindow::GetTabRenderInfos() const {
    return m_tabController.GetTabRenderInfos();
}

void AppWindow::OpenFileDialog() {
    // Build the filter string at runtime from the shared extension lists so it never drifts
    // from what the app can actually open. Entries are "description\0pattern\0".
    const std::wstring pdfPattern = SupportedFormats::BuildPattern(SupportedFormats::kPdf);
    const std::wstring pdfLabel = SupportedFormats::BuildFilterLabel(L"PDF Documents", SupportedFormats::kPdf);

    std::wstring filter;
    filter.reserve(128);
    auto addFilter = [&filter](const wchar_t* desc, const wchar_t* pattern) {
        filter.append(desc);
        filter.push_back(L'\0');
        filter.append(pattern);
        filter.push_back(L'\0');
    };
    addFilter(L"All Supported Documents", pdfPattern.c_str());
    addFilter(pdfLabel.c_str(), pdfPattern.c_str());
    addFilter(L"All Files (*.*)", L"*.*");
    filter.push_back(L'\0'); // double-null terminator

    // Large buffer so multiple selections and long paths fit.
    std::vector<wchar_t> buffer(32768, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = L"Open PDF Document";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING |
                OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        // Single selection: buffer holds the full path. Multi-selection: "dir\0file1\0file2\0\0".
        const wchar_t* first = buffer.data();
        const wchar_t* next = first + wcslen(first) + 1;
        if (*next == L'\0') {
            if (SupportedFormats::IsSupported(first)) {
                OpenTab(first);
            }
        } else {
            std::wstring dir = first;
            if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
            for (const wchar_t* p = next; *p; p += wcslen(p) + 1) {
                std::wstring fullPath = dir + p;
                if (SupportedFormats::IsSupported(fullPath)) {
                    OpenTab(fullPath);
                }
            }
        }
    }
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

    std::vector<bool> type3Pages(totalPages, false);
    if (pTab->parser) {
        for (uint32_t p = 0; p < totalPages; ++p) {
            type3Pages[p] = pTab->parser->PageHasType3Fonts(p);
        }
    }

    m_printThread = std::thread([this, hwnd, currentPage, totalPages, docName, doc, type3Pages]() {
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
                                        bool hasType3 = (pageIndex < type3Pages.size()) ? type3Pages[pageIndex] : false;
                                        m_renderer.PrintPageToHdc(page, pdex.hDC, pSize, hasType3);
                                    }
                                } catch (const winrt::hresult_error& ex) {
                                    OutputDebugStringW((L"[LightPDF] Print page render failed: " + std::wstring(ex.message()) + L"\n").c_str());
                                } catch (const std::exception& ex) {
                                    OutputDebugStringA(("[LightPDF] Print page render std::exception: " + std::string(ex.what()) + "\n").c_str());
                                } catch (...) {
                                    OutputDebugStringW(L"[LightPDF] Print page render unknown error\n");
                                }
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

namespace {

std::wstring AppendFilenameSuffix(const std::wstring& origName, const std::wstring& suffix) {
    size_t dotPos = origName.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        return origName.substr(0, dotPos) + suffix + L".pdf";
    }
    return origName + suffix + L".pdf";
}

std::wstring ShowNativeSavePdfDialog(
    HWND hwndOwner,
    const std::wstring& title,
    const std::wstring& initialDir,
    const std::wstring& defaultFileName
) {
    ComPtr<IFileSaveDialog> pFileSave;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pFileSave));
    if (SUCCEEDED(hr) && pFileSave) {
        std::wstring pdfDesc = SupportedFormats::BuildFilterLabel(L"PDF Documents", SupportedFormats::kPdf);
        std::wstring pdfPat = SupportedFormats::BuildPattern(SupportedFormats::kPdf);
        COMDLG_FILTERSPEC filterSpecs[] = {
            { pdfDesc.c_str(), pdfPat.c_str() },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileSave->SetFileTypes(ARRAYSIZE(filterSpecs), filterSpecs);
        pFileSave->SetDefaultExtension(L"pdf");
        pFileSave->SetFileName(defaultFileName.c_str());
        pFileSave->SetTitle(title.c_str());

        if (!initialDir.empty()) {
            ComPtr<IShellItem> pFolder;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&pFolder))) && pFolder) {
                pFileSave->SetFolder(pFolder.Get());
            }
        }

        hr = pFileSave->Show(hwndOwner);
        if (SUCCEEDED(hr)) {
            ComPtr<IShellItem> pItem;
            if (SUCCEEDED(pFileSave->GetResult(&pItem)) && pItem) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                    std::wstring targetPath(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                    return targetPath;
                }
            }
        }
        return L"";
    }

    std::wstring saveFilter;
    saveFilter += SupportedFormats::BuildFilterLabel(L"PDF Documents", SupportedFormats::kPdf);
    saveFilter.push_back(L'\0');
    saveFilter += SupportedFormats::BuildPattern(SupportedFormats::kPdf);
    saveFilter.push_back(L'\0');
    saveFilter += L"All Files (*.*)";
    saveFilter.push_back(L'\0');
    saveFilter += L"*.*";
    saveFilter.push_back(L'\0');
    saveFilter.push_back(L'\0');

    wchar_t szFile[MAX_PATH * 2] = { 0 };
    wcsncpy_s(szFile, defaultFileName.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFilter = saveFilter.c_str();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = _countof(szFile);
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.lpstrDefExt = L"pdf";
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn)) {
        return std::wstring(szFile);
    }
    return L"";
}

} // anonymous namespace

bool AppWindow::DoesDocumentNeedOcr(DocumentTab* pTab) {
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        return false;
    }
    if (pTab->document.IsPasswordProtected() || (pTab->parser && pTab->parser->IsEncrypted())) {
        return false;
    }

    auto curPageText = GetOrExtractPageText(pTab, pTab->currentPage);
    if (!curPageText || !curPageText->hasDigitalText) {
        return true;
    }

    uint32_t totalPages = pTab->document.GetPageCount();
    uint32_t checkLimit = std::min(totalPages, 25u);
    for (uint32_t p = 0; p < checkLimit; ++p) {
        if (p == pTab->currentPage) continue;
        auto pt = GetOrExtractPageText(pTab, p);
        if (!pt || !pt->hasDigitalText) {
            return true;
        }
    }
    return false;
}

bool AppWindow::ReloadOrSwitchActiveTabToFile(const std::wstring& targetPath, const std::wstring& password) {
    auto* pTab = GetActiveTab();
    if (!pTab || targetPath.empty()) return false;

    uint32_t curPage = pTab->currentPage;
    float curZoom = pTab->zoom;
    ZoomMode curZoomMode = pTab->zoomMode;
    bool curContinuous = pTab->continuousScroll;
    float curScrollY = pTab->scrollY;
    float curOffsetX = pTab->offsetX;
    float curOffsetY = pTab->offsetY;

    pTab->document.Close();
    if (pTab->parser) pTab->parser->Close();
    pTab->selection.Clear();

    // Complete any pending atomic .tmp swap now that viewer handles are closed
    std::wstring tmpPath = targetPath + L".tmp";
    if (GetFileAttributesW(tmpPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!ReplaceFileW(targetPath.c_str(), tmpPath.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            MoveFileExW(tmpPath.c_str(), targetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }

    if (pTab->document.OpenWithPassword(targetPath, m_hwnd, password) != PdfLoadStatus::Success) {
        return false;
    }

    pTab->parser = std::make_unique<PdfParser>();
    pTab->parser->Load(targetPath);
    pTab->metadataLoaded = false;
    pTab->textCache = std::make_shared<PageTextCache>();
    uint32_t totalPages = pTab->document.GetPageCount();
    pTab->textCache->pages.resize(totalPages);

    pTab->currentPage = (totalPages > 0) ? std::min(curPage, totalPages - 1) : 0;
    pTab->zoom = curZoom;
    pTab->zoomMode = curZoomMode;
    pTab->continuousScroll = curContinuous;
    pTab->scrollY = curScrollY;
    pTab->offsetX = curOffsetX;
    pTab->offsetY = curOffsetY;
    pTab->lastOffsetsZoom = -1.0f;

    m_recentController.AddRecentFile(targetPath, totalPages, pTab->currentPage);
    m_renderer.InvalidatePageCache();
    RecalculateLayout();
    UpdateTitle();
    return true;
}

void AppWindow::RunDialogThread(std::function<void()> task) {
    if (m_dialogThread.joinable()) {
        m_dialogThread.join();
    }
    m_dialogThread = std::thread([task = std::move(task)]() {
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        task();
        if (SUCCEEDED(hrCo)) CoUninitialize();
    });
}

void AppWindow::CopyPdfAndEnqueueDone(const std::wstring& origPath, const std::wstring& targetPath, const std::wstring& password, const std::wstring& successPrefix) {
    if (_wcsicmp(targetPath.c_str(), origPath.c_str()) == 0) {
        EnqueueSaveDone(false, L"", L"", L"Document saved.");
    } else if (CopyFileW(origPath.c_str(), targetPath.c_str(), FALSE)) {
        std::wstring savedName = PathUtils::GetFileName(targetPath);
        std::wstring msg = successPrefix.empty() ? (L"Saved to \"" + savedName + L"\"") : (successPrefix + L"\"" + savedName + L"\"");
        EnqueueSaveDone(true, targetPath, password, msg);
    } else {
        EnqueueSaveDone(false, L"", L"", L"Error: Failed to save file to destination.");
    }
}

namespace {
int ShowTaskDialogChoice(
    HWND hwnd,
    const wchar_t* title,
    const wchar_t* instruction,
    const wchar_t* content,
    const TASKDIALOG_BUTTON* buttons,
    UINT buttonCount,
    int defaultButton
) {
    typedef HRESULT (WINAPI *PFN_TaskDialogIndirect)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
    if (!hComCtl) return 0;

    PFN_TaskDialogIndirect pfnTaskDialogIndirect = (PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");
    int nButton = 0;
    if (pfnTaskDialogIndirect) {
        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        tdc.hwndParent = hwnd;
        tdc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        tdc.pszWindowTitle = title;
        tdc.pszMainInstruction = instruction;
        tdc.pszContent = content;
        tdc.pButtons = buttons;
        tdc.cButtons = buttonCount;
        tdc.nDefaultButton = defaultButton;

        if (SUCCEEDED(pfnTaskDialogIndirect(&tdc, &nButton, nullptr, nullptr))) {
            FreeLibrary(hComCtl);
            return nButton;
        }
    }
    FreeLibrary(hComCtl);
    return 0;
}
} // namespace

void AppWindow::PromptSaveAsPdf() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        ShowToast(L"No document is currently loaded.");
        return;
    }

    std::wstring origPath = pTab->document.GetFilePath();
    std::wstring origName = pTab->document.GetFileName();
    std::wstring password = pTab->document.GetPassword();
    HWND hwnd = m_hwnd;

    RunDialogThread([this, hwnd, origPath, origName, password]() {
        std::wstring initialDir = PathUtils::GetDirectory(origPath);
        std::wstring targetPath = ShowNativeSavePdfDialog(hwnd, L"Save PDF As", initialDir, origName);
        if (!targetPath.empty()) {
            CopyPdfAndEnqueueDone(origPath, targetPath, password);
        }
    });
}

void AppWindow::PromptSaveSearchablePdf(bool forceSaveAs) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) {
        ShowToast(L"No document is currently loaded.");
        return;
    }

    if (pTab->document.IsPasswordProtected() || (pTab->parser && pTab->parser->IsEncrypted())) {
        PromptSaveDecryptedPdf();
        return;
    }

    if (m_isBakingPdf) {
        ShowToast(L"Searchable PDF generation is already in progress...");
        return;
    }

    if (!DoesDocumentNeedOcr(pTab)) {
        PromptSaveAsPdf();
        return;
    }

    std::wstring origPath = pTab->document.GetFilePath();
    std::wstring origName = pTab->document.GetFileName();
    HWND hwnd = m_hwnd;

    RunDialogThread([this, hwnd, origPath, origName, forceSaveAs]() {
        int action = forceSaveAs ? 101 : 0;

        if (!forceSaveAs) {
            TASKDIALOG_BUTTON buttons[] = {
                { 101, L"Save Searchable PDF As...\nRun OCR and save to a new searchable PDF file (Original file untouched)" },
                { 102, L"Update Original File (Bake OCR In-Place)\nRun OCR and update the current file directly using a safe atomic swap" },
                { 103, L"Save Original Copy As... (Without OCR)\nSave a fast copy of the document as-is without running OCR" }
            };
            int nButton = ShowTaskDialogChoice(
                hwnd,
                L"Save Scanned PDF",
                L"How would you like to save this scanned document?",
                L"This PDF contains scanned pages without selectable text. You can bake an invisible English & Arabic OCR text layer or save a direct copy.",
                buttons,
                ARRAYSIZE(buttons),
                101
            );
            if (nButton == 101 || nButton == 102 || nButton == 103) {
                action = nButton;
            } else if (nButton != 0) {
                return;
            } else {
                int res = MessageBoxW(
                    hwnd,
                    L"Do you want to bake OCR and overwrite the original file?\n\n"
                    L"Click 'Yes' to update the original file with OCR.\n"
                    L"Click 'No' to save as a new searchable file.\n"
                    L"Click 'Cancel' to abort.",
                    L"Save Searchable PDF",
                    MB_YESNOCANCEL | MB_ICONQUESTION
                );
                if (res == IDYES) action = 102;
                else if (res == IDNO) action = 101;
                else return;
            }
        }

        std::wstring initialDir = PathUtils::GetDirectory(origPath);

        if (action == 102) {
            EnqueueBakePdf(origPath, true);
        } else if (action == 101) {
            std::wstring defaultName = AppendFilenameSuffix(origName, L"_searchable");
            std::wstring targetPath = ShowNativeSavePdfDialog(hwnd, L"Save Searchable PDF As", initialDir, defaultName);
            if (!targetPath.empty()) {
                bool isSame = (_wcsicmp(targetPath.c_str(), origPath.c_str()) == 0);
                EnqueueBakePdf(targetPath, isSame);
            }
        } else if (action == 103) {
            std::wstring targetPath = ShowNativeSavePdfDialog(hwnd, L"Save PDF Copy As", initialDir, origName);
            if (!targetPath.empty()) {
                CopyPdfAndEnqueueDone(origPath, targetPath);
            }
        }
    });
}

void AppWindow::PromptSaveDecryptedPdf() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) {
        ShowToast(L"No document is currently loaded.");
        return;
    }

    std::wstring origPath = pTab->document.GetFilePath();
    std::wstring origName = pTab->document.GetFileName();
    std::wstring password = pTab->document.GetPassword();
    HWND hwnd = m_hwnd;

    RunDialogThread([this, hwnd, origPath, origName, password]() {
        TASKDIALOG_BUTTON buttons[] = {
            { 101, L"Save Decrypted Copy As...\nSave an unencrypted, password-free copy to a new file (Original file untouched)" },
            { 102, L"Remove Password from Original File\nPermanently unlock the current file in-place using a safe atomic swap" },
            { 103, L"Save Encrypted Copy As...\nSave a copy of the document keeping its password protection" }
        };
        int action = ShowTaskDialogChoice(
            hwnd,
            L"Save Password-Protected PDF",
            L"How would you like to save this encrypted document?",
            L"Since you have unlocked this document, you can save a decrypted (password-free) version, unlock the original file in-place, or save a copy keeping the password.",
            buttons,
            ARRAYSIZE(buttons),
            101
        );
        if (action != 101 && action != 102 && action != 103) {
            return;
        }

        std::wstring initialDir = PathUtils::GetDirectory(origPath);

        if (action == 103) {
            std::wstring targetPath = ShowNativeSavePdfDialog(hwnd, L"Save Encrypted PDF Copy As", initialDir, origName);
            if (!targetPath.empty()) {
                CopyPdfAndEnqueueDone(origPath, targetPath, password, L"Saved encrypted copy to ");
            }
            return;
        }

        std::wstring targetPath;
        if (action == 102) {
            targetPath = origPath;
        } else {
            std::wstring defaultName = AppendFilenameSuffix(origName, L"_unprotected");
            targetPath = ShowNativeSavePdfDialog(hwnd, L"Save Decrypted Copy As", initialDir, defaultName);
        }

        if (!targetPath.empty()) {
            bool isOverwrite = (_wcsicmp(targetPath.c_str(), origPath.c_str()) == 0);
            auto res = PdfDecryptor::DecryptAndSave(origPath, targetPath, password, nullptr);
            if (res.success) {
                std::wstring savedName = PathUtils::GetFileName(targetPath);
                std::wstring msg = isOverwrite
                    ? (L"Password removed from \"" + savedName + L"\"!")
                    : (L"Saved unencrypted copy to \"" + savedName + L"\"");
                EnqueueSaveDone(true, targetPath, L"", msg);
            } else {
                std::wstring msg = res.errorMessage.empty() ? L"Decryption failed" : res.errorMessage;
                EnqueueSaveDone(false, L"", L"", L"Error: " + msg);
            }
        }
    });
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

    if (overwriteOriginal && pTab->parser) {
        pTab->parser->Close();
    }

    m_activeBakeTargetPath = targetPath;
    std::wstring srcPath = pTab->document.GetFilePath();
    uint32_t totalPages = pTab->document.GetPageCount();
    HWND hwnd = m_hwnd;

    ShowToast(L"Processing searchable PDF...");

    m_bakePdfThread = std::thread([this, hwnd, srcPath, targetPath, overwriteOriginal, totalPages]() {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& ex) {
            OutputDebugStringW((L"[LightPDF] Baking thread apartment init note: " + std::wstring(ex.message()) + L"\n").c_str());
        } catch (...) {}

        PdfParser parser;
        if (!parser.Load(srcPath)) {
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 0, 0);
            return;
        }

        std::vector<OcrPageItem> ocrPages;
        winrt::Windows::Data::Pdf::PdfDocument doc{ nullptr };
        BilingualOcrEngines ocrEngines;
        bool ocrInitAttempted = false;

        for (uint32_t p = 0; p < totalPages; ++p) {
            if (m_cancelBakingPdf) {
                PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 2, 0);
                return;
            }

            PdfPageText pageText;
            parser.ExtractPageText(p, pageText);

            if (!pageText.hasDigitalText) {
                if (!ocrInitAttempted) {
                    ocrInitAttempted = true;
                    try {
                        auto storageFile = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(srcPath).get();
                        if (storageFile) {
                            doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(storageFile).get();
                        }
                    } catch (const winrt::hresult_error& ex) {
                        OutputDebugStringW((L"[LightPDF] OCR document load failed: " + std::wstring(ex.message()) + L"\n").c_str());
                        doc = nullptr;
                    } catch (...) {
                        doc = nullptr;
                    }

                    ocrEngines = CreateBilingualOcrEngines();
                }

                if (!ocrEngines.IsValid() || !doc) {
                    PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 0, 0);
                    return;
                }

                try {
                    auto page = doc.GetPage(p);
                    if (page) {
                        OcrPageItem pageItem;
                        if (ExtractBilingualPageOcr(page, p, pageText.pageWidth, pageText.pageHeight, ocrEngines.arEngine, ocrEngines.enEngine, pageItem, 2.0f)) {
                            ocrPages.push_back(std::move(pageItem));
                        }
                    }
                } catch (const winrt::hresult_error& ex) {
                    OutputDebugStringW((L"[LightPDF] OCR recognition failed: " + std::wstring(ex.message()) + L"\n").c_str());
                } catch (const std::exception& ex) {
                    OutputDebugStringA(("[LightPDF] OCR recognition std::exception: " + std::string(ex.what()) + "\n").c_str());
                } catch (...) {}
            }

            PostMessageW(hwnd, WM_APP_BAKE_PDF_PROGRESS, p + 1, totalPages);
        }

        if (m_cancelBakingPdf) {
            PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 2, 0);
            return;
        }

        if (ocrPages.empty()) {
            if (!overwriteOriginal) {
                bool copyOk = CopyFileW(srcPath.c_str(), targetPath.c_str(), FALSE) != 0;
                PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, copyOk ? 3 : 0, 0);
            } else {
                PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, 3, 1);
            }
            return;
        }

        PostMessageW(hwnd, WM_APP_BAKE_PDF_PROGRESS, 0, totalPages);

        bool ok = PdfSearchableWriter::WriteSearchablePdf(srcPath, targetPath, ocrPages, nullptr);
        PostMessageW(hwnd, WM_APP_BAKE_PDF_DONE, ok ? 1 : 0, overwriteOriginal ? 1 : 0);
    });
}

void AppWindow::UpdateTitle() {
    auto* pTab = GetActiveTab();
    if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
        wchar_t title[512];
        const wchar_t* modeSuffix = pTab->continuousScroll ? L" (Continuous)" : L"";
        if (m_tabController.HasMultipleTabs()) {
            swprintf_s(
                title,
                L"[Tab %zu/%zu] [%u / %u] - %s - LightPDF%s",
                m_tabController.GetActiveIndex() + 1,
                m_tabController.GetTabCount(),
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

void AppWindow::SetZoomMode(ZoomMode mode, float customZoom) {
    auto* pTab = GetActiveTab();
    if (!pTab) return;
    pTab->zoomMode = mode;
    if (mode == ZoomMode::Custom) {
        pTab->zoom = customZoom;
    }

    if (mode == ZoomMode::FitPage) {
        m_settingsController.SetDefaultZoomMode(0);
    } else if (mode == ZoomMode::FitWidth) {
        m_settingsController.SetDefaultZoomMode(1);
    } else if (mode == ZoomMode::Custom && std::abs(customZoom - 1.0f) < 0.05f) {
        m_settingsController.SetDefaultZoomMode(2);
    }
    ApplySettingsToSession();

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

    m_settingsController.SetDefaultContinuous(pTab->continuousScroll);
    ApplySettingsToSession();

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

void AppWindow::ScrollHorizontal(float deltaX) {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return;

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipW = (float)m_renderer.GetWidth() * dipScale;
    const float margin = 24.0f;

    if (pTab->continuousScroll) {
        float maxPageW = 0.0f;
        uint32_t count = pTab->document.GetPageCount();
        for (uint32_t i = 0; i < count; ++i) {
            float w = pTab->document.GetPageSize(i).width * pTab->zoom;
            if (w > maxPageW) maxPageW = w;
        }

        if (maxPageW > dipW - margin * 2.0f) {
            pTab->offsetX -= deltaX;
            ClampCanvasOffsets(pTab);
            Render();
        }
    } else {
        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pTab->currentPage);
        float renderedW = pSize.width * pTab->zoom;

        if (renderedW > dipW - margin * 2.0f) {
            pTab->offsetX -= deltaX;
            if (renderedW > dipW - 48.0f) {
                pTab->zoomMode = ZoomMode::Custom;
            }
            ClampCanvasOffsets(pTab);
            Render();
        }
    }
}

void AppWindow::UpdateContinuousOffsets(DocumentTab* pTab) {
    float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
    m_tabController.UpdateContinuousOffsets(pTab, (float)m_renderer.GetWidth(), (float)m_renderer.GetWidth() * 96.0f / m_renderer.GetDpi(), dipH);
}

float AppWindow::GetTotalDocumentHeight(const DocumentTab* pTab) const {
    return m_tabController.GetTotalDocumentHeight(pTab);
}

float AppWindow::GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const {
    return m_tabController.GetPageYOffset(pTab, pageIndex);
}

uint32_t AppWindow::GetPageAtScrollOffset(const DocumentTab* pTab) const {
    return m_tabController.GetPageAtScrollOffset(pTab);
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
    LinkHoverRenderInfo linkHover;
    linkHover.active = m_hasHoveredLink;
    if (m_hasHoveredLink) {
        linkHover.pageIndex = m_hoveredLinkPage;
        linkHover.pageRect = m_hoveredLink.rect;
        if (m_hoveredLink.type == PdfLinkType::ExternalUri) {
            linkHover.label = m_hoveredLink.uri;
        } else if (m_hoveredLink.type == PdfLinkType::InternalPage) {
            linkHover.label = L"\u21AA Go to Page " + std::to_wstring(m_hoveredLink.targetPage + 1);
        }
    }

    PasswordOverlayRenderInfo pwdInfo;
    pwdInfo.visible = m_showPasswordOverlay;
    if (m_showPasswordOverlay) {
        pwdInfo.fileName = RecentFilesController::ExtractFileName(m_activePasswordReq.filePath);
        pwdInfo.passwordInput = m_passwordInput;
        pwdInfo.errorMessage = m_passwordError;
        pwdInfo.showPassword = m_passwordShowPlain;
        pwdInfo.hoveredButton = m_passwordHoveredBtn;
    }

    auto* pTab = GetActiveTab();
    auto tabInfos = m_tabController.GetTabRenderInfos();
    bool hoveredAdd = m_tabController.IsHoveredAdd();
    const auto& dictCardInfo = m_selectionController.GetDictCardInfo();

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

    TtsBarRenderInfo ttsBarInfo;
    if (m_readAloudController.IsActive()) {
        ttsBarInfo = m_readAloudController.GetBarInfo();
    }

    auto recentInfo = m_recentController.GetRenderInfo();
    OptionsOverlayRenderInfo optInfo = GetOptionsInfo();

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
        if (m_readAloudController.IsActive()) {
            auto ttsSpan = m_readAloudController.GetActiveWordSpan();
            if (!ttsSpan.rects.empty()) {
                selectionSpans.push_back(std::move(ttsSpan));
            }
        }

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
                    info.hasType3Font = pTab->parser ? pTab->parser->PageHasType3Fonts(i) : false;
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
                hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo,
                selectionSpans,
                dictCardInfo,
                laserInfo,
                presenterBarInfo,
                ttsBarInfo,
                &recentInfo,
                &pwdInfo,
                &linkHover,
                &optInfo
            );
            return;
        }

        if (pTab->currentPage < pTab->document.GetPageCount()) {
            auto page = pTab->document.GetPage(pTab->currentPage);
            auto pSize = pTab->document.GetPageSize(pTab->currentPage);
            bool hasType3 = pTab->parser ? pTab->parser->PageHasType3Fonts(pTab->currentPage) : false;

            m_renderer.RenderPage(
                page,
                pTab->zoom,
                pTab->offsetX,
                pTab->offsetY,
                pSize,
                pTab->currentPage,
                pTab->document.GetPageCount(),
                modeStr,
                hasType3,
                GetHelpInfo(),
                tabInfos,
                hoveredAdd,
                GetScrollbarInfo(),
                m_showGoToPage,
                m_goToPageBuffer,
                GetSearchBarInfo(),
                GetSearchHighlights(),
                m_docPropsInfo,
                selectionSpans,
                dictCardInfo,
                laserInfo,
                presenterBarInfo,
                ttsBarInfo,
                &recentInfo,
                &pwdInfo,
                &linkHover,
                &optInfo
            );
            return;
        }
    }



    m_renderer.RenderBlank(
        L"",
        GetHelpInfo(),
        tabInfos,
        hoveredAdd,
        m_showGoToPage,
        m_goToPageBuffer,
        GetSearchBarInfo(),
        m_docPropsInfo,
        dictCardInfo,
        laserInfo,
        presenterBarInfo,
        ttsBarInfo,
        &recentInfo,
        &pwdInfo,
        &linkHover,
        &optInfo
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
    return m_searchController.GetSearchBarInfo(m_tabController.HasMultipleTabs());
}

const std::vector<SearchHighlight>& AppWindow::GetSearchHighlights() const {
    return m_searchController.GetSearchHighlights();
}

int AppWindow::HitTestSearchBar(POINT pt) const {
    return m_searchController.HitTest(pt, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), m_tabController.HasMultipleTabs());
}

void AppWindow::TriggerSearch() {
    KillTimer(m_hwnd, 2);
    auto* pTab = GetActiveTab();
    if (pTab && pTab->document.IsPasswordProtected() && !m_searchController.IsOcrEnabled()) {
        m_searchController.ToggleOcr();
    }
    m_searchController.StartSearch(m_hwnd, pTab);
}

void AppWindow::JumpToActiveMatch() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    InvalidateSearchHighlights();

    SearchMatch match = m_searchController.GetActiveMatch();
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
    m_searchController.Close();
}

void AppWindow::ScheduleSearchDebounce() {
    KillTimer(m_hwnd, 2);
    m_searchController.SetDebouncePending(true);
    m_searchController.GetEngine().CancelAsync();
    m_searchController.GetEngine().Clear();
    m_searchController.SetLastJumpedMatch(-1);
    m_searchController.InvalidateHighlights();

    if (m_searchController.GetQuery().empty()) {
        m_searchController.SetDebouncePending(false);
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
    if (pTab->document.IsPasswordProtected() || meta.isEncrypted) {
        m_docPropsInfo.pdfFormat += L" (Password Protected)";
    }
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
    m_searchController.SetOpen(false);

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

    SetClipboardText(text);

    m_propsCopiedFeedbackTime = GetTickCount64();
    Render();
}

void AppWindow::ToggleOptionsOverlay() {
    m_showOptions = !m_showOptions;
    if (m_showOptions) {
        m_optionsHoveredId = 0;
        m_showHelp = false;
        m_showProperties = false;
        m_showGoToPage = false;
        m_recentController.CloseSwitcher();
        m_searchController.SetOpen(false);
        SyncSettingsWithActiveTab();
    } else {
        m_settingsController.SaveToRegistry();
    }
    Render();
}

void AppWindow::CloseOptionsOverlay() {
    if (m_showOptions) {
        m_showOptions = false;
        m_optionsHoveredId = 0;
        m_settingsController.SaveToRegistry();
        Render();
    }
}

OptionsOverlayRenderInfo AppWindow::GetOptionsInfo() const {
    OptionsOverlayRenderInfo info;
    info.visible = m_showOptions;
    info.activeCategory = m_optionsActiveCategory;
    info.hoveredId = m_optionsHoveredId;
    info.recentCount = m_recentController.GetCount();
    info.settings = m_settingsController.Get();
    return info;
}

void AppWindow::ApplySettingsToSession() {
    const auto& s = m_settingsController.Get();
    ZoomMode defZoom = ZoomMode::FitPage;
    if (s.defaultZoomMode == 1) defZoom = ZoomMode::FitWidth;
    else if (s.defaultZoomMode == 2) defZoom = ZoomMode::Custom;

    m_tabController.SetDefaultViewSettings(defZoom, s.defaultContinuous);
    m_renderer.SetPageTheme(s.pageTheme);
    m_readAloudController.SetRate(s.defaultTtsRate);
}

void AppWindow::SyncSettingsWithActiveTab() {
    auto* pTab = GetActiveTab();
    if (!pTab) return;

    bool changed = false;
    if (m_settingsController.Get().defaultContinuous != pTab->continuousScroll) {
        m_settingsController.Get().defaultContinuous = pTab->continuousScroll;
        changed = true;
    }

    uint32_t activeZoomMode = m_settingsController.Get().defaultZoomMode;
    if (pTab->zoomMode == ZoomMode::FitPage) {
        activeZoomMode = 0;
    } else if (pTab->zoomMode == ZoomMode::FitWidth) {
        activeZoomMode = 1;
    } else if (pTab->zoomMode == ZoomMode::Custom && std::abs(pTab->zoom - 1.0f) < 0.05f) {
        activeZoomMode = 2;
    }

    if (m_settingsController.Get().defaultZoomMode != activeZoomMode) {
        m_settingsController.Get().defaultZoomMode = activeZoomMode;
        changed = true;
    }

    if (changed) {
        m_settingsController.SaveToRegistry();
        ApplySettingsToSession();
    }
}

void AppWindow::HandleOptionsClick(int hitId) {
    auto& s = m_settingsController.Get();

    switch (hitId) {
    case OPT_HIT_OUTSIDE:
    case OPT_HIT_CLOSE_BTN:
    case OPT_HIT_DONE_BTN:
        CloseOptionsOverlay();
        return;

    case OPT_HIT_TAB_VIEW:
    case OPT_HIT_TAB_TOOLS:
    case OPT_HIT_TAB_SYSTEM:
        m_optionsActiveCategory = hitId - OPT_HIT_TAB_VIEW;
        Render();
        return;

    case OPT_HIT_RESET_BTN:
        m_settingsController.ResetToDefaults();
        ApplySettingsToSession();
        if (auto* pTab = GetActiveTab()) {
            pTab->zoomMode = ZoomMode::FitPage;
            pTab->continuousScroll = false;
            RecalculateLayout();
        }
        ShowToast(L"Settings reset to defaults");
        Render();
        return;

    // Tab 0: View & Reading
    case OPT_HIT_ZOOM_FIT_PAGE:
    case OPT_HIT_ZOOM_FIT_WIDTH:
    case OPT_HIT_ZOOM_100: {
        uint32_t zMode = static_cast<uint32_t>(hitId - OPT_HIT_ZOOM_FIT_PAGE);
        if (zMode == 0) SetZoomMode(ZoomMode::FitPage);
        else if (zMode == 1) SetZoomMode(ZoomMode::FitWidth);
        else SetZoomMode(ZoomMode::Custom, 1.0f);
        return;
    }
    case OPT_HIT_SCROLL_SINGLE:
    case OPT_HIT_SCROLL_CONT: {
        bool wantCont = (hitId == OPT_HIT_SCROLL_CONT);
        auto* pTab = GetActiveTab();
        if (pTab && pTab->continuousScroll != wantCont) {
            ToggleContinuousScroll();
        } else {
            m_settingsController.SetDefaultContinuous(wantCont);
            ApplySettingsToSession();
            Render();
        }
        return;
    }
    case OPT_HIT_THEME_ORIG:
    case OPT_HIT_THEME_NIGHT:
    case OPT_HIT_THEME_SEPIA:
        s.pageTheme = static_cast<PageTheme>(hitId - OPT_HIT_THEME_ORIG);
        ApplySettingsToSession();
        m_settingsController.SaveToRegistry();
        Render();
        return;

    case OPT_HIT_REMEMBER_PAGE:
        s.rememberLastPage = !s.rememberLastPage;
        m_settingsController.SaveToRegistry();
        Render();
        return;

    // Tab 1: Selection & Speech
    case OPT_HIT_DBLCLK_LOOKUP:
        s.doubleClickLookup = !s.doubleClickLookup;
        m_settingsController.SaveToRegistry();
        Render();
        return;

    case OPT_HIT_RCLICK_SELECT:
        s.rightClickAutoSelect = !s.rightClickAutoSelect;
        m_settingsController.SaveToRegistry();
        Render();
        return;

    case OPT_HIT_TTS_075X: s.defaultTtsRate = -2; ApplySettingsToSession(); m_settingsController.SaveToRegistry(); Render(); return;
    case OPT_HIT_TTS_100X: s.defaultTtsRate = 0;  ApplySettingsToSession(); m_settingsController.SaveToRegistry(); Render(); return;
    case OPT_HIT_TTS_125X: s.defaultTtsRate = 1;  ApplySettingsToSession(); m_settingsController.SaveToRegistry(); Render(); return;
    case OPT_HIT_TTS_150X: s.defaultTtsRate = 2;  ApplySettingsToSession(); m_settingsController.SaveToRegistry(); Render(); return;
    case OPT_HIT_TTS_200X: s.defaultTtsRate = 4;  ApplySettingsToSession(); m_settingsController.SaveToRegistry(); Render(); return;

    case OPT_HIT_AUTO_OCR:
        s.autoOcrSearch = !s.autoOcrSearch;
        m_settingsController.SaveToRegistry();
        Render();
        return;

    // Tab 2: System & Privacy
    case OPT_HIT_SET_DEFAULT:
        RegisterShellAssociations();
        ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
        ShowToast(L"Registered! Select LightPDF in Windows Default Apps.");
        Render();
        return;

    case OPT_HIT_UNREGISTER:
        UnregisterShellAssociations();
        ShowToast(L"File associations removed from registry.");
        Render();
        return;

    case OPT_HIT_KEEP_RECENT:
        s.keepRecentHistory = !s.keepRecentHistory;
        if (!s.keepRecentHistory) {
            m_recentController.ClearRecentFiles();
        }
        m_settingsController.SaveToRegistry();
        Render();
        return;

    case OPT_HIT_CLEAR_RECENT:
        m_recentController.ClearRecentFiles();
        ShowToast(L"Recent files history cleared");
        Render();
        return;

    default:
        break;
    }
}

void AppWindow::SetToolMode(ToolMode mode) {
    m_toolMode = mode;
    m_isLaserActive = false;
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
    return m_selectionController.GetOrExtractPageText(pTab, pageIndex);
}

bool AppWindow::HitTestPageText(const POINT& clientPt, uint32_t& outPage, size_t& outCharIndex, bool& outAfterChar) {
    auto* pTab = GetActiveTab();
    if (!pTab) return false;
    return m_selectionController.HitTestPageText(
        pTab, clientPt, (float)m_renderer.GetWidth(), (float)m_renderer.GetHeight(),
        m_renderer.GetDpi(), GetTopOffset(), outPage, outCharIndex, outAfterChar
    );
}

std::vector<SelectionHighlightSpan> AppWindow::GetSelectionSpans() const {
    return const_cast<SelectionController&>(m_selectionController).GetSelectionSpans(const_cast<DocumentTab*>(GetActiveTab()));
}

void AppWindow::CopySelectionToClipboard() {
    auto* pTab = GetActiveTab();
    if (!pTab) return;
    if (m_selectionController.CopySelectionToClipboard(m_hwnd, pTab)) {
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

    if (!m_selectionController.GetDictEngine().IsLoaded()) {
        m_selectionController.GetDictEngine().Initialize();
    }

    if (!m_selectionController.GetDictEngine().IsLoaded()) {
        ShowToast(L"Dictionary not found (dict\\en-ar.dat)");
        return false;
    }

    float dipScale = 96.0f / m_renderer.GetDpi();
    float dipWidth = (float)m_renderer.GetWidth() * dipScale;
    float dipHeight = (float)m_renderer.GetHeight() * dipScale;
    float topOffset = GetTopOffset();

    if (m_selectionController.TriggerDictionaryLookup(query, anchorRect, dipWidth, dipHeight, m_renderer.GetDpi(), topOffset)) {
        Render();
        return true;
    } else {
        ShowToast(L"No dictionary entry found for \"" + query + L"\"");
        return false;
    }
}

void AppWindow::DismissDictionaryCard() {
    if (m_selectionController.IsDictCardVisible()) {
        m_selectionController.DismissDictionaryCard();
        Render();
    }
}

bool AppWindow::HitTestDictionaryCard(POINT pt) const {
    return m_selectionController.HitTestDictionaryCard(pt, m_renderer.GetDpi());
}

std::wstring AppWindow::GetSelectedWordOrText(D2D1_RECT_F& outAnchorRect) {
    auto* pTab = GetActiveTab();
    if (!pTab) return L"";
    return m_selectionController.GetSelectedWordOrText(pTab, m_renderer.GetDpi(), GetTopOffset(), outAnchorRect);
}

void AppWindow::CopyDictionaryDefinitionToClipboard() {
    if (m_selectionController.CopyDictionaryDefinitionToClipboard(m_hwnd)) {
        ShowToast(L"Definition copied to clipboard");
    }
}

void AppWindow::ToggleReadAloud() {
    if (m_readAloudController.IsActive()) {
        StopReadAloud();
        return;
    }

    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    if (!m_readAloudController.Init(m_hwnd)) {
        ShowToast(L"Read Aloud: No speech voices available");
        return;
    }
    m_readAloudController.SetRate(m_settingsController.Get().defaultTtsRate);

    auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
    if (pageText && !pageText->hasDigitalText && pageText->fullText.empty()) {
        auto engines = CreateBilingualOcrEngines();
        if (engines.IsValid()) {
            ShowToast(L"Extracting text via OCR...");
            auto page = pTab->document.GetPage(pTab->currentPage);
            auto pSize = pTab->document.GetPageSize(pTab->currentPage);
            OcrPageItem ocrItem;
            if (ExtractBilingualPageOcr(page, pTab->currentPage, pSize.width, pSize.height, engines.arEngine, engines.enEngine, ocrItem)) {
                PopulatePageTextFromOcr(ocrItem, *pageText);
            }
        }
    }

    if (!pageText || pageText->fullText.empty()) {
        ShowToast(L"Read Aloud: No readable text on this page");
        return;
    }

    if (pTab->selection.HasSelection()) {
        D2D1_RECT_F anchor{};
        std::wstring selText = GetSelectedWordOrText(anchor);
        size_t startChar = (std::min)(pTab->selection.startIndex, pTab->selection.endIndex);
        size_t endChar = (std::max)(pTab->selection.startIndex, pTab->selection.endIndex);
        size_t count = (endChar > startChar) ? (endChar - startChar) : selText.size();
        m_readAloudController.Start(selText, pageText, pTab->currentPage, startChar, count);
    } else {
        m_readAloudController.Start(L"", pageText, pTab->currentPage);
    }

    ShowToast(L"▶ Read Aloud");
    Render();
}

void AppWindow::StopReadAloud() {
    m_readAloudController.Stop();
    ShowToast(L"Read Aloud stopped");
    Render();
}


void AppWindow::RegisterShellAssociations() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    auto setString = [](HKEY key, const wchar_t* name, const wchar_t* value) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                       static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    };

    const wchar_t* progId = L"LightPDF.Document";

    HKEY hKeyProgId;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document", 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyProgId, nullptr) == ERROR_SUCCESS) {

        setString(hKeyProgId, nullptr, L"PDF Document (LightPDF)");

        // Default Icon
        HKEY hKeyIcon;
        if (RegCreateKeyExW(hKeyProgId, L"DefaultIcon", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyIcon, nullptr) == ERROR_SUCCESS) {
            wchar_t iconVal[MAX_PATH + 4];
            swprintf_s(iconVal, L"%s,0", exePath);
            setString(hKeyIcon, nullptr, iconVal);
            RegCloseKey(hKeyIcon);
        }

        // Open command
        HKEY hKeyCmd;
        if (RegCreateKeyExW(hKeyProgId, L"shell\\open\\command", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyCmd, nullptr) == ERROR_SUCCESS) {
            wchar_t cmdVal[MAX_PATH + 16];
            swprintf_s(cmdVal, L"\"%s\" \"%%1\"", exePath);
            setString(hKeyCmd, nullptr, cmdVal);
            RegCloseKey(hKeyCmd);
        }

        RegCloseKey(hKeyProgId);
    }

    for (const wchar_t* ext : SupportedFormats::kPdf) {
        wchar_t keyPath[128];
        swprintf_s(keyPath, L"Software\\Classes\\%s\\OpenWithProgids", ext);
        HKEY hKeyExt;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyExt, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(hKeyExt, progId, 0, REG_NONE, nullptr, 0);
            RegCloseKey(hKeyExt);
        }
    }

    // Applications\\LightPDF.exe: makes the app show up reliably in "Open with" lists
    // and lets Explorer know which file types it handles.
    const wchar_t* pSlash = wcsrchr(exePath, L'\\');
    const wchar_t* exeName = pSlash ? (pSlash + 1) : exePath;
    wchar_t appKeyPath[128];
    swprintf_s(appKeyPath, L"Software\\Classes\\Applications\\%s", exeName);
    HKEY hKeyApp;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, appKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyApp, nullptr) == ERROR_SUCCESS) {
        setString(hKeyApp, L"FriendlyAppName", L"LightPDF");

        HKEY hKeyCmd;
        if (RegCreateKeyExW(hKeyApp, L"shell\\open\\command", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyCmd, nullptr) == ERROR_SUCCESS) {
            wchar_t cmdVal[MAX_PATH + 16];
            swprintf_s(cmdVal, L"\"%s\" \"%%1\"", exePath);
            setString(hKeyCmd, nullptr, cmdVal);
            RegCloseKey(hKeyCmd);
        }

        HKEY hKeyTypes;
        if (RegCreateKeyExW(hKeyApp, L"SupportedTypes", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyTypes, nullptr) == ERROR_SUCCESS) {
            for (const wchar_t* ext : SupportedFormats::kPdf) {
                setString(hKeyTypes, ext, L"");
            }
            RegCloseKey(hKeyTypes);
        }
        RegCloseKey(hKeyApp);
    }

    // Capabilities + RegisteredApplications: lists LightPDF in
    // Windows Settings > Apps > Default apps so users can choose it as the default viewer.
    HKEY hKeyCaps;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LightPDF\\Capabilities", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyCaps, nullptr) == ERROR_SUCCESS) {
        setString(hKeyCaps, L"ApplicationName", L"LightPDF");
        setString(hKeyCaps, L"ApplicationDescription", L"Ultra-fast, minimalist PDF viewer");

        HKEY hKeyAssoc;
        if (RegCreateKeyExW(hKeyCaps, L"FileAssociations", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyAssoc, nullptr) == ERROR_SUCCESS) {
            for (const wchar_t* ext : SupportedFormats::kPdf) {
                setString(hKeyAssoc, ext, progId);
            }
            RegCloseKey(hKeyAssoc);
        }
        RegCloseKey(hKeyCaps);
    }

    HKEY hKeyRegApps;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyRegApps, nullptr) == ERROR_SUCCESS) {
        setString(hKeyRegApps, L"LightPDF", L"Software\\LightPDF\\Capabilities");
        RegCloseKey(hKeyRegApps);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void AppWindow::UnregisterShellAssociations() {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\LightPDF.Document");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\LightPDF.exe");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\LightPDF\\Capabilities");

    HKEY hKeyRegApps;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, KEY_WRITE, &hKeyRegApps) == ERROR_SUCCESS) {
        RegDeleteValueW(hKeyRegApps, L"LightPDF");
        RegCloseKey(hKeyRegApps);
    }

    for (const wchar_t* ext : SupportedFormats::kPdf) {
        wchar_t keyPath[128];
        swprintf_s(keyPath, L"Software\\Classes\\%s\\OpenWithProgids", ext);
        HKEY hKeyExt;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_WRITE, &hKeyExt) == ERROR_SUCCESS) {
            RegDeleteValueW(hKeyExt, L"LightPDF.Document");
            RegCloseKey(hKeyExt);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void AppWindow::ClearHoveredLink() {
    m_hasHoveredLink = false;
    m_hoveredLinkPage = 0;
    m_hoveredLink = PdfLink{};
}

void AppWindow::ActivatePdfLink(const PdfLink& link) {
    if (link.type == PdfLinkType::ExternalUri) {
        if (!PdfParser::IsSafeExternalUri(link.uri)) {
            ShowToast(L"Blocked unsafe link protocol");
            return;
        }
        HINSTANCE hRes = ShellExecuteW(nullptr, L"open", link.uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)hRes <= 32) {
            ShowToast(L"Failed to open link");
        }
    } else if (link.type == PdfLinkType::InternalPage) {
        auto* pTab = GetActiveTab();
        if (!pTab || !pTab->document.IsLoaded()) return;
        uint32_t count = pTab->document.GetPageCount();
        if (link.targetPage < count) {
            PushNavHistory(pTab);
            pTab->navForwardStack.clear();
            GoToPage(link.targetPage);
            if (pTab->continuousScroll && link.targetTopDip >= 0.0f) {
                UpdateContinuousOffsets(pTab);
                float targetY = pTab->pageOffsets[link.targetPage] + link.targetTopDip * pTab->zoom;
                float dipH = (m_renderer.GetHeight() * (96.0f / m_renderer.GetDpi())) - GetTopOffset();
                float maxScroll = (std::max)(0.0f, GetTotalDocumentHeight(pTab) - dipH);
                pTab->scrollY = std::clamp(targetY, 0.0f, maxScroll);
                Render();
            }
            ShowToast(L"Page " + std::to_wstring(link.targetPage + 1) + L"  (Alt+\x2190 to go back)");
        }
    }
}

void AppWindow::PushNavHistory(DocumentTab* pTab) {
    if (!pTab || !pTab->document.IsLoaded()) return;
    NavHistoryEntry entry;
    entry.pageIndex = pTab->currentPage;
    entry.scrollY = pTab->scrollY;
    entry.offsetX = pTab->offsetX;
    entry.offsetY = pTab->offsetY;
    if (pTab->navBackStack.size() >= 64) {
        pTab->navBackStack.erase(pTab->navBackStack.begin());
    }
    pTab->navBackStack.push_back(entry);
}

void AppWindow::NavigateBack() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->navBackStack.empty()) return;

    NavHistoryEntry cur;
    cur.pageIndex = pTab->currentPage;
    cur.scrollY = pTab->scrollY;
    cur.offsetX = pTab->offsetX;
    cur.offsetY = pTab->offsetY;
    pTab->navForwardStack.push_back(cur);

    NavHistoryEntry target = pTab->navBackStack.back();
    pTab->navBackStack.pop_back();

    GoToPage(target.pageIndex);
    if (pTab->continuousScroll) {
        pTab->scrollY = target.scrollY;
        pTab->offsetX = target.offsetX;
    } else {
        pTab->offsetX = target.offsetX;
        pTab->offsetY = target.offsetY;
    }
    ShowToast(L"\u2190 Page " + std::to_wstring(target.pageIndex + 1));
    Render();
}

void AppWindow::NavigateForward() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded() || pTab->navForwardStack.empty()) return;

    NavHistoryEntry cur;
    cur.pageIndex = pTab->currentPage;
    cur.scrollY = pTab->scrollY;
    cur.offsetX = pTab->offsetX;
    cur.offsetY = pTab->offsetY;
    pTab->navBackStack.push_back(cur);

    NavHistoryEntry target = pTab->navForwardStack.back();
    pTab->navForwardStack.pop_back();

    GoToPage(target.pageIndex);
    if (pTab->continuousScroll) {
        pTab->scrollY = target.scrollY;
        pTab->offsetX = target.offsetX;
    } else {
        pTab->offsetX = target.offsetX;
        pTab->offsetY = target.offsetY;
    }
    ShowToast(L"Page " + std::to_wstring(target.pageIndex + 1) + L" \u2192");
    Render();
}


void AppWindow::SetClipboardText(const std::wstring& text) {
    ClipboardUtils::SetText(m_hwnd, text);
}

void AppWindow::SelectAll() {
    auto* pTab = GetActiveTab();
    if (!pTab || !pTab->document.IsLoaded()) return;

    auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
    if (!pageText || pageText->chars.empty()) {
        ShowToast(L"No selectable text on this page");
        return;
    }

    pTab->selection.active = true;
    pTab->selection.isDragging = false;
    pTab->selection.startPage = pTab->currentPage;
    pTab->selection.startIndex = 0;
    pTab->selection.endPage = pTab->currentPage;
    pTab->selection.endIndex = pageText->chars.size();
    Render();
}

void AppWindow::Deselect() {
    auto* pTab = GetActiveTab();
    if (!pTab) return;
    if (pTab->selection.active || pTab->selection.HasSelection()) {
        pTab->selection.Clear();
        Render();
    }
}

void AppWindow::HandleContextMenu(POINT ptScreen) {
    if (m_showPasswordOverlay || m_isFullscreen) {
        return;
    }

    POINT ptClient = ptScreen;
    ScreenToClient(m_hwnd, &ptClient);

    float dipScale = 96.0f / (m_renderer.GetDpi() > 0.0f ? m_renderer.GetDpi() : 96.0f);
    float dipY = (float)ptClient.y * dipScale;
    float topOffset = GetTopOffset();

    ContextMenuData data;
    data.canReopenClosedTab = m_recentController.CanReopenClosedTab();

    // 1. Tab strip hit test (if multiple tabs and clicked within top tab bar)
    if (m_tabController.HasMultipleTabs() && dipY >= 0.0f && dipY <= topOffset) {
        bool dummyClose = false, dummyAdd = false;
        int hitTab = m_tabController.HitTestTab(ptClient, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), dummyClose, dummyAdd);
        if (hitTab >= 0 && (size_t)hitTab < m_tabController.GetTabCount()) {
            data.targetTabIndex = (size_t)hitTab;
            data.totalTabs = m_tabController.GetTabCount();
            const auto* pTargetTab = m_tabController.GetTab((size_t)hitTab);
            if (pTargetTab && pTargetTab->document.IsLoaded()) {
                data.tabFilePath = pTargetTab->document.GetFilePath();
            }
            UINT_PTR cmd = ContextMenuController::ShowContextMenu(m_hwnd, ptScreen, ContextMenuContext::TabStrip, data);
            if (cmd != ID_CTX_NONE) {
                ExecuteContextMenuCommand(cmd, data);
            }
            return;
        }
    }

    auto* pTab = GetActiveTab();
    if (pTab && pTab->document.IsLoaded()) {
        data.isDocumentLoaded = true;
        data.currentPage = pTab->currentPage;
        data.totalPages = pTab->document.GetPageCount();
        data.isContinuousScroll = pTab->continuousScroll;
        data.isPasswordProtected = pTab->document.IsPasswordProtected() || (pTab->parser && pTab->parser->IsEncrypted());
        if (!data.isPasswordProtected && pTab->document.GetPageCount() > 0) {
            auto curPageText = GetOrExtractPageText(pTab, pTab->currentPage);
            data.hasScannedPages = (!curPageText || !curPageText->hasDigitalText);
            if (!data.hasScannedPages && pTab->currentPage != 0) {
                auto firstPageText = GetOrExtractPageText(pTab, 0);
                data.hasScannedPages = (!firstPageText || !firstPageText->hasDigitalText);
            }
        }

        // 2. Interactive Hyperlink hit test
        uint32_t linkPage = 0;
        PdfLink hitLink;
        if (m_selectionController.HitTestPageLink(pTab, ptClient, (float)m_renderer.GetWidth(), m_renderer.GetDpi(), topOffset, linkPage, hitLink)) {
            data.hasLink = true;
            data.hitLink = hitLink;
            UINT_PTR cmd = ContextMenuController::ShowContextMenu(m_hwnd, ptScreen, ContextMenuContext::Hyperlink, data);
            if (cmd != ID_CTX_NONE) {
                ExecuteContextMenuCommand(cmd, data);
            }
            return;
        }

        // 3. Optional auto-word selection on right-click (when enabled in Options)
        if (!pTab->selection.HasSelection() && m_settingsController.Get().rightClickAutoSelect) {
            uint32_t hitPage = 0;
            size_t hitChar = 0;
            bool afterChar = false;
            if (HitTestPageText(ptClient, hitPage, hitChar, afterChar)) {
                auto pageText = GetOrExtractPageText(pTab, hitPage);
                if (pageText && hitChar < pageText->chars.size()) {
                    auto isWordChar = [](wchar_t c) { return iswalnum(c) || c == L'_'; };
                    if (isWordChar(pageText->chars[hitChar].ch)) {
                        size_t wStart = hitChar;
                        while (wStart > 0 && isWordChar(pageText->chars[wStart - 1].ch)) wStart--;
                        size_t wEnd = hitChar;
                        while (wEnd < pageText->chars.size() && isWordChar(pageText->chars[wEnd].ch)) wEnd++;
                        if (wEnd > wStart) {
                            pTab->selection.active = true;
                            pTab->selection.isDragging = false;
                            pTab->selection.startPage = hitPage;
                            pTab->selection.startIndex = wStart;
                            pTab->selection.endPage = hitPage;
                            pTab->selection.endIndex = wEnd;
                            Render();
                        }
                    }
                }
            }
        }

        // 4. Text Selection context menu
        if (pTab->selection.HasSelection()) {
            D2D1_RECT_F anchorRect{};
            data.hasSelection = true;
            data.selectedText = GetSelectedWordOrText(anchorRect);
            UINT_PTR cmd = ContextMenuController::ShowContextMenu(m_hwnd, ptScreen, ContextMenuContext::TextSelection, data);
            if (cmd != ID_CTX_NONE) {
                ExecuteContextMenuCommand(cmd, data);
            }
            return;
        }
    }

    // 4. Default: Document Canvas Context Menu (or empty viewer)
    UINT_PTR cmd = ContextMenuController::ShowContextMenu(m_hwnd, ptScreen, ContextMenuContext::Canvas, data);
    if (cmd != ID_CTX_NONE) {
        ExecuteContextMenuCommand(cmd, data);
    }
}

void AppWindow::ExecuteContextMenuCommand(UINT_PTR cmd, const ContextMenuData& data) {
    switch (cmd) {
    // Selection commands
    case ID_CTX_COPY:
        CopySelectionToClipboard();
        break;
    case ID_CTX_SELECT_ALL:
        SelectAll();
        break;
    case ID_CTX_DESELECT:
        Deselect();
        break;
    case ID_CTX_DICT_LOOKUP: {
        D2D1_RECT_F anchorRect = { 0, 0, 0, 0 };
        std::wstring query = GetSelectedWordOrText(anchorRect);
        if (!query.empty()) {
            TriggerDictionaryLookup(query, anchorRect);
        }
        break;
    }
    case ID_CTX_READ_ALOUD_SELECTION: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->selection.HasSelection()) {
            ToggleReadAloud();
        }
        break;
    }
    case ID_CTX_SEARCH_SELECTION: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            D2D1_RECT_F anchorRect{};
            std::wstring query = GetSelectedWordOrText(anchorRect);
            if (!query.empty()) {
                auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
                if (pageText && !pageText->hasDigitalText) {
                    m_searchController.SetOcrEnabled(true);
                }
                m_searchController.Open();
                m_showGoToPage = false;
                m_showHelp = false;
                m_searchController.SetQuery(query);
                TriggerSearch();
                Render();
            }
        }
        break;
    }

    // Link commands
    case ID_CTX_OPEN_LINK:
        ActivatePdfLink(data.hitLink);
        break;
    case ID_CTX_COPY_LINK: {
        if (data.hitLink.type == PdfLinkType::ExternalUri) {
            SetClipboardText(data.hitLink.uri);
            ShowToast(L"Link copied to clipboard");
        } else if (data.hitLink.type == PdfLinkType::InternalPage) {
            std::wstring linkStr = L"Page " + std::to_wstring(data.hitLink.targetPage + 1);
            SetClipboardText(linkStr);
            ShowToast(L"Link target copied: " + linkStr);
        }
        break;
    }

    // Canvas / Reading commands
    case ID_CTX_NEXT_PAGE:
        NextPage();
        break;
    case ID_CTX_PREV_PAGE:
        PrevPage();
        break;
    case ID_CTX_GOTO_PAGE: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded() && pTab->document.GetPageCount() > 0) {
            m_showGoToPage = true;
            m_goToPageBuffer.clear();
            Render();
        }
        break;
    }
    case ID_CTX_FIT_PAGE:
        SetZoomMode(ZoomMode::FitPage);
        break;
    case ID_CTX_ACTUAL_SIZE:
        SetZoomMode(ZoomMode::Custom, 1.0f);
        break;
    case ID_CTX_FIT_WIDTH:
        SetZoomMode(ZoomMode::FitWidth);
        break;
    case ID_CTX_CONTINUOUS_SCROLL:
        ToggleContinuousScroll();
        break;
    case ID_CTX_READ_ALOUD:
        ToggleReadAloud();
        break;
    case ID_CTX_FIND: {
        auto* pTab = GetActiveTab();
        if (pTab && pTab->document.IsLoaded()) {
            auto pageText = GetOrExtractPageText(pTab, pTab->currentPage);
            if (pageText && !pageText->hasDigitalText && m_settingsController.Get().autoOcrSearch) {
                m_searchController.SetOcrEnabled(true);
            }
            m_searchController.Open();
            m_showGoToPage = false;
            m_showHelp = false;
            if (!m_searchController.GetQuery().empty()) {
                TriggerSearch();
            }
            Render();
        }
        break;
    }
    case ID_CTX_PRINT:
        PromptPrint();
        break;
    case ID_CTX_SAVE_AS:
        PromptSaveAsPdf();
        break;
    case ID_CTX_SAVE_SEARCHABLE:
        PromptSaveSearchablePdf(false);
        break;
    case ID_CTX_SAVE_DECRYPTED:
        PromptSaveDecryptedPdf();
        break;
    case ID_CTX_DOC_PROPERTIES:
        ShowDocumentProperties();
        break;
    case ID_CTX_OPTIONS:
        ToggleOptionsOverlay();
        break;

    // Tab commands
    case ID_CTX_TAB_CLOSE:
        CloseTab(data.targetTabIndex);
        break;
    case ID_CTX_TAB_CLOSE_OTHERS:
        CloseOtherTabs(data.targetTabIndex);
        break;
    case ID_CTX_TAB_CLOSE_RIGHT:
        CloseTabsToTheRight(data.targetTabIndex);
        break;
    case ID_CTX_TAB_REOPEN_CLOSED:
        ReopenClosedTab();
        break;
    case ID_CTX_TAB_COPY_PATH:
        if (!data.tabFilePath.empty()) {
            SetClipboardText(data.tabFilePath);
            ShowToast(L"File path copied to clipboard");
        }
        break;
    case ID_CTX_TAB_OPEN_FOLDER:
        if (!data.tabFilePath.empty()) {
            std::wstring param = L"/select,\"" + data.tabFilePath + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    }
}

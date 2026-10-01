/**
 * @file app_window.hpp
 * @brief Main Win32 application window, lifecycle coordination, input routing, and multi-threaded event dispatching.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include "d2d_renderer.hpp"
#include "pdf_document.hpp"
#include "pdf_search.hpp"
#include "pdf_parser.hpp"
#include "dictionary_engine.hpp"
#include "pdf_searchable_writer.hpp"
#include "tab_controller.hpp"
#include "search_controller.hpp"
#include "selection_controller.hpp"
#include "read_aloud_controller.hpp"

/// @brief Windows message posted when an external process opens a file via IPC (single-instance).
#define WM_APP_OPEN_FILE (WM_APP + 1)
/// @brief Windows message posted when background OCR searchable PDF baking finishes.
#define WM_APP_BAKE_PDF_DONE (WM_APP + 4)
/// @brief Windows message posted to initiate background OCR searchable PDF baking.
#define WM_APP_BAKE_PDF_START (WM_APP + 5)
/// @brief Windows message posted periodically with OCR baking progress.
#define WM_APP_BAKE_PDF_PROGRESS (WM_APP + 6)
/// @brief Windows message posted by SAPI when a word boundary or end-of-stream event occurs.
#define WM_APP_TTS_EVENT (WM_APP + 7)

extern const wchar_t* WINDOW_CLASS_NAME;

/// @brief Interactive tool mode for mouse cursor manipulation.
enum class ToolMode {
    TextSelect, ///< Marquee text selection and double-click word lookup
    Hand        ///< Click-and-drag viewport panning
};

/**
 * @class AppWindow
 * @brief Top-level Win32 application window orchestrating controllers, rendering, and background tasks.
 */
class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    /**
     * @brief Creates the native Win32 window, registers window class, and initializes Direct2D.
     * @param hInstance Application instance handle.
     * @param nCmdShow Window display command (e.g. SW_SHOWDEFAULT).
     * @param initialFile Optional file path to open upon startup.
     * @return true if window creation succeeded.
     */
    bool Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile = L"");

    /**
     * @brief Enters the Win32 message pump until WM_QUIT is received.
     * @return Exit code from WM_QUIT wParam.
     */
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleKeyDown(WPARAM wParam, LPARAM lParam);
    LRESULT HandleMouseEvent(UINT msg, WPARAM wParam, LPARAM lParam);

    void OpenTab(const std::wstring& path);
    void CloseTab(size_t index);
    void SelectTab(size_t index);
    void NextTab();
    void PrevTab();

    void OpenFile(const std::wstring& path) { OpenTab(path); }
    void PromptOpenFile();
    void PromptPrint();
    void PromptSaveSearchablePdf(bool forceSaveAs = false);
    void StartBakingSearchablePdf(const std::wstring& targetPath, bool overwriteOriginal);
    void CancelBakingSearchablePdf();
    void Render();
    void UpdateTitle();

    void SetZoomMode(ZoomMode mode);
    void AdjustZoom(float factor, POINT mousePos);
    void RecalculateLayout();

    void NextPage(bool shouldRender = true);
    void PrevPage(bool shouldRender = true);
    void GoToPage(uint32_t pageIndex);

    void ToggleContinuousScroll();
    void ScrollContinuous(float deltaY);
    void ScrollSinglePage(float deltaY);
    void ScrollHorizontal(float deltaX);
    float GetTotalDocumentHeight(const DocumentTab* pTab) const;
    float GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const;
    uint32_t GetPageAtScrollOffset(const DocumentTab* pTab) const;

    void ToggleFullscreen();
    void ToggleLaserPointer();
    void CycleLaserColor();

    float GetTopOffset() const { return m_tabController.GetTopOffset(); }
    int HitTestTab(POINT pt, bool& outClose, bool& outAdd) const;
    std::vector<TabRenderInfo> GetTabRenderInfos() const;
    void UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const;

    ScrollbarRenderInfo GetScrollbarInfo() const;
    bool HitTestScrollbar(POINT pt, bool& outThumb, ScrollbarRenderInfo* outInfo = nullptr) const;
    bool HitTestHud(POINT pt) const;
    void ShowScrollbar();
    void HandleScrollbarDrag(float mouseY);

    void UpdateContinuousOffsets(DocumentTab* pTab);

    SearchBarRenderInfo GetSearchBarInfo() const;
    const std::vector<SearchHighlight>& GetSearchHighlights() const;
    void InvalidateSearchHighlights() { m_searchController.InvalidateHighlights(); }
    int HitTestSearchBar(POINT pt) const;
    void TriggerSearch();
    void JumpToActiveMatch();
    void CloseSearch();
    void ScheduleSearchDebounce();

    void ShowDocumentProperties();
    void CloseDocumentProperties();
    void CopyPropertiesToClipboard();

    void SetToolMode(ToolMode mode);
    void ShowToast(const std::wstring& text);
    void ToggleReadAloud();
    void StopReadAloud();
    std::shared_ptr<PdfPageText> GetOrExtractPageText(DocumentTab* pTab, uint32_t pageIndex);
    bool HitTestPageText(const POINT& clientPt, uint32_t& outPage, size_t& outCharIndex, bool& outAfterChar);
    std::vector<SelectionHighlightSpan> GetSelectionSpans() const;
    void CopySelectionToClipboard();
    bool TriggerDictionaryLookup(const std::wstring& query, const D2D1_RECT_F& anchorRect);
    void DismissDictionaryCard();
    bool HitTestDictionaryCard(POINT pt) const;
    std::wstring GetSelectedWordOrText(D2D1_RECT_F& outAnchorRect);
    void CopyDictionaryDefinitionToClipboard();
    void ClampCanvasOffsets(DocumentTab* pTab);

    HelpOverlayRenderInfo GetHelpInfo() const;

    void EnqueueOpenFile(const std::wstring& path);
    void EnqueueBakePdf(const std::wstring& targetPath, bool overwriteOriginal);

    DocumentTab* GetActiveTab() {
        return m_tabController.GetActiveTab();
    }
    const DocumentTab* GetActiveTab() const {
        return m_tabController.GetActiveTab();
    }

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    D2DRenderer m_renderer;

    // Feature Controllers (Aggregated by value - Zero-Cost Abstraction)
    TabController m_tabController;
    SearchController m_searchController;
    SelectionController m_selectionController;
    ReadAloudController m_readAloudController;

    // Mouse Panning
    bool m_isPanning = false;
    POINT m_lastMousePos = { 0, 0 };

    // Fullscreen state
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_prevPlacement = { sizeof(WINDOWPLACEMENT) };

    // Presenter Mode & Laser Pointer state
    bool m_isLaserActive = false;
    LaserColor m_laserColor = LaserColor::Red;
    POINT m_laserPos = { 0, 0 };
    bool m_showPresenterBar = false;
    int m_presenterBarHoveredBtn = -1;

    // Help Overlay state
    bool m_showHelp = false;
    int m_helpActiveCategory = 0; // 0=All, 1=Navigation, 2=Zoom & View, 3=Tabs & Files, 4=Search & Tools
    int m_helpHoveredCategory = -1;
    int m_helpHoveredClose = 0;   // 0=none, 1=close

    // Go to Page state
    bool m_showGoToPage = false;
    std::wstring m_goToPageBuffer;

    // Document Properties state
    bool m_showProperties = false;
    int m_propsHoveredBtn = 0; // 0=body/none, 1=close, 2=copy, 3=ok
    uint64_t m_propsCopiedFeedbackTime = 0;
    DocumentPropertiesRenderInfo m_docPropsInfo;

    // Scrollbar state
    bool m_isDraggingScrollbar = false;
    float m_scrollbarDragThumbOffsetY = 0.0f;
    mutable float m_scrollbarDragThumbY = 0.0f;
    uint64_t m_lastScrollbarActiveTime = 0;
    float m_scrollbarAlpha = 0.0f;
    bool m_isScrollbarHovered = false;

    // Cursors
    HCURSOR m_cursorArrow = nullptr;
    HCURSOR m_cursorHand = nullptr;
    HCURSOR m_cursorIBeam = nullptr;
    HCURSOR m_cursorSizeAll = nullptr;

    // Tool Mode & Toast Feedback
    ToolMode m_toolMode = ToolMode::TextSelect;
    uint64_t m_hudToastTime = 0;
    std::wstring m_hudToastText;

    // File Open Dialog state
    std::atomic<bool> m_isDialogOpen{ false };
    std::thread m_dialogThread;

    // Printing state
    std::atomic<bool> m_isPrinting{ false };
    std::atomic<bool> m_cancelPrint{ false };
    std::thread m_printThread;

    // Searchable PDF Baking state
    std::atomic<bool> m_isBakingPdf{ false };
    std::atomic<bool> m_cancelBakingPdf{ false };
    std::thread m_bakePdfThread;

    // Safe cross-thread message queues (prevents raw pointer passing over Win32 messages)
    std::mutex m_pendingOpenFilesMutex;
    std::vector<std::wstring> m_pendingOpenFiles;

    struct PendingBakeRequest {
        std::wstring targetPath;
        bool overwriteOriginal = false;
    };
    std::mutex m_pendingBakeMutex;
    std::vector<PendingBakeRequest> m_pendingBakes;
};

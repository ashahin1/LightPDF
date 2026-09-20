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
#include "d2d_renderer.hpp"
#include "pdf_document.hpp"
#include "pdf_search.hpp"
#include "pdf_parser.hpp"
#include "dictionary_engine.hpp"

#define WM_APP_OPEN_FILE (WM_APP + 1)

extern const wchar_t* WINDOW_CLASS_NAME;

enum class ZoomMode {
    FitPage,
    FitWidth,
    Custom
};

enum class ToolMode {
    TextSelect,
    Hand
};

struct TextSelection {
    bool active = false;
    bool isDragging = false;
    uint32_t startPage = 0;
    size_t startIndex = 0;
    uint32_t endPage = 0;
    size_t endIndex = 0;

    void Clear() {
        active = false;
        isDragging = false;
        startPage = 0;
        startIndex = 0;
        endPage = 0;
        endIndex = 0;
    }

    bool HasSelection() const {
        if (!active) return false;
        if (startPage != endPage) return true;
        return startIndex != endIndex;
    }

    void GetOrderedRange(uint32_t& outStartPage, size_t& outStartIdx, uint32_t& outEndPage, size_t& outEndIdx) const {
        if (startPage < endPage || (startPage == endPage && startIndex <= endIndex)) {
            outStartPage = startPage;
            outStartIdx = startIndex;
            outEndPage = endPage;
            outEndIdx = endIndex;
        } else {
            outStartPage = endPage;
            outStartIdx = endIndex;
            outEndPage = startPage;
            outEndIdx = startIndex;
        }
    }
};

struct DocumentTab {
    PdfDocumentWrapper document;
    uint32_t currentPage = 0;
    ZoomMode zoomMode = ZoomMode::FitPage;
    float zoom = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool continuousScroll = false;
    float scrollY = 0.0f;

    // Continuous scroll prefix sums & dimensions cache
    std::vector<float> pageOffsets;
    float totalDocHeight = 0.0f;
    float lastOffsetsZoom = -1.0f;

    // Metadata cache for document properties (Ctrl+D)
    PdfMetadata metadata;
    bool metadataLoaded = false;

    // Parsed page text cache for fast search & selection
    std::shared_ptr<PageTextCache> textCache;

    // Text selection state
    TextSelection selection;

    // Parser for lazy, zero-latency single-page text extraction
    std::unique_ptr<PdfParser> parser;
};

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile = L"");
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OpenTab(const std::wstring& path);
    void CloseTab(size_t index);
    void SelectTab(size_t index);
    void NextTab();
    void PrevTab();

    void OpenFile(const std::wstring& path) { OpenTab(path); }
    void PromptOpenFile();
    void PromptPrint();
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
    float GetTotalDocumentHeight(const DocumentTab* pTab) const;
    float GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const;
    uint32_t GetPageAtScrollOffset(const DocumentTab* pTab) const;

    void ToggleFullscreen();

    float GetTopOffset() const { return (m_tabs.size() > 1) ? 34.0f : 0.0f; }
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
    void InvalidateSearchHighlights() { m_highlightsDirty = true; }
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

    DocumentTab* GetActiveTab() {
        if (m_tabs.empty() || m_activeTab >= m_tabs.size()) return nullptr;
        return &m_tabs[m_activeTab];
    }
    const DocumentTab* GetActiveTab() const {
        if (m_tabs.empty() || m_activeTab >= m_tabs.size()) return nullptr;
        return &m_tabs[m_activeTab];
    }

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    D2DRenderer m_renderer;

    // Multi-tab collection
    std::vector<DocumentTab> m_tabs;
    size_t m_activeTab = 0;

    // Tab Bar Mouse Hover state
    int m_hoveredTab = -1;
    bool m_hoveredClose = false;
    bool m_hoveredAdd = false;

    // Mouse Panning
    bool m_isPanning = false;
    POINT m_lastMousePos = { 0, 0 };

    // Fullscreen state
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_prevPlacement = { sizeof(WINDOWPLACEMENT) };

    // Help Overlay state
    bool m_showHelp = false;
    int m_helpActiveCategory = 0; // 0=All, 1=Navigation, 2=Zoom & View, 3=Tabs & Files, 4=Search & Tools
    int m_helpHoveredCategory = -1;
    int m_helpHoveredClose = 0;   // 0=none, 1=close

    // Go to Page state
    bool m_showGoToPage = false;
    std::wstring m_goToPageBuffer;

    // Search state
    bool m_showSearch = false;
    std::wstring m_searchQuery;
    bool m_searchMatchCase = false;
    bool m_searchOcrEnabled = false;
    bool m_searchDebouncePending = false;
    PdfSearchEngine m_searchEngine;
    int m_searchHoveredBtn = 0; // 0=body/none, 1=prev, 2=next, 3=case, 4=ocr, 5=close
    int m_lastJumpedMatch = -1;
    mutable std::vector<SearchHighlight> m_cachedHighlights;
    mutable bool m_highlightsDirty = true;

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

    // Tab rendering cache
    mutable std::vector<TabRenderInfo> m_cachedTabInfos;

    // Dictionary Engine & Floating Card state
    DictionaryEngine m_dictEngine;
    DictionaryCardRenderInfo m_dictCardInfo;
    D2D1_RECT_F m_dictCardBounds = { 0, 0, 0, 0 };
};

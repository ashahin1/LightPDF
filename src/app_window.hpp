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

#define WM_APP_OPEN_FILE (WM_APP + 1)

extern const wchar_t* WINDOW_CLASS_NAME;

enum class ZoomMode {
    FitPage,
    FitWidth,
    Custom
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

    void NextPage();
    void PrevPage();
    void GoToPage(uint32_t pageIndex);

    void ToggleContinuousScroll();
    void ScrollContinuous(float deltaY);
    float GetTotalDocumentHeight(const DocumentTab* pTab) const;
    uint32_t GetPageAtScrollOffset(const DocumentTab* pTab) const;

    void ToggleFullscreen();

    float GetTopOffset() const { return (m_tabs.size() > 1) ? 34.0f : 0.0f; }
    int HitTestTab(POINT pt, bool& outClose, bool& outAdd) const;
    std::vector<TabRenderInfo> GetTabRenderInfos() const;

    ScrollbarRenderInfo GetScrollbarInfo() const;
    bool HitTestScrollbar(POINT pt, bool& outThumb) const;
    bool HitTestHud(POINT pt) const;
    void ShowScrollbar();
    void HandleScrollbarDrag(float mouseY);

    SearchBarRenderInfo GetSearchBarInfo() const;
    std::vector<SearchHighlight> GetSearchHighlights() const;
    int HitTestSearchBar(POINT pt) const;
    void TriggerSearch();
    void JumpToActiveMatch();
    void CloseSearch();
    void ScheduleSearchDebounce();

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

    // Scrollbar state
    bool m_isDraggingScrollbar = false;
    float m_scrollbarDragThumbOffsetY = 0.0f;
    float m_scrollbarDragThumbY = 0.0f;
    uint64_t m_lastScrollbarActiveTime = 0;
    float m_scrollbarAlpha = 0.0f;
    bool m_isScrollbarHovered = false;

    // File Open Dialog state
    std::atomic<bool> m_isDialogOpen{ false };

    // Printing state
    std::atomic<bool> m_isPrinting{ false };
    std::atomic<bool> m_cancelPrint{ false };
    std::thread m_printThread;
};

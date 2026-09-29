#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "d2d_renderer.hpp"
#include "pdf_document.hpp"
#include "pdf_parser.hpp"
#include "pdf_search.hpp"

enum class ZoomMode {
    FitPage,
    FitWidth,
    Custom
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
    DocumentTab() = default;
    ~DocumentTab() = default;
    DocumentTab(DocumentTab&&) noexcept = default;
    DocumentTab& operator=(DocumentTab&&) noexcept = default;
    DocumentTab(const DocumentTab&) = delete;
    DocumentTab& operator=(const DocumentTab&) = delete;

    PdfDocumentWrapper document;
    uint32_t currentPage = 0;
    ZoomMode zoomMode = ZoomMode::FitPage;
    float zoom = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool continuousScroll = false;
    float scrollY = 0.0f;

    // Thread safety contract:
    // Layout caching fields are mutable to enable const-correct geometry queries
    // (GetTotalDocumentHeight, GetPageYOffset, GetPageAtScrollOffset).
    // These fields are accessed exclusively from the main UI thread.
    mutable std::vector<float> pageOffsets;
    mutable float totalDocHeight = 0.0f;
    mutable float lastOffsetsZoom = -1.0f;

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

class TabController {
public:
    TabController() = default;
    ~TabController() = default;

    size_t GetTabCount() const { return m_tabs.size(); }
    bool IsEmpty() const { return m_tabs.empty(); }
    size_t GetActiveIndex() const { return m_activeTab; }
    void SetActiveIndex(size_t index) { if (index < m_tabs.size()) m_activeTab = index; }

    DocumentTab* GetActiveTab();
    const DocumentTab* GetActiveTab() const;
    DocumentTab* GetTab(size_t index);
    const DocumentTab* GetTab(size_t index) const;

    bool HasMultipleTabs() const { return m_tabs.size() > 1; }
    float GetTopOffset() const { return (m_tabs.size() > 1) ? 34.0f : 0.0f; }

    // Tab lifecycle operations
    bool OpenTab(const std::wstring& path, HWND hwnd, std::wstring& outCanonicalPath, bool& outAlreadyOpen);
    bool CloseTab(size_t index);
    bool SelectTab(size_t index);
    bool NextTab();
    bool PrevTab();

    // Hover & hit-testing
    int HitTestTab(POINT pt, float winWidth, float dpi, bool& outClose, bool& outAdd) const;
    void SetHover(int hoveredTab, bool hoveredClose, bool hoveredAdd) {
        m_hoveredTab = hoveredTab;
        m_hoveredClose = hoveredClose;
        m_hoveredAdd = hoveredAdd;
    }
    int GetHoveredTab() const { return m_hoveredTab; }
    bool IsHoveredClose() const { return m_hoveredClose; }
    bool IsHoveredAdd() const { return m_hoveredAdd; }

    // Rendering info
    std::vector<TabRenderInfo> GetTabRenderInfos() const;
    void UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const;

    // Layout & scroll helpers
    void UpdateContinuousOffsets(const DocumentTab* pTab, float winWidth, float dipW, float dipH) const;
    float GetTotalDocumentHeight(const DocumentTab* pTab) const;
    float GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const;
    uint32_t GetPageAtScrollOffset(const DocumentTab* pTab) const;

private:
    std::vector<DocumentTab> m_tabs;
    size_t m_activeTab = 0;

    int m_hoveredTab = -1;
    bool m_hoveredClose = false;
    bool m_hoveredAdd = false;

    mutable std::vector<TabRenderInfo> m_cachedTabInfos;
};

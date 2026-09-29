/**
 * @file tab_controller.hpp
 * @brief Multi-document tab lifecycle, view geometry, continuous scroll offsets, and hit-testing.
 */

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

/// @brief Document viewing zoom mode configuration.
enum class ZoomMode {
    FitPage,    ///< Scale page to fully fit inside window client area
    FitWidth,   ///< Scale page width to match window client width
    Custom      ///< Arbitrary user-controlled zoom factor
};

/// @brief Tracks start and end coordinates of an interactive text selection.
struct TextSelection {
    bool active = false;        ///< Whether a selection currently exists
    bool isDragging = false;    ///< Whether user is actively dragging the selection marquee
    uint32_t startPage = 0;     ///< Page index of selection start
    size_t startIndex = 0;      ///< Character index on start page
    uint32_t endPage = 0;       ///< Page index of selection end
    size_t endIndex = 0;        ///< Character index on end page

    /// @brief Clears active selection range and state.
    void Clear() {
        active = false;
        isDragging = false;
        startPage = 0;
        startIndex = 0;
        endPage = 0;
        endIndex = 0;
    }

    /// @brief Returns whether selection contains non-empty range.
    bool HasSelection() const {
        if (!active) return false;
        if (startPage != endPage) return true;
        return startIndex != endIndex;
    }

    /// @brief Retrieves ordered start and end coordinates (ensuring start <= end).
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

/// @brief Encapsulates complete document state, layout metrics, and text cache for a single tab.
struct DocumentTab {
    DocumentTab() = default;
    ~DocumentTab() = default;
    DocumentTab(DocumentTab&&) noexcept = default;
    DocumentTab& operator=(DocumentTab&&) noexcept = default;
    DocumentTab(const DocumentTab&) = delete;
    DocumentTab& operator=(const DocumentTab&) = delete;

    PdfDocumentWrapper document;            ///< Underlying WinRT PDF document instance
    uint32_t currentPage = 0;               ///< Current 0-based active page index
    ZoomMode zoomMode = ZoomMode::FitPage;  ///< Active zoom mode
    float zoom = 1.0f;                      ///< Active zoom scale multiplier
    float offsetX = 0.0f;                   ///< Single-page horizontal pan offset in DIPs
    float offsetY = 0.0f;                   ///< Single-page vertical pan offset in DIPs
    bool continuousScroll = false;          ///< Whether continuous scroll mode is enabled
    float scrollY = 0.0f;                   ///< Continuous vertical scroll position in DIPs

    // Thread safety contract:
    // Layout caching fields are mutable to enable const-correct geometry queries
    // (GetTotalDocumentHeight, GetPageYOffset, GetPageAtScrollOffset).
    // These fields are accessed exclusively from the main UI thread.
    mutable std::vector<float> pageOffsets; ///< Cached Y offsets per page in continuous view
    mutable float totalDocHeight = 0.0f;    ///< Cached cumulative document height in DIPs
    mutable float lastOffsetsZoom = -1.0f;  ///< Zoom factor when continuous offsets were computed

    // Metadata cache for document properties (Ctrl+D)
    PdfMetadata metadata;                   ///< Cached document properties
    bool metadataLoaded = false;            ///< Flag indicating if metadata was extracted

    // Parsed page text cache for fast search & selection
    std::shared_ptr<PageTextCache> textCache;///< Shared page text extraction cache

    // Text selection state
    TextSelection selection;                ///< Active text selection on this tab

    // Parser for lazy, zero-latency single-page text extraction
    std::unique_ptr<PdfParser> parser;      ///< Thread-confined parser owned by UI thread
};

/**
 * @class TabController
 * @brief Manages open document tabs, active tab switching, tab strip hit-testing, and layout offsets.
 */
class TabController {
public:
    TabController() = default;
    ~TabController() = default;

    /// @brief Returns the total number of open tabs.
    size_t GetTabCount() const { return m_tabs.size(); }

    /// @brief Returns true if no documents are open.
    bool IsEmpty() const { return m_tabs.empty(); }

    /// @brief Returns the 0-based index of the currently active tab.
    size_t GetActiveIndex() const { return m_activeTab; }

    /// @brief Sets the active tab index if within valid range.
    void SetActiveIndex(size_t index) { if (index < m_tabs.size()) m_activeTab = index; }

    /// @brief Returns pointer to the currently active DocumentTab (or nullptr if none).
    DocumentTab* GetActiveTab();

    /// @brief Returns const pointer to the currently active DocumentTab (or nullptr if none).
    const DocumentTab* GetActiveTab() const;

    /// @brief Returns pointer to DocumentTab at given index (or nullptr if out of bounds).
    DocumentTab* GetTab(size_t index);

    /// @brief Returns const pointer to DocumentTab at given index (or nullptr if out of bounds).
    const DocumentTab* GetTab(size_t index) const;

    /// @brief Returns true if more than one tab is open.
    bool HasMultipleTabs() const { return m_tabs.size() > 1; }

    /// @brief Returns top offset in DIPs reserved for tab strip bar (34 DIPs if multiple tabs, 0 if single).
    float GetTopOffset() const { return (m_tabs.size() > 1) ? 34.0f : 0.0f; }

    /**
     * @brief Opens a PDF file in a new tab, or activates existing tab if already open.
     * @param path Filesystem path to the PDF.
     * @param hwnd Parent window HWND for notification messages.
     * @param outCanonicalPath Canonicalized full path resolved for the file.
     * @param outAlreadyOpen Set to true if file was already open in a tab.
     * @return true if opened or focused successfully, false on error.
     */
    bool OpenTab(const std::wstring& path, HWND hwnd, std::wstring& outCanonicalPath, bool& outAlreadyOpen);

    /**
     * @brief Closes tab at specified index.
     * @param index 0-based tab index to close.
     * @return true if closed, false if index invalid.
     */
    bool CloseTab(size_t index);

    /**
     * @brief Activates tab at specified index.
     * @param index 0-based tab index.
     * @return true if valid and selected.
     */
    bool SelectTab(size_t index);

    /// @brief Switches to the next tab with cyclic wraparound.
    bool NextTab();

    /// @brief Switches to the previous tab with cyclic wraparound.
    bool PrevTab();

    /**
     * @brief Hit-tests tab bar buttons and headers.
     * @param pt Client-coordinate point.
     * @param winWidth Client width in DIPs.
     * @param dpi Monitor DPI.
     * @param outClose Set to true if close button ('x') was hit.
     * @param outAdd Set to true if '+' add tab button was hit.
     * @return Tab index hit, or -1 if none.
     */
    int HitTestTab(POINT pt, float winWidth, float dpi, bool& outClose, bool& outAdd) const;

    /// @brief Sets current hover state for tab bar rendering.
    void SetHover(int hoveredTab, bool hoveredClose, bool hoveredAdd) {
        m_hoveredTab = hoveredTab;
        m_hoveredClose = hoveredClose;
        m_hoveredAdd = hoveredAdd;
    }

    int GetHoveredTab() const { return m_hoveredTab; }
    bool IsHoveredClose() const { return m_hoveredClose; }
    bool IsHoveredAdd() const { return m_hoveredAdd; }

    /// @brief Returns cached tab rendering information structures.
    std::vector<TabRenderInfo> GetTabRenderInfos() const;

    /// @brief Updates vector of tab rendering info for paint pipeline.
    void UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const;

    /// @brief Recomputes continuous scroll page offsets and document height.
    void UpdateContinuousOffsets(const DocumentTab* pTab, float winWidth, float dipW, float dipH) const;

    /// @brief Returns total continuous document height in DIPs.
    float GetTotalDocumentHeight(const DocumentTab* pTab) const;

    /// @brief Returns top vertical offset in DIPs for a specific page in continuous view.
    float GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const;

    /// @brief Calculates which page index is located at the current continuous scroll offset.
    uint32_t GetPageAtScrollOffset(const DocumentTab* pTab) const;

private:
    std::vector<DocumentTab> m_tabs;
    size_t m_activeTab = 0;

    int m_hoveredTab = -1;
    bool m_hoveredClose = false;
    bool m_hoveredAdd = false;

    mutable std::vector<TabRenderInfo> m_cachedTabInfos;
};

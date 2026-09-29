/**
 * @file search_controller.hpp
 * @brief Interactive in-document search controller, query debounce, match navigation, and UI hit-testing.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include "d2d_renderer.hpp"
#include "pdf_search.hpp"
#include "tab_controller.hpp"

/**
 * @class SearchController
 * @brief Coordinates the search bar UI, incremental query typing, debounce timers, and match navigation.
 */
class SearchController {
public:
    SearchController() = default;
    ~SearchController() = default;

    /// @brief Returns whether the floating search bar is currently visible.
    bool IsOpen() const { return m_showSearch; }

    /// @brief Toggles search bar visibility.
    void SetOpen(bool open) { m_showSearch = open; }

    /// @brief Opens the search bar.
    void Open() { m_showSearch = true; }

    /// @brief Closes the search bar and cancels active background scanning.
    void Close();

    /// @brief Returns the current search query string.
    const std::wstring& GetQuery() const { return m_searchQuery; }

    /// @brief Replaces current search query and marks highlights dirty.
    void SetQuery(const std::wstring& q) { m_searchQuery = q; InvalidateHighlights(); }

    /// @brief Appends a single character to query string (incremental find).
    void AppendQueryChar(wchar_t ch) { m_searchQuery.push_back(ch); InvalidateHighlights(); }

    /// @brief Removes trailing character from query string (backspace).
    void PopQueryChar() { if (!m_searchQuery.empty()) { m_searchQuery.pop_back(); InvalidateHighlights(); } }

    /// @brief Clears active search query string.
    void ClearQuery() { m_searchQuery.clear(); InvalidateHighlights(); }

    /// @brief Returns whether case-sensitive matching is enabled.
    bool IsMatchCase() const { return m_searchMatchCase; }

    /// @brief Toggles case sensitivity mode.
    void ToggleMatchCase() { m_searchMatchCase = !m_searchMatchCase; InvalidateHighlights(); }

    /// @brief Returns whether OCR fallback is enabled for scanned documents.
    bool IsOcrEnabled() const { return m_searchOcrEnabled; }

    /// @brief Toggles OCR fallback mode.
    void ToggleOcr() { m_searchOcrEnabled = !m_searchOcrEnabled; InvalidateHighlights(); }

    /// @brief Returns 1-based index of hovered button in search bar (0=none).
    int GetHoveredButton() const { return m_searchHoveredBtn; }

    /// @brief Sets index of currently hovered search button.
    void SetHoveredButton(int btn) { m_searchHoveredBtn = btn; }

    /// @brief Returns whether a debounce timer is waiting to trigger a search.
    bool IsDebouncePending() const { return m_searchDebouncePending; }

    /// @brief Sets state of pending debounce timer.
    void SetDebouncePending(bool pending) { m_searchDebouncePending = pending; }

    /// @brief Returns index of match last scrolled into view.
    int GetLastJumpedMatch() const { return m_lastJumpedMatch; }

    /// @brief Sets index of match last scrolled into view.
    void SetLastJumpedMatch(int idx) { m_lastJumpedMatch = idx; }

    /// @brief Accesses underlying asynchronous search engine.
    PdfSearchEngine& GetEngine() { return m_searchEngine; }

    /// @brief Const access to underlying asynchronous search engine.
    const PdfSearchEngine& GetEngine() const { return m_searchEngine; }

    /// @brief Marks cached search highlights dirty to trigger regeneration.
    void InvalidateHighlights() { m_highlightsDirty = true; }

    /// @brief Constructs rendering descriptor for search bar HUD.
    SearchBarRenderInfo GetSearchBarInfo(bool hasTabs) const;

    /// @brief Retrieves vector of highlight bounding boxes for document pages.
    const std::vector<SearchHighlight>& GetSearchHighlights() const;

    /**
     * @brief Hit-tests search bar controls (input, prev, next, case, OCR, close).
     * @param pt Mouse point in client coordinates.
     * @param winWidth Window width in DIPs.
     * @param dpi Monitor DPI scale.
     * @param hasTabs Whether tab strip is visible.
     * @return Button index hit (0=bar body, 1=prev, 2=next, 3=case, 4=OCR, 5=close, -1=outside).
     */
    int HitTest(POINT pt, float winWidth, float dpi, bool hasTabs) const;

    /**
     * @brief Initiates search worker on the active document tab.
     * @param hwnd Notification target HWND.
     * @param pTab Active document tab.
     */
    void StartSearch(HWND hwnd, DocumentTab* pTab);

    /// @brief Navigates to next match in document.
    bool NextMatch();

    /// @brief Navigates to previous match in document.
    bool PrevMatch();

    /// @brief Retrieves currently selected match.
    SearchMatch GetActiveMatch() const { return m_searchEngine.GetActiveMatch(); }

private:
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
};

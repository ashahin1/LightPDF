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

class SearchController {
public:
    SearchController() = default;
    ~SearchController() = default;

    bool IsOpen() const { return m_showSearch; }
    void SetOpen(bool open) { m_showSearch = open; }
    void Open() { m_showSearch = true; }
    void Close();

    const std::wstring& GetQuery() const { return m_searchQuery; }
    void SetQuery(const std::wstring& q) { m_searchQuery = q; InvalidateHighlights(); }
    void AppendQueryChar(wchar_t ch) { m_searchQuery.push_back(ch); InvalidateHighlights(); }
    void PopQueryChar() { if (!m_searchQuery.empty()) { m_searchQuery.pop_back(); InvalidateHighlights(); } }
    void ClearQuery() { m_searchQuery.clear(); InvalidateHighlights(); }

    bool IsMatchCase() const { return m_searchMatchCase; }
    void ToggleMatchCase() { m_searchMatchCase = !m_searchMatchCase; InvalidateHighlights(); }

    bool IsOcrEnabled() const { return m_searchOcrEnabled; }
    void ToggleOcr() { m_searchOcrEnabled = !m_searchOcrEnabled; InvalidateHighlights(); }

    int GetHoveredButton() const { return m_searchHoveredBtn; }
    void SetHoveredButton(int btn) { m_searchHoveredBtn = btn; }

    bool IsDebouncePending() const { return m_searchDebouncePending; }
    void SetDebouncePending(bool pending) { m_searchDebouncePending = pending; }

    int GetLastJumpedMatch() const { return m_lastJumpedMatch; }
    void SetLastJumpedMatch(int idx) { m_lastJumpedMatch = idx; }

    PdfSearchEngine& GetEngine() { return m_searchEngine; }
    const PdfSearchEngine& GetEngine() const { return m_searchEngine; }

    void InvalidateHighlights() { m_highlightsDirty = true; }

    SearchBarRenderInfo GetSearchBarInfo(bool hasTabs) const;
    const std::vector<SearchHighlight>& GetSearchHighlights() const;

    int HitTest(POINT pt, float winWidth, float dpi, bool hasTabs) const;
    void StartSearch(HWND hwnd, DocumentTab* pTab);

    bool NextMatch();
    bool PrevMatch();
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

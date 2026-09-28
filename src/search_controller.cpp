#include "search_controller.hpp"

void SearchController::Close() {
    m_searchDebouncePending = false;
    m_showSearch = false;
    m_searchHoveredBtn = 0;
    m_searchEngine.Cancel();
    m_searchEngine.Clear();
    m_lastJumpedMatch = -1;
    InvalidateHighlights();
}

SearchBarRenderInfo SearchController::GetSearchBarInfo(bool hasTabs) const {
    SearchBarRenderInfo info;
    info.visible = m_showSearch;
    info.hasTabs = hasTabs;
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

const std::vector<SearchHighlight>& SearchController::GetSearchHighlights() const {
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

int SearchController::HitTest(POINT pt, float winWidth, float dpi, bool hasTabs) const {
    if (!m_showSearch) return -1;

    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = winWidth * dipScale;

    D2D1_RECT_F bar = SearchBarLayout::GetBarRect(dipW, hasTabs);
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

void SearchController::StartSearch(HWND hwnd, DocumentTab* pTab) {
    m_searchDebouncePending = false;

    if (!pTab || !pTab->document.IsLoaded() || m_searchQuery.empty()) {
        m_searchEngine.Cancel();
        m_searchEngine.Clear();
        m_lastJumpedMatch = -1;
        InvalidateHighlights();
        return;
    }

    m_lastJumpedMatch = -1;
    InvalidateHighlights();
    m_searchEngine.StartSearch(
        hwnd,
        pTab->document.GetFilePath(),
        pTab->document.GetPageCount(),
        m_searchQuery,
        m_searchMatchCase,
        m_searchOcrEnabled,
        pTab->document.GetDoc(),
        pTab->textCache
    );
}

bool SearchController::NextMatch() {
    bool ok = m_searchEngine.NextMatch();
    if (ok) {
        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
        InvalidateHighlights();
    }
    return ok;
}

bool SearchController::PrevMatch() {
    bool ok = m_searchEngine.PrevMatch();
    if (ok) {
        m_lastJumpedMatch = m_searchEngine.GetActiveMatchIndex();
        InvalidateHighlights();
    }
    return ok;
}

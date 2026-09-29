#include "tab_controller.hpp"
#include <algorithm>

DocumentTab* TabController::GetActiveTab() {
    if (m_tabs.empty() || m_activeTab >= m_tabs.size()) return nullptr;
    return &m_tabs[m_activeTab];
}

const DocumentTab* TabController::GetActiveTab() const {
    if (m_tabs.empty() || m_activeTab >= m_tabs.size()) return nullptr;
    return &m_tabs[m_activeTab];
}

DocumentTab* TabController::GetTab(size_t index) {
    if (index >= m_tabs.size()) return nullptr;
    return &m_tabs[index];
}

const DocumentTab* TabController::GetTab(size_t index) const {
    if (index >= m_tabs.size()) return nullptr;
    return &m_tabs[index];
}

bool TabController::OpenTab(const std::wstring& path, HWND hwnd, std::wstring& outCanonicalPath, bool& outAlreadyOpen) {
    outAlreadyOpen = false;
    outCanonicalPath.clear();
    if (path.empty()) return false;

    wchar_t fullPath[MAX_PATH * 2] = { 0 };
    DWORD len = GetFullPathNameW(path.c_str(), _countof(fullPath), fullPath, nullptr);
    std::wstring resolvedPath = (len > 0 && len < _countof(fullPath)) ? fullPath : path;
    outCanonicalPath = resolvedPath;

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].document.IsLoaded() &&
            _wcsicmp(m_tabs[i].document.GetFilePath().c_str(), resolvedPath.c_str()) == 0) {
            SelectTab(i);
            outAlreadyOpen = true;
            return true;
        }
    }

    DocumentTab newTab;
    if (newTab.document.Open(resolvedPath, hwnd)) {
        newTab.currentPage = 0;
        newTab.zoomMode = ZoomMode::FitPage;
        newTab.continuousScroll = false;
        newTab.scrollY = 0.0f;
        newTab.textCache = std::make_shared<PageTextCache>();
        newTab.textCache->pages.resize(newTab.document.GetPageCount());
        newTab.selection.Clear();
        newTab.parser = std::make_unique<PdfParser>();
        newTab.parser->Load(resolvedPath);

        m_tabs.push_back(std::move(newTab));
        m_activeTab = m_tabs.size() - 1;
        return true;
    }
    return false;
}

bool TabController::CloseTab(size_t index) {
    if (index >= m_tabs.size()) return false;

    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_activeTab = 0;
        return true;
    }

    if (m_activeTab >= m_tabs.size()) {
        m_activeTab = m_tabs.size() - 1;
    } else if (m_activeTab > index) {
        m_activeTab--;
    }
    return true;
}

bool TabController::SelectTab(size_t index) {
    if (index >= m_tabs.size() || index == m_activeTab) return false;
    m_activeTab = index;
    return true;
}

bool TabController::NextTab() {
    if (m_tabs.size() <= 1) return false;
    m_activeTab = (m_activeTab + 1) % m_tabs.size();
    return true;
}

bool TabController::PrevTab() {
    if (m_tabs.size() <= 1) return false;
    m_activeTab = (m_activeTab == 0) ? (m_tabs.size() - 1) : (m_activeTab - 1);
    return true;
}

int TabController::HitTestTab(POINT pt, float winWidth, float dpi, bool& outClose, bool& outAdd) const {
    outClose = false;
    outAdd = false;
    if (m_tabs.size() <= 1) return -1;

    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;

    if (dipY < 0.0f || dipY > 34.0f) return -1;

    float dipWidth = winWidth * dipScale;
    float availW = dipWidth - 44.0f;
    float tabW = std::clamp(availW / (float)m_tabs.size(), 100.0f, 220.0f);

    float addX = (float)m_tabs.size() * tabW + 6.0f;
    if (dipX >= addX && dipX <= addX + 24.0f && dipY >= 5.0f && dipY <= 29.0f) {
        outAdd = true;
        return -1;
    }

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        float tx = (float)i * tabW;
        if (dipX >= tx && dipX < tx + tabW) {
            if (dipX >= tx + tabW - 24.0f && dipX <= tx + tabW - 8.0f && dipY >= 8.0f && dipY <= 26.0f) {
                outClose = true;
            }
            return (int)i;
        }
    }

    return -1;
}

void TabController::UpdateTabRenderInfos(std::vector<TabRenderInfo>& infos) const {
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

std::vector<TabRenderInfo> TabController::GetTabRenderInfos() const {
    std::vector<TabRenderInfo> infos;
    UpdateTabRenderInfos(infos);
    return infos;
}

void TabController::UpdateContinuousOffsets(const DocumentTab* pTab, float /*winWidth*/, float /*dipW*/, float /*dipH*/) const {
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
    float y = 24.0f;
    float gap = 12.0f;
    for (uint32_t i = 0; i < count; ++i) {
        pTab->pageOffsets[i] = y;
        y += pTab->document.GetPageSize(i).height * pTab->zoom;
        if (i + 1 < count) {
            y += gap;
        }
    }
    pTab->totalDocHeight = y + 24.0f;
    pTab->lastOffsetsZoom = pTab->zoom;
}

float TabController::GetTotalDocumentHeight(const DocumentTab* pTab) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0.0f;
    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != pTab->document.GetPageCount()) {
        UpdateContinuousOffsets(pTab, 0, 0, 0);
    }
    return pTab->totalDocHeight;
}

float TabController::GetPageYOffset(const DocumentTab* pTab, uint32_t pageIndex) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0.0f;
    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != pTab->document.GetPageCount()) {
        UpdateContinuousOffsets(pTab, 0, 0, 0);
    }
    if (pageIndex < pTab->pageOffsets.size()) {
        return pTab->pageOffsets[pageIndex];
    }
    return pTab->totalDocHeight;
}

uint32_t TabController::GetPageAtScrollOffset(const DocumentTab* pTab) const {
    if (!pTab || !pTab->document.IsLoaded()) return 0;
    uint32_t count = pTab->document.GetPageCount();
    if (count <= 1) return 0;

    if (pTab->lastOffsetsZoom != pTab->zoom || pTab->pageOffsets.size() != count) {
        UpdateContinuousOffsets(pTab, 0, 0, 0);
    }

    float targetY = pTab->scrollY + 400.0f;
    auto it = std::upper_bound(pTab->pageOffsets.begin(), pTab->pageOffsets.end(), targetY);
    if (it == pTab->pageOffsets.begin()) return 0;
    size_t idx = std::distance(pTab->pageOffsets.begin(), it) - 1;
    return (uint32_t)std::min(idx, (size_t)(count - 1));
}

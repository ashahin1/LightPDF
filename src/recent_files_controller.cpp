/**
 * @file recent_files_controller.cpp
 * @brief Implementation of RecentFilesController.
 */

#include "recent_files_controller.hpp"
#include "common_utils.hpp"
#include <cwchar>

RecentFilesController::RecentFilesController(const std::wstring& regSubKey)
    : m_regSubKey(regSubKey) {
    m_items.reserve(MAX_MRU_COUNT);
    m_closedTabs.reserve(MAX_CLOSED_TABS);
}

std::wstring RecentFilesController::NormalizePath(const std::wstring& path) {
    return PathUtils::NormalizePath(path);
}

std::wstring RecentFilesController::ExtractFileName(const std::wstring& path) {
    return PathUtils::GetFileName(path);
}

void RecentFilesController::LoadFromRegistry() {
    m_items.clear();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, m_regSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD count = 0;
    DWORD countSize = sizeof(count);
    DWORD type = REG_DWORD;
    if (RegQueryValueExW(hKey, L"Count", nullptr, &type, reinterpret_cast<LPBYTE>(&count), &countSize) != ERROR_SUCCESS) {
        count = 0;
    }
    if (count > MAX_MRU_COUNT) {
        count = static_cast<DWORD>(MAX_MRU_COUNT);
    }

    for (DWORD i = 0; i < count; ++i) {
        wchar_t valueName[32] = { 0 };
        swprintf_s(valueName, L"Slot_%u", i);

        wchar_t buffer[MAX_PATH * 4] = { 0 };
        DWORD bufferBytes = sizeof(buffer);
        DWORD valType = REG_SZ;
        if (RegQueryValueExW(hKey, valueName, nullptr, &valType, reinterpret_cast<LPBYTE>(buffer), &bufferBytes) == ERROR_SUCCESS) {
            // Expected format: "page|totalPages|timestamp|fullPath"
            wchar_t* nextToken = nullptr;
            wchar_t* tokenPage = wcstok_s(buffer, L"|", &nextToken);
            wchar_t* tokenTotal = tokenPage ? wcstok_s(nullptr, L"|", &nextToken) : nullptr;
            wchar_t* tokenTime = tokenTotal ? wcstok_s(nullptr, L"|", &nextToken) : nullptr;
            wchar_t* tokenPath = tokenTime ? nextToken : nullptr;

            if (tokenPage && tokenTotal && tokenTime && tokenPath && *tokenPath != L'\0') {
                RecentFileItem item;
                item.lastPage = static_cast<uint32_t>(_wtoi(tokenPage));
                item.totalPages = static_cast<uint32_t>(_wtoi(tokenTotal));
                item.lastOpenedTime = _wcstoui64(tokenTime, nullptr, 10);
                item.filePath = NormalizePath(tokenPath);
                item.fileName = ExtractFileName(item.filePath);

                // Discard duplicates if already in list
                auto it = std::find_if(m_items.begin(), m_items.end(), [&](const RecentFileItem& existing) {
                    return _wcsicmp(existing.filePath.c_str(), item.filePath.c_str()) == 0;
                });
                if (it == m_items.end() && !item.filePath.empty()) {
                    m_items.push_back(std::move(item));
                }
            }
        }
    }

    RegCloseKey(hKey);
}

void RecentFilesController::SaveToRegistry() const {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, m_regSubKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return;
    }

    DWORD count = static_cast<DWORD>(m_items.size());
    RegSetValueExW(hKey, L"Count", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&count), sizeof(count));

    for (DWORD i = 0; i < count; ++i) {
        wchar_t valueName[32] = { 0 };
        swprintf_s(valueName, L"Slot_%u", i);

        const auto& item = m_items[i];
        wchar_t payload[MAX_PATH * 4] = { 0 };
        swprintf_s(
            payload,
            L"%u|%u|%llu|%s",
            item.lastPage,
            item.totalPages,
            item.lastOpenedTime,
            item.filePath.c_str()
        );

        DWORD byteSize = static_cast<DWORD>((wcslen(payload) + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(payload), byteSize);
    }

    // Clean up any stale trailing slots beyond current count
    for (DWORD i = count; i < MAX_MRU_COUNT; ++i) {
        wchar_t valueName[32] = { 0 };
        swprintf_s(valueName, L"Slot_%u", i);
        RegDeleteValueW(hKey, valueName);
    }

    RegCloseKey(hKey);
}

void RecentFilesController::AddRecentFile(const std::wstring& path, uint32_t totalPages, uint32_t lastPage) {
    std::wstring normalized = NormalizePath(path);
    if (normalized.empty()) return;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    uint64_t nowTime = uli.QuadPart;

    // Check if already in list
    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const RecentFileItem& item) {
        return _wcsicmp(item.filePath.c_str(), normalized.c_str()) == 0;
    });

    RecentFileItem newItem;
    if (it != m_items.end()) {
        newItem = *it;
        m_items.erase(it);
    } else {
        newItem.filePath = normalized;
        newItem.fileName = ExtractFileName(normalized);
    }

    if (totalPages > 0) newItem.totalPages = totalPages;
    if (lastPage > 0 || newItem.lastPage == 0) newItem.lastPage = lastPage;
    newItem.lastOpenedTime = nowTime;

    // Insert at front
    m_items.insert(m_items.begin(), std::move(newItem));

    // Cap to maximum
    if (m_items.size() > MAX_MRU_COUNT) {
        m_items.resize(MAX_MRU_COUNT);
    }

    SaveToRegistry();
}

void RecentFilesController::UpdateLastPage(const std::wstring& path, uint32_t lastPage) {
    std::wstring normalized = NormalizePath(path);
    if (normalized.empty()) return;

    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const RecentFileItem& item) {
        return _wcsicmp(item.filePath.c_str(), normalized.c_str()) == 0;
    });

    if (it != m_items.end()) {
        it->lastPage = lastPage;
        SaveToRegistry();
    }
}

bool RecentFilesController::RemoveRecentFile(size_t index) {
    if (index >= m_items.size()) return false;
    m_items.erase(m_items.begin() + index);
    SaveToRegistry();
    return true;
}

bool RecentFilesController::RemoveRecentFileByPath(const std::wstring& path) {
    std::wstring normalized = NormalizePath(path);
    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const RecentFileItem& item) {
        return _wcsicmp(item.filePath.c_str(), normalized.c_str()) == 0;
    });
    if (it != m_items.end()) {
        m_items.erase(it);
        SaveToRegistry();
        return true;
    }
    return false;
}

void RecentFilesController::ClearRecentFiles() {
    m_items.clear();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, m_regSubKey.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        DWORD count = 0;
        RegSetValueExW(hKey, L"Count", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&count), sizeof(count));
        for (DWORD i = 0; i < MAX_MRU_COUNT; ++i) {
            wchar_t valueName[32] = { 0 };
            swprintf_s(valueName, L"Slot_%u", i);
            RegDeleteValueW(hKey, valueName);
        }
        RegCloseKey(hKey);
    }
}

void RecentFilesController::PushClosedTab(const ClosedTabInfo& tab) {
    if (tab.filePath.empty()) return;
    m_closedTabs.push_back(tab);
    if (m_closedTabs.size() > MAX_CLOSED_TABS) {
        m_closedTabs.erase(m_closedTabs.begin());
    }
}

bool RecentFilesController::PopClosedTab(ClosedTabInfo& outTab) {
    if (m_closedTabs.empty()) return false;
    outTab = m_closedTabs.back();
    m_closedTabs.pop_back();
    return true;
}

void RecentFilesController::ToggleSwitcher() {
    if (m_showSwitcher) {
        CloseSwitcher();
    } else {
        OpenSwitcher();
    }
}

void RecentFilesController::OpenSwitcher() {
    m_showSwitcher = true;
    m_switcherSelectedIndex = m_items.empty() ? -1 : 0;
    m_switcherHoveredIndex = -1;
}

void RecentFilesController::CloseSwitcher() {
    m_showSwitcher = false;
    m_switcherSelectedIndex = -1;
    m_switcherHoveredIndex = -1;
}

void RecentFilesController::SelectPrevSwitcherItem() {
    if (m_items.empty()) return;
    if (m_switcherSelectedIndex <= 0) {
        m_switcherSelectedIndex = static_cast<int>(m_items.size() - 1);
    } else {
        m_switcherSelectedIndex--;
    }
}

void RecentFilesController::SelectNextSwitcherItem() {
    if (m_items.empty()) return;
    if (m_switcherSelectedIndex < 0 || m_switcherSelectedIndex >= static_cast<int>(m_items.size() - 1)) {
        m_switcherSelectedIndex = 0;
    } else {
        m_switcherSelectedIndex++;
    }
}

void RecentFilesController::SetSwitcherSelectedIndex(int idx) {
    if (idx >= 0 && idx < static_cast<int>(m_items.size())) {
        m_switcherSelectedIndex = idx;
    }
}

RecentRenderInfo RecentFilesController::GetRenderInfo() const {
    RecentRenderInfo info;
    info.showSwitcher = m_showSwitcher;
    info.switcherSelectedIndex = m_switcherSelectedIndex;
    info.switcherHoveredIndex = m_switcherHoveredIndex;
    info.dashboardHoveredIndex = m_dashboardHoveredIndex;
    info.dashboardHoveredRemove = m_dashboardHoveredRemove;
    info.items = m_items;
    return info;
}

D2D1_RECT_F RecentFilesController::CalculateSwitcherCardRect(float dipWidth, float dipHeight) {
    constexpr float cardW = 540.0f;
    constexpr float cardH = 380.0f;
    float w = (std::min)(cardW, dipWidth - 24.0f);
    float h = (std::min)(cardH, dipHeight - 32.0f);
    float left = (std::max)(12.0f, (dipWidth - w) * 0.5f);
    float top = (std::max)(16.0f, (dipHeight - h) * 0.5f);
    return D2D1::RectF(left, top, left + w, top + h);
}

D2D1_RECT_F RecentFilesController::CalculateSwitcherItemRect(const D2D1_RECT_F& cardRect, size_t index) {
    constexpr float startYOffset = 64.0f;
    constexpr float rowH = 32.0f;
    float itemTop = cardRect.top + startYOffset + static_cast<float>(index) * rowH;
    return D2D1::RectF(cardRect.left + 14.0f, itemTop, cardRect.right - 14.0f, itemTop + rowH - 2.0f);
}

RecentItemLayout RecentFilesController::CalculateDashboardCardLayout(
    float dipWidth,
    float dipHeight,
    size_t index,
    size_t totalItems
) {
    RecentItemLayout layout;
    if (totalItems == 0) return layout;

    constexpr float maxDashboardW = 680.0f;
    float cardW = (std::min)(maxDashboardW, dipWidth - 48.0f);
    constexpr float rowH = 46.0f;
    constexpr float rowGap = 6.0f;

    float totalBlockH = static_cast<float>(totalItems) * (rowH + rowGap);
    // Center vertically in the viewport with a gentle bias towards the upper third
    float blockTop = (dipHeight - totalBlockH) * 0.42f;
    if (blockTop < 70.0f) blockTop = 70.0f;

    float left = (dipWidth - cardW) * 0.5f;
    float top = blockTop + static_cast<float>(index) * (rowH + rowGap);
    layout.itemRect = D2D1::RectF(left, top, left + cardW, top + rowH);

    // [1]..[9] Badge
    layout.badgeRect = D2D1::RectF(left + 8.0f, top + 10.0f, left + 34.0f, top + 36.0f);

    // Title / Filename
    layout.titleRect = D2D1::RectF(left + 42.0f, top + 6.0f, left + cardW - 40.0f, top + 26.0f);

    // Subtitle (Path and page stats)
    layout.subRect = D2D1::RectF(left + 42.0f, top + 26.0f, left + cardW - 40.0f, top + 42.0f);

    // Remove 'x' button
    layout.removeBtnRect = D2D1::RectF(left + cardW - 32.0f, top + 12.0f, left + cardW - 8.0f, top + 34.0f);

    return layout;
}

int RecentFilesController::HitTestSwitcher(
    D2D1_POINT_2F pt,
    float dipWidth,
    float dipHeight,
    size_t itemCount,
    bool& outCardClicked
) {
    D2D1_RECT_F card = CalculateSwitcherCardRect(dipWidth, dipHeight);
    outCardClicked = (pt.x >= card.left && pt.x <= card.right && pt.y >= card.top && pt.y <= card.bottom);
    if (!outCardClicked) return -1;

    for (size_t i = 0; i < itemCount; ++i) {
        D2D1_RECT_F itemR = CalculateSwitcherItemRect(card, i);
        if (pt.x >= itemR.left && pt.x <= itemR.right && pt.y >= itemR.top && pt.y <= itemR.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int RecentFilesController::HitTestDashboard(
    D2D1_POINT_2F pt,
    float dipWidth,
    float dipHeight,
    size_t itemCount,
    bool& outRemoveClicked
) {
    outRemoveClicked = false;
    for (size_t i = 0; i < itemCount; ++i) {
        RecentItemLayout layout = CalculateDashboardCardLayout(dipWidth, dipHeight, i, itemCount);
        if (pt.x >= layout.itemRect.left && pt.x <= layout.itemRect.right &&
            pt.y >= layout.itemRect.top && pt.y <= layout.itemRect.bottom) {
            if (pt.x >= layout.removeBtnRect.left && pt.x <= layout.removeBtnRect.right &&
                pt.y >= layout.removeBtnRect.top && pt.y <= layout.removeBtnRect.bottom) {
                outRemoveClicked = true;
            }
            return static_cast<int>(i);
        }
    }
    return -1;
}

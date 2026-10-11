/**
 * @file recent_files_controller.hpp
 * @brief Manages recent files history, closed-tab recovery, and quick switcher state.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <algorithm>
#include "tab_controller.hpp"

/// @brief Represents a single persistent recent document entry.
struct RecentFileItem {
    std::wstring filePath;          ///< Normalized full path to PDF file
    std::wstring fileName;          ///< Base filename for display (e.g. "sample.pdf")
    uint32_t lastPage = 0;          ///< 0-based index of last read page
    uint32_t totalPages = 0;        ///< Total document page count
    uint64_t lastOpenedTime = 0;    ///< FILETIME timestamp of last open
};

/// @brief State of an in-session closed tab for instant Ctrl+Shift+T restoration.
struct ClosedTabInfo {
    std::wstring filePath;          ///< Normalized file path
    uint32_t currentPage = 0;       ///< 0-based page index
    float zoom = 1.0f;              ///< Active zoom factor
    float scrollY = 0.0f;           ///< Continuous vertical scroll offset
    ZoomMode zoomMode = ZoomMode::FitPage; ///< Zoom mode
    bool continuousScroll = false;  ///< Whether continuous scroll was enabled
};

/// @brief Pure layout geometry for rendering a recent document card or row.
struct RecentItemLayout {
    D2D1_RECT_F itemRect = { 0, 0, 0, 0 };
    D2D1_RECT_F badgeRect = { 0, 0, 0, 0 };
    D2D1_RECT_F titleRect = { 0, 0, 0, 0 };
    D2D1_RECT_F subRect = { 0, 0, 0, 0 };
    D2D1_RECT_F removeBtnRect = { 0, 0, 0, 0 };
};

/// @brief Render information bundle passed to Direct2D overlay and dashboard views.
struct RecentRenderInfo {
    bool showSwitcher = false;      ///< Whether Ctrl+H quick switcher modal is active
    int switcherSelectedIndex = -1; ///< Active keyboard selection index in switcher (0..N-1)
    int switcherHoveredIndex = -1;  ///< Mouse hovered index in switcher
    int dashboardHoveredIndex = -1; ///< Mouse hovered index in empty-state dashboard
    int dashboardHoveredRemove = -1;///< Mouse hovered remove button in dashboard
    std::vector<RecentFileItem> items; ///< Up to 9 recent file entries
};

/**
 * @class RecentFilesController
 * @brief Handles MRU history persistence, closed tab stack, and quick switcher interaction.
 */
class RecentFilesController {
public:
    static constexpr size_t MAX_MRU_COUNT = 9;
    static constexpr size_t MAX_CLOSED_TABS = 15;

    /**
     * @brief Constructs the controller.
     * @param regSubKey Custom registry subkey path for isolation/testing.
     */
    explicit RecentFilesController(const std::wstring& regSubKey = L"Software\\LightPDF\\RecentFiles");

    ~RecentFilesController() = default;

    // --- MRU History Management ---

    /// @brief Loads recent files list from Windows Registry.
    void LoadFromRegistry();

    /// @brief Saves current recent files list to Windows Registry.
    void SaveToRegistry() const;

    /**
     * @brief Records or updates a document in recent history.
     * @param path Full or relative path to PDF file.
     * @param totalPages Total page count if known (0 if unknown).
     * @param lastPage Last read page index.
     */
    void AddRecentFile(const std::wstring& path, uint32_t totalPages = 0, uint32_t lastPage = 0);

    /**
     * @brief Updates the last read page for an existing recent file.
     * @param path Document path.
     * @param lastPage Last read page index.
     */
    void UpdateLastPage(const std::wstring& path, uint32_t lastPage);

    /// @brief Removes a single item from the recent files list by index.
    bool RemoveRecentFile(size_t index);

    /// @brief Removes a single item by its file path.
    bool RemoveRecentFileByPath(const std::wstring& path);

    /// @brief Clears all recent files and deletes registry entries.
    void ClearRecentFiles();

    /// @brief Returns the list of recent files (up to MAX_MRU_COUNT).
    const std::vector<RecentFileItem>& GetItems() const { return m_items; }

    /// @brief Returns true if there are no recent file records.
    bool IsEmpty() const { return m_items.empty(); }

    /// @brief Returns the count of recent items.
    size_t GetCount() const { return m_items.size(); }

    // --- Closed Tab Recovery (Ctrl+Shift+T) ---

    /// @brief Pushes a closed tab state onto the in-memory LIFO stack.
    void PushClosedTab(const ClosedTabInfo& tab);

    /// @brief Pops the most recently closed tab state if available.
    bool PopClosedTab(ClosedTabInfo& outTab);

    /// @brief Returns true if there is at least one closed tab in the stack.
    bool CanReopenClosedTab() const { return !m_closedTabs.empty(); }

    /// @brief Clears the in-memory closed tab stack.
    void ClearClosedTabs() { m_closedTabs.clear(); }

    // --- Quick Switcher Modal (Ctrl+H) ---

    /// @brief Toggles visibility of the Ctrl+H quick switcher modal.
    void ToggleSwitcher();

    /// @brief Opens the quick switcher modal.
    void OpenSwitcher();

    /// @brief Closes the quick switcher modal.
    void CloseSwitcher();

    /// @brief Returns whether the quick switcher modal is open.
    bool IsSwitcherOpen() const { return m_showSwitcher; }

    /// @brief Moves keyboard selection up in switcher.
    void SelectPrevSwitcherItem();

    /// @brief Moves keyboard selection down in switcher.
    void SelectNextSwitcherItem();

    /// @brief Gets active keyboard selected index in switcher.
    int GetSwitcherSelectedIndex() const { return m_switcherSelectedIndex; }

    /// @brief Sets active keyboard selected index in switcher.
    void SetSwitcherSelectedIndex(int idx);

    /// @brief Sets mouse hovered index in switcher.
    void SetSwitcherHoveredIndex(int idx) { m_switcherHoveredIndex = idx; }

    /// @brief Sets mouse hovered index on empty-state dashboard.
    void SetDashboardHoveredIndex(int idx, int removeIdx = -1) {
        m_dashboardHoveredIndex = idx;
        m_dashboardHoveredRemove = removeIdx;
    }

    /// @brief Populates the render info bundle for Direct2D drawing.
    RecentRenderInfo GetRenderInfo() const;

    // --- Pure Geometry & Hit-Testing Utilities ---

    /**
     * @brief Calculates layout bounding box for the Ctrl+H switcher dialog.
     * @param dipWidth Viewport width in DIPs.
     * @param dipHeight Viewport height in DIPs.
     * @return Centered modal card rectangle.
     */
    static D2D1_RECT_F CalculateSwitcherCardRect(float dipWidth, float dipHeight);

    /**
     * @brief Calculates layout bounds for an individual item inside the switcher.
     * @param cardRect Modal card rectangle.
     * @param index Item row index (0..count-1).
     * @return Bounding rectangle of the item row.
     */
    static D2D1_RECT_F CalculateSwitcherItemRect(const D2D1_RECT_F& cardRect, size_t index);

    /**
     * @brief Calculates layout bounds for an individual card in the empty-state dashboard.
     * @param dipWidth Viewport width in DIPs.
     * @param dipHeight Viewport height in DIPs.
     * @param index Card index (0..count-1).
     * @param totalItems Total items displayed.
     * @return Bounding layout descriptor for the card and sub-elements.
     */
    static RecentItemLayout CalculateDashboardCardLayout(
        float dipWidth,
        float dipHeight,
        size_t index,
        size_t totalItems
    );

    /**
     * @brief Hit-tests mouse coordinates against the switcher dialog.
     * @param pt Mouse point in DIPs.
     * @param dipWidth Viewport width.
     * @param dipHeight Viewport height.
     * @param itemCount Count of items currently in the list.
     * @param outCardClicked Set to true if click occurred inside modal card.
     * @return Clicked item index, or -1 if none.
     */
    static int HitTestSwitcher(
        D2D1_POINT_2F pt,
        float dipWidth,
        float dipHeight,
        size_t itemCount,
        bool& outCardClicked
    );

    /**
     * @brief Hit-tests mouse coordinates against the empty-state dashboard cards.
     * @param pt Mouse point in DIPs.
     * @param dipWidth Viewport width.
     * @param dipHeight Viewport height.
     * @param itemCount Count of items currently in the list.
     * @param outRemoveClicked Set to true if the small remove button was hit.
     * @return Clicked item index, or -1 if none.
     */
    static int HitTestDashboard(
        D2D1_POINT_2F pt,
        float dipWidth,
        float dipHeight,
        size_t itemCount,
        bool& outRemoveClicked
    );

    /**
     * @brief Normalizes a file path to its canonical absolute form.
     * @param path Input file path.
     * @return Resolved full path, or original if resolution fails.
     */
    static std::wstring NormalizePath(const std::wstring& path);

    /**
     * @brief Extracts filename from a path.
     * @param path Full file path.
     * @return File title.
     */
    static std::wstring ExtractFileName(const std::wstring& path);

private:
    std::wstring m_regSubKey;
    std::vector<RecentFileItem> m_items;
    std::vector<ClosedTabInfo> m_closedTabs;

    bool m_showSwitcher = false;
    int m_switcherSelectedIndex = 0;
    int m_switcherHoveredIndex = -1;
    int m_dashboardHoveredIndex = -1;
    int m_dashboardHoveredRemove = -1;
};

/**
 * @file settings_controller.hpp
 * @brief Persistent application settings model, registry serialization, and Options modal layout/hit-testing.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <cstdint>
#include <algorithm>
#include "tab_controller.hpp"

/// @brief Color filter theme applied to rendered PDF pages.
enum class PageTheme : uint32_t {
    Original = 0, ///< Standard white paper background
    Night    = 1, ///< Dark inverted contrast-friendly night reading mode
    Sepia    = 2  ///< Warm eye-care paper tint
};

/// @brief Persistent user preferences stored in HKCU\Software\LightPDF\Settings.
struct AppSettings {
    uint32_t defaultZoomMode   = 0; ///< 0 = FitPage, 1 = FitWidth, 2 = ActualSize (100%)
    bool defaultContinuous     = false;
    PageTheme pageTheme        = PageTheme::Original;
    bool rememberLastPage      = true;

    bool doubleClickLookup     = true;
    bool rightClickAutoSelect  = false;
    int32_t defaultTtsRate     = 0; ///< SAPI rate: -2 (0.75x), 0 (1.0x), 1 (1.25x), 2 (1.5x), 4 (2.0x)
    bool autoOcrSearch         = true;

    bool keepRecentHistory     = true;
};

/// @brief Hit-test identifiers for interactive controls inside the Options modal.
enum OptionsHitId : int {
    OPT_HIT_OUTSIDE        = -1,
    OPT_HIT_CARD_BODY      = 0,
    OPT_HIT_CLOSE_BTN      = 1,
    OPT_HIT_TAB_VIEW       = 10,
    OPT_HIT_TAB_TOOLS      = 11,
    OPT_HIT_TAB_SYSTEM     = 12,
    OPT_HIT_RESET_BTN      = 20,
    OPT_HIT_DONE_BTN       = 21,

    // Tab 0: View & Reading
    OPT_HIT_ZOOM_FIT_PAGE  = 100,
    OPT_HIT_ZOOM_FIT_WIDTH = 101,
    OPT_HIT_ZOOM_100       = 102,
    OPT_HIT_SCROLL_SINGLE  = 110,
    OPT_HIT_SCROLL_CONT    = 111,
    OPT_HIT_THEME_ORIG     = 120,
    OPT_HIT_THEME_NIGHT    = 121,
    OPT_HIT_THEME_SEPIA    = 122,
    OPT_HIT_REMEMBER_PAGE  = 130,

    // Tab 1: Selection & Speech
    OPT_HIT_DBLCLK_LOOKUP  = 200,
    OPT_HIT_RCLICK_SELECT  = 210,
    OPT_HIT_TTS_075X       = 220,
    OPT_HIT_TTS_100X       = 221,
    OPT_HIT_TTS_125X       = 222,
    OPT_HIT_TTS_150X       = 223,
    OPT_HIT_TTS_200X       = 224,
    OPT_HIT_AUTO_OCR       = 230,

    // Tab 2: System & Privacy
    OPT_HIT_SET_DEFAULT    = 300,
    OPT_HIT_UNREGISTER     = 301,
    OPT_HIT_KEEP_RECENT    = 310,
    OPT_HIT_CLEAR_RECENT   = 320
};

/// @brief Render state descriptor for the Direct2D Options modal overlay.
struct OptionsOverlayRenderInfo {
    bool visible = false;
    int activeCategory = 0; ///< 0 = View & Reading, 1 = Selection & Speech, 2 = System & Privacy
    int hoveredId = 0;      ///< OptionsHitId currently hovered by mouse
    size_t recentCount = 0; ///< Number of items currently in recent history
    AppSettings settings;
};

/// @brief Pure layout geometry calculator for the Options modal card.
struct OptionsOverlayLayout {
    static constexpr float WIDTH  = 640.0f;
    static constexpr float HEIGHT = 480.0f;

    static inline D2D1_RECT_F GetCardRect(float dipWidth, float dipHeight) {
        float w = (std::min)(WIDTH, (std::max)(320.0f, dipWidth - 24.0f));
        float h = (std::min)(HEIGHT, (std::max)(300.0f, dipHeight - 32.0f));
        float left = (std::max)(12.0f, (dipWidth - w) * 0.5f);
        float top  = (std::max)(16.0f, (dipHeight - h) * 0.5f);
        return D2D1::RectF(left, top, left + w, top + h);
    }

    static inline D2D1_RECT_F GetCloseBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 38.0f, card.top + 14.0f, card.right - 14.0f, card.top + 38.0f);
    }

    static inline D2D1_RECT_F GetCategoryTabRect(const D2D1_RECT_F& card, int index) {
        static const float widths[3] = { 136.0f, 152.0f, 144.0f };
        static const float gap = 8.0f;
        float x = card.left + 24.0f;
        for (int i = 0; i < index && i < 3; ++i) {
            x += widths[i] + gap;
        }
        float w = (index >= 0 && index < 3) ? widths[index] : 136.0f;
        float y = card.top + 52.0f;
        return D2D1::RectF(x, y, x + w, y + 28.0f);
    }

    static inline D2D1_RECT_F GetRowRect(const D2D1_RECT_F& card, int rowIndex) {
        float top = card.top + 104.0f + rowIndex * 76.0f;
        return D2D1::RectF(card.left + 24.0f, top, card.right - 24.0f, top + 64.0f);
    }

    static inline D2D1_RECT_F GetToggleRect(const D2D1_RECT_F& card, int rowIndex) {
        D2D1_RECT_F row = GetRowRect(card, rowIndex);
        float cy = (row.top + row.bottom) * 0.5f;
        return D2D1::RectF(row.right - 82.0f, cy - 14.0f, row.right - 10.0f, cy + 14.0f);
    }

    static inline D2D1_RECT_F GetSegmentedBtnRect(const D2D1_RECT_F& card, int rowIndex, int btnIndex, int totalBtns) {
        D2D1_RECT_F row = GetRowRect(card, rowIndex);
        float cy = (row.top + row.bottom) * 0.5f;
        float totalW = (totalBtns == 5) ? 260.0f : 252.0f;
        float gap = 6.0f;
        float btnW = (totalW - gap * (totalBtns - 1)) / static_cast<float>(totalBtns);
        float startX = row.right - 10.0f - totalW;
        float x = startX + btnIndex * (btnW + gap);
        return D2D1::RectF(x, cy - 14.0f, x + btnW, cy + 14.0f);
    }

    static inline D2D1_RECT_F GetResetBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.left + 24.0f, card.bottom - 44.0f, card.left + 154.0f, card.bottom - 14.0f);
    }

    static inline D2D1_RECT_F GetDoneBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 104.0f, card.bottom - 44.0f, card.right - 24.0f, card.bottom - 14.0f);
    }
};

/**
 * @class SettingsController
 * @brief Manages loading, saving, resetting, and querying user preferences in HKCU.
 */
class SettingsController {
public:
    explicit SettingsController(const std::wstring& regSubKey = L"Software\\LightPDF\\Settings");
    ~SettingsController() = default;

    /// @brief Loads settings from HKCU registry (gracefully falling back to defaults).
    void LoadFromRegistry();

    /// @brief Saves current settings to HKCU registry.
    void SaveToRegistry() const;

    /// @brief Resets all settings to factory defaults and saves to registry.
    void ResetToDefaults();

    /// @brief Mutable access to current settings.
    AppSettings& Get() { return m_settings; }

    /// @brief Const access to current settings.
    const AppSettings& Get() const { return m_settings; }

    /// @brief Cycles PageTheme (Original -> Night -> Sepia -> Original) and returns new theme.
    PageTheme CyclePageTheme();

    /// @brief Returns human-readable name for the given PageTheme.
    static const wchar_t* GetThemeName(PageTheme theme);

private:
    std::wstring m_regSubKey;
    AppSettings m_settings;
};

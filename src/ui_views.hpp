/**
 * @file ui_views.hpp
 * @brief Modular Direct2D HUD and overlay rendering components for LightPDF.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "d2d_renderer.hpp"

/**
 * @namespace UIViews
 * @brief Encapsulates decoupled Direct2D drawing subroutines for controls, HUDs, and popups.
 */
namespace UIViews {

// =========================================================================
// 1. TabStrip View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for rendering the tab strip.
struct TabStripResources {
    ID2D1SolidColorBrush* brushTabBarBg = nullptr;
    ID2D1SolidColorBrush* brushTabBorder = nullptr;
    ID2D1SolidColorBrush* brushTabActiveBg = nullptr;
    ID2D1SolidColorBrush* brushTabAccent = nullptr;
    ID2D1SolidColorBrush* brushTabHoverBg = nullptr;
    ID2D1SolidColorBrush* brushTabInactiveBg = nullptr;
    ID2D1SolidColorBrush* brushTabText = nullptr;
    ID2D1SolidColorBrush* brushTabTextInactive = nullptr;
    ID2D1SolidColorBrush* brushTabCloseHover = nullptr;
    IDWriteTextFormat* textFormatTab = nullptr;
    IDWriteTextFormat* textFormatTabClose = nullptr;
    IDWriteTextFormat* textFormatTabAdd = nullptr;
};

/// @brief Renders the multi-tab navigation bar at the top of the window.
class TabStripView {
public:
    /**
     * @brief Renders the tab strip bar.
     * @param ctx Active Direct2D device context.
     * @param tabs Vector of tab descriptions.
     * @param isAddHovered Whether the '+' new tab button is hovered.
     * @param dipWidth Window client width in DIPs.
     * @param res Resource bundle for tab strip rendering.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const std::vector<TabRenderInfo>& tabs,
        bool isAddHovered,
        float dipWidth,
        const TabStripResources& res
    );
};

// =========================================================================
// 2. ScrollBar View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for scrollbar and HUD badge.
struct ScrollBarResources {
    ID2D1SolidColorBrush* brushScrollbarTrack = nullptr;
    ID2D1SolidColorBrush* brushScrollbarThumb = nullptr;
    ID2D1SolidColorBrush* brushScrollbarThumbHover = nullptr;
    ID2D1SolidColorBrush* brushHudBg = nullptr;
    ID2D1SolidColorBrush* brushHudBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    IDWriteTextFormat* textFormatHud = nullptr;
};

/// @brief Renders the minimalist auto-hiding scrollbar and page number bubble HUD.
class ScrollBarView {
public:
    /**
     * @brief Renders the scrollbar track, thumb, and hover tooltip.
     * @param ctx Active Direct2D device context.
     * @param scrollbar Scrollbar layout and state metrics.
     * @param dipWidth Window client width in DIPs.
     * @param res Resource bundle for scrollbar rendering.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const ScrollbarRenderInfo& scrollbar,
        float dipWidth,
        const ScrollBarResources& res
    );
};

// =========================================================================
// 3. GoToPage Overlay View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for the Go To Page modal popup.
struct GoToPageOverlayResources {
    ID2D1SolidColorBrush* brushHelpBackdrop = nullptr;
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushHelpCardBg = nullptr;
    ID2D1SolidColorBrush* brushTabAccent = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    ID2D1SolidColorBrush* brushGoToPageBox = nullptr;
    ID2D1SolidColorBrush* brushTabBorder = nullptr;
    ID2D1SolidColorBrush* brushHelpSubText = nullptr;
    IDWriteTextFormat* textFormatHelpTitle = nullptr;
    IDWriteTextFormat* textFormatGoToPageInput = nullptr;
    IDWriteTextFormat* textFormatHelpSub = nullptr;
};

/// @brief Renders the modal numeric jump-to-page dialog (Ctrl+G).
class GoToPageOverlayView {
public:
    /**
     * @brief Renders the Go To Page dialog card.
     * @param ctx Active Direct2D device context.
     * @param buffer Current text input digits typed by the user.
     * @param totalPages Total page count of the active document.
     * @param dipWidth Window client width in DIPs.
     * @param dipHeight Window client height in DIPs.
     * @param res Resource bundle for Go To Page rendering.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const std::wstring& buffer,
        uint32_t totalPages,
        float dipWidth,
        float dipHeight,
        const GoToPageOverlayResources& res
    );
};

// =========================================================================
// 4. SearchBar View
// =========================================================================

/// @brief Text layout cache for the search bar input query and blinking caret.
struct SearchBarCache {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::wstring query;
    float layoutW = 0.0f;
    float layoutH = 0.0f;
    FLOAT caretX = 0.0f;
};

/// @brief Pre-allocated brush and typography resources for the floating search bar.
struct SearchBarResources {
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushHudBg = nullptr;
    ID2D1SolidColorBrush* brushHudBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    ID2D1SolidColorBrush* brushTabTextInactive = nullptr;
    ID2D1SolidColorBrush* brushHelpKeyText = nullptr;
    ID2D1SolidColorBrush* brushSearchBtnBg = nullptr;
    ID2D1SolidColorBrush* brushSearchBtnActive = nullptr;
    ID2D1SolidColorBrush* brushTabAccent = nullptr;
    ID2D1SolidColorBrush* brushTabCloseHover = nullptr;
    IDWriteTextFormat* textFormatSearchInput = nullptr;
    IDWriteTextFormat* textFormatSearchBadge = nullptr;
    IDWriteTextFormat* textFormatSearchBtn = nullptr;
};

/// @brief Renders the floating search bar with match count badge and navigation buttons.
class SearchBarView {
public:
    /**
     * @brief Renders the floating search bar.
     * @param ctx Active Direct2D device context.
     * @param dwriteFactory DirectWrite factory for creating text layouts.
     * @param searchBar Search bar state descriptor.
     * @param dipWidth Window client width in DIPs.
     * @param res Resource bundle for search bar rendering.
     * @param cache Reusable text layout cache for the input box.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        IDWriteFactory* dwriteFactory,
        const SearchBarRenderInfo& searchBar,
        float dipWidth,
        const SearchBarResources& res,
        SearchBarCache& cache
    );
};

// =========================================================================
// 5. HelpOverlay View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for the keyboard shortcut cheatsheet.
struct HelpOverlayResources {
    ID2D1SolidColorBrush* brushHelpBackdrop = nullptr;
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushHelpCardBg = nullptr;
    ID2D1SolidColorBrush* brushHelpCardBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    ID2D1SolidColorBrush* brushTabCloseHover = nullptr;
    ID2D1SolidColorBrush* brushHelpSubText = nullptr;
    ID2D1SolidColorBrush* brushSearchBtnActive = nullptr;
    ID2D1SolidColorBrush* brushTabAccent = nullptr;
    ID2D1SolidColorBrush* brushTabHoverBg = nullptr;
    ID2D1SolidColorBrush* brushSearchBtnBg = nullptr;
    ID2D1SolidColorBrush* brushTabTextInactive = nullptr;
    ID2D1SolidColorBrush* brushTabBorder = nullptr;
    ID2D1SolidColorBrush* brushPropsAccent = nullptr;
    ID2D1SolidColorBrush* brushHelpKeyText = nullptr;
    ID2D1SolidColorBrush* brushHelpDescText = nullptr;
    ID2D1SolidColorBrush* brushHelpRowAlt = nullptr;
    ID2D1SolidColorBrush* brushHelpKeycapBg = nullptr;
    ID2D1SolidColorBrush* brushHelpKeycapBorder = nullptr;

    IDWriteTextFormat* textFormatHelpTitle = nullptr;
    IDWriteTextFormat* textFormatTabClose = nullptr;
    IDWriteTextFormat* textFormatSearchBtn = nullptr;
    IDWriteTextFormat* textFormatHelpSection = nullptr;
    IDWriteTextFormat* textFormatHelpColKey = nullptr;
    IDWriteTextFormat* textFormatHelpColDesc = nullptr;
    IDWriteTextFormat* textFormatHelpSingleKey = nullptr;
    IDWriteTextFormat* textFormatHelpSingleDesc = nullptr;
    IDWriteTextFormat* textFormatHelpFooterLeft = nullptr;
    IDWriteTextFormat* textFormatHelpFooterRight = nullptr;
};

/// @brief Renders the categorization-enabled keyboard shortcuts and mouse actions cheatsheet modal (F1 / ?).
class HelpOverlayView {
public:
    /**
     * @brief Renders the shortcuts cheatsheet modal dialog.
     * @param ctx Active Direct2D device context.
     * @param help State and category filter descriptor.
     * @param dipWidth Window client width in DIPs.
     * @param dipHeight Window client height in DIPs.
     * @param res Resource bundle for help overlay.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const HelpOverlayRenderInfo& help,
        float dipWidth,
        float dipHeight,
        const HelpOverlayResources& res
    );

    /**
     * @brief Hit-tests category tabs and close button in help modal.
     * @param pt Mouse point in client coordinates.
     * @param width Window width in physical pixels.
     * @param height Window height in physical pixels.
     * @param dpi Monitor DPI scale.
     * @return Button/category index hit.
     */
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

// =========================================================================
// 6. DocProperties View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for document metadata dialog.
struct DocPropertiesResources {
    ID2D1SolidColorBrush* brushHelpBackdrop = nullptr;
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushHelpCardBg = nullptr;
    ID2D1SolidColorBrush* brushHelpCardBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    ID2D1SolidColorBrush* brushPropsSecBtnHover = nullptr;
    ID2D1SolidColorBrush* brushHelpSubText = nullptr;
    ID2D1SolidColorBrush* brushPropsAccent = nullptr;
    ID2D1SolidColorBrush* brushPropsBtn = nullptr;
    ID2D1SolidColorBrush* brushPropsBtnHover = nullptr;
    ID2D1SolidColorBrush* brushPropsSecBtn = nullptr;
    ID2D1SolidColorBrush* brushPropsSuccess = nullptr;
    ID2D1SolidColorBrush* brushPageBg = nullptr;

    IDWriteTextFormat* textFormatHelpTitle = nullptr;
    IDWriteTextFormat* textFormatTabClose = nullptr;
    IDWriteTextFormat* textFormatPropsSection = nullptr;
    IDWriteTextFormat* textFormatPropsLabel = nullptr;
    IDWriteTextFormat* textFormatTab = nullptr;
};

/// @brief Renders the PDF metadata properties modal dialog (Ctrl+D).
class DocPropertiesView {
public:
    /**
     * @brief Renders the document properties dialog card.
     * @param ctx Active Direct2D device context.
     * @param props Extracted document metadata properties.
     * @param dipWidth Window client width in DIPs.
     * @param dipHeight Window client height in DIPs.
     * @param res Resource bundle for document properties.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const DocumentPropertiesRenderInfo& props,
        float dipWidth,
        float dipHeight,
        const DocPropertiesResources& res
    );

    /**
     * @brief Hit-tests action buttons in document properties dialog (Close, Copy, OK).
     * @param pt Mouse point in client coordinates.
     * @param width Window width in physical pixels.
     * @param height Window height in physical pixels.
     * @param dpi Monitor DPI scale.
     * @return Button index hit.
     */
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

// =========================================================================
// 7. DictionaryCard View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for the dictionary lookup card.
struct DictionaryCardResources {
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushDictCardBg = nullptr;
    ID2D1SolidColorBrush* brushDictCardBorder = nullptr;
    ID2D1SolidColorBrush* brushDictTagBg = nullptr;
    ID2D1SolidColorBrush* brushDictTagText = nullptr;
    ID2D1SolidColorBrush* brushDictDefText = nullptr;
    ID2D1SolidColorBrush* brushDictHintText = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    IDWriteTextFormat* textFormatDictWord = nullptr;
    IDWriteTextFormat* textFormatDictTag = nullptr;
    IDWriteTextFormat* textFormatDictDef = nullptr;
    IDWriteTextFormat* textFormatDictHint = nullptr;
};

/// @brief Renders the floating contextual definition popup card near selected text.
class DictionaryCardView {
public:
    /**
     * @brief Renders the dictionary lookup card with category badge and Arabic translation.
     * @param ctx Active Direct2D device context.
     * @param dwriteFactory DirectWrite factory.
     * @param dictCard Dictionary entry descriptor and anchor rect.
     * @param dipWidth Window client width in DIPs.
     * @param dipHeight Window client height in DIPs.
     * @param topOffset Tab strip height offset.
     * @param res Resource bundle for dictionary card.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        IDWriteFactory* dwriteFactory,
        const DictionaryCardRenderInfo& dictCard,
        float dipWidth,
        float dipHeight,
        float topOffset,
        const DictionaryCardResources& res
    );
};

// =========================================================================
// 8. PresenterBar View
// =========================================================================

/// @brief Pre-allocated brush and typography resources for presenter floating controls.
struct PresenterBarResources {
    ID2D1SolidColorBrush* brushPageShadow = nullptr;
    ID2D1SolidColorBrush* brushHudBg = nullptr;
    ID2D1SolidColorBrush* brushHudBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    ID2D1SolidColorBrush* brushPresenterBtnHover = nullptr;
    ID2D1SolidColorBrush* brushPresenterBtnActive = nullptr;
    ID2D1SolidColorBrush* brushLaserCore = nullptr;
    IDWriteTextFormat* textFormatPresenter = nullptr;
};

/// @brief Renders the bottom floating toolbar for presenter mode (F5) and laser pointer controls (L / C).
class PresenterBarView {
public:
    /**
     * @brief Renders the presenter floating toolbar.
     * @param ctx Active Direct2D device context.
     * @param presenterBar Presenter bar state and laser color descriptor.
     * @param dipWidth Window client width in DIPs.
     * @param dipHeight Window client height in DIPs.
     * @param res Resource bundle for presenter bar.
     */
    static void Render(
        ID2D1DeviceContext* ctx,
        const PresenterBarRenderInfo& presenterBar,
        float dipWidth,
        float dipHeight,
        const PresenterBarResources& res
    );

    /**
     * @brief Hit-tests action buttons in presenter toolbar (prev, next, laser, color, exit).
     * @param pt Mouse point in client coordinates.
     * @param width Window width in physical pixels.
     * @param height Window height in physical pixels.
     * @param dpi Monitor DPI scale.
     * @return Button index hit.
     */
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

} // namespace UIViews

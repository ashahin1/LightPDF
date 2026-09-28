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

namespace UIViews {

// =========================================================================
// 1. TabStrip View
// =========================================================================
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

class TabStripView {
public:
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
struct ScrollBarResources {
    ID2D1SolidColorBrush* brushScrollbarTrack = nullptr;
    ID2D1SolidColorBrush* brushScrollbarThumb = nullptr;
    ID2D1SolidColorBrush* brushScrollbarThumbHover = nullptr;
    ID2D1SolidColorBrush* brushHudBg = nullptr;
    ID2D1SolidColorBrush* brushHudBorder = nullptr;
    ID2D1SolidColorBrush* brushHudText = nullptr;
    IDWriteTextFormat* textFormatHud = nullptr;
};

class ScrollBarView {
public:
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

class GoToPageOverlayView {
public:
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
struct SearchBarCache {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::wstring query;
    float layoutW = 0.0f;
    float layoutH = 0.0f;
    FLOAT caretX = 0.0f;
};

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

class SearchBarView {
public:
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

class HelpOverlayView {
public:
    static void Render(
        ID2D1DeviceContext* ctx,
        const HelpOverlayRenderInfo& help,
        float dipWidth,
        float dipHeight,
        const HelpOverlayResources& res
    );
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

// =========================================================================
// 6. DocProperties View
// =========================================================================
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

class DocPropertiesView {
public:
    static void Render(
        ID2D1DeviceContext* ctx,
        const DocumentPropertiesRenderInfo& props,
        float dipWidth,
        float dipHeight,
        const DocPropertiesResources& res
    );
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

// =========================================================================
// 7. DictionaryCard View
// =========================================================================
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

class DictionaryCardView {
public:
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

class PresenterBarView {
public:
    static void Render(
        ID2D1DeviceContext* ctx,
        const PresenterBarRenderInfo& presenterBar,
        float dipWidth,
        float dipHeight,
        const PresenterBarResources& res
    );
    static int HitTest(POINT pt, UINT width, UINT height, float dpi);
};

} // namespace UIViews

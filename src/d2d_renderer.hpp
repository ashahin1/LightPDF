/**
 * @file d2d_renderer.hpp
 * @brief Zero-copy hardware-accelerated Direct2D 1.1 / Direct3D 11 rendering pipeline.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>
#include <winrt/Windows.Data.Pdf.h>
#include <windows.data.pdf.interop.h>
#include <string>
#include <mutex>

using Microsoft::WRL::ComPtr;

/// @brief Cache entry for a rendered page Direct2D bitmap.
struct PageBitmapCache {
    uint32_t pageIndex = UINT32_MAX;    ///< 0-based page index
    float zoom = 0.0f;                  ///< Render zoom factor
    UINT32 pixelW = 0;                  ///< Bitmap pixel width
    UINT32 pixelH = 0;                  ///< Bitmap pixel height
    uint64_t lastUsedTime = 0;          ///< Timestamp for MRU/LRU eviction
    ComPtr<ID2D1Bitmap1> bitmap;        ///< GPU texture bitmap
};

/// @brief Tab state information passed to renderer for drawing tab strip.
struct TabRenderInfo {
    std::wstring title;                 ///< Tab title
    bool isActive = false;              ///< Whether tab is currently active
    bool isHovered = false;             ///< Whether tab header is hovered
    bool isCloseHovered = false;        ///< Whether tab close button is hovered
};

struct ContinuousPageInfo {
    winrt::Windows::Data::Pdf::PdfPage page{ nullptr };
    D2D1_SIZE_F pageSize = { 0.0f, 0.0f };
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    uint32_t pageIndex = 0;
    bool hasType3Font = false;
};

struct ScrollbarRenderInfo {
    bool visible = false;
    float trackY = 0.0f;
    float trackH = 0.0f;
    float thumbY = 0.0f;
    float thumbH = 0.0f;
    float alpha = 0.0f;
    bool isHovered = false;
    bool isDragging = false;
    uint32_t hoverPage = 0;
    uint32_t totalPages = 0;
};

struct SearchBarRenderInfo {
    bool visible = false;
    bool hasTabs = false;
    std::wstring query;
    uint32_t activeMatch = 0; // 1-based index (e.g. 1)
    uint32_t totalMatches = 0;
    bool matchCase = false;
    bool isSearching = false;
    bool isDebouncing = false;
    bool hasScanned = false;
    bool ocrEnabled = false;

    bool isPrevHovered = false;
    bool isNextHovered = false;
    bool isCaseHovered = false;
    bool isOcrHovered = false;
    bool isCloseHovered = false;
};

struct SearchBarLayout {
    static constexpr float WIDTH = 384.0f;
    static constexpr float HEIGHT = 36.0f;
    static constexpr float MARGIN_RIGHT = 24.0f;

    static inline D2D1_RECT_F GetBarRect(float dipWidth, bool hasTabs) {
        float x = dipWidth - WIDTH - MARGIN_RIGHT;
        float y = (hasTabs ? 34.0f : 0.0f) + 10.0f;
        return D2D1::RectF(x, y, x + WIDTH, y + HEIGHT);
    }

    static inline D2D1_RECT_F GetInputRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 10.0f, bar.top + 4.0f, bar.left + 160.0f, bar.bottom - 4.0f);
    }

    static inline D2D1_RECT_F GetBadgeRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 162.0f, bar.top + 4.0f, bar.left + 224.0f, bar.bottom - 4.0f);
    }

    static inline D2D1_RECT_F GetPrevBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 230.0f, bar.top + 5.0f, bar.left + 256.0f, bar.bottom - 5.0f);
    }

    static inline D2D1_RECT_F GetNextBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 258.0f, bar.top + 5.0f, bar.left + 284.0f, bar.bottom - 5.0f);
    }

    static inline D2D1_RECT_F GetCaseBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 286.0f, bar.top + 5.0f, bar.left + 314.0f, bar.bottom - 5.0f);
    }

    static inline D2D1_RECT_F GetOcrBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 316.0f, bar.top + 5.0f, bar.left + 348.0f, bar.bottom - 5.0f);
    }

    static inline D2D1_RECT_F GetCloseBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 350.0f, bar.top + 5.0f, bar.left + 376.0f, bar.bottom - 5.0f);
    }
};

struct SearchHighlight {
    uint32_t pageIndex = 0;
    D2D1_RECT_F pageRect = { 0, 0, 0, 0 }; // PDF page coordinates in DIPs
    std::vector<D2D1_RECT_F> rects;        // Individual line rectangles
    bool isActive = false;
};

struct SelectionHighlightSpan {
    uint32_t pageIndex = 0;
    std::vector<D2D1_RECT_F> rects;        // PDF page coordinates in DIPs
    bool isTtsHighlight = false;           // true = warm amber TTS word highlight, false = blue selection
};

/// @brief Render info for the floating TTS playback control bar.
struct TtsBarRenderInfo {
    bool visible = false;
    bool isPaused = false;
    std::wstring rateLabel;                ///< e.g. "1.0x", "1.5x"
    std::wstring voiceName;                ///< e.g. "Microsoft Naayf" or "Microsoft David"
    int hoveredBtn = -1;                   ///< -1=none, 0=prev, 1=play/pause, 2=next, 3=speed, 4=close
};

/// @brief Layout calculations for the floating TTS playback bar.
struct TtsBarLayout {
    static constexpr float BAR_WIDTH = 340.0f;
    static constexpr float BAR_HEIGHT = 38.0f;
    static constexpr float BTN_SIZE = 28.0f;

    static inline D2D1_RECT_F GetBarRect(float dipWidth, float topOffset) {
        float left = (dipWidth - BAR_WIDTH) * 0.5f;
        float top = topOffset + 12.0f;
        return D2D1::RectF(left, top, left + BAR_WIDTH, top + BAR_HEIGHT);
    }

    static inline D2D1_RECT_F GetBtnRect(const D2D1_RECT_F& bar, int index) {
        float cy = (bar.top + bar.bottom) * 0.5f;
        float by0 = cy - BTN_SIZE * 0.5f;
        float by1 = cy + BTN_SIZE * 0.5f;

        switch (index) {
        case 0: { // Prev sentence
            float bx0 = bar.left + 8.0f;
            return D2D1::RectF(bx0, by0, bx0 + BTN_SIZE, by1);
        }
        case 1: { // Play / Pause
            float bx0 = bar.left + 8.0f + BTN_SIZE + 4.0f;
            return D2D1::RectF(bx0, by0, bx0 + BTN_SIZE, by1);
        }
        case 2: { // Next sentence
            float bx0 = bar.left + 8.0f + (BTN_SIZE + 4.0f) * 2.0f;
            return D2D1::RectF(bx0, by0, bx0 + BTN_SIZE, by1);
        }
        case 3: { // Speed button
            float bx1 = bar.right - 8.0f - BTN_SIZE - 6.0f;
            return D2D1::RectF(bx1 - 50.0f, by0, bx1, by1);
        }
        case 4: { // Close [X]
            float bx1 = bar.right - 8.0f;
            return D2D1::RectF(bx1 - BTN_SIZE, by0, bx1, by1);
        }
        default:
            return D2D1::RectF(0, 0, 0, 0);
        }
    }

    static inline D2D1_RECT_F GetVoiceInfoRect(const D2D1_RECT_F& bar) {
        float left = bar.left + 8.0f + (BTN_SIZE + 4.0f) * 3.0f + 6.0f;
        float right = bar.right - 8.0f - BTN_SIZE - 6.0f - 54.0f;
        return D2D1::RectF(left, bar.top, (std::max)(left, right), bar.bottom);
    }
};

struct DictionaryCardRenderInfo {
    bool visible = false;
    D2D1_RECT_F anchorRect = { 0, 0, 0, 0 }; // Screen DIPs around target word/phrase
    std::wstring word;
    std::wstring definition;
    std::wstring categoryTag;
    uint16_t category = 0;
};

struct DictionaryCardLayout {
    static constexpr float WIDTH = 380.0f;
    static constexpr float MIN_HEIGHT = 130.0f;
    static constexpr float MAX_HEIGHT = 280.0f;

    static inline D2D1_RECT_F CalculateCardRect(const D2D1_RECT_F& anchorRect, float defHeight, float dipWidth, float dipHeight, float topOffset) {
        float cardWidth = (std::min)(WIDTH, (std::max)(100.0f, dipWidth - 24.0f));
        float cardHeight = std::clamp(38.0f + 10.0f + defHeight + 14.0f + 20.0f + 12.0f, MIN_HEIGHT, MAX_HEIGHT);

        float left = (anchorRect.left + anchorRect.right) * 0.5f - cardWidth * 0.5f;
        left = std::clamp(left, 12.0f, (std::max)(12.0f, dipWidth - cardWidth - 12.0f));

        // Prefer placing card above the anchor word
        float top = anchorRect.top - cardHeight - 8.0f;
        if (top < topOffset + 8.0f) {
            // Not enough room above, place below
            top = anchorRect.bottom + 8.0f;
        }
        if (top + cardHeight > dipHeight - 12.0f) {
            top = (std::max)(topOffset + 8.0f, dipHeight - cardHeight - 12.0f);
        }

        return D2D1::RectF(left, top, left + cardWidth, top + cardHeight);
    }
};

struct DocumentPropertiesRenderInfo {
    bool visible = false;
    // Document Information
    std::wstring title = L"—";
    std::wstring author = L"—";
    std::wstring subject = L"—";
    std::wstring keywords = L"—";
    std::wstring creator = L"—";
    std::wstring producer = L"—";

    // File & Page Details
    std::wstring totalPages = L"—";
    std::wstring fileSize = L"—";
    std::wstring pdfFormat = L"—";
    std::wstring pageSize = L"—";
    std::wstring created = L"—";
    std::wstring modified = L"—";

    // UI state
    int hoveredBtn = 0; // 0=none, 1=close, 2=copy, 3=ok
    bool copyFeedback = false;
};

struct DocumentPropertiesLayout {
    static constexpr float WIDTH = 540.0f;
    static constexpr float HEIGHT = 552.0f;

    static inline D2D1_RECT_F GetCardRect(float dipWidth, float dipHeight) {
        float left = (std::max)(10.0f, (dipWidth - WIDTH) * 0.5f);
        float top = (std::max)(10.0f, (dipHeight - HEIGHT) * 0.5f);
        return D2D1::RectF(left, top, left + WIDTH, top + HEIGHT);
    }

    static inline D2D1_RECT_F GetCloseBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 38.0f, card.top + 14.0f, card.right - 14.0f, card.top + 38.0f);
    }

    static inline D2D1_RECT_F GetCopyBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 170.0f, card.top + 504.0f, card.right - 86.0f, card.top + 536.0f);
    }

    static inline D2D1_RECT_F GetOkBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 76.0f, card.top + 504.0f, card.right - 24.0f, card.top + 536.0f);
    }
};

enum class LaserColor {
    Red,
    Green,
    Cyan,
    Gold
};

struct LaserPointerRenderInfo {
    bool active = false;
    D2D1_POINT_2F position = { 0.0f, 0.0f }; // in DIPs
    LaserColor color = LaserColor::Red;
};

struct PresenterBarRenderInfo {
    bool visible = false;
    int hoveredBtn = -1; // -1 = none, 0 = Prev, 1 = Next, 2 = Laser, 3 = Color, 4 = ExitFullscreen
    uint32_t currentPage = 0;
    uint32_t totalPages = 0;
    bool isLaserActive = false;
    LaserColor laserColor = LaserColor::Red;
};

struct PresenterBarLayout {
    static constexpr float WIDTH = 340.0f;
    static constexpr float HEIGHT = 44.0f;
    static constexpr float BOTTOM_MARGIN = 20.0f;

    static inline D2D1_RECT_F GetBarRect(float dipWidth, float dipHeight) {
        float left = (dipWidth - WIDTH) * 0.5f;
        float top = dipHeight - HEIGHT - BOTTOM_MARGIN;
        return D2D1::RectF(left, top, left + WIDTH, top + HEIGHT);
    }

    static inline D2D1_RECT_F GetPrevBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 6.0f, bar.top + 6.0f, bar.left + 46.0f, bar.bottom - 6.0f);
    }
    static inline D2D1_RECT_F GetPageInfoRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 48.0f, bar.top + 6.0f, bar.left + 148.0f, bar.bottom - 6.0f);
    }
    static inline D2D1_RECT_F GetNextBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 150.0f, bar.top + 6.0f, bar.left + 190.0f, bar.bottom - 6.0f);
    }
    static inline D2D1_RECT_F GetLaserBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 196.0f, bar.top + 6.0f, bar.left + 246.0f, bar.bottom - 6.0f);
    }
    static inline D2D1_RECT_F GetColorBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 250.0f, bar.top + 6.0f, bar.left + 290.0f, bar.bottom - 6.0f);
    }
    static inline D2D1_RECT_F GetExitBtnRect(const D2D1_RECT_F& bar) {
        return D2D1::RectF(bar.left + 294.0f, bar.top + 6.0f, bar.right - 6.0f, bar.bottom - 6.0f);
    }
};

struct HelpOverlayRenderInfo {
    bool visible = false;
    int activeCategory = 0; // 0=All, 1=Navigation, 2=Zoom & View, 3=Tabs & Files, 4=Search & Tools
    int hoveredCategory = -1;
    int hoveredClose = 0;   // 0=none, 1=close
};

struct HelpOverlayLayout {
    static constexpr float WIDTH = 860.0f;
    static constexpr float HEIGHT = 580.0f;

    static inline D2D1_RECT_F GetCardRect(float dipWidth, float dipHeight) {
        float w = (std::min)(WIDTH, dipWidth - 24.0f);
        float h = (std::min)(HEIGHT, dipHeight - 32.0f);
        float left = (std::max)(12.0f, (dipWidth - w) * 0.5f);
        float top = (std::max)(16.0f, (dipHeight - h) * 0.5f);
        return D2D1::RectF(left, top, left + w, top + h);
    }

    static inline D2D1_RECT_F GetCloseBtnRect(const D2D1_RECT_F& card) {
        return D2D1::RectF(card.right - 36.0f, card.top + 12.0f, card.right - 12.0f, card.top + 36.0f);
    }

    static inline D2D1_RECT_F GetCategoryTabRect(const D2D1_RECT_F& card, int index) {
        static const float widths[5] = { 80.0f, 122.0f, 132.0f, 126.0f, 152.0f };
        static const float gap = 6.0f;
        float x = card.left + 24.0f;
        for (int i = 0; i < index && i < 5; ++i) {
            x += widths[i] + gap;
        }
        float w = (index >= 0 && index < 5) ? widths[index] : 80.0f;
        float y = card.top + 46.0f;
        return D2D1::RectF(x, y, x + w, y + 26.0f);
    }
};

/**
 * @class D2DRenderer
 * @brief High-performance Direct2D 1.1 / Direct3D 11 hardware-accelerated rendering engine.
 */
class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    /**
     * @brief Initializes Direct3D device, Direct2D context, and DXGI swap chain.
     * @param hwnd Target window handle.
     * @return true if graphics pipeline initialized successfully.
     */
    bool Initialize(HWND hwnd);

    /**
     * @brief Releases all device-dependent graphics resources.
     */
    void Cleanup();

    /**
     * @brief Handles window resize events and re-creates swap chain buffers.
     * @param width New client width in physical pixels.
     * @param height New client height in physical pixels.
     */
    void Resize(UINT width, UINT height);

    /**
     * @brief Renders the empty/blank state (welcome screen or error message).
     * @param message Informational message to display.
     * @param help Help overlay descriptor.
     * @param tabs Tab strip descriptor.
     * @param isAddHovered Add tab button hover state.
     * @param showGoToPage Whether Go To Page modal is active.
     * @param goToPageBuffer Go To Page input text.
     * @param searchBar Search bar descriptor.
     * @param docProps Document properties descriptor.
     * @param dictCard Dictionary card descriptor.
     * @param laser Laser pointer descriptor.
     * @param presenterBar Presenter bar descriptor.
     */
    void RenderBlank(
        const std::wstring& message,
        const HelpOverlayRenderInfo& help = {},
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const DocumentPropertiesRenderInfo& docProps = {},
        const DictionaryCardRenderInfo& dictCard = {},
        const LaserPointerRenderInfo& laser = {},
        const PresenterBarRenderInfo& presenterBar = {},
        const TtsBarRenderInfo& ttsBar = {}
    );

    /**
     * @brief Renders a single PDF page centered or panned with all active HUD overlays.
     */
    void RenderPage(
        winrt::Windows::Data::Pdf::PdfPage page,
        float zoom,
        float offsetX,
        float offsetY,
        D2D1_SIZE_F pageSize,
        uint32_t currentPageIndex,
        uint32_t totalPages,
        const std::wstring& zoomModeText,
        bool hasType3Font = false,
        const HelpOverlayRenderInfo& help = {},
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        const ScrollbarRenderInfo& scrollbar = {},
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const std::vector<SearchHighlight>& highlights = {},
        const DocumentPropertiesRenderInfo& docProps = {},
        const std::vector<SelectionHighlightSpan>& selectionSpans = {},
        const DictionaryCardRenderInfo& dictCard = {},
        const LaserPointerRenderInfo& laser = {},
        const PresenterBarRenderInfo& presenterBar = {},
        const TtsBarRenderInfo& ttsBar = {}
    );

    /**
     * @brief Renders multiple visible pages in continuous vertical scroll layout.
     */
    void RenderContinuous(
        const std::vector<ContinuousPageInfo>& visiblePages,
        float zoom,
        uint32_t currentPageIndex,
        uint32_t totalPages,
        const std::wstring& zoomModeText,
        bool isContinuous,
        const HelpOverlayRenderInfo& help = {},
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        const ScrollbarRenderInfo& scrollbar = {},
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const std::vector<SearchHighlight>& highlights = {},
        const DocumentPropertiesRenderInfo& docProps = {},
        const std::vector<SelectionHighlightSpan>& selectionSpans = {},
        const DictionaryCardRenderInfo& dictCard = {},
        const LaserPointerRenderInfo& laser = {},
        const PresenterBarRenderInfo& presenterBar = {},
        const TtsBarRenderInfo& ttsBar = {}
    );

    /// @brief Draws the floating dictionary translation card.
    void DrawDictionaryCard(const DictionaryCardRenderInfo& dictCard);

    /// @brief Draws the presentation laser pointer dot and halo glow.
    void DrawLaserPointer(const LaserPointerRenderInfo& laser);

    /// @brief Draws the presenter floating control bar.
    void DrawPresenterBar(const PresenterBarRenderInfo& presenterBar);

    /// @brief Draws the Read Aloud floating playback control bar.
    void DrawTtsBar(const TtsBarRenderInfo& ttsBar, float topOffset = 0.0f);

    /// @brief Hit-tests document properties modal buttons.
    int HitTestDocumentProperties(POINT pt) const;

    /// @brief Hit-tests keyboard shortcut help modal buttons.
    int HitTestHelpOverlay(POINT pt) const;

    /// @brief Hit-tests presenter floating toolbar buttons.
    int HitTestPresenterBar(POINT pt) const;

    /// @brief Hit-tests Read Aloud floating toolbar buttons.
    int HitTestTtsBar(POINT pt, bool hasTabs = false) const;

    /**
     * @brief Renders a page directly to a printer device context (HDC).
     * @param page WinRT PDF page.
     * @param hdc Printer device context.
     * @param pageSize Dimensions of page.
     * @param hasType3Font Whether page uses Type 3 fonts.
     * @return true on success.
     */
    bool PrintPageToHdc(
        winrt::Windows::Data::Pdf::PdfPage page,
        HDC hdc,
        D2D1_SIZE_F pageSize,
        bool hasType3Font = false
    );

    /// @brief Returns current monitor DPI.
    float GetDpi() const { return m_dpi; }

    /// @brief Updates DPI scaling and recreates typography layouts.
    void UpdateDpi(float dpi);

    /// @brief Window client width in physical pixels.
    UINT GetWidth() const { return m_width; }

    /// @brief Window client height in physical pixels.
    UINT GetHeight() const { return m_height; }

    void InvalidatePageCache() {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        m_pageCache = PageBitmapCache();
        m_continuousPageCache.clear();
    }

private:
    bool CreateDeviceIndependentResources();
    bool CreateDeviceResources();
    bool CreateWindowSizeDependentResources();
    void DiscardDeviceResources();
    void DrawHelpOverlay(const HelpOverlayRenderInfo& help);
    void DrawTabBar(const std::vector<TabRenderInfo>& tabs, bool isAddHovered);
    void DrawScrollbar(const ScrollbarRenderInfo& scrollbar);
    void DrawGoToPageOverlay(const std::wstring& buffer, uint32_t totalPages);
    void DrawSearchBar(const SearchBarRenderInfo& searchBar);
    void DrawDocumentProperties(const DocumentPropertiesRenderInfo& props);
    void DrawOverlays(
        const std::vector<TabRenderInfo>& tabs,
        bool isAddHovered,
        const ScrollbarRenderInfo* pScrollbar,
        bool showGoToPage,
        const std::wstring& goToPageBuffer,
        uint32_t totalPages,
        const SearchBarRenderInfo& searchBar,
        const HelpOverlayRenderInfo& help,
        const DocumentPropertiesRenderInfo& docProps,
        const DictionaryCardRenderInfo* pDictCard = nullptr,
        const LaserPointerRenderInfo* pLaser = nullptr,
        const PresenterBarRenderInfo* pPresenterBar = nullptr,
        const TtsBarRenderInfo* pTtsBar = nullptr
    );

    HWND m_hwnd = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    float m_dpi = 96.0f;

    // Direct3D & DXGI
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain1> m_swapChain;

    // Direct2D
    ComPtr<ID2D1Factory1> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;

    // DirectWrite (HUD overlay)
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormatHud;
    ComPtr<IDWriteTextFormat> m_textFormatBlank;
    ComPtr<IDWriteTextFormat> m_textFormatHelpTitle;
    ComPtr<IDWriteTextFormat> m_textFormatHelpSub;
    ComPtr<IDWriteTextFormat> m_textFormatHelpKey;
    ComPtr<IDWriteTextFormat> m_textFormatHelpDesc;
    ComPtr<IDWriteTextFormat> m_textFormatHelpSection;
    ComPtr<IDWriteTextFormat> m_textFormatHelpColKey;
    ComPtr<IDWriteTextFormat> m_textFormatHelpColDesc;
    ComPtr<IDWriteTextFormat> m_textFormatHelpSingleKey;
    ComPtr<IDWriteTextFormat> m_textFormatHelpSingleDesc;
    ComPtr<IDWriteTextFormat> m_textFormatHelpFooterLeft;
    ComPtr<IDWriteTextFormat> m_textFormatHelpFooterRight;
    ComPtr<IDWriteTextFormat> m_textFormatTab;
    ComPtr<IDWriteTextFormat> m_textFormatTabClose;
    ComPtr<IDWriteTextFormat> m_textFormatTabAdd;
    ComPtr<IDWriteTextFormat> m_textFormatGoToPageInput;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushBg;
    ComPtr<ID2D1SolidColorBrush> m_brushPageBg;
    ComPtr<ID2D1SolidColorBrush> m_brushPageShadow;
    ComPtr<ID2D1SolidColorBrush> m_brushPageBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushHudBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHudBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushHudText;
    ComPtr<ID2D1SolidColorBrush> m_brushBlankText;

    // Help Overlay Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushHelpBackdrop;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpCardBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpCardBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpKeyText;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpDescText;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpSubText;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpKeycapBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpKeycapBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushHelpRowAlt;

    // Tab Bar Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushTabBarBg;
    ComPtr<ID2D1SolidColorBrush> m_brushTabActiveBg;
    ComPtr<ID2D1SolidColorBrush> m_brushTabInactiveBg;
    ComPtr<ID2D1SolidColorBrush> m_brushTabHoverBg;
    ComPtr<ID2D1SolidColorBrush> m_brushTabBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushTabAccent;
    ComPtr<ID2D1SolidColorBrush> m_brushTabCloseHover;
    ComPtr<ID2D1SolidColorBrush> m_brushTabText;
    ComPtr<ID2D1SolidColorBrush> m_brushTabTextInactive;

    // Scrollbar & Go to Page Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushScrollbarTrack;
    ComPtr<ID2D1SolidColorBrush> m_brushScrollbarThumb;
    ComPtr<ID2D1SolidColorBrush> m_brushScrollbarThumbHover;
    ComPtr<ID2D1SolidColorBrush> m_brushGoToPageBox;

    // Search Bar & Highlight Brushes & Formats
    ComPtr<IDWriteTextFormat> m_textFormatSearchInput;
    ComPtr<IDWriteTextFormat> m_textFormatSearchBadge;
    ComPtr<IDWriteTextFormat> m_textFormatSearchBtn;

    ComPtr<ID2D1SolidColorBrush> m_brushSearchHighlight;
    ComPtr<ID2D1SolidColorBrush> m_brushSearchActiveHighlight;
    ComPtr<ID2D1SolidColorBrush> m_brushSearchActiveBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushSearchBtnBg;
    ComPtr<ID2D1SolidColorBrush> m_brushSearchBtnActive;
    ComPtr<ID2D1SolidColorBrush> m_brushTextSelection;

    // Document Properties Formats & Brushes
    ComPtr<IDWriteTextFormat> m_textFormatPropsLabel;
    ComPtr<IDWriteTextFormat> m_textFormatPropsSection;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsAccent;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsBtn;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSecBtn;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSecBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSuccess;

    // Dictionary Card Formats & Brushes
    ComPtr<IDWriteTextFormat> m_textFormatDictWord;
    ComPtr<IDWriteTextFormat> m_textFormatDictTag;
    ComPtr<IDWriteTextFormat> m_textFormatDictDef;
    ComPtr<IDWriteTextFormat> m_textFormatDictHint;
    ComPtr<ID2D1SolidColorBrush> m_brushDictCardBg;
    ComPtr<ID2D1SolidColorBrush> m_brushDictCardBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushDictTagBg;
    ComPtr<ID2D1SolidColorBrush> m_brushDictTagText;
    ComPtr<ID2D1SolidColorBrush> m_brushDictDefText;
    ComPtr<ID2D1SolidColorBrush> m_brushDictHintText;

    // Laser Pointer & Presenter Bar Brushes & Formats
    ComPtr<IDWriteTextFormat> m_textFormatPresenter;
    ComPtr<ID2D1SolidColorBrush> m_brushLaserOuter;
    ComPtr<ID2D1SolidColorBrush> m_brushLaserMiddle;
    ComPtr<ID2D1SolidColorBrush> m_brushLaserCore;
    ComPtr<ID2D1SolidColorBrush> m_brushPresenterBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushPresenterBtnActive;

    // Read Aloud (TTS) Formats & Brushes
    ComPtr<IDWriteTextFormat> m_textFormatTtsBar;
    ComPtr<IDWriteTextFormat> m_textFormatTtsSpeed;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsHighlight;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsBarBg;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsBarBorder;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsBarText;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsBarBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushTtsBarBtnActive;

    // Native PDF Hardware Renderer
    ComPtr<IPdfRendererNative> m_pdfRenderer;

    // WIC Factory for safe stream rasterization fallback (e.g. Type 3 fonts)
    ComPtr<IWICImagingFactory> m_wicFactory;

    bool RasterizePageViaStream(
        winrt::Windows::Data::Pdf::PdfPage page,
        UINT32 renderW,
        UINT32 renderH,
        ComPtr<ID2D1Bitmap1>& outBitmap
    );

    // Fast Page Bitmap Cache for zero-cost pan & UI hover blits (single page mode)
    PageBitmapCache m_pageCache;

    // Multi-page LRU Bitmap Cache for smooth continuous scroll (up to 8 pages)
    static constexpr size_t MAX_CONTINUOUS_CACHED_PAGES = 8;
    std::vector<PageBitmapCache> m_continuousPageCache;
    uint64_t m_continuousCacheClock = 0;

    // Cached print textures and readback buffer to avoid 70MB allocation/deallocation per page
    ComPtr<ID3D11Texture2D> m_printRenderTexture;
    ComPtr<ID3D11Texture2D> m_printStagingTexture;
    ComPtr<ID2D1Bitmap1> m_printTargetBitmap;
    UINT m_cachedPrintW = 0;
    UINT m_cachedPrintH = 0;
    std::vector<uint8_t> m_printPixelBuffer;

    // Cached DirectWrite text layout for search bar
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_cachedSearchLayout;
    std::wstring m_cachedSearchQuery;
    float m_cachedSearchLayoutW = 0.0f;
    float m_cachedSearchLayoutH = 0.0f;
    FLOAT m_cachedSearchCaretX = 0.0f;

    // Mutex for thread-safe rendering between UI and background print worker
    std::mutex m_renderMutex;
};

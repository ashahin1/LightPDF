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
#include <winrt/Windows.Data.Pdf.h>
#include <windows.data.pdf.interop.h>
#include <string>
#include <mutex>

using Microsoft::WRL::ComPtr;

struct TabRenderInfo {
    std::wstring title;
    bool isActive = false;
    bool isHovered = false;
    bool isCloseHovered = false;
};

struct ContinuousPageInfo {
    winrt::Windows::Data::Pdf::PdfPage page{ nullptr };
    D2D1_SIZE_F pageSize = { 0.0f, 0.0f };
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    uint32_t pageIndex = 0;
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
        float left = std::max(10.0f, (dipWidth - WIDTH) * 0.5f);
        float top = std::max(10.0f, (dipHeight - HEIGHT) * 0.5f);
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

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    bool Initialize(HWND hwnd);
    void Cleanup();
    void Resize(UINT width, UINT height);

    void RenderBlank(
        const std::wstring& message,
        bool showHelp = false,
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const DocumentPropertiesRenderInfo& docProps = {}
    );
    void RenderPage(
        winrt::Windows::Data::Pdf::PdfPage page,
        float zoom,
        float offsetX,
        float offsetY,
        D2D1_SIZE_F pageSize,
        uint32_t currentPageIndex,
        uint32_t totalPages,
        const std::wstring& zoomModeText,
        bool showHelp = false,
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        const ScrollbarRenderInfo& scrollbar = {},
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const std::vector<SearchHighlight>& highlights = {},
        const DocumentPropertiesRenderInfo& docProps = {}
    );
    void RenderContinuous(
        const std::vector<ContinuousPageInfo>& visiblePages,
        float zoom,
        uint32_t currentPageIndex,
        uint32_t totalPages,
        const std::wstring& zoomModeText,
        bool isContinuous,
        bool showHelp = false,
        const std::vector<TabRenderInfo>& tabs = {},
        bool isAddHovered = false,
        const ScrollbarRenderInfo& scrollbar = {},
        bool showGoToPage = false,
        const std::wstring& goToPageBuffer = L"",
        const SearchBarRenderInfo& searchBar = {},
        const std::vector<SearchHighlight>& highlights = {},
        const DocumentPropertiesRenderInfo& docProps = {}
    );

    int HitTestDocumentProperties(POINT pt) const;

    bool PrintPageToHdc(
        winrt::Windows::Data::Pdf::PdfPage page,
        HDC hdc,
        D2D1_SIZE_F pageSize
    );

    float GetDpi() const { return m_dpi; }
    void UpdateDpi(float dpi);

    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }

private:
    bool CreateDeviceIndependentResources();
    bool CreateDeviceResources();
    bool CreateWindowSizeDependentResources();
    void DiscardDeviceResources();
    void DrawHelpOverlay();
    void DrawTabBar(const std::vector<TabRenderInfo>& tabs, bool isAddHovered);
    void DrawScrollbar(const ScrollbarRenderInfo& scrollbar);
    void DrawGoToPageOverlay(const std::wstring& buffer, uint32_t totalPages);
    void DrawSearchBar(const SearchBarRenderInfo& searchBar);
    void DrawDocumentProperties(const DocumentPropertiesRenderInfo& props);

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

    // Document Properties Formats & Brushes
    ComPtr<IDWriteTextFormat> m_textFormatPropsLabel;
    ComPtr<IDWriteTextFormat> m_textFormatPropsSection;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsAccent;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsBtn;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSecBtn;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSecBtnHover;
    ComPtr<ID2D1SolidColorBrush> m_brushPropsSuccess;

    // Native PDF Hardware Renderer
    ComPtr<IPdfRendererNative> m_pdfRenderer;

    // Mutex for thread-safe rendering between UI and background print worker
    std::mutex m_renderMutex;
};

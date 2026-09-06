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
        const std::wstring& goToPageBuffer = L""
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
        const std::wstring& goToPageBuffer = L""
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
        const std::wstring& goToPageBuffer = L""
    );

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

    // Native PDF Hardware Renderer
    ComPtr<IPdfRendererNative> m_pdfRenderer;

    // Mutex for thread-safe rendering between UI and background print worker
    std::mutex m_renderMutex;
};

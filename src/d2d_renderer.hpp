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

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    bool Initialize(HWND hwnd);
    void Cleanup();
    void Resize(UINT width, UINT height);

    void RenderBlank(const std::wstring& message, bool showHelp = false);
    void RenderPage(
        winrt::Windows::Data::Pdf::PdfPage page,
        float zoom,
        float offsetX,
        float offsetY,
        D2D1_SIZE_F pageSize,
        uint32_t currentPageIndex,
        uint32_t totalPages,
        const std::wstring& zoomModeText,
        bool showHelp = false
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

    // Native PDF Hardware Renderer
    ComPtr<IPdfRendererNative> m_pdfRenderer;

    // Mutex for thread-safe rendering between UI and background print worker
    std::mutex m_renderMutex;
};

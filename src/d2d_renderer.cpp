#include "d2d_renderer.hpp"
#include "settings_controller.hpp"
#include "ui_views.hpp"
#include <cmath>

// Direct2D Color Matrix Effect CLSID {921F03D6-641C-47DF-852D-B4BB6153AE11}
EXTERN_C const GUID CLSID_D2D1ColorMatrix = { 0x921F03D6, 0x641C, 0x47DF, { 0x85, 0x2D, 0xB4, 0xBB, 0x61, 0x53, 0xAE, 0x11 } };
#include <algorithm>
#include <shcore.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windows.data.pdf.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shcore.lib")

D2DRenderer::D2DRenderer() = default;

D2DRenderer::~D2DRenderer() {
    Cleanup();
}

bool D2DRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // Query initial DPI
    m_dpi = (float)GetDpiForWindow(hwnd);
    if (m_dpi <= 0.0f) m_dpi = 96.0f;

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = std::max(1L, rc.right - rc.left);
    m_height = std::max(1L, rc.bottom - rc.top);

    if (!CreateDeviceIndependentResources()) return false;
    if (!CreateDeviceResources()) return false;
    if (!CreateWindowSizeDependentResources()) return false;

    return true;
}

void D2DRenderer::Cleanup() {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    DiscardDeviceResources();
    m_dwriteFactory = nullptr;
    m_textFormatHud = nullptr;
    m_textFormatBlank = nullptr;
    m_textFormatHelpTitle = nullptr;
    m_textFormatHelpSub = nullptr;
    m_textFormatHelpKey = nullptr;
    m_textFormatHelpDesc = nullptr;
    m_textFormatTab = nullptr;
    m_textFormatTabClose = nullptr;
    m_textFormatTabAdd = nullptr;
    m_textFormatGoToPageInput = nullptr;
    m_textFormatSearchInput = nullptr;
    m_textFormatSearchBadge = nullptr;
    m_textFormatSearchBtn = nullptr;
    m_wicFactory = nullptr;
    m_d2dFactory = nullptr;
}

void D2DRenderer::UpdateDpi(float dpi) {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_dpi = dpi;
    if (m_d2dContext) {
        m_d2dContext->SetDpi(m_dpi, m_dpi);
    }
}

void D2DRenderer::Resize(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    if (m_width == width && m_height == height) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_width = width;
    m_height = height;

    CreateWindowSizeDependentResources();
}

namespace {
bool CreateFormat(
    IDWriteFactory* factory,
    ComPtr<IDWriteTextFormat>& outFormat,
    const wchar_t* fontName,
    float sizeDip,
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL,
    DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
    DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
    bool noWrap = false,
    bool ellipsis = false,
    const wchar_t* locale = L"en-us"
) {
    if (!factory) return false;
    HRESULT hr = factory->CreateTextFormat(
        fontName,
        nullptr,
        weight,
        style,
        DWRITE_FONT_STRETCH_NORMAL,
        sizeDip,
        locale,
        &outFormat
    );
    if (FAILED(hr)) return false;

    outFormat->SetTextAlignment(textAlign);
    outFormat->SetParagraphAlignment(paraAlign);
    if (noWrap) {
        outFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    if (ellipsis) {
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> inlineEllipsis;
        if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(outFormat.Get(), &inlineEllipsis))) {
            outFormat->SetTrimming(&trimming, inlineEllipsis.Get());
        }
    }
    return true;
}
} // namespace

bool D2DRenderer::CreateDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS options = {};
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        &options,
        &m_d2dFactory
    );
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        &m_dwriteFactory
    );
    if (FAILED(hr)) return false;

    // HUD & Screen Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHud, L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatBlank, L"Segoe UI", 16.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    // Help Overlay Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpTitle, L"Segoe UI", 17.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpSub, L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpKey, L"Consolas", 12.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpDesc, L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpSection, L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpColKey, L"Consolas", 10.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true, true)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpColDesc, L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true, true)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpSingleKey, L"Consolas", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    m_textFormatHelpSingleDesc = m_textFormatHelpDesc;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpFooterLeft, L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatHelpFooterRight, L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    // Tab Bar & Navigation Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatTab, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true, true)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatTabClose, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatTabAdd, L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatGoToPageInput, L"Segoe UI", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    // Search Bar Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatSearchInput, L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true, false)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatSearchBadge, L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    m_textFormatSearchBtn = m_textFormatSearchBadge;

    // Properties Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatPropsLabel, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatPropsSection, L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    // Dictionary Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatDictWord, L"Segoe UI", 14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatDictTag, L"Segoe UI", 10.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatDictDef, L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false, false, L"ar-sa")) return false;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatDictHint, L"Segoe UI", 9.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    // Presenter & TTS Formats
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatPresenter, L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;
    m_textFormatTtsBar = m_textFormatPresenter;
    if (!CreateFormat(m_dwriteFactory.Get(), m_textFormatTtsSpeed, L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) return false;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)
    );
    if (FAILED(hr)) return false;

    return true;
}

bool D2DRenderer::CreateDeviceResources() {
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        0,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &featureLevel,
        &m_d3dContext
    );
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    // Create D2D Device & Context
    hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    m_d2dContext->SetDpi(m_dpi, m_dpi);

    // Create Native Windows PDF Renderer
    hr = PdfCreateRenderer(dxgiDevice.Get(), &m_pdfRenderer);
    if (FAILED(hr)) return false;

    // Create Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &m_brushBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushPageBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.12f, 0.14f, 1.0f), &m_brushThemeNightBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.91f, 0.81f, 1.0f), &m_brushThemeSepiaBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &m_brushPageShadow);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f), &m_brushPageBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &m_brushThemeNightBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.14f, 0.90f), &m_brushHudBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 0.60f), &m_brushHudBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f), &m_brushHudText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.60f, 0.60f, 1.0f), &m_brushBlankText);

    // Help Overlay Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &m_brushHelpBackdrop);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.14f, 0.96f), &m_brushHelpCardBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 0.80f), &m_brushHelpCardBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.74f, 0.97f, 1.0f), &m_brushHelpKeyText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.0f), &m_brushHelpDescText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.65f, 0.70f, 1.0f), &m_brushHelpSubText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.90f), &m_brushHelpKeycapBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.30f, 0.34f, 0.70f), &m_brushHelpKeycapBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.03f), &m_brushHelpRowAlt);

    // Tab Bar Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.09f, 1.0f), &m_brushTabBarBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.17f, 0.17f, 0.17f, 1.0f), &m_brushTabActiveBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.11f, 0.11f, 1.0f), &m_brushTabInactiveBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f), &m_brushTabHoverBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.24f, 0.80f), &m_brushTabBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.84f, 1.0f), &m_brushTabAccent);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.77f, 0.17f, 0.11f, 0.90f), &m_brushTabCloseHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f), &m_brushTabText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f), &m_brushTabTextInactive);

    // Scrollbar & Go to Page Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &m_brushScrollbarTrack);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &m_brushScrollbarThumb);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f), &m_brushScrollbarThumbHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.95f), &m_brushGoToPageBox);

    // Search Highlights & Search Bar Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.90f, 0.18f, 0.40f), &m_brushSearchHighlight);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.65f, 0.96f, 0.55f), &m_brushSearchActiveHighlight);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.75f, 1.0f, 0.95f), &m_brushSearchActiveBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f), &m_brushSearchBtnBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.84f, 0.40f), &m_brushSearchBtnActive);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.84f, 0.35f), &m_brushTextSelection);

    // Document Properties Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.74f, 0.97f, 1.0f), &m_brushPropsAccent);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.145f, 0.388f, 0.922f, 1.0f), &m_brushPropsBtn);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.114f, 0.306f, 0.847f, 1.0f), &m_brushPropsBtnHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.153f, 0.153f, 0.165f, 0.95f), &m_brushPropsSecBtn);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.247f, 0.247f, 0.275f, 1.0f), &m_brushPropsSecBtnHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.133f, 0.773f, 0.369f, 1.0f), &m_brushPropsSuccess);

    // Dictionary Card Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.13f, 0.15f, 0.96f), &m_brushDictCardBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.30f, 0.35f, 0.90f), &m_brushDictCardBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.22f, 0.28f, 0.95f), &m_brushDictTagBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.75f, 1.0f, 1.0f), &m_brushDictTagText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.94f, 0.96f, 1.0f), &m_brushDictDefText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.62f, 0.66f, 0.85f), &m_brushDictHintText);

    // Laser Pointer Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.12f, 0.12f, 0.30f), &m_brushLaserOuter);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.20f, 0.20f, 0.85f), &m_brushLaserMiddle);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.95f, 0.95f, 0.98f), &m_brushLaserCore);

    // Presenter Bar Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &m_brushPresenterBtnHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f), &m_brushPresenterBtnActive);

    // Read Aloud (TTS) Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.72f, 0.0f, 0.35f), &m_brushTtsHighlight);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f), &m_brushTtsBarBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.32f, 0.36f, 0.90f), &m_brushTtsBarBorder);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.96f, 1.0f), &m_brushTtsBarText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &m_brushTtsBarBtnHover);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &m_brushTtsBarBtnActive);

    return true;
}

bool D2DRenderer::CreateWindowSizeDependentResources() {
    if (!m_d3dDevice) return false;

    m_d2dContext->SetTarget(nullptr);
    m_d2dTargetBitmap = nullptr;

    if (m_swapChain) {
        HRESULT hr = m_swapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            DiscardDeviceResources();
            if (!CreateDeviceResources()) return false;
        }
    }

    if (!m_swapChain) {
        ComPtr<IDXGIDevice> dxgiDevice;
        m_d3dDevice.As(&dxgiDevice);

        ComPtr<IDXGIAdapter> dxgiAdapter;
        dxgiDevice->GetAdapter(&dxgiAdapter);

        ComPtr<IDXGIFactory2> dxgiFactory;
        dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.Stereo = FALSE;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
            m_d3dDevice.Get(),
            m_hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &m_swapChain
        );
        if (FAILED(hr)) return false;
    }

    // Bind backbuffer surface to D2D bitmap
    ComPtr<IDXGISurface> dxgiBackBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        m_dpi, m_dpi
    );

    hr = m_d2dContext->CreateBitmapFromDxgiSurface(
        dxgiBackBuffer.Get(),
        &bitmapProperties,
        &m_d2dTargetBitmap
    );
    if (FAILED(hr)) return false;

    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
    return true;
}

void D2DRenderer::DiscardDeviceResources() {
    if (m_d2dContext) {
        m_d2dContext->SetTarget(nullptr);
    }
    m_d2dTargetBitmap = nullptr;
    m_brushBg = nullptr;
    m_brushPageBg = nullptr;
    m_brushThemeNightBg = nullptr;
    m_brushThemeSepiaBg = nullptr;
    m_brushPageShadow = nullptr;
    m_brushPageBorder = nullptr;
    m_brushThemeNightBorder = nullptr;
    m_brushHudBg = nullptr;
    m_brushHudBorder = nullptr;
    m_brushHudText = nullptr;
    m_brushBlankText = nullptr;
    m_brushHelpBackdrop = nullptr;
    m_brushHelpCardBg = nullptr;
    m_brushHelpCardBorder = nullptr;
    m_brushHelpKeyText = nullptr;
    m_brushHelpDescText = nullptr;
    m_brushHelpSubText = nullptr;
    m_brushHelpKeycapBg = nullptr;
    m_brushHelpKeycapBorder = nullptr;
    m_brushHelpRowAlt = nullptr;
    m_brushTabBarBg = nullptr;
    m_brushTabActiveBg = nullptr;
    m_brushTabInactiveBg = nullptr;
    m_brushTabHoverBg = nullptr;
    m_brushTabBorder = nullptr;
    m_brushTabAccent = nullptr;
    m_brushTabCloseHover = nullptr;
    m_brushTabText = nullptr;
    m_brushTabTextInactive = nullptr;
    m_brushScrollbarTrack = nullptr;
    m_brushScrollbarThumb = nullptr;
    m_brushScrollbarThumbHover = nullptr;
    m_brushGoToPageBox = nullptr;
    m_brushSearchHighlight = nullptr;
    m_brushSearchActiveHighlight = nullptr;
    m_brushSearchActiveBorder = nullptr;
    m_brushSearchBtnBg = nullptr;
    m_brushSearchBtnActive = nullptr;
    m_brushTextSelection = nullptr;
    m_brushPropsAccent = nullptr;
    m_brushPropsBtn = nullptr;
    m_brushPropsBtnHover = nullptr;
    m_brushPropsSecBtn = nullptr;
    m_brushPropsSecBtnHover = nullptr;
    m_brushPropsSuccess = nullptr;
    m_brushDictCardBg = nullptr;
    m_brushDictCardBorder = nullptr;
    m_brushDictTagBg = nullptr;
    m_brushDictTagText = nullptr;
    m_brushDictDefText = nullptr;
    m_brushDictHintText = nullptr;
    m_brushLaserOuter = nullptr;
    m_brushLaserMiddle = nullptr;
    m_brushLaserCore = nullptr;
    m_brushPresenterBtnHover = nullptr;
    m_brushPresenterBtnActive = nullptr;
    m_brushTtsHighlight = nullptr;
    m_brushTtsBarBg = nullptr;
    m_brushTtsBarBorder = nullptr;
    m_brushTtsBarText = nullptr;
    m_brushTtsBarBtnHover = nullptr;
    m_brushTtsBarBtnActive = nullptr;
    m_colorMatrixEffect = nullptr;
    m_continuousPageCache.clear();
    m_printRenderTexture = nullptr;
    m_printStagingTexture = nullptr;
    m_printTargetBitmap = nullptr;
    m_cachedPrintW = 0;
    m_cachedPrintH = 0;
    m_pdfRenderer = nullptr;
    m_d2dContext = nullptr;
    m_d2dDevice = nullptr;
    m_swapChain = nullptr;
    m_d3dContext = nullptr;
    m_d3dDevice = nullptr;
}

void D2DRenderer::DrawPageBitmapWithTheme(ID2D1Bitmap1* pBitmap, const D2D1_RECT_F& destRect) {
    if (!pBitmap || !m_d2dContext) return;

    if (m_pageTheme == PageTheme::Original) {
        m_d2dContext->DrawBitmap(pBitmap, destRect);
        return;
    }

    if (!m_colorMatrixEffect) {
        m_d2dContext->CreateEffect(CLSID_D2D1ColorMatrix, &m_colorMatrixEffect);
    }

    if (!m_colorMatrixEffect) {
        m_d2dContext->DrawBitmap(pBitmap, destRect);
        return;
    }

    D2D1_MATRIX_5X4_F matrix = {};
    if (m_pageTheme == PageTheme::Night) {
        // Dark contrast-friendly inversion: maps white (1,1,1) -> dark charcoal (0.11, 0.12, 0.14)
        // and black (0,0,0) -> soft warm white (0.93, 0.93, 0.95)
        matrix = D2D1::Matrix5x4F(
            -0.82f,  0.0f,   0.0f,  0.0f,
             0.0f,  -0.81f,  0.0f,  0.0f,
             0.0f,   0.0f,  -0.81f, 0.0f,
             0.0f,   0.0f,   0.0f,  1.0f,
             0.93f,  0.93f,  0.95f, 0.0f
        );
    } else {
        // Sepia warm paper tint: maps white (1,1,1) -> warm cream (0.96, 0.91, 0.81)
        matrix = D2D1::Matrix5x4F(
            0.96f, 0.0f,  0.0f,  0.0f,
            0.0f,  0.91f, 0.0f,  0.0f,
            0.0f,  0.0f,  0.81f, 0.0f,
            0.0f,  0.0f,  0.0f,  1.0f,
            0.0f,  0.0f,  0.0f,  0.0f
        );
    }

    m_colorMatrixEffect->SetInput(0, pBitmap);
    m_colorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);

    D2D1_SIZE_F bmpSize = pBitmap->GetSize();
    float destW = destRect.right - destRect.left;
    float destH = destRect.bottom - destRect.top;
    float scaleX = (bmpSize.width > 0.0f) ? (destW / bmpSize.width) : 1.0f;
    float scaleY = (bmpSize.height > 0.0f) ? (destH / bmpSize.height) : 1.0f;

    D2D1_MATRIX_3X2_F prevTransform;
    m_d2dContext->GetTransform(&prevTransform);
    m_d2dContext->SetTransform(
        D2D1::Matrix3x2F::Scale(scaleX, scaleY) *
        D2D1::Matrix3x2F::Translation(destRect.left, destRect.top) *
        prevTransform
    );
    m_d2dContext->DrawImage(m_colorMatrixEffect.Get(), D2D1::Point2F(0.0f, 0.0f));
    m_d2dContext->SetTransform(prevTransform);
    m_colorMatrixEffect->SetInput(0, nullptr);
}

void D2DRenderer::EndDrawAndPresent() {
    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    } else {
        HRESULT hrPres = m_swapChain->Present(1, 0);
        if (hrPres == DXGI_ERROR_DEVICE_REMOVED || hrPres == DXGI_ERROR_DEVICE_RESET) {
            DiscardDeviceResources();
            CreateDeviceResources();
            CreateWindowSizeDependentResources();
        }
    }
}

void D2DRenderer::RenderBlank(
    const std::wstring& message,
    const HelpOverlayRenderInfo& help,
    const std::vector<TabRenderInfo>& tabs,
    bool isAddHovered,
    bool showGoToPage,
    const std::wstring& goToPageBuffer,
    const SearchBarRenderInfo& searchBar,
    const DocumentPropertiesRenderInfo& docProps,
    const DictionaryCardRenderInfo& dictCard,
    const LaserPointerRenderInfo& laser,
    const PresenterBarRenderInfo& presenterBar,
    const TtsBarRenderInfo& ttsBar,
    const RecentRenderInfo* pRecent,
    const PasswordOverlayRenderInfo* pPasswordOverlay,
    const LinkHoverRenderInfo* pLinkHover,
    const OptionsOverlayRenderInfo* pOptionsOverlay
) {
    if (!m_d2dContext || !m_swapChain) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

    // Convert pixel dimensions to DIPs
    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);
    float topOffset = (tabs.size() > 1) ? 34.0f : 0.0f;
    D2D1_RECT_F layoutRect = D2D1::RectF(20.0f, 20.0f + topOffset, dipWidth - 20.0f, dipHeight - 20.0f);

    UIViews::RecentFilesResources res;
    res.brushCardBg = m_brushHelpCardBg.Get();
    res.brushBackdrop = m_brushHelpBackdrop.Get();
    res.brushBorder = m_brushTabBorder.Get();
    res.brushAccent = m_brushTabAccent.Get();
    res.brushHoverBg = m_brushTabHoverBg.Get();
    res.brushActiveBg = m_brushTabActiveBg.Get();
    res.brushText = m_brushHudText.Get();
    res.brushTextDim = m_brushTabTextInactive.Get();
    res.brushCloseHover = m_brushTabCloseHover.Get();
    res.textFormatTitle = m_textFormatHelpTitle.Get();
    res.textFormatSub = m_textFormatHelpSub.Get();
    res.textFormatItemTitle = m_textFormatTab.Get();
    res.textFormatItemSub = m_textFormatHelpColDesc.Get();
    res.textFormatBadge = m_textFormatTabClose.Get();
    res.textFormatBlank = m_textFormatBlank.Get();

    if (pRecent) {
        UIViews::RecentDashboardView::Render(
            m_d2dContext.Get(),
            dipWidth,
            dipHeight,
            *pRecent,
            message,
            res
        );
    } else {
        std::wstring displayMsg = message.empty() ? L"Drag and drop a PDF file here,\ndouble-click to browse, or press Ctrl+O\n\n(Press F1 for keyboard shortcuts)" : message;
        m_d2dContext->DrawText(
            displayMsg.c_str(),
            (UINT32)displayMsg.length(),
            m_textFormatBlank.Get(),
            layoutRect,
            m_brushBlankText.Get()
        );
    }

    DrawOverlays(tabs, isAddHovered, nullptr, showGoToPage, goToPageBuffer, 0, searchBar, help, docProps, &dictCard, &laser, &presenterBar, &ttsBar, pRecent, pPasswordOverlay, pLinkHover, pOptionsOverlay);

    EndDrawAndPresent();
}

bool D2DRenderer::RasterizePageViaStream(
    winrt::Windows::Data::Pdf::PdfPage page,
    UINT32 renderW,
    UINT32 renderH,
    ComPtr<ID2D1Bitmap1>& outBitmap
) {
    if (!page || !m_d2dContext || renderW == 0 || renderH == 0) return false;

    try {
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        options.DestinationWidth(renderW);
        options.DestinationHeight(renderH);

        // Render page to in-memory PNG stream synchronously
        page.RenderToStreamAsync(stream, options).get();

        if (stream.Size() == 0) return false;

        stream.Seek(0);

        ComPtr<IStream> spIStream;
        HRESULT hr = CreateStreamOverRandomAccessStream(
            reinterpret_cast<IUnknown*>(winrt::get_abi(stream)),
            IID_PPV_ARGS(&spIStream)
        );
        if (FAILED(hr) || !spIStream) return false;

        if (!m_wicFactory) {
            hr = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&m_wicFactory)
            );
            if (FAILED(hr) || !m_wicFactory) return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = m_wicFactory->CreateDecoderFromStream(
            spIStream.Get(),
            nullptr,
            WICDecodeMetadataCacheOnDemand,
            &decoder
        );
        if (FAILED(hr) || !decoder) return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr) || !frame) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = m_wicFactory->CreateFormatConverter(&converter);
        if (FAILED(hr) || !converter) return false;

        hr = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeCustom
        );
        if (FAILED(hr)) return false;

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            m_dpi, m_dpi
        );

        hr = m_d2dContext->CreateBitmapFromWicBitmap(
            converter.Get(),
            &bp,
            &outBitmap
        );
        return SUCCEEDED(hr) && outBitmap != nullptr;
    } catch (...) {
        return false;
    }
}

void D2DRenderer::RenderPage(
    winrt::Windows::Data::Pdf::PdfPage page,
    float zoom,
    float offsetX,
    float offsetY,
    D2D1_SIZE_F pageSize,
    uint32_t currentPageIndex,
    uint32_t totalPages,
    const std::wstring& zoomModeText,
    bool hasType3Font,
    const HelpOverlayRenderInfo& help,
    const std::vector<TabRenderInfo>& tabs,
    bool isAddHovered,
    const ScrollbarRenderInfo& scrollbar,
    bool showGoToPage,
    const std::wstring& goToPageBuffer,
    const SearchBarRenderInfo& searchBar,
    const std::vector<SearchHighlight>& highlights,
    const DocumentPropertiesRenderInfo& docProps,
    const std::vector<SelectionHighlightSpan>& selectionSpans,
    const DictionaryCardRenderInfo& dictCard,
    const LaserPointerRenderInfo& laser,
    const PresenterBarRenderInfo& presenterBar,
    const TtsBarRenderInfo& ttsBar,
    const RecentRenderInfo* pRecent,
    const PasswordOverlayRenderInfo* pPasswordOverlay,
    const LinkHoverRenderInfo* pLinkHover,
    const OptionsOverlayRenderInfo* pOptionsOverlay
) {
    if (!m_d2dContext || !m_swapChain) return;

    if (!page) {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        m_d2dContext->BeginDraw();
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

        float dipWidth = m_width * (96.0f / m_dpi);
        float dipHeight = m_height * (96.0f / m_dpi);
        float topOffset = (tabs.size() > 1) ? 34.0f : 0.0f;
        D2D1_RECT_F layoutRect = D2D1::RectF(24.0f, 24.0f + topOffset, dipWidth - 24.0f, dipHeight - 24.0f);

        std::wstring errMsg = L"Error: Unable to display page " + std::to_wstring(currentPageIndex + 1) +
                              L".\nThe page data may be corrupted, password-protected, or in an unsupported format.\n\nPress Ctrl+O or Ctrl+T to open another file.";
        if (m_brushBlankText && m_textFormatBlank) {
            m_d2dContext->DrawText(
                errMsg.c_str(),
                (UINT32)errMsg.length(),
                m_textFormatBlank.Get(),
                layoutRect,
                m_brushBlankText.Get()
            );
        }

        DrawOverlays(tabs, isAddHovered, &scrollbar, showGoToPage, goToPageBuffer, totalPages, searchBar, help, docProps, &dictCard, &laser, &presenterBar, &ttsBar, pRecent, pPasswordOverlay, pLinkHover, pOptionsOverlay);
        EndDrawAndPresent();
        return;
    }

    std::vector<ContinuousPageInfo> visiblePages;
    ContinuousPageInfo info;
    info.page = page;
    info.pageSize = pageSize;
    info.xOffset = offsetX;
    info.yOffset = offsetY;
    info.pageIndex = currentPageIndex;
    info.hasType3Font = hasType3Font;
    visiblePages.push_back(std::move(info));

    RenderContinuous(
        visiblePages,
        zoom,
        currentPageIndex,
        totalPages,
        zoomModeText,
        false,
        help,
        tabs,
        isAddHovered,
        scrollbar,
        showGoToPage,
        goToPageBuffer,
        searchBar,
        highlights,
        docProps,
        selectionSpans,
        dictCard,
        laser,
        presenterBar,
        ttsBar,
        pRecent,
        pPasswordOverlay,
        pLinkHover,
        pOptionsOverlay
    );
}

void D2DRenderer::RenderContinuous(
    const std::vector<ContinuousPageInfo>& visiblePages,
    float zoom,
    uint32_t currentPageIndex,
    uint32_t totalPages,
    const std::wstring& zoomModeText,
    bool isContinuous,
    const HelpOverlayRenderInfo& help,
    const std::vector<TabRenderInfo>& tabs,
    bool isAddHovered,
    const ScrollbarRenderInfo& scrollbar,
    bool showGoToPage,
    const std::wstring& goToPageBuffer,
    const SearchBarRenderInfo& searchBar,
    const std::vector<SearchHighlight>& highlights,
    const DocumentPropertiesRenderInfo& docProps,
    const std::vector<SelectionHighlightSpan>& selectionSpans,
    const DictionaryCardRenderInfo& dictCard,
    const LaserPointerRenderInfo& laser,
    const PresenterBarRenderInfo& presenterBar,
    const TtsBarRenderInfo& ttsBar,
    const RecentRenderInfo* pRecent,
    const PasswordOverlayRenderInfo* pPasswordOverlay,
    const LinkHoverRenderInfo* pLinkHover,
    const OptionsOverlayRenderInfo* pOptionsOverlay
) {
    if (!m_d2dContext || !m_swapChain) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    // Pre-pass: Rasterize any uncached visible pages to offscreen bitmaps before beginning main swapchain draw
    for (const auto& vp : visiblePages) {
        if (!vp.page) continue;

        float destW = vp.pageSize.width * zoom;
        float destH = vp.pageSize.height * zoom;
        UINT32 renderW = (UINT32)std::max(1.0f, std::round(destW));
        UINT32 renderH = (UINT32)std::max(1.0f, std::round(destH));

        bool found = false;
        for (auto& entry : m_continuousPageCache) {
            if (entry.pageIndex == vp.pageIndex &&
                std::abs(entry.zoom - zoom) < 0.0001f &&
                entry.pixelW == renderW &&
                entry.pixelH == renderH &&
                entry.bitmap) {
                entry.lastUsedTime = ++m_continuousCacheClock;
                found = true;
                break;
            }
        }

        if (!found && renderW > 0 && renderH > 0) {
            ComPtr<ID2D1Bitmap1> pageBitmap;
            bool success = false;
            if (vp.hasType3Font) {
                success = RasterizePageViaStream(vp.page, renderW, renderH, pageBitmap);
            } else if (m_pdfRenderer) {
                D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                    m_dpi, m_dpi
                );
                D2D1_SIZE_U pixelSize = D2D1::SizeU(renderW, renderH);

                HRESULT hrBmp = m_d2dContext->CreateBitmap(pixelSize, nullptr, 0, &bp, &pageBitmap);
                if (SUCCEEDED(hrBmp) && pageBitmap) {
                    m_d2dContext->SetTarget(pageBitmap.Get());
                    m_d2dContext->BeginDraw();
                    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
                    m_d2dContext->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));

                    PDF_RENDER_PARAMS params = {};
                    params.SourceRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
                    params.DestinationWidth = renderW;
                    params.DestinationHeight = renderH;
                    params.BackgroundColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
                    params.IgnoreHighContrast = FALSE;

                    m_pdfRenderer->RenderPageToDeviceContext(
                        (IUnknown*)winrt::get_abi(vp.page),
                        m_d2dContext.Get(),
                        &params
                    );
                    m_d2dContext->EndDraw();

                    // Restore main swapchain target
                    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
                    success = true;
                }
            }

            if (success && pageBitmap) {
                PageBitmapCache entry;
                entry.pageIndex = vp.pageIndex;
                entry.zoom = zoom;
                entry.pixelW = renderW;
                entry.pixelH = renderH;
                entry.lastUsedTime = ++m_continuousCacheClock;
                entry.bitmap = pageBitmap;

                if (m_continuousPageCache.size() >= MAX_CONTINUOUS_CACHED_PAGES) {
                    auto oldest = std::min_element(m_continuousPageCache.begin(), m_continuousPageCache.end(),
                        [](const PageBitmapCache& a, const PageBitmapCache& b) {
                            return a.lastUsedTime < b.lastUsedTime;
                        });
                    if (oldest != m_continuousPageCache.end()) {
                        *oldest = std::move(entry);
                    }
                } else {
                    m_continuousPageCache.push_back(std::move(entry));
                }
            }
        }
    }

    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

    float topOffset = (tabs.size() > 1) ? 34.0f : 0.0f;

    for (const auto& vp : visiblePages) {
        if (!vp.page) continue;

        float pageX = vp.xOffset;
        float pageY = vp.yOffset + topOffset;
        float destW = vp.pageSize.width * zoom;
        float destH = vp.pageSize.height * zoom;

        // 1. Draw Page Drop Shadow
        D2D1_RECT_F shadowRect = D2D1::RectF(
            pageX + 4.0f,
            pageY + 4.0f,
            pageX + destW + 5.0f,
            pageY + destH + 5.0f
        );
        m_d2dContext->FillRoundedRectangle(
            D2D1::RoundedRect(shadowRect, 2.0f, 2.0f),
            m_brushPageShadow.Get()
        );

        // 2. Draw Page Background (Theme-aware)
        D2D1_RECT_F pageRect = D2D1::RectF(
            pageX,
            pageY,
            pageX + destW,
            pageY + destH
        );
        ID2D1SolidColorBrush* pBgBrush = m_brushPageBg.Get();
        if (m_pageTheme == PageTheme::Night && m_brushThemeNightBg) {
            pBgBrush = m_brushThemeNightBg.Get();
        } else if (m_pageTheme == PageTheme::Sepia && m_brushThemeSepiaBg) {
            pBgBrush = m_brushThemeSepiaBg.Get();
        }
        m_d2dContext->FillRectangle(pageRect, pBgBrush);

        // 3. Render PDF Content (Fast hardware bitblt or fallback direct render)
        ID2D1Bitmap1* pCachedBmp = nullptr;
        for (const auto& entry : m_continuousPageCache) {
            if (entry.pageIndex == vp.pageIndex &&
                std::abs(entry.zoom - zoom) < 0.0001f &&
                entry.bitmap) {
                pCachedBmp = entry.bitmap.Get();
                break;
            }
        }

        if (pCachedBmp) {
            DrawPageBitmapWithTheme(pCachedBmp, pageRect);
        } else if (!vp.hasType3Font && m_pdfRenderer) {
            PDF_RENDER_PARAMS params = {};
            params.SourceRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            params.DestinationWidth = (UINT32)std::max(1.0f, std::round(destW));
            params.DestinationHeight = (UINT32)std::max(1.0f, std::round(destH));
            params.BackgroundColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
            params.IgnoreHighContrast = FALSE;

            m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(pageX, pageY));
            m_pdfRenderer->RenderPageToDeviceContext(
                (IUnknown*)winrt::get_abi(vp.page),
                m_d2dContext.Get(),
                &params
            );
            m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // 3a. Draw Text Selection Highlights for this page
        for (const auto& span : selectionSpans) {
            if (span.pageIndex == vp.pageIndex) {
                for (const auto& pr : span.rects) {
                    float hx = pageX + pr.left * zoom;
                    float hy = pageY + pr.top * zoom;
                    float hw = (pr.right - pr.left) * zoom;
                    float hh = (pr.bottom - pr.top) * zoom;
                    D2D1_RECT_F r = D2D1::RectF(hx, hy, hx + hw, hy + hh);
                    if (span.isTtsHighlight && m_brushTtsHighlight) {
                        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 3.0f, 3.0f), m_brushTtsHighlight.Get());
                    } else if (m_brushTextSelection) {
                        m_d2dContext->FillRectangle(r, m_brushTextSelection.Get());
                    }
                }
            }
        }

        // 3b. Draw Search Match Highlights for this page (O(log N) lookup)
        struct HighlightPageComp {
            bool operator()(const SearchHighlight& a, uint32_t page) const { return a.pageIndex < page; }
            bool operator()(uint32_t page, const SearchHighlight& a) const { return page < a.pageIndex; }
        };
        auto hlRange = std::equal_range(highlights.begin(), highlights.end(), vp.pageIndex, HighlightPageComp{});
        for (auto it = hlRange.first; it != hlRange.second; ++it) {
            const auto& hl = *it;
            auto drawBox = [&](const D2D1_RECT_F& pr) {
                float hx = pageX + pr.left * zoom;
                float hy = pageY + pr.top * zoom;
                float hw = (pr.right - pr.left) * zoom;
                float hh = (pr.bottom - pr.top) * zoom;
                D2D1_RECT_F r = D2D1::RectF(hx, hy, hx + hw, hy + hh);
                if (hl.isActive) {
                    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 2.0f, 2.0f), m_brushSearchActiveHighlight.Get());
                    m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(r, 2.0f, 2.0f), m_brushSearchActiveBorder.Get(), 1.5f);
                } else {
                    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 2.0f, 2.0f), m_brushSearchHighlight.Get());
                }
            };

            if (!hl.rects.empty()) {
                for (const auto& lr : hl.rects) drawBox(lr);
            } else {
                drawBox(hl.pageRect);
            }
        }

        // 3c. Draw Link Hover Highlight for this page
        if (pLinkHover && pLinkHover->active && pLinkHover->pageIndex == vp.pageIndex) {
            float hx = pageX + pLinkHover->pageRect.left * zoom;
            float hy = pageY + pLinkHover->pageRect.top * zoom;
            float hw = (pLinkHover->pageRect.right - pLinkHover->pageRect.left) * zoom;
            float hh = (pLinkHover->pageRect.bottom - pLinkHover->pageRect.top) * zoom;
            D2D1_RECT_F r = D2D1::RectF(hx, hy, hx + hw, hy + hh);
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 2.0f, 2.0f);
            if (m_brushTextSelection) {
                m_d2dContext->FillRoundedRectangle(rr, m_brushTextSelection.Get());
            }
            if (m_brushTabAccent) {
                m_d2dContext->DrawLine(D2D1::Point2F(r.left, r.bottom), D2D1::Point2F(r.right, r.bottom), m_brushTabAccent.Get(), 1.2f);
            }
        }

        // 4. Draw 1px crisp outline around page (Theme-aware)
        ID2D1SolidColorBrush* pBorderBrush = (m_pageTheme == PageTheme::Night && m_brushThemeNightBorder) ?
            m_brushThemeNightBorder.Get() : m_brushPageBorder.Get();
        m_d2dContext->DrawRectangle(pageRect, pBorderBrush, 1.0f);
    }

    // 5. Draw Sleek Minimalist HUD Pill (bottom-center)
    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);

    wchar_t hudText[160];
    int zoomPct = (int)std::round(zoom * 100.0f);
    if (!zoomModeText.empty()) {
        swprintf_s(hudText, L"%u / %u  \x2022  %d%% (%s)%s", currentPageIndex + 1, totalPages, zoomPct, zoomModeText.c_str(), isContinuous ? L"  \x2022  Continuous" : L"");
    } else {
        swprintf_s(hudText, L"%u / %u  \x2022  %d%%%s", currentPageIndex + 1, totalPages, zoomPct, isContinuous ? L"  \x2022  Continuous" : L"");
    }

    float pillWidth = isContinuous
        ? (!zoomModeText.empty() ? 330.0f : 280.0f)
        : (!zoomModeText.empty() ? 240.0f : 190.0f);
    float pillHeight = 32.0f;
    float pillLeft = (dipWidth - pillWidth) * 0.5f;
    float pillTop = dipHeight - pillHeight - 16.0f;

    if (pillLeft > 0.0f && pillTop > 0.0f) {
        D2D1_RECT_F pillRect = D2D1::RectF(pillLeft, pillTop, pillLeft + pillWidth, pillTop + pillHeight);
        D2D1_ROUNDED_RECT roundedPill = D2D1::RoundedRect(pillRect, 16.0f, 16.0f);

        m_d2dContext->FillRoundedRectangle(roundedPill, m_brushHudBg.Get());
        m_d2dContext->DrawRoundedRectangle(roundedPill, m_brushHudBorder.Get(), 1.0f);
        m_d2dContext->DrawText(
            hudText,
            (UINT32)wcslen(hudText),
            m_textFormatHud.Get(),
            pillRect,
            m_brushHudText.Get()
        );
    }

    // 6. Draw Overlays (Scrollbar, Search Bar, Tab Bar, Overlays)
    DrawOverlays(tabs, isAddHovered, &scrollbar, showGoToPage, goToPageBuffer, totalPages, searchBar, help, docProps, &dictCard, &laser, &presenterBar, &ttsBar, pRecent, pPasswordOverlay, pLinkHover, pOptionsOverlay);

    EndDrawAndPresent();
}

void D2DRenderer::DrawOverlays(
    const std::vector<TabRenderInfo>& tabs,
    bool isAddHovered,
    const ScrollbarRenderInfo* pScrollbar,
    bool showGoToPage,
    const std::wstring& goToPageBuffer,
    uint32_t totalPages,
    const SearchBarRenderInfo& searchBar,
    const HelpOverlayRenderInfo& help,
    const DocumentPropertiesRenderInfo& docProps,
    const DictionaryCardRenderInfo* pDictCard,
    const LaserPointerRenderInfo* pLaser,
    const PresenterBarRenderInfo* pPresenterBar,
    const TtsBarRenderInfo* pTtsBar,
    const RecentRenderInfo* pRecent,
    const PasswordOverlayRenderInfo* pPasswordOverlay,
    const LinkHoverRenderInfo* pLinkHover,
    const OptionsOverlayRenderInfo* pOptionsOverlay
) {
    // 1. Draw Scrollbar
    if (pScrollbar && pScrollbar->visible) {
        DrawScrollbar(*pScrollbar);
    }

    // 2. Draw Floating Search Bar if visible
    if (searchBar.visible) {
        DrawSearchBar(searchBar);
    }

    // 3. Draw Tab Bar if 2+ tabs exist (above page content)
    if (tabs.size() > 1) {
        DrawTabBar(tabs, isAddHovered);
    }

    // 4. Draw Go to Page Overlay if active
    if (showGoToPage) {
        DrawGoToPageOverlay(goToPageBuffer, totalPages);
    }

    // 5. Draw Help Overlay if toggled
    if (help.visible) {
        DrawHelpOverlay(help);
    }

    // 6. Draw Document Properties Overlay if active
    if (docProps.visible) {
        DrawDocumentProperties(docProps);
    }

    // 7. Draw Dictionary Card if visible
    if (pDictCard && pDictCard->visible) {
        DrawDictionaryCard(*pDictCard);
    }

    // 8. Draw Fullscreen Presenter Bar if visible
    if (pPresenterBar && pPresenterBar->visible) {
        DrawPresenterBar(*pPresenterBar);
    }

    // 9. Draw Read Aloud Floating Bar if visible
    if (pTtsBar && pTtsBar->visible) {
        float topOffset = (tabs.size() > 1) ? 34.0f : 0.0f;
        DrawTtsBar(*pTtsBar, topOffset);
    }

    // 10. Draw Laser Pointer if active (topmost element)
    if (pLaser && pLaser->active) {
        DrawLaserPointer(*pLaser);
    }

    // 11. Draw Recent Documents Switcher if active (modal overlay)
    if (pRecent && pRecent->showSwitcher) {
        float dipWidth = m_width * (96.0f / m_dpi);
        float dipHeight = m_height * (96.0f / m_dpi);
        UIViews::RecentFilesResources res;
        res.brushCardBg = m_brushHelpCardBg.Get();
        res.brushBackdrop = m_brushHelpBackdrop.Get();
        res.brushBorder = m_brushTabBorder.Get();
        res.brushAccent = m_brushTabAccent.Get();
        res.brushHoverBg = m_brushTabHoverBg.Get();
        res.brushActiveBg = m_brushTabActiveBg.Get();
        res.brushText = m_brushHudText.Get();
        res.brushTextDim = m_brushTabTextInactive.Get();
        res.brushCloseHover = m_brushTabCloseHover.Get();
        res.textFormatTitle = m_textFormatHelpTitle.Get();
        res.textFormatSub = m_textFormatHelpSub.Get();
        res.textFormatItemTitle = m_textFormatTab.Get();
        res.textFormatItemSub = m_textFormatHelpColDesc.Get();
        res.textFormatBadge = m_textFormatTabClose.Get();
        res.textFormatBlank = m_textFormatBlank.Get();

        UIViews::RecentSwitcherView::Render(
            m_d2dContext.Get(),
            dipWidth,
            dipHeight,
            *pRecent,
            res
        );
    }

    // 12. Draw Password Overlay if active (modal prompt)
    if (pPasswordOverlay && pPasswordOverlay->visible) {
        DrawPasswordOverlay(*pPasswordOverlay);
    }

    // 13. Draw Options Overlay if active (modal preferences dialog)
    if (pOptionsOverlay && pOptionsOverlay->visible) {
        DrawOptionsOverlay(*pOptionsOverlay);
    }

    // 14. Draw Link Preview Pill if hovered (bottom-left status preview, non-modal)
    if (pLinkHover && pLinkHover->active && !pLinkHover->label.empty() &&
        !help.visible && !showGoToPage && !docProps.visible &&
        !(pPasswordOverlay && pPasswordOverlay->visible) &&
        !(pOptionsOverlay && pOptionsOverlay->visible) &&
        !(pRecent && pRecent->showSwitcher)) {
        DrawLinkPreviewPill(*pLinkHover);
    }
}

void D2DRenderer::DrawTabBar(const std::vector<TabRenderInfo>& tabs, bool isAddHovered) {
    if (tabs.size() <= 1 || !m_d2dContext) return;
    float dipWidth = m_width * (96.0f / m_dpi);
    UIViews::TabStripResources res{
        m_brushTabBarBg.Get(),
        m_brushTabBorder.Get(),
        m_brushTabActiveBg.Get(),
        m_brushTabAccent.Get(),
        m_brushTabHoverBg.Get(),
        m_brushTabInactiveBg.Get(),
        m_brushTabText.Get(),
        m_brushTabTextInactive.Get(),
        m_brushTabCloseHover.Get(),
        m_textFormatTab.Get(),
        m_textFormatTabClose.Get(),
        m_textFormatTabAdd.Get()
    };
    UIViews::TabStripView::Render(m_d2dContext.Get(), tabs, isAddHovered, dipWidth, res);
}

void D2DRenderer::DrawScrollbar(const ScrollbarRenderInfo& scrollbar) {
    if (!scrollbar.visible || scrollbar.alpha <= 0.001f || !m_d2dContext) return;
    float dipWidth = m_width * (96.0f / m_dpi);
    UIViews::ScrollBarResources res{
        m_brushScrollbarTrack.Get(),
        m_brushScrollbarThumb.Get(),
        m_brushScrollbarThumbHover.Get(),
        m_brushHudBg.Get(),
        m_brushHudBorder.Get(),
        m_brushHudText.Get(),
        m_textFormatHud.Get()
    };
    UIViews::ScrollBarView::Render(m_d2dContext.Get(), scrollbar, dipWidth, res);
}

void D2DRenderer::DrawGoToPageOverlay(const std::wstring& buffer, uint32_t totalPages) {
    if (!m_d2dContext) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;
    UIViews::GoToPageOverlayResources res{
        m_brushHelpBackdrop.Get(),
        m_brushPageShadow.Get(),
        m_brushHelpCardBg.Get(),
        m_brushTabAccent.Get(),
        m_brushHudText.Get(),
        m_brushGoToPageBox.Get(),
        m_brushTabBorder.Get(),
        m_brushHelpSubText.Get(),
        m_textFormatHelpTitle.Get(),
        m_textFormatGoToPageInput.Get(),
        m_textFormatHelpSub.Get()
    };
    UIViews::GoToPageOverlayView::Render(m_d2dContext.Get(), buffer, totalPages, dipWidth, dipHeight, res);
}

void D2DRenderer::DrawSearchBar(const SearchBarRenderInfo& searchBar) {
    if (!searchBar.visible || !m_d2dContext) return;
    float dipWidth = m_width * (96.0f / m_dpi);
    UIViews::SearchBarResources res{
        m_brushPageShadow.Get(),
        m_brushHudBg.Get(),
        m_brushHudBorder.Get(),
        m_brushHudText.Get(),
        m_brushTabTextInactive.Get(),
        m_brushHelpKeyText.Get(),
        m_brushSearchBtnBg.Get(),
        m_brushSearchBtnActive.Get(),
        m_brushTabAccent.Get(),
        m_brushTabCloseHover.Get(),
        m_textFormatSearchInput.Get(),
        m_textFormatSearchBadge.Get(),
        m_textFormatSearchBtn.Get()
    };
    UIViews::SearchBarCache cache{
        m_cachedSearchLayout,
        m_cachedSearchQuery,
        m_cachedSearchLayoutW,
        m_cachedSearchLayoutH,
        m_cachedSearchCaretX
    };
    UIViews::SearchBarView::Render(m_d2dContext.Get(), m_dwriteFactory.Get(), searchBar, dipWidth, res, cache);
    m_cachedSearchLayout = cache.layout;
    m_cachedSearchQuery = std::move(cache.query);
    m_cachedSearchLayoutW = cache.layoutW;
    m_cachedSearchLayoutH = cache.layoutH;
    m_cachedSearchCaretX = cache.caretX;
}

int D2DRenderer::HitTestHelpOverlay(POINT pt) const {
    return UIViews::HelpOverlayView::HitTest(pt, m_width, m_height, m_dpi);
}

void D2DRenderer::DrawHelpOverlay(const HelpOverlayRenderInfo& help) {
    if (!m_d2dContext) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;
    UIViews::HelpOverlayResources res{
        m_brushHelpBackdrop.Get(),
        m_brushPageShadow.Get(),
        m_brushHelpCardBg.Get(),
        m_brushHelpCardBorder.Get(),
        m_brushHudText.Get(),
        m_brushTabCloseHover.Get(),
        m_brushHelpSubText.Get(),
        m_brushSearchBtnActive.Get(),
        m_brushTabAccent.Get(),
        m_brushTabHoverBg.Get(),
        m_brushSearchBtnBg.Get(),
        m_brushTabTextInactive.Get(),
        m_brushTabBorder.Get(),
        m_brushPropsAccent.Get(),
        m_brushHelpKeyText.Get(),
        m_brushHelpDescText.Get(),
        m_brushHelpRowAlt.Get(),
        m_brushHelpKeycapBg.Get(),
        m_brushHelpKeycapBorder.Get(),
        m_textFormatHelpTitle.Get(),
        m_textFormatTabClose.Get(),
        m_textFormatSearchBtn.Get(),
        m_textFormatHelpSection.Get(),
        m_textFormatHelpColKey.Get(),
        m_textFormatHelpColDesc.Get(),
        m_textFormatHelpSingleKey.Get(),
        m_textFormatHelpSingleDesc.Get(),
        m_textFormatHelpFooterLeft.Get(),
        m_textFormatHelpFooterRight.Get()
    };
    UIViews::HelpOverlayView::Render(m_d2dContext.Get(), help, dipWidth, dipHeight, res);
}

int D2DRenderer::HitTestDocumentProperties(POINT pt) const {
    return UIViews::DocPropertiesView::HitTest(pt, m_width, m_height, m_dpi);
}

void D2DRenderer::DrawDocumentProperties(const DocumentPropertiesRenderInfo& props) {
    if (!m_d2dContext) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;
    UIViews::DocPropertiesResources res{
        m_brushHelpBackdrop.Get(),
        m_brushPageShadow.Get(),
        m_brushHelpCardBg.Get(),
        m_brushHelpCardBorder.Get(),
        m_brushHudText.Get(),
        m_brushPropsSecBtnHover.Get(),
        m_brushHelpSubText.Get(),
        m_brushPropsAccent.Get(),
        m_brushPropsBtn.Get(),
        m_brushPropsBtnHover.Get(),
        m_brushPropsSecBtn.Get(),
        m_brushPropsSuccess.Get(),
        m_brushPageBg.Get(),
        m_textFormatHelpTitle.Get(),
        m_textFormatTabClose.Get(),
        m_textFormatPropsSection.Get(),
        m_textFormatPropsLabel.Get(),
        m_textFormatTab.Get()
    };
    UIViews::DocPropertiesView::Render(m_d2dContext.Get(), props, dipWidth, dipHeight, res);
}

bool D2DRenderer::PrintPageToHdc(
    winrt::Windows::Data::Pdf::PdfPage page,
    HDC hdc,
    D2D1_SIZE_F pageSize,
    bool hasType3Font
) {
    if (!m_d3dDevice || !m_d3dContext || (!hasType3Font && !m_pdfRenderer) || !m_d2dContext || !page) {
        return false;
    }

    if (pageSize.width <= 0.0f || pageSize.height <= 0.0f) {
        return false;
    }

    // 1. Query physical printer dimensions and DPI
    int pagePixelW = GetDeviceCaps(hdc, HORZRES);
    int pagePixelH = GetDeviceCaps(hdc, VERTRES);

    if (pagePixelW <= 0 || pagePixelH <= 0) {
        return false;
    }

    // 2. High-quality print raster target (300 DPI)
    float printDpi = 300.0f;
    float scale = printDpi / 72.0f;
    UINT32 renderW = (UINT32)std::round(pageSize.width * scale);
    UINT32 renderH = (UINT32)std::round(pageSize.height * scale);

    // Bound max texture dimension to 4096 to protect GPU VRAM
    if (renderW > 4096) {
        float r = 4096.0f / (float)renderW;
        renderW = 4096;
        renderH = (UINT32)std::round((float)renderH * r);
    }
    if (renderH > 4096) {
        float r = 4096.0f / (float)renderH;
        renderH = 4096;
        renderW = (UINT32)std::round((float)renderW * r);
    }

    renderW = std::max(1u, renderW);
    renderH = std::max(1u, renderH);

    // 3. Compute destination rectangle preserving aspect ratio centered on printable paper area
    float paperAspect = (float)pagePixelW / (float)pagePixelH;
    float pdfAspect = pageSize.width / pageSize.height;

    int destX = 0, destY = 0, destW = 0, destH = 0;
    if (pdfAspect > paperAspect) {
        destW = pagePixelW;
        destH = (int)std::round((float)pagePixelW / pdfAspect);
        destY = (pagePixelH - destH) / 2;
    } else {
        destH = pagePixelH;
        destW = (int)std::round((float)pagePixelH * pdfAspect);
        destX = (pagePixelW - destW) / 2;
    }

    // 4. Render and stage page pixels under m_renderMutex
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);

        // Reuse or recreate D3D11 Texture2D for rendering
        if (!m_printRenderTexture || m_cachedPrintW != renderW || m_cachedPrintH != renderH) {
            D3D11_TEXTURE2D_DESC texDesc = {};
            texDesc.Width = renderW;
            texDesc.Height = renderH;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            m_printRenderTexture = nullptr;
            m_printTargetBitmap = nullptr;
            HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_printRenderTexture);
            if (FAILED(hr)) return false;

            ComPtr<IDXGISurface> dxgiSurface;
            hr = m_printRenderTexture.As(&dxgiSurface);
            if (FAILED(hr)) return false;

            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                printDpi, printDpi
            );

            hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bp, &m_printTargetBitmap);
            if (FAILED(hr)) return false;

            texDesc.Usage = D3D11_USAGE_STAGING;
            texDesc.BindFlags = 0;
            texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            m_printStagingTexture = nullptr;
            hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_printStagingTexture);
            if (FAILED(hr)) return false;

            m_cachedPrintW = renderW;
            m_cachedPrintH = renderH;
        }

        // 5. Render PDF Page onto the texture
        m_d2dContext->SetTarget(m_printTargetBitmap.Get());
        m_d2dContext->BeginDraw();
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        m_d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::White));

        if (hasType3Font) {
            ComPtr<ID2D1Bitmap1> safeBmp;
            if (RasterizePageViaStream(page, renderW, renderH, safeBmp) && safeBmp) {
                m_d2dContext->DrawBitmap(safeBmp.Get(), D2D1::RectF(0.0f, 0.0f, (float)renderW, (float)renderH));
            }
        } else if (m_pdfRenderer) {
            PDF_RENDER_PARAMS params = {};
            params.SourceRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            params.DestinationWidth = renderW;
            params.DestinationHeight = renderH;
            params.BackgroundColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
            params.IgnoreHighContrast = FALSE;

            m_pdfRenderer->RenderPageToDeviceContext(
                (IUnknown*)winrt::get_abi(page),
                m_d2dContext.Get(),
                &params
            );
        }

        HRESULT hr = m_d2dContext->EndDraw();

        // Restore screen target bitmap immediately
        if (m_d2dTargetBitmap) {
            m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
        }

        if (FAILED(hr)) return false;

        // 6. Copy pixels from GPU to staging texture
        m_d3dContext->CopyResource(m_printStagingTexture.Get(), m_printRenderTexture.Get());

        // 7. Map staging texture and extract pixel bytes
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = m_d3dContext->Map(m_printStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return false;

        size_t rowBytes = (size_t)renderW * 4;
        m_printPixelBuffer.resize(rowBytes * renderH);
        const uint8_t* pSrc = (const uint8_t*)mapped.pData;
        uint8_t* pDst = m_printPixelBuffer.data();
        for (UINT32 y = 0; y < renderH; ++y) {
            memcpy(pDst + y * rowBytes, pSrc + y * mapped.RowPitch, rowBytes);
        }

        m_d3dContext->Unmap(m_printStagingTexture.Get(), 0);
    }

    // 8. Transfer to Printer HDC via StretchDIBits without holding m_renderMutex!
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)renderW;
    bmi.bmiHeader.biHeight = -(LONG)renderH; // negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);

    int scanlines = StretchDIBits(
        hdc,
        destX, destY, destW, destH,
        0, 0, (int)renderW, (int)renderH,
        m_printPixelBuffer.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );

    return scanlines > 0;
}

void D2DRenderer::DrawDictionaryCard(const DictionaryCardRenderInfo& dictCard) {
    if (!m_d2dContext || !dictCard.visible) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;
    float topOffset = 0.0f;
    UIViews::DictionaryCardResources res{
        m_brushPageShadow.Get(),
        m_brushDictCardBg.Get(),
        m_brushDictCardBorder.Get(),
        m_brushDictTagBg.Get(),
        m_brushDictTagText.Get(),
        m_brushDictDefText.Get(),
        m_brushDictHintText.Get(),
        m_brushHudText.Get(),
        m_textFormatDictWord.Get(),
        m_textFormatDictTag.Get(),
        m_textFormatDictDef.Get(),
        m_textFormatDictHint.Get()
    };
    UIViews::DictionaryCardView::Render(m_d2dContext.Get(), m_dwriteFactory.Get(), dictCard, dipWidth, dipHeight, topOffset, res);
}

void D2DRenderer::DrawLaserPointer(const LaserPointerRenderInfo& laser) {
    if (!laser.active || !m_d2dContext || !m_brushLaserOuter) return;

    D2D1_COLOR_F outerColor, middleColor, coreColor;
    switch (laser.color) {
    case LaserColor::Red:
        outerColor = D2D1::ColorF(1.0f, 0.12f, 0.12f, 0.32f);
        middleColor = D2D1::ColorF(1.0f, 0.20f, 0.20f, 0.85f);
        coreColor = D2D1::ColorF(1.0f, 0.95f, 0.95f, 0.98f);
        break;
    case LaserColor::Green:
        outerColor = D2D1::ColorF(0.0f, 1.0f, 0.35f, 0.32f);
        middleColor = D2D1::ColorF(0.1f, 1.0f, 0.40f, 0.85f);
        coreColor = D2D1::ColorF(0.92f, 1.0f, 0.92f, 0.98f);
        break;
    case LaserColor::Cyan:
        outerColor = D2D1::ColorF(0.0f, 0.85f, 1.0f, 0.32f);
        middleColor = D2D1::ColorF(0.2f, 0.90f, 1.0f, 0.85f);
        coreColor = D2D1::ColorF(0.92f, 0.98f, 1.0f, 0.98f);
        break;
    case LaserColor::Gold:
        outerColor = D2D1::ColorF(1.0f, 0.72f, 0.0f, 0.32f);
        middleColor = D2D1::ColorF(1.0f, 0.82f, 0.1f, 0.85f);
        coreColor = D2D1::ColorF(1.0f, 1.0f, 0.92f, 0.98f);
        break;
    }

    m_brushLaserOuter->SetColor(outerColor);
    m_brushLaserMiddle->SetColor(middleColor);
    m_brushLaserCore->SetColor(coreColor);

    // Three concentric glowing ellipses for optical laser bloom
    m_d2dContext->FillEllipse(D2D1::Ellipse(laser.position, 15.0f, 15.0f), m_brushLaserOuter.Get());
    m_d2dContext->FillEllipse(D2D1::Ellipse(laser.position, 7.0f, 7.0f), m_brushLaserMiddle.Get());
    m_d2dContext->FillEllipse(D2D1::Ellipse(laser.position, 3.0f, 3.0f), m_brushLaserCore.Get());
}

void D2DRenderer::DrawPresenterBar(const PresenterBarRenderInfo& presenterBar) {
    if (!presenterBar.visible || !m_d2dContext) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = (float)m_width * dipScale;
    float dipHeight = (float)m_height * dipScale;
    UIViews::PresenterBarResources res{
        m_brushPageShadow.Get(),
        m_brushHudBg.Get(),
        m_brushHudBorder.Get(),
        m_brushHudText.Get(),
        m_brushPresenterBtnHover.Get(),
        m_brushPresenterBtnActive.Get(),
        m_brushLaserCore.Get(),
        m_textFormatPresenter.Get()
    };
    UIViews::PresenterBarView::Render(m_d2dContext.Get(), presenterBar, dipWidth, dipHeight, res);
}

int D2DRenderer::HitTestPresenterBar(POINT pt) const {
    return UIViews::PresenterBarView::HitTest(pt, m_width, m_height, m_dpi);
}

void D2DRenderer::DrawTtsBar(const TtsBarRenderInfo& ttsBar, float topOffset) {
    if (!ttsBar.visible || !m_d2dContext) return;
    float dipScale = 96.0f / (m_dpi > 0.0f ? m_dpi : 96.0f);
    float dipWidth = (float)m_width * dipScale;
    UIViews::TtsBarResources res{
        m_brushTtsBarBg.Get(),
        m_brushTtsBarBorder.Get(),
        m_brushTtsBarText.Get(),
        m_brushTtsBarBtnHover.Get(),
        m_brushTtsBarBtnActive.Get(),
        m_textFormatTtsBar.Get(),
        m_textFormatTtsSpeed.Get()
    };
    UIViews::TtsBarView::Render(m_d2dContext.Get(), ttsBar, dipWidth, topOffset, res);
}

int D2DRenderer::HitTestTtsBar(POINT pt, bool hasTabs) const {
    float topOffset = hasTabs ? 34.0f : 0.0f;
    return UIViews::TtsBarView::HitTest(pt, m_width, m_height, m_dpi, topOffset);
}

void D2DRenderer::DrawPasswordOverlay(const PasswordOverlayRenderInfo& pwdInfo) {
    if (!m_d2dContext || !pwdInfo.visible) return;
    float dipScale = 96.0f / (m_dpi > 0.0f ? m_dpi : 96.0f);
    float dipWidth = (float)m_width * dipScale;
    float dipHeight = (float)m_height * dipScale;
    UIViews::PasswordOverlayResources res{
        m_brushHelpBackdrop.Get(),
        m_brushPageShadow.Get(),
        m_brushHelpCardBg.Get(),
        m_brushTabBorder.Get(),
        m_brushTabAccent.Get(),
        m_brushHudText.Get(),
        m_brushGoToPageBox.Get(),
        m_brushHelpSubText.Get(),
        m_brushTabCloseHover.Get(),
        m_brushPropsBtn.Get(),
        m_brushPropsBtnHover.Get(),
        m_brushPropsSecBtn.Get(),
        m_brushPropsSecBtnHover.Get(),
        m_textFormatHelpTitle.Get(),
        m_textFormatTabClose.Get(),
        m_textFormatHelpSub.Get(),
        m_textFormatSearchInput.Get(),
        m_textFormatSearchBtn.Get()
    };
    UIViews::PasswordOverlayView::Render(m_d2dContext.Get(), pwdInfo, dipWidth, dipHeight, res);
}

int D2DRenderer::HitTestPasswordOverlay(POINT pt) const {
    return UIViews::PasswordOverlayView::HitTest(pt, m_width, m_height, m_dpi);
}

void D2DRenderer::DrawLinkPreviewPill(const LinkHoverRenderInfo& linkHover) {
    if (!m_d2dContext || !m_dwriteFactory || !m_textFormatHud ||
        !m_brushHudBg || !m_brushHudBorder || !m_brushHudText || linkHover.label.empty()) {
        return;
    }

    float dipScale = 96.0f / (m_dpi > 0.0f ? m_dpi : 96.0f);
    float dipWidth = (float)m_width * dipScale;
    float dipHeight = (float)m_height * dipScale;

    // HUD bottom-center pill is centered. Ensure link pill stops at least 16 DIPs before the center HUD pill.
    float centerPillWidth = 330.0f;
    float centerPillLeft = (dipWidth - centerPillWidth) * 0.5f;

    float pillLeft = 16.0f;
    float maxPillWidth = dipWidth * 0.40f;
    if (pillLeft + maxPillWidth > centerPillLeft - 16.0f) {
        maxPillWidth = (std::max)(60.0f, centerPillLeft - 16.0f - pillLeft);
    }

    if (maxPillWidth < 60.0f || dipWidth < 300.0f) {
        return; // Not enough screen room to render pill comfortably
    }

    float pillHeight = 28.0f;
    float pillTop = dipHeight - pillHeight - 16.0f;
    float padX = 12.0f;
    float maxTextWidth = (std::max)(10.0f, maxPillWidth - (padX * 2.0f));

    ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        linkHover.label.c_str(),
        (UINT32)linkHover.label.length(),
        m_textFormatHud.Get(),
        maxTextWidth,
        pillHeight,
        &textLayout
    );
    if (FAILED(hr) || !textLayout) return;

    textLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    ComPtr<IDWriteInlineObject> inlineEllipsis;
    if (SUCCEEDED(m_dwriteFactory->CreateEllipsisTrimmingSign(m_textFormatHud.Get(), &inlineEllipsis))) {
        textLayout->SetTrimming(&trimming, inlineEllipsis.Get());
    }

    DWRITE_TEXT_METRICS tm = {};
    textLayout->GetMetrics(&tm);

    float actualPillWidth = (std::min)(tm.widthIncludingTrailingWhitespace + (padX * 2.0f), maxPillWidth);
    actualPillWidth = (std::max)(actualPillWidth, 48.0f);

    D2D1_RECT_F pillRect = D2D1::RectF(pillLeft, pillTop, pillLeft + actualPillWidth, pillTop + pillHeight);
    D2D1_ROUNDED_RECT roundedPill = D2D1::RoundedRect(pillRect, 6.0f, 6.0f);

    m_d2dContext->FillRoundedRectangle(roundedPill, m_brushHudBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedPill, m_brushHudBorder.Get(), 1.0f);

    D2D1_POINT_2F textOrigin = D2D1::Point2F(pillLeft + padX, pillTop);
    m_d2dContext->DrawTextLayout(textOrigin, textLayout.Get(), m_brushHudText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void D2DRenderer::DrawOptionsOverlay(const OptionsOverlayRenderInfo& optInfo) {
    if (!m_d2dContext || !optInfo.visible) return;
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;

    UIViews::OptionsOverlayResources res{
        m_brushHelpBackdrop.Get(),
        m_brushPageShadow.Get(),
        m_brushHelpCardBg.Get(),
        m_brushHelpCardBorder.Get(),
        m_brushTabAccent.Get(),
        m_brushHudText.Get(),
        m_brushHelpSubText.Get(),
        m_brushTabCloseHover.Get(),
        m_brushTabHoverBg.Get(),
        m_brushPropsSecBtn.Get(),
        m_brushPropsSecBtnHover.Get(),
        m_brushSearchBtnActive.Get(),
        m_brushPropsBtn.Get(),
        m_brushPropsBtnHover.Get(),
        m_brushHelpRowAlt.Get(),
        m_textFormatHelpTitle.Get(),
        m_textFormatTabClose.Get(),
        m_textFormatHelpColDesc.Get(),
        m_textFormatTab.Get(),
        m_textFormatSearchBtn.Get()
    };
    UIViews::OptionsOverlayView::Render(m_d2dContext.Get(), optInfo, dipWidth, dipHeight, res);
}

int D2DRenderer::HitTestOptionsOverlay(POINT pt, int activeCategory) const {
    return UIViews::OptionsOverlayView::HitTest(pt, m_width, m_height, m_dpi, activeCategory);
}

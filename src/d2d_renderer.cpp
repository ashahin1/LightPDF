#include "d2d_renderer.hpp"
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windows.data.pdf.lib")

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

    // HUD Text Format: Segoe UI, 12pt, Semi-Bold
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-us",
        &m_textFormatHud
    );
    if (FAILED(hr)) return false;

    m_textFormatHud->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatHud->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Blank screen message format: Segoe UI, 16pt, Regular
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        16.0f,
        L"en-us",
        &m_textFormatBlank
    );
    if (FAILED(hr)) return false;

    m_textFormatBlank->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatBlank->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Title: Segoe UI, 17pt, Bold
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        17.0f,
        L"en-us",
        &m_textFormatHelpTitle
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Help Subtitle: Segoe UI, 11pt, Regular
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-us",
        &m_textFormatHelpSub
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpSub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    // Help Keys: Consolas, 12pt, Bold, Right-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatHelpKey
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpKey->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_textFormatHelpKey->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Descriptions: Segoe UI, 12pt, Regular, Left-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.5f,
        L"en-us",
        &m_textFormatHelpDesc
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatHelpDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Section Format: Segoe UI, 11.5pt, Bold, Left-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-us",
        &m_textFormatHelpSection
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpSection->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatHelpSection->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Column Key: Consolas, 10.5pt, Bold, Right-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-us",
        &m_textFormatHelpColKey
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpColKey->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_textFormatHelpColKey->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Column Description: Segoe UI, 10.5pt, Regular, Left-aligned with ellipsis trimming
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-us",
        &m_textFormatHelpColDesc
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpColDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatHelpColDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormatHelpColDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    {
        DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> inlineEllipsis;
        m_dwriteFactory->CreateEllipsisTrimmingSign(m_textFormatHelpColDesc.Get(), &inlineEllipsis);
        m_textFormatHelpColDesc->SetTrimming(&trim, inlineEllipsis.Get());
    }

    // Help Single-Category Key: Consolas, 12.0pt, Semi-Bold, Centered
    hr = m_dwriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatHelpSingleKey
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpSingleKey->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatHelpSingleKey->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Single-Category Description: Segoe UI, 12.5pt, Regular, Left-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.5f,
        L"en-us",
        &m_textFormatHelpSingleDesc
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpSingleDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatHelpSingleDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Footer Left: Segoe UI, 10.5pt, Regular, Left-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-us",
        &m_textFormatHelpFooterLeft
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpFooterLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatHelpFooterLeft->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Help Footer Right: Segoe UI, 10.5pt, Regular, Right-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-us",
        &m_textFormatHelpFooterRight
    );
    if (FAILED(hr)) return false;
    m_textFormatHelpFooterRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_textFormatHelpFooterRight->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Tab Text Format: Segoe UI, 12pt, Regular, Left-aligned, Trimming with ellipsis
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatTab
    );
    if (FAILED(hr)) return false;
    m_textFormatTab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatTab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormatTab->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    ComPtr<IDWriteInlineObject> inlineObject;
    m_dwriteFactory->CreateEllipsisTrimmingSign(m_textFormatTab.Get(), &inlineObject);
    m_textFormatTab->SetTrimming(&trimming, inlineObject.Get());

    // Tab Close Button Format: Segoe UI, 12pt, Normal, Centered
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatTabClose
    );
    if (FAILED(hr)) return false;
    m_textFormatTabClose->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatTabClose->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Tab Add Button Format: Segoe UI, 14pt, Normal, Centered
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f,
        L"en-us",
        &m_textFormatTabAdd
    );
    if (FAILED(hr)) return false;
    m_textFormatTabAdd->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatTabAdd->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Go to Page input text format: Segoe UI, 20pt, Semi-Bold, Centered
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        20.0f,
        L"en-us",
        &m_textFormatGoToPageInput
    );
    if (FAILED(hr)) return false;
    m_textFormatGoToPageInput->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatGoToPageInput->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Search Bar Input Format: Segoe UI, 12.5pt, Regular
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.5f,
        L"en-us",
        &m_textFormatSearchInput
    );
    if (FAILED(hr)) return false;
    m_textFormatSearchInput->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatSearchInput->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormatSearchInput->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Search Bar Badge Format: Segoe UI, 11.5pt, Semi-Bold
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-us",
        &m_textFormatSearchBadge
    );
    if (FAILED(hr)) return false;
    m_textFormatSearchBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatSearchBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Search Bar Button Format: Segoe UI, 11.5pt, Semi-Bold
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-us",
        &m_textFormatSearchBtn
    );
    if (FAILED(hr)) return false;
    m_textFormatSearchBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatSearchBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Document Properties Label Format: Segoe UI, 12pt, Semi-Bold, Right-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatPropsLabel
    );
    if (FAILED(hr)) return false;
    m_textFormatPropsLabel->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    m_textFormatPropsLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Document Properties Section Format: Segoe UI, 13pt, Semi-Bold, Left-aligned
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-us",
        &m_textFormatPropsSection
    );
    if (FAILED(hr)) return false;
    m_textFormatPropsSection->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatPropsSection->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Dictionary Card Word Format: Segoe UI, 14.5pt, Semi-Bold, Leading
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.5f,
        L"en-us",
        &m_textFormatDictWord
    );
    if (FAILED(hr)) return false;
    m_textFormatDictWord->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatDictWord->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Dictionary Card Category Tag Format: Segoe UI, 10.0pt, Bold, Center
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.0f,
        L"en-us",
        &m_textFormatDictTag
    );
    if (FAILED(hr)) return false;
    m_textFormatDictTag->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatDictTag->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Dictionary Card Definition Format: Segoe UI, 13.0pt, Regular, Leading
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"ar-sa",
        &m_textFormatDictDef
    );
    if (FAILED(hr)) return false;
    m_textFormatDictDef->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatDictDef->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_textFormatDictDef->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    // Dictionary Card Hint Format: Segoe UI, 9.5pt, Regular, Leading
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        9.5f,
        L"en-us",
        &m_textFormatDictHint
    );
    m_textFormatDictHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatDictHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Presenter Bar Text Format: Segoe UI, 12.0pt, Semi-Bold, Center
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-us",
        &m_textFormatPresenter
    );
    if (FAILED(hr)) return false;
    m_textFormatPresenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatPresenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &m_brushPageShadow);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f), &m_brushPageBorder);
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
    m_brushPageShadow = nullptr;
    m_brushPageBorder = nullptr;
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
    m_pageCache = PageBitmapCache();
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
    const PresenterBarRenderInfo& presenterBar
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

    std::wstring displayMsg = message.empty() ? L"Drag and drop a PDF file here,\ndouble-click to browse, or press Ctrl+O\n\n(Press F1 for keyboard shortcuts)" : message;
    m_d2dContext->DrawText(
        displayMsg.c_str(),
        (UINT32)displayMsg.length(),
        m_textFormatBlank.Get(),
        layoutRect,
        m_brushBlankText.Get()
    );

    DrawOverlays(tabs, isAddHovered, nullptr, showGoToPage, goToPageBuffer, 0, searchBar, help, docProps, &dictCard, &laser, &presenterBar);

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

void D2DRenderer::RenderPage(
    winrt::Windows::Data::Pdf::PdfPage page,
    float zoom,
    float offsetX,
    float offsetY,
    D2D1_SIZE_F pageSize,
    uint32_t currentPageIndex,
    uint32_t totalPages,
    const std::wstring& zoomModeText,
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
    const PresenterBarRenderInfo& presenterBar
) {
    if (!m_d2dContext || !m_swapChain) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    if (!page) {
        // Fallback: If page could not be retrieved, clear to dark background and render friendly message
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

        DrawOverlays(tabs, isAddHovered, &scrollbar, showGoToPage, goToPageBuffer, totalPages, searchBar, help, docProps, &dictCard, &laser, &presenterBar);

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
        return;
    }

    float topOffset = (tabs.size() > 1) ? 34.0f : 0.0f;
    float pageY = offsetY + topOffset;

    float destW = pageSize.width * zoom;
    float destH = pageSize.height * zoom;
    UINT32 renderW = (UINT32)std::max(1.0f, std::round(destW));
    UINT32 renderH = (UINT32)std::max(1.0f, std::round(destH));

    // Check if we have a valid cached bitmap for this page and zoom
    bool needRasterize = !m_pageCache.bitmap ||
                         m_pageCache.pageIndex != currentPageIndex ||
                         std::abs(m_pageCache.zoom - zoom) > 0.0001f ||
                         m_pageCache.pixelW != renderW ||
                         m_pageCache.pixelH != renderH;

    if (needRasterize && m_pdfRenderer && renderW > 0 && renderH > 0) {
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            m_dpi, m_dpi
        );
        D2D1_SIZE_U pixelSize = D2D1::SizeU(renderW, renderH);

        ComPtr<ID2D1Bitmap1> pageBitmap;
        HRESULT hrBmp = m_d2dContext->CreateBitmap(
            pixelSize,
            nullptr,
            0,
            &bp,
            &pageBitmap
        );

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
                (IUnknown*)winrt::get_abi(page),
                m_d2dContext.Get(),
                &params
            );

            m_d2dContext->EndDraw();

            m_pageCache.bitmap = pageBitmap;
            m_pageCache.pageIndex = currentPageIndex;
            m_pageCache.zoom = zoom;
            m_pageCache.pixelW = renderW;
            m_pageCache.pixelH = renderH;

            // Restore screen target bitmap
            m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
        }
    }

    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

    // 1. Draw Page Drop Shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(
        offsetX + 4.0f,
        pageY + 4.0f,
        offsetX + destW + 5.0f,
        pageY + destH + 5.0f
    );
    m_d2dContext->FillRoundedRectangle(
        D2D1::RoundedRect(shadowRect, 2.0f, 2.0f),
        m_brushPageShadow.Get()
    );

    // 2. Draw Page Background (pure white)
    D2D1_RECT_F pageRect = D2D1::RectF(
        offsetX,
        pageY,
        offsetX + destW,
        pageY + destH
    );
    m_d2dContext->FillRectangle(pageRect, m_brushPageBg.Get());

    // 3. Render PDF Content (Fast hardware bitblt or fallback direct render)
    if (m_pageCache.bitmap && m_pageCache.pageIndex == currentPageIndex) {
        m_d2dContext->DrawBitmap(m_pageCache.bitmap.Get(), pageRect);
    } else if (m_pdfRenderer) {
        PDF_RENDER_PARAMS params = {};
        params.SourceRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        params.DestinationWidth = renderW;
        params.DestinationHeight = renderH;
        params.BackgroundColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        params.IgnoreHighContrast = FALSE;

        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, pageY));
        m_pdfRenderer->RenderPageToDeviceContext(
            (IUnknown*)winrt::get_abi(page),
            m_d2dContext.Get(),
            &params
        );
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    // 3a. Draw Text Selection Highlights over page
    if (m_brushTextSelection) {
        for (const auto& span : selectionSpans) {
            if (span.pageIndex == currentPageIndex) {
                for (const auto& pr : span.rects) {
                    float hx = offsetX + pr.left * zoom;
                    float hy = pageY + pr.top * zoom;
                    float hw = (pr.right - pr.left) * zoom;
                    float hh = (pr.bottom - pr.top) * zoom;
                    D2D1_RECT_F r = D2D1::RectF(hx, hy, hx + hw, hy + hh);
                    m_d2dContext->FillRectangle(r, m_brushTextSelection.Get());
                }
            }
        }
    }

    // 3b. Draw Search Match Highlights over page (O(log N) lookup)
    struct HighlightPageComp {
        bool operator()(const SearchHighlight& a, uint32_t page) const { return a.pageIndex < page; }
        bool operator()(uint32_t page, const SearchHighlight& a) const { return page < a.pageIndex; }
    };
    auto hlRange = std::equal_range(highlights.begin(), highlights.end(), currentPageIndex, HighlightPageComp{});
    for (auto it = hlRange.first; it != hlRange.second; ++it) {
        const auto& hl = *it;
        auto drawBox = [&](const D2D1_RECT_F& pr) {
            float hx = offsetX + pr.left * zoom;
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

    // 4. Draw 1px crisp outline around page
    m_d2dContext->DrawRectangle(pageRect, m_brushPageBorder.Get(), 1.0f);

    // 5. Draw Sleek Minimalist HUD Pill (bottom-center)
    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);

    wchar_t hudText[128];
    int zoomPct = (int)std::round(zoom * 100.0f);
    if (!zoomModeText.empty()) {
        swprintf_s(hudText, L"%u / %u  \x2022  %d%% (%s)", currentPageIndex + 1, totalPages, zoomPct, zoomModeText.c_str());
    } else {
        swprintf_s(hudText, L"%u / %u  \x2022  %d%%", currentPageIndex + 1, totalPages, zoomPct);
    }

    float pillWidth = 190.0f;
    if (!zoomModeText.empty()) pillWidth = 240.0f;
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
    DrawOverlays(tabs, isAddHovered, &scrollbar, showGoToPage, goToPageBuffer, totalPages, searchBar, help, docProps, &dictCard, &laser, &presenterBar);

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
    const PresenterBarRenderInfo& presenterBar
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

        if (!found && m_pdfRenderer && renderW > 0 && renderH > 0) {
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                m_dpi, m_dpi
            );
            D2D1_SIZE_U pixelSize = D2D1::SizeU(renderW, renderH);

            ComPtr<ID2D1Bitmap1> pageBitmap;
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

                // Restore main swapchain target
                m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
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

        // 2. Draw Page Background (pure white)
        D2D1_RECT_F pageRect = D2D1::RectF(
            pageX,
            pageY,
            pageX + destW,
            pageY + destH
        );
        m_d2dContext->FillRectangle(pageRect, m_brushPageBg.Get());

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
            m_d2dContext->DrawBitmap(pCachedBmp, pageRect);
        } else if (m_pdfRenderer) {
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
        if (m_brushTextSelection) {
            for (const auto& span : selectionSpans) {
                if (span.pageIndex == vp.pageIndex) {
                    for (const auto& pr : span.rects) {
                        float hx = pageX + pr.left * zoom;
                        float hy = pageY + pr.top * zoom;
                        float hw = (pr.right - pr.left) * zoom;
                        float hh = (pr.bottom - pr.top) * zoom;
                        D2D1_RECT_F r = D2D1::RectF(hx, hy, hx + hw, hy + hh);
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

        // 4. Draw 1px crisp outline around page
        m_d2dContext->DrawRectangle(pageRect, m_brushPageBorder.Get(), 1.0f);
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

    float pillWidth = 280.0f;
    if (!zoomModeText.empty()) pillWidth = 330.0f;
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
    DrawOverlays(tabs, isAddHovered, &scrollbar, showGoToPage, goToPageBuffer, totalPages, searchBar, help, docProps, &dictCard, &laser, &presenterBar);

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
    const PresenterBarRenderInfo* pPresenterBar
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

    // 9. Draw Laser Pointer if active (topmost element)
    if (pLaser && pLaser->active) {
        DrawLaserPointer(*pLaser);
    }
}

void D2DRenderer::DrawTabBar(const std::vector<TabRenderInfo>& tabs, bool isAddHovered) {
    if (tabs.size() <= 1 || !m_d2dContext) return;

    float dipWidth = m_width * (96.0f / m_dpi);
    float barH = 34.0f;

    // 1. Tab Bar Background
    D2D1_RECT_F barRect = D2D1::RectF(0.0f, 0.0f, dipWidth, barH);
    m_d2dContext->FillRectangle(barRect, m_brushTabBarBg.Get());

    // 2. Bottom Divider Line
    m_d2dContext->DrawLine(
        D2D1::Point2F(0.0f, barH),
        D2D1::Point2F(dipWidth, barH),
        m_brushTabBorder.Get(),
        1.0f
    );

    float availW = dipWidth - 44.0f;
    float tabW = std::clamp(availW / (float)tabs.size(), 100.0f, 220.0f);

    for (size_t i = 0; i < tabs.size(); ++i) {
        float tx = (float)i * tabW;
        D2D1_RECT_F tabRect = D2D1::RectF(tx, 3.0f, tx + tabW, barH);

        if (tabs[i].isActive) {
            m_d2dContext->FillRectangle(tabRect, m_brushTabActiveBg.Get());

            // Top accent indicator bar
            D2D1_RECT_F accentRect = D2D1::RectF(tx, 1.0f, tx + tabW, 3.0f);
            m_d2dContext->FillRectangle(accentRect, m_brushTabAccent.Get());

            // Subtle vertical borders
            m_d2dContext->DrawLine(D2D1::Point2F(tx, 3.0f), D2D1::Point2F(tx, barH), m_brushTabBorder.Get(), 1.0f);
            m_d2dContext->DrawLine(D2D1::Point2F(tx + tabW, 3.0f), D2D1::Point2F(tx + tabW, barH), m_brushTabBorder.Get(), 1.0f);
        } else {
            if (tabs[i].isHovered) {
                m_d2dContext->FillRectangle(tabRect, m_brushTabHoverBg.Get());
            } else {
                m_d2dContext->FillRectangle(tabRect, m_brushTabInactiveBg.Get());
            }
            // Vertical separator between inactive tabs
            m_d2dContext->DrawLine(D2D1::Point2F(tx + tabW, 9.0f), D2D1::Point2F(tx + tabW, barH - 9.0f), m_brushTabBorder.Get(), 1.0f);
        }

        // Tab Title Text
        D2D1_RECT_F textRect = D2D1::RectF(tx + 12.0f, 4.0f, tx + tabW - 28.0f, barH);
        ID2D1SolidColorBrush* textBrush = tabs[i].isActive ? m_brushTabText.Get() : m_brushTabTextInactive.Get();
        const std::wstring& title = tabs[i].title.empty() ? L"Untitled" : tabs[i].title;
        m_d2dContext->DrawText(
            title.c_str(),
            (UINT32)title.length(),
            m_textFormatTab.Get(),
            textRect,
            textBrush
        );

        // Close Button '×' (U+00D7)
        D2D1_RECT_F closeRect = D2D1::RectF(tx + tabW - 24.0f, 9.0f, tx + tabW - 8.0f, 25.0f);
        if (tabs[i].isCloseHovered) {
            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 3.0f, 3.0f), m_brushTabCloseHover.Get());
        }
        const wchar_t* closeStr = L"\x00D7";
        m_d2dContext->DrawText(
            closeStr,
            1,
            m_textFormatTabClose.Get(),
            closeRect,
            tabs[i].isCloseHovered ? m_brushTabText.Get() : (tabs[i].isActive ? m_brushTabText.Get() : m_brushTabTextInactive.Get())
        );
    }

    // '+' Add Tab button
    float addX = (float)tabs.size() * tabW + 6.0f;
    D2D1_RECT_F addRect = D2D1::RectF(addX, 7.0f, addX + 22.0f, 27.0f);
    if (isAddHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(addRect, 4.0f, 4.0f), m_brushTabHoverBg.Get());
    }
    const wchar_t* addStr = L"+";
    m_d2dContext->DrawText(
        addStr,
        1,
        m_textFormatTabAdd.Get(),
        addRect,
        isAddHovered ? m_brushTabText.Get() : m_brushTabTextInactive.Get()
    );
}

void D2DRenderer::DrawScrollbar(const ScrollbarRenderInfo& scrollbar) {
    if (!scrollbar.visible || scrollbar.alpha <= 0.001f || !m_d2dContext) return;

    float dipWidth = m_width * (96.0f / m_dpi);
    float width = (scrollbar.isHovered || scrollbar.isDragging) ? 10.0f : 7.0f;
    float x = dipWidth - width - 4.0f;

    // 1. Draw Track
    D2D1_RECT_F trackRect = D2D1::RectF(x, scrollbar.trackY, x + width, scrollbar.trackY + scrollbar.trackH);
    D2D1_ROUNDED_RECT roundedTrack = D2D1::RoundedRect(trackRect, width * 0.5f, width * 0.5f);

    if (m_brushScrollbarTrack) {
        m_brushScrollbarTrack->SetOpacity(scrollbar.alpha * 0.08f);
        m_d2dContext->FillRoundedRectangle(roundedTrack, m_brushScrollbarTrack.Get());
    }

    // 2. Draw Thumb
    D2D1_RECT_F thumbRect = D2D1::RectF(x, scrollbar.thumbY, x + width, scrollbar.thumbY + scrollbar.thumbH);
    D2D1_ROUNDED_RECT roundedThumb = D2D1::RoundedRect(thumbRect, width * 0.5f, width * 0.5f);

    ID2D1SolidColorBrush* pThumbBrush = (scrollbar.isDragging || scrollbar.isHovered)
        ? m_brushScrollbarThumbHover.Get()
        : m_brushScrollbarThumb.Get();
    if (pThumbBrush) {
        pThumbBrush->SetOpacity(scrollbar.alpha * ((scrollbar.isDragging || scrollbar.isHovered) ? 0.70f : 0.40f));
        m_d2dContext->FillRoundedRectangle(roundedThumb, pThumbBrush);
    }

    // 3. Floating Tooltip while dragging: e.g. "Page 14 / 80"
    if (scrollbar.isDragging && scrollbar.totalPages > 0) {
        wchar_t tipText[64];
        swprintf_s(tipText, L"Page %u / %u", scrollbar.hoverPage + 1, scrollbar.totalPages);
        float tipW = 110.0f;
        float tipH = 26.0f;
        float tipX = x - tipW - 10.0f;
        float tipY = std::clamp(scrollbar.thumbY + (scrollbar.thumbH - tipH) * 0.5f, scrollbar.trackY, scrollbar.trackY + scrollbar.trackH - tipH);

        D2D1_RECT_F tipRect = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT roundedTip = D2D1::RoundedRect(tipRect, 6.0f, 6.0f);

        m_d2dContext->FillRoundedRectangle(roundedTip, m_brushHudBg.Get());
        m_d2dContext->DrawRoundedRectangle(roundedTip, m_brushHudBorder.Get(), 1.0f);
        m_d2dContext->DrawText(
            tipText,
            (UINT32)wcslen(tipText),
            m_textFormatHud.Get(),
            tipRect,
            m_brushHudText.Get()
        );
    }
}

void D2DRenderer::DrawGoToPageOverlay(const std::wstring& buffer, uint32_t totalPages) {
    if (!m_d2dContext) return;

    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);

    // 1. Dim background
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    m_d2dContext->FillRectangle(backdropRect, m_brushHelpBackdrop.Get());

    // 2. Centered Card
    float cardW = 320.0f;
    float cardH = 150.0f;
    float cardX = (dipWidth - cardW) * 0.5f;
    float cardY = (dipHeight - cardH) * 0.5f;

    D2D1_RECT_F cardRect = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 12.0f, 12.0f);

    // Drop shadow
    D2D1_RECT_F cardShadow = D2D1::RectF(cardX + 4.0f, cardY + 4.0f, cardX + cardW + 6.0f, cardY + cardH + 6.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), m_brushPageShadow.Get());

    // Card background & cyan accent border
    m_d2dContext->FillRoundedRectangle(roundedCard, m_brushHelpCardBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedCard, m_brushTabAccent.Get(), 1.5f);

    // Title: "Go to Page"
    D2D1_RECT_F titleRect = D2D1::RectF(cardX, cardY + 14.0f, cardX + cardW, cardY + 36.0f);
    const wchar_t* titleStr = L"Go to Page";
    m_d2dContext->DrawText(titleStr, (UINT32)wcslen(titleStr), m_textFormatHelpTitle.Get(), titleRect, m_brushHudText.Get());

    // Input Box in center
    float boxW = 200.0f;
    float boxH = 42.0f;
    float boxX = cardX + (cardW - boxW) * 0.5f;
    float boxY = cardY + 46.0f;

    D2D1_RECT_F boxRect = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 6.0f, 6.0f);
    m_d2dContext->FillRoundedRectangle(roundedBox, m_brushGoToPageBox.Get());
    m_d2dContext->DrawRoundedRectangle(roundedBox, m_brushTabBorder.Get(), 1.0f);

    // Display string: e.g. "42|  / 150"
    wchar_t displayText[64];
    if (buffer.empty()) {
        if (totalPages > 0) swprintf_s(displayText, L"|  / %u", totalPages);
        else swprintf_s(displayText, L"|");
    } else {
        if (totalPages > 0) swprintf_s(displayText, L"%s|  / %u", buffer.c_str(), totalPages);
        else swprintf_s(displayText, L"%s|", buffer.c_str());
    }

    m_d2dContext->DrawText(
        displayText,
        (UINT32)wcslen(displayText),
        m_textFormatGoToPageInput.Get(),
        boxRect,
        m_brushHudText.Get()
    );

    // Subtitle / Hint: "Press Enter to jump • Esc to cancel"
    D2D1_RECT_F subRect = D2D1::RectF(cardX, cardY + 104.0f, cardX + cardW, cardY + 130.0f);
    const wchar_t* subStr = L"Enter to jump  \x2022  Esc to cancel";
    m_d2dContext->DrawText(subStr, (UINT32)wcslen(subStr), m_textFormatHelpSub.Get(), subRect, m_brushHelpSubText.Get());
}

void D2DRenderer::DrawSearchBar(const SearchBarRenderInfo& searchBar) {
    if (!searchBar.visible || !m_d2dContext) return;

    float dipWidth = m_width * (96.0f / m_dpi);
    D2D1_RECT_F barRect = SearchBarLayout::GetBarRect(dipWidth, searchBar.hasTabs);

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(barRect.left + 3.0f, barRect.top + 3.0f, barRect.right + 4.0f, barRect.bottom + 4.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 6.0f, 6.0f), m_brushPageShadow.Get());

    // Search Bar Card Background & Border
    D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(barRect, 6.0f, 6.0f);
    m_d2dContext->FillRoundedRectangle(roundedBar, m_brushHudBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedBar, m_brushHudBorder.Get(), 1.0f);

    // 1. Search Query Input Area
    D2D1_RECT_F inputRect = SearchBarLayout::GetInputRect(barRect);
    if (searchBar.query.empty()) {
        const wchar_t* placeholder = L"Find in document...";
        m_d2dContext->DrawText(
            placeholder,
            (UINT32)wcslen(placeholder),
            m_textFormatSearchInput.Get(),
            inputRect,
            m_brushTabTextInactive.Get()
        );
    } else {
        bool hasArabic = false;
        for (wchar_t ch : searchBar.query) {
            if ((ch >= 0x0600 && ch <= 0x06FF) || (ch >= 0xFB50 && ch <= 0xFEFF)) {
                hasArabic = true;
                break;
            }
        }

        if (hasArabic && m_dwriteFactory) {
            float boxW = inputRect.right - inputRect.left;
            float boxH = inputRect.bottom - inputRect.top;
            if (!m_cachedSearchLayout || m_cachedSearchQuery != searchBar.query ||
                std::abs(m_cachedSearchLayoutW - boxW) > 1.0f || std::abs(m_cachedSearchLayoutH - boxH) > 1.0f) {
                m_cachedSearchLayout.Reset();
                m_cachedSearchQuery = searchBar.query;
                m_cachedSearchLayoutW = boxW;
                m_cachedSearchLayoutH = boxH;
                m_cachedSearchCaretX = 0;
                if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
                    searchBar.query.c_str(),
                    (UINT32)searchBar.query.length(),
                    m_textFormatSearchInput.Get(),
                    boxW,
                    boxH,
                    &m_cachedSearchLayout))) {
                    DWRITE_HIT_TEST_METRICS htm = {};
                    FLOAT caretY = 0;
                    m_cachedSearchLayout->HitTestTextPosition((UINT32)searchBar.query.length(), FALSE, &m_cachedSearchCaretX, &caretY, &htm);
                }
            }

            if (m_cachedSearchLayout) {
                m_d2dContext->DrawTextLayout(
                    D2D1::Point2F(inputRect.left, inputRect.top),
                    m_cachedSearchLayout.Get(),
                    m_brushHudText.Get()
                );
                float cx = inputRect.left + m_cachedSearchCaretX;
                if (cx >= inputRect.left && cx <= inputRect.right) {
                    m_d2dContext->DrawLine(
                        D2D1::Point2F(cx, inputRect.top + 3.0f),
                        D2D1::Point2F(cx, inputRect.bottom - 3.0f),
                        m_brushHudText.Get(),
                        1.5f
                    );
                }
            }
        } else {
            std::wstring queryWithCursor = searchBar.query + L"|";
            m_d2dContext->DrawText(
                queryWithCursor.c_str(),
                (UINT32)queryWithCursor.length(),
                m_textFormatSearchInput.Get(),
                inputRect,
                m_brushHudText.Get()
            );
        }
    }

    // 2. Match Count Badge
    D2D1_RECT_F badgeRect = SearchBarLayout::GetBadgeRect(barRect);
    if (searchBar.isSearching) {
        const wchar_t* searchingStr = L"Searching...";
        m_d2dContext->DrawText(
            searchingStr,
            (UINT32)wcslen(searchingStr),
            m_textFormatSearchBadge.Get(),
            badgeRect,
            m_brushHelpKeyText.Get()
        );
    } else if (!searchBar.query.empty() && !searchBar.isDebouncing) {
        wchar_t badgeText[64];
        if (searchBar.totalMatches == 0) {
            swprintf_s(badgeText, L"0 / 0");
            m_d2dContext->DrawText(
                badgeText,
                (UINT32)wcslen(badgeText),
                m_textFormatSearchBadge.Get(),
                badgeRect,
                m_brushTabTextInactive.Get()
            );
        } else {
            swprintf_s(badgeText, L"%u of %u", searchBar.activeMatch, searchBar.totalMatches);
            m_d2dContext->DrawText(
                badgeText,
                (UINT32)wcslen(badgeText),
                m_textFormatSearchBadge.Get(),
                badgeRect,
                m_brushHelpKeyText.Get()
            );
        }
    }

    // 3. Subtle Vertical Separator
    float sepX = barRect.left + 227.0f;
    m_d2dContext->DrawLine(
        D2D1::Point2F(sepX, barRect.top + 7.0f),
        D2D1::Point2F(sepX, barRect.bottom - 7.0f),
        m_brushHudBorder.Get(),
        1.0f
    );

    // 4. Previous Button (▲)
    D2D1_RECT_F prevRect = SearchBarLayout::GetPrevBtnRect(barRect);
    if (searchBar.isPrevHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(prevRect, 4.0f, 4.0f), m_brushSearchBtnBg.Get());
    }
    const wchar_t* prevIcon = L"\x25B2";
    m_d2dContext->DrawText(
        prevIcon,
        1,
        m_textFormatSearchBtn.Get(),
        prevRect,
        searchBar.isPrevHovered ? m_brushHudText.Get() : m_brushTabTextInactive.Get()
    );

    // 5. Next Button (▼)
    D2D1_RECT_F nextRect = SearchBarLayout::GetNextBtnRect(barRect);
    if (searchBar.isNextHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(nextRect, 4.0f, 4.0f), m_brushSearchBtnBg.Get());
    }
    const wchar_t* nextIcon = L"\x25BC";
    m_d2dContext->DrawText(
        nextIcon,
        1,
        m_textFormatSearchBtn.Get(),
        nextRect,
        searchBar.isNextHovered ? m_brushHudText.Get() : m_brushTabTextInactive.Get()
    );

    // 6. Match Case Button (Aa)
    D2D1_RECT_F caseRect = SearchBarLayout::GetCaseBtnRect(barRect);
    if (searchBar.matchCase) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), m_brushSearchBtnActive.Get());
        m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), m_brushTabAccent.Get(), 1.0f);
    } else if (searchBar.isCaseHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), m_brushSearchBtnBg.Get());
    }
    const wchar_t* caseText = L"Aa";
    m_d2dContext->DrawText(
        caseText,
        2,
        m_textFormatSearchBtn.Get(),
        caseRect,
        searchBar.matchCase ? m_brushHelpKeyText.Get() : (searchBar.isCaseHovered ? m_brushHudText.Get() : m_brushTabTextInactive.Get())
    );

    // 7. OCR Toggle Button (OCR)
    D2D1_RECT_F ocrRect = SearchBarLayout::GetOcrBtnRect(barRect);
    if (searchBar.ocrEnabled) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), m_brushSearchBtnActive.Get());
        m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), m_brushTabAccent.Get(), 1.0f);
    } else if (searchBar.isOcrHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), m_brushSearchBtnBg.Get());
    }
    const wchar_t* ocrText = L"OCR";
    m_d2dContext->DrawText(
        ocrText,
        3,
        m_textFormatSearchBtn.Get(),
        ocrRect,
        searchBar.ocrEnabled ? m_brushHelpKeyText.Get() : (searchBar.isOcrHovered ? m_brushHudText.Get() : m_brushTabTextInactive.Get())
    );

    // 8. Close Button (✕)
    D2D1_RECT_F closeRect = SearchBarLayout::GetCloseBtnRect(barRect);
    if (searchBar.isCloseHovered) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), m_brushTabCloseHover.Get());
    }
    const wchar_t* closeIcon = L"\x2715";
    m_d2dContext->DrawText(
        closeIcon,
        1,
        m_textFormatSearchBtn.Get(),
        closeRect,
        searchBar.isCloseHovered ? m_brushHudText.Get() : m_brushTabTextInactive.Get()
    );
}

int D2DRenderer::HitTestHelpOverlay(POINT pt) const {
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;

    float px = (float)pt.x * dipScale;
    float py = (float)pt.y * dipScale;

    D2D1_RECT_F card = HelpOverlayLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F closeBtn = HelpOverlayLayout::GetCloseBtnRect(card);

    if (px >= closeBtn.left && px <= closeBtn.right && py >= closeBtn.top && py <= closeBtn.bottom) {
        return 100; // Close button
    }

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F tabRect = HelpOverlayLayout::GetCategoryTabRect(card, i);
        if (px >= tabRect.left && px <= tabRect.right && py >= tabRect.top && py <= tabRect.bottom) {
            return i; // Category tab 0..4
        }
    }

    if (px >= card.left && px <= card.right && py >= card.top && py <= card.bottom) {
        return 999; // Inside card body
    }

    return -1; // Outside card (backdrop)
}

void D2DRenderer::DrawHelpOverlay(const HelpOverlayRenderInfo& help) {
    if (!m_d2dContext) return;

    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;

    // 1. Semi-transparent backdrop over entire window
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    m_d2dContext->FillRectangle(backdropRect, m_brushHelpBackdrop.Get());

    // 2. Centered Help Card
    D2D1_RECT_F card = HelpOverlayLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(card, 12.0f, 12.0f);

    // Drop shadow
    D2D1_RECT_F cardShadow = D2D1::RectF(card.left + 6.0f, card.top + 6.0f, card.right + 8.0f, card.bottom + 8.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), m_brushPageShadow.Get());

    // Card background & crisp border
    m_d2dContext->FillRoundedRectangle(roundedCard, m_brushHelpCardBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedCard, m_brushHelpCardBorder.Get(), 1.5f);

    // 3. Header: Title and Close Button [×]
    D2D1_RECT_F titleRect = D2D1::RectF(card.left, card.top + 14.0f, card.right, card.top + 38.0f);
    const wchar_t* titleStr = L"Keyboard & Mouse Shortcuts";
    m_d2dContext->DrawText(titleStr, (UINT32)wcslen(titleStr), m_textFormatHelpTitle.Get(), titleRect, m_brushHudText.Get());

    // Close Button [×]
    D2D1_RECT_F closeRect = HelpOverlayLayout::GetCloseBtnRect(card);
    if (help.hoveredClose == 1) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), m_brushTabCloseHover.Get());
    }
    const wchar_t* closeGlyph = L"\x00D7";
    m_d2dContext->DrawText(
        closeGlyph,
        1,
        m_textFormatTabClose.Get(),
        closeRect,
        (help.hoveredClose == 1) ? m_brushHudText.Get() : m_brushHelpSubText.Get()
    );

    // 4. Category Tabs
    static const wchar_t* tabLabels[5] = {
        L"All (27)",
        L"Navigation (6)",
        L"Zoom & View (8)",
        L"Tabs & Files (7)",
        L"Search & Tools (6)"
    };

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F tabRect = HelpOverlayLayout::GetCategoryTabRect(card, i);
        D2D1_ROUNDED_RECT rTab = D2D1::RoundedRect(tabRect, 4.0f, 4.0f);

        if (help.activeCategory == i) {
            // Active tab pill
            m_d2dContext->FillRoundedRectangle(rTab, m_brushSearchBtnActive.Get());
            m_d2dContext->DrawRoundedRectangle(rTab, m_brushTabAccent.Get(), 1.0f);
            m_d2dContext->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                m_textFormatSearchBtn.Get(),
                tabRect,
                m_brushHudText.Get()
            );
        } else if (help.hoveredCategory == i) {
            // Hovered inactive tab
            m_d2dContext->FillRoundedRectangle(rTab, m_brushTabHoverBg.Get());
            m_d2dContext->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                m_textFormatSearchBtn.Get(),
                tabRect,
                m_brushHudText.Get()
            );
        } else {
            // Inactive tab
            m_d2dContext->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                m_textFormatSearchBtn.Get(),
                tabRect,
                m_brushTabTextInactive.Get()
            );
        }
    }

    // Divider line under tabs
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 78.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 78.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    // Shortcuts Data Categorized
    struct ShortcutItem {
        const wchar_t* key;
        const wchar_t* desc;
    };

    static const ShortcutItem navItems[] = {
        { L"Page Down / Space",     L"Advance to next page" },
        { L"Page Up / Shift+Space", L"Go to previous page" },
        { L"Right / Left Arrow",    L"Next / Previous page" },
        { L"Home / End",            L"Jump to first / last page" },
        { L"Mouse Wheel",           L"Scroll page vertically" },
        { L"Middle Drag / Space",   L"Smooth pan / drag document" },
        { L"H",                     L"Hand Tool (toggle drag pan)" },
        { L"V / S",                 L"Text Selection Tool" }
    };

    static const ShortcutItem zoomItems[] = {
        { L"Ctrl + Wheel / + / -",  L"Zoom in / out centered on cursor" },
        { L"Ctrl + 0",              L"Fit full page to window" },
        { L"Ctrl + 1",              L"Actual size (100% zoom)" },
        { L"Ctrl + 2",              L"Fit page width to window" },
        { L"Ctrl + 3",              L"Toggle continuous vertical scroll" },
        { L"Double Click",          L"Fit Page / Fit Width (or Open file)" },
        { L"F11",                   L"Toggle borderless fullscreen" },
        { L"Scrollbar Drag",        L"Scrub through document pages" }
    };

    static const ShortcutItem tabItems[] = {
        { L"Ctrl + O / Ctrl + T",    L"Open PDF document in new tab" },
        { L"Ctrl + W",              L"Close active tab" },
        { L"Ctrl + Tab",            L"Switch to next tab" },
        { L"Ctrl + Shift + Tab",    L"Switch to previous tab" },
        { L"Alt + 1..9",            L"Jump directly to tab 1 through 9" },
        { L"Middle Click Tab",      L"Close clicked tab" },
        { L"Drag & Drop",           L"Open dropped PDF files as tabs" }
    };

    static const ShortcutItem toolItems[] = {
        { L"Ctrl + F",              L"Find text in document (search)" },
        { L"F3 / Shift + F3",       L"Next / previous search match" },
        { L"L",                     L"Toggle Presentation Laser Pointer" },
        { L"C",                     L"Cycle Laser Color (Red/Green/Cyan/Gold)" },
        { L"Ctrl + C",              L"Copy selected text to clipboard" },
        { L"D / Double-Click",      L"Offline English-Arabic Dictionary" },
        { L"Ctrl + P",              L"Print document (All / Current / Range)" },
        { L"Ctrl + G",              L"Go to specific page number prompt" },
        { L"Ctrl + D",              L"Document properties (Information)" },
        { L"F1 / Esc",              L"Toggle / dismiss this help overlay" }
    };

    if (help.activeCategory == 0) {
        // Mode 0: All (30) shortcuts in balanced 2-column layout
        float col1Left = card.left + 24.0f;
        float col2Left = card.left + 354.0f;
        float keyColW = 136.0f;
        float gap = 8.0f;
        float descColW = 158.0f;
        float rowH = 16.5f;
        float headerH = 18.0f;

        // Vertical divider line between columns
        m_d2dContext->DrawLine(
            D2D1::Point2F(card.left + 340.0f, card.top + 88.0f),
            D2D1::Point2F(card.left + 340.0f, card.top + 406.0f),
            m_brushTabBorder.Get(),
            1.0f
        );

        auto drawSection = [&](float colX, float startY, const wchar_t* title, const ShortcutItem* items, size_t count) -> float {
            D2D1_RECT_F headRect = D2D1::RectF(colX, startY, colX + 302.0f, startY + headerH);
            m_d2dContext->DrawText(title, (UINT32)wcslen(title), m_textFormatHelpSection.Get(), headRect, m_brushPropsAccent.Get());

            float y = startY + headerH + 2.0f;
            for (size_t i = 0; i < count; ++i) {
                D2D1_RECT_F keyRect = D2D1::RectF(colX, y, colX + keyColW, y + rowH);
                D2D1_RECT_F descRect = D2D1::RectF(colX + keyColW + gap, y, colX + keyColW + gap + descColW, y + rowH);

                m_d2dContext->DrawText(items[i].key, (UINT32)wcslen(items[i].key), m_textFormatHelpColKey.Get(), keyRect, m_brushHelpKeyText.Get());
                m_d2dContext->DrawText(items[i].desc, (UINT32)wcslen(items[i].desc), m_textFormatHelpColDesc.Get(), descRect, m_brushHelpDescText.Get());
                y += rowH;
            }
            return y;
        };

        // Left Column: Navigation (6) + Tabs & Files (7)
        float curY1 = drawSection(col1Left, card.top + 86.0f, L"NAVIGATION", navItems, sizeof(navItems) / sizeof(navItems[0]));
        drawSection(col1Left, curY1 + 10.0f, L"TABS & FILES", tabItems, sizeof(tabItems) / sizeof(tabItems[0]));

        // Right Column: Zoom & View (8) + Search & Tools (6)
        float curY2 = drawSection(col2Left, card.top + 86.0f, L"ZOOM & VIEW", zoomItems, sizeof(zoomItems) / sizeof(zoomItems[0]));
        drawSection(col2Left, curY2 + 10.0f, L"SEARCH & TOOLS", toolItems, sizeof(toolItems) / sizeof(toolItems[0]));
    } else {
        // Modes 1..4: Single category view with spacious row badges
        const ShortcutItem* catItems = nullptr;
        size_t catCount = 0;

        switch (help.activeCategory) {
        case 1:
            catItems = navItems;
            catCount = sizeof(navItems) / sizeof(navItems[0]);
            break;
        case 2:
            catItems = zoomItems;
            catCount = sizeof(zoomItems) / sizeof(zoomItems[0]);
            break;
        case 3:
            catItems = tabItems;
            catCount = sizeof(tabItems) / sizeof(tabItems[0]);
            break;
        case 4:
            catItems = toolItems;
            catCount = sizeof(toolItems) / sizeof(toolItems[0]);
            break;
        }

        if (catItems && catCount > 0) {
            float startY = card.top + 92.0f;
            float rowH = 36.0f;
            float rowW = card.right - card.left - 48.0f;

            for (size_t i = 0; i < catCount; ++i) {
                float y = startY + (float)i * rowH;

                // Subtle alternating row background
                if (i % 2 == 1) {
                    D2D1_RECT_F altRect = D2D1::RectF(card.left + 24.0f, y, card.left + 24.0f + rowW, y + 32.0f);
                    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(altRect, 4.0f, 4.0f), m_brushHelpRowAlt.Get());
                }

                // Keycap badge
                float keycapX = card.left + 32.0f;
                float keycapY = y + 3.0f;
                float keycapW = 180.0f;
                float keycapH = 26.0f;
                D2D1_RECT_F keycapRect = D2D1::RectF(keycapX, keycapY, keycapX + keycapW, keycapY + keycapH);
                D2D1_ROUNDED_RECT rKeycap = D2D1::RoundedRect(keycapRect, 4.0f, 4.0f);

                m_d2dContext->FillRoundedRectangle(rKeycap, m_brushHelpKeycapBg.Get());
                m_d2dContext->DrawRoundedRectangle(rKeycap, m_brushHelpKeycapBorder.Get(), 1.0f);

                m_d2dContext->DrawText(
                    catItems[i].key,
                    (UINT32)wcslen(catItems[i].key),
                    m_textFormatHelpSingleKey.Get(),
                    keycapRect,
                    m_brushHelpKeyText.Get()
                );

                // Description
                float descX = keycapX + keycapW + 18.0f;
                float descW = card.right - 32.0f - descX;
                D2D1_RECT_F descRect = D2D1::RectF(descX, y, descX + descW, y + 32.0f);

                m_d2dContext->DrawText(
                    catItems[i].desc,
                    (UINT32)wcslen(catItems[i].desc),
                    m_textFormatHelpSingleDesc.Get(),
                    descRect,
                    m_brushHelpDescText.Get()
                );
            }
        }
    }

    // 5. Footer: Divider line and Keyboard Hints
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 412.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 412.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    D2D1_RECT_F footLeftRect = D2D1::RectF(card.left + 24.0f, card.top + 414.0f, card.left + 380.0f, card.top + 434.0f);
    const wchar_t* footLeftStr = L"Switch tabs: 1\x2013\x0035, Tab / Shift+Tab, or \x2190 \x2192";
    m_d2dContext->DrawText(footLeftStr, (UINT32)wcslen(footLeftStr), m_textFormatHelpFooterLeft.Get(), footLeftRect, m_brushHelpSubText.Get());

    D2D1_RECT_F footRightRect = D2D1::RectF(card.right - 200.0f, card.top + 414.0f, card.right - 24.0f, card.top + 434.0f);
    const wchar_t* footRightStr = L"Press Esc or F1 to close";
    m_d2dContext->DrawText(footRightStr, (UINT32)wcslen(footRightStr), m_textFormatHelpFooterRight.Get(), footRightRect, m_brushHelpSubText.Get());
}

int D2DRenderer::HitTestDocumentProperties(POINT pt) const {
    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;

    float px = (float)pt.x * dipScale;
    float py = (float)pt.y * dipScale;

    D2D1_RECT_F card = DocumentPropertiesLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F closeRect = DocumentPropertiesLayout::GetCloseBtnRect(card);
    D2D1_RECT_F copyRect = DocumentPropertiesLayout::GetCopyBtnRect(card);
    D2D1_RECT_F okRect = DocumentPropertiesLayout::GetOkBtnRect(card);

    if (px >= closeRect.left && px <= closeRect.right && py >= closeRect.top && py <= closeRect.bottom) {
        return 1; // Close button
    }
    if (px >= copyRect.left && px <= copyRect.right && py >= copyRect.top && py <= copyRect.bottom) {
        return 2; // Copy All button
    }
    if (px >= okRect.left && px <= okRect.right && py >= okRect.top && py <= okRect.bottom) {
        return 3; // OK button
    }

    if (px >= card.left && px <= card.right && py >= card.top && py <= card.bottom) {
        return 0; // Inside card body
    }

    return -1; // Backdrop click (outside card)
}

void D2DRenderer::DrawDocumentProperties(const DocumentPropertiesRenderInfo& props) {
    if (!m_d2dContext) return;

    float dipScale = 96.0f / m_dpi;
    float dipWidth = m_width * dipScale;
    float dipHeight = m_height * dipScale;

    // 1. Semi-transparent backdrop
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    m_d2dContext->FillRectangle(backdropRect, m_brushHelpBackdrop.Get());

    // 2. Card container & drop shadow
    D2D1_RECT_F card = DocumentPropertiesLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F cardShadow = D2D1::RectF(card.left + 6.0f, card.top + 6.0f, card.right + 8.0f, card.bottom + 8.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), m_brushPageShadow.Get());

    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(card, 12.0f, 12.0f), m_brushHelpCardBg.Get());
    m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(card, 12.0f, 12.0f), m_brushHelpCardBorder.Get(), 1.5f);

    // 3. Header
    D2D1_RECT_F titleRect = D2D1::RectF(card.left + 24.0f, card.top + 16.0f, card.right - 50.0f, card.top + 42.0f);
    const wchar_t* titleText = L"Document Properties";
    m_d2dContext->DrawText(titleText, (UINT32)wcslen(titleText), m_textFormatHelpTitle.Get(), titleRect, m_brushHudText.Get());

    // Close button [×]
    D2D1_RECT_F closeRect = DocumentPropertiesLayout::GetCloseBtnRect(card);
    if (props.hoveredBtn == 1) {
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), m_brushPropsSecBtnHover.Get());
    }
    const wchar_t* closeGlyph = L"\x00D7";
    m_d2dContext->DrawText(closeGlyph, 1, m_textFormatTabClose.Get(), closeRect, (props.hoveredBtn == 1) ? m_brushHudText.Get() : m_brushHelpSubText.Get());

    // Header divider line
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 48.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 48.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    // 4. Section 1: Document Information
    D2D1_RECT_F sec1Rect = D2D1::RectF(card.left + 24.0f, card.top + 56.0f, card.right - 24.0f, card.top + 78.0f);
    const wchar_t* sec1Title = L"Document Information";
    m_d2dContext->DrawText(sec1Title, (UINT32)wcslen(sec1Title), m_textFormatPropsSection.Get(), sec1Rect, m_brushPropsAccent.Get());

    struct FieldPair {
        const wchar_t* label;
        const std::wstring& value;
    };

    FieldPair sec1Fields[] = {
        { L"Title:",    props.title },
        { L"Author:",   props.author },
        { L"Subject:",  props.subject },
        { L"Keywords:", props.keywords },
        { L"Creator:",  props.creator },
        { L"Producer:", props.producer }
    };

    float y0 = card.top + 84.0f;
    float rowH = 28.0f;
    float labelW = 96.0f;
    float gap = 14.0f;

    for (int i = 0; i < 6; ++i) {
        float rowY = y0 + i * rowH;
        D2D1_RECT_F lRect = D2D1::RectF(card.left + 24.0f, rowY, card.left + 24.0f + labelW, rowY + rowH);
        D2D1_RECT_F vRect = D2D1::RectF(card.left + 24.0f + labelW + gap, rowY, card.right - 24.0f, rowY + rowH);

        m_d2dContext->DrawText(sec1Fields[i].label, (UINT32)wcslen(sec1Fields[i].label), m_textFormatPropsLabel.Get(), lRect, m_brushHelpSubText.Get());
        
        ID2D1SolidColorBrush* valBrush = (sec1Fields[i].value == L"—") ? m_brushHelpSubText.Get() : m_brushHudText.Get();
        m_d2dContext->DrawText(sec1Fields[i].value.c_str(), (UINT32)sec1Fields[i].value.size(), m_textFormatTab.Get(), vRect, valBrush);
    }

    // Divider between sections
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 262.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 262.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    // 5. Section 2: File & Page Details
    D2D1_RECT_F sec2Rect = D2D1::RectF(card.left + 24.0f, card.top + 270.0f, card.right - 24.0f, card.top + 292.0f);
    const wchar_t* sec2Title = L"File & Page Details";
    m_d2dContext->DrawText(sec2Title, (UINT32)wcslen(sec2Title), m_textFormatPropsSection.Get(), sec2Rect, m_brushPropsAccent.Get());

    FieldPair sec2Fields[] = {
        { L"Total Pages:", props.totalPages },
        { L"File Size:",   props.fileSize },
        { L"PDF Format:",  props.pdfFormat },
        { L"Page Size:",   props.pageSize },
        { L"Created:",     props.created },
        { L"Modified:",    props.modified }
    };

    float y1 = card.top + 298.0f;
    for (int i = 0; i < 6; ++i) {
        float rowY = y1 + i * rowH;
        D2D1_RECT_F lRect = D2D1::RectF(card.left + 24.0f, rowY, card.left + 24.0f + labelW, rowY + rowH);
        D2D1_RECT_F vRect = D2D1::RectF(card.left + 24.0f + labelW + gap, rowY, card.right - 24.0f, rowY + rowH);

        m_d2dContext->DrawText(sec2Fields[i].label, (UINT32)wcslen(sec2Fields[i].label), m_textFormatPropsLabel.Get(), lRect, m_brushHelpSubText.Get());

        ID2D1SolidColorBrush* valBrush = (sec2Fields[i].value == L"—") ? m_brushHelpSubText.Get() : m_brushHudText.Get();
        m_d2dContext->DrawText(sec2Fields[i].value.c_str(), (UINT32)sec2Fields[i].value.size(), m_textFormatTab.Get(), vRect, valBrush);
    }

    // Footer divider line
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 480.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 480.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    // 6. Buttons
    D2D1_RECT_F copyRect = DocumentPropertiesLayout::GetCopyBtnRect(card);
    D2D1_RECT_F okRect = DocumentPropertiesLayout::GetOkBtnRect(card);

    // Copy All button
    ID2D1SolidColorBrush* copyBg = (props.hoveredBtn == 2) ? m_brushPropsSecBtnHover.Get() : m_brushPropsSecBtn.Get();
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(copyRect, 6.0f, 6.0f), copyBg);
    m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(copyRect, 6.0f, 6.0f), m_brushHelpCardBorder.Get(), 1.0f);

    if (props.copyFeedback) {
        const wchar_t* copiedText = L"Copied!";
        m_d2dContext->DrawText(copiedText, (UINT32)wcslen(copiedText), m_textFormatTabClose.Get(), copyRect, m_brushPropsSuccess.Get());
    } else {
        const wchar_t* copyText = L"Copy All";
        m_d2dContext->DrawText(copyText, (UINT32)wcslen(copyText), m_textFormatTabClose.Get(), copyRect, m_brushHudText.Get());
    }

    // OK button
    ID2D1SolidColorBrush* okBg = (props.hoveredBtn == 3) ? m_brushPropsBtnHover.Get() : m_brushPropsBtn.Get();
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(okRect, 6.0f, 6.0f), okBg);

    const wchar_t* okText = L"OK";
    m_d2dContext->DrawText(okText, (UINT32)wcslen(okText), m_textFormatTabClose.Get(), okRect, m_brushPageBg.Get());
}

bool D2DRenderer::PrintPageToHdc(
    winrt::Windows::Data::Pdf::PdfPage page,
    HDC hdc,
    D2D1_SIZE_F pageSize
) {
    if (!m_d3dDevice || !m_d3dContext || !m_pdfRenderer || !m_d2dContext || !page) {
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

    float maxDefWidth = DictionaryCardLayout::WIDTH - 32.0f;
    float defHeight = 36.0f;

    // Measure definition text layout height dynamically
    ComPtr<IDWriteTextLayout> defLayout;
    if (m_dwriteFactory && !dictCard.definition.empty()) {
        HRESULT hr = m_dwriteFactory->CreateTextLayout(
            dictCard.definition.c_str(),
            (UINT32)dictCard.definition.length(),
            m_textFormatDictDef.Get(),
            maxDefWidth,
            1000.0f,
            &defLayout
        );
        if (SUCCEEDED(hr) && defLayout) {
            // Check for Arabic characters to set BiDi RTL reading direction
            bool hasArabic = false;
            for (wchar_t ch : dictCard.definition) {
                if (ch >= 0x0600 && ch <= 0x06FF) {
                    hasArabic = true;
                    break;
                }
            }
            if (hasArabic) {
                defLayout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
                defLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            }

            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(defLayout->GetMetrics(&tm))) {
                defHeight = (std::max)(28.0f, tm.height);
            }
        }
    }

    // Position the card container
    D2D1_RECT_F card = DictionaryCardLayout::CalculateCardRect(
        dictCard.anchorRect, defHeight, dipWidth, dipHeight, topOffset
    );

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(card.left + 4.0f, card.top + 4.0f, card.right + 6.0f, card.bottom + 6.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 8.0f, 8.0f), m_brushPageShadow.Get());

    // Card background & crisp border
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(card, 8.0f, 8.0f), m_brushDictCardBg.Get());
    m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(card, 8.0f, 8.0f), m_brushDictCardBorder.Get(), 1.5f);

    // 1. Header: Word Title
    float badgeWidth = 0.0f;
    if (!dictCard.categoryTag.empty()) {
        badgeWidth = (float)(dictCard.categoryTag.length() * 7 + 18);
        badgeWidth = std::clamp(badgeWidth, 80.0f, 180.0f);
    }
    float wordRight = card.right - badgeWidth - 20.0f;
    D2D1_RECT_F wordRect = D2D1::RectF(card.left + 16.0f, card.top + 10.0f, (std::max)(card.left + 20.0f, wordRight), card.top + 34.0f);
    m_d2dContext->DrawText(
        dictCard.word.c_str(),
        (UINT32)dictCard.word.length(),
        m_textFormatDictWord.Get(),
        wordRect,
        m_brushHudText.Get()
    );

    // 2. Category Badge Pill
    if (!dictCard.categoryTag.empty() && m_brushDictTagBg && m_brushDictTagText) {
        D2D1_COLOR_F textColor;
        D2D1_COLOR_F bgColor;
        switch (dictCard.category) {
        case 1: // Architecture: Amber/Orange
            textColor = D2D1::ColorF(0.98f, 0.65f, 0.20f, 1.0f);
            bgColor = D2D1::ColorF(0.98f, 0.65f, 0.20f, 0.18f);
            break;
        case 2: // Networks/IoT/WSN: Cyan/Blue
            textColor = D2D1::ColorF(0.25f, 0.75f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.25f, 0.75f, 0.98f, 0.18f);
            break;
        case 3: // AI/Vision: Purple/Violet
            textColor = D2D1::ColorF(0.75f, 0.45f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.75f, 0.45f, 0.98f, 0.18f);
            break;
        case 4: // Cybersecurity: Coral/Red
            textColor = D2D1::ColorF(0.98f, 0.40f, 0.40f, 1.0f);
            bgColor = D2D1::ColorF(0.98f, 0.40f, 0.40f, 0.18f);
            break;
        case 5: // Academic/ABET/NCAAA: Emerald
            textColor = D2D1::ColorF(0.25f, 0.85f, 0.50f, 1.0f);
            bgColor = D2D1::ColorF(0.25f, 0.85f, 0.50f, 0.18f);
            break;
        case 6: // QA: Gold
            textColor = D2D1::ColorF(0.96f, 0.82f, 0.25f, 1.0f);
            bgColor = D2D1::ColorF(0.96f, 0.82f, 0.25f, 0.18f);
            break;
        case 7: // Algorithms & Optimization: Mint / Cyan Teal
            textColor = D2D1::ColorF(0.12f, 0.88f, 0.72f, 1.0f);
            bgColor = D2D1::ColorF(0.12f, 0.88f, 0.72f, 0.18f);
            break;
        default: // General
            textColor = D2D1::ColorF(0.55f, 0.78f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.55f, 0.78f, 0.98f, 0.15f);
            break;
        }

        D2D1_RECT_F badgeRect = D2D1::RectF(card.right - badgeWidth - 14.0f, card.top + 12.0f, card.right - 14.0f, card.top + 32.0f);
        m_brushDictTagBg->SetColor(bgColor);
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(badgeRect, 4.0f, 4.0f), m_brushDictTagBg.Get());
        m_brushDictTagText->SetColor(textColor);
        m_d2dContext->DrawText(
            dictCard.categoryTag.c_str(),
            (UINT32)dictCard.categoryTag.length(),
            m_textFormatDictTag.Get(),
            badgeRect,
            m_brushDictTagText.Get()
        );
    }

    // 3. Subtle Header Divider Line
    m_d2dContext->DrawLine(
        D2D1::Point2F(card.left + 16.0f, card.top + 38.0f),
        D2D1::Point2F(card.right - 16.0f, card.top + 38.0f),
        m_brushDictCardBorder.Get(),
        1.0f
    );

    // 4. Definition Content
    D2D1_RECT_F defRect = D2D1::RectF(card.left + 16.0f, card.top + 46.0f, card.right - 16.0f, card.top + 46.0f + defHeight);
    if (defLayout) {
        m_d2dContext->DrawTextLayout(
            D2D1::Point2F(defRect.left, defRect.top),
            defLayout.Get(),
            m_brushDictDefText.Get()
        );
    } else {
        m_d2dContext->DrawText(
            dictCard.definition.c_str(),
            (UINT32)dictCard.definition.length(),
            m_textFormatDictDef.Get(),
            defRect,
            m_brushDictDefText.Get()
        );
    }

    // 5. Footer Hint
    D2D1_RECT_F hintRect = D2D1::RectF(card.left + 16.0f, card.bottom - 22.0f, card.right - 16.0f, card.bottom - 6.0f);
    const wchar_t* hint = L"Esc: dismiss \x2022 Ctrl+C: copy \x2022 100% Offline Lexicon";
    m_d2dContext->DrawText(
        hint,
        (UINT32)wcslen(hint),
        m_textFormatDictHint.Get(),
        hintRect,
        m_brushDictHintText.Get()
    );
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
    float dipW = (float)m_width * dipScale;
    float dipH = (float)m_height * dipScale;

    D2D1_RECT_F barRect = PresenterBarLayout::GetBarRect(dipW, dipH);
    D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(barRect, 22.0f, 22.0f);

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(barRect.left + 3.0f, barRect.top + 3.0f, barRect.right + 4.0f, barRect.bottom + 4.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 22.0f, 22.0f), m_brushPageShadow.Get());

    // Background & border
    m_d2dContext->FillRoundedRectangle(roundedBar, m_brushHudBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedBar, m_brushHudBorder.Get(), 1.0f);

    // Buttons
    D2D1_RECT_F btnPrev = PresenterBarLayout::GetPrevBtnRect(barRect);
    D2D1_RECT_F pageInfoRect = PresenterBarLayout::GetPageInfoRect(barRect);
    D2D1_RECT_F btnNext = PresenterBarLayout::GetNextBtnRect(barRect);
    D2D1_RECT_F btnLaser = PresenterBarLayout::GetLaserBtnRect(barRect);
    D2D1_RECT_F btnColor = PresenterBarLayout::GetColorBtnRect(barRect);
    D2D1_RECT_F btnExit = PresenterBarLayout::GetExitBtnRect(barRect);

    // Highlight hovered / active buttons
    auto drawButtonBg = [&](const D2D1_RECT_F& r, int btnIdx, bool isActive = false) {
        if (isActive) {
            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 14.0f, 14.0f), m_brushPresenterBtnActive.Get());
        } else if (presenterBar.hoveredBtn == btnIdx) {
            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 14.0f, 14.0f), m_brushPresenterBtnHover.Get());
        }
    };

    drawButtonBg(btnPrev, 0);
    drawButtonBg(btnNext, 1);
    drawButtonBg(btnLaser, 2, presenterBar.isLaserActive);
    drawButtonBg(btnColor, 3);
    drawButtonBg(btnExit, 4);

    // Button Labels
    m_d2dContext->DrawText(L"◀", 1, m_textFormatPresenter.Get(), btnPrev, m_brushHudText.Get());

    wchar_t pageBuf[32];
    swprintf_s(pageBuf, L"%u / %u", presenterBar.currentPage + 1, presenterBar.totalPages);
    m_d2dContext->DrawText(pageBuf, (UINT32)wcslen(pageBuf), m_textFormatPresenter.Get(), pageInfoRect, m_brushHudText.Get());

    m_d2dContext->DrawText(L"▶", 1, m_textFormatPresenter.Get(), btnNext, m_brushHudText.Get());
    m_d2dContext->DrawText(L"Laser", 5, m_textFormatPresenter.Get(), btnLaser, m_brushHudText.Get());

    // Color indicator dot
    D2D1_POINT_2F dotCenter = D2D1::Point2F((btnColor.left + btnColor.right) * 0.5f, (btnColor.top + btnColor.bottom) * 0.5f);
    D2D1_COLOR_F dotColor = D2D1::ColorF(1.0f, 0.2f, 0.2f);
    switch (presenterBar.laserColor) {
    case LaserColor::Red:   dotColor = D2D1::ColorF(1.0f, 0.2f, 0.2f); break;
    case LaserColor::Green: dotColor = D2D1::ColorF(0.0f, 1.0f, 0.35f); break;
    case LaserColor::Cyan:  dotColor = D2D1::ColorF(0.0f, 0.85f, 1.0f); break;
    case LaserColor::Gold:  dotColor = D2D1::ColorF(1.0f, 0.75f, 0.0f); break;
    }
    if (m_brushLaserCore) {
        m_brushLaserCore->SetColor(dotColor);
        m_d2dContext->FillEllipse(D2D1::Ellipse(dotCenter, 6.0f, 6.0f), m_brushLaserCore.Get());
    }

    m_d2dContext->DrawText(L"⛶", 1, m_textFormatPresenter.Get(), btnExit, m_brushHudText.Get());
}

int D2DRenderer::HitTestPresenterBar(POINT pt) const {
    float dipScale = 96.0f / m_dpi;
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = (float)m_width * dipScale;
    float dipH = (float)m_height * dipScale;

    D2D1_RECT_F bar = PresenterBarLayout::GetBarRect(dipW, dipH);
    if (dipX < bar.left || dipX > bar.right || dipY < bar.top || dipY > bar.bottom) {
        return -1;
    }

    auto inRect = [&](const D2D1_RECT_F& r) {
        return dipX >= r.left && dipX <= r.right && dipY >= r.top && dipY <= r.bottom;
    };

    if (inRect(PresenterBarLayout::GetPrevBtnRect(bar))) return 0;
    if (inRect(PresenterBarLayout::GetNextBtnRect(bar))) return 1;
    if (inRect(PresenterBarLayout::GetLaserBtnRect(bar))) return 2;
    if (inRect(PresenterBarLayout::GetColorBtnRect(bar))) return 3;
    if (inRect(PresenterBarLayout::GetExitBtnRect(bar))) return 4;

    return 100; // Inside bar body
}

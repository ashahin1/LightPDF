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
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
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
    m_pdfRenderer = nullptr;
    m_d2dContext = nullptr;
    m_d2dDevice = nullptr;
    m_swapChain = nullptr;
    m_d3dContext = nullptr;
    m_d3dDevice = nullptr;
}

void D2DRenderer::RenderBlank(const std::wstring& message, bool showHelp) {
    if (!m_d2dContext || !m_swapChain) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

    // Convert pixel dimensions to DIPs
    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);
    D2D1_RECT_F layoutRect = D2D1::RectF(20.0f, 20.0f, dipWidth - 20.0f, dipHeight - 20.0f);

    std::wstring displayMsg = message.empty() ? L"Drag and drop a PDF file here\nor press Ctrl+O to open\n\n(Press F1 for keyboard shortcuts)" : message;
    m_d2dContext->DrawText(
        displayMsg.c_str(),
        (UINT32)displayMsg.length(),
        m_textFormatBlank.Get(),
        layoutRect,
        m_brushBlankText.Get()
    );

    if (showHelp) {
        DrawHelpOverlay();
    }

    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    } else {
        m_swapChain->Present(1, 0);
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
    bool showHelp
) {
    if (!m_d2dContext || !m_swapChain || !page) return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    m_d2dContext->BeginDraw();
    m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    m_d2dContext->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

    float destW = pageSize.width * zoom;
    float destH = pageSize.height * zoom;

    // 1. Draw Page Drop Shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(
        offsetX + 4.0f,
        offsetY + 4.0f,
        offsetX + destW + 5.0f,
        offsetY + destH + 5.0f
    );
    m_d2dContext->FillRoundedRectangle(
        D2D1::RoundedRect(shadowRect, 2.0f, 2.0f),
        m_brushPageShadow.Get()
    );

    // 2. Draw Page Background (pure white)
    D2D1_RECT_F pageRect = D2D1::RectF(
        offsetX,
        offsetY,
        offsetX + destW,
        offsetY + destH
    );
    m_d2dContext->FillRectangle(pageRect, m_brushPageBg.Get());

    // 3. Render PDF Content via Hardware Renderer
    if (m_pdfRenderer) {
        float dpiScale = m_dpi / 96.0f;
        UINT32 pixelW = (UINT32)std::max(1.0f, std::round(destW * dpiScale));
        UINT32 pixelH = (UINT32)std::max(1.0f, std::round(destH * dpiScale));

        PDF_RENDER_PARAMS params = {};
        params.SourceRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        params.DestinationWidth = pixelW;
        params.DestinationHeight = pixelH;
        params.BackgroundColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        params.IgnoreHighContrast = FALSE;

        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, offsetY));
        m_pdfRenderer->RenderPageToDeviceContext(
            (IUnknown*)winrt::get_abi(page),
            m_d2dContext.Get(),
            &params
        );
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
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

    // 6. Draw Help Overlay if toggled
    if (showHelp) {
        DrawHelpOverlay();
    }

    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        CreateDeviceResources();
        CreateWindowSizeDependentResources();
    } else {
        m_swapChain->Present(1, 0);
    }
}

void D2DRenderer::DrawHelpOverlay() {
    if (!m_d2dContext) return;

    float dipWidth = m_width * (96.0f / m_dpi);
    float dipHeight = m_height * (96.0f / m_dpi);

    // 1. Semi-transparent backdrop over entire window
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    m_d2dContext->FillRectangle(backdropRect, m_brushHelpBackdrop.Get());

    // 2. Centered Help Card
    float cardW = 540.0f;
    float cardH = 430.0f;
    float cardLeft = std::max(10.0f, (dipWidth - cardW) * 0.5f);
    float cardTop = std::max(10.0f, (dipHeight - cardH) * 0.5f);

    D2D1_RECT_F cardRect = D2D1::RectF(cardLeft, cardTop, cardLeft + cardW, cardTop + cardH);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 14.0f, 14.0f);

    // Drop shadow
    D2D1_RECT_F cardShadow = D2D1::RectF(cardLeft + 6.0f, cardTop + 6.0f, cardLeft + cardW + 8.0f, cardTop + cardH + 8.0f);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 14.0f, 14.0f), m_brushPageShadow.Get());

    // Card background & crisp border
    m_d2dContext->FillRoundedRectangle(roundedCard, m_brushHelpCardBg.Get());
    m_d2dContext->DrawRoundedRectangle(roundedCard, m_brushHelpCardBorder.Get(), 1.5f);

    // Title
    D2D1_RECT_F titleRect = D2D1::RectF(cardLeft, cardTop + 16.0f, cardLeft + cardW, cardTop + 42.0f);
    const wchar_t* titleStr = L"LightPDF \x2014 Keyboard & Mouse Shortcuts";
    m_d2dContext->DrawText(titleStr, (UINT32)wcslen(titleStr), m_textFormatHelpTitle.Get(), titleRect, m_brushHudText.Get());

    // Subtitle
    D2D1_RECT_F subRect = D2D1::RectF(cardLeft, cardTop + 42.0f, cardLeft + cardW, cardTop + 62.0f);
    const wchar_t* subStr = L"Press F1 or Esc to close";
    m_d2dContext->DrawText(subStr, (UINT32)wcslen(subStr), m_textFormatHelpSub.Get(), subRect, m_brushHelpSubText.Get());

    // Separator line
    m_d2dContext->DrawLine(
        D2D1::Point2F(cardLeft + 24.0f, cardTop + 68.0f),
        D2D1::Point2F(cardLeft + cardW - 24.0f, cardTop + 68.0f),
        m_brushHelpCardBorder.Get(),
        1.0f
    );

    // Shortcuts List
    struct ShortcutItem {
        const wchar_t* key;
        const wchar_t* desc;
    };

    static const ShortcutItem items[] = {
        { L"Ctrl + O",              L"Open PDF document file dialog" },
        { L"Ctrl + P",              L"Print document (All / Current / Range)" },
        { L"Drag & Drop",           L"Open dropped PDF file directly" },
        { L"Page Down / Space",     L"Advance to next page" },
        { L"Page Up / Shift+Space", L"Go to previous page" },
        { L"Right / Left Arrow",    L"Next / Previous page" },
        { L"Home / End",            L"Jump to first / last page" },
        { L"Mouse Wheel",           L"Scroll page vertically" },
        { L"Left / Middle Drag",    L"Smooth pan / drag document" },
        { L"Ctrl + Wheel / + / -",  L"Zoom in / out centered on cursor" },
        { L"Ctrl + 0",              L"Fit full page to window" },
        { L"Ctrl + 1",              L"Actual size (100% zoom)" },
        { L"Ctrl + 2",              L"Fit page width to window" },
        { L"Double Click",          L"Toggle between Fit Page and Fit Width" },
        { L"F11",                   L"Toggle borderless fullscreen" },
        { L"F1 / Esc",              L"Toggle / dismiss this help overlay" },
    };

    float startY = cardTop + 76.0f;
    float rowH = 21.0f;
    float keyColW = 195.0f;
    float gap = 16.0f;
    float leftColX = cardLeft + 24.0f;
    float descColX = leftColX + keyColW + gap;
    float descColW = cardW - 48.0f - keyColW - gap;

    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        float y = startY + (float)i * rowH;
        D2D1_RECT_F keyRect = D2D1::RectF(leftColX, y, leftColX + keyColW, y + rowH);
        D2D1_RECT_F descRect = D2D1::RectF(descColX, y, descColX + descColW, y + rowH);

        m_d2dContext->DrawText(items[i].key, (UINT32)wcslen(items[i].key), m_textFormatHelpKey.Get(), keyRect, m_brushHelpKeyText.Get());
        m_d2dContext->DrawText(items[i].desc, (UINT32)wcslen(items[i].desc), m_textFormatHelpDesc.Get(), descRect, m_brushHelpDescText.Get());
    }
}

bool D2DRenderer::PrintPageToHdc(
    winrt::Windows::Data::Pdf::PdfPage page,
    HDC hdc,
    D2D1_SIZE_F pageSize
) {
    if (!m_d3dDevice || !m_d3dContext || !m_pdfRenderer || !m_d2dContext || !page) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_renderMutex);

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

    // 4. Create D3D11 Texture2D for rendering
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = renderW;
    texDesc.Height = renderH;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> renderTexture;
    HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &renderTexture);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface> dxgiSurface;
    hr = renderTexture.As(&dxgiSurface);
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        printDpi, printDpi
    );

    ComPtr<ID2D1Bitmap1> targetBitmap;
    hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bp, &targetBitmap);
    if (FAILED(hr)) return false;

    // 5. Render PDF Page onto the texture
    m_d2dContext->SetTarget(targetBitmap.Get());
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

    hr = m_d2dContext->EndDraw();

    // Restore screen target bitmap immediately
    if (m_d2dTargetBitmap) {
        m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
    }

    if (FAILED(hr)) return false;

    // 6. Create staging texture to copy pixels from GPU to CPU
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> stagingTexture;
    hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) return false;

    m_d3dContext->CopyResource(stagingTexture.Get(), renderTexture.Get());

    // 7. Map staging texture and transfer to Printer HDC via StretchDIBits
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_d3dContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

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
        mapped.pData,
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );

    m_d3dContext->Unmap(stagingTexture.Get(), 0);

    return scanlines > 0;
}

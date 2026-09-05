#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include "d2d_renderer.hpp"
#include "pdf_document.hpp"

#define WM_APP_OPEN_FILE (WM_APP + 1)

enum class ZoomMode {
    FitPage,
    FitWidth,
    Custom
};

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile = L"");
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OpenFile(const std::wstring& path);
    void PromptOpenFile();
    void PromptPrint();
    void Render();
    void UpdateTitle();

    void SetZoomMode(ZoomMode mode);
    void AdjustZoom(float factor, POINT mousePos);
    void RecalculateLayout();

    void NextPage();
    void PrevPage();
    void GoToPage(uint32_t pageIndex);

    void ToggleFullscreen();

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    D2DRenderer m_renderer;
    PdfDocumentWrapper m_document;

    uint32_t m_currentPage = 0;
    ZoomMode m_zoomMode = ZoomMode::FitPage;
    float m_zoom = 1.0f;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;

    // Mouse Panning
    bool m_isPanning = false;
    POINT m_lastMousePos = { 0, 0 };

    // Fullscreen state
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_prevPlacement = { sizeof(WINDOWPLACEMENT) };

    // Help Overlay state
    bool m_showHelp = false;

    // File Open Dialog state
    std::atomic<bool> m_isDialogOpen{ false };

    // Printing state
    std::atomic<bool> m_isPrinting{ false };
    std::atomic<bool> m_cancelPrint{ false };
    std::thread m_printThread;
};

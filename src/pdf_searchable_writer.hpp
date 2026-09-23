#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

struct OcrWordItem {
    std::wstring text;
    D2D1_RECT_F dipRect = { 0, 0, 0, 0 }; // Page display coordinate space (DIPs)
};

struct OcrPageItem {
    uint32_t pageIndex = 0;
    float pageWidthDip = 0.0f;
    float pageHeightDip = 0.0f;
    std::vector<OcrWordItem> words;
};

struct WordCorrectionItem {
    uint32_t pageIndex = 0;
    std::wstring newText;
    D2D1_RECT_F pageDipRect = { 0, 0, 0, 0 }; // Page display coordinate space (DIPs)
    uint32_t color = 0x000000;                // 24-bit RGB (0x00RRGGBB)
};

class PdfSearchableWriter {
public:
    // Bake OCR invisible text layer into PDF using native incremental update.
    // If dstPdfPath == srcPdfPath, safely writes to a temporary file first (.tmp)
    // and uses ReplaceFileW / MoveFileExW for safe atomic replacement.
    static bool WriteSearchablePdf(
        const std::wstring& srcPdfPath,
        const std::wstring& dstPdfPath,
        const std::vector<OcrPageItem>& ocrPages,
        std::function<void(float progress, const std::wstring& status)> progressCallback = nullptr
    );

    // Apply inline word corrections via native incremental update.
    // Injects a background-matched cover patch and writes visible replacement text with matching color & ToUnicode CMap.
    static bool WriteWordCorrections(
        const std::wstring& srcPdfPath,
        const std::wstring& dstPdfPath,
        const std::vector<WordCorrectionItem>& corrections,
        std::function<void(float progress, const std::wstring& status)> progressCallback = nullptr
    );
};

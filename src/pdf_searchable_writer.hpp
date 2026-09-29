/**
 * @file pdf_searchable_writer.hpp
 * @brief Incremental PDF update engine for baking searchable invisible OCR text layers.
 */

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

/// @brief Represents a single OCR recognized word and its bounding box on the page.
struct OcrWordItem {
    std::wstring text;                      ///< Unicode string content of the word
    D2D1_RECT_F dipRect = { 0, 0, 0, 0 };   ///< Page display coordinate space in DIPs
};

/// @brief Represents OCR recognition results for an entire document page.
struct OcrPageItem {
    uint32_t pageIndex = 0;                 ///< 0-based page index
    float pageWidthDip = 0.0f;              ///< Page width in DIPs
    float pageHeightDip = 0.0f;             ///< Page height in DIPs
    std::vector<OcrWordItem> words;         ///< Extracted word elements
};

/**
 * @class PdfSearchableWriter
 * @brief Provides incremental PDF writing capabilities to embed invisible text layers (OCR sandwich).
 */
class PdfSearchableWriter {
public:
    /**
     * @brief Bake OCR invisible text layer into PDF using native incremental update.
     * @details If dstPdfPath == srcPdfPath, safely writes to a temporary file first (.tmp)
     *          and uses ReplaceFileW / MoveFileExW for safe atomic replacement.
     * @param srcPdfPath Path to source PDF document.
     * @param dstPdfPath Path where the output PDF should be written.
     * @param ocrPages Vector of pages containing OCR recognized text and bounding boxes.
     * @param progressCallback Optional callback invoked periodically with progress (0.0 to 1.0) and status string.
     * @return true if baking succeeded and output was written; false on error.
     */
    static bool WriteSearchablePdf(
        const std::wstring& srcPdfPath,
        const std::wstring& dstPdfPath,
        const std::vector<OcrPageItem>& ocrPages,
        std::function<void(float progress, const std::wstring& status)> progressCallback = nullptr
    );
};

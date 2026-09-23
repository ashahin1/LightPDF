#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <cstdint>
#include "pdf_parser.hpp"

struct WordEdit {
    uint32_t pageIndex = 0;
    size_t charStartIdx = 0;
    size_t charEndIdx = 0; // Exclusive
    std::wstring originalText;
    std::wstring newText;
    std::vector<PdfTextChar> originalChars; // Exact character glyphs & rects for lossless undo
    D2D1_RECT_F pageDipRect = { 0, 0, 0, 0 };
    uint32_t color = 0x000000; // 0x00RRGGBB
    bool committed = false;
};

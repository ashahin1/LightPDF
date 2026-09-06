#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct PdfTextChar {
    wchar_t ch = 0;
    D2D1_RECT_F rect = { 0, 0, 0, 0 }; // Page coordinate space in DIPs (origin top-left)
};

struct PdfPageText {
    uint32_t pageIndex = 0;
    float pageWidth = 0.0f;
    float pageHeight = 0.0f;
    std::wstring fullText;
    std::vector<PdfTextChar> chars;
    bool hasDigitalText = false;
};

class PdfParser {
public:
    PdfParser() = default;
    ~PdfParser() = default;

    // Load and index PDF file structure
    bool Load(const std::wstring& filePath);
    void Close();

    bool IsLoaded() const { return !m_buffer.empty(); }
    uint32_t GetPageCount() const { return (uint32_t)m_pageObjectOffsets.size(); }

    // Extract text for a specific page (0-based)
    bool ExtractPageText(uint32_t pageIndex, PdfPageText& outPage);

private:
    // Flate / zlib decompressor
    static bool InflateStream(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData);

    // Font CMap mapping
    std::map<uint32_t, wchar_t> ParseToUnicodeCMap(const std::vector<uint8_t>& streamData);

    // Parse stream content text operators (BT...ET, Tj, TJ, Tm, Td, etc.)
    void ParseContentStream(
        const std::vector<uint8_t>& streamBytes,
        float pageHeight,
        const std::map<std::string, std::map<uint32_t, wchar_t>>& fontCMaps,
        PdfPageText& outPage
    );

    std::vector<uint8_t> m_buffer;
    std::vector<size_t> m_pageObjectOffsets;
    std::map<uint32_t, size_t> m_objectOffsets; // Object number -> file offset
};

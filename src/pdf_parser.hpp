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

struct PdfFontInfo {
    std::string fontName;
    std::string baseFont;
    std::string subtype;
    int firstChar = 0;
    int lastChar = -1;
    std::vector<float> widths;
    float defaultWidth = 500.0f;
    std::map<uint32_t, float> cidWidths;
    std::map<uint32_t, wchar_t> toUnicode;

    float GetCharWidth(uint32_t charCode) const;
    wchar_t DecodeChar(uint32_t charCode) const;
};

struct Matrix2D {
    float a = 1.0f, b = 0.0f;
    float c = 0.0f, d = 1.0f;
    float e = 0.0f, f = 0.0f;

    static Matrix2D Identity() {
        return Matrix2D{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    }

    Matrix2D Multiply(const Matrix2D& m) const {
        return Matrix2D{
            a * m.a + b * m.c,
            a * m.b + b * m.d,
            c * m.a + d * m.c,
            c * m.b + d * m.d,
            e * m.a + f * m.c + m.e,
            e * m.b + f * m.d + m.f
        };
    }

    void Transform(float px, float py, float& ox, float& oy) const {
        ox = px * a + py * c + e;
        oy = px * b + py * d + f;
    }
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

    // Helpers to resolve indirect objects
    std::string GetObjectString(uint32_t objNum) const;
    bool GetObjectStreamData(uint32_t objNum, std::vector<uint8_t>& outData) const;
    bool FindIndirectRef(const std::string& dictStr, const std::string& key, uint32_t& outObjNum) const;
    std::string ResolveDict(const std::string& parentDict, const std::string& key) const;

    // Font and XObject parsing
    std::map<uint32_t, wchar_t> ParseToUnicodeCMap(const std::vector<uint8_t>& streamData);
    std::map<std::string, PdfFontInfo> ExtractPageFonts(const std::string& pageDict);
    std::map<std::string, uint32_t> ExtractPageXObjects(const std::string& pageDict);

    // Parse stream content text operators (BT...ET, Tj, TJ, Tm, Td, cm, Do, etc.)
    void ParseContentStream(
        const std::vector<uint8_t>& streamBytes,
        float pageHeight,
        const std::map<std::string, PdfFontInfo>& fonts,
        PdfPageText& outPage,
        const std::map<std::string, uint32_t>& xobjects = {},
        Matrix2D initialCtm = Matrix2D::Identity(),
        int recursionDepth = 0,
        D2D1_RECT_F localClip = { -1e9f, -1e9f, 1e9f, 1e9f }
    );

    std::vector<uint8_t> m_buffer;
    std::string m_bufferStr;
    std::vector<size_t> m_pageObjectOffsets;
    std::map<uint32_t, size_t> m_objectOffsets; // Object number -> file offset
};

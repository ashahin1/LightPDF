#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <cstdint>

constexpr float PDF_POINT_TO_DIP = 96.0f / 72.0f;

struct NumericTokens {
    float values[6] = { 0.0f };
    size_t count = 0;
};

struct PdfTextChar {
    wchar_t ch = 0;
    D2D1_RECT_F rect = { 0, 0, 0, 0 }; // Page coordinate space in DIPs (origin top-left)
};

struct PdfPageText {
    uint32_t pageIndex = 0;
    float pageWidth = 0.0f;  // Display width in DIPs (accounting for rotation)
    float pageHeight = 0.0f; // Display height in DIPs (accounting for rotation)
    int rotation = 0;        // Clockwise rotation degrees (0, 90, 180, 270)
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
    std::map<uint32_t, std::wstring> toUnicode;

    float GetCharWidth(uint32_t charCode) const;
    std::wstring DecodeString(uint32_t charCode) const;
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

struct PdfXRefEntry {
    int type = 0;             // 0 = free, 1 = uncompressed in file, 2 = compressed in ObjStm
    uint32_t offsetOrStm = 0; // type 1: file byte offset; type 2: container ObjStm object number
    uint32_t genOrIndex = 0;  // type 1: generation; type 2: index within ObjStm
};

struct PdfMetadata {
    std::wstring title = L"—";
    std::wstring author = L"—";
    std::wstring subject = L"—";
    std::wstring keywords = L"—";
    std::wstring creator = L"—";
    std::wstring producer = L"—";
    std::wstring creationDate = L"—";
    std::wstring modDate = L"—";
    std::wstring pdfFormat = L"—";
};

class PdfParser {
public:
    PdfParser() = default;
    ~PdfParser() { Close(); }

    // Load and index PDF file structure
    bool Load(const std::wstring& filePath);
    void Close();

    bool IsLoaded() const { return !m_bufferView.empty(); }
    uint32_t GetPageCount() const { return (uint32_t)m_pageObjectNums.size(); }

    // Extract text for a specific page (0-based)
    bool ExtractPageText(uint32_t pageIndex, PdfPageText& outPage);

    // Extract document metadata from /Info and header
    bool ExtractMetadata(PdfMetadata& outMetadata) const;
    std::string GetPdfVersion() const { return m_pdfVersion; }

    // Check if a specific page contains Type 3 fonts
    bool PageHasType3Fonts(uint32_t pageIndex) const;

private:
    // Stream decompressors & predictor decoding
    static bool DecodeASCII85(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData);
    static bool InflateStream(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData);
    static bool DecodePredictor(const std::vector<uint8_t>& inData, int predictor, int columns, int colors, int bpc, std::vector<uint8_t>& outData);

    // Cross reference and object stream parsing
    bool ParseXRefStream(size_t offset, std::string& outTrailerDict);
    bool ParseClassicXRef(size_t offset, std::string& outTrailerDict);
    void DecodeObjStream(uint32_t stmObjNum) const;

    // Helpers to resolve indirect objects
    std::string_view GetObjectView(uint32_t objNum) const;
    std::string GetObjectString(uint32_t objNum) const;
    bool GetObjectStreamData(uint32_t objNum, std::vector<uint8_t>& outData) const;
    bool FindIndirectRef(const std::string& dictStr, const std::string& key, uint32_t& outObjNum) const;
    std::string ResolveDict(const std::string& parentDict, const std::string& key) const;

    // Font and XObject parsing
    std::map<uint32_t, std::wstring> ParseToUnicodeCMap(const std::vector<uint8_t>& streamData) const;
    std::map<std::string, PdfFontInfo> ExtractPageFonts(const std::string& pageDict) const;
    std::map<std::string, uint32_t> ExtractPageXObjects(const std::string& pageDict) const;

    // Parse stream content text operators (BT...ET, Tj, TJ, Tm, Td, cm, Do, etc.)
    void ParseContentStream(
        const std::vector<uint8_t>& streamBytes,
        float cropX0,
        float cropY0,
        float cropW,
        float cropH,
        int rotate,
        const std::map<std::string, PdfFontInfo>& fonts,
        PdfPageText& outPage,
        const std::map<std::string, uint32_t>& xobjects = {},
        Matrix2D initialCtm = Matrix2D::Identity(),
        int recursionDepth = 0,
        D2D1_RECT_F localClip = { -1e9f, -1e9f, 1e9f, 1e9f }
    );

    friend class PdfSearchableWriter;

    HANDLE m_hFile = INVALID_HANDLE_VALUE;
    HANDLE m_hMapping = nullptr;
    const char* m_mappedData = nullptr;
    size_t m_fileSize = 0;
    std::string_view m_bufferView;
    std::string m_pdfVersion = "1.4";
    uint32_t m_rootObjNum = 0;
    uint32_t m_infoObjNum = 0;
    size_t m_startXrefOffset = 0;
    std::string m_idString;
    std::vector<uint32_t> m_pageObjectNums;
    std::map<uint32_t, PdfXRefEntry> m_xref;
    mutable std::map<uint32_t, std::map<uint32_t, std::string>> m_objStmCache;
    mutable std::map<uint32_t, bool> m_pageHasType3Cache;
};

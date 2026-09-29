/**
 * @file pdf_parser.hpp
 * @brief High-speed zero-copy PDF parser, cross-reference indexer, and CMap text extractor.
 */

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

/// @brief Conversion ratio from standard PDF typographical points (72 DPI) to Direct2D DIPs (96 DPI).
constexpr float PDF_POINT_TO_DIP = 96.0f / 72.0f;

/// @brief Buffer containing numeric operands for PDF graphics and text operators.
struct NumericTokens {
    float values[6] = { 0.0f };
    size_t count = 0;
};

/// @brief Represents a single decoded character glyph and its bounding box in DIPs.
struct PdfTextChar {
    wchar_t ch = 0;                     ///< Unicode character
    D2D1_RECT_F rect = { 0, 0, 0, 0 };  ///< Page coordinate space in DIPs (origin top-left)
};

/// @brief Contains all extracted digital text and character bounding geometry for a single page.
struct PdfPageText {
    uint32_t pageIndex = 0;             ///< 0-based page index
    float pageWidth = 0.0f;             ///< Display width in DIPs (accounting for rotation)
    float pageHeight = 0.0f;            ///< Display height in DIPs (accounting for rotation)
    int rotation = 0;                   ///< Clockwise rotation degrees (0, 90, 180, 270)
    std::wstring fullText;              ///< Linear text string representation of the page
    std::vector<PdfTextChar> chars;     ///< Glyph-by-glyph character list with bounding boxes
    bool hasDigitalText = false;        ///< Flag indicating if extractable digital text was found
};

/// @brief Font metrics and CMap unicode decoding table for PDF text interpretation.
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

    /// @brief Computes font glyph advance width for a character code.
    float GetCharWidth(uint32_t charCode) const;

    /// @brief Decodes a character code into a Unicode string using ToUnicode CMap or WinAnsi.
    std::wstring DecodeString(uint32_t charCode) const;

    /// @brief Decodes a single character code.
    wchar_t DecodeChar(uint32_t charCode) const;
};

/// @brief 2D affine transformation matrix for PDF graphics state coordinate spaces.
struct Matrix2D {
    float a = 1.0f, b = 0.0f;
    float c = 0.0f, d = 1.0f;
    float e = 0.0f, f = 0.0f;

    /// @brief Creates an identity transformation matrix.
    static Matrix2D Identity() {
        return Matrix2D{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    }

    /// @brief Multiplies this matrix with another affine transform matrix.
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

    /// @brief Transforms a 2D point (px, py) to (ox, oy).
    void Transform(float px, float py, float& ox, float& oy) const {
        ox = px * a + py * c + e;
        oy = px * b + py * d + f;
    }
};

/// @brief Cross-reference table entry describing an indirect object's location.
struct PdfXRefEntry {
    int type = 0;             ///< 0 = free, 1 = uncompressed in file, 2 = compressed in ObjStm
    uint32_t offsetOrStm = 0; ///< type 1: file byte offset; type 2: container ObjStm object number
    uint32_t genOrIndex = 0;  ///< type 1: generation; type 2: index within ObjStm
};

/// @brief Extracted document metadata fields from /Info dictionary and header.
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

/**
 * @class PdfParser
 * @brief High-speed thread-confined PDF parser for text extraction, CMap decoding, and metadata indexing.
 * @note Instances are thread-confined and must not be accessed concurrently from multiple threads.
 */
class PdfParser {
public:
    PdfParser() = default;
    ~PdfParser() { Close(); }

    /**
     * @brief Loads and indexes PDF file structure via memory mapping.
     * @param filePath Canonical path to PDF file.
     * @return true on success, false on failure or corrupted format.
     */
    bool Load(const std::wstring& filePath);

    /**
     * @brief Closes memory mapping and releases file handles.
     */
    void Close();

    /// @brief Returns whether a valid PDF is loaded and mapped.
    bool IsLoaded() const { return !m_bufferView.empty(); }

    /// @brief Returns indexed page count.
    uint32_t GetPageCount() const { return (uint32_t)m_pageObjectNums.size(); }

    /**
     * @brief Extracts text and glyph coordinates for a specific page.
     * @param pageIndex 0-based page index.
     * @param outPage Populated with extracted characters, bounding boxes, and metadata.
     * @return true if page text was extracted, false otherwise.
     */
    bool ExtractPageText(uint32_t pageIndex, PdfPageText& outPage);

    /**
     * @brief Extracts document metadata from /Info dictionary and header.
     * @param outMetadata Target struct receiving metadata fields.
     * @return true if metadata dictionary was resolved.
     */
    bool ExtractMetadata(PdfMetadata& outMetadata) const;

    /// @brief Returns PDF specification version string (e.g. "1.7").
    std::string GetPdfVersion() const { return m_pdfVersion; }

    /**
     * @brief Checks if a specific page contains Type 3 rasterized bitmap fonts.
     * @param pageIndex 0-based page index.
     * @return true if Type 3 fonts are present on the page.
     */
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
    // Thread safety contract:
    // PdfParser instances are designed to be thread-confined. Each background worker
    // (such as OCR baking or search threads) creates its own local PdfParser instance,
    // and the UI thread exclusively owns tab-level parsers.
    // Do not share a single PdfParser instance concurrently across threads.
    mutable std::map<uint32_t, std::map<uint32_t, std::string>> m_objStmCache;
    mutable std::map<uint32_t, bool> m_pageHasType3Cache;
};

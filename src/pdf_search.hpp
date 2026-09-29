/**
 * @file pdf_search.hpp
 * @brief Multilingual search engine with Arabic orthographic normalization and background scanning.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include "pdf_parser.hpp"
#include "pdf_searchable_writer.hpp"
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Media.Ocr.h>

/// @brief Windows message posted to HWND when background search discovers matches or finishes scanning.
#define WM_APP_SEARCH_UPDATE (WM_APP + 2)

/// @brief Holder for initialized Arabic and English OCR engines.
struct BilingualOcrEngines {
    winrt::Windows::Media::Ocr::OcrEngine arEngine{ nullptr };
    winrt::Windows::Media::Ocr::OcrEngine enEngine{ nullptr };

    bool IsValid() const { return arEngine != nullptr || enEngine != nullptr; }
};

/// @brief Factory creating both Arabic and English Windows OCR engines if available.
BilingualOcrEngines CreateBilingualOcrEngines();

/**
 * @brief Performs high-resolution dual-engine OCR on a PDF page.
 * @details Renders page at 2x resolution, runs both Arabic and English engines, sorts Arabic words RTL,
 *          and merges English tokens (URLs, emails, Latin text).
 * @param page WinRT PDF page to render.
 * @param pageIndex 0-based page index.
 * @param pageWidthDip Page width in DIPs.
 * @param pageHeightDip Page height in DIPs.
 * @param arEngine Active Arabic OCR engine.
 * @param enEngine Active English/Latin OCR engine.
 * @param outOcrPage Receives extracted words and bounding boxes.
 * @param scale Supersampling scale factor (default 2.0f).
 * @return true if any words were successfully extracted.
 */
bool ExtractBilingualPageOcr(
    winrt::Windows::Data::Pdf::PdfPage page,
    uint32_t pageIndex,
    float pageWidthDip,
    float pageHeightDip,
    winrt::Windows::Media::Ocr::OcrEngine arEngine,
    winrt::Windows::Media::Ocr::OcrEngine enEngine,
    OcrPageItem& outOcrPage,
    float scale = 2.0f
);

/**
 * @brief Converts an OcrPageItem into a PdfPageText structure for search and selection.
 * @param ocrPage Source OCR page items.
 * @param outPageText Target page text structure.
 */
void PopulatePageTextFromOcr(const OcrPageItem& ocrPage, PdfPageText& outPageText);


/// @brief Represents a single match result on a page, supporting multi-line bounding boxes.
struct SearchMatch {
    uint32_t pageIndex = 0;                 ///< 0-based index of the containing page
    D2D1_RECT_F pageRect = { 0, 0, 0, 0 };  ///< Page coordinates in DIPs (union bounding box)
    std::vector<D2D1_RECT_F> rects;         ///< Individual line rectangles for multi-line text matches
    std::wstring matchedText;               ///< Matched text content
};

/// @brief Checks if a string contains any Unicode characters in the Arabic script range.
bool ContainsArabic(const std::wstring& str);

/// @brief Checks if a string contains Arabic letters (excluding isolated diacritics and punctuation).
bool HasArabicLetters(const std::wstring& str);

/**
 * @brief Normalizes Arabic text by unifying Alef variants, Ta Marbuta, Persian characters, and stripping tashkeel.
 * @param in Input Unicode string.
 * @param out Output normalized string.
 * @param outCharMap Optional index mapping from normalized character positions back to original input indices.
 */
void NormalizeArabic(const std::wstring& in, std::wstring& out, std::vector<size_t>* outCharMap = nullptr);

/**
 * @brief Normalizes Arabic text (functional overload).
 * @param in Input Unicode string.
 * @param outCharMap Optional index mapping from normalized characters back to original positions.
 * @return Normalized Unicode string.
 */
std::wstring NormalizeArabic(const std::wstring& in, std::vector<size_t>* outCharMap = nullptr);

/// @brief Thread-safe cache of extracted page text shared between search engine and rendering components.
struct PageTextCache {
    std::mutex mutex;                                   ///< Mutex guarding access to the cached pages vector
    std::vector<std::shared_ptr<PdfPageText>> pages;    ///< Cached page text pointers indexed by page
};

/**
 * @class PdfSearchEngine
 * @brief Asynchronous multi-page PDF search worker with progressive UI updates and Arabic normalization.
 */
class PdfSearchEngine {
public:
    PdfSearchEngine();
    ~PdfSearchEngine();

    /**
     * @brief Launches an asynchronous search across document pages.
     * @param hwndNotify Target HWND to receive WM_APP_SEARCH_UPDATE messages.
     * @param filePath Canonical path to the PDF file.
     * @param totalPages Total number of pages in the document.
     * @param query Search query string.
     * @param matchCase Whether to perform case-sensitive matching.
     * @param ocrEnabled Whether to fall back to OCR for scanned/image-only pages.
     * @param doc Optional WinRT document instance (used for OCR fallback).
     * @param textCache Optional shared page text cache to avoid re-parsing already loaded pages.
     */
    void StartSearch(
        HWND hwndNotify,
        const std::wstring& filePath,
        uint32_t totalPages,
        const std::wstring& query,
        bool matchCase,
        bool ocrEnabled,
        winrt::Windows::Data::Pdf::PdfDocument doc = nullptr,
        std::shared_ptr<PageTextCache> textCache = nullptr
    );

    /// @brief Cancels any ongoing search synchronously and joins the worker thread.
    void Cancel();

    /// @brief Signals the worker thread cancellation token asynchronously without blocking.
    void CancelAsync() { m_cancelToken = true; }

    /// @brief Clears active matches and resets state.
    void Clear();

    /// @brief Returns whether a search worker is actively executing in the background.
    bool IsSearching() const { return m_isSearching.load(); }

    /// @brief Returns whether at least one page scan pass has finished.
    bool HasScannedPages() const { return m_hasScannedPages.load(); }

    /// @brief Returns total count of matches found so far.
    uint32_t GetTotalMatches() const;

    /// @brief Returns index of currently active match (0-based, or -1 if none).
    int GetActiveMatchIndex() const;

    /// @brief Returns the active match object.
    SearchMatch GetActiveMatch() const;

    /// @brief Returns all matches discovered on a specific page.
    std::vector<SearchMatch> GetMatchesForPage(uint32_t pageIndex) const;

    /// @brief Returns a copy of all discovered matches across all pages.
    std::vector<SearchMatch> GetAllMatches() const;

    /// @brief Advances active match index to the next match, wrapping around if needed.
    bool NextMatch();

    /// @brief Moves active match index to the previous match, wrapping around if needed.
    bool PrevMatch();

    /// @brief Returns the query string currently being searched.
    std::wstring GetCurrentQuery() const;

private:
    void SearchWorker(
        HWND hwndNotify,
        std::wstring filePath,
        uint32_t totalPages,
        std::wstring query,
        bool matchCase,
        bool ocrEnabled,
        winrt::Windows::Data::Pdf::PdfDocument doc,
        std::shared_ptr<PageTextCache> textCache
    );

    mutable std::mutex m_mutex;
    std::thread m_workerThread;
    std::atomic<bool> m_cancelToken{ false };
    std::atomic<bool> m_isSearching{ false };
    std::atomic<bool> m_hasScannedPages{ false };

    std::wstring m_currentQuery;
    bool m_currentMatchCase = false;
    std::vector<SearchMatch> m_matches;
    int m_activeMatchIndex = -1;
};


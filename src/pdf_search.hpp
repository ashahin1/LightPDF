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
#include <winrt/Windows.Data.Pdf.h>

#define WM_APP_SEARCH_UPDATE (WM_APP + 2)

struct SearchMatch {
    uint32_t pageIndex = 0;
    D2D1_RECT_F pageRect = { 0, 0, 0, 0 }; // Page coordinates in DIPs (union bounding box)
    std::vector<D2D1_RECT_F> rects;        // Line rectangles (supports multi-line matches)
    std::wstring matchedText;
};

// Arabic script detection and orthographic normalization helpers
bool ContainsArabic(const std::wstring& str);
bool HasArabicLetters(const std::wstring& str);
std::wstring NormalizeArabic(const std::wstring& in, std::vector<size_t>* outCharMap = nullptr);

struct PageTextCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<PdfPageText>> pages;
};

class PdfSearchEngine {
public:
    PdfSearchEngine();
    ~PdfSearchEngine();

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

    void Cancel();
    void CancelAsync() { m_cancelToken = true; }
    void Clear();

    bool IsSearching() const { return m_isSearching.load(); }
    bool HasScannedPages() const { return m_hasScannedPages.load(); }
    uint32_t GetTotalMatches() const;
    int GetActiveMatchIndex() const;

    SearchMatch GetActiveMatch() const;
    std::vector<SearchMatch> GetMatchesForPage(uint32_t pageIndex) const;
    std::vector<SearchMatch> GetAllMatches() const;

    bool NextMatch();
    bool PrevMatch();
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

#include "pdf_search.hpp"
#include <cwctype>
#include <algorithm>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

PdfSearchEngine::PdfSearchEngine() = default;

PdfSearchEngine::~PdfSearchEngine() {
    Cancel();
}

void PdfSearchEngine::Cancel() {
    m_cancelToken = true;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    m_isSearching = false;
}

void PdfSearchEngine::Clear() {
    Cancel();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_matches.clear();
    m_activeMatchIndex = -1;
    m_currentQuery.clear();
    m_hasScannedPages = false;
}

uint32_t PdfSearchEngine::GetTotalMatches() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (uint32_t)m_matches.size();
}

int PdfSearchEngine::GetActiveMatchIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeMatchIndex;
}

SearchMatch PdfSearchEngine::GetActiveMatch() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeMatchIndex >= 0 && (size_t)m_activeMatchIndex < m_matches.size()) {
        return m_matches[m_activeMatchIndex];
    }
    return SearchMatch{};
}

std::vector<SearchMatch> PdfSearchEngine::GetMatchesForPage(uint32_t pageIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SearchMatch> result;
    for (const auto& m : m_matches) {
        if (m.pageIndex == pageIndex) {
            result.push_back(m);
        }
    }
    return result;
}

std::vector<SearchMatch> PdfSearchEngine::GetAllMatches() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_matches;
}

bool PdfSearchEngine::NextMatch() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_matches.empty()) return false;
    m_activeMatchIndex = (m_activeMatchIndex + 1) % (int)m_matches.size();
    return true;
}

bool PdfSearchEngine::PrevMatch() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_matches.empty()) return false;
    m_activeMatchIndex = (m_activeMatchIndex <= 0) ? (int)m_matches.size() - 1 : m_activeMatchIndex - 1;
    return true;
}

std::wstring PdfSearchEngine::GetCurrentQuery() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentQuery;
}

void PdfSearchEngine::StartSearch(
    HWND hwndNotify,
    const std::wstring& filePath,
    uint32_t totalPages,
    const std::wstring& query,
    bool matchCase,
    bool ocrEnabled,
    winrt::Windows::Data::Pdf::PdfDocument doc
) {
    Cancel();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_matches.clear();
        m_activeMatchIndex = -1;
        m_currentQuery = query;
        m_currentMatchCase = matchCase;
        m_hasScannedPages = false;
    }

    if (query.empty() || filePath.empty() || totalPages == 0) {
        PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
        return;
    }

    m_cancelToken = false;
    m_isSearching = true;

    m_workerThread = std::thread(
        &PdfSearchEngine::SearchWorker,
        this,
        hwndNotify,
        filePath,
        totalPages,
        query,
        matchCase,
        ocrEnabled,
        doc
    );
}

static std::wstring ToUpperStr(const std::wstring& s) {
    std::wstring result = s;
    for (wchar_t& c : result) {
        c = (wchar_t)towupper(c);
    }
    return result;
}

void PdfSearchEngine::SearchWorker(
    HWND hwndNotify,
    std::wstring filePath,
    uint32_t totalPages,
    std::wstring query,
    bool matchCase,
    bool ocrEnabled,
    winrt::Windows::Data::Pdf::PdfDocument doc
) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    PdfParser parser;
    if (!parser.Load(filePath)) {
        m_isSearching = false;
        PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
        return;
    }

    std::wstring needle = matchCase ? query : ToUpperStr(query);

    for (uint32_t p = 0; p < totalPages && !m_cancelToken; ++p) {
        PdfPageText pageText;
        parser.ExtractPageText(p, pageText);

        // Check if page has digital text
        if (!pageText.hasDigitalText) {
            m_hasScannedPages = true;

            // OCR fallback if enabled by user
            if (ocrEnabled && doc) {
                try {
                    auto ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
                    if (ocrEngine) {
                        auto page = doc.GetPage(p);
                        if (page) {
                            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
                            page.RenderToStreamAsync(stream).get();
                            auto decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
                            auto bitmap = decoder.GetSoftwareBitmapAsync().get();
                            auto ocrResult = ocrEngine.RecognizeAsync(bitmap).get();

                            float scaleX = (bitmap.PixelWidth() > 0) ? (pageText.pageWidth / (float)bitmap.PixelWidth()) : 1.0f;
                            float scaleY = (bitmap.PixelHeight() > 0) ? (pageText.pageHeight / (float)bitmap.PixelHeight()) : 1.0f;

                            for (auto line : ocrResult.Lines()) {
                                for (auto word : line.Words()) {
                                    auto r = word.BoundingRect();
                                    D2D1_RECT_F wRect = D2D1::RectF(
                                        r.X * scaleX,
                                        r.Y * scaleY,
                                        (r.X + r.Width) * scaleX,
                                        (r.Y + r.Height) * scaleY
                                    );
                                    std::wstring wText = std::wstring(word.Text());
                                    float charW = (wRect.right - wRect.left) / (float)std::max(1ULL, (unsigned long long)wText.size());
                                    for (size_t ci = 0; ci < wText.size(); ++ci) {
                                        float cx = wRect.left + ci * charW;
                                        pageText.fullText.push_back(wText[ci]);
                                        pageText.chars.push_back({ wText[ci], D2D1::RectF(cx, wRect.top, cx + charW, wRect.bottom) });
                                    }
                                    pageText.fullText.push_back(L' ');
                                    pageText.chars.push_back({ L' ', D2D1::RectF(wRect.right, wRect.top, wRect.right + 4.0f, wRect.bottom) });
                                }
                            }
                            pageText.hasDigitalText = !pageText.chars.empty();
                        }
                    }
                } catch (...) {}
            }
        }

        if (m_cancelToken) break;

        // Perform text matching on pageText
        if (!pageText.fullText.empty() && !pageText.chars.empty()) {
            std::wstring hay = matchCase ? pageText.fullText : ToUpperStr(pageText.fullText);
            size_t pos = 0;
            std::vector<SearchMatch> pageMatches;

            while ((pos = hay.find(needle, pos)) != std::wstring::npos) {
                if (m_cancelToken) break;

                // Group matching characters into line rectangles
                std::vector<D2D1_RECT_F> lineRects;
                D2D1_RECT_F curLine = { 1e9f, 1e9f, -1e9f, -1e9f };
                float curLineTop = -9999.0f;
                float overallMinX = 1e9f, overallMinY = 1e9f, overallMaxX = -1e9f, overallMaxY = -1e9f;

                for (size_t c = pos; c < pos + needle.size() && c < pageText.chars.size(); ++c) {
                    const auto& r = pageText.chars[c].rect;
                    if (r.right <= r.left || r.bottom <= r.top) continue;

                    overallMinX = std::min(overallMinX, r.left);
                    overallMinY = std::min(overallMinY, r.top);
                    overallMaxX = std::max(overallMaxX, r.right);
                    overallMaxY = std::max(overallMaxY, r.bottom);

                    if (curLineTop < -9000.0f) {
                        curLineTop = r.top;
                        curLine = r;
                    } else if (std::abs(r.top - curLineTop) > 4.0f) {
                        // Different line
                        if (curLine.right > curLine.left && curLine.bottom > curLine.top) {
                            lineRects.push_back(D2D1::RectF(curLine.left - 1.0f, curLine.top - 0.5f, curLine.right + 1.0f, curLine.bottom + 0.5f));
                        }
                        curLineTop = r.top;
                        curLine = r;
                    } else {
                        // Same line
                        curLine.left = std::min(curLine.left, r.left);
                        curLine.top = std::min(curLine.top, r.top);
                        curLine.right = std::max(curLine.right, r.right);
                        curLine.bottom = std::max(curLine.bottom, r.bottom);
                    }
                }

                if (curLine.right > curLine.left && curLine.bottom > curLine.top) {
                    lineRects.push_back(D2D1::RectF(curLine.left - 1.0f, curLine.top - 0.5f, curLine.right + 1.0f, curLine.bottom + 0.5f));
                }

                if (!lineRects.empty()) {
                    SearchMatch match;
                    match.pageIndex = p;
                    match.pageRect = D2D1::RectF(overallMinX - 1.0f, overallMinY - 0.5f, overallMaxX + 1.0f, overallMaxY + 0.5f);
                    match.rects = std::move(lineRects);
                    match.matchedText = pageText.fullText.substr(pos, needle.size());
                    pageMatches.push_back(std::move(match));
                }

                pos += std::max(1ULL, (unsigned long long)needle.size());
            }

            if (!pageMatches.empty() && !m_cancelToken) {
                std::lock_guard<std::mutex> lock(m_mutex);
                bool wasEmpty = m_matches.empty();
                for (auto& m : pageMatches) {
                    m_matches.push_back(std::move(m));
                }
                if (wasEmpty && !m_matches.empty() && m_activeMatchIndex == -1) {
                    m_activeMatchIndex = 0;
                }
                PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
            }
        }
    }

    m_isSearching = false;
    PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
}

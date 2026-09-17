#include "pdf_search.hpp"
#include <cwctype>
#include <algorithm>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Globalization.h>
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
    winrt::Windows::Data::Pdf::PdfDocument doc,
    std::shared_ptr<PageTextCache> textCache
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
        doc,
        textCache
    );
}

static std::wstring ToUpperStr(const std::wstring& s) {
    std::wstring result = s;
    for (wchar_t& c : result) {
        c = (wchar_t)towupper(c);
    }
    return result;
}

bool ContainsArabic(const std::wstring& str) {
    for (wchar_t ch : str) {
        if ((ch >= 0x0600 && ch <= 0x06FF) ||
            (ch >= 0x0750 && ch <= 0x077F) ||
            (ch >= 0x08A0 && ch <= 0x08FF) ||
            (ch >= 0xFB50 && ch <= 0xFDFF) ||
            (ch >= 0xFE70 && ch <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

bool HasArabicLetters(const std::wstring& str) {
    for (wchar_t ch : str) {
        if ((ch >= 0x0621 && ch <= 0x064A) ||
            (ch >= 0x0671 && ch <= 0x06D3) ||
            (ch >= 0xFB50 && ch <= 0xFDFF) ||
            (ch >= 0xFE70 && ch <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

std::wstring NormalizeArabic(const std::wstring& in, std::vector<size_t>* outCharMap) {
    std::wstring out;
    out.reserve(in.size());
    if (outCharMap) {
        outCharMap->clear();
        outCharMap->reserve(in.size());
    }

    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t ch = in[i];

        // 1. Strip Tashkeel diacritics & Tatweel
        if ((ch >= 0x064B && ch <= 0x0652) || ch == 0x0640 || ch == 0x0670) {
            continue;
        }

        // 2. Normalize Arabic-Indic digits (0x0660 - 0x0669) to ASCII '0' - '9'
        if (ch >= 0x0660 && ch <= 0x0669) {
            ch = L'0' + (ch - 0x0660);
        }
        // Eastern Arabic-Indic digits (0x06F0 - 0x06F9) to ASCII '0' - '9'
        else if (ch >= 0x06F0 && ch <= 0x06F9) {
            ch = L'0' + (ch - 0x06F0);
        }

        // 3. Map Presentation Forms-B (0xFE70 - 0xFEFF) and Forms-A (0xFB50 - 0xFDFF)
        if (ch == 0xFE80) ch = 0x0621;
        else if (ch == 0xFE81 || ch == 0xFE82) ch = 0x0622;
        else if (ch == 0xFE83 || ch == 0xFE84) ch = 0x0623;
        else if (ch == 0xFE85 || ch == 0xFE86) ch = 0x0624;
        else if (ch == 0xFE87 || ch == 0xFE88) ch = 0x0625;
        else if (ch >= 0xFE89 && ch <= 0xFE8C) ch = 0x0626;
        else if (ch == 0xFE8D || ch == 0xFE8E) ch = 0x0627;
        else if (ch >= 0xFE8F && ch <= 0xFE92) ch = 0x0628;
        else if (ch == 0xFE93 || ch == 0xFE94) ch = 0x0629;
        else if (ch >= 0xFE95 && ch <= 0xFE98) ch = 0x062A;
        else if (ch >= 0xFE99 && ch <= 0xFE9C) ch = 0x062B;
        else if (ch >= 0xFE9D && ch <= 0xFEA0) ch = 0x062C;
        else if (ch >= 0xFEA1 && ch <= 0xFEA4) ch = 0x062D;
        else if (ch >= 0xFEA5 && ch <= 0xFEA8) ch = 0x062E;
        else if (ch == 0xFEA9 || ch == 0xFEAA) ch = 0x062F;
        else if (ch == 0xFEAB || ch == 0xFEAC) ch = 0x0630;
        else if (ch == 0xFEAD || ch == 0xFEAE) ch = 0x0631;
        else if (ch == 0xFEAF || ch == 0xFEB0) ch = 0x0632;
        else if (ch >= 0xFEB1 && ch <= 0xFEB4) ch = 0x0633;
        else if (ch >= 0xFEB5 && ch <= 0xFEB8) ch = 0x0634;
        else if (ch >= 0xFEB9 && ch <= 0xFEBC) ch = 0x0635;
        else if (ch >= 0xFEBD && ch <= 0xFEC0) ch = 0x0636;
        else if (ch >= 0xFEC1 && ch <= 0xFEC4) ch = 0x0637;
        else if (ch >= 0xFEC5 && ch <= 0xFEC8) ch = 0x0638;
        else if (ch >= 0xFEC9 && ch <= 0xFECC) ch = 0x0639;
        else if (ch >= 0xFECD && ch <= 0xFED0) ch = 0x063A;
        else if (ch >= 0xFED1 && ch <= 0xFED4) ch = 0x0641;
        else if (ch >= 0xFED5 && ch <= 0xFED8) ch = 0x0642;
        else if (ch >= 0xFED9 && ch <= 0xFEDC) ch = 0x0643;
        else if (ch >= 0xFEDD && ch <= 0xFEE0) ch = 0x0644;
        else if (ch >= 0xFEE1 && ch <= 0xFEE4) ch = 0x0645;
        else if (ch >= 0xFEE5 && ch <= 0xFEE8) ch = 0x0646;
        else if (ch >= 0xFEE9 && ch <= 0xFEEC) ch = 0x0647;
        else if (ch == 0xFEED || ch == 0xFEEE) ch = 0x0648;
        else if (ch == 0xFEEF || ch == 0xFEF0) ch = 0x0649;
        else if (ch >= 0xFEF1 && ch <= 0xFEF4) ch = 0x064A;

        // Handle Lam-Alef ligatures in Forms-B
        if (ch >= 0xFEF5 && ch <= 0xFEFC) {
            out.push_back(0x0644); // Lam
            if (outCharMap) outCharMap->push_back(i);
            out.push_back(0x0627); // Alef
            if (outCharMap) outCharMap->push_back(i);
            continue;
        }

        // 3. Normalize Alef variants: أ, إ, آ, ٱ -> bare Alef ا
        if (ch == 0x0622 || ch == 0x0623 || ch == 0x0625 || ch == 0x0671) {
            ch = 0x0627;
        }
        // 4. Normalize Yaa / Alef Maksura: ى -> ي
        else if (ch == 0x0649) {
            ch = 0x064A;
        }
        // 5. Normalize Taa Marbuta: ة -> ه
        else if (ch == 0x0629) {
            ch = 0x0647;
        }

        out.push_back(ch);
        if (outCharMap) {
            outCharMap->push_back(i);
        }
    }

    return out;
}

void PdfSearchEngine::SearchWorker(
    HWND hwndNotify,
    std::wstring filePath,
    uint32_t totalPages,
    std::wstring query,
    bool matchCase,
    bool ocrEnabled,
    winrt::Windows::Data::Pdf::PdfDocument doc,
    std::shared_ptr<PageTextCache> textCache
) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    PdfParser parser;
    if (!parser.Load(filePath)) {
        m_isSearching = false;
        PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
        return;
    }

    std::wstring needle = matchCase ? query : ToUpperStr(query);

    bool isQueryArabic = ContainsArabic(query);
    std::wstring qFwd = NormalizeArabic(needle);
    std::wstring qRev = qFwd;
    std::wstring qWordRev;
    bool canReverse = HasArabicLetters(query);
    if (canReverse) {
        std::reverse(qRev.begin(), qRev.end());
        size_t start = 0;
        while (start < qFwd.size()) {
            while (start < qFwd.size() && iswspace(qFwd[start])) {
                qWordRev.push_back(qFwd[start]);
                start++;
            }
            size_t end = start;
            while (end < qFwd.size() && !iswspace(qFwd[end])) {
                end++;
            }
            if (start < end) {
                std::wstring token = qFwd.substr(start, end - start);
                if (HasArabicLetters(token)) {
                    std::reverse(token.begin(), token.end());
                }
                qWordRev += token;
                start = end;
            }
        }
    }

    winrt::Windows::Media::Ocr::OcrEngine ocrEngine{ nullptr };
    if (ocrEnabled && doc) {
        try {
            if (isQueryArabic) {
                auto arLang = winrt::Windows::Globalization::Language(L"ar-SA");
                if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arLang)) {
                    ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arLang);
                }
                if (!ocrEngine) {
                    auto arGen = winrt::Windows::Globalization::Language(L"ar");
                    if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arGen)) {
                        ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arGen);
                    }
                }
            }
            if (!ocrEngine) {
                ocrEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
            }
        } catch (...) {}
    }

    auto lastNotifyTime = std::chrono::steady_clock::now();
    bool hasFirstMatchNotified = false;

    PdfPageText pageText;
    for (uint32_t p = 0; p < totalPages && !m_cancelToken; ++p) {
        std::shared_ptr<PdfPageText> cachedPage;
        if (textCache) {
            std::lock_guard<std::mutex> lock(textCache->mutex);
            if (p < textCache->pages.size() && textCache->pages[p]) {
                cachedPage = textCache->pages[p];
            }
        }

        if (cachedPage && (!cachedPage->chars.empty() || !ocrEnabled || cachedPage->hasDigitalText)) {
            pageText = *cachedPage;
        } else {
            pageText.fullText.clear();
            pageText.chars.clear();
            pageText.hasDigitalText = false;
            parser.ExtractPageText(p, pageText);

            // Check if page has digital text
            if (!pageText.hasDigitalText) {
                m_hasScannedPages = true;

                // OCR fallback if enabled by user
                if (ocrEnabled && doc && ocrEngine) {
                    try {
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
                    } catch (...) {}
                }
            }

            if (textCache) {
                std::lock_guard<std::mutex> lock(textCache->mutex);
                if (p < textCache->pages.size()) {
                    textCache->pages[p] = std::make_shared<PdfPageText>(pageText);
                }
            }
        }

        // Perform text matching on pageText
        if (!pageText.fullText.empty() && !pageText.chars.empty()) {
            std::vector<SearchMatch> pageMatches;

            bool isPageArabic = isQueryArabic || ContainsArabic(pageText.fullText);

            if (isPageArabic) {
                std::vector<size_t> charMap;
                std::wstring textToNorm = matchCase ? pageText.fullText : ToUpperStr(pageText.fullText);
                std::wstring normHay = NormalizeArabic(textToNorm, &charMap);

                struct QueryVariant {
                    std::wstring q;
                    bool isReversed;
                };
                std::vector<QueryVariant> variants;
                if (!qFwd.empty()) variants.push_back({ qFwd, false });
                if (canReverse && !qRev.empty() && qRev != qFwd) variants.push_back({ qRev, true });
                if (canReverse && !qWordRev.empty() && qWordRev != qFwd && qWordRev != qRev) variants.push_back({ qWordRev, true });

                for (const auto& variant : variants) {
                    if (m_cancelToken) break;
                    size_t pos = 0;
                    while ((pos = normHay.find(variant.q, pos)) != std::wstring::npos) {
                        if (m_cancelToken) break;
                        size_t matchLen = variant.q.size();
                        if (pos >= charMap.size() || pos + matchLen - 1 >= charMap.size()) {
                            pos += std::max(1ULL, (unsigned long long)matchLen);
                            continue;
                        }

                        size_t origStart = charMap[pos];
                        size_t origEnd = charMap[pos + matchLen - 1];
                        size_t cMin = std::min(origStart, origEnd);
                        size_t cMax = std::max(origStart, origEnd);

                        // Group matching characters into line rectangles
                        std::vector<D2D1_RECT_F> lineRects;
                        D2D1_RECT_F curLine = { 1e9f, 1e9f, -1e9f, -1e9f };
                        float curLineTop = -9999.0f;
                        float overallMinX = 1e9f, overallMinY = 1e9f, overallMaxX = -1e9f, overallMaxY = -1e9f;

                        for (size_t c = cMin; c <= cMax && c < pageText.chars.size(); ++c) {
                            const auto& r = pageText.chars[c].rect;
                            if (r.right <= r.left || r.bottom <= r.top) continue;

                            overallMinX = std::min(overallMinX, r.left);
                            overallMinY = std::min(overallMinY, r.top);
                            overallMaxX = std::max(overallMaxX, r.right);
                            overallMaxY = std::max(overallMaxY, r.bottom);

                            if (curLineTop < -9000.0f) {
                                curLineTop = r.top;
                                curLine = r;
                            } else if (std::abs(r.top - curLineTop) > 6.0f) {
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
                            match.matchedText = pageText.fullText.substr(cMin, cMax - cMin + 1);

                            // Check duplicate against existing matches on this page
                            bool isDup = false;
                            for (const auto& em : pageMatches) {
                                float ix0 = std::max(match.pageRect.left, em.pageRect.left);
                                float iy0 = std::max(match.pageRect.top, em.pageRect.top);
                                float ix1 = std::min(match.pageRect.right, em.pageRect.right);
                                float iy1 = std::min(match.pageRect.bottom, em.pageRect.bottom);
                                if (ix1 > ix0 && iy1 > iy0) {
                                    float interArea = (ix1 - ix0) * (iy1 - iy0);
                                    float matchArea = (match.pageRect.right - match.pageRect.left) * (match.pageRect.bottom - match.pageRect.top);
                                    if (matchArea > 0.0f && (interArea / matchArea) > 0.6f) {
                                        isDup = true;
                                        break;
                                    }
                                }
                            }

                            if (!isDup) {
                                pageMatches.push_back(std::move(match));
                            }
                        }

                        pos += std::max(1ULL, (unsigned long long)matchLen);
                    }
                }
            } else {
                std::wstring hay = matchCase ? pageText.fullText : ToUpperStr(pageText.fullText);
                size_t pos = 0;

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
                        } else if (std::abs(r.top - curLineTop) > 6.0f) {
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
            }

            if (!pageMatches.empty() && !m_cancelToken) {
                bool shouldNotify = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    bool wasEmpty = m_matches.empty();
                    for (auto& m : pageMatches) {
                        m_matches.push_back(std::move(m));
                    }
                    if (wasEmpty && !m_matches.empty() && m_activeMatchIndex == -1) {
                        m_activeMatchIndex = 0;
                    }

                    auto now = std::chrono::steady_clock::now();
                    if (!hasFirstMatchNotified) {
                        hasFirstMatchNotified = true;
                        shouldNotify = true;
                        lastNotifyTime = now;
                    } else {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNotifyTime).count();
                        if (elapsed >= 60) {
                            shouldNotify = true;
                            lastNotifyTime = now;
                        }
                    }
                }
                if (shouldNotify) {
                    PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
                }
            }
        }
    }

    m_isSearching = false;
    PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
}

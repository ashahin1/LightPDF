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

static void ToUpperInPlace(const std::wstring& s, std::wstring& out) {
    out.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        out[i] = (wchar_t)towupper(s[i]);
    }
}

void NormalizeArabic(const std::wstring& in, std::wstring& out, std::vector<size_t>* outCharMap) {
    out.clear();
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
}

std::wstring NormalizeArabic(const std::wstring& in, std::vector<size_t>* outCharMap) {
    std::wstring out;
    NormalizeArabic(in, out, outCharMap);
    return out;
}

BilingualOcrEngines CreateBilingualOcrEngines() {
    BilingualOcrEngines engines;
    try {
        auto arLang = winrt::Windows::Globalization::Language(L"ar-SA");
        if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arLang)) {
            engines.arEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arLang);
        }
        if (!engines.arEngine) {
            auto arGen = winrt::Windows::Globalization::Language(L"ar");
            if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(arGen)) {
                engines.arEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(arGen);
            }
        }
    } catch (...) {}

    try {
        auto enLang = winrt::Windows::Globalization::Language(L"en-US");
        if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(enLang)) {
            engines.enEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(enLang);
        }
        if (!engines.enEngine) {
            engines.enEngine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        }
    } catch (...) {}

    return engines;
}

bool ExtractBilingualPageOcr(
    winrt::Windows::Data::Pdf::PdfPage page,
    uint32_t pageIndex,
    float pageWidthDip,
    float pageHeightDip,
    winrt::Windows::Media::Ocr::OcrEngine arEngine,
    winrt::Windows::Media::Ocr::OcrEngine enEngine,
    OcrPageItem& outOcrPage,
    float scale
) {
    outOcrPage.pageIndex = pageIndex;
    outOcrPage.pageWidthDip = pageWidthDip;
    outOcrPage.pageHeightDip = pageHeightDip;
    outOcrPage.words.clear();

    if (!page || (!arEngine && !enEngine)) {
        return false;
    }

    try {
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        winrt::Windows::Data::Pdf::PdfPageRenderOptions options;
        if (scale > 1.0f) {
            options.DestinationWidth((uint32_t)(pageWidthDip * scale));
            options.DestinationHeight((uint32_t)(pageHeightDip * scale));
            page.RenderToStreamAsync(stream, options).get();
        } else {
            page.RenderToStreamAsync(stream).get();
        }

        auto decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
        auto bitmap = decoder.GetSoftwareBitmapAsync().get();
        if (!bitmap) return false;

        float effectiveScaleX = (bitmap.PixelWidth() > 0) ? (pageWidthDip / (float)bitmap.PixelWidth()) : (1.0f / scale);
        float effectiveScaleY = (bitmap.PixelHeight() > 0) ? (pageHeightDip / (float)bitmap.PixelHeight()) : (1.0f / scale);

        // 1. Process Arabic OCR pass
        if (arEngine) {
            auto arResult = arEngine.RecognizeAsync(bitmap).get();
            for (auto line : arResult.Lines()) {
                std::vector<OcrWordItem> lineWords;
                for (auto word : line.Words()) {
                    auto r = word.BoundingRect();
                    D2D1_RECT_F wRect = D2D1::RectF(
                        r.X * effectiveScaleX,
                        r.Y * effectiveScaleY,
                        (r.X + r.Width) * effectiveScaleX,
                        (r.Y + r.Height) * effectiveScaleY
                    );
                    lineWords.push_back({ std::wstring(word.Text()), wRect });
                }

                // If line contains Arabic characters, sort words from right to left (descending X)
                if (ContainsArabic(std::wstring(line.Text()))) {
                    std::sort(lineWords.begin(), lineWords.end(), [](const OcrWordItem& a, const OcrWordItem& b) {
                        return a.dipRect.left > b.dipRect.left;
                    });
                }

                for (auto& wb : lineWords) {
                    outOcrPage.words.push_back(std::move(wb));
                }
            }
        }

        // 2. Process English/Latin OCR pass (capturing URLs, emails, technical terms)
        if (enEngine) {
            auto enResult = enEngine.RecognizeAsync(bitmap).get();
            for (auto line : enResult.Lines()) {
                for (auto word : line.Words()) {
                    std::wstring wText = std::wstring(word.Text());
                    // Filter for tokens containing Latin letters or digits
                    bool hasLatinOrAlnum = false;
                    for (wchar_t ch : wText) {
                        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'@' || ch == L'.') {
                            hasLatinOrAlnum = true;
                            break;
                        }
                    }
                    if (!hasLatinOrAlnum) continue;

                    auto r = word.BoundingRect();
                    D2D1_RECT_F wRect = D2D1::RectF(
                        r.X * effectiveScaleX,
                        r.Y * effectiveScaleY,
                        (r.X + r.Width) * effectiveScaleX,
                        (r.Y + r.Height) * effectiveScaleY
                    );

                    // Check for overlap with already registered Arabic words
                    bool overlaps = false;
                    for (const auto& existing : outOcrPage.words) {
                        float interL = (std::max)(existing.dipRect.left, wRect.left);
                        float interT = (std::max)(existing.dipRect.top, wRect.top);
                        float interR = (std::min)(existing.dipRect.right, wRect.right);
                        float interB = (std::min)(existing.dipRect.bottom, wRect.bottom);
                        if (interR > interL && interB > interT) {
                            float interArea = (interR - interL) * (interB - interT);
                            float wArea = (wRect.right - wRect.left) * (wRect.bottom - wRect.top);
                            if (wArea > 0.0f && interArea > 0.4f * wArea) {
                                overlaps = true;
                                break;
                            }
                        }
                    }

                    if (!overlaps) {
                        outOcrPage.words.push_back({ std::move(wText), wRect });
                    }
                }
            }
        }

        return !outOcrPage.words.empty();
    } catch (...) {
        return false;
    }
}

void PopulatePageTextFromOcr(const OcrPageItem& ocrPage, PdfPageText& outPageText) {
    outPageText.fullText.clear();
    outPageText.chars.clear();
    outPageText.pageIndex = ocrPage.pageIndex;
    outPageText.pageWidth = ocrPage.pageWidthDip;
    outPageText.pageHeight = ocrPage.pageHeightDip;

    for (const auto& wd : ocrPage.words) {
        if (wd.text.empty()) continue;
        float charW = (wd.dipRect.right - wd.dipRect.left) / (float)(std::max)(1ULL, (unsigned long long)wd.text.size());
        for (size_t ci = 0; ci < wd.text.size(); ++ci) {
            float cx = wd.dipRect.left + ci * charW;
            outPageText.fullText.push_back(wd.text[ci]);
            outPageText.chars.push_back({ wd.text[ci], D2D1::RectF(cx, wd.dipRect.top, cx + charW, wd.dipRect.bottom) });
        }
        outPageText.fullText.push_back(L' ');
        outPageText.chars.push_back({ L' ', D2D1::RectF(wd.dipRect.right, wd.dipRect.top, wd.dipRect.right + 4.0f, wd.dipRect.bottom) });
    }
    outPageText.hasDigitalText = !outPageText.chars.empty();
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

    // Initial check to verify parser can open the file
    {
        PdfParser testParser;
        if (!testParser.Load(filePath)) {
            m_isSearching = false;
            PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
            return;
        }
    }

    std::wstring needle = matchCase ? query : ToUpperStr(query);

    bool isQueryArabic = ContainsArabic(query);
    bool queryHasDigits = false;
    for (wchar_t ch : query) {
        if (ch >= L'0' && ch <= L'9') {
            queryHasDigits = true;
            break;
        }
    }
    std::wstring qFwd = NormalizeArabic(needle);

    auto lastNotifyTime = std::chrono::steady_clock::now();
    bool hasFirstMatchNotified = false;
    std::atomic<uint32_t> nextPageIndex{ 0 };

    uint32_t numWorkers = std::thread::hardware_concurrency();
    if (numWorkers == 0) numWorkers = 4;
    numWorkers = std::min({ numWorkers, 8u, totalPages });

    std::vector<std::thread> workers;
    workers.reserve(numWorkers);

    for (uint32_t w = 0; w < numWorkers; ++w) {
        workers.emplace_back([&, w]() {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            } catch (...) {}

            PdfParser parser;
            if (!parser.Load(filePath)) return;

            // Reusable buffers to eliminate per-page heap allocations
            std::wstring workerUpperBuf;
            std::wstring workerNormBuf;
            std::vector<size_t> workerCharMap;

            BilingualOcrEngines ocrEngines;
            if (ocrEnabled && doc) {
                ocrEngines = CreateBilingualOcrEngines();
            }

            PdfPageText pageText;
            while (!m_cancelToken) {
                uint32_t p = nextPageIndex.fetch_add(1);
                if (p >= totalPages) break;

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

                        // Bilingual high-resolution OCR fallback if enabled by user
                        if (ocrEnabled && doc && ocrEngines.IsValid()) {
                            try {
                                auto page = doc.GetPage(p);
                                if (page) {
                                    OcrPageItem ocrPage;
                                    if (ExtractBilingualPageOcr(page, p, pageText.pageWidth, pageText.pageHeight, ocrEngines.arEngine, ocrEngines.enEngine, ocrPage, 2.0f)) {
                                        PopulatePageTextFromOcr(ocrPage, pageText);
                                    }
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

                    bool isPageArabic = isQueryArabic || (queryHasDigits && ContainsArabic(pageText.fullText));

                    if (isPageArabic) {
                        const std::wstring* pTextToNorm = &pageText.fullText;
                        if (!matchCase) {
                            ToUpperInPlace(pageText.fullText, workerUpperBuf);
                            pTextToNorm = &workerUpperBuf;
                        }
                        NormalizeArabic(*pTextToNorm, workerNormBuf, &workerCharMap);
                        const std::wstring& normHay = workerNormBuf;
                        const std::vector<size_t>& charMap = workerCharMap;

                        if (!qFwd.empty()) {
                            size_t pos = 0;
                            while ((pos = normHay.find(qFwd, pos)) != std::wstring::npos) {
                                if (m_cancelToken) break;
                                size_t matchLen = qFwd.size();
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

                                    pageMatches.push_back(std::move(match));
                                }

                                pos += std::max(1ULL, (unsigned long long)matchLen);
                            }
                        }
                    } else {
                        const std::wstring* pHay = &pageText.fullText;
                        if (!matchCase) {
                            ToUpperInPlace(pageText.fullText, workerUpperBuf);
                            pHay = &workerUpperBuf;
                        }
                        const std::wstring& hay = *pHay;
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
                            auto it = std::upper_bound(m_matches.begin(), m_matches.end(), pageMatches.front(),
                                [](const SearchMatch& a, const SearchMatch& b) {
                                    if (a.pageIndex != b.pageIndex) return a.pageIndex < b.pageIndex;
                                    if (std::abs(a.pageRect.top - b.pageRect.top) > 1.0f) return a.pageRect.top < b.pageRect.top;
                                    return a.pageRect.left < b.pageRect.left;
                                });
                            m_matches.insert(it, std::make_move_iterator(pageMatches.begin()), std::make_move_iterator(pageMatches.end()));

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
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    m_isSearching = false;
    PostMessageW(hwndNotify, WM_APP_SEARCH_UPDATE, 0, 0);
}

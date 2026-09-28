#include "selection_controller.hpp"
#include <algorithm>
#include <cmath>

static bool HitTestCharInPage(
    const PdfPageText& pageText,
    float pdfX,
    float pdfY,
    size_t& outCharIndex,
    bool& outAfterChar
) {
    if (pageText.chars.empty()) return false;

    // 1. Direct character bounding box containment test
    for (size_t i = 0; i < pageText.chars.size(); ++i) {
        const auto& c = pageText.chars[i];
        if (c.rect.right <= c.rect.left) continue;
        if (pdfY >= c.rect.top - 2.0f && pdfY <= c.rect.bottom + 2.0f) {
            if (pdfX >= c.rect.left && pdfX <= c.rect.right) {
                outCharIndex = i;
                outAfterChar = (pdfX > (c.rect.left + c.rect.right) * 0.5f);
                return true;
            }
        }
    }

    // 2. Line proximity test: Find line nearest to pdfY
    float closestLineDist = 1e9f;
    float bestLineY = 0.0f;
    for (const auto& c : pageText.chars) {
        if (c.rect.right <= c.rect.left) continue;
        float midY = (c.rect.top + c.rect.bottom) * 0.5f;
        float dist = std::abs(pdfY - midY);
        if (dist < closestLineDist) {
            closestLineDist = dist;
            bestLineY = midY;
        }
    }

    if (closestLineDist <= 24.0f) {
        float minX = 1e9f, maxX = -1e9f;
        size_t minIdx = 0, maxIdx = 0;
        size_t closestHorizIdx = 0;
        float closestHorizDist = 1e9f;

        for (size_t i = 0; i < pageText.chars.size(); ++i) {
            const auto& c = pageText.chars[i];
            if (c.rect.right <= c.rect.left) continue;
            float midY = (c.rect.top + c.rect.bottom) * 0.5f;
            if (std::abs(midY - bestLineY) <= 6.0f) {
                if (c.rect.left < minX) { minX = c.rect.left; minIdx = i; }
                if (c.rect.right > maxX) { maxX = c.rect.right; maxIdx = i; }

                float midX = (c.rect.left + c.rect.right) * 0.5f;
                float hDist = std::abs(pdfX - midX);
                if (hDist < closestHorizDist) {
                    closestHorizDist = hDist;
                    closestHorizIdx = i;
                }
            }
        }

        if (maxX >= minX) {
            if (pdfX <= minX) {
                outCharIndex = minIdx;
                outAfterChar = false;
                return true;
            } else if (pdfX >= maxX) {
                outCharIndex = maxIdx;
                outAfterChar = true;
                return true;
            } else {
                outCharIndex = closestHorizIdx;
                const auto& c = pageText.chars[closestHorizIdx];
                outAfterChar = (pdfX > (c.rect.left + c.rect.right) * 0.5f);
                return true;
            }
        }
    }

    return false;
}

std::shared_ptr<PdfPageText> SelectionController::GetOrExtractPageText(DocumentTab* pTab, uint32_t pageIndex) {
    if (!pTab || !pTab->document.IsLoaded() || pageIndex >= pTab->document.GetPageCount()) {
        return nullptr;
    }
    if (!pTab->textCache) {
        pTab->textCache = std::make_shared<PageTextCache>();
        pTab->textCache->pages.resize(pTab->document.GetPageCount());
    }
    {
        std::lock_guard<std::mutex> lock(pTab->textCache->mutex);
        if (pageIndex < pTab->textCache->pages.size() && pTab->textCache->pages[pageIndex]) {
            return pTab->textCache->pages[pageIndex];
        }
    }

    if (!pTab->parser) {
        pTab->parser = std::make_unique<PdfParser>();
        pTab->parser->Load(pTab->document.GetFilePath());
    }

    PdfPageText pageText;
    if (pTab->parser->ExtractPageText(pageIndex, pageText)) {
        auto sharedPage = std::make_shared<PdfPageText>(std::move(pageText));
        std::lock_guard<std::mutex> lock(pTab->textCache->mutex);
        if (pageIndex < pTab->textCache->pages.size()) {
            pTab->textCache->pages[pageIndex] = sharedPage;
        }
        return sharedPage;
    }
    return nullptr;
}

bool SelectionController::HitTestPageText(
    DocumentTab* pTab,
    POINT clientPt,
    float winWidth,
    float /*winHeight*/,
    float dpi,
    float topOffset,
    uint32_t& outPage,
    size_t& outCharIndex,
    bool& outAfterChar
) {
    if (!pTab || !pTab->document.IsLoaded() || pTab->document.GetPageCount() == 0) return false;

    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipX = (float)clientPt.x * dipScale;
    float dipY = (float)clientPt.y * dipScale;
    if (dipY < topOffset) return false;

    if (pTab->continuousScroll) {
        float mouseY = dipY - topOffset;
        float docY = pTab->scrollY + mouseY;
        uint32_t count = pTab->document.GetPageCount();
        const auto& offsets = pTab->pageOffsets;

        auto it = std::upper_bound(offsets.begin(), offsets.end(), docY);
        uint32_t pageIdx = 0;
        if (it != offsets.begin()) {
            pageIdx = static_cast<uint32_t>(std::distance(offsets.begin(), it) - 1);
        }
        if (pageIdx >= count) return false;

        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pageIdx);
        float pageW = pSize.width * pTab->zoom;
        float pageH = pSize.height * pTab->zoom;
        float dipW = winWidth * dipScale;
        float margin = 24.0f;
        float pageX = (pageW <= dipW - margin * 2.0f) ? (dipW - pageW) * 0.5f + pTab->offsetX : margin + pTab->offsetX;
        float pageTopY = offsets[pageIdx] - pTab->scrollY;

        if (dipX < pageX - 20.0f || dipX > pageX + pageW + 20.0f) return false;
        if (mouseY < pageTopY - 10.0f || mouseY > pageTopY + pageH + 10.0f) return false;

        float pdfX = (dipX - pageX) / pTab->zoom;
        float pdfY = (mouseY - pageTopY) / pTab->zoom;
        pdfX = std::clamp(pdfX, 0.0f, pSize.width);
        pdfY = std::clamp(pdfY, 0.0f, pSize.height);

        auto pageText = GetOrExtractPageText(pTab, pageIdx);
        if (!pageText || pageText->chars.empty()) return false;

        outPage = pageIdx;
        return HitTestCharInPage(*pageText, pdfX, pdfY, outCharIndex, outAfterChar);
    } else {
        uint32_t pageIdx = pTab->currentPage;
        if (pageIdx >= pTab->document.GetPageCount()) return false;

        D2D1_SIZE_F pSize = pTab->document.GetPageSize(pageIdx);
        float pageW = pSize.width * pTab->zoom;
        float pageH = pSize.height * pTab->zoom;
        float pageX = pTab->offsetX;
        float pageY = pTab->offsetY + topOffset;

        if (dipX < pageX - 20.0f || dipX > pageX + pageW + 20.0f) return false;
        if (dipY < pageY - 10.0f || dipY > pageY + pageH + 10.0f) return false;

        float pdfX = (dipX - pageX) / pTab->zoom;
        float pdfY = (dipY - pageY) / pTab->zoom;
        pdfX = std::clamp(pdfX, 0.0f, pSize.width);
        pdfY = std::clamp(pdfY, 0.0f, pSize.height);

        auto pageText = GetOrExtractPageText(pTab, pageIdx);
        if (!pageText || pageText->chars.empty()) return false;

        outPage = pageIdx;
        return HitTestCharInPage(*pageText, pdfX, pdfY, outCharIndex, outAfterChar);
    }
}

std::vector<SelectionHighlightSpan> SelectionController::GetSelectionSpans(DocumentTab* pTab) {
    std::vector<SelectionHighlightSpan> spans;
    if (!pTab || !pTab->selection.HasSelection()) return spans;

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = GetOrExtractPageText(pTab, p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        SelectionHighlightSpan span;
        span.pageIndex = p;

        D2D1_RECT_F curBand = { 0, 0, 0, 0 };
        bool hasBand = false;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.rect.right <= ch.rect.left || ch.rect.bottom <= ch.rect.top) {
                continue;
            }

            if (!hasBand) {
                curBand = ch.rect;
                hasBand = true;
            } else {
                bool sameLine = (std::abs(ch.rect.top - curBand.top) < 6.0f) &&
                                (std::abs(ch.rect.bottom - curBand.bottom) < 6.0f);
                bool adjacent = (ch.rect.left <= curBand.right + 12.0f);

                if (sameLine && adjacent) {
                    curBand.left = (std::min)(curBand.left, ch.rect.left);
                    curBand.right = (std::max)(curBand.right, ch.rect.right);
                    curBand.top = (std::min)(curBand.top, ch.rect.top);
                    curBand.bottom = (std::max)(curBand.bottom, ch.rect.bottom);
                } else {
                    span.rects.push_back(curBand);
                    curBand = ch.rect;
                }
            }
        }
        if (hasBand) {
            span.rects.push_back(curBand);
        }

        if (!span.rects.empty()) {
            spans.push_back(std::move(span));
        }
    }
    return spans;
}

bool SelectionController::CopySelectionToClipboard(HWND hwnd, DocumentTab* pTab) {
    if (!pTab || !pTab->selection.HasSelection()) return false;

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    std::wstring result;
    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = GetOrExtractPageText(pTab, p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        if (p > startPage && !result.empty()) {
            result += L"\r\n\r\n";
        }

        float lastY = -1.0f;
        float lastRight = -1.0f;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.ch == 0) continue;

            if (lastY >= 0.0f) {
                if (std::abs(ch.rect.top - lastY) > 8.0f) {
                    result += L"\r\n";
                    lastRight = -1.0f;
                } else if (lastRight >= 0.0f && (ch.rect.left - lastRight) > 6.0f) {
                    if (!result.empty() && result.back() != L' ') {
                        result += L' ';
                    }
                }
            }

            result += ch.ch;
            lastY = ch.rect.top;
            if (ch.rect.right > ch.rect.left) {
                lastRight = ch.rect.right;
            }
        }
    }

    if (result.empty()) return false;

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (result.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, result.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
        return true;
    }
    return false;
}

std::wstring SelectionController::GetSelectedWordOrText(
    DocumentTab* pTab,
    float dpi,
    float topOffset,
    D2D1_RECT_F& outAnchorRect
) {
    outAnchorRect = { 0, 0, 0, 0 };
    if (!pTab || !pTab->document.IsLoaded() || !pTab->selection.HasSelection()) {
        return L"";
    }

    uint32_t startPage = 0, endPage = 0;
    size_t startIdx = 0, endIdx = 0;
    pTab->selection.GetOrderedRange(startPage, startIdx, endPage, endIdx);

    std::wstring result;
    D2D1_RECT_F pageBounds = { 1e9f, 1e9f, -1e9f, -1e9f };
    bool hasBounds = false;
    uint32_t primaryPage = startPage;

    for (uint32_t p = startPage; p <= endPage; ++p) {
        auto pageText = GetOrExtractPageText(pTab, p);
        if (!pageText || pageText->chars.empty()) continue;

        size_t pStart = (p == startPage) ? startIdx : 0;
        size_t pEnd = (p == endPage) ? endIdx : pageText->chars.size();
        if (pStart >= pageText->chars.size()) continue;
        if (pEnd > pageText->chars.size()) pEnd = pageText->chars.size();
        if (pStart >= pEnd) continue;

        for (size_t i = pStart; i < pEnd; ++i) {
            const auto& ch = pageText->chars[i];
            if (ch.ch != 0) {
                if (!result.empty() && ch.rect.left > pageBounds.right + 4.0f && result.back() != L' ') {
                    result += L' ';
                }
                result += ch.ch;
            }
            if (ch.rect.right > ch.rect.left && ch.rect.bottom > ch.rect.top) {
                pageBounds.left = (std::min)(pageBounds.left, ch.rect.left);
                pageBounds.top = (std::min)(pageBounds.top, ch.rect.top);
                pageBounds.right = (std::max)(pageBounds.right, ch.rect.right);
                pageBounds.bottom = (std::max)(pageBounds.bottom, ch.rect.bottom);
                hasBounds = true;
            }
        }
    }

    if (result.empty() || !hasBounds) return L"";

    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);

    if (pTab->continuousScroll) {
        if (primaryPage < pTab->pageOffsets.size()) {
            D2D1_SIZE_F pSize = pTab->document.GetPageSize(primaryPage);
            float pageW = pSize.width * pTab->zoom;
            float dipW = (float)GetSystemMetrics(SM_CXSCREEN) * dipScale;
            float margin = 24.0f;
            float pageX = (pageW <= dipW - margin * 2.0f) ? (dipW - pageW) * 0.5f + pTab->offsetX : margin + pTab->offsetX;
            float pageTopY = pTab->pageOffsets[primaryPage] - pTab->scrollY + topOffset;

            outAnchorRect.left = pageX + pageBounds.left * pTab->zoom;
            outAnchorRect.top = pageTopY + pageBounds.top * pTab->zoom;
            outAnchorRect.right = pageX + pageBounds.right * pTab->zoom;
            outAnchorRect.bottom = pageTopY + pageBounds.bottom * pTab->zoom;
        }
    } else {
        float pageX = pTab->offsetX;
        float pageY = pTab->offsetY + topOffset;
        outAnchorRect.left = pageX + pageBounds.left * pTab->zoom;
        outAnchorRect.top = pageY + pageBounds.top * pTab->zoom;
        outAnchorRect.right = pageX + pageBounds.right * pTab->zoom;
        outAnchorRect.bottom = pageY + pageBounds.bottom * pTab->zoom;
    }

    return result;
}

bool SelectionController::TriggerDictionaryLookup(
    const std::wstring& query,
    const D2D1_RECT_F& anchorRect,
    float winWidth,
    float winHeight,
    float dpi,
    float topOffset
) {
    if (query.empty()) return false;

    if (!m_dictEngine.IsLoaded()) {
        m_dictEngine.Initialize();
    }

    if (!m_dictEngine.IsLoaded()) {
        return false;
    }

    DictionaryResult res;
    if (m_dictEngine.Lookup(query, res)) {
        m_dictCardInfo.visible = true;
        m_dictCardInfo.anchorRect = anchorRect;
        m_dictCardInfo.word = res.word;
        m_dictCardInfo.definition = res.definition;
        m_dictCardInfo.categoryTag = res.GetCategoryName();
        m_dictCardInfo.category = (uint16_t)res.category;

        float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
        float dipWidth = winWidth * dipScale;
        float dipHeight = winHeight * dipScale;

        float approxDefHeight = (float)(res.definition.length() / 25 + 1) * 22.0f;
        approxDefHeight = (std::max)(32.0f, approxDefHeight);
        m_dictCardBounds = DictionaryCardLayout::CalculateCardRect(
            anchorRect, approxDefHeight, dipWidth, dipHeight, topOffset
        );
        return true;
    }
    return false;
}

void SelectionController::DismissDictionaryCard() {
    m_dictCardInfo.visible = false;
}

bool SelectionController::HitTestDictionaryCard(POINT pt, float dpi) const {
    if (!m_dictCardInfo.visible) return false;
    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float x = (float)pt.x * dipScale;
    float y = (float)pt.y * dipScale;
    return (x >= m_dictCardBounds.left && x <= m_dictCardBounds.right &&
            y >= m_dictCardBounds.top && y <= m_dictCardBounds.bottom);
}

bool SelectionController::CopyDictionaryDefinitionToClipboard(HWND hwnd) {
    if (!m_dictCardInfo.visible || m_dictCardInfo.definition.empty()) return false;
    std::wstring text = m_dictCardInfo.word + L" \x2014 " + m_dictCardInfo.definition;

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
        return true;
    }
    return false;
}

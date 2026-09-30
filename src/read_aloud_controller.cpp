/**
 * @file read_aloud_controller.cpp
 * @brief Implementation of ReadAloudController for fluent, bilingual text-to-speech reading.
 */

#include "read_aloud_controller.hpp"
#include "app_window.hpp"
#include <algorithm>
#include <cctype>

namespace {
    inline bool IsSentenceDelimiter(wchar_t ch) {
        return ch == L'.' || ch == L'!' || ch == L'?' || ch == L'\n' || ch == L'\r' ||
               ch == 0x060C || // Arabic comma (،)
               ch == 0x061B || // Arabic semicolon (؛)
               ch == 0x061F || // Arabic question mark (؟)
               ch == 0x3002;   // CJK full stop (。)
    }

    inline bool IsIgnoredChar(wchar_t ch) {
        return ch == 0x200E || // Left-to-Right Mark (LRM)
               ch == 0x200F || // Right-to-Left Mark (RLM)
               ch == 0x200B || // Zero Width Space (ZWSP)
               ch == 0x00AD || // Soft Hyphen
               ch == 0xFEFF;   // Byte Order Mark (BOM)
    }

    inline bool IsArabicChar(wchar_t ch) {
        return (ch >= 0x0600 && ch <= 0x06FF) ||
               (ch >= 0x0750 && ch <= 0x077F) ||
               (ch >= 0x08A0 && ch <= 0x08FF) ||
               (ch >= 0xFB50 && ch <= 0xFDFF) ||
               (ch >= 0xFE70 && ch <= 0xFEFF);
    }

    inline bool IsLatinChar(wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= 0x00C0 && ch <= 0x024F);
    }
}

bool ReadAloudController::Init(HWND hwnd) {
    return m_engine.Init(hwnd, WM_APP_TTS_EVENT);
}

void ReadAloudController::Start(
    const std::wstring& selectedText,
    std::shared_ptr<PdfPageText> pageText,
    uint32_t pageIndex,
    size_t selStartChar,
    size_t selCharCount
) {
    m_currentPage = pageIndex;
    m_pageText = pageText;
    m_chunkIndex = 0;
    m_chunks.clear();
    m_needsNextPage = false;
    m_hasActiveHighlight = false;

    if (!selectedText.empty() && pageText) {
        m_readingSelectionOnly = true;
        size_t count = (selCharCount > 0) ? selCharCount : selectedText.size();
        BuildChunks(pageText->fullText, selStartChar, count);
    } else if (pageText && !pageText->fullText.empty()) {
        m_readingSelectionOnly = false;
        BuildChunks(pageText->fullText, 0, pageText->fullText.size());
    }

    if (!m_chunks.empty()) {
        m_active = true;
        SpeakCurrentChunk();
    } else {
        m_active = false;
        m_engine.Stop();
    }
}

void ReadAloudController::ContinueWithPage(std::shared_ptr<PdfPageText> pageText, uint32_t pageIndex) {
    m_currentPage = pageIndex;
    m_pageText = pageText;
    m_chunkIndex = 0;
    m_chunks.clear();
    m_needsNextPage = false;
    m_hasActiveHighlight = false;

    if (pageText && !pageText->fullText.empty()) {
        BuildChunks(pageText->fullText, 0, pageText->fullText.size());
        if (!m_chunks.empty()) {
            m_active = true;
            SpeakCurrentChunk();
            return;
        }
    }

    // If page has no readable text, trigger next page request
    m_needsNextPage = true;
}

void ReadAloudController::BuildChunks(const std::wstring& fullText, size_t baseOffset, size_t maxCount) {
    m_chunks.clear();
    if (fullText.empty() || baseOffset >= fullText.size()) return;

    size_t endOffset = (std::min)(fullText.size(), baseOffset + maxCount);
    size_t chunkStart = baseOffset;

    while (chunkStart < endOffset) {
        // Skip leading whitespace or newlines
        while (chunkStart < endOffset && iswspace(fullText[chunkStart])) {
            chunkStart++;
        }
        if (chunkStart >= endOffset) break;

        size_t scan = chunkStart;
        size_t lastSpace = chunkStart;
        size_t nonSpaceChars = 0;

        // Scan ahead looking for sentence boundary or 200-char limit
        while (scan < endOffset) {
            wchar_t ch = fullText[scan];

            if (iswspace(ch)) {
                lastSpace = scan;
            } else if (!IsIgnoredChar(ch)) {
                nonSpaceChars++;
            }

            if (IsSentenceDelimiter(ch)) {
                scan++; // include delimiter in current chunk
                break;
            }

            // Fallback split: If chunk reaches ~200 characters without punctuation, split at word boundary
            if (nonSpaceChars >= 200) {
                if (lastSpace > chunkStart) {
                    scan = lastSpace + 1;
                }
                break;
            }

            scan++;
        }

        // Construct cleaned TtsChunk and character mapping
        TtsChunk chunk;
        chunk.baseCharIndex = chunkStart;
        chunk.text.reserve(scan - chunkStart);
        chunk.charMap.reserve(scan - chunkStart);

        size_t arabicCount = 0;
        size_t latinCount = 0;

        for (size_t i = chunkStart; i < scan; ++i) {
            wchar_t ch = fullText[i];
            if (IsIgnoredChar(ch)) {
                continue; // skip formatting marks in speech text
            }

            if (IsArabicChar(ch)) arabicCount++;
            else if (IsLatinChar(ch)) latinCount++;

            chunk.charMap.push_back(i);
            chunk.text.push_back(ch);
        }

        // Trim trailing whitespace from chunk text & map
        while (!chunk.text.empty() && iswspace(chunk.text.back())) {
            chunk.text.pop_back();
            chunk.charMap.pop_back();
        }

        if (!chunk.text.empty()) {
            chunk.isArabic = (arabicCount > 0 && arabicCount >= latinCount);
            m_chunks.push_back(std::move(chunk));
        }

        chunkStart = scan;
    }
}

void ReadAloudController::SpeakCurrentChunk() {
    if (m_chunkIndex >= m_chunks.size()) {
        m_hasActiveHighlight = false;
        if (!m_readingSelectionOnly) {
            m_needsNextPage = true;
        } else {
            Stop();
        }
        return;
    }

    const auto& chunk = m_chunks[m_chunkIndex];
    m_engine.SelectVoiceForLanguage(chunk.isArabic);
    m_engine.SpeakAsync(chunk.text);
}

void ReadAloudController::TogglePause() {
    if (!m_active) return;
    if (m_engine.IsPaused()) {
        m_engine.Resume();
    } else {
        m_engine.Pause();
    }
}

void ReadAloudController::Stop() {
    m_engine.Stop();
    m_active = false;
    m_needsNextPage = false;
    m_readingSelectionOnly = false;
    m_hasActiveHighlight = false;
    m_chunks.clear();
    m_chunkIndex = 0;
}

void ReadAloudController::SpeedUp() {
    m_engine.SetRate(m_engine.GetRate() + 1);
}

void ReadAloudController::SpeedDown() {
    m_engine.SetRate(m_engine.GetRate() - 1);
}

void ReadAloudController::SkipForward() {
    if (!m_active || m_chunks.empty()) return;
    if (m_chunkIndex + 1 < m_chunks.size()) {
        m_chunkIndex++;
        m_hasActiveHighlight = false;
        SpeakCurrentChunk();
    } else if (!m_readingSelectionOnly) {
        m_needsNextPage = true;
    }
}

void ReadAloudController::SkipBackward() {
    if (!m_active || m_chunks.empty()) return;
    if (m_chunkIndex > 0) {
        m_chunkIndex--;
        m_hasActiveHighlight = false;
        SpeakCurrentChunk();
    } else {
        // Re-read current chunk from beginning
        m_hasActiveHighlight = false;
        SpeakCurrentChunk();
    }
}

bool ReadAloudController::OnTtsEvent() {
    if (!m_active) return false;

    TtsEvent ev = m_engine.ProcessEvent();
    if (ev.type == TtsEvent::WordBoundary) {
        ComputeActiveWordRect(ev.charOffset, ev.charLength);
        return true;
    } else if (ev.type == TtsEvent::EndStream) {
        m_chunkIndex++;
        m_hasActiveHighlight = false;
        if (m_chunkIndex < m_chunks.size()) {
            SpeakCurrentChunk();
        } else {
            if (!m_readingSelectionOnly) {
                m_needsNextPage = true;
            } else {
                Stop();
            }
        }
        return true;
    }

    return false;
}

void ReadAloudController::ComputeActiveWordRect(size_t charOffset, size_t charLength) {
    if (m_chunkIndex >= m_chunks.size() || !m_pageText) {
        m_hasActiveHighlight = false;
        return;
    }

    const auto& chunk = m_chunks[m_chunkIndex];
    if (charOffset >= chunk.charMap.size()) {
        return;
    }

    size_t startChar = chunk.charMap[charOffset];
    size_t endOffset = (std::min)(charOffset + charLength, chunk.charMap.size());
    size_t endChar = (endOffset > charOffset) ? chunk.charMap[endOffset - 1] + 1 : startChar + 1;

    D2D1_RECT_F unionRect = { 0, 0, 0, 0 };
    bool first = true;

    for (size_t ci = startChar; ci < (std::min)(endChar, m_pageText->chars.size()); ++ci) {
        const auto& cr = m_pageText->chars[ci].rect;
        if (cr.right > cr.left && cr.bottom > cr.top) {
            if (first) {
                unionRect = cr;
                first = false;
            } else {
                unionRect.left = (std::min)(unionRect.left, cr.left);
                unionRect.top = (std::min)(unionRect.top, cr.top);
                unionRect.right = (std::max)(unionRect.right, cr.right);
                unionRect.bottom = (std::max)(unionRect.bottom, cr.bottom);
            }
        }
    }

    if (!first) {
        m_activeWordRect = unionRect;
        m_hasActiveHighlight = true;
        m_highlightPage = m_currentPage;
    }
}

SelectionHighlightSpan ReadAloudController::GetActiveWordSpan() const {
    SelectionHighlightSpan span;
    if (m_active && m_hasActiveHighlight) {
        span.pageIndex = m_highlightPage;
        span.rects = { m_activeWordRect };
        span.isTtsHighlight = true;
    }
    return span;
}

TtsBarRenderInfo ReadAloudController::GetBarInfo() const {
    TtsBarRenderInfo info;
    info.visible = m_active;
    info.isPaused = m_engine.IsPaused();
    info.rateLabel = m_engine.GetRateLabel();
    info.voiceName = GetCurrentVoiceName();
    info.hoveredBtn = m_barHoveredBtn;
    return info;
}

std::wstring ReadAloudController::GetCurrentVoiceName() const {
    const auto& voices = m_engine.GetVoices();
    size_t idx = m_engine.GetCurrentVoiceIndex();
    if (idx < voices.size()) {
        const std::wstring& fullName = voices[idx].name;
        // Clean "Microsoft " prefix for compact bar display (e.g. "Microsoft Naayf" -> "Naayf")
        if (fullName.rfind(L"Microsoft ", 0) == 0) {
            return fullName.substr(10);
        }
        return fullName;
    }
    return L"Voice";
}

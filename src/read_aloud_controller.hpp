/**
 * @file read_aloud_controller.hpp
 * @brief Coordinates text chunking, language routing, word tracking, and page continuation for Read Aloud.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "tts_engine.hpp"
#include "pdf_parser.hpp"
#include "d2d_renderer.hpp"

/// @brief A single text chunk prepared for speech synthesis with character mapping.
struct TtsChunk {
    std::wstring text;                  ///< Cleaned text to speak via SAPI
    std::vector<size_t> charMap;        ///< Maps each character in text to index in PdfPageText::chars
    size_t baseCharIndex = 0;           ///< Starting offset in PdfPageText::chars
    bool isArabic = false;              ///< Dominant language of this chunk
};

/**
 * @class ReadAloudController
 * @brief High-level controller managing playback lifecycle, bilingual switching, and visual tracking.
 */
class ReadAloudController {
public:
    ReadAloudController() = default;
    ~ReadAloudController() = default;

    /**
     * @brief Initializes underlying TTS engine with window handle for notification messages.
     * @param hwnd Window to receive WM_APP_TTS_EVENT.
     * @return true if initialized successfully.
     */
    bool Init(HWND hwnd);

    /**
     * @brief Starts speech from selected text or from the top of the page.
     * @param selectedText Optional explicit text string (empty for full page).
     * @param pageText Digital page text containing character bounding boxes.
     * @param pageIndex 0-based index of active page.
     * @param selStartChar Starting character index for selection.
     * @param selCharCount Number of characters in selection.
     */
    void Start(
        const std::wstring& selectedText,
        std::shared_ptr<PdfPageText> pageText,
        uint32_t pageIndex,
        size_t selStartChar = 0,
        size_t selCharCount = 0
    );

    /**
     * @brief Feeds the next page text to continue continuous document reading.
     * @param pageText Digital page text for the new page.
     * @param pageIndex 0-based page index.
     */
    void ContinueWithPage(std::shared_ptr<PdfPageText> pageText, uint32_t pageIndex);

    /// @brief Toggles between pause and resume.
    void TogglePause();

    /// @brief Stops reading and dismisses visual highlighting.
    void Stop();

    /// @brief Increases speech rate by one step.
    void SpeedUp();

    /// @brief Decreases speech rate by one step.
    void SpeedDown();

    /// @brief Skips forward to the next sentence/chunk.
    void SkipForward();

    /// @brief Skips backward to the previous sentence/chunk.
    void SkipBackward();

    /// @brief Returns whether Read Aloud is currently active.
    bool IsActive() const { return m_active; }

    /// @brief Returns whether speech is currently paused.
    bool IsPaused() const { return m_engine.IsPaused(); }

    /// @brief Handles SAPI window notification. Returns true if UI redraw is required.
    bool OnTtsEvent();

    /// @brief Returns true if current page reading finished and next page text is required.
    bool NeedsNextPage() const { return m_needsNextPage; }

    /// @brief Returns the 0-based index of the page currently being read.
    uint32_t GetCurrentPage() const { return m_currentPage; }

    /// @brief Generates a SelectionHighlightSpan for the word currently being spoken.
    SelectionHighlightSpan GetActiveWordSpan() const;

    /// @brief Returns render info for drawing the floating playback bar.
    TtsBarRenderInfo GetBarInfo() const;

    /// @brief Sets the index of the currently hovered button on the playback bar.
    void SetBarHoveredBtn(int btn) { m_barHoveredBtn = btn; }

    /// @brief Returns the speed rate label (e.g. "1.0x", "1.2x").
    std::wstring GetRateLabel() const { return m_engine.GetRateLabel(); }

    /// @brief Returns the name of the currently selected voice.
    std::wstring GetCurrentVoiceName() const;

private:
    void BuildChunks(const std::wstring& fullText, size_t baseOffset, size_t maxCount);
    void SpeakCurrentChunk();
    void ComputeActiveWordRect(size_t charOffset, size_t charLength);

    TtsEngine m_engine;
    bool m_active = false;
    bool m_needsNextPage = false;
    bool m_readingSelectionOnly = false;

    std::vector<TtsChunk> m_chunks;
    size_t m_chunkIndex = 0;

    uint32_t m_currentPage = 0;
    std::shared_ptr<PdfPageText> m_pageText;

    // Visual word highlight geometry
    uint32_t m_highlightPage = 0;
    D2D1_RECT_F m_activeWordRect = { 0, 0, 0, 0 };
    bool m_hasActiveHighlight = false;

    int m_barHoveredBtn = -1;
};

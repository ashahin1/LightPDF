/**
 * @file selection_controller.hpp
 * @brief Text selection marquee management, clipboard extraction, and contextual dictionary lookup card.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "d2d_renderer.hpp"
#include "dictionary_engine.hpp"
#include "tab_controller.hpp"

/**
 * @class SelectionController
 * @brief Manages text selection hit-testing, clipboard copy, and offline dictionary card lookup.
 */
class SelectionController {
public:
    SelectionController() = default;
    ~SelectionController() = default;

    /// @brief Accesses underlying offline dictionary engine.
    DictionaryEngine& GetDictEngine() { return m_dictEngine; }

    /// @brief Returns render info descriptor for current dictionary card popup.
    const DictionaryCardRenderInfo& GetDictCardInfo() const { return m_dictCardInfo; }

    /// @brief Returns whether dictionary card popup is currently shown.
    bool IsDictCardVisible() const { return m_dictCardInfo.visible; }

    /**
     * @brief Lazy-extracts and caches digital text for a page.
     * @param pTab Target document tab.
     * @param pageIndex 0-based page index.
     * @return Shared pointer to page text structure.
     */
    std::shared_ptr<PdfPageText> GetOrExtractPageText(DocumentTab* pTab, uint32_t pageIndex);

    /**
     * @brief Hit-tests client mouse coordinates against glyph bounding boxes.
     * @param pTab Target document tab.
     * @param clientPt Mouse point in window client coordinates.
     * @param winWidth Client width in DIPs.
     * @param winHeight Client height in DIPs.
     * @param dpi Monitor DPI scale.
     * @param topOffset Tab strip height offset.
     * @param outPage Page hit.
     * @param outCharIndex Character index within page hit.
     * @param outAfterChar Set to true if point is on the trailing half of the glyph.
     * @return true if a text character was hit.
     */
    bool HitTestPageText(
        DocumentTab* pTab,
        POINT clientPt,
        float winWidth,
        float winHeight,
        float dpi,
        float topOffset,
        uint32_t& outPage,
        size_t& outCharIndex,
        bool& outAfterChar
    );

    /// @brief Generates selection highlight bounding boxes for active selection range.
    std::vector<SelectionHighlightSpan> GetSelectionSpans(DocumentTab* pTab);

    /**
     * @brief Copies currently selected text range to the Windows clipboard.
     * @param hwnd Window handle for clipboard ownership.
     * @param pTab Target document tab.
     * @return true if copied successfully.
     */
    bool CopySelectionToClipboard(HWND hwnd, DocumentTab* pTab);

    /**
     * @brief Retrieves selected text string and computes screen anchor rectangle for dictionary card positioning.
     * @param pTab Target document tab.
     * @param dpi Monitor DPI.
     * @param topOffset Tab strip offset.
     * @param outAnchorRect Screen DIP bounding rectangle of the selected word.
     * @return Selected string.
     */
    std::wstring GetSelectedWordOrText(
        DocumentTab* pTab,
        float dpi,
        float topOffset,
        D2D1_RECT_F& outAnchorRect
    );

    /**
     * @brief Triggers offline dictionary lookup and positions popup card.
     * @param query Word or term to define.
     * @param anchorRect Screen bounding box of word to position card adjacent to.
     * @param winWidth Window client width in DIPs.
     * @param winHeight Window client height in DIPs.
     * @param dpi Monitor DPI scale.
     * @param topOffset Tab strip offset.
     * @return true if definition was found and card displayed.
     */
    bool TriggerDictionaryLookup(
        const std::wstring& query,
        const D2D1_RECT_F& anchorRect,
        float winWidth,
        float winHeight,
        float dpi,
        float topOffset
    );

    /// @brief Hides the dictionary card popup.
    void DismissDictionaryCard();

    /// @brief Tests if mouse point falls inside active dictionary card popup bounds.
    bool HitTestDictionaryCard(POINT pt, float dpi) const;

    /// @brief Copies current dictionary card translation to the clipboard.
    bool CopyDictionaryDefinitionToClipboard(HWND hwnd);

private:
    DictionaryEngine m_dictEngine;
    DictionaryCardRenderInfo m_dictCardInfo;
    D2D1_RECT_F m_dictCardBounds = { 0, 0, 0, 0 };
};


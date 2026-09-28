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

class SelectionController {
public:
    SelectionController() = default;
    ~SelectionController() = default;

    DictionaryEngine& GetDictEngine() { return m_dictEngine; }
    const DictionaryCardRenderInfo& GetDictCardInfo() const { return m_dictCardInfo; }
    bool IsDictCardVisible() const { return m_dictCardInfo.visible; }

    std::shared_ptr<PdfPageText> GetOrExtractPageText(DocumentTab* pTab, uint32_t pageIndex);
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

    std::vector<SelectionHighlightSpan> GetSelectionSpans(DocumentTab* pTab);
    bool CopySelectionToClipboard(HWND hwnd, DocumentTab* pTab);
    std::wstring GetSelectedWordOrText(
        DocumentTab* pTab,
        float dpi,
        float topOffset,
        D2D1_RECT_F& outAnchorRect
    );

    bool TriggerDictionaryLookup(
        const std::wstring& query,
        const D2D1_RECT_F& anchorRect,
        float winWidth,
        float winHeight,
        float dpi,
        float topOffset
    );
    void DismissDictionaryCard();
    bool HitTestDictionaryCard(POINT pt, float dpi) const;
    bool CopyDictionaryDefinitionToClipboard(HWND hwnd);

private:
    DictionaryEngine m_dictEngine;
    DictionaryCardRenderInfo m_dictCardInfo;
    D2D1_RECT_F m_dictCardBounds = { 0, 0, 0, 0 };
};

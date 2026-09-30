#include "ui_views.hpp"
#include <algorithm>
#include <cmath>

namespace UIViews {

// =========================================================================
// 1. TabStrip View
// =========================================================================
void TabStripView::Render(
    ID2D1DeviceContext* ctx,
    const std::vector<TabRenderInfo>& tabs,
    bool isAddHovered,
    float dipWidth,
    const TabStripResources& res
) {
    if (tabs.size() <= 1 || !ctx) return;

    float barH = 34.0f;

    // 1. Tab Bar Background
    D2D1_RECT_F barRect = D2D1::RectF(0.0f, 0.0f, dipWidth, barH);
    ctx->FillRectangle(barRect, res.brushTabBarBg);

    // 2. Bottom Divider Line
    ctx->DrawLine(
        D2D1::Point2F(0.0f, barH),
        D2D1::Point2F(dipWidth, barH),
        res.brushTabBorder,
        1.0f
    );

    float availW = dipWidth - 44.0f;
    float tabW = std::clamp(availW / (float)tabs.size(), 100.0f, 220.0f);

    for (size_t i = 0; i < tabs.size(); ++i) {
        float tx = (float)i * tabW;
        D2D1_RECT_F tabRect = D2D1::RectF(tx, 3.0f, tx + tabW, barH);

        if (tabs[i].isActive) {
            ctx->FillRectangle(tabRect, res.brushTabActiveBg);

            // Top accent indicator bar
            D2D1_RECT_F accentRect = D2D1::RectF(tx, 1.0f, tx + tabW, 3.0f);
            ctx->FillRectangle(accentRect, res.brushTabAccent);

            // Subtle vertical borders
            ctx->DrawLine(D2D1::Point2F(tx, 3.0f), D2D1::Point2F(tx, barH), res.brushTabBorder, 1.0f);
            ctx->DrawLine(D2D1::Point2F(tx + tabW, 3.0f), D2D1::Point2F(tx + tabW, barH), res.brushTabBorder, 1.0f);
        } else {
            if (tabs[i].isHovered) {
                ctx->FillRectangle(tabRect, res.brushTabHoverBg);
            } else {
                ctx->FillRectangle(tabRect, res.brushTabInactiveBg);
            }
            // Vertical separator between inactive tabs
            ctx->DrawLine(D2D1::Point2F(tx + tabW, 9.0f), D2D1::Point2F(tx + tabW, barH - 9.0f), res.brushTabBorder, 1.0f);
        }

        // Tab Title Text
        D2D1_RECT_F textRect = D2D1::RectF(tx + 12.0f, 4.0f, tx + tabW - 28.0f, barH);
        ID2D1SolidColorBrush* textBrush = tabs[i].isActive ? res.brushTabText : res.brushTabTextInactive;
        const std::wstring& title = tabs[i].title.empty() ? L"Untitled" : tabs[i].title;
        ctx->DrawText(
            title.c_str(),
            (UINT32)title.length(),
            res.textFormatTab,
            textRect,
            textBrush
        );

        // Close Button '×' (U+00D7)
        D2D1_RECT_F closeRect = D2D1::RectF(tx + tabW - 24.0f, 9.0f, tx + tabW - 8.0f, 25.0f);
        if (tabs[i].isCloseHovered) {
            ctx->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 3.0f, 3.0f), res.brushTabCloseHover);
        }
        const wchar_t* closeStr = L"\x00D7";
        ctx->DrawText(
            closeStr,
            1,
            res.textFormatTabClose,
            closeRect,
            tabs[i].isCloseHovered ? res.brushTabText : (tabs[i].isActive ? res.brushTabText : res.brushTabTextInactive)
        );
    }

    // '+' Add Tab button
    float addX = (float)tabs.size() * tabW + 6.0f;
    D2D1_RECT_F addRect = D2D1::RectF(addX, 7.0f, addX + 22.0f, 27.0f);
    if (isAddHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(addRect, 4.0f, 4.0f), res.brushTabHoverBg);
    }
    const wchar_t* addStr = L"+";
    ctx->DrawText(
        addStr,
        1,
        res.textFormatTabAdd,
        addRect,
        isAddHovered ? res.brushTabText : res.brushTabTextInactive
    );
}

// =========================================================================
// 2. ScrollBar View
// =========================================================================
void ScrollBarView::Render(
    ID2D1DeviceContext* ctx,
    const ScrollbarRenderInfo& scrollbar,
    float dipWidth,
    const ScrollBarResources& res
) {
    if (!scrollbar.visible || scrollbar.alpha <= 0.001f || !ctx) return;

    float width = (scrollbar.isHovered || scrollbar.isDragging) ? 10.0f : 7.0f;
    float x = dipWidth - width - 4.0f;

    // 1. Draw Track
    D2D1_RECT_F trackRect = D2D1::RectF(x, scrollbar.trackY, x + width, scrollbar.trackY + scrollbar.trackH);
    D2D1_ROUNDED_RECT roundedTrack = D2D1::RoundedRect(trackRect, width * 0.5f, width * 0.5f);

    if (res.brushScrollbarTrack) {
        res.brushScrollbarTrack->SetOpacity(scrollbar.alpha * 0.08f);
        ctx->FillRoundedRectangle(roundedTrack, res.brushScrollbarTrack);
    }

    // 2. Draw Thumb
    D2D1_RECT_F thumbRect = D2D1::RectF(x, scrollbar.thumbY, x + width, scrollbar.thumbY + scrollbar.thumbH);
    D2D1_ROUNDED_RECT roundedThumb = D2D1::RoundedRect(thumbRect, width * 0.5f, width * 0.5f);

    ID2D1SolidColorBrush* pThumbBrush = (scrollbar.isDragging || scrollbar.isHovered)
        ? res.brushScrollbarThumbHover
        : res.brushScrollbarThumb;
    if (pThumbBrush) {
        pThumbBrush->SetOpacity(scrollbar.alpha * ((scrollbar.isDragging || scrollbar.isHovered) ? 0.70f : 0.40f));
        ctx->FillRoundedRectangle(roundedThumb, pThumbBrush);
    }

    // 3. Floating Tooltip while dragging: e.g. "Page 14 / 80"
    if (scrollbar.isDragging && scrollbar.totalPages > 0) {
        wchar_t tipText[64];
        swprintf_s(tipText, L"Page %u / %u", scrollbar.hoverPage + 1, scrollbar.totalPages);
        float tipW = 110.0f;
        float tipH = 26.0f;
        float tipX = x - tipW - 10.0f;
        float tipY = std::clamp(scrollbar.thumbY + (scrollbar.thumbH - tipH) * 0.5f, scrollbar.trackY, scrollbar.trackY + scrollbar.trackH - tipH);

        D2D1_RECT_F tipRect = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
        D2D1_ROUNDED_RECT roundedTip = D2D1::RoundedRect(tipRect, 6.0f, 6.0f);

        ctx->FillRoundedRectangle(roundedTip, res.brushHudBg);
        ctx->DrawRoundedRectangle(roundedTip, res.brushHudBorder, 1.0f);
        ctx->DrawText(
            tipText,
            (UINT32)wcslen(tipText),
            res.textFormatHud,
            tipRect,
            res.brushHudText
        );
    }
}

// =========================================================================
// 3. GoToPage Overlay View
// =========================================================================
void GoToPageOverlayView::Render(
    ID2D1DeviceContext* ctx,
    const std::wstring& buffer,
    uint32_t totalPages,
    float dipWidth,
    float dipHeight,
    const GoToPageOverlayResources& res
) {
    if (!ctx) return;

    // 1. Dim background
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    ctx->FillRectangle(backdropRect, res.brushHelpBackdrop);

    // 2. Centered Card
    float cardW = 320.0f;
    float cardH = 150.0f;
    float cardX = (dipWidth - cardW) * 0.5f;
    float cardY = (dipHeight - cardH) * 0.5f;

    D2D1_RECT_F cardRect = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(cardRect, 12.0f, 12.0f);

    // Drop shadow
    D2D1_RECT_F cardShadow = D2D1::RectF(cardX + 4.0f, cardY + 4.0f, cardX + cardW + 6.0f, cardY + cardH + 6.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), res.brushPageShadow);

    // Card background & cyan accent border
    ctx->FillRoundedRectangle(roundedCard, res.brushHelpCardBg);
    ctx->DrawRoundedRectangle(roundedCard, res.brushTabAccent, 1.5f);

    // Title: "Go to Page"
    D2D1_RECT_F titleRect = D2D1::RectF(cardX, cardY + 14.0f, cardX + cardW, cardY + 36.0f);
    const wchar_t* titleStr = L"Go to Page";
    ctx->DrawText(titleStr, (UINT32)wcslen(titleStr), res.textFormatHelpTitle, titleRect, res.brushHudText);

    // Input Box in center
    float boxW = 200.0f;
    float boxH = 42.0f;
    float boxX = cardX + (cardW - boxW) * 0.5f;
    float boxY = cardY + 46.0f;

    D2D1_RECT_F boxRect = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
    D2D1_ROUNDED_RECT roundedBox = D2D1::RoundedRect(boxRect, 6.0f, 6.0f);
    ctx->FillRoundedRectangle(roundedBox, res.brushGoToPageBox);
    ctx->DrawRoundedRectangle(roundedBox, res.brushTabBorder, 1.0f);

    // Display string: e.g. "42|  / 150"
    wchar_t displayText[64];
    if (buffer.empty()) {
        if (totalPages > 0) swprintf_s(displayText, L"|  / %u", totalPages);
        else swprintf_s(displayText, L"|");
    } else {
        if (totalPages > 0) swprintf_s(displayText, L"%s|  / %u", buffer.c_str(), totalPages);
        else swprintf_s(displayText, L"%s|", buffer.c_str());
    }

    ctx->DrawText(
        displayText,
        (UINT32)wcslen(displayText),
        res.textFormatGoToPageInput,
        boxRect,
        res.brushHudText
    );

    // Subtitle / Hint: "Press Enter to jump • Esc to cancel"
    D2D1_RECT_F subRect = D2D1::RectF(cardX, cardY + 104.0f, cardX + cardW, cardY + 130.0f);
    const wchar_t* subStr = L"Enter to jump  \x2022  Esc to cancel";
    ctx->DrawText(subStr, (UINT32)wcslen(subStr), res.textFormatHelpSub, subRect, res.brushHelpSubText);
}

// =========================================================================
// 4. SearchBar View
// =========================================================================
void SearchBarView::Render(
    ID2D1DeviceContext* ctx,
    IDWriteFactory* dwriteFactory,
    const SearchBarRenderInfo& searchBar,
    float dipWidth,
    const SearchBarResources& res,
    SearchBarCache& cache
) {
    if (!searchBar.visible || !ctx) return;

    D2D1_RECT_F barRect = SearchBarLayout::GetBarRect(dipWidth, searchBar.hasTabs);

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(barRect.left + 3.0f, barRect.top + 3.0f, barRect.right + 4.0f, barRect.bottom + 4.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 6.0f, 6.0f), res.brushPageShadow);

    // Search Bar Card Background & Border
    D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(barRect, 6.0f, 6.0f);
    ctx->FillRoundedRectangle(roundedBar, res.brushHudBg);
    ctx->DrawRoundedRectangle(roundedBar, res.brushHudBorder, 1.0f);

    // 1. Search Query Input Area
    D2D1_RECT_F inputRect = SearchBarLayout::GetInputRect(barRect);
    if (searchBar.query.empty()) {
        const wchar_t* placeholder = L"Find in document...";
        ctx->DrawText(
            placeholder,
            (UINT32)wcslen(placeholder),
            res.textFormatSearchInput,
            inputRect,
            res.brushTabTextInactive
        );
    } else {
        bool hasArabic = false;
        for (wchar_t ch : searchBar.query) {
            if ((ch >= 0x0600 && ch <= 0x06FF) || (ch >= 0xFB50 && ch <= 0xFEFF)) {
                hasArabic = true;
                break;
            }
        }

        if (hasArabic && dwriteFactory) {
            float boxW = inputRect.right - inputRect.left;
            float boxH = inputRect.bottom - inputRect.top;
            if (!cache.layout || cache.query != searchBar.query ||
                std::abs(cache.layoutW - boxW) > 1.0f || std::abs(cache.layoutH - boxH) > 1.0f) {
                cache.layout.Reset();
                cache.query = searchBar.query;
                cache.layoutW = boxW;
                cache.layoutH = boxH;
                cache.caretX = 0;
                if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                    searchBar.query.c_str(),
                    (UINT32)searchBar.query.length(),
                    res.textFormatSearchInput,
                    boxW,
                    boxH,
                    &cache.layout))) {
                    DWRITE_HIT_TEST_METRICS htm = {};
                    FLOAT caretY = 0;
                    cache.layout->HitTestTextPosition((UINT32)searchBar.query.length(), FALSE, &cache.caretX, &caretY, &htm);
                }
            }

            if (cache.layout) {
                ctx->DrawTextLayout(
                    D2D1::Point2F(inputRect.left, inputRect.top),
                    cache.layout.Get(),
                    res.brushHudText
                );
                float cx = inputRect.left + cache.caretX;
                if (cx >= inputRect.left && cx <= inputRect.right) {
                    ctx->DrawLine(
                        D2D1::Point2F(cx, inputRect.top + 3.0f),
                        D2D1::Point2F(cx, inputRect.bottom - 3.0f),
                        res.brushHudText,
                        1.5f
                    );
                }
            }
        } else {
            std::wstring queryWithCursor = searchBar.query + L"|";
            ctx->DrawText(
                queryWithCursor.c_str(),
                (UINT32)queryWithCursor.length(),
                res.textFormatSearchInput,
                inputRect,
                res.brushHudText
            );
        }
    }

    // 2. Match Count Badge
    D2D1_RECT_F badgeRect = SearchBarLayout::GetBadgeRect(barRect);
    if (searchBar.isSearching) {
        const wchar_t* searchingStr = L"Searching...";
        ctx->DrawText(
            searchingStr,
            (UINT32)wcslen(searchingStr),
            res.textFormatSearchBadge,
            badgeRect,
            res.brushHelpKeyText
        );
    } else if (!searchBar.query.empty() && !searchBar.isDebouncing) {
        wchar_t badgeText[64];
        if (searchBar.totalMatches == 0) {
            swprintf_s(badgeText, L"0 / 0");
            ctx->DrawText(
                badgeText,
                (UINT32)wcslen(badgeText),
                res.textFormatSearchBadge,
                badgeRect,
                res.brushTabTextInactive
            );
        } else {
            swprintf_s(badgeText, L"%u of %u", searchBar.activeMatch, searchBar.totalMatches);
            ctx->DrawText(
                badgeText,
                (UINT32)wcslen(badgeText),
                res.textFormatSearchBadge,
                badgeRect,
                res.brushHelpKeyText
            );
        }
    }

    // 3. Subtle Vertical Separator
    float sepX = barRect.left + 227.0f;
    ctx->DrawLine(
        D2D1::Point2F(sepX, barRect.top + 7.0f),
        D2D1::Point2F(sepX, barRect.bottom - 7.0f),
        res.brushHudBorder,
        1.0f
    );

    // 4. Previous Button (▲)
    D2D1_RECT_F prevRect = SearchBarLayout::GetPrevBtnRect(barRect);
    if (searchBar.isPrevHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(prevRect, 4.0f, 4.0f), res.brushSearchBtnBg);
    }
    const wchar_t* prevIcon = L"\x25B2";
    ctx->DrawText(
        prevIcon,
        1,
        res.textFormatSearchBtn,
        prevRect,
        searchBar.isPrevHovered ? res.brushHudText : res.brushTabTextInactive
    );

    // 5. Next Button (▼)
    D2D1_RECT_F nextRect = SearchBarLayout::GetNextBtnRect(barRect);
    if (searchBar.isNextHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(nextRect, 4.0f, 4.0f), res.brushSearchBtnBg);
    }
    const wchar_t* nextIcon = L"\x25BC";
    ctx->DrawText(
        nextIcon,
        1,
        res.textFormatSearchBtn,
        nextRect,
        searchBar.isNextHovered ? res.brushHudText : res.brushTabTextInactive
    );

    // 6. Match Case Button (Aa)
    D2D1_RECT_F caseRect = SearchBarLayout::GetCaseBtnRect(barRect);
    if (searchBar.matchCase) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), res.brushSearchBtnActive);
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), res.brushTabAccent, 1.0f);
    } else if (searchBar.isCaseHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(caseRect, 4.0f, 4.0f), res.brushSearchBtnBg);
    }
    const wchar_t* caseText = L"Aa";
    ctx->DrawText(
        caseText,
        2,
        res.textFormatSearchBtn,
        caseRect,
        searchBar.matchCase ? res.brushHelpKeyText : (searchBar.isCaseHovered ? res.brushHudText : res.brushTabTextInactive)
    );

    // 7. OCR Toggle Button (OCR)
    D2D1_RECT_F ocrRect = SearchBarLayout::GetOcrBtnRect(barRect);
    if (searchBar.ocrEnabled) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), res.brushSearchBtnActive);
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), res.brushTabAccent, 1.0f);
    } else if (searchBar.isOcrHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(ocrRect, 4.0f, 4.0f), res.brushSearchBtnBg);
    }
    const wchar_t* ocrText = L"OCR";
    ctx->DrawText(
        ocrText,
        3,
        res.textFormatSearchBtn,
        ocrRect,
        searchBar.ocrEnabled ? res.brushHelpKeyText : (searchBar.isOcrHovered ? res.brushHudText : res.brushTabTextInactive)
    );

    // 8. Close Button (✕)
    D2D1_RECT_F closeRect = SearchBarLayout::GetCloseBtnRect(barRect);
    if (searchBar.isCloseHovered) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), res.brushTabCloseHover);
    }
    const wchar_t* closeIcon = L"\x2715";
    ctx->DrawText(
        closeIcon,
        1,
        res.textFormatSearchBtn,
        closeRect,
        searchBar.isCloseHovered ? res.brushHudText : res.brushTabTextInactive
    );
}

// =========================================================================
// 5. HelpOverlay View
// =========================================================================
int HelpOverlayView::HitTest(POINT pt, UINT width, UINT height, float dpi) {
    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipWidth = (float)width * dipScale;
    float dipHeight = (float)height * dipScale;

    float px = (float)pt.x * dipScale;
    float py = (float)pt.y * dipScale;

    D2D1_RECT_F card = HelpOverlayLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F closeBtn = HelpOverlayLayout::GetCloseBtnRect(card);

    if (px >= closeBtn.left && px <= closeBtn.right && py >= closeBtn.top && py <= closeBtn.bottom) {
        return 100; // Close button
    }

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F tabRect = HelpOverlayLayout::GetCategoryTabRect(card, i);
        if (px >= tabRect.left && px <= tabRect.right && py >= tabRect.top && py <= tabRect.bottom) {
            return i; // Category tab 0..4
        }
    }

    if (px >= card.left && px <= card.right && py >= card.top && py <= card.bottom) {
        return 999; // Inside card body
    }

    return -1; // Outside card (backdrop)
}

void HelpOverlayView::Render(
    ID2D1DeviceContext* ctx,
    const HelpOverlayRenderInfo& help,
    float dipWidth,
    float dipHeight,
    const HelpOverlayResources& res
) {
    if (!ctx) return;

    // 1. Semi-transparent backdrop over entire window
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    ctx->FillRectangle(backdropRect, res.brushHelpBackdrop);

    // 2. Centered Help Card
    D2D1_RECT_F card = HelpOverlayLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(card, 12.0f, 12.0f);

    // Drop shadow
    D2D1_RECT_F cardShadow = D2D1::RectF(card.left + 6.0f, card.top + 6.0f, card.right + 8.0f, card.bottom + 8.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), res.brushPageShadow);

    // Card background & crisp border
    ctx->FillRoundedRectangle(roundedCard, res.brushHelpCardBg);
    ctx->DrawRoundedRectangle(roundedCard, res.brushHelpCardBorder, 1.5f);

    // 3. Header: Title and Close Button [×]
    D2D1_RECT_F titleRect = D2D1::RectF(card.left, card.top + 14.0f, card.right, card.top + 38.0f);
    const wchar_t* titleStr = L"Keyboard & Mouse Shortcuts";
    ctx->DrawText(titleStr, (UINT32)wcslen(titleStr), res.textFormatHelpTitle, titleRect, res.brushHudText);

    // Close Button [×]
    D2D1_RECT_F closeRect = HelpOverlayLayout::GetCloseBtnRect(card);
    if (help.hoveredClose == 1) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), res.brushTabCloseHover);
    }
    const wchar_t* closeGlyph = L"\x00D7";
    ctx->DrawText(
        closeGlyph,
        1,
        res.textFormatTabClose,
        closeRect,
        (help.hoveredClose == 1) ? res.brushHudText : res.brushHelpSubText
    );

    // 4. Category Tabs
    static const wchar_t* tabLabels[5] = {
        L"All (34)",
        L"Navigation (8)",
        L"Zoom & View (8)",
        L"Tabs & Files (8)",
        L"Search & Tools (10)"
    };

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F tabRect = HelpOverlayLayout::GetCategoryTabRect(card, i);
        D2D1_ROUNDED_RECT rTab = D2D1::RoundedRect(tabRect, 4.0f, 4.0f);

        if (help.activeCategory == i) {
            // Active tab pill
            ctx->FillRoundedRectangle(rTab, res.brushSearchBtnActive);
            ctx->DrawRoundedRectangle(rTab, res.brushTabAccent, 1.0f);
            ctx->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                res.textFormatSearchBtn,
                tabRect,
                res.brushHudText
            );
        } else if (help.hoveredCategory == i) {
            // Hovered inactive tab
            ctx->FillRoundedRectangle(rTab, res.brushTabHoverBg);
            ctx->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                res.textFormatSearchBtn,
                tabRect,
                res.brushHudText
            );
        } else {
            // Inactive tab
            ctx->DrawText(
                tabLabels[i],
                (UINT32)wcslen(tabLabels[i]),
                res.textFormatSearchBtn,
                tabRect,
                res.brushTabTextInactive
            );
        }
    }

    // Divider line under tabs
    ctx->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 78.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 78.0f),
        res.brushHelpCardBorder,
        1.0f
    );

    // Shortcuts Data Categorized
    struct ShortcutItem {
        const wchar_t* key;
        const wchar_t* desc;
    };

    static const ShortcutItem navItems[] = {
        { L"PgDn / Space",          L"Advance to next page" },
        { L"PgUp / Shift+Space",    L"Go to previous page" },
        { L"Right / Left Arrow",    L"Next / Previous page" },
        { L"Home / End",            L"Jump to first / last page" },
        { L"Mouse Wheel",           L"Scroll page vertically" },
        { L"Middle Drag / Space",   L"Smooth pan / drag document" },
        { L"H",                     L"Hand Tool (toggle drag pan)" },
        { L"V / S",                 L"Text Selection Tool" }
    };

    static const ShortcutItem zoomItems[] = {
        { L"Ctrl + Wheel / +/-",    L"Zoom in / out centered on cursor" },
        { L"Ctrl + 0",              L"Fit full page to window" },
        { L"Ctrl + 1",              L"Actual size (100% zoom)" },
        { L"Ctrl + 2",              L"Fit page width to window" },
        { L"Ctrl + 3",              L"Toggle continuous vertical scroll" },
        { L"Double Click",          L"Fit Page / Fit Width" },
        { L"F11",                   L"Toggle borderless fullscreen" },
        { L"Scrollbar Drag",        L"Scrub through document pages" }
    };

    static const ShortcutItem tabItems[] = {
        { L"Ctrl + O / Ctrl + T",   L"Open PDF document in new tab" },
        { L"Ctrl + S / Shift+S",    L"Save as Searchable PDF (Bake OCR)" },
        { L"Ctrl + W",              L"Close active tab" },
        { L"Ctrl + Tab",            L"Switch to next tab" },
        { L"Ctrl + Shift+Tab",      L"Switch to previous tab" },
        { L"Alt + 1..9",            L"Jump directly to tab 1 through 9" },
        { L"Middle Click Tab",      L"Close clicked tab" },
        { L"Drag & Drop",           L"Open dropped PDF files as tabs" }
    };

    static const ShortcutItem toolItems[] = {
        { L"Ctrl + F",              L"Find text in document (search)" },
        { L"F3 / Shift + F3",       L"Next / previous search match" },
        { L"Ctrl + R",              L"Read Aloud (toggle speech)" },
        { L"Ctrl + [ / ]",          L"Speech rate (slower / faster)" },
        { L"L / C",                 L"Laser pointer (toggle / color)" },
        { L"Ctrl + C",              L"Copy selected text to clipboard" },
        { L"D / Double-Click",      L"Offline English-Arabic Dictionary" },
        { L"Ctrl + P",              L"Print document (All / Range)" },
        { L"Ctrl + G",              L"Go to specific page prompt" },
        { L"Ctrl + D",              L"Document properties (Information)" }
    };

    if (help.activeCategory == 0) {
        // Mode 0: All shortcuts in balanced 2-column layout
        float col1Left = card.left + 24.0f;
        float divX = card.left + 424.0f;
        float col2Left = card.left + 438.0f;
        float keyColW = 168.0f;
        float gap = 10.0f;
        float descColW = 210.0f;
        float rowH = 19.5f;
        float headerH = 18.0f;

        // Vertical divider line between columns (anchored dynamically)
        ctx->DrawLine(
            D2D1::Point2F(divX, card.top + 84.0f),
            D2D1::Point2F(divX, card.bottom - 48.0f),
            res.brushTabBorder,
            1.0f
        );

        auto drawSection = [&](float colX, float startY, const wchar_t* title, const ShortcutItem* items, size_t count) -> float {
            D2D1_RECT_F headRect = D2D1::RectF(colX, startY, colX + 382.0f, startY + headerH);
            ctx->DrawText(title, (UINT32)wcslen(title), res.textFormatHelpSection, headRect, res.brushPropsAccent);

            float y = startY + headerH + 3.0f;
            for (size_t i = 0; i < count; ++i) {
                D2D1_RECT_F keyRect = D2D1::RectF(colX, y, colX + keyColW, y + rowH);
                D2D1_RECT_F descRect = D2D1::RectF(colX + keyColW + gap, y, colX + keyColW + gap + descColW, y + rowH);

                ctx->DrawText(items[i].key, (UINT32)wcslen(items[i].key), res.textFormatHelpColKey, keyRect, res.brushHelpKeyText);
                ctx->DrawText(items[i].desc, (UINT32)wcslen(items[i].desc), res.textFormatHelpColDesc, descRect, res.brushHelpDescText);
                y += rowH;
            }
            return y;
        };

        // Left Column: Navigation (8) + Tabs & Files (8)
        float curY1 = drawSection(col1Left, card.top + 86.0f, L"NAVIGATION", navItems, sizeof(navItems) / sizeof(navItems[0]));
        drawSection(col1Left, curY1 + 10.0f, L"TABS & FILES", tabItems, sizeof(tabItems) / sizeof(tabItems[0]));

        // Right Column: Zoom & View (8) + Search & Tools (10)
        float curY2 = drawSection(col2Left, card.top + 86.0f, L"ZOOM & VIEW", zoomItems, sizeof(zoomItems) / sizeof(zoomItems[0]));
        drawSection(col2Left, curY2 + 10.0f, L"SEARCH & TOOLS", toolItems, sizeof(toolItems) / sizeof(toolItems[0]));
    } else {
        // Modes 1..4: Single category view with spacious row badges
        const ShortcutItem* catItems = nullptr;
        size_t catCount = 0;

        switch (help.activeCategory) {
        case 1:
            catItems = navItems;
            catCount = sizeof(navItems) / sizeof(navItems[0]);
            break;
        case 2:
            catItems = zoomItems;
            catCount = sizeof(zoomItems) / sizeof(zoomItems[0]);
            break;
        case 3:
            catItems = tabItems;
            catCount = sizeof(tabItems) / sizeof(tabItems[0]);
            break;
        case 4:
            catItems = toolItems;
            catCount = sizeof(toolItems) / sizeof(toolItems[0]);
            break;
        }

        if (catItems && catCount > 0) {
            float startY = card.top + 90.0f;
            float availH = (card.bottom - 50.0f) - startY;
            float rowH = (std::min)(36.0f, availH / (float)catCount);
            float rowW = card.right - card.left - 48.0f;

            for (size_t i = 0; i < catCount; ++i) {
                float y = startY + (float)i * rowH;

                // Subtle alternating row background
                if (i % 2 == 1) {
                    D2D1_RECT_F altRect = D2D1::RectF(card.left + 24.0f, y, card.left + 24.0f + rowW, y + rowH - 4.0f);
                    ctx->FillRoundedRectangle(D2D1::RoundedRect(altRect, 4.0f, 4.0f), res.brushHelpRowAlt);
                }

                // Keycap badge
                float keycapX = card.left + 32.0f;
                float keycapW = 210.0f;
                float keycapH = 26.0f;
                float keycapY = y + (rowH - keycapH) * 0.5f;
                D2D1_RECT_F keycapRect = D2D1::RectF(keycapX, keycapY, keycapX + keycapW, keycapY + keycapH);
                D2D1_ROUNDED_RECT rKeycap = D2D1::RoundedRect(keycapRect, 4.0f, 4.0f);

                ctx->FillRoundedRectangle(rKeycap, res.brushHelpKeycapBg);
                ctx->DrawRoundedRectangle(rKeycap, res.brushHelpKeycapBorder, 1.0f);

                ctx->DrawText(
                    catItems[i].key,
                    (UINT32)wcslen(catItems[i].key),
                    res.textFormatHelpSingleKey,
                    keycapRect,
                    res.brushHelpKeyText
                );

                // Description
                float descX = keycapX + keycapW + 20.0f;
                float descW = card.right - 32.0f - descX;
                D2D1_RECT_F descRect = D2D1::RectF(descX, keycapY, descX + descW, keycapY + keycapH);

                ctx->DrawText(
                    catItems[i].desc,
                    (UINT32)wcslen(catItems[i].desc),
                    res.textFormatHelpSingleDesc,
                    descRect,
                    res.brushHelpDescText
                );
            }
        }
    }

    // 5. Footer: Divider line and Keyboard Hints (anchored dynamically to card bottom)
    float footerLineY = card.bottom - 44.0f;
    float footerTextY = card.bottom - 36.0f;

    ctx->DrawLine(
        D2D1::Point2F(card.left + 24.0f, footerLineY),
        D2D1::Point2F(card.right - 24.0f, footerLineY),
        res.brushHelpCardBorder,
        1.0f
    );

    D2D1_RECT_F footLeftRect = D2D1::RectF(card.left + 24.0f, footerTextY, card.left + 420.0f, footerTextY + 22.0f);
    const wchar_t* footLeftStr = L"Switch tabs: 1\x2013\x0035, Tab / Shift+Tab, or \x2190 \x2192";
    ctx->DrawText(footLeftStr, (UINT32)wcslen(footLeftStr), res.textFormatHelpFooterLeft, footLeftRect, res.brushHelpSubText);

    D2D1_RECT_F footRightRect = D2D1::RectF(card.right - 240.0f, footerTextY, card.right - 24.0f, footerTextY + 22.0f);
    const wchar_t* footRightStr = L"Press Esc or F1 to close";
    ctx->DrawText(footRightStr, (UINT32)wcslen(footRightStr), res.textFormatHelpFooterRight, footRightRect, res.brushHelpSubText);
}

// =========================================================================
// 6. DocProperties View
// =========================================================================
int DocPropertiesView::HitTest(POINT pt, UINT width, UINT height, float dpi) {
    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipWidth = (float)width * dipScale;
    float dipHeight = (float)height * dipScale;

    float px = (float)pt.x * dipScale;
    float py = (float)pt.y * dipScale;

    D2D1_RECT_F card = DocumentPropertiesLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F closeRect = DocumentPropertiesLayout::GetCloseBtnRect(card);
    D2D1_RECT_F copyRect = DocumentPropertiesLayout::GetCopyBtnRect(card);
    D2D1_RECT_F okRect = DocumentPropertiesLayout::GetOkBtnRect(card);

    if (px >= closeRect.left && px <= closeRect.right && py >= closeRect.top && py <= closeRect.bottom) {
        return 1; // Close button
    }
    if (px >= copyRect.left && px <= copyRect.right && py >= copyRect.top && py <= copyRect.bottom) {
        return 2; // Copy All button
    }
    if (px >= okRect.left && px <= okRect.right && py >= okRect.top && py <= okRect.bottom) {
        return 3; // OK button
    }

    if (px >= card.left && px <= card.right && py >= card.top && py <= card.bottom) {
        return 0; // Inside card body
    }

    return -1; // Backdrop click (outside card)
}

void DocPropertiesView::Render(
    ID2D1DeviceContext* ctx,
    const DocumentPropertiesRenderInfo& props,
    float dipWidth,
    float dipHeight,
    const DocPropertiesResources& res
) {
    if (!ctx) return;

    // 1. Semi-transparent backdrop
    D2D1_RECT_F backdropRect = D2D1::RectF(0.0f, 0.0f, dipWidth, dipHeight);
    ctx->FillRectangle(backdropRect, res.brushHelpBackdrop);

    // 2. Card container & drop shadow
    D2D1_RECT_F card = DocumentPropertiesLayout::GetCardRect(dipWidth, dipHeight);
    D2D1_RECT_F cardShadow = D2D1::RectF(card.left + 6.0f, card.top + 6.0f, card.right + 8.0f, card.bottom + 8.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(cardShadow, 12.0f, 12.0f), res.brushPageShadow);

    ctx->FillRoundedRectangle(D2D1::RoundedRect(card, 12.0f, 12.0f), res.brushHelpCardBg);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(card, 12.0f, 12.0f), res.brushHelpCardBorder, 1.5f);

    // 3. Header
    D2D1_RECT_F titleRect = D2D1::RectF(card.left + 24.0f, card.top + 16.0f, card.right - 50.0f, card.top + 42.0f);
    const wchar_t* titleText = L"Document Properties";
    ctx->DrawText(titleText, (UINT32)wcslen(titleText), res.textFormatHelpTitle, titleRect, res.brushHudText);

    // Close button [×]
    D2D1_RECT_F closeRect = DocumentPropertiesLayout::GetCloseBtnRect(card);
    if (props.hoveredBtn == 1) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(closeRect, 4.0f, 4.0f), res.brushPropsSecBtnHover);
    }
    const wchar_t* closeGlyph = L"\x00D7";
    ctx->DrawText(closeGlyph, 1, res.textFormatTabClose, closeRect, (props.hoveredBtn == 1) ? res.brushHudText : res.brushHelpSubText);

    // Header divider line
    ctx->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 48.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 48.0f),
        res.brushHelpCardBorder,
        1.0f
    );

    // 4. Section 1: Document Information
    D2D1_RECT_F sec1Rect = D2D1::RectF(card.left + 24.0f, card.top + 56.0f, card.right - 24.0f, card.top + 78.0f);
    const wchar_t* sec1Title = L"Document Information";
    ctx->DrawText(sec1Title, (UINT32)wcslen(sec1Title), res.textFormatPropsSection, sec1Rect, res.brushPropsAccent);

    struct FieldPair {
        const wchar_t* label;
        const std::wstring& value;
    };

    FieldPair sec1Fields[] = {
        { L"Title:",    props.title },
        { L"Author:",   props.author },
        { L"Subject:",  props.subject },
        { L"Keywords:", props.keywords },
        { L"Creator:",  props.creator },
        { L"Producer:", props.producer }
    };

    float y0 = card.top + 84.0f;
    float rowH = 28.0f;
    float labelW = 96.0f;
    float gap = 14.0f;

    for (int i = 0; i < 6; ++i) {
        float rowY = y0 + i * rowH;
        D2D1_RECT_F lRect = D2D1::RectF(card.left + 24.0f, rowY, card.left + 24.0f + labelW, rowY + rowH);
        D2D1_RECT_F vRect = D2D1::RectF(card.left + 24.0f + labelW + gap, rowY, card.right - 24.0f, rowY + rowH);

        ctx->DrawText(sec1Fields[i].label, (UINT32)wcslen(sec1Fields[i].label), res.textFormatPropsLabel, lRect, res.brushHelpSubText);
        
        ID2D1SolidColorBrush* valBrush = (sec1Fields[i].value == L"—") ? res.brushHelpSubText : res.brushHudText;
        ctx->DrawText(sec1Fields[i].value.c_str(), (UINT32)sec1Fields[i].value.size(), res.textFormatTab, vRect, valBrush);
    }

    // Divider between sections
    ctx->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 262.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 262.0f),
        res.brushHelpCardBorder,
        1.0f
    );

    // 5. Section 2: File & Page Details
    D2D1_RECT_F sec2Rect = D2D1::RectF(card.left + 24.0f, card.top + 270.0f, card.right - 24.0f, card.top + 292.0f);
    const wchar_t* sec2Title = L"File & Page Details";
    ctx->DrawText(sec2Title, (UINT32)wcslen(sec2Title), res.textFormatPropsSection, sec2Rect, res.brushPropsAccent);

    FieldPair sec2Fields[] = {
        { L"Total Pages:", props.totalPages },
        { L"File Size:",   props.fileSize },
        { L"PDF Format:",  props.pdfFormat },
        { L"Page Size:",   props.pageSize },
        { L"Created:",     props.created },
        { L"Modified:",    props.modified }
    };

    float y1 = card.top + 298.0f;
    for (int i = 0; i < 6; ++i) {
        float rowY = y1 + i * rowH;
        D2D1_RECT_F lRect = D2D1::RectF(card.left + 24.0f, rowY, card.left + 24.0f + labelW, rowY + rowH);
        D2D1_RECT_F vRect = D2D1::RectF(card.left + 24.0f + labelW + gap, rowY, card.right - 24.0f, rowY + rowH);

        ctx->DrawText(sec2Fields[i].label, (UINT32)wcslen(sec2Fields[i].label), res.textFormatPropsLabel, lRect, res.brushHelpSubText);

        ID2D1SolidColorBrush* valBrush = (sec2Fields[i].value == L"—") ? res.brushHelpSubText : res.brushHudText;
        ctx->DrawText(sec2Fields[i].value.c_str(), (UINT32)sec2Fields[i].value.size(), res.textFormatTab, vRect, valBrush);
    }

    // Footer divider line
    ctx->DrawLine(
        D2D1::Point2F(card.left + 24.0f, card.top + 480.0f),
        D2D1::Point2F(card.right - 24.0f, card.top + 480.0f),
        res.brushHelpCardBorder,
        1.0f
    );

    // 6. Buttons
    D2D1_RECT_F copyRect = DocumentPropertiesLayout::GetCopyBtnRect(card);
    D2D1_RECT_F okRect = DocumentPropertiesLayout::GetOkBtnRect(card);

    // Copy All button
    ID2D1SolidColorBrush* copyBg = (props.hoveredBtn == 2) ? res.brushPropsSecBtnHover : res.brushPropsSecBtn;
    ctx->FillRoundedRectangle(D2D1::RoundedRect(copyRect, 6.0f, 6.0f), copyBg);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(copyRect, 6.0f, 6.0f), res.brushHelpCardBorder, 1.0f);

    if (props.copyFeedback) {
        const wchar_t* copiedText = L"Copied!";
        ctx->DrawText(copiedText, (UINT32)wcslen(copiedText), res.textFormatTabClose, copyRect, res.brushPropsSuccess);
    } else {
        const wchar_t* copyText = L"Copy All";
        ctx->DrawText(copyText, (UINT32)wcslen(copyText), res.textFormatTabClose, copyRect, res.brushHudText);
    }

    // OK button
    ID2D1SolidColorBrush* okBg = (props.hoveredBtn == 3) ? res.brushPropsBtnHover : res.brushPropsBtn;
    ctx->FillRoundedRectangle(D2D1::RoundedRect(okRect, 6.0f, 6.0f), okBg);

    const wchar_t* okText = L"OK";
    ctx->DrawText(okText, (UINT32)wcslen(okText), res.textFormatTabClose, okRect, res.brushPageBg);
}

// =========================================================================
// 7. DictionaryCard View
// =========================================================================
void DictionaryCardView::Render(
    ID2D1DeviceContext* ctx,
    IDWriteFactory* dwriteFactory,
    const DictionaryCardRenderInfo& dictCard,
    float dipWidth,
    float dipHeight,
    float topOffset,
    const DictionaryCardResources& res
) {
    if (!ctx || !dictCard.visible) return;

    float maxDefWidth = DictionaryCardLayout::WIDTH - 32.0f;
    float defHeight = 36.0f;

    // Measure definition text layout height dynamically
    Microsoft::WRL::ComPtr<IDWriteTextLayout> defLayout;
    if (dwriteFactory && !dictCard.definition.empty()) {
        HRESULT hr = dwriteFactory->CreateTextLayout(
            dictCard.definition.c_str(),
            (UINT32)dictCard.definition.length(),
            res.textFormatDictDef,
            maxDefWidth,
            1000.0f,
            &defLayout
        );
        if (SUCCEEDED(hr) && defLayout) {
            // Check for Arabic characters to set BiDi RTL reading direction
            bool hasArabic = false;
            for (wchar_t ch : dictCard.definition) {
                if (ch >= 0x0600 && ch <= 0x06FF) {
                    hasArabic = true;
                    break;
                }
            }
            if (hasArabic) {
                defLayout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
                defLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            }

            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(defLayout->GetMetrics(&tm))) {
                defHeight = (std::max)(28.0f, tm.height);
            }
        }
    }

    // Position the card container
    D2D1_RECT_F card = DictionaryCardLayout::CalculateCardRect(
        dictCard.anchorRect, defHeight, dipWidth, dipHeight, topOffset
    );

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(card.left + 4.0f, card.top + 4.0f, card.right + 6.0f, card.bottom + 6.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 8.0f, 8.0f), res.brushPageShadow);

    // Card background & crisp border
    ctx->FillRoundedRectangle(D2D1::RoundedRect(card, 8.0f, 8.0f), res.brushDictCardBg);
    ctx->DrawRoundedRectangle(D2D1::RoundedRect(card, 8.0f, 8.0f), res.brushDictCardBorder, 1.5f);

    // 1. Header: Word Title
    float badgeWidth = 0.0f;
    if (!dictCard.categoryTag.empty()) {
        badgeWidth = (float)(dictCard.categoryTag.length() * 7 + 18);
        badgeWidth = std::clamp(badgeWidth, 80.0f, 180.0f);
    }
    float wordRight = card.right - badgeWidth - 20.0f;
    D2D1_RECT_F wordRect = D2D1::RectF(card.left + 16.0f, card.top + 10.0f, (std::max)(card.left + 20.0f, wordRight), card.top + 34.0f);
    ctx->DrawText(
        dictCard.word.c_str(),
        (UINT32)dictCard.word.length(),
        res.textFormatDictWord,
        wordRect,
        res.brushHudText
    );

    // 2. Category Badge Pill
    if (!dictCard.categoryTag.empty() && res.brushDictTagBg && res.brushDictTagText) {
        D2D1_COLOR_F textColor;
        D2D1_COLOR_F bgColor;
        switch (dictCard.category) {
        case 1: // Architecture: Amber/Orange
            textColor = D2D1::ColorF(0.98f, 0.65f, 0.20f, 1.0f);
            bgColor = D2D1::ColorF(0.98f, 0.65f, 0.20f, 0.18f);
            break;
        case 2: // Networks/IoT/WSN: Cyan/Blue
            textColor = D2D1::ColorF(0.25f, 0.75f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.25f, 0.75f, 0.98f, 0.18f);
            break;
        case 3: // AI/Vision: Purple/Violet
            textColor = D2D1::ColorF(0.75f, 0.45f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.75f, 0.45f, 0.98f, 0.18f);
            break;
        case 4: // Cybersecurity: Coral/Red
            textColor = D2D1::ColorF(0.98f, 0.40f, 0.40f, 1.0f);
            bgColor = D2D1::ColorF(0.98f, 0.40f, 0.40f, 0.18f);
            break;
        case 5: // Academic/ABET/NCAAA: Emerald
            textColor = D2D1::ColorF(0.25f, 0.85f, 0.50f, 1.0f);
            bgColor = D2D1::ColorF(0.25f, 0.85f, 0.50f, 0.18f);
            break;
        case 6: // QA: Gold
            textColor = D2D1::ColorF(0.96f, 0.82f, 0.25f, 1.0f);
            bgColor = D2D1::ColorF(0.96f, 0.82f, 0.25f, 0.18f);
            break;
        case 7: // Algorithms & Optimization: Mint / Cyan Teal
            textColor = D2D1::ColorF(0.12f, 0.88f, 0.72f, 1.0f);
            bgColor = D2D1::ColorF(0.12f, 0.88f, 0.72f, 0.18f);
            break;
        default: // General
            textColor = D2D1::ColorF(0.55f, 0.78f, 0.98f, 1.0f);
            bgColor = D2D1::ColorF(0.55f, 0.78f, 0.98f, 0.15f);
            break;
        }

        D2D1_RECT_F badgeRect = D2D1::RectF(card.right - badgeWidth - 14.0f, card.top + 12.0f, card.right - 14.0f, card.top + 32.0f);
        res.brushDictTagBg->SetColor(bgColor);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(badgeRect, 4.0f, 4.0f), res.brushDictTagBg);
        res.brushDictTagText->SetColor(textColor);
        ctx->DrawText(
            dictCard.categoryTag.c_str(),
            (UINT32)dictCard.categoryTag.length(),
            res.textFormatDictTag,
            badgeRect,
            res.brushDictTagText
        );
    }

    // 3. Subtle Header Divider Line
    ctx->DrawLine(
        D2D1::Point2F(card.left + 16.0f, card.top + 38.0f),
        D2D1::Point2F(card.right - 16.0f, card.top + 38.0f),
        res.brushDictCardBorder,
        1.0f
    );

    // 4. Definition Content
    D2D1_RECT_F defRect = D2D1::RectF(card.left + 16.0f, card.top + 46.0f, card.right - 16.0f, card.top + 46.0f + defHeight);
    if (defLayout) {
        ctx->DrawTextLayout(
            D2D1::Point2F(defRect.left, defRect.top),
            defLayout.Get(),
            res.brushDictDefText
        );
    } else {
        ctx->DrawText(
            dictCard.definition.c_str(),
            (UINT32)dictCard.definition.length(),
            res.textFormatDictDef,
            defRect,
            res.brushDictDefText
        );
    }

    // 5. Footer Hint
    D2D1_RECT_F hintRect = D2D1::RectF(card.left + 16.0f, card.bottom - 22.0f, card.right - 16.0f, card.bottom - 6.0f);
    const wchar_t* hint = L"Esc: dismiss \x2022 Ctrl+C: copy \x2022 100% Offline Lexicon";
    ctx->DrawText(
        hint,
        (UINT32)wcslen(hint),
        res.textFormatDictHint,
        hintRect,
        res.brushDictHintText
    );
}

// =========================================================================
// 8. PresenterBar View
// =========================================================================
void PresenterBarView::Render(
    ID2D1DeviceContext* ctx,
    const PresenterBarRenderInfo& presenterBar,
    float dipWidth,
    float dipHeight,
    const PresenterBarResources& res
) {
    if (!presenterBar.visible || !ctx) return;

    D2D1_RECT_F barRect = PresenterBarLayout::GetBarRect(dipWidth, dipHeight);
    D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(barRect, 22.0f, 22.0f);

    // Drop shadow
    D2D1_RECT_F shadowRect = D2D1::RectF(barRect.left + 3.0f, barRect.top + 3.0f, barRect.right + 4.0f, barRect.bottom + 4.0f);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 22.0f, 22.0f), res.brushPageShadow);

    // Background & border
    ctx->FillRoundedRectangle(roundedBar, res.brushHudBg);
    ctx->DrawRoundedRectangle(roundedBar, res.brushHudBorder, 1.0f);

    // Buttons
    D2D1_RECT_F btnPrev = PresenterBarLayout::GetPrevBtnRect(barRect);
    D2D1_RECT_F pageInfoRect = PresenterBarLayout::GetPageInfoRect(barRect);
    D2D1_RECT_F btnNext = PresenterBarLayout::GetNextBtnRect(barRect);
    D2D1_RECT_F btnLaser = PresenterBarLayout::GetLaserBtnRect(barRect);
    D2D1_RECT_F btnColor = PresenterBarLayout::GetColorBtnRect(barRect);
    D2D1_RECT_F btnExit = PresenterBarLayout::GetExitBtnRect(barRect);

    // Highlight hovered / active buttons
    auto drawButtonBg = [&](const D2D1_RECT_F& r, int btnIdx, bool isActive = false) {
        if (isActive) {
            ctx->FillRoundedRectangle(D2D1::RoundedRect(r, 14.0f, 14.0f), res.brushPresenterBtnActive);
        } else if (presenterBar.hoveredBtn == btnIdx) {
            ctx->FillRoundedRectangle(D2D1::RoundedRect(r, 14.0f, 14.0f), res.brushPresenterBtnHover);
        }
    };

    drawButtonBg(btnPrev, 0);
    drawButtonBg(btnNext, 1);
    drawButtonBg(btnLaser, 2, presenterBar.isLaserActive);
    drawButtonBg(btnColor, 3);
    drawButtonBg(btnExit, 4);

    // Button Labels
    ctx->DrawText(L"◀", 1, res.textFormatPresenter, btnPrev, res.brushHudText);

    wchar_t pageBuf[32];
    swprintf_s(pageBuf, L"%u / %u", presenterBar.currentPage + 1, presenterBar.totalPages);
    ctx->DrawText(pageBuf, (UINT32)wcslen(pageBuf), res.textFormatPresenter, pageInfoRect, res.brushHudText);

    ctx->DrawText(L"▶", 1, res.textFormatPresenter, btnNext, res.brushHudText);
    ctx->DrawText(L"Laser", 5, res.textFormatPresenter, btnLaser, res.brushHudText);

    // Color indicator dot
    D2D1_POINT_2F dotCenter = D2D1::Point2F((btnColor.left + btnColor.right) * 0.5f, (btnColor.top + btnColor.bottom) * 0.5f);
    D2D1_COLOR_F dotColor = D2D1::ColorF(1.0f, 0.2f, 0.2f);
    switch (presenterBar.laserColor) {
    case LaserColor::Red:   dotColor = D2D1::ColorF(1.0f, 0.2f, 0.2f); break;
    case LaserColor::Green: dotColor = D2D1::ColorF(0.0f, 1.0f, 0.35f); break;
    case LaserColor::Cyan:  dotColor = D2D1::ColorF(0.0f, 0.85f, 1.0f); break;
    case LaserColor::Gold:  dotColor = D2D1::ColorF(1.0f, 0.75f, 0.0f); break;
    }
    if (res.brushLaserCore) {
        res.brushLaserCore->SetColor(dotColor);
        ctx->FillEllipse(D2D1::Ellipse(dotCenter, 6.0f, 6.0f), res.brushLaserCore);
    }

    ctx->DrawText(L"⛶", 1, res.textFormatPresenter, btnExit, res.brushHudText);
}

int PresenterBarView::HitTest(POINT pt, UINT width, UINT height, float dpi) {
    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = (float)width * dipScale;
    float dipH = (float)height * dipScale;

    D2D1_RECT_F bar = PresenterBarLayout::GetBarRect(dipW, dipH);
    if (dipX < bar.left || dipX > bar.right || dipY < bar.top || dipY > bar.bottom) {
        return -1;
    }

    auto inRect = [&](const D2D1_RECT_F& r) {
        return dipX >= r.left && dipX <= r.right && dipY >= r.top && dipY <= r.bottom;
    };

    if (inRect(PresenterBarLayout::GetPrevBtnRect(bar))) return 0;
    if (inRect(PresenterBarLayout::GetNextBtnRect(bar))) return 1;
    if (inRect(PresenterBarLayout::GetLaserBtnRect(bar))) return 2;
    if (inRect(PresenterBarLayout::GetColorBtnRect(bar))) return 3;
    if (inRect(PresenterBarLayout::GetExitBtnRect(bar))) return 4;

    return 100; // Inside bar body
}

// =========================================================================
// 9. TtsBar View (Read Aloud Floating HUD)
// =========================================================================

void TtsBarView::Render(
    ID2D1DeviceContext* ctx,
    const TtsBarRenderInfo& ttsBar,
    float dipWidth,
    float topOffset,
    const TtsBarResources& res
) {
    if (!ctx || !ttsBar.visible) return;

    D2D1_RECT_F bar = TtsBarLayout::GetBarRect(dipWidth, topOffset);

    // Main bar background and border
    if (res.brushTtsBarBg) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(bar, 8.0f, 8.0f), res.brushTtsBarBg);
    }
    if (res.brushTtsBarBorder) {
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(bar, 8.0f, 8.0f), res.brushTtsBarBorder, 1.0f);
    }

    auto drawButton = [&](int btnIdx, const wchar_t* symbol, IDWriteTextFormat* fmt) {
        D2D1_RECT_F btnRect = TtsBarLayout::GetBtnRect(bar, btnIdx);
        if (ttsBar.hoveredBtn == btnIdx && res.brushTtsBarBtnHover) {
            ctx->FillRoundedRectangle(D2D1::RoundedRect(btnRect, 4.0f, 4.0f), res.brushTtsBarBtnHover);
        }
        if (fmt && res.brushTtsBarText) {
            ctx->DrawText(symbol, (UINT32)wcslen(symbol), fmt, btnRect, res.brushTtsBarText);
        }
    };

    // Button 0: Prev sentence ⏮
    drawButton(0, L"\x23EE", res.textFormatTtsBar);

    // Button 1: Play/Pause ▶ / ⏸
    drawButton(1, ttsBar.isPaused ? L"\x25B6" : L"\x23F8", res.textFormatTtsBar);

    // Button 2: Next sentence ⏭
    drawButton(2, L"\x23ED", res.textFormatTtsBar);

    // Middle Voice & Status display
    D2D1_RECT_F voiceRect = TtsBarLayout::GetVoiceInfoRect(bar);
    std::wstring voiceText = ttsBar.voiceName;
    if (ttsBar.isPaused) {
        voiceText += L" (Paused)";
    }
    if (res.textFormatTtsBar && res.brushTtsBarText) {
        ctx->DrawText(
            voiceText.c_str(),
            (UINT32)voiceText.length(),
            res.textFormatTtsBar,
            voiceRect,
            res.brushTtsBarText
        );
    }

    // Button 3: Speed rate pill (e.g. "1.0x", "1.2x")
    drawButton(3, ttsBar.rateLabel.c_str(), res.textFormatTtsSpeed ? res.textFormatTtsSpeed : res.textFormatTtsBar);

    // Button 4: Close [✕]
    drawButton(4, L"\x2715", res.textFormatTtsBar);
}

int TtsBarView::HitTest(POINT pt, UINT width, [[maybe_unused]] UINT height, float dpi, float topOffset) {
    float dipScale = 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    float dipX = (float)pt.x * dipScale;
    float dipY = (float)pt.y * dipScale;
    float dipW = (float)width * dipScale;

    D2D1_RECT_F bar = TtsBarLayout::GetBarRect(dipW, topOffset);
    if (dipX < bar.left || dipX > bar.right || dipY < bar.top || dipY > bar.bottom) {
        return -1;
    }

    auto inRect = [&](const D2D1_RECT_F& r) {
        return dipX >= r.left && dipX <= r.right && dipY >= r.top && dipY <= r.bottom;
    };

    if (inRect(TtsBarLayout::GetBtnRect(bar, 0))) return 0;
    if (inRect(TtsBarLayout::GetBtnRect(bar, 1))) return 1;
    if (inRect(TtsBarLayout::GetBtnRect(bar, 2))) return 2;
    if (inRect(TtsBarLayout::GetBtnRect(bar, 3))) return 3;
    if (inRect(TtsBarLayout::GetBtnRect(bar, 4))) return 4;

    return 100; // Inside bar body
}

} // namespace UIViews

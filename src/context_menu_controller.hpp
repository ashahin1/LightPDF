/**
 * @file context_menu_controller.hpp
 * @brief Native context-sensitive right-click menus for tabs, links, selections, and document canvas.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <cstdint>
#include "pdf_parser.hpp"

/// @brief Context categories determining the displayed right-click menu items.
enum class ContextMenuContext {
    Canvas,         ///< General document page or empty background area
    TextSelection,  ///< Active text selection highlight
    Hyperlink,      ///< Interactive link annotation (internal or external)
    TabStrip        ///< Tab header in the top tab strip
};

/// @brief Unique command IDs for context menu actions.
enum ContextMenuCommand : UINT_PTR {
    ID_CTX_NONE = 0,

    // Selection commands
    ID_CTX_COPY = 2001,
    ID_CTX_SELECT_ALL,
    ID_CTX_DESELECT,
    ID_CTX_DICT_LOOKUP,
    ID_CTX_READ_ALOUD_SELECTION,
    ID_CTX_SEARCH_SELECTION,

    // Link commands
    ID_CTX_OPEN_LINK = 2010,
    ID_CTX_COPY_LINK,

    // Canvas / Reading commands
    ID_CTX_NEXT_PAGE = 2020,
    ID_CTX_PREV_PAGE,
    ID_CTX_GOTO_PAGE,
    ID_CTX_FIT_PAGE,
    ID_CTX_FIT_WIDTH,
    ID_CTX_CONTINUOUS_SCROLL,
    ID_CTX_READ_ALOUD,
    ID_CTX_FIND,
    ID_CTX_PRINT,
    ID_CTX_SAVE_SEARCHABLE,
    ID_CTX_SAVE_DECRYPTED,
    ID_CTX_DOC_PROPERTIES,

    // Tab commands
    ID_CTX_TAB_CLOSE = 2040,
    ID_CTX_TAB_CLOSE_OTHERS,
    ID_CTX_TAB_CLOSE_RIGHT,
    ID_CTX_TAB_REOPEN_CLOSED,
    ID_CTX_TAB_COPY_PATH,
    ID_CTX_TAB_OPEN_FOLDER,
};

/// @brief State snapshot passed to context menu builder to customize labels, icons, and enable flags.
struct ContextMenuData {
    // Document state
    bool isDocumentLoaded = false;
    bool isPasswordProtected = false;
    bool isContinuousScroll = false;
    uint32_t currentPage = 0;
    uint32_t totalPages = 0;

    // Selection state
    bool hasSelection = false;
    std::wstring selectedText;

    // Link state
    bool hasLink = false;
    PdfLink hitLink;

    // Tab state
    size_t targetTabIndex = 0;
    size_t totalTabs = 0;
    bool canReopenClosedTab = false;
    std::wstring tabFilePath;
};

/**
 * @class ContextMenuController
 * @brief Constructs, styles, and tracks native Win32 context menus for LightPDF.
 */
class ContextMenuController {
public:
    /**
     * @brief Constructs an HMENU populated with items appropriate for the given context and state.
     * @param context Active context category.
     * @param data Current document and UI state snapshot.
     * @return Created HMENU (caller must destroy via DestroyMenu if not shown with TrackPopupMenuEx).
     */
    static HMENU CreateMenuForContext(ContextMenuContext context, const ContextMenuData& data);

    /**
     * @brief Displays the context-sensitive menu at the specified screen coordinates and returns selected command.
     * @param hwnd Window handle owning the popup menu.
     * @param ptScreen Screen-coordinate point where right-click occurred.
     * @param context Active context category.
     * @param data Current document and UI state snapshot.
     * @return Selected ContextMenuCommand ID, or ID_CTX_NONE if dismissed without selection.
     */
    static UINT_PTR ShowContextMenu(
        HWND hwnd,
        POINT ptScreen,
        ContextMenuContext context,
        const ContextMenuData& data
    );
};

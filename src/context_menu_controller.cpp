/**
 * @file context_menu_controller.cpp
 * @brief Implementation of native context-sensitive right-click menus for LightPDF.
 */

#include "context_menu_controller.hpp"
#include <algorithm>

namespace {

enum PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);

void EnsureDarkModeInitialized() {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;

    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxTheme) {
        hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (hUxTheme) {
        auto pfnSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(
            GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135))
        );
        if (pfnSetPreferredAppMode) {
            pfnSetPreferredAppMode(ForceDark);
        }
    }
}

std::wstring FormatTruncatedQuery(const std::wstring& in, size_t maxLen = 18) {
    std::wstring clean;
    clean.reserve(in.size());
    bool inSpace = false;
    for (wchar_t ch : in) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L' ') {
            if (!inSpace && !clean.empty()) {
                clean.push_back(L' ');
                inSpace = true;
            }
        } else {
            clean.push_back(ch);
            inSpace = false;
        }
    }
    while (!clean.empty() && clean.back() == L' ') {
        clean.pop_back();
    }
    if (clean.size() > maxLen) {
        clean = clean.substr(0, maxLen) + L"...";
    }
    return clean;
}

} // anonymous namespace

HMENU ContextMenuController::CreateMenuForContext(ContextMenuContext context, const ContextMenuData& data) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return nullptr;

    switch (context) {
    case ContextMenuContext::TabStrip: {
        AppendMenuW(hMenu, MF_STRING, ID_CTX_TAB_CLOSE, L"Close Tab\tCtrl+W");
        AppendMenuW(hMenu, MF_STRING | (data.totalTabs <= 1 ? MF_GRAYED : 0), ID_CTX_TAB_CLOSE_OTHERS, L"Close Other Tabs");
        AppendMenuW(hMenu, MF_STRING | (data.targetTabIndex + 1 >= data.totalTabs ? MF_GRAYED : 0), ID_CTX_TAB_CLOSE_RIGHT, L"Close Tabs to the Right");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING | (!data.canReopenClosedTab ? MF_GRAYED : 0), ID_CTX_TAB_REOPEN_CLOSED, L"Reopen Closed Tab\tCtrl+Shift+T");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING | (data.tabFilePath.empty() ? MF_GRAYED : 0), ID_CTX_TAB_COPY_PATH, L"Copy File Path");
        AppendMenuW(hMenu, MF_STRING | (data.tabFilePath.empty() ? MF_GRAYED : 0), ID_CTX_TAB_OPEN_FOLDER, L"Open Containing Folder");
        break;
    }

    case ContextMenuContext::Hyperlink: {
        AppendMenuW(hMenu, MF_STRING, ID_CTX_OPEN_LINK, L"Open Link");
        SetMenuDefaultItem(hMenu, ID_CTX_OPEN_LINK, FALSE);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPY_LINK, L"Copy Link Address");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_FIT_PAGE, L"Fit to Page\tCtrl+0");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+1");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_FIT_WIDTH, L"Fit to Width\tCtrl+2");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_PRINT, L"Print...\tCtrl+P");
        break;
    }

    case ContextMenuContext::TextSelection: {
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPY, L"Copy\tCtrl+C");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        std::wstring truncQuery = FormatTruncatedQuery(data.selectedText);
        std::wstring dictLabel = truncQuery.empty()
            ? L"Look up in Dictionary\tD"
            : (L"Look up \"" + truncQuery + L"\" in Dictionary\tD");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_DICT_LOOKUP, dictLabel.c_str());

        AppendMenuW(hMenu, MF_STRING, ID_CTX_READ_ALOUD_SELECTION, L"Read Aloud Selection");

        std::wstring searchLabel = truncQuery.empty()
            ? L"Find in Page...\tCtrl+F"
            : (L"Search for \"" + truncQuery + L"\"\tCtrl+F");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_SEARCH_SELECTION, searchLabel.c_str());

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_SELECT_ALL, L"Select All\tCtrl+A");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_DESELECT, L"Deselect\tEsc");
        break;
    }

    case ContextMenuContext::Canvas: {
        if (!data.isDocumentLoaded) {
            AppendMenuW(hMenu, MF_STRING | (!data.canReopenClosedTab ? MF_GRAYED : 0), ID_CTX_TAB_REOPEN_CLOSED, L"Reopen Closed Tab\tCtrl+Shift+T");
            break;
        }

        UINT prevFlag = (data.currentPage == 0) ? MF_GRAYED : 0;
        UINT nextFlag = (data.totalPages == 0 || data.currentPage + 1 >= data.totalPages) ? MF_GRAYED : 0;

        AppendMenuW(hMenu, MF_STRING | prevFlag, ID_CTX_PREV_PAGE, L"Previous Page\tPage Up");
        AppendMenuW(hMenu, MF_STRING | nextFlag, ID_CTX_NEXT_PAGE, L"Next Page\tPage Down");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_GOTO_PAGE, L"Go to Page...\tCtrl+G");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_FIT_PAGE, L"Fit to Page\tCtrl+0");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_ACTUAL_SIZE, L"Actual Size (100%)\tCtrl+1");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_FIT_WIDTH, L"Fit to Width\tCtrl+2");
        AppendMenuW(hMenu, MF_STRING | (data.isContinuousScroll ? MF_CHECKED : MF_UNCHECKED), ID_CTX_CONTINUOUS_SCROLL, L"Continuous Scrolling\tCtrl+3");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_READ_ALOUD, L"Read Aloud\tCtrl+R");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_FIND, L"Find in Page...\tCtrl+F");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_PRINT, L"Print...\tCtrl+P");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        if (data.isPasswordProtected) {
            AppendMenuW(hMenu, MF_STRING, ID_CTX_SAVE_DECRYPTED, L"Save Decrypted Copy...\tCtrl+S");
        } else {
            AppendMenuW(hMenu, MF_STRING, ID_CTX_SAVE_SEARCHABLE, L"Save Searchable PDF...\tCtrl+S");
        }

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_DOC_PROPERTIES, L"Document Properties...\tCtrl+D");
        break;
    }
    }

    return hMenu;
}

UINT_PTR ContextMenuController::ShowContextMenu(
    HWND hwnd,
    POINT ptScreen,
    ContextMenuContext context,
    const ContextMenuData& data
) {
    EnsureDarkModeInitialized();

    HMENU hMenu = CreateMenuForContext(context, data);
    if (!hMenu) return ID_CTX_NONE;

    SetForegroundWindow(hwnd);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
    int chosenCmd = TrackPopupMenuEx(hMenu, flags, ptScreen.x, ptScreen.y, hwnd, nullptr);
    DestroyMenu(hMenu);
    return static_cast<UINT_PTR>(chosenCmd);
}

/**
 * @file common_utils.hpp
 * @brief Centralized header-only utilities for DPI scaling, path operations, string conversions, and clipboard management.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <cstring>
#include <string>
#include <string_view>

/**
 * @namespace DpiUtils
 * @brief Utilities for monitor DPI scaling, DIP coordinate conversions, and geometric hit-testing.
 */
namespace DpiUtils {
    /// @brief Computes scale factor to convert monitor physical pixels to Direct2D DIPs (96 DPI base).
    inline float Scale(float dpi) {
        return 96.0f / (dpi > 0.0f ? dpi : 96.0f);
    }

    /// @brief Converts a Win32 physical pixel POINT to a Direct2D DIP coordinate.
    inline D2D1_POINT_2F ToDip(POINT pt, float dpi) {
        float s = Scale(dpi);
        return D2D1::Point2F(static_cast<float>(pt.x) * s, static_cast<float>(pt.y) * s);
    }

    /// @brief Determines whether a 2D point (x, y) falls inside the bounding rectangle.
    inline bool PointInRect(float x, float y, const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }
}

/**
 * @namespace PathUtils
 * @brief Filesystem path manipulation, normalization, and directory extraction helpers.
 */
namespace PathUtils {
    /// @brief Extracts the file name (with extension) from a path string.
    inline std::wstring GetFileName(const std::wstring& path) {
        size_t pos = path.find_last_of(L"\\/");
        return (pos != std::wstring::npos && pos + 1 < path.size()) ? path.substr(pos + 1) : path;
    }

    /// @brief Extracts the parent directory path from a file path.
    inline std::wstring GetDirectory(const std::wstring& path) {
        size_t pos = path.find_last_of(L"\\/");
        return (pos != std::wstring::npos) ? path.substr(0, pos) : L"";
    }

    /// @brief Resolves and canonicalizes a relative or standard path to its full Windows absolute path.
    inline std::wstring NormalizePath(const std::wstring& path) {
        if (path.empty()) return L"";
        wchar_t fullPath[MAX_PATH * 2] = { 0 };
        DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(fullPath)), fullPath, nullptr);
        return (len > 0 && len < static_cast<DWORD>(_countof(fullPath))) ? std::wstring(fullPath, len) : path;
    }
}

/**
 * @namespace StringUtils
 * @brief High-performance character encoding conversions between UTF-8 and UTF-16 (wide).
 */
namespace StringUtils {
    /// @brief Converts a wide UTF-16 string view to an allocated UTF-8 std::string.
    inline std::string ToUtf8(std::wstring_view wstr) {
        if (wstr.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string out(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), out.data(), len, nullptr, nullptr);
        return out;
    }

    /// @brief Converts an 8-bit string view (UTF-8 by default) to an allocated wide UTF-16 std::wstring.
    inline std::wstring ToUtf16(std::string_view str, UINT codePage = CP_UTF8, DWORD flags = 0) {
        if (str.empty()) return {};
        int wlen = MultiByteToWideChar(codePage, flags, str.data(), static_cast<int>(str.size()), nullptr, 0);
        if (wlen <= 0) return {};
        std::wstring out(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(codePage, 0, str.data(), static_cast<int>(str.size()), out.data(), wlen);
        return out;
    }
}

/**
 * @namespace ClipboardUtils
 * @brief Safe Win32 clipboard text read/write operations with guaranteed lock release.
 */
namespace ClipboardUtils {
    /// @brief Copies a Unicode string to the Windows system clipboard.
    inline bool SetText(HWND hwnd, const std::wstring& text) {
        if (text.empty() || !OpenClipboard(hwnd)) return false;
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        bool ok = false;
        if (hMem) {
            if (void* pMem = GlobalLock(hMem)) {
                std::memcpy(pMem, text.c_str(), bytes);
                GlobalUnlock(hMem);
                ok = (SetClipboardData(CF_UNICODETEXT, hMem) != nullptr);
            }
            if (!ok) GlobalFree(hMem);
        }
        CloseClipboard();
        return ok;
    }

    /// @brief Retrieves the current plain text string from the Windows system clipboard.
    inline std::wstring GetText(HWND hwnd) {
        std::wstring out;
        if (!OpenClipboard(hwnd)) return out;
        if (HANDLE hData = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData))) {
                out = pText;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
        return out;
    }
}

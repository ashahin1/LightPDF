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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

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

    /// @brief Resolves and canonicalizes a relative, 8.3 short, or standard path to its full Windows long path.
    inline std::wstring NormalizePath(const std::wstring& path) {
        if (path.empty()) return L"";
        wchar_t fullPath[MAX_PATH * 2] = { 0 };
        DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(fullPath)), fullPath, nullptr);
        if (len == 0 || len >= static_cast<DWORD>(_countof(fullPath))) {
            return path;
        }
        wchar_t longPath[MAX_PATH * 2] = { 0 };
        DWORD longLen = GetLongPathNameW(fullPath, longPath, static_cast<DWORD>(_countof(longPath)));
        if (longLen > 0 && longLen < static_cast<DWORD>(_countof(longPath))) {
            return std::wstring(longPath, longLen);
        }
        return std::wstring(fullPath, len);
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

/**
 * @namespace PrintUtils
 * @brief Utilities for document print orientation detection, copy count resolution, and spool page scheduling.
 */
namespace PrintUtils {
    /**
     * @brief Determines whether a PDF page should default to landscape or portrait orientation.
     * @param pageSize Page dimensions in points / DIPs.
     * @return DMORIENT_LANDSCAPE (2) if width > height, else DMORIENT_PORTRAIT (1).
     */
    inline short DetectOrientation(D2D1_SIZE_F pageSize) {
        return (pageSize.width > pageSize.height) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    }

    /**
     * @brief Resolves effective print copy count from dialog result and DEVMODE fields.
     * @param dlgCopies Number of copies reported by PrintDlgExW (pdex.nCopies).
     * @param devModeCopies Number of copies specified in printer DEVMODE (dmCopies).
     * @param devModeFields Bitfield flags from DEVMODE (dmFields).
     * @return Clamped copy count (between 1 and 999).
     */
    inline DWORD ResolveCopyCount(DWORD dlgCopies, short devModeCopies, DWORD devModeFields) {
        DWORD copies = 1;
        if (dlgCopies > 1) {
            copies = dlgCopies;
        } else if ((devModeFields & DM_COPIES) && devModeCopies > 1) {
            copies = static_cast<DWORD>(devModeCopies);
        }
        return (std::max)(1UL, (std::min)(copies, 999UL));
    }

    /**
     * @brief Builds the exact sequence of 0-based page indices to send to StartPage/EndPage.
     * @param pages Set of page indices to print.
     * @param copies Number of copies to produce.
     * @param collate True for collated (1,2, 1,2), false for uncollated (1,1, 2,2).
     * @return Ordered list of page indices to render.
     */
    inline std::vector<uint32_t> BuildPrintPageSchedule(const std::vector<uint32_t>& pages, DWORD copies, bool collate) {
        std::vector<uint32_t> schedule;
        if (pages.empty() || copies == 0) return schedule;
        DWORD safeCopies = (std::min)(copies, 999UL);
        schedule.reserve(pages.size() * safeCopies);

        if (collate) {
            for (DWORD c = 0; c < safeCopies; ++c) {
                for (uint32_t pageIndex : pages) {
                    schedule.push_back(pageIndex);
                }
            }
        } else {
            for (uint32_t pageIndex : pages) {
                for (DWORD c = 0; c < safeCopies; ++c) {
                    schedule.push_back(pageIndex);
                }
            }
        }
        return schedule;
    }
}

/**
 * @namespace RotationUtils
 * @brief Orthogonal quarter-turn coordinate and matrix utilities for live page rotation.
 */
namespace RotationUtils {
    /**
     * @brief Normalizes any signed quarter-turn count into {0, 1, 2, 3} (0, 90, 180, 270 degrees CW).
     * @param turns Signed integer quarter-turn count.
     * @return Normalized quarter turns in [0, 3].
     */
    inline uint8_t NormalizeQuarterTurns(int turns) {
        return static_cast<uint8_t>(((turns % 4) + 4) % 4);
    }

    /**
     * @brief Converts quarter turns into clockwise degrees (0, 90, 180, 270).
     * @param quarterTurns Quarter turns count.
     * @return Clockwise rotation angle in degrees.
     */
    inline int QuarterTurnsToDegrees(uint8_t quarterTurns) {
        return static_cast<int>(NormalizeQuarterTurns(quarterTurns)) * 90;
    }

    /**
     * @brief Returns effective displayed page dimensions after applying quarter-turn rotation.
     * @param intrinsicSize Unrotated page dimensions in DIPs.
     * @param quarterTurns Clockwise quarter-turn count.
     * @return Rotated page size (width and height swapped when quarterTurns is odd).
     */
    inline D2D1_SIZE_F GetRotatedSize(D2D1_SIZE_F intrinsicSize, uint8_t quarterTurns) {
        float w = (intrinsicSize.width > 0.0f) ? intrinsicSize.width : 0.0f;
        float h = (intrinsicSize.height > 0.0f) ? intrinsicSize.height : 0.0f;
        return ((NormalizeQuarterTurns(quarterTurns) & 1u) != 0u)
            ? D2D1::SizeF(h, w)
            : D2D1::SizeF(w, h);
    }

    /**
     * @brief Maps a point from intrinsic page space into rotated display page space.
     * @param pt Point in intrinsic space [0..intrinsicSize.width] x [0..intrinsicSize.height].
     * @param intrinsicSize Intrinsic dimensions of the source coordinate space.
     * @param quarterTurns Clockwise quarter-turn delta.
     * @return Point in rotated display space.
     */
    inline D2D1_POINT_2F IntrinsicToDisplayPoint(D2D1_POINT_2F pt, D2D1_SIZE_F intrinsicSize, uint8_t quarterTurns) {
        switch (NormalizeQuarterTurns(quarterTurns)) {
        case 1:  return D2D1::Point2F(intrinsicSize.height - pt.y, pt.x);
        case 2:  return D2D1::Point2F(intrinsicSize.width - pt.x, intrinsicSize.height - pt.y);
        case 3:  return D2D1::Point2F(pt.y, intrinsicSize.width - pt.x);
        default: return pt;
        }
    }

    /**
     * @brief Maps a point from rotated display page space back into intrinsic page space.
     * @param pt Point in rotated display space.
     * @param intrinsicSize Intrinsic dimensions of the target coordinate space.
     * @param quarterTurns Clockwise quarter-turn delta that was applied to intrinsic space.
     * @return Point in intrinsic space.
     */
    inline D2D1_POINT_2F DisplayToIntrinsicPoint(D2D1_POINT_2F pt, D2D1_SIZE_F intrinsicSize, uint8_t quarterTurns) {
        switch (NormalizeQuarterTurns(quarterTurns)) {
        case 1:  return D2D1::Point2F(pt.y, intrinsicSize.height - pt.x);
        case 2:  return D2D1::Point2F(intrinsicSize.width - pt.x, intrinsicSize.height - pt.y);
        case 3:  return D2D1::Point2F(intrinsicSize.width - pt.y, pt.x);
        default: return pt;
        }
    }

    /**
     * @brief Maps an axis-aligned rectangle from intrinsic space into rotated display space.
     * @param rect Rectangle in intrinsic space.
     * @param intrinsicSize Intrinsic dimensions of the source coordinate space.
     * @param quarterTurns Clockwise quarter-turn delta.
     * @return Normalized axis-aligned rectangle in display space.
     */
    inline D2D1_RECT_F IntrinsicToDisplayRect(const D2D1_RECT_F& rect, D2D1_SIZE_F intrinsicSize, uint8_t quarterTurns) {
        D2D1_POINT_2F p1 = IntrinsicToDisplayPoint(D2D1::Point2F(rect.left, rect.top), intrinsicSize, quarterTurns);
        D2D1_POINT_2F p2 = IntrinsicToDisplayPoint(D2D1::Point2F(rect.right, rect.bottom), intrinsicSize, quarterTurns);
        return D2D1::RectF(
            (std::min)(p1.x, p2.x),
            (std::min)(p1.y, p2.y),
            (std::max)(p1.x, p2.x),
            (std::max)(p1.y, p2.y)
        );
    }

    /**
     * @brief Maps an axis-aligned rectangle from rotated display space back into intrinsic space.
     * @param rect Rectangle in rotated display space.
     * @param intrinsicSize Intrinsic dimensions of the target coordinate space.
     * @param quarterTurns Clockwise quarter-turn delta.
     * @return Normalized axis-aligned rectangle in intrinsic space.
     */
    inline D2D1_RECT_F DisplayToIntrinsicRect(const D2D1_RECT_F& rect, D2D1_SIZE_F intrinsicSize, uint8_t quarterTurns) {
        D2D1_POINT_2F p1 = DisplayToIntrinsicPoint(D2D1::Point2F(rect.left, rect.top), intrinsicSize, quarterTurns);
        D2D1_POINT_2F p2 = DisplayToIntrinsicPoint(D2D1::Point2F(rect.right, rect.bottom), intrinsicSize, quarterTurns);
        return D2D1::RectF(
            (std::min)(p1.x, p2.x),
            (std::min)(p1.y, p2.y),
            (std::max)(p1.x, p2.x),
            (std::max)(p1.y, p2.y)
        );
    }

    /**
     * @brief Builds a Direct2D 3x2 transform matrix mapping scaled intrinsic coordinates [0..scaledIntrinsicW] x [0..scaledIntrinsicH]
     *        onto screen coordinates anchored at (screenLeft, screenTop).
     * @param quarterTurns Clockwise quarter-turn count.
     * @param scaledIntrinsicW Intrinsic width multiplied by zoom.
     * @param scaledIntrinsicH Intrinsic height multiplied by zoom.
     * @param screenLeft Destination top-left X coordinate in screen DIPs (or pixels).
     * @param screenTop Destination top-left Y coordinate in screen DIPs (or pixels).
     * @return Orthogonal Direct2D transformation matrix.
     */
    inline D2D1::Matrix3x2F BuildPageDisplayMatrix(
        uint8_t quarterTurns,
        float scaledIntrinsicW,
        float scaledIntrinsicH,
        float screenLeft,
        float screenTop
    ) {
        switch (NormalizeQuarterTurns(quarterTurns)) {
        case 1:
            return D2D1::Matrix3x2F::Rotation(90.0f) *
                   D2D1::Matrix3x2F::Translation(screenLeft + scaledIntrinsicH, screenTop);
        case 2:
            return D2D1::Matrix3x2F::Rotation(180.0f) *
                   D2D1::Matrix3x2F::Translation(screenLeft + scaledIntrinsicW, screenTop + scaledIntrinsicH);
        case 3:
            return D2D1::Matrix3x2F::Rotation(270.0f) *
                   D2D1::Matrix3x2F::Translation(screenLeft, screenTop + scaledIntrinsicW);
        default:
            return D2D1::Matrix3x2F::Translation(screenLeft, screenTop);
        }
    }
}

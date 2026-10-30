#pragma once

#include <string>
#include <vector>

/**
 * @file supported_formats.hpp
 * @brief Single source of truth for every file extension LightPDF can open and register.
 * Used by Open/Save File dialogs, shell (/register) associations, and format validation.
 */
/**
 * @namespace SupportedFormats
 * @brief Single source of truth for supported document extensions, wildcard filters, and validation.
 */
namespace SupportedFormats {

/// @brief PDF Documents extension list (all lowercase with leading dot).
inline constexpr const wchar_t* kPdf[] = {
    L".pdf"
};

/**
 * @brief Builds a semicolon-separated wildcard pattern such as "*.pdf".
 * @tparam N Number of extensions in fixed array.
 * @param exts Fixed array of extension string pointers.
 * @return Semicolon-separated wildcard pattern string.
 */
template <size_t N>
inline std::wstring BuildPattern(const wchar_t* const (&exts)[N]) {
    std::wstring out;
    for (size_t i = 0; i < N; ++i) {
        if (i > 0) out += L';';
        out += L'*';
        out += exts[i];
    }
    return out;
}

/**
 * @brief Builds a human-readable filter label with wildcard pattern e.g. "PDF Documents (*.pdf)".
 * @tparam N Number of extensions in fixed array.
 * @param prefix Human-readable category label prefix.
 * @param exts Fixed array of extension string pointers.
 * @return Formatted filter label string.
 */
template <size_t N>
inline std::wstring BuildFilterLabel(const wchar_t* prefix, const wchar_t* const (&exts)[N]) {
    std::wstring out = prefix;
    out += L" (*";
    out += exts[0];
    for (size_t i = 1; i < N; ++i) {
        out += L";*";
        out += exts[i];
    }
    out += L")";
    return out;
}

/**
 * @brief Returns all supported extensions across all categories.
 * @return Vector of extension strings with leading dots.
 */
inline std::vector<std::wstring> GetAllExtensions() {
    std::vector<std::wstring> exts;
    exts.reserve(sizeof(kPdf) / sizeof(kPdf[0]));
    for (const wchar_t* e : kPdf) {
        exts.emplace_back(e);
    }
    return exts;
}

/**
 * @brief Builds an all-inclusive wildcard pattern covering EVERY supported extension.
 * @return Wildcard pattern string.
 */
inline std::wstring BuildAllSupportedPattern() {
    return BuildPattern(kPdf);
}

/**
 * @brief Checks if an extension or file path is in the supported formats list.
 * @param extOrPath File extension (e.g. ".pdf") or full file path.
 * @return true if extension is supported, false otherwise.
 */
inline bool IsSupported(const std::wstring& extOrPath) {
    if (extOrPath.empty()) return false;
    size_t dotPos = extOrPath.rfind(L'.');
    const wchar_t* ext = (dotPos != std::wstring::npos) ? (extOrPath.c_str() + dotPos) : extOrPath.c_str();
    if (*ext != L'.') return false;
    for (const wchar_t* e : kPdf) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}

} // namespace SupportedFormats

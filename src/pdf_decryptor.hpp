/**
 * @file pdf_decryptor.hpp
 * @brief Lossless PDF decryption and unencrypted PDF serialization engine.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

/// @brief Result of a PDF decryption operation.
struct PdfDecryptResult {
    bool success = false;
    std::wstring errorMessage;
    size_t objectsDecrypted = 0;
    size_t streamsDecrypted = 0;
};

/**
 * @class PdfDecryptor
 * @brief Provides capabilities to decrypt password-protected PDFs and save unencrypted copies.
 */
class PdfDecryptor {
public:
    /**
     * @brief Decrypts an encrypted PDF file using the provided password and saves an unencrypted copy.
     * @param srcPath Path to the encrypted source PDF.
     * @param dstPath Path where the decrypted unencrypted PDF should be written.
     * @param password User or owner password string used to unlock the document.
     * @param progressCallback Optional progress reporter callback (0.0 to 1.0).
     * @return PdfDecryptResult with operation status and statistics.
     */
    static PdfDecryptResult DecryptAndSave(
        const std::wstring& srcPath,
        const std::wstring& dstPath,
        const std::wstring& password,
        std::function<void(float progress, const std::wstring& status)> progressCallback = nullptr
    );
};

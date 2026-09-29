/**
 * @file pdf_document.hpp
 * @brief WinRT Windows.Data.Pdf document wrapper and asynchronous page prefetcher.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shcore.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <d2d1.h>

/// @brief Windows message posted when background page dimension prefetching completes.
#define WM_APP_PAGE_SIZES_READY (WM_APP + 3)

/// @brief Thread-safe shared container for asynchronous page size caching.
struct PageSizesData {
    std::mutex mutex;                   ///< Synchronizes access between prefetch thread and UI thread
    std::vector<D2D1_SIZE_F> sizes;     ///< Cached page dimensions in DIPs
    std::atomic<bool> cancel{ false };  ///< Cancellation flag signaled when document closes
};

/**
 * @class PdfDocumentWrapper
 * @brief RAII wrapper around WinRT PdfDocument providing background dimension prefetching and thread-safe page access.
 */
class PdfDocumentWrapper {
public:
    PdfDocumentWrapper();
    ~PdfDocumentWrapper();
    PdfDocumentWrapper(PdfDocumentWrapper&& other) noexcept;
    PdfDocumentWrapper& operator=(PdfDocumentWrapper&& other) noexcept;
    PdfDocumentWrapper(const PdfDocumentWrapper&) = delete;
    PdfDocumentWrapper& operator=(const PdfDocumentWrapper&) = delete;

    /**
     * @brief Opens a PDF file and starts background page dimension prefetching.
     * @param filePath Canonical absolute path to the PDF document.
     * @param hwndNotify Optional HWND to notify with WM_APP_PAGE_SIZES_READY when prefetch finishes.
     * @return true if the document was successfully opened, false otherwise.
     */
    bool Open(const std::wstring& filePath, HWND hwndNotify = nullptr);

    /**
     * @brief Closes the open document and cleanly joins the background prefetch worker thread.
     */
    void Close();

    /// @brief Returns whether a valid PDF document is loaded.
    bool IsLoaded() const { return m_loaded; }

    /// @brief Total page count of the loaded document.
    uint32_t GetPageCount() const { return m_pageCount; }

    /// @brief Full filesystem path of the loaded document.
    const std::wstring& GetFilePath() const { return m_filePath; }

    /// @brief Base filename of the loaded document for UI tab titles.
    const std::wstring& GetFileName() const { return m_fileName; }

    /**
     * @brief Retrieves a WinRT PdfPage for rendering.
     * @param pageIndex 0-based page index.
     * @return winrt::Windows::Data::Pdf::PdfPage instance.
     */
    winrt::Windows::Data::Pdf::PdfPage GetPage(uint32_t pageIndex);

    /**
     * @brief Retrieves cached or synchronous page dimension in DIPs.
     * @param pageIndex 0-based page index.
     * @return D2D1_SIZE_F dimensions (width and height).
     */
    D2D1_SIZE_F GetPageSize(uint32_t pageIndex) const;

    /// @brief Returns the underlying WinRT PdfDocument handle.
    winrt::Windows::Data::Pdf::PdfDocument GetDoc() const { return m_doc; }

private:
    winrt::Windows::Data::Pdf::PdfDocument m_doc{ nullptr };
    std::wstring m_filePath;
    std::wstring m_fileName;
    uint32_t m_pageCount = 0;
    bool m_loaded = false;

    // Cache of page dimensions to avoid querying pages on every paint
    std::shared_ptr<PageSizesData> m_sizesData;
    std::thread m_prefetchThread;
};



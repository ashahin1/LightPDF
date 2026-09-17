#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Data.Pdf.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <d2d1.h>

#define WM_APP_PAGE_SIZES_READY (WM_APP + 3)

struct PageSizesData {
    std::mutex mutex;
    std::vector<D2D1_SIZE_F> sizes;
    std::atomic<bool> cancel{ false };
};

class PdfDocumentWrapper {
public:
    PdfDocumentWrapper();
    ~PdfDocumentWrapper();

    bool Open(const std::wstring& filePath, HWND hwndNotify = nullptr);
    void Close();

    bool IsLoaded() const { return m_loaded; }
    uint32_t GetPageCount() const { return m_pageCount; }
    const std::wstring& GetFilePath() const { return m_filePath; }
    const std::wstring& GetFileName() const { return m_fileName; }

    winrt::Windows::Data::Pdf::PdfPage GetPage(uint32_t pageIndex);
    D2D1_SIZE_F GetPageSize(uint32_t pageIndex) const;
    winrt::Windows::Data::Pdf::PdfDocument GetDoc() const { return m_doc; }

private:
    winrt::Windows::Data::Pdf::PdfDocument m_doc{ nullptr };
    std::wstring m_filePath;
    std::wstring m_fileName;
    uint32_t m_pageCount = 0;
    bool m_loaded = false;

    // Cache of page dimensions to avoid querying pages on every paint
    std::shared_ptr<PageSizesData> m_sizesData;
};


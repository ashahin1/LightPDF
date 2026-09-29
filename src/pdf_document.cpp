#include "pdf_document.hpp"
#include <filesystem>

PdfDocumentWrapper::PdfDocumentWrapper() = default;

PdfDocumentWrapper::~PdfDocumentWrapper() {
    Close();
}

bool PdfDocumentWrapper::Open(const std::wstring& filePath, HWND hwndNotify) {
    Close();

    if (filePath.empty()) {
        return false;
    }

    try {
        // Resolve absolute canonical path
        wchar_t fullPath[MAX_PATH * 2] = { 0 };
        DWORD len = GetFullPathNameW(filePath.c_str(), _countof(fullPath), fullPath, nullptr);
        std::wstring resolvedPath = (len > 0 && len < _countof(fullPath)) ? fullPath : filePath;

        // Primary: Load via Win32 stream (CreateRandomAccessStreamOnFile)
        // This bypasses WinRT StorageFile restrictions on network UNC paths, Administrator elevation, and 8.3 paths.
        winrt::Windows::Data::Pdf::PdfDocument doc{ nullptr };
        try {
            winrt::Windows::Storage::Streams::IRandomAccessStream stream{ nullptr };
            HRESULT hrStream = CreateRandomAccessStreamOnFile(
                resolvedPath.c_str(),
                static_cast<DWORD>(winrt::Windows::Storage::FileAccessMode::Read),
                winrt::guid_of<winrt::Windows::Storage::Streams::IRandomAccessStream>(),
                winrt::put_abi(stream)
            );
            if (SUCCEEDED(hrStream) && stream) {
                doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(stream).get();
            }
        } catch (...) {
            doc = nullptr;
        }

        // Secondary fallback: Try StorageFile::GetFileFromPathAsync
        if (!doc) {
            try {
                auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(resolvedPath).get();
                if (file) {
                    doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
                }
            } catch (...) {
                doc = nullptr;
            }
        }

        if (!doc) {
            return false;
        }

        uint32_t pageCount = doc.PageCount();
        if (pageCount == 0) {
            return false;
        }

        // Verify that page 0 can be queried and loaded successfully
        // (prevents accepting encrypted, password-protected, or corrupted files as "loaded")
        try {
            auto testPage = doc.GetPage(0);
            if (!testPage) {
                return false;
            }
            auto sz = testPage.Size();
            if (sz.Width <= 0.0f || sz.Height <= 0.0f) {
                return false;
            }
        } catch (...) {
            return false;
        }

        m_doc = doc;
        m_pageCount = pageCount;
        m_filePath = resolvedPath;

        // Extract filename
        size_t lastSlash = resolvedPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            m_fileName = resolvedPath.substr(lastSlash + 1);
        } else {
            m_fileName = resolvedPath;
        }

        m_sizesData = std::make_shared<PageSizesData>();
        m_sizesData->sizes.assign(m_pageCount, D2D1::SizeF(0.0f, 0.0f));
        m_loaded = true;

        // Pre-query page 0 size synchronously so layout can immediately use it as baseline
        if (m_pageCount > 0) {
            GetPageSize(0);
        }

        // Launch background thread to prefetch remaining page sizes without blocking the UI thread
        if (m_pageCount > 1) {
            auto data = m_sizesData;
            auto doc = m_doc;
            uint32_t count = m_pageCount;
            D2D1_SIZE_F p0 = D2D1::SizeF(0.0f, 0.0f);
            {
                std::lock_guard<std::mutex> lock(data->mutex);
                if (!data->sizes.empty()) p0 = data->sizes[0];
            }

            std::thread([data, doc, count, p0, hwndNotify]() {
                bool anyDiffer = false;
                for (uint32_t i = 1; i < count; ++i) {
                    if (data->cancel.load()) break;

                    bool alreadyKnown = false;
                    {
                        std::lock_guard<std::mutex> lock(data->mutex);
                        if (data->sizes[i].width > 0.0f && data->sizes[i].height > 0.0f) {
                            alreadyKnown = true;
                        }
                    }

                    if (!alreadyKnown) {
                        try {
                            auto page = doc.GetPage(i);
                            if (page) {
                                auto sz = page.Size();
                                D2D1_SIZE_F size = D2D1::SizeF(sz.Width, sz.Height);
                                {
                                    std::lock_guard<std::mutex> lock(data->mutex);
                                    data->sizes[i] = size;
                                }
                                if (std::abs(size.width - p0.width) > 0.5f || std::abs(size.height - p0.height) > 0.5f) {
                                    anyDiffer = true;
                                }
                            }
                        } catch (...) {}
                    }
                }

                if (anyDiffer && !data->cancel.load() && hwndNotify && IsWindow(hwndNotify)) {
                    PostMessageW(hwndNotify, WM_APP_PAGE_SIZES_READY, 0, 0);
                }
            }).detach();
        }

        return true;
    } catch (...) {
        Close();
        return false;
    }
}

void PdfDocumentWrapper::Close() {
    if (m_sizesData) {
        m_sizesData->cancel.store(true);
        m_sizesData = nullptr;
    }
    m_doc = nullptr;
    m_pageCount = 0;
    m_loaded = false;
    m_filePath.clear();
    m_fileName.clear();
}

winrt::Windows::Data::Pdf::PdfPage PdfDocumentWrapper::GetPage(uint32_t pageIndex) {
    if (!m_loaded || pageIndex >= m_pageCount) {
        return nullptr;
    }
    try {
        auto page = m_doc.GetPage(pageIndex);
        if (page && m_sizesData) {
            auto size = page.Size();
            std::lock_guard<std::mutex> lock(m_sizesData->mutex);
            if (pageIndex < m_sizesData->sizes.size()) {
                m_sizesData->sizes[pageIndex] = D2D1::SizeF(size.Width, size.Height);
            }
        }
        return page;
    } catch (...) {
        return nullptr;
    }
}

D2D1_SIZE_F PdfDocumentWrapper::GetPageSize(uint32_t pageIndex) const {
    if (!m_loaded || !m_sizesData || pageIndex >= m_pageCount) {
        return D2D1::SizeF(0.0f, 0.0f);
    }

    {
        std::lock_guard<std::mutex> lock(m_sizesData->mutex);
        if (m_sizesData->sizes[pageIndex].width > 0.0f && m_sizesData->sizes[pageIndex].height > 0.0f) {
            return m_sizesData->sizes[pageIndex];
        }

        // Fast path heuristic: If page 0 is known, return page 0 size immediately so continuous
        // scroll offset calculation and fit-width layout do not freeze the UI thread on load.
        if (pageIndex > 0 && m_sizesData->sizes[0].width > 0.0f && m_sizesData->sizes[0].height > 0.0f) {
            return m_sizesData->sizes[0];
        }
    }

    try {
        auto page = m_doc.GetPage(pageIndex);
        if (page) {
            auto size = page.Size();
            D2D1_SIZE_F s = D2D1::SizeF(size.Width, size.Height);
            std::lock_guard<std::mutex> lock(m_sizesData->mutex);
            m_sizesData->sizes[pageIndex] = s;
            return s;
        }
    } catch (...) {}

    return D2D1::SizeF(0.0f, 0.0f);
}

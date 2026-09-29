#include "pdf_document.hpp"
#include <filesystem>

PdfDocumentWrapper::PdfDocumentWrapper() = default;

PdfDocumentWrapper::~PdfDocumentWrapper() {
    Close();
}

PdfDocumentWrapper::PdfDocumentWrapper(PdfDocumentWrapper&& other) noexcept
    : m_doc(std::move(other.m_doc))
    , m_filePath(std::move(other.m_filePath))
    , m_fileName(std::move(other.m_fileName))
    , m_pageCount(other.m_pageCount)
    , m_loaded(other.m_loaded)
    , m_sizesData(std::move(other.m_sizesData))
    , m_prefetchThread(std::move(other.m_prefetchThread))
{
    other.m_pageCount = 0;
    other.m_loaded = false;
}

PdfDocumentWrapper& PdfDocumentWrapper::operator=(PdfDocumentWrapper&& other) noexcept {
    if (this != &other) {
        Close();
        m_doc = std::move(other.m_doc);
        m_filePath = std::move(other.m_filePath);
        m_fileName = std::move(other.m_fileName);
        m_pageCount = other.m_pageCount;
        m_loaded = other.m_loaded;
        m_sizesData = std::move(other.m_sizesData);
        m_prefetchThread = std::move(other.m_prefetchThread);
        other.m_pageCount = 0;
        other.m_loaded = false;
    }
    return *this;
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
        } catch (const winrt::hresult_error& ex) {
            OutputDebugStringW((L"[LightPDF] Stream load failed: " + std::wstring(ex.message()) + L"\n").c_str());
            doc = nullptr;
        } catch (...) {
            OutputDebugStringW(L"[LightPDF] Stream load failed with unknown exception\n");
            doc = nullptr;
        }

        // Secondary fallback: Try StorageFile::GetFileFromPathAsync
        if (!doc) {
            try {
                auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(resolvedPath).get();
                if (file) {
                    doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
                }
            } catch (const winrt::hresult_error& ex) {
                OutputDebugStringW((L"[LightPDF] StorageFile fallback failed: " + std::wstring(ex.message()) + L"\n").c_str());
                doc = nullptr;
            } catch (...) {
                OutputDebugStringW(L"[LightPDF] StorageFile fallback failed with unknown exception\n");
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
        } catch (const winrt::hresult_error& ex) {
            OutputDebugStringW((L"[LightPDF] Page 0 verification failed: " + std::wstring(ex.message()) + L"\n").c_str());
            return false;
        } catch (...) {
            OutputDebugStringW(L"[LightPDF] Page 0 verification failed: unknown exception\n");
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
            auto docInst = m_doc;
            uint32_t count = m_pageCount;
            D2D1_SIZE_F p0 = D2D1::SizeF(0.0f, 0.0f);
            {
                std::lock_guard<std::mutex> lock(data->mutex);
                if (!data->sizes.empty()) p0 = data->sizes[0];
            }

            if (m_prefetchThread.joinable()) {
                m_prefetchThread.join();
            }

            m_prefetchThread = std::thread([data, docInst, count, p0, hwndNotify]() {
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
                            auto page = docInst.GetPage(i);
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
                        } catch (const winrt::hresult_error& ex) {
                            OutputDebugStringW((L"[LightPDF] Page size prefetch failed: " + std::wstring(ex.message()) + L"\n").c_str());
                        } catch (...) {}
                    }
                }

                if (anyDiffer && !data->cancel.load() && hwndNotify && IsWindow(hwndNotify)) {
                    PostMessageW(hwndNotify, WM_APP_PAGE_SIZES_READY, 0, 0);
                }
            });
        }

        return true;
    } catch (const winrt::hresult_error& ex) {
        OutputDebugStringW((L"[LightPDF] PdfDocumentWrapper::Open failed: " + std::wstring(ex.message()) + L"\n").c_str());
        Close();
        return false;
    } catch (...) {
        OutputDebugStringW(L"[LightPDF] PdfDocumentWrapper::Open failed with unknown exception\n");
        Close();
        return false;
    }
}

void PdfDocumentWrapper::Close() {
    if (m_sizesData) {
        m_sizesData->cancel.store(true);
    }
    if (m_prefetchThread.joinable()) {
        m_prefetchThread.join();
    }
    m_sizesData = nullptr;
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
    } catch (const winrt::hresult_error& ex) {
        OutputDebugStringW((L"[LightPDF] GetPage failed: " + std::wstring(ex.message()) + L"\n").c_str());
        return nullptr;
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
    } catch (const winrt::hresult_error& ex) {
        OutputDebugStringW((L"[LightPDF] GetPageSize failed: " + std::wstring(ex.message()) + L"\n").c_str());
    } catch (...) {}

    return D2D1::SizeF(0.0f, 0.0f);
}

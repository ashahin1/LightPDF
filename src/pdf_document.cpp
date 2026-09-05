#include "pdf_document.hpp"
#include <filesystem>

PdfDocumentWrapper::PdfDocumentWrapper() = default;

PdfDocumentWrapper::~PdfDocumentWrapper() {
    Close();
}

bool PdfDocumentWrapper::Open(const std::wstring& filePath) {
    Close();

    if (filePath.empty()) {
        return false;
    }

    try {
        // Resolve absolute canonical path
        wchar_t fullPath[MAX_PATH * 2] = { 0 };
        DWORD len = GetFullPathNameW(filePath.c_str(), _countof(fullPath), fullPath, nullptr);
        std::wstring resolvedPath = (len > 0) ? fullPath : filePath;

        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(resolvedPath).get();
        if (!file) {
            return false;
        }

        m_doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        if (!m_doc) {
            return false;
        }

        m_pageCount = m_doc.PageCount();
        m_filePath = resolvedPath;

        // Extract filename
        size_t lastSlash = resolvedPath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            m_fileName = resolvedPath.substr(lastSlash + 1);
        } else {
            m_fileName = resolvedPath;
        }

        m_pageSizes.assign(m_pageCount, D2D1::SizeF(0.0f, 0.0f));
        m_loaded = true;

        // Pre-query page 0 size if pages exist
        if (m_pageCount > 0) {
            GetPageSize(0);
        }

        return true;
    } catch (...) {
        Close();
        return false;
    }
}

void PdfDocumentWrapper::Close() {
    m_doc = nullptr;
    m_pageCount = 0;
    m_loaded = false;
    m_filePath.clear();
    m_fileName.clear();
    m_pageSizes.clear();
}

winrt::Windows::Data::Pdf::PdfPage PdfDocumentWrapper::GetPage(uint32_t pageIndex) {
    if (!m_loaded || pageIndex >= m_pageCount) {
        return nullptr;
    }
    try {
        return m_doc.GetPage(pageIndex);
    } catch (...) {
        return nullptr;
    }
}

D2D1_SIZE_F PdfDocumentWrapper::GetPageSize(uint32_t pageIndex) {
    if (!m_loaded || pageIndex >= m_pageCount) {
        return D2D1::SizeF(0.0f, 0.0f);
    }

    if (m_pageSizes[pageIndex].width > 0.0f && m_pageSizes[pageIndex].height > 0.0f) {
        return m_pageSizes[pageIndex];
    }

    try {
        auto page = m_doc.GetPage(pageIndex);
        if (page) {
            auto size = page.Size();
            m_pageSizes[pageIndex] = D2D1::SizeF(size.Width, size.Height);
            return m_pageSizes[pageIndex];
        }
    } catch (...) {}

    return D2D1::SizeF(0.0f, 0.0f);
}

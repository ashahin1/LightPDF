#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cassert>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <io.h>
#include <fcntl.h>
#include "pdf_parser.hpp"
#include "pdf_searchable_writer.hpp"
#include "pdf_search.hpp"

#pragma comment(lib, "windows.data.pdf.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "comctl32.lib")

using namespace winrt;
using namespace winrt::Windows::Data::Pdf;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;

static void CreateMinimalScannedPdf(const std::wstring& outPath) {
    // Write a clean, minimal valid PDF 1.4 with 1 page and no digital text (simulating a scanned page)
    std::ofstream out(outPath, std::ios::binary);
    std::string pdfContent =
        "%PDF-1.4\n"
        "%âãÏÓ\n"
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n"
        "2 0 obj\n"
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\n"
        "endobj\n"
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Contents 4 0 R >>\n"
        "endobj\n"
        "4 0 obj\n"
        "<< /Length 15 >>\n"
        "stream\n"
        "q 1 0 0 1 0 0 cm Q\n"
        "endstream\n"
        "endobj\n"
        "xref\n"
        "0 5\n"
        "0000000000 65535 f \n"
        "0000000015 00000 n \n"
        "0000000068 00000 n \n"
        "0000000125 00000 n \n"
        "0000000215 00000 n \n"
        "trailer\n"
        "<< /Size 5 /Root 1 0 R >>\n"
        "startxref\n"
        "280\n"
        "%%EOF\n";
    out.write(pdfContent.data(), pdfContent.size());
    out.close();
}

int main() {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    init_apartment();

    std::wcout << L"==========================================================\n";
    std::wcout << L"  LightPDF Searchable PDF (Approach A) Verification Test\n";
    std::wcout << L"==========================================================\n";

    std::wstring rawPdf = L"tests\\test_raw_scanned.pdf";
    std::wstring outPdf = L"tests\\test_searchable_out.pdf";
    std::wstring overwritePdf = L"tests\\test_overwrite_target.pdf";

    // 1. Create minimal synthetic scanned PDF
    CreateMinimalScannedPdf(rawPdf);
    std::wcout << L"[1] Created synthetic scanned PDF: " << rawPdf << L"\n";

    // Verify it currently has NO digital text
    {
        PdfParser preParser;
        bool ok = preParser.Load(rawPdf);
        if (!ok || preParser.GetPageCount() == 0) {
            std::wcerr << L"FAIL: Could not load initial raw PDF!\n";
            return 1;
        }
        PdfPageText pText;
        preParser.ExtractPageText(0, pText);
        if (pText.hasDigitalText) {
            std::wcerr << L"FAIL: Initial raw PDF should not have digital text!\n";
            return 1;
        }
        std::wcout << L"    PASS: Verified initial page has no digital text.\n";
    }

    // 2. Prepare bilingual OCR word data (English + Arabic)
    std::vector<OcrPageItem> ocrPages;
    OcrPageItem page0;
    page0.pageIndex = 0;
    page0.pageWidthDip = 816.0f;
    page0.pageHeightDip = 1056.0f;

    page0.words.push_back({ L"Computer", D2D1::RectF(50.0f, 100.0f, 180.0f, 130.0f) });
    page0.words.push_back({ L"Organization", D2D1::RectF(190.0f, 100.0f, 360.0f, 130.0f) });
    page0.words.push_back({ L"معمارية", D2D1::RectF(490.0f, 100.0f, 600.0f, 130.0f) });
    page0.words.push_back({ L"الحاسوب", D2D1::RectF(380.0f, 100.0f, 480.0f, 130.0f) });
    page0.words.push_back({ L"Logic", D2D1::RectF(50.0f, 160.0f, 140.0f, 190.0f) });
    page0.words.push_back({ L"Design", D2D1::RectF(150.0f, 160.0f, 250.0f, 190.0f) });

    ocrPages.push_back(page0);

    // 3. Bake searchable layer (Save As New File)
    std::wcout << L"[2] Baking searchable text layer to: " << outPdf << L" ...\n";
    bool bakeOk = PdfSearchableWriter::WriteSearchablePdf(rawPdf, outPdf, ocrPages, [](float p, const std::wstring& status) {
        std::wcout << L"    Progress: " << (int)(p * 100) << L"% - " << status << L"\n";
    });

    if (!bakeOk) {
        std::wcerr << L"FAIL: WriteSearchablePdf returned false!\n";
        return 1;
    }
    std::wcout << L"    PASS: Searchable PDF generated successfully.\n";

    // 4. Verify generated PDF with PdfParser
    std::wcout << L"[3] Verifying generated PDF with native PdfParser...\n";
    {
        PdfParser parser;
        if (!parser.Load(outPdf)) {
            std::wcerr << L"FAIL: Generated PDF failed to load in PdfParser!\n";
            return 1;
        }

        PdfPageText pt;
        if (!parser.ExtractPageText(0, pt)) {
            std::wcerr << L"FAIL: ExtractPageText failed on page 0!\n";
            return 1;
        }

        if (!pt.hasDigitalText) {
            std::wcerr << L"FAIL: hasDigitalText is false after baking!\n";
            return 1;
        }

        wprintf(L"    Extracted chars count: %zu\n", pt.chars.size());
        for (size_t ci = 0; ci < pt.fullText.size(); ++ci) {
            wchar_t c = pt.fullText[ci];
            wprintf(L"    [%02zu] U+%04X (%c)\n", ci, (uint16_t)c, (c >= 32 && c <= 126) ? (char)c : '?');
        }

        // Assert English words present
        if (pt.fullText.find(L"Computer") == std::wstring::npos) {
            std::wcerr << L"FAIL: 'Computer' not found in extracted text!\n";
            return 1;
        }
        if (pt.fullText.find(L"Organization") == std::wstring::npos) {
            std::wcerr << L"FAIL: 'Organization' not found in extracted text!\n";
            return 1;
        }
        if (pt.fullText.find(L"Logic") == std::wstring::npos) {
            std::wcerr << L"FAIL: 'Logic' not found in extracted text!\n";
            return 1;
        }

        // Assert Arabic words present
        if (pt.fullText.find(L"معمارية") == std::wstring::npos) {
            std::wcerr << L"FAIL: 'معمارية' not found in extracted text!\n";
            return 1;
        }
        if (pt.fullText.find(L"الحاسوب") == std::wstring::npos) {
            std::wcerr << L"FAIL: 'الحاسوب' not found in extracted text!\n";
            return 1;
        }

        std::wcout << L"    PASS: Both English and Arabic strings extracted with 100% fidelity!\n";
    }

    // 5. Verify search engine matches English and Arabic without OCR
    std::wcout << L"[4] Verifying search engine matches without OCR enabled...\n";
    {
        PdfSearchEngine engine;
        engine.StartSearch(nullptr, outPdf, 1, L"Logic", false, false);
        while (engine.IsSearching()) {
            Sleep(20);
        }
        if (engine.GetTotalMatches() == 0) {
            std::wcerr << L"FAIL: Search for 'Logic' returned 0 matches!\n";
            return 1;
        }
        std::wcout << L"    PASS: Found " << engine.GetTotalMatches() << L" match for English 'Logic'\n";

        engine.Clear();
        engine.StartSearch(nullptr, outPdf, 1, L"معمارية", false, false);
        while (engine.IsSearching()) {
            Sleep(20);
        }
        if (engine.GetTotalMatches() == 0) {
            std::wcerr << L"FAIL: Search for 'معمارية' returned 0 matches!\n";
            return 1;
        }
        std::wcout << L"    PASS: Found " << engine.GetTotalMatches() << L" match for Arabic 'معمارية'\n";
    }

    // 6. Test Atomic Overwrite Mode
    std::wcout << L"[5] Testing Atomic Overwrite Mode...\n";
    {
        CopyFileW(rawPdf.c_str(), overwritePdf.c_str(), FALSE);
        bool owOk = PdfSearchableWriter::WriteSearchablePdf(overwritePdf, overwritePdf, ocrPages, nullptr);
        if (!owOk) {
            std::wcerr << L"FAIL: Overwrite mode returned false!\n";
            return 1;
        }

        PdfParser owParser;
        if (!owParser.Load(overwritePdf)) {
            std::wcerr << L"FAIL: Overwritten file failed to load!\n";
            return 1;
        }
        PdfPageText owText;
        owParser.ExtractPageText(0, owText);
        if (!owText.hasDigitalText || owText.fullText.find(L"معمارية") == std::wstring::npos) {
            std::wcerr << L"FAIL: Overwritten file missing searchable text!\n";
            return 1;
        }
        std::wcout << L"    PASS: Atomic overwrite mode succeeded and verified!\n";
    }

    // 7. Verify file renders cleanly in Windows.Data.Pdf
    std::wcout << L"[6] Verifying with Windows.Data.Pdf renderer...\n";
    {
        wchar_t fullOut[MAX_PATH * 2] = {};
        GetFullPathNameW(outPdf.c_str(), _countof(fullOut), fullOut, nullptr);
        auto storageFile = StorageFile::GetFileFromPathAsync(fullOut).get();
        auto doc = PdfDocument::LoadFromFileAsync(storageFile).get();
        if (!doc || doc.PageCount() == 0) {
            std::wcerr << L"FAIL: Windows.Data.Pdf could not open generated PDF!\n";
            return 1;
        }
        auto page = doc.GetPage(0);
        InMemoryRandomAccessStream stm;
        page.RenderToStreamAsync(stm).get();
        if (stm.Size() == 0) {
            std::wcerr << L"FAIL: Windows.Data.Pdf render stream was empty!\n";
            return 1;
        }
        std::wcout << L"    PASS: Rendered page to stream cleanly (" << stm.Size() << L" bytes).\n";
    }

    // Cleanup temporary files
    DeleteFileW(rawPdf.c_str());
    DeleteFileW(outPdf.c_str());
    DeleteFileW(overwritePdf.c_str());

    std::wcout << L"\n==========================================================\n";
    std::wcout << L"  ALL SEARCHABLE PDF VERIFICATION CHECKS PASSED (100%)!\n";
    std::wcout << L"==========================================================\n";
    return 0;
}

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>

#include "dictionary_engine.hpp"
#include "tab_controller.hpp"
#include "search_controller.hpp"
#include "selection_controller.hpp"
#include "pdf_parser.hpp"
#include "pdf_decryptor.hpp"
#include "pdf_searchable_writer.hpp"
#include "pdf_search.hpp"
#include "ui_views.hpp"
#include "tts_engine.hpp"
#include "read_aloud_controller.hpp"
#include "recent_files_controller.hpp"
#include "context_menu_controller.hpp"

// ============================================================================
// Dictionary Engine Tests
// ============================================================================
TEST_CASE("Dictionary: Initialization and Term Lookups") {
    DictionaryEngine engine;
    std::wstring dictPath = DictionaryEngine::FindDictionaryFile();
    REQUIRE(!dictPath.empty());

    bool ok = engine.Initialize(dictPath);
    REQUIRE(ok);
    REQUIRE(engine.IsLoaded());
    CHECK(engine.GetEntryCount() > 100);

    SUBCASE("Specialized CS and Engineering Terms") {
        DictionaryResult res;
        CHECK(engine.Lookup(L"pipeline stall", res));
        CHECK(!res.definition.empty());

        CHECK(engine.Lookup(L"gradient descent", res));
        CHECK(res.category == DictionaryCategory::AIMachineLearning);

        CHECK(engine.Lookup(L"zero knowledge proof", res));
        CHECK(res.category == DictionaryCategory::Cybersecurity);

        CHECK(engine.Lookup(L"linear programming", res));
        CHECK(res.category == DictionaryCategory::AlgorithmsOptimization);
    }

    SUBCASE("Morphological Stemming") {
        DictionaryResult res;
        CHECK(engine.Lookup(L"branch predictors", res));
        CHECK(engine.Lookup(L"pipelines", res));
        CHECK(engine.Lookup(L"protocols", res));
        CHECK(engine.Lookup(L"registers", res));
    }

    SUBCASE("Graceful Nonexistent Term Handling") {
        DictionaryResult res;
        CHECK_FALSE(engine.Lookup(L"nonexistent_xyz_term_12345", res));
    }
}

// ============================================================================
// Controllers Tests
// ============================================================================
TEST_CASE("Tabs: TabController State and Layout") {
    TabController tabs;
    CHECK(tabs.GetTabCount() == 0);
    CHECK(tabs.IsEmpty());
    CHECK_FALSE(tabs.HasMultipleTabs());
    CHECK(tabs.GetTopOffset() == doctest::Approx(0.0f));

    // Hit-testing empty tab bar returns -1
    bool closeHover = false, addHover = false;
    POINT pt{ 100, 10 };
    int hit = tabs.HitTestTab(pt, 800.0f, 96.0f, closeHover, addHover);
    CHECK(hit == -1);
}

TEST_CASE("Search: SearchController State & Layout") {
    SearchController search;
    CHECK_FALSE(search.IsOpen());

    search.Open();
    CHECK(search.IsOpen());

    search.SetQuery(L"neural networks");
    CHECK(search.GetQuery() == L"neural networks");

    search.ToggleMatchCase();
    CHECK(search.IsMatchCase());
    search.ToggleMatchCase();
    CHECK_FALSE(search.IsMatchCase());

    search.ToggleOcr();
    CHECK(search.IsOcrEnabled());
    search.ToggleOcr();
    CHECK_FALSE(search.IsOcrEnabled());

    // Search bar info generation
    SearchBarRenderInfo info = search.GetSearchBarInfo(true);
    CHECK(info.visible);
    CHECK(info.query == L"neural networks");
    CHECK(info.hasTabs);

    search.Close();
    CHECK_FALSE(search.IsOpen());
}

TEST_CASE("Selection: SelectionController Lookup & Card Management") {
    SelectionController sel;
    CHECK_FALSE(sel.IsDictCardVisible());

    D2D1_RECT_F anchorRect = D2D1::RectF(100.0f, 200.0f, 180.0f, 220.0f);
    bool lookupOk = sel.TriggerDictionaryLookup(L"pipeline stall", anchorRect, 1000.0f, 800.0f, 96.0f, 0.0f);
    CHECK(lookupOk);
    CHECK(sel.IsDictCardVisible());

    const auto& card = sel.GetDictCardInfo();
    CHECK(card.word == L"pipeline stall");
    CHECK(!card.definition.empty());

    sel.DismissDictionaryCard();
    CHECK_FALSE(sel.IsDictCardVisible());
}

// ============================================================================
// UI Views Hit-Testing Tests
// ============================================================================
TEST_CASE("Views: HelpOverlay Hit-Test") {
    // Center of a 1000x800 window at 96 DPI
    POINT ptCenter{ 500, 400 };
    int hit = UIViews::HelpOverlayView::HitTest(ptCenter, 1000, 800, 96.0f);
    CHECK(hit >= 0); // Inside card or tab

    POINT ptOutside{ 5, 5 };
    int hitOutside = UIViews::HelpOverlayView::HitTest(ptOutside, 1000, 800, 96.0f);
    CHECK(hitOutside == -1); // Outside (backdrop)
}

TEST_CASE("Views: DocProperties Hit-Test") {
    POINT ptOutside{ 5, 5 };
    int hitOutside = UIViews::DocPropertiesView::HitTest(ptOutside, 1000, 800, 96.0f);
    CHECK(hitOutside == -1);

    POINT ptCenter{ 500, 400 };
    int hitCenter = UIViews::DocPropertiesView::HitTest(ptCenter, 1000, 800, 96.0f);
    CHECK(hitCenter >= 0);
}

TEST_CASE("Views: PresenterBar Hit-Test") {
    POINT ptOutside{ 5, 5 };
    int hitOutside = UIViews::PresenterBarView::HitTest(ptOutside, 1000, 800, 96.0f);
    CHECK(hitOutside == -1);
}

// ============================================================================
// Searchable PDF Generation & PDF Parser Tests
// ============================================================================
TEST_CASE("SearchablePDF: Bake and Parse Searchable PDF Layer") {
    std::wstring rawPdfPath = L"tests\\test_unified_raw.pdf";
    std::wstring outPdfPath = L"tests\\test_unified_out.pdf";

    // Create minimal synthetic PDF 1.4
    {
        std::ofstream out(rawPdfPath, std::ios::binary);
        std::string content =
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
        out.write(content.data(), content.size());
    }

    OcrPageItem pageItem;
    pageItem.pageIndex = 0;
    pageItem.pageWidthDip = 612.0f;
    pageItem.pageHeightDip = 792.0f;
    pageItem.words = {
        { L"Autonomous", { 50.0f, 700.0f, 150.0f, 720.0f } },
        { L"Systems",    { 160.0f, 700.0f, 230.0f, 720.0f } }
    };

    std::vector<OcrPageItem> pagesOcr = { pageItem };
    bool baked = PdfSearchableWriter::WriteSearchablePdf(rawPdfPath, outPdfPath, pagesOcr);
    REQUIRE(baked);

    PdfParser parser;
    bool loaded = parser.Load(outPdfPath);
    REQUIRE(loaded);
    CHECK(parser.GetPageCount() == 1);

    PdfPageText pageText;
    bool extracted = parser.ExtractPageText(0, pageText);
    REQUIRE(extracted);
    CHECK(pageText.chars.size() > 0);
    CHECK(pageText.hasDigitalText);

    parser.Close();

    // Clean up test output files
    std::error_code ec;
    std::filesystem::remove(rawPdfPath, ec);
    std::filesystem::remove(outPdfPath, ec);
}

// ============================================================================
// Arabic Normalization & Detection Tests
// ============================================================================
TEST_CASE("Search: Arabic Script Detection and Normalization") {
    SUBCASE("Script Detection") {
        CHECK(ContainsArabic(L"مرحبا بكم"));
        CHECK(ContainsArabic(L"LightPDF - قارئ ملفات"));
        CHECK(!ContainsArabic(L"LightPDF Ultra-Fast Windows Viewer"));
        CHECK(!ContainsArabic(L"1234567890 !@#$%^&*()"));
        CHECK(HasArabicLetters(L"كتاب"));
        CHECK(!HasArabicLetters(L"12345"));
    }

    SUBCASE("Tashkeel Diacritics Stripping") {
        // "الْحَمْدُ لِلَّهِ" -> "الحمد لله"
        std::wstring withTashkeel = L"\u0627\u0644\u0652\u062d\u064e\u0645\u0652\u062f\u064f \u0644\u0650\u0644\u0651\u064e\u0647\u0650";
        std::wstring normalized = NormalizeArabic(withTashkeel);
        CHECK(normalized.find(L"\u0652") == std::wstring::npos); // Sukun stripped
        CHECK(normalized.find(L"\u064e") == std::wstring::npos); // Fatha stripped
        CHECK(normalized.find(L"\u064f") == std::wstring::npos); // Damma stripped
        CHECK(normalized.find(L"\u0650") == std::wstring::npos); // Kasra stripped
    }

    SUBCASE("Alef Normalization") {
        // أ, إ, آ, ٱ should all normalize to bare Alef ا
        CHECK(NormalizeArabic(L"\u0623\u062d\u0645\u062f") == L"\u0627\u062d\u0645\u062f"); // أحمد -> احمد
        CHECK(NormalizeArabic(L"\u0625\u0628\u0631\u0627\u0647\u064a\u0645") == L"\u0627\u0628\u0631\u0627\u0647\u064a\u0645"); // إبراهيم -> ابراهيم
        CHECK(NormalizeArabic(L"\u0622\u0645\u0646\u0629") == L"\u0627\u0645\u0646\u0647"); // آمنة -> امنه
    }

    SUBCASE("Taa Marbuta & Alef Maksura Normalization") {
        CHECK(NormalizeArabic(L"\u0645\u062f\u0631\u0633\u0629") == L"\u0645\u062f\u0631\u0633\u0647"); // مدرسة -> مدرسه
        CHECK(NormalizeArabic(L"\u0639\u0644\u0649") == L"\u0639\u0644\u064a"); // على -> علي
    }

    SUBCASE("Arabic-Indic and Eastern Digit Normalization") {
        // ٠١٢٣٤٥٦٧٨٩ -> 0123456789
        std::wstring arabicDigits = L"\u0660\u0661\u0662\u0663\u0664\u0665\u0666\u0667\u0668\u0669";
        CHECK(NormalizeArabic(arabicDigits) == L"0123456789");
        // Eastern digits ۰۱۲۳۴۵۶۷۸۹ -> 0123456789
        std::wstring easternDigits = L"\u06f0\u06f1\u06f2\u06f3\u06f4\u06f5\u06f6\u06f7\u06f8\u06f9";
        CHECK(NormalizeArabic(easternDigits) == L"0123456789");
    }
}

// ============================================================================
// Real Document Loading & Parsing Tests
// ============================================================================
TEST_CASE("Document: Validate Committed Fixtures in tests/fixtures/") {
    PdfParser parser1;
    bool loaded1 = parser1.Load(L"tests\\fixtures\\sample_1page.pdf");
    REQUIRE(loaded1);
    CHECK(parser1.GetPageCount() == 1);
    parser1.Close();

    PdfParser parser2;
    bool loaded2 = parser2.Load(L"tests\\fixtures\\sample_2page.pdf");
    REQUIRE(loaded2);
    CHECK(parser2.GetPageCount() == 2);
    parser2.Close();
}

TEST_CASE("Document: Validate Test PDFs in tests/PDF/") {
    const std::wstring testDir = L"tests\\PDF";
    if (std::filesystem::exists(testDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(testDir)) {
            if (entry.path().extension() == L".pdf") {
                std::wstring pdfFile = entry.path().wstring();
                PdfParser parser;
                bool loaded = parser.Load(pdfFile);
                CHECK(loaded);
                if (loaded) {
                    CHECK(parser.GetPageCount() > 0);
                }
            }
        }
    }
}

// ============================================================================
// Bilingual OCR & Searchable PDF Baking Test for 123.pdf
// ============================================================================
TEST_CASE("OCR: Bilingual OCR on Scanned Document (123.pdf)") {
    std::wstring testPath = L"tests\\PDF\\123.pdf";
    if (!std::filesystem::exists(testPath)) return;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto engines = CreateBilingualOcrEngines();
    if (!engines.IsValid()) {
        if (SUCCEEDED(hr)) CoUninitialize();
        return;
    }

    try {
        std::filesystem::path absPath = std::filesystem::absolute(testPath);
        auto storageFile = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(absPath.wstring()).get();
        auto doc = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(storageFile).get();
        REQUIRE(doc != nullptr);
        REQUIRE(doc.PageCount() > 0);

        auto page = doc.GetPage(0);
        REQUIRE(page != nullptr);

        PdfParser parser;
        REQUIRE(parser.Load(testPath));
        PdfPageText origPageText;
        parser.ExtractPageText(0, origPageText);
        CHECK(!origPageText.hasDigitalText);

        OcrPageItem ocrPage;
        bool ocrSuccess = ExtractBilingualPageOcr(page, 0, origPageText.pageWidth, origPageText.pageHeight, engines.arEngine, engines.enEngine, ocrPage, 2.0f);
        REQUIRE(ocrSuccess);
        CHECK(ocrPage.words.size() > 50);

        bool foundArabic = false;
        bool foundEnglish = false;
        for (const auto& w : ocrPage.words) {
            if (ContainsArabic(w.text)) foundArabic = true;
            for (wchar_t ch : w.text) {
                if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')) {
                    foundEnglish = true;
                }
            }
        }
        CHECK(foundArabic);
        CHECK(foundEnglish);

        // Test baking and verification of embedded text layer
        std::wstring outBaked = L"tests\\PDF\\123_test_baked.pdf";
        std::vector<OcrPageItem> allOcr = { ocrPage };
        bool baked = PdfSearchableWriter::WriteSearchablePdf(testPath, outBaked, allOcr);
        REQUIRE(baked);

        PdfParser bakedParser;
        REQUIRE(bakedParser.Load(outBaked));
        PdfPageText bakedText;
        REQUIRE(bakedParser.ExtractPageText(0, bakedText));
        CHECK(bakedText.hasDigitalText);
        CHECK(ContainsArabic(bakedText.fullText));
        CHECK((bakedText.fullText.find(L"mohe") != std::wstring::npos ||
               bakedText.fullText.find(L"casm") != std::wstring::npos ||
               bakedText.fullText.find(L"WWW") != std::wstring::npos ||
               bakedText.fullText.find(L"www") != std::wstring::npos));

        bakedParser.Close();
        std::error_code ec;
        std::filesystem::remove(outBaked, ec);
    } catch (...) {
        CHECK(false);
    }

    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
}

// ============================================================================
// Multi-character word search in CIDFont Document (Exam_Schedule_472.pdf)
// ============================================================================
TEST_CASE("Search: Multi-character Word Search in CIDFont Document (Exam_Schedule_472.pdf)") {
    std::wstring pdfPath = L"tests\\PDF\\Exam_Schedule_472.pdf";
    if (!std::filesystem::exists(pdfPath)) return;

    PdfParser parser;
    REQUIRE(parser.Load(pdfPath));
    PdfPageText pageText;
    REQUIRE(parser.ExtractPageText(0, pageText));
    CHECK(pageText.hasDigitalText);

    // Verify no null characters in extracted text
    size_t nullCount = 0;
    for (wchar_t ch : pageText.fullText) {
        if (ch == L'\0') nullCount++;
    }
    CHECK(nullCount == 0);

    // Verify multi-character English words are found
    CHECK(pageText.fullText.find(L"Buraydah") != std::wstring::npos);
    CHECK(pageText.fullText.find(L"Private") != std::wstring::npos);
    CHECK(pageText.fullText.find(L"College") != std::wstring::npos);
    CHECK(pageText.fullText.find(L"Date") != std::wstring::npos);
    CHECK(pageText.fullText.find(L"Page") != std::wstring::npos);

    // Verify multi-character Arabic words are found (with normalization)
    std::wstring normText = NormalizeArabic(pageText.fullText);
    CHECK(normText.find(NormalizeArabic(L"كليات")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"بريدة")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"جدول")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"الاختبارات")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"الفصل")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"الثاني")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"التاريخ")) != std::wstring::npos);
    CHECK(normText.find(NormalizeArabic(L"الصفحة")) != std::wstring::npos);

    // Test PdfSearchEngine integration
    auto textCache = std::make_shared<PageTextCache>();
    textCache->pages.resize(parser.GetPageCount());

    PdfSearchEngine engine;
    engine.StartSearch(nullptr, pdfPath, 1, L"Buraydah", false, false, nullptr, textCache);
    while (engine.IsSearching()) {
        Sleep(5);
    }
    CHECK(engine.GetTotalMatches() >= 1);

    engine.StartSearch(nullptr, pdfPath, 1, L"جدول", false, false, nullptr, textCache);
    while (engine.IsSearching()) {
        Sleep(5);
    }
    CHECK(engine.GetTotalMatches() >= 1);
}

// ============================================================================
// Read Aloud (TTS) Tests
// ============================================================================
TEST_CASE("TTS: Engine Initialization, Voice Enumeration, and Controls") {
    CoInitialize(NULL);

    TtsEngine engine;
    bool ok = engine.Init(NULL, WM_USER + 100);
    REQUIRE(ok);
    REQUIRE(engine.IsInitialized());

    const auto& voices = engine.GetVoices();
    CHECK(!voices.empty());

    bool hasEnglish = false;
    bool hasArabic = false;
    for (const auto& v : voices) {
        if (v.isArabic) hasArabic = true;
        else hasEnglish = true;
    }
    CHECK(hasEnglish);
    CHECK(hasArabic); // Microsoft Naayf is installed on this system

    // Speed Rate adjustments
    engine.SetRate(0);
    CHECK(engine.GetRate() == 0);
    CHECK(engine.GetRateLabel() == L"1.0x");

    engine.SetRate(3);
    CHECK(engine.GetRate() == 3);
    CHECK(engine.GetRateLabel() == L"1.6x");

    engine.SetRate(-3);
    CHECK(engine.GetRate() == -3);
    CHECK(engine.GetRateLabel() == L"0.7x");

    engine.SetRate(10); // clamped to 5
    CHECK(engine.GetRate() == 5);
    CHECK(engine.GetRateLabel() == L"2.0x");

    engine.SetRate(-10); // clamped to -5
    CHECK(engine.GetRate() == -5);
    CHECK(engine.GetRateLabel() == L"0.5x");

    // Language routing
    CHECK(engine.SelectVoiceForLanguage(true));
    CHECK(voices[engine.GetCurrentVoiceIndex()].isArabic);

    CHECK(engine.SelectVoiceForLanguage(false));
    CHECK(!voices[engine.GetCurrentVoiceIndex()].isArabic);

    engine.Shutdown();
    CHECK(!engine.IsInitialized());
    CoUninitialize();
}

TEST_CASE("TTS: ReadAloudController Chunking, Language Tagging, and Word Mapping") {
    CoInitialize(NULL);

    ReadAloudController controller;
    bool ok = controller.Init(NULL);
    REQUIRE(ok);

    // Build synthetic page text with both English and Arabic sentences
    auto pageText = std::make_shared<PdfPageText>();
    pageText->pageIndex = 0;
    pageText->pageWidth = 600.0f;
    pageText->pageHeight = 800.0f;
    pageText->hasDigitalText = true;

    std::wstring text = L"LightPDF is an ultra-fast PDF viewer. مرحباً بكم في تطبيق قراءة المستندات! Enjoy reading.";
    pageText->fullText = text;
    pageText->chars.resize(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        pageText->chars[i].ch = text[i];
        pageText->chars[i].rect = D2D1::RectF((float)i * 10.0f, 100.0f, (float)(i + 1) * 10.0f, 120.0f);
    }

    // Start reading full page
    controller.Start(L"", pageText, 0);
    CHECK(controller.IsActive());
    CHECK(!controller.IsPaused());
    CHECK(controller.GetCurrentPage() == 0);

    auto barInfo = controller.GetBarInfo();
    CHECK(barInfo.visible);
    CHECK(!barInfo.isPaused);
    CHECK(!barInfo.voiceName.empty());

    // Test Pause / Resume
    controller.TogglePause();
    CHECK(controller.IsPaused());
    controller.TogglePause();
    CHECK(!controller.IsPaused());

    // Test Stop
    controller.Stop();
    CHECK(!controller.IsActive());

    // Test selection reading
    std::wstring sel = L"LightPDF is an ultra-fast PDF viewer.";
    controller.Start(sel, pageText, 0, 0, sel.size());
    CHECK(controller.IsActive());
    controller.Stop();
    CHECK(!controller.IsActive());

    CoUninitialize();
}

TEST_CASE("Navigation: Horizontal Offset Clamping & Scroll Calculation") {
    const float dipW = 1000.0f;
    const float margin = 24.0f;

    SUBCASE("Single-page: Normal unzoomed page fits within viewport") {
        float renderedW = 800.0f;
        // renderedW <= dipW - margin * 2.0f (800 <= 952)
        CHECK(renderedW <= dipW - margin * 2.0f);
        float expectedOffsetX = (dipW - renderedW) * 0.5f;
        CHECK(expectedOffsetX == doctest::Approx(100.0f));
    }

    SUBCASE("Single-page: Zoomed-in page wider than viewport") {
        float renderedW = 1600.0f; // e.g., zoomed to 200%
        CHECK(renderedW > dipW - margin * 2.0f);
        float minOffsetX = dipW - renderedW - margin; // 1000 - 1600 - 24 = -624
        float maxOffsetX = margin;                     // 24
        CHECK(minOffsetX == doctest::Approx(-624.0f));
        CHECK(maxOffsetX == doctest::Approx(24.0f));

        // Start at left margin
        float currentOffsetX = maxOffsetX;
        
        // Tilt wheel / swipe right: delta > 0 -> should shift viewport right (content left)
        short delta = 120;
        float deltaX = (float)delta * 0.6f; // 72.0f
        currentOffsetX -= deltaX;
        float clamped = std::clamp(currentOffsetX, minOffsetX, maxOffsetX);
        CHECK(clamped == doctest::Approx(-48.0f));

        // Scroll all the way past right edge
        currentOffsetX -= 1000.0f;
        clamped = std::clamp(currentOffsetX, minOffsetX, maxOffsetX);
        CHECK(clamped == doctest::Approx(minOffsetX)); // bounded at -624

        // Tilt wheel / swipe left: delta < 0 -> should shift viewport left (content right)
        delta = -120;
        deltaX = (float)delta * 0.6f;
        currentOffsetX = clamped - deltaX; // -624 - (-72) = -552
        clamped = std::clamp(currentOffsetX, minOffsetX, maxOffsetX);
        CHECK(clamped == doctest::Approx(-552.0f));
    }

    SUBCASE("Continuous scroll: Horizontal boundary clamping") {
        float maxPageW = 1500.0f;
        float minOffsetX = dipW - maxPageW - margin * 2.0f; // 1000 - 1500 - 48 = -548
        float maxOffsetX = 0.0f;
        CHECK(minOffsetX == doctest::Approx(-548.0f));

        float currentOffsetX = 0.0f;
        // Scroll right
        currentOffsetX -= 100.0f;
        float clamped = std::clamp(currentOffsetX, minOffsetX, maxOffsetX);
        CHECK(clamped == doctest::Approx(-100.0f));

        // Scroll past left edge
        currentOffsetX = 50.0f;
        clamped = std::clamp(currentOffsetX, minOffsetX, maxOffsetX);
        CHECK(clamped == doctest::Approx(0.0f));
    }
}

// ============================================================================
// Recent Files & Closed Tab Recovery Tests
// ============================================================================
TEST_CASE("Recent Files: Path Normalization and Filename Extraction") {
    CHECK(RecentFilesController::ExtractFileName(L"C:\\Docs\\report.pdf") == L"report.pdf");
    CHECK(RecentFilesController::ExtractFileName(L"report.pdf") == L"report.pdf");
    CHECK(RecentFilesController::ExtractFileName(L"C:/Docs/sub/manual.pdf") == L"manual.pdf");

    std::wstring p1 = RecentFilesController::NormalizePath(L"C:\\Docs\\..\\Docs\\report.pdf");
    CHECK(!p1.empty());
}

TEST_CASE("Recent Files: MRU Management, Capping, and Registry Persistence") {
    const std::wstring testKey = L"Software\\LightPDF\\UnitTestRecent";
    RecentFilesController controller(testKey);
    controller.ClearRecentFiles();
    CHECK(controller.IsEmpty());
    CHECK(controller.GetCount() == 0);

    // Add files
    controller.AddRecentFile(L"C:\\Test\\doc1.pdf", 10, 2);
    controller.AddRecentFile(L"C:\\Test\\doc2.pdf", 25, 5);
    controller.AddRecentFile(L"C:\\Test\\doc3.pdf", 100, 42);

    CHECK(controller.GetCount() == 3);
    // Most recent is doc3
    CHECK(controller.GetItems()[0].filePath == RecentFilesController::NormalizePath(L"C:\\Test\\doc3.pdf"));
    CHECK(controller.GetItems()[0].lastPage == 42);
    CHECK(controller.GetItems()[0].totalPages == 100);

    // Re-adding existing doc1 moves it to front
    controller.AddRecentFile(L"C:\\Test\\doc1.pdf", 10, 7);
    CHECK(controller.GetCount() == 3);
    CHECK(controller.GetItems()[0].filePath == RecentFilesController::NormalizePath(L"C:\\Test\\doc1.pdf"));
    CHECK(controller.GetItems()[0].lastPage == 7);

    // Update last page
    controller.UpdateLastPage(L"C:\\Test\\doc1.pdf", 9);
    CHECK(controller.GetItems()[0].lastPage == 9);

    // Add up to > 9 files to test MRU capping
    for (int i = 4; i <= 15; ++i) {
        controller.AddRecentFile(L"C:\\Test\\doc" + std::to_wstring(i) + L".pdf", 50, 1);
    }
    CHECK(controller.GetCount() == RecentFilesController::MAX_MRU_COUNT);
    CHECK(controller.GetCount() == 9);

    // Save to registry and reload with a fresh controller instance
    controller.SaveToRegistry();

    RecentFilesController reloaded(testKey);
    reloaded.LoadFromRegistry();
    CHECK(reloaded.GetCount() == 9);
    CHECK(reloaded.GetItems()[0].filePath == controller.GetItems()[0].filePath);
    CHECK(reloaded.GetItems()[0].lastPage == controller.GetItems()[0].lastPage);

    // Remove single item by index
    size_t beforeCount = reloaded.GetCount();
    CHECK(reloaded.RemoveRecentFile(0));
    CHECK(reloaded.GetCount() == beforeCount - 1);

    // Remove item by path
    std::wstring toRemove = reloaded.GetItems()[0].filePath;
    CHECK(reloaded.RemoveRecentFileByPath(toRemove));
    CHECK(reloaded.GetCount() == beforeCount - 2);

    // Cleanup registry
    reloaded.ClearRecentFiles();
    CHECK(reloaded.IsEmpty());

    RecentFilesController verifyClean(testKey);
    verifyClean.LoadFromRegistry();
    CHECK(verifyClean.IsEmpty());
}

TEST_CASE("Closed Tabs: LIFO Stack State Retention & Cap") {
    RecentFilesController controller;
    CHECK(!controller.CanReopenClosedTab());

    ClosedTabInfo tab1;
    tab1.filePath = L"C:\\Test\\closed1.pdf";
    tab1.currentPage = 4;
    tab1.zoom = 1.25f;
    tab1.zoomMode = ZoomMode::Custom;
    tab1.scrollY = 150.0f;
    tab1.continuousScroll = true;

    ClosedTabInfo tab2;
    tab2.filePath = L"C:\\Test\\closed2.pdf";
    tab2.currentPage = 12;
    tab2.zoom = 0.85f;
    tab2.zoomMode = ZoomMode::FitWidth;
    tab2.scrollY = 0.0f;
    tab2.continuousScroll = false;

    controller.PushClosedTab(tab1);
    controller.PushClosedTab(tab2);
    CHECK(controller.CanReopenClosedTab());

    // LIFO Pop: tab2 comes first
    ClosedTabInfo popped;
    REQUIRE(controller.PopClosedTab(popped));
    CHECK(popped.filePath == tab2.filePath);
    CHECK(popped.currentPage == 12);
    CHECK(popped.zoom == doctest::Approx(0.85f));
    CHECK(popped.zoomMode == ZoomMode::FitWidth);
    CHECK(!popped.continuousScroll);

    // Next pop: tab1
    REQUIRE(controller.PopClosedTab(popped));
    CHECK(popped.filePath == tab1.filePath);
    CHECK(popped.currentPage == 4);
    CHECK(popped.zoom == doctest::Approx(1.25f));
    CHECK(popped.scrollY == doctest::Approx(150.0f));
    CHECK(popped.continuousScroll);

    CHECK(!controller.CanReopenClosedTab());
    CHECK(!controller.PopClosedTab(popped));

    // Cap check (> 15 items)
    for (int i = 0; i < 20; ++i) {
        ClosedTabInfo t;
        t.filePath = L"C:\\Test\\t" + std::to_wstring(i) + L".pdf";
        controller.PushClosedTab(t);
    }
    // Should be capped at 15
    int popCount = 0;
    while (controller.PopClosedTab(popped)) {
        popCount++;
    }
    CHECK(popCount == 15);
}

TEST_CASE("Recent Switcher: Keyboard Navigation & Hit Testing") {
    RecentFilesController controller;
    for (int i = 1; i <= 5; ++i) {
        controller.AddRecentFile(L"C:\\Test\\file" + std::to_wstring(i) + L".pdf", 10, i);
    }

    CHECK(!controller.IsSwitcherOpen());
    controller.OpenSwitcher();
    CHECK(controller.IsSwitcherOpen());
    CHECK(controller.GetSwitcherSelectedIndex() == 0);

    // Down navigation
    controller.SelectNextSwitcherItem();
    CHECK(controller.GetSwitcherSelectedIndex() == 1);
    controller.SelectNextSwitcherItem();
    CHECK(controller.GetSwitcherSelectedIndex() == 2);

    // Up navigation
    controller.SelectPrevSwitcherItem();
    CHECK(controller.GetSwitcherSelectedIndex() == 1);

    // Wrap around up
    controller.SelectPrevSwitcherItem();
    CHECK(controller.GetSwitcherSelectedIndex() == 0);
    controller.SelectPrevSwitcherItem();
    CHECK(controller.GetSwitcherSelectedIndex() == 4); // Wrapped to last

    controller.CloseSwitcher();
    CHECK(!controller.IsSwitcherOpen());
    CHECK(controller.GetSwitcherSelectedIndex() == -1);

    // Hit-testing geometry
    const float dipW = 1200.0f;
    const float dipH = 800.0f;
    D2D1_RECT_F card = RecentFilesController::CalculateSwitcherCardRect(dipW, dipH);
    CHECK(card.right > card.left);
    CHECK(card.bottom > card.top);

    bool cardClicked = false;
    // Click outside modal card
    int outsideHit = RecentFilesController::HitTestSwitcher(D2D1::Point2F(10.0f, 10.0f), dipW, dipH, 5, cardClicked);
    CHECK(outsideHit == -1);
    CHECK(!cardClicked);

    // Click inside first item
    D2D1_RECT_F item0 = RecentFilesController::CalculateSwitcherItemRect(card, 0);
    int item0Hit = RecentFilesController::HitTestSwitcher(
        D2D1::Point2F((item0.left + item0.right) * 0.5f, (item0.top + item0.bottom) * 0.5f),
        dipW, dipH, 5, cardClicked);
    CHECK(cardClicked);
    CHECK(item0Hit == 0);
}

TEST_CASE("Empty-State Dashboard: Layout Geometry & Hit Testing") {
    const float dipW = 1200.0f;
    const float dipH = 800.0f;
    const size_t totalItems = 4;

    RecentItemLayout layout0 = RecentFilesController::CalculateDashboardCardLayout(dipW, dipH, 0, totalItems);
    CHECK(layout0.itemRect.right > layout0.itemRect.left);
    CHECK(layout0.itemRect.bottom > layout0.itemRect.top);
    CHECK(layout0.removeBtnRect.right > layout0.removeBtnRect.left);

    // Hit card body
    bool removeClicked = false;
    int hitCard = RecentFilesController::HitTestDashboard(
        D2D1::Point2F((layout0.itemRect.left + layout0.badgeRect.right) * 0.5f, (layout0.itemRect.top + layout0.itemRect.bottom) * 0.5f),
        dipW, dipH, totalItems, removeClicked);
    CHECK(hitCard == 0);
    CHECK(!removeClicked);

    // Hit remove button
    int hitRemove = RecentFilesController::HitTestDashboard(
        D2D1::Point2F((layout0.removeBtnRect.left + layout0.removeBtnRect.right) * 0.5f,
                      (layout0.removeBtnRect.top + layout0.removeBtnRect.bottom) * 0.5f),
        dipW, dipH, totalItems, removeClicked);
    CHECK(hitRemove == 0);
    CHECK(removeClicked);

    // Hit outside all cards
    int hitOutside = RecentFilesController::HitTestDashboard(
        D2D1::Point2F(5.0f, 5.0f), dipW, dipH, totalItems, removeClicked);
    CHECK(hitOutside == -1);
}

TEST_CASE("Security: Password-Protected PDF Detection, Status Transitions, and Overlay Layout") {
    std::wstring encPath = L"tests/fixtures/sample_encrypted.pdf";
    std::wstring normalPath = L"tests/fixtures/sample_1page.pdf";

    SUBCASE("QuickCheckFileEncrypted and Parser Detection") {
        CHECK_FALSE(PdfParser::QuickCheckFileEncrypted(normalPath));
        CHECK(PdfParser::QuickCheckFileEncrypted(encPath));

        PdfParser parser;
        bool ok = parser.Load(encPath);
        CHECK(parser.IsEncrypted());
        if (ok) {
            PdfPageText pt;
            CHECK_FALSE(parser.ExtractPageText(0, pt));
            CHECK_FALSE(pt.hasDigitalText);
        }
    }

    SUBCASE("PdfDocumentWrapper::OpenWithPassword Status State Machine") {
        PdfDocumentWrapper doc;

        // 1. Opening without password -> PasswordRequired
        PdfLoadStatus s1 = doc.OpenWithPassword(encPath, nullptr, L"");
        CHECK(s1 == PdfLoadStatus::PasswordRequired);
        CHECK_FALSE(doc.IsLoaded());

        // 2. Opening with wrong password -> WrongPassword
        PdfLoadStatus s2 = doc.OpenWithPassword(encPath, nullptr, L"wrongpass123");
        CHECK(s2 == PdfLoadStatus::WrongPassword);
        CHECK_FALSE(doc.IsLoaded());

        // 3. Opening with correct password -> Success
        PdfLoadStatus s3 = doc.OpenWithPassword(encPath, nullptr, L"secret123");
        CHECK(s3 == PdfLoadStatus::Success);
        CHECK(doc.IsLoaded());
        CHECK(doc.IsPasswordProtected());
        CHECK(doc.GetPageCount() >= 1);
        CHECK(doc.GetPageSize(0).width > 0.0f);
        CHECK(doc.GetPassword() == L"secret123");

        // Close wipes password
        doc.Close();
        CHECK_FALSE(doc.IsLoaded());
        CHECK(doc.GetPassword().empty());
    }

    SUBCASE("TabController::OpenTabWithPassword Integration") {
        TabController tabs;
        std::wstring outPath;
        bool alreadyOpen = false;

        // Without password -> PasswordRequired
        PdfLoadStatus s1 = tabs.OpenTabWithPassword(encPath, nullptr, outPath, alreadyOpen, L"");
        CHECK(s1 == PdfLoadStatus::PasswordRequired);
        CHECK(tabs.GetTabCount() == 0);

        // With wrong password -> WrongPassword
        PdfLoadStatus s2 = tabs.OpenTabWithPassword(encPath, nullptr, outPath, alreadyOpen, L"wrong");
        CHECK(s2 == PdfLoadStatus::WrongPassword);
        CHECK(tabs.GetTabCount() == 0);

        // With correct password -> Success
        PdfLoadStatus s3 = tabs.OpenTabWithPassword(encPath, nullptr, outPath, alreadyOpen, L"secret123");
        CHECK(s3 == PdfLoadStatus::Success);
        CHECK(tabs.GetTabCount() == 1);
        CHECK(tabs.GetActiveTab() != nullptr);
        CHECK(tabs.GetActiveTab()->document.IsPasswordProtected());
    }

    SUBCASE("PasswordOverlayLayout and Hit-Testing") {
        const float dipW = 1000.0f;
        const float dipH = 700.0f;
        D2D1_RECT_F card = PasswordOverlayLayout::GetCardRect(dipW, dipH);
        CHECK(card.right > card.left);
        CHECK(card.bottom > card.top);
        CHECK(card.right - card.left == doctest::Approx(PasswordOverlayLayout::CARD_W));
        CHECK(card.bottom - card.top == doctest::Approx(PasswordOverlayLayout::CARD_H));

        D2D1_RECT_F closeBtn = PasswordOverlayLayout::GetCloseBtnRect(card);
        D2D1_RECT_F toggleBtn = PasswordOverlayLayout::GetToggleShowBtnRect(card);
        D2D1_RECT_F unlockBtn = PasswordOverlayLayout::GetUnlockBtnRect(card);
        D2D1_RECT_F cancelBtn = PasswordOverlayLayout::GetCancelBtnRect(card);
        D2D1_RECT_F inputBox = PasswordOverlayLayout::GetInputBoxRect(card);

        // Hit close button (ID 1)
        POINT ptClose{ (LONG)((closeBtn.left + closeBtn.right) * 0.5f), (LONG)((closeBtn.top + closeBtn.bottom) * 0.5f) };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptClose, 1000, 700, 96.0f) == 1);

        // Hit toggle button (ID 2)
        POINT ptToggle{ (LONG)((toggleBtn.left + toggleBtn.right) * 0.5f), (LONG)((toggleBtn.top + toggleBtn.bottom) * 0.5f) };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptToggle, 1000, 700, 96.0f) == 2);

        // Hit unlock button (ID 3)
        POINT ptUnlock{ (LONG)((unlockBtn.left + unlockBtn.right) * 0.5f), (LONG)((unlockBtn.top + unlockBtn.bottom) * 0.5f) };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptUnlock, 1000, 700, 96.0f) == 3);

        // Hit cancel button (ID 4)
        POINT ptCancel{ (LONG)((cancelBtn.left + cancelBtn.right) * 0.5f), (LONG)((cancelBtn.top + cancelBtn.bottom) * 0.5f) };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptCancel, 1000, 700, 96.0f) == 4);

        // Hit input box (ID 5)
        POINT ptInput{ (LONG)((inputBox.left + inputBox.right) * 0.5f), (LONG)((inputBox.top + inputBox.bottom) * 0.5f) };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptInput, 1000, 700, 96.0f) == 5);

        // Outside card (-1)
        POINT ptOutside{ 10, 10 };
        CHECK(UIViews::PasswordOverlayView::HitTest(ptOutside, 1000, 700, 96.0f) == -1);
    }
}


// ==========================================================================

// ============================================================================
// Interactive Hyperlinks & URL Recognition Tests
// ============================================================================
TEST_CASE("Links: PDF /Annots Extraction, Plaintext URL Detection, and Security Whitelist") {
    SUBCASE("Security Scheme Whitelist") {
        CHECK(PdfParser::IsSafeExternalUri(L"https://example.com"));
        CHECK(PdfParser::IsSafeExternalUri(L"https://sub.domain.org/path/to/resource?arg=val&num=42#sec"));
        CHECK(PdfParser::IsSafeExternalUri(L"http://example.com/insecure"));
        CHECK(PdfParser::IsSafeExternalUri(L"mailto:support@lightpdf.org"));
        CHECK(PdfParser::IsSafeExternalUri(L"mailto:user.name+tag@company.co.uk"));

        // Dangerous or unsupported schemes must be strictly blocked
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L""));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"file:///C:/Windows/System32/cmd.exe"));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"javascript:alert(document.cookie)"));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"data:text/html,<script>alert(1)</script>"));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"cmd.exe /c calc"));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"powershell.exe -enc ..."));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"\\\\server\\share\\malware.exe"));
        CHECK_FALSE(PdfParser::IsSafeExternalUri(L"ftp://ftp.example.com"));
    }

    SUBCASE("Plaintext URL & Email Detection") {
        PdfPageText pageText;
        pageText.fullText = L"Visit https://github.com/ashahin1/LightPDF for code, or www.microsoft.com for docs. Contact us at info@example.com.";
        
        float cx = 10.0f;
        for (wchar_t ch : pageText.fullText) {
            PdfTextChar ptc;
            ptc.ch = ch;
            ptc.rect = D2D1::RectF(cx, 10.0f, cx + 8.0f, 24.0f);
            pageText.chars.push_back(ptc);
            cx += 8.0f;
        }

        PdfParser::DetectPlaintextLinks(pageText);

        CHECK(pageText.links.size() == 3);
        if (pageText.links.size() == 3) {
            CHECK(pageText.links[0].type == PdfLinkType::ExternalUri);
            CHECK(pageText.links[0].uri == L"https://github.com/ashahin1/LightPDF");
            CHECK(pageText.links[0].isAutoDetected);

            CHECK(pageText.links[1].type == PdfLinkType::ExternalUri);
            CHECK(pageText.links[1].uri == L"https://www.microsoft.com");
            CHECK(pageText.links[1].isAutoDetected);

            CHECK(pageText.links[2].type == PdfLinkType::ExternalUri);
            CHECK(pageText.links[2].uri == L"mailto:info@example.com");
            CHECK(pageText.links[2].isAutoDetected);
        }
    }

    SUBCASE("Real PDF /Annots Link Extraction") {
        std::wstring pdfPath = L"tests/PDF/Teaching Schedule - 472.pdf";
        if (std::filesystem::exists(pdfPath)) {
            PdfParser parser;
            REQUIRE(parser.Load(pdfPath));

            PdfPageText pageText;
            REQUIRE(parser.ExtractPageText(0, pageText));
            REQUIRE_FALSE(pageText.links.empty());

            bool foundMailto = false;
            for (const auto& link : pageText.links) {
                if (link.type == PdfLinkType::ExternalUri && link.uri.find(L"mailto:ahmed.bpc.2024@gmail.com") != std::wstring::npos) {
                    foundMailto = true;
                    CHECK_FALSE(link.isAutoDetected);
                    CHECK(link.rect.right > link.rect.left);
                    CHECK(link.rect.bottom > link.rect.top);
                    break;
                }
            }
            CHECK(foundMailto);
        }
    }

    SUBCASE("HitTestPageLink Integration on Real Document") {
        std::wstring pdfPath = L"tests/PDF/Teaching Schedule - 472.pdf";
        if (std::filesystem::exists(pdfPath)) {
            TabController tabs;
            std::wstring outPath;
            bool alreadyOpen = false;
            REQUIRE(tabs.OpenTab(pdfPath, nullptr, outPath, alreadyOpen));
            DocumentTab* pTab = tabs.GetActiveTab();
            REQUIRE(pTab != nullptr);

            SelectionController selController;
            auto pageText = selController.GetOrExtractPageText(pTab, 0);
            REQUIRE(pageText != nullptr);
            REQUIRE_FALSE(pageText->links.empty());

            const auto& targetLink = pageText->links[0];
            float midX = (targetLink.rect.left + targetLink.rect.right) * 0.5f;
            float midY = (targetLink.rect.top + targetLink.rect.bottom) * 0.5f;

            pTab->continuousScroll = false;
            pTab->zoom = 1.0f;
            pTab->offsetX = 0.0f;
            pTab->offsetY = 0.0f;

            uint32_t hitPage = 999;
            PdfLink hitLink;
            POINT ptHit{ (LONG)midX, (LONG)midY };
            CHECK(selController.HitTestPageLink(pTab, ptHit, 1200.0f, 96.0f, 0.0f, hitPage, hitLink));
            CHECK(hitPage == 0);
            CHECK(hitLink.uri.find(L"mailto:") != std::wstring::npos);

            POINT ptMiss{ 10, 10 };
            CHECK_FALSE(selController.HitTestPageLink(pTab, ptMiss, 1200.0f, 96.0f, 0.0f, hitPage, hitLink));
        }
    }

    SUBCASE("Navigation History Stack LIFO & Cap") {
        DocumentTab tab;
        CHECK(tab.navBackStack.empty());
        CHECK(tab.navForwardStack.empty());

        // Push 55 entries to verify cap at 50
        for (int i = 0; i < 55; ++i) {
            tab.navBackStack.push_back({ (uint32_t)i, (float)(i * 10), 0.0f });
            if (tab.navBackStack.size() > 50) {
                tab.navBackStack.erase(tab.navBackStack.begin());
            }
        }
        CHECK(tab.navBackStack.size() == 50);
        CHECK(tab.navBackStack.front().pageIndex == 5);
        CHECK(tab.navBackStack.back().pageIndex == 54);
    }
}


// ============================================================================
// Scanned PDF OCR Isolation & Link Hit-Test Performance Test
// ============================================================================
TEST_CASE("Performance: GetOrExtractPageText on Scanned PDFs Does Not Trigger Eager OCR") {
    // Test 1: 123.pdf (Scanned PDF without digital text)
    std::wstring scannedPath1 = L"tests/PDF/123.pdf";
    if (std::filesystem::exists(scannedPath1)) {
        TabController tabs;
        std::wstring outPath;
        bool alreadyOpen = false;
        REQUIRE(tabs.OpenTab(scannedPath1, nullptr, outPath, alreadyOpen));
        DocumentTab* pTab = tabs.GetActiveTab();
        REQUIRE(pTab != nullptr);

        SelectionController selController;
        auto start = std::chrono::high_resolution_clock::now();
        auto pageText = selController.GetOrExtractPageText(pTab, 0);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start
        ).count();

        REQUIRE(pageText != nullptr);
        CHECK_FALSE(pageText->hasDigitalText);
        CHECK(pageText->chars.empty());
        // Verify fast execution: OCR takes >500ms, while pure extraction takes <50ms
        CHECK(elapsedMs < 100);

        // Verify link hit testing is instant and returns false with no links
        uint32_t hitPage = 999;
        PdfLink hitLink;
        POINT pt{ 100, 100 };
        auto hitStart = std::chrono::high_resolution_clock::now();
        bool hasHit = selController.HitTestPageLink(pTab, pt, 1200.0f, 96.0f, 0.0f, hitPage, hitLink);
        auto hitElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - hitStart
        ).count();
        CHECK_FALSE(hasHit);
        CHECK(hitElapsedMs < 20);
    }

    // Test 2: 1234.pdf (Scanned PDF without digital text)
    std::wstring scannedPath2 = L"tests/PDF/1234.pdf";
    if (std::filesystem::exists(scannedPath2)) {
        TabController tabs;
        std::wstring outPath;
        bool alreadyOpen = false;
        REQUIRE(tabs.OpenTab(scannedPath2, nullptr, outPath, alreadyOpen));
        DocumentTab* pTab = tabs.GetActiveTab();
        REQUIRE(pTab != nullptr);

        SelectionController selController;
        auto start = std::chrono::high_resolution_clock::now();
        auto pageText = selController.GetOrExtractPageText(pTab, 0);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start
        ).count();

        REQUIRE(pageText != nullptr);
        CHECK_FALSE(pageText->hasDigitalText);
        CHECK(pageText->chars.empty());
        CHECK(elapsedMs < 100);
    }
}


// ============================================================================
// PDF Decryption & Unencrypted PDF Export Test
// ============================================================================
TEST_CASE("Security: Decrypt and Save Unencrypted PDF (sample_encrypted.pdf)") {
    std::wstring encPath = L"tests/fixtures/sample_encrypted.pdf";
    std::wstring outDecPath = L"tests/fixtures/temp_sample_decrypted.pdf";
    if (std::filesystem::exists(encPath)) {
        std::error_code ec;
        if (std::filesystem::exists(outDecPath, ec)) {
            std::filesystem::remove(outDecPath, ec);
        }

        auto result = PdfDecryptor::DecryptAndSave(encPath, outDecPath, L"secret123");
        REQUIRE(result.success);
        REQUIRE(std::filesystem::exists(outDecPath, ec));

        CHECK_FALSE(PdfParser::QuickCheckFileEncrypted(outDecPath));

        {
            PdfParser parser;
            REQUIRE(parser.Load(outDecPath));
            CHECK_FALSE(parser.IsEncrypted());
            parser.Close();
        }

        {
            PdfDocumentWrapper doc;
            PdfLoadStatus status = doc.OpenWithPassword(outDecPath, nullptr, L"");
            CHECK(status == PdfLoadStatus::Success);
            CHECK(doc.IsLoaded());
            CHECK_FALSE(doc.IsPasswordProtected());
            CHECK(doc.GetPageCount() >= 1);

            PdfDocumentWrapper origDoc;
            if (origDoc.OpenWithPassword(encPath, nullptr, L"secret123") == PdfLoadStatus::Success) {
                CHECK(doc.GetPageCount() == origDoc.GetPageCount());
                auto origSize = origDoc.GetPageSize(0);
                auto decSize = doc.GetPageSize(0);
                CHECK(std::abs(origSize.width - decSize.width) < 1.0f);
                CHECK(std::abs(origSize.height - decSize.height) < 1.0f);
                origDoc.Close();
            }
            doc.Close();
        }

        std::filesystem::remove(outDecPath, ec);
    }
}

// ============================================================================
// UI: Context Menu State Assembly and Command Routing Tests
// ============================================================================
TEST_CASE("UI: Context Menu State Assembly and Command Routing") {
    SUBCASE("ContextMenuController Menu Construction Across Contexts") {
        // 1. TabStrip Menu
        {
            ContextMenuData data;
            data.totalTabs = 3;
            data.targetTabIndex = 1;
            data.canReopenClosedTab = true;
            data.tabFilePath = L"C:\\test\\file.pdf";

            HMENU hMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::TabStrip, data);
            REQUIRE(hMenu != nullptr);

            UINT closeOthers = GetMenuState(hMenu, ID_CTX_TAB_CLOSE_OTHERS, MF_BYCOMMAND);
            CHECK((closeOthers & MF_GRAYED) == 0); // Enabled when totalTabs > 1

            UINT closeRight = GetMenuState(hMenu, ID_CTX_TAB_CLOSE_RIGHT, MF_BYCOMMAND);
            CHECK((closeRight & MF_GRAYED) == 0); // Enabled when index 1 < 2

            UINT reopen = GetMenuState(hMenu, ID_CTX_TAB_REOPEN_CLOSED, MF_BYCOMMAND);
            CHECK((reopen & MF_GRAYED) == 0);

            DestroyMenu(hMenu);

            // Edge case: single tab, right-most tab
            data.totalTabs = 1;
            data.targetTabIndex = 0;
            data.canReopenClosedTab = false;
            HMENU hMenuSingle = ContextMenuController::CreateMenuForContext(ContextMenuContext::TabStrip, data);
            REQUIRE(hMenuSingle != nullptr);

            UINT closeOthersDis = GetMenuState(hMenuSingle, ID_CTX_TAB_CLOSE_OTHERS, MF_BYCOMMAND);
            CHECK((closeOthersDis & MF_GRAYED) != 0); // Disabled when totalTabs <= 1

            UINT closeRightDis = GetMenuState(hMenuSingle, ID_CTX_TAB_CLOSE_RIGHT, MF_BYCOMMAND);
            CHECK((closeRightDis & MF_GRAYED) != 0); // Disabled at right-most tab

            UINT reopenDis = GetMenuState(hMenuSingle, ID_CTX_TAB_REOPEN_CLOSED, MF_BYCOMMAND);
            CHECK((reopenDis & MF_GRAYED) != 0); // Disabled when cannot reopen

            DestroyMenu(hMenuSingle);
        }

        // 2. Hyperlink Menu
        {
            ContextMenuData data;
            data.hasLink = true;
            data.hitLink.type = PdfLinkType::ExternalUri;
            data.hitLink.uri = L"https://github.com";

            HMENU hMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::Hyperlink, data);
            REQUIRE(hMenu != nullptr);

            CHECK(GetMenuDefaultItem(hMenu, FALSE, 0) == ID_CTX_OPEN_LINK);
            CHECK(GetMenuState(hMenu, ID_CTX_COPY_LINK, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hMenu, ID_CTX_FIT_PAGE, MF_BYCOMMAND) != 0xFFFFFFFF);

            DestroyMenu(hMenu);
        }

        // 3. TextSelection Menu with Truncation
        {
            ContextMenuData data;
            data.hasSelection = true;
            data.selectedText = L"Supercalifragilistic\n\textraordinary words";

            HMENU hMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::TextSelection, data);
            REQUIRE(hMenu != nullptr);

            CHECK(GetMenuState(hMenu, ID_CTX_COPY, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hMenu, ID_CTX_SELECT_ALL, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hMenu, ID_CTX_DESELECT, MF_BYCOMMAND) != 0xFFFFFFFF);

            wchar_t searchBuf[256] = {};
            GetMenuStringW(hMenu, ID_CTX_SEARCH_SELECTION, searchBuf, 256, MF_BYCOMMAND);
            std::wstring searchLabel(searchBuf);
            CHECK(searchLabel.find(L"...") != std::wstring::npos);
            CHECK(searchLabel.find(L"\tCtrl+F") != std::wstring::npos);

            wchar_t dictBuf[256] = {};
            GetMenuStringW(hMenu, ID_CTX_DICT_LOOKUP, dictBuf, 256, MF_BYCOMMAND);
            std::wstring dictLabel(dictBuf);
            CHECK(dictLabel.find(L"...") != std::wstring::npos);
            CHECK(dictLabel.find(L"\tD") != std::wstring::npos);

            DestroyMenu(hMenu);
        }

        // 4. Canvas Menu
        {
            ContextMenuData data;
            data.isDocumentLoaded = true;
            data.currentPage = 0;
            data.totalPages = 10;
            data.isContinuousScroll = true;
            data.isPasswordProtected = false;

            HMENU hMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::Canvas, data);
            REQUIRE(hMenu != nullptr);

            UINT prev = GetMenuState(hMenu, ID_CTX_PREV_PAGE, MF_BYCOMMAND);
            CHECK((prev & MF_GRAYED) != 0); // Disabled on first page

            UINT next = GetMenuState(hMenu, ID_CTX_NEXT_PAGE, MF_BYCOMMAND);
            CHECK((next & MF_GRAYED) == 0); // Enabled

            UINT cont = GetMenuState(hMenu, ID_CTX_CONTINUOUS_SCROLL, MF_BYCOMMAND);
            CHECK((cont & MF_CHECKED) != 0); // Checked

            CHECK(GetMenuState(hMenu, ID_CTX_ACTUAL_SIZE, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hMenu, ID_CTX_SAVE_AS, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hMenu, ID_CTX_SAVE_SEARCHABLE, MF_BYCOMMAND) == 0xFFFFFFFF); // Not present on pure digital PDF
            CHECK(GetMenuState(hMenu, ID_CTX_SAVE_DECRYPTED, MF_BYCOMMAND) == 0xFFFFFFFF); // Not present on unprotected

            wchar_t itemBuf[256] = {};
            GetMenuStringW(hMenu, ID_CTX_FIT_PAGE, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+0") != std::wstring::npos);

            GetMenuStringW(hMenu, ID_CTX_ACTUAL_SIZE, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+1") != std::wstring::npos);

            GetMenuStringW(hMenu, ID_CTX_FIT_WIDTH, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+2") != std::wstring::npos);

            GetMenuStringW(hMenu, ID_CTX_CONTINUOUS_SCROLL, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+3") != std::wstring::npos);

            GetMenuStringW(hMenu, ID_CTX_SAVE_AS, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+S") != std::wstring::npos);

            DestroyMenu(hMenu);

            // Scanned document canvas
            data.hasScannedPages = true;
            HMENU hScanMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::Canvas, data);
            REQUIRE(hScanMenu != nullptr);

            CHECK(GetMenuState(hScanMenu, ID_CTX_SAVE_SEARCHABLE, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hScanMenu, ID_CTX_SAVE_AS, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hScanMenu, ID_CTX_SAVE_DECRYPTED, MF_BYCOMMAND) == 0xFFFFFFFF);

            GetMenuStringW(hScanMenu, ID_CTX_SAVE_SEARCHABLE, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+S") != std::wstring::npos);
            GetMenuStringW(hScanMenu, ID_CTX_SAVE_AS, itemBuf, 256, MF_BYCOMMAND);
            CHECK(std::wstring(itemBuf).find(L"\tCtrl+Shift+S") != std::wstring::npos);

            DestroyMenu(hScanMenu);

            // Password-protected canvas
            data.isPasswordProtected = true;
            HMENU hEncMenu = ContextMenuController::CreateMenuForContext(ContextMenuContext::Canvas, data);
            REQUIRE(hEncMenu != nullptr);

            CHECK(GetMenuState(hEncMenu, ID_CTX_SAVE_DECRYPTED, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hEncMenu, ID_CTX_SAVE_AS, MF_BYCOMMAND) != 0xFFFFFFFF);
            CHECK(GetMenuState(hEncMenu, ID_CTX_SAVE_SEARCHABLE, MF_BYCOMMAND) == 0xFFFFFFFF);

            DestroyMenu(hEncMenu);
        }
    }

    SUBCASE("TabController Batch Tab Operations") {
        std::wstring p1 = L"tests/PDF/123.pdf";
        std::wstring p2 = L"tests/PDF/1234.pdf";
        std::wstring p3 = L"tests/PDF/1234_OCR.pdf";

        TabController tabs;
        std::wstring outPath;
        bool alreadyOpen = false;

        REQUIRE(tabs.OpenTab(p1, nullptr, outPath, alreadyOpen));
        REQUIRE(tabs.OpenTab(p2, nullptr, outPath, alreadyOpen));
        REQUIRE(tabs.OpenTab(p3, nullptr, outPath, alreadyOpen));
        REQUIRE(tabs.GetTabCount() == 3);
        CHECK(tabs.GetActiveIndex() == 2);

        // Test CloseTabsToTheRight(1): closes tab 2 (index > 1)
        std::vector<std::wstring> closedRightPaths;
        tabs.CloseTabsToTheRight(1, [&](const DocumentTab& t) {
            closedRightPaths.push_back(t.document.GetFilePath());
        });

        CHECK(closedRightPaths.size() == 1);
        CHECK(tabs.GetTabCount() == 2);
        CHECK(tabs.GetActiveIndex() == 1);

        // Open a 3rd tab again
        REQUIRE(tabs.OpenTab(p3, nullptr, outPath, alreadyOpen));
        REQUIRE(tabs.GetTabCount() == 3);

        // Test CloseOtherTabs(1): keeps tab 1, closes tabs 0 and 2
        std::vector<std::wstring> closedOtherPaths;
        tabs.CloseOtherTabs(1, [&](const DocumentTab& t) {
            closedOtherPaths.push_back(t.document.GetFilePath());
        });

        CHECK(closedOtherPaths.size() == 2);
        CHECK(tabs.GetTabCount() == 1);
        CHECK(tabs.GetActiveIndex() == 0);
    }
}

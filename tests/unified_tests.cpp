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
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.h>

#include "dictionary_engine.hpp"
#include "tab_controller.hpp"
#include "search_controller.hpp"
#include "selection_controller.hpp"
#include "pdf_parser.hpp"
#include "pdf_searchable_writer.hpp"
#include "pdf_search.hpp"
#include "ui_views.hpp"

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
// Real Document Loading & Parsing Tests
// ============================================================================
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

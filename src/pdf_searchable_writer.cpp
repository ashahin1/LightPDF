#include "pdf_searchable_writer.hpp"
#include "pdf_parser.hpp"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cmath>

struct ArabicCharForm {
    uint16_t iso;
    uint16_t fin;
    uint16_t ini;
    uint16_t med;
    bool joinsLeft;
};

static const std::unordered_map<wchar_t, ArabicCharForm> s_arabicForms = {
    { 0x0621, { 0xFE80, 0xFE80, 0xFE80, 0xFE80, false } }, // ء
    { 0x0622, { 0xFE81, 0xFE82, 0xFE81, 0xFE82, false } }, // آ
    { 0x0623, { 0xFE83, 0xFE84, 0xFE83, 0xFE84, false } }, // أ
    { 0x0624, { 0xFE85, 0xFE86, 0xFE85, 0xFE86, false } }, // ؤ
    { 0x0625, { 0xFE87, 0xFE88, 0xFE87, 0xFE88, false } }, // إ
    { 0x0626, { 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C, true } },  // ئ
    { 0x0627, { 0xFE8D, 0xFE8E, 0xFE8D, 0xFE8E, false } }, // ا
    { 0x0628, { 0xFE8F, 0xFE90, 0xFE91, 0xFE92, true } },  // ب
    { 0x0629, { 0xFE93, 0xFE94, 0xFE93, 0xFE94, false } }, // ة
    { 0x062A, { 0xFE95, 0xFE96, 0xFE97, 0xFE98, true } },  // ت
    { 0x062B, { 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C, true } },  // ث
    { 0x062C, { 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0, true } },  // ج
    { 0x062D, { 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4, true } },  // ح
    { 0x062E, { 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8, true } },  // خ
    { 0x062F, { 0xFEA9, 0xFEAA, 0xFEA9, 0xFEAA, false } }, // د
    { 0x0630, { 0xFEAB, 0xFEAC, 0xFEAB, 0xFEAC, false } }, // ذ
    { 0x0631, { 0xFEAD, 0xFEAE, 0xFEAD, 0xFEAE, false } }, // ر
    { 0x0632, { 0xFEAF, 0xFEB0, 0xFEAF, 0xFEB0, false } }, // ز
    { 0x0633, { 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4, true } },  // س
    { 0x0634, { 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8, true } },  // ش
    { 0x0635, { 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC, true } },  // ص
    { 0x0636, { 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0, true } },  // ض
    { 0x0637, { 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4, true } },  // ط
    { 0x0638, { 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8, true } },  // ظ
    { 0x0639, { 0xFEC9, 0xFECA, 0xFECB, 0xFECC, true } },  // ع
    { 0x063A, { 0xFECD, 0xFECE, 0xFECF, 0xFED0, true } },  // غ
    { 0x0641, { 0xFED1, 0xFED2, 0xFED3, 0xFED4, true } },  // ف
    { 0x0642, { 0xFED5, 0xFED6, 0xFED7, 0xFED8, true } },  // ق
    { 0x0643, { 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC, true } },  // ك
    { 0x0644, { 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0, true } },  // ل
    { 0x0645, { 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4, true } },  // م
    { 0x0646, { 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8, true } },  // ن
    { 0x0647, { 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC, true } },  // ه
    { 0x0648, { 0xFEED, 0xFEEE, 0xFEED, 0xFEEE, false } }, // و
    { 0x0649, { 0xFEEF, 0xFEF0, 0xFEEF, 0xFEF0, false } }, // ى
    { 0x064A, { 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4, true } },  // ي
    { 0x0671, { 0xFB50, 0xFB51, 0xFB50, 0xFB51, false } }  // ٱ
};

static const std::unordered_map<wchar_t, std::pair<uint16_t, uint16_t>> s_lamAlef = {
    { 0x0622, { 0xFEF5, 0xFEF6 } }, // لآ
    { 0x0623, { 0xFEF7, 0xFEF8 } }, // لأ
    { 0x0625, { 0xFEF9, 0xFEFA } }, // لإ
    { 0x0627, { 0xFEFB, 0xFEFC } }, // لا
};

struct ShapedGlyphItem {
    uint16_t shapedChar;       // Unicode Presentation Forms-B
    std::wstring logicalChars; // Logical Unicode character(s) for /ToUnicode mapping
};

static std::vector<ShapedGlyphItem> ShapeArabicWord(const std::wstring& text) {
    std::vector<ShapedGlyphItem> result;
    if (text.empty()) return result;

    struct Token {
        bool isLA = false;
        wchar_t cp = 0;
        std::wstring orig;
    };

    std::vector<Token> tokens;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == 0x0644 && i + 1 < text.size() && s_lamAlef.find(text[i + 1]) != s_lamAlef.end()) {
            Token t;
            t.isLA = true;
            t.cp = text[i + 1];
            t.orig = text.substr(i, 2);
            tokens.push_back(t);
            i++;
        } else {
            Token t;
            t.isLA = false;
            t.cp = c;
            t.orig = std::wstring(1, c);
            tokens.push_back(t);
        }
    }

    for (size_t idx = 0; idx < tokens.size(); ++idx) {
        bool prevJoins = false;
        if (idx > 0) {
            const auto& pt = tokens[idx - 1];
            if (!pt.isLA) {
                auto it = s_arabicForms.find(pt.cp);
                if (it != s_arabicForms.end()) {
                    prevJoins = it->second.joinsLeft;
                }
            }
        }

        bool nextJoins = false;
        if (idx + 1 < tokens.size()) {
            const auto& nt = tokens[idx + 1];
            if (nt.isLA) {
                nextJoins = true;
            } else {
                auto it = s_arabicForms.find(nt.cp);
                if (it != s_arabicForms.end() && nt.cp != 0x0621) {
                    nextJoins = true;
                }
            }
        }

        const auto& tok = tokens[idx];
        if (tok.isLA) {
            auto itLA = s_lamAlef.find(tok.cp);
            uint16_t laCode = prevJoins ? itLA->second.second : itLA->second.first;
            result.push_back({ laCode, tok.orig });
        } else {
            auto itF = s_arabicForms.find(tok.cp);
            if (itF != s_arabicForms.end()) {
                uint16_t resCode = itF->second.iso;
                if (prevJoins && nextJoins && itF->second.joinsLeft) {
                    resCode = itF->second.med;
                } else if (prevJoins) {
                    resCode = itF->second.fin;
                } else if (nextJoins && itF->second.joinsLeft) {
                    resCode = itF->second.ini;
                } else {
                    resCode = itF->second.iso;
                }
                result.push_back({ resCode, tok.orig });
            } else {
                result.push_back({ (uint16_t)tok.cp, tok.orig });
            }
        }
    }

    return result;
}

static bool ParseBoxHelper(const std::string& dict, const std::string& key, float& rx0, float& ry0, float& rx1, float& ry1) {
    size_t pos = dict.find(key);
    if (pos != std::string::npos) {
        size_t b1 = dict.find('[', pos);
        size_t b2 = dict.find(']', b1);
        if (b1 != std::string::npos && b2 != std::string::npos) {
            std::string mbStr = dict.substr(b1 + 1, b2 - b1 - 1);
            std::stringstream ss(mbStr);
            float a = 0, b = 0, c = 0, d = 0;
            if (ss >> a >> b >> c >> d) {
                rx0 = std::min(a, c);
                ry0 = std::min(b, d);
                rx1 = std::max(a, c);
                ry1 = std::max(b, d);
                return true;
            }
        }
    }
    return false;
}

static int ParseRotateHelper(const std::string& dict) {
    size_t pos = dict.find("/Rotate");
    if (pos != std::string::npos) {
        size_t valPos = pos + 7;
        while (valPos < dict.size() && (dict[valPos] == ' ' || dict[valPos] == '\t' || dict[valPos] == '\r' || dict[valPos] == '\n')) valPos++;
        if (valPos < dict.size()) {
            int rot = (int)strtol(dict.c_str() + valPos, nullptr, 10);
            return ((rot % 360) + 360) % 360;
        }
    }
    return 0;
}

bool PdfSearchableWriter::WriteSearchablePdf(
    const std::wstring& srcPdfPath,
    const std::wstring& dstPdfPath,
    const std::vector<OcrPageItem>& ocrPages,
    std::function<void(float progress, const std::wstring& status)> progressCallback
) {
    if (srcPdfPath.empty() || dstPdfPath.empty()) return false;

    PdfParser parser;
    if (!parser.Load(srcPdfPath)) {
        return false;
    }

    if (parser.m_bufferView.empty() || parser.m_pageObjectNums.empty()) {
        return false;
    }

    if (progressCallback) progressCallback(0.1f, L"Analyzing PDF structure...");

    size_t origFileSize = parser.m_bufferView.size();
    size_t oldStartXref = parser.m_startXrefOffset;
    uint32_t rootObjNum = parser.m_rootObjNum;
    uint32_t infoObjNum = parser.m_infoObjNum;
    std::string idStr = parser.m_idString;

    // 1. Determine highest object number in existing xref
    uint32_t maxObjNum = 0;
    for (const auto& kv : parser.m_xref) {
        if (kv.first > maxObjNum) maxObjNum = kv.first;
    }
    if (maxObjNum == 0) maxObjNum = 1;

    // 2. Collect unique Unicode characters for /ToUnicode CMap
    std::set<wchar_t> uniqueChars;
    // Always include standard printable ASCII (0x0020 - 0x007E)
    for (wchar_t c = 0x20; c <= 0x7E; ++c) {
        uniqueChars.insert(c);
    }
    for (const auto& pg : ocrPages) {
        for (const auto& wd : pg.words) {
            for (wchar_t ch : wd.text) {
                if (ch >= 0x20) {
                    uniqueChars.insert(ch);
                }
            }
        }
    }

    // Group characters into consecutive runs of <= 95 characters for beginbfrange
    std::vector<std::pair<uint16_t, uint16_t>> ranges;
    auto itChar = uniqueChars.begin();
    while (itChar != uniqueChars.end()) {
        uint16_t startC = (uint16_t)*itChar;
        uint16_t endC = startC;
        ++itChar;
        while (itChar != uniqueChars.end() && (uint16_t)*itChar == endC + 1 && (endC - startC) < 95) {
            endC = (uint16_t)*itChar;
            ++itChar;
        }
        ranges.push_back({ startC, endC });
    }

    // 3. Build /ToUnicode CMap stream
    std::string toUnicodeStream;
    toUnicodeStream += "/CIDInit /ProcSet findresource begin\n";
    toUnicodeStream += "12 dict begin\n";
    toUnicodeStream += "begincmap\n";
    toUnicodeStream += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
    toUnicodeStream += "/CMapName /LightPDF-Searchable-ToUnicode def\n";
    toUnicodeStream += "/CMapType 2 def\n";
    toUnicodeStream += "1 begincodespacerange\n";
    toUnicodeStream += "<0000> <FFFF>\n";
    toUnicodeStream += "endcodespacerange\n";

    for (size_t rIdx = 0; rIdx < ranges.size(); rIdx += 100) {
        size_t chunkCount = std::min((size_t)100, ranges.size() - rIdx);
        toUnicodeStream += std::to_string(chunkCount) + " beginbfrange\n";
        for (size_t k = 0; k < chunkCount; ++k) {
            char buf[64];
            snprintf(buf, sizeof(buf), "<%04X> <%04X> <%04X>\n",
                     ranges[rIdx + k].first, ranges[rIdx + k].second, ranges[rIdx + k].first);
            toUnicodeStream += buf;
        }
        toUnicodeStream += "endbfrange\n";
    }

    toUnicodeStream += "endcmap\n";
    toUnicodeStream += "CMapName currentdict /CMap defineresource pop\n";
    toUnicodeStream += "end\nend\n";

    // 4. Allocate font object numbers
    uint32_t objToUnicode = ++maxObjNum;
    uint32_t objFontDescriptor = ++maxObjNum;
    uint32_t objDescendantFont = ++maxObjNum;
    uint32_t objType0Font = ++maxObjNum;

    // Buffer to accumulate the incremental update
    std::string incUpdate;
    incUpdate.reserve(256 * 1024);

    // Map of object number -> byte offset in destination file
    std::map<uint32_t, size_t> newOffsets;

    auto appendObject = [&](uint32_t objNum, const std::string& objBody) {
        newOffsets[objNum] = origFileSize + incUpdate.size();
        incUpdate += std::to_string(objNum) + " 0 obj\n";
        incUpdate += objBody;
        if (!objBody.empty() && objBody.back() != '\n') incUpdate += "\n";
        incUpdate += "endobj\n";
    };

    // Serialize Font Descriptor
    appendObject(objFontDescriptor,
        "<<\n"
        "  /Type /FontDescriptor\n"
        "  /FontName /LightPDF-Searchable\n"
        "  /Flags 4\n"
        "  /FontBBox [ 0 -200 1000 800 ]\n"
        "  /Ascent 800\n"
        "  /Descent -200\n"
        "  /CapHeight 700\n"
        "  /StemV 80\n"
        ">>"
    );

    // Serialize Descendant CIDFontType2
    appendObject(objDescendantFont,
        "<<\n"
        "  /Type /Font\n"
        "  /Subtype /CIDFontType2\n"
        "  /BaseFont /LightPDF-Searchable\n"
        "  /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>\n"
        "  /FontDescriptor " + std::to_string(objFontDescriptor) + " 0 R\n"
        "  /DW 1000\n"
        ">>"
    );

    // Serialize Type0 Composite Font
    appendObject(objType0Font,
        "<<\n"
        "  /Type /Font\n"
        "  /Subtype /Type0\n"
        "  /BaseFont /LightPDF-Searchable\n"
        "  /Encoding /Identity-H\n"
        "  /DescendantFonts [ " + std::to_string(objDescendantFont) + " 0 R ]\n"
        "  /ToUnicode " + std::to_string(objToUnicode) + " 0 R\n"
        ">>"
    );

    // Serialize /ToUnicode stream
    appendObject(objToUnicode,
        "<< /Length " + std::to_string(toUnicodeStream.size()) + " >>\n"
        "stream\r\n" + toUnicodeStream + "\r\nendstream"
    );

    // Encapsulate existing page contents with graphics state save/restore to shield from CTM leakage
    uint32_t objQOpen = ++maxObjNum;
    appendObject(objQOpen,
        "<< /Length 2 >>\n"
        "stream\r\nq\n\r\nendstream"
    );

    uint32_t objQClose = ++maxObjNum;
    appendObject(objQClose,
        "<< /Length 2 >>\n"
        "stream\r\nQ\n\r\nendstream"
    );

    // 5. For each OCR page, build invisible text stream and update page dictionary
    std::map<uint32_t, std::string> updatedResourceDicts; // cache updated indirect /Resources

    for (size_t pIdx = 0; pIdx < ocrPages.size(); ++pIdx) {
        const auto& pg = ocrPages[pIdx];
        if (pg.pageIndex >= parser.m_pageObjectNums.size()) continue;

        uint32_t pageObjNum = parser.m_pageObjectNums[pg.pageIndex];
        std::string pageDict = parser.GetObjectString(pageObjNum);
        if (pageDict.empty()) continue;

        // Parse page geometry
        float x0 = 0.0f, y0 = 0.0f, x1 = 595.28f, y1 = 841.89f;
        if (!ParseBoxHelper(pageDict, "/CropBox", x0, y0, x1, y1)) {
            if (!ParseBoxHelper(pageDict, "/MediaBox", x0, y0, x1, y1)) {
                uint32_t parentObj = 0;
                if (parser.FindIndirectRef(pageDict, "/Parent", parentObj)) {
                    std::string parentDict = parser.GetObjectString(parentObj);
                    if (!ParseBoxHelper(parentDict, "/CropBox", x0, y0, x1, y1)) {
                        ParseBoxHelper(parentDict, "/MediaBox", x0, y0, x1, y1);
                    }
                }
            }
        }
        float cropW = std::max(1.0f, x1 - x0);
        float cropH = std::max(1.0f, y1 - y0);

        int rotate = ParseRotateHelper(pageDict);
        if (rotate == 0) {
            uint32_t parentObj = 0;
            if (parser.FindIndirectRef(pageDict, "/Parent", parentObj)) {
                std::string parentDict = parser.GetObjectString(parentObj);
                rotate = ParseRotateHelper(parentDict);
            }
        }

        // Build invisible text content stream
        std::string contentStream;
        contentStream += "q\n";
        contentStream += "3 Tr\n"; // Invisible mode: neither fill nor stroke

        for (const auto& wd : pg.words) {
            if (wd.text.empty()) continue;

            float dipLeft = wd.dipRect.left;
            float dipTop = wd.dipRect.top;
            float dipRight = wd.dipRect.right;
            float dipBottom = wd.dipRect.bottom;
            float dipW = dipRight - dipLeft;
            float dipH = dipBottom - dipTop;
            if (dipW <= 0.5f || dipH <= 0.5f) continue;

            float pdfX = 0.0f, pdfY = 0.0f, pdfW = 0.0f, pdfH = 0.0f;

            if (rotate == 90) {
                pdfX = x0 + dipTop * (72.0f / 96.0f);
                pdfY = y0 + dipLeft * (72.0f / 96.0f);
                pdfW = dipH * (72.0f / 96.0f);
                pdfH = dipW * (72.0f / 96.0f);
            } else if (rotate == 180) {
                pdfX = x1 - dipRight * (72.0f / 96.0f);
                pdfY = y0 + dipTop * (72.0f / 96.0f);
                pdfW = dipW * (72.0f / 96.0f);
                pdfH = dipH * (72.0f / 96.0f);
            } else if (rotate == 270) {
                pdfX = x1 - dipBottom * (72.0f / 96.0f);
                pdfY = y1 - dipRight * (72.0f / 96.0f);
                pdfW = dipH * (72.0f / 96.0f);
                pdfH = dipW * (72.0f / 96.0f);
            } else {
                pdfX = x0 + dipLeft * (72.0f / 96.0f);
                pdfY = y0 + (cropH - (dipBottom * (72.0f / 96.0f)));
                pdfW = dipW * (72.0f / 96.0f);
                pdfH = dipH * (72.0f / 96.0f);
            }

            float fontSize = std::max(2.0f, pdfH);

            auto hasArabicLetters = [](const std::wstring& str) -> bool {
                for (wchar_t ch : str) {
                    if ((ch >= 0x0621 && ch <= 0x064A) ||
                        (ch >= 0x066E && ch <= 0x06D3) ||
                        (ch >= 0xFB50 && ch <= 0xFDFF) ||
                        (ch >= 0xFE70 && ch <= 0xFEFF)) {
                        return true;
                    }
                }
                return false;
            };

            if (hasArabicLetters(wd.text)) {
                // Arabic / RTL text:
                // Emit individual glyphs positioned Right-to-Left within the word's bounding box.
                // This ensures physical alignment with the underlying scanned glyphs and prevents
                // PDF parsers from misinterpreting visual-order vs logical-order sequences.
                float charW = (wd.text.size() > 0) ? (pdfW / (float)wd.text.size()) : pdfW;
                float charTz = (fontSize > 0.0f) ? (charW / fontSize) * 100.0f : 100.0f;
                if (charTz < 5.0f) charTz = 5.0f;
                if (charTz > 2000.0f) charTz = 2000.0f;

                char fBuf[128];
                snprintf(fBuf, sizeof(fBuf), "BT\n/F_OCR %.2f Tf\n%.2f Tz\n", fontSize, charTz);
                contentStream += fBuf;

                for (size_t ci = 0; ci < wd.text.size(); ++ci) {
                    float charX = pdfX + (float)(wd.text.size() - 1 - ci) * charW;
                    char opBuf[128];
                    snprintf(opBuf, sizeof(opBuf), "1 0 0 1 %.2f %.2f Tm\n<%04X> Tj\n",
                             charX, pdfY, (uint16_t)wd.text[ci]);
                    contentStream += opBuf;
                }
                contentStream += "ET\n";
            } else {
                // LTR words (Latin, digits, symbols)
                float unscaledW = (float)wd.text.size() * fontSize;
                float tz = (unscaledW > 0.0f) ? (pdfW / unscaledW) * 100.0f : 100.0f;
                if (tz < 5.0f) tz = 5.0f;
                if (tz > 2000.0f) tz = 2000.0f;

                std::string hexText;
                for (wchar_t ch : wd.text) {
                    char hexBuf[8];
                    snprintf(hexBuf, sizeof(hexBuf), "%04X", (uint16_t)ch);
                    hexText += hexBuf;
                }

                char opBuf[256];
                snprintf(opBuf, sizeof(opBuf), "BT\n/F_OCR %.2f Tf\n%.2f Tz\n1 0 0 1 %.2f %.2f Tm\n<%s> Tj\nET\n",
                         fontSize, tz, pdfX, pdfY, hexText.c_str());
                contentStream += opBuf;
            }
        }

        contentStream += "Q\n";

        // Create new content stream object
        uint32_t objContent = ++maxObjNum;
        appendObject(objContent,
            "<< /Length " + std::to_string(contentStream.size()) + " >>\n"
            "stream\r\n" + contentStream + "\r\nendstream"
        );

        // Update Page dictionary:
        std::string updatedPage = pageDict;

        // Strip surrounding << and >> for clean manipulation
        size_t dStart = updatedPage.find("<<");
        size_t dEnd = updatedPage.rfind(">>");
        if (dStart != std::string::npos && dEnd != std::string::npos && dEnd > dStart + 1) {
            std::string body = updatedPage.substr(dStart + 2, dEnd - dStart - 2);

            // a. Update /Contents
            size_t cPos = body.find("/Contents");
            if (cPos != std::string::npos) {
                size_t afterC = body.find_first_not_of(" \t\r\n", cPos + 9);
                if (afterC != std::string::npos) {
                    if (body[afterC] == '[') {
                        size_t bClose = body.find(']', afterC);
                        if (bClose != std::string::npos) {
                            body.insert(bClose, " " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ");
                            body.insert(afterC + 1, " " + std::to_string(objQOpen) + " 0 R ");
                        }
                    } else if (body[afterC] >= '0' && body[afterC] <= '9') {
                        // Indirect ref "12 0 R"
                        size_t rPos = body.find('R', afterC);
                        if (rPos != std::string::npos) {
                            size_t refEnd = rPos + 1;
                            std::string oldRef = body.substr(afterC, refEnd - afterC);
                            std::string newArray = "[ " + std::to_string(objQOpen) + " 0 R " + oldRef + " " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ]";
                            body.replace(afterC, refEnd - afterC, newArray);
                        }
                    }
                }
            } else {
                body += " /Contents [ " + std::to_string(objQOpen) + " 0 R " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ] ";
            }

            // b. Update /Resources
            uint32_t resObjNum = 0;
            if (parser.FindIndirectRef(pageDict, "/Resources", resObjNum) && resObjNum > 0) {
                // Indirect /Resources dictionary
                if (updatedResourceDicts.find(resObjNum) == updatedResourceDicts.end()) {
                    std::string resStr = parser.GetObjectString(resObjNum);
                    size_t resDStart = resStr.find("<<");
                    size_t resDEnd = resStr.rfind(">>");
                    if (resDStart != std::string::npos && resDEnd != std::string::npos) {
                        std::string resBody = resStr.substr(resDStart + 2, resDEnd - resDStart - 2);
                        size_t fontPos = resBody.find("/Font");
                        if (fontPos != std::string::npos) {
                            size_t fDictStart = resBody.find("<<", fontPos);
                            if (fDictStart != std::string::npos) {
                                resBody.insert(fDictStart + 2, " /F_OCR " + std::to_string(objType0Font) + " 0 R ");
                            }
                        } else {
                            resBody += " /Font << /F_OCR " + std::to_string(objType0Font) + " 0 R >> ";
                        }
                        std::string newResStr = "<< " + resBody + " >>";
                        updatedResourceDicts[resObjNum] = newResStr;
                    }
                }
            } else {
                // Inline /Resources or inherited
                size_t resPos = body.find("/Resources");
                if (resPos != std::string::npos) {
                    size_t rDictStart = body.find("<<", resPos);
                    if (rDictStart != std::string::npos) {
                        size_t fontPos = body.find("/Font", rDictStart);
                        if (fontPos != std::string::npos) {
                            size_t fDictStart = body.find("<<", fontPos);
                            if (fDictStart != std::string::npos) {
                                body.insert(fDictStart + 2, " /F_OCR " + std::to_string(objType0Font) + " 0 R ");
                            }
                        } else {
                            body.insert(rDictStart + 2, " /Font << /F_OCR " + std::to_string(objType0Font) + " 0 R >> ");
                        }
                    }
                } else {
                    body += " /Resources << /Font << /F_OCR " + std::to_string(objType0Font) + " 0 R >> >> ";
                }
            }

            updatedPage = "<< " + body + " >>";
        }

        appendObject(pageObjNum, updatedPage);

        if (progressCallback) {
            float pct = 0.1f + 0.7f * ((float)(pIdx + 1) / (float)ocrPages.size());
            progressCallback(pct, L"Injecting searchable text layer...");
        }
    }

    // Append any updated indirect resource objects
    for (const auto& kv : updatedResourceDicts) {
        appendObject(kv.first, kv.second);
    }

    // 6. Append classic XRef table and Trailer
    size_t startXrefOffset = origFileSize + incUpdate.size();
    incUpdate += "xref\n";

    // Collect all updated object IDs
    std::vector<uint32_t> modObjs;
    for (const auto& kv : newOffsets) {
        modObjs.push_back(kv.first);
    }
    std::sort(modObjs.begin(), modObjs.end());

    // Group into contiguous sub-sections
    size_t idx = 0;
    while (idx < modObjs.size()) {
        uint32_t first = modObjs[idx];
        size_t count = 1;
        while (idx + count < modObjs.size() && modObjs[idx + count] == first + count) {
            count++;
        }
        incUpdate += std::to_string(first) + " " + std::to_string(count) + "\n";
        for (size_t k = 0; k < count; ++k) {
            char entryBuf[32];
            snprintf(entryBuf, sizeof(entryBuf), "%010llu 00000 n \n", (unsigned long long)newOffsets[first + k]);
            incUpdate += entryBuf;
        }
        idx += count;
    }

    // Trailer
    incUpdate += "trailer\n<<\n";
    incUpdate += "  /Size " + std::to_string(maxObjNum + 1) + "\n";
    if (rootObjNum > 0) {
        incUpdate += "  /Root " + std::to_string(rootObjNum) + " 0 R\n";
    }
    if (infoObjNum > 0) {
        incUpdate += "  /Info " + std::to_string(infoObjNum) + " 0 R\n";
    }
    if (oldStartXref > 0) {
        incUpdate += "  /Prev " + std::to_string(oldStartXref) + "\n";
    }
    if (!idStr.empty()) {
        incUpdate += "  /ID " + idStr + "\n";
    }
    incUpdate += ">>\n";
    incUpdate += "startxref\n";
    incUpdate += std::to_string(startXrefOffset) + "\n";
    incUpdate += "%%EOF\n";

    // 7. Write to destination file
    bool isOverwrite = (_wcsicmp(srcPdfPath.c_str(), dstPdfPath.c_str()) == 0);
    std::wstring writeTarget = isOverwrite ? (srcPdfPath + L".tmp") : dstPdfPath;

    if (progressCallback) progressCallback(0.9f, L"Writing searchable PDF...");

    {
        std::ofstream out(writeTarget, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        // Write original bytes
        out.write(parser.m_bufferView.data(), parser.m_bufferView.size());
        // Append incremental update
        out.write(incUpdate.data(), incUpdate.size());
        out.close();
    }

    // Verify written file is non-empty
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(writeTarget.c_str(), GetFileExInfoStandard, &fad) ||
        fad.nFileSizeLow < origFileSize + incUpdate.size()) {
        if (isOverwrite) DeleteFileW(writeTarget.c_str());
        return false;
    }

    // Release parser handle before possible file replacement
    parser.Close();

    if (isOverwrite) {
        if (!ReplaceFileW(srcPdfPath.c_str(), writeTarget.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            // Fallback: MoveFileEx with replace
            if (!MoveFileExW(writeTarget.c_str(), srcPdfPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(writeTarget.c_str());
                return false;
            }
        }
    }

    if (progressCallback) progressCallback(1.0f, L"Done");
    return true;
}

bool PdfSearchableWriter::WriteWordCorrections(
    const std::wstring& srcPdfPath,
    const std::wstring& dstPdfPath,
    const std::vector<WordCorrectionItem>& corrections,
    std::function<void(float progress, const std::wstring& status)> progressCallback
) {
    if (srcPdfPath.empty() || dstPdfPath.empty()) return false;
    if (corrections.empty()) {
        if (_wcsicmp(srcPdfPath.c_str(), dstPdfPath.c_str()) != 0) {
            return CopyFileW(srcPdfPath.c_str(), dstPdfPath.c_str(), FALSE) != 0;
        }
        return true;
    }

    PdfParser parser;
    if (!parser.Load(srcPdfPath)) {
        return false;
    }

    if (parser.m_bufferView.empty() || parser.m_pageObjectNums.empty()) {
        return false;
    }

    if (progressCallback) progressCallback(0.1f, L"Preparing PDF word corrections...");

    size_t origFileSize = parser.m_bufferView.size();
    size_t oldStartXref = parser.m_startXrefOffset;
    uint32_t rootObjNum = parser.m_rootObjNum;
    uint32_t infoObjNum = parser.m_infoObjNum;
    std::string idStr = parser.m_idString;

    uint32_t maxObjNum = 0;
    for (const auto& kv : parser.m_xref) {
        if (kv.first > maxObjNum) maxObjNum = kv.first;
    }
    if (maxObjNum == 0) maxObjNum = 1;

    auto hasArabicLetters = [](const std::wstring& str) -> bool {
        for (wchar_t ch : str) {
            if ((ch >= 0x0621 && ch <= 0x064A) ||
                (ch >= 0x066E && ch <= 0x06D3) ||
                (ch >= 0xFB50 && ch <= 0xFDFF) ||
                (ch >= 0xFE70 && ch <= 0xFEFF)) {
                return true;
            }
        }
        return false;
    };

    bool anyArabic = false;
    for (const auto& item : corrections) {
        if (hasArabicLetters(item.newText)) {
            anyArabic = true;
            break;
        }
    }

    std::string incUpdate;
    incUpdate.reserve(anyArabic ? (2 * 1024 * 1024) : (256 * 1024));

    std::map<uint32_t, size_t> newOffsets;
    auto appendObject = [&](uint32_t objNum, const std::string& objBody) {
        newOffsets[objNum] = origFileSize + incUpdate.size();
        incUpdate += std::to_string(objNum) + " 0 obj\n";
        incUpdate += objBody;
        if (!objBody.empty() && objBody.back() != '\n') incUpdate += "\n";
        incUpdate += "endobj\n";
    };

    // Shield against CTM leakage from preceding streams
    uint32_t objQOpen = ++maxObjNum;
    appendObject(objQOpen,
        "<< /Length 2 >>\n"
        "stream\r\nq\n\r\nendstream"
    );

    uint32_t objQClose = ++maxObjNum;
    appendObject(objQClose,
        "<< /Length 2 >>\n"
        "stream\r\nQ\n\r\nendstream"
    );

    uint32_t fontObjToUse = 0;

    struct GlyphEntry {
        uint16_t gid;
        float width;
        std::wstring logicalChars;
    };
    std::map<uint16_t, GlyphEntry> gidTable;

    HDC hdc = NULL;
    HFONT hFont = NULL;
    HGDIOBJ oldFont = NULL;

    if (anyArabic) {
        hdc = CreateCompatibleDC(NULL);
        hFont = CreateFontW(
            -1000, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial"
        );
        oldFont = SelectObject(hdc, hFont);

        DWORD fontDataSize = GetFontData(hdc, 0, 0, NULL, 0);
        std::vector<uint8_t> fontData;
        if (fontDataSize != GDI_ERROR && fontDataSize > 0) {
            fontData.resize(fontDataSize);
            GetFontData(hdc, 0, 0, fontData.data(), fontDataSize);
        }

        if (fontData.empty()) {
            anyArabic = false;
        } else {
            auto registerGlyph = [&](uint16_t shapedChar, const std::wstring& logical) -> uint16_t {
                WORD gid = 0;
                GetGlyphIndicesW(hdc, (LPCWSTR)&shapedChar, 1, &gid, 0);
                if (gidTable.find(gid) == gidTable.end()) {
                    ABCFLOAT abc = {};
                    GetCharABCWidthsFloatW(hdc, shapedChar, shapedChar, &abc);
                    float w = abc.abcfA + abc.abcfB + abc.abcfC;
                    if (w <= 0.0f) w = 500.0f;
                    gidTable[gid] = { gid, w, logical };
                }
                return gid;
            };

            for (const auto& item : corrections) {
                if (hasArabicLetters(item.newText)) {
                    auto shaped = ShapeArabicWord(item.newText);
                    for (const auto& sg : shaped) {
                        registerGlyph(sg.shapedChar, sg.logicalChars);
                    }
                } else {
                    for (wchar_t ch : item.newText) {
                        registerGlyph((uint16_t)ch, std::wstring(1, ch));
                    }
                }
            }

            registerGlyph(0x0020, L" ");

            std::vector<GlyphEntry> allEntries;
            for (const auto& kv : gidTable) {
                allEntries.push_back(kv.second);
            }

            std::string toUnicodeStream;
            toUnicodeStream += "/CIDInit /ProcSet findresource begin\n";
            toUnicodeStream += "12 dict begin\n";
            toUnicodeStream += "begincmap\n";
            toUnicodeStream += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
            toUnicodeStream += "/CMapName /LightPDF-Edit-ToUnicode def\n";
            toUnicodeStream += "/CMapType 2 def\n";
            toUnicodeStream += "1 begincodespacerange\n";
            toUnicodeStream += "<0000> <FFFF>\n";
            toUnicodeStream += "endcodespacerange\n";

            for (size_t i = 0; i < allEntries.size(); i += 100) {
                size_t count = std::min((size_t)100, allEntries.size() - i);
                toUnicodeStream += std::to_string(count) + " beginbfchar\n";
                for (size_t k = 0; k < count; ++k) {
                    const auto& ge = allEntries[i + k];
                    std::string logHex;
                    for (wchar_t wc : ge.logicalChars) {
                        char h[8];
                        snprintf(h, sizeof(h), "%04X", (uint16_t)wc);
                        logHex += h;
                    }
                    char buf[64];
                    snprintf(buf, sizeof(buf), "<%04X> <%s>\n", ge.gid, logHex.c_str());
                    toUnicodeStream += buf;
                }
                toUnicodeStream += "endbfchar\n";
            }

            toUnicodeStream += "endcmap\n";
            toUnicodeStream += "CMapName currentdict /CMap defineresource pop\n";
            toUnicodeStream += "end\nend\n";

            std::string wArray = "[ ";
            for (const auto& ge : allEntries) {
                wArray += std::to_string(ge.gid) + " [ " + std::to_string((int)std::round(ge.width)) + " ] ";
            }
            wArray += "]";

            uint32_t objFontFile2 = ++maxObjNum;
            uint32_t objFontDescriptor = ++maxObjNum;
            uint32_t objDescendantFont = ++maxObjNum;
            uint32_t objToUnicode = ++maxObjNum;
            uint32_t objType0Font = ++maxObjNum;

            appendObject(objFontFile2,
                "<<\n"
                "  /Length " + std::to_string(fontData.size()) + "\n"
                "  /Length1 " + std::to_string(fontData.size()) + "\n"
                ">>\nstream\r\n" +
                std::string(reinterpret_cast<const char*>(fontData.data()), fontData.size()) +
                "\r\nendstream"
            );

            appendObject(objFontDescriptor,
                "<<\n"
                "  /Type /FontDescriptor\n"
                "  /FontName /Arial\n"
                "  /Flags 32\n"
                "  /FontBBox [ -665 -325 2000 1006 ]\n"
                "  /Ascent 905\n"
                "  /Descent -212\n"
                "  /CapHeight 716\n"
                "  /StemV 80\n"
                "  /FontFile2 " + std::to_string(objFontFile2) + " 0 R\n"
                ">>"
            );

            appendObject(objDescendantFont,
                "<<\n"
                "  /Type /Font\n"
                "  /Subtype /CIDFontType2\n"
                "  /BaseFont /Arial\n"
                "  /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>\n"
                "  /FontDescriptor " + std::to_string(objFontDescriptor) + " 0 R\n"
                "  /DW 1000\n"
                "  /W " + wArray + "\n"
                "  /CIDToGIDMap /Identity\n"
                ">>"
            );

            appendObject(objType0Font,
                "<<\n"
                "  /Type /Font\n"
                "  /Subtype /Type0\n"
                "  /BaseFont /Arial\n"
                "  /Encoding /Identity-H\n"
                "  /DescendantFonts [ " + std::to_string(objDescendantFont) + " 0 R ]\n"
                "  /ToUnicode " + std::to_string(objToUnicode) + " 0 R\n"
                ">>"
            );

            appendObject(objToUnicode,
                "<< /Length " + std::to_string(toUnicodeStream.size()) + " >>\n"
                "stream\r\n" + toUnicodeStream + "\r\nendstream"
            );

            fontObjToUse = objType0Font;
        }
    }

    if (!anyArabic) {
        uint32_t objType1Font = ++maxObjNum;
        appendObject(objType1Font,
            "<<\n"
            "  /Type /Font\n"
            "  /Subtype /Type1\n"
            "  /BaseFont /Helvetica\n"
            "  /Encoding /WinAnsiEncoding\n"
            ">>"
        );
        fontObjToUse = objType1Font;
    }

    // Group corrections by page index
    std::map<uint32_t, std::vector<WordCorrectionItem>> pageMap;
    for (const auto& item : corrections) {
        pageMap[item.pageIndex].push_back(item);
    }

    std::map<uint32_t, std::string> updatedResourceDicts;

    size_t processedPages = 0;
    for (const auto& kv : pageMap) {
        uint32_t pageIndex = kv.first;
        const auto& pageItems = kv.second;
        if (pageIndex >= parser.m_pageObjectNums.size()) continue;

        uint32_t pageObjNum = parser.m_pageObjectNums[pageIndex];
        std::string pageDict = parser.GetObjectString(pageObjNum);
        if (pageDict.empty()) continue;

        float x0 = 0.0f, y0 = 0.0f, x1 = 595.28f, y1 = 841.89f;
        if (!ParseBoxHelper(pageDict, "/CropBox", x0, y0, x1, y1)) {
            if (!ParseBoxHelper(pageDict, "/MediaBox", x0, y0, x1, y1)) {
                uint32_t parentObj = 0;
                if (parser.FindIndirectRef(pageDict, "/Parent", parentObj)) {
                    std::string parentDict = parser.GetObjectString(parentObj);
                    if (!ParseBoxHelper(parentDict, "/CropBox", x0, y0, x1, y1)) {
                        ParseBoxHelper(parentDict, "/MediaBox", x0, y0, x1, y1);
                    }
                }
            }
        }
        float cropH = std::max(1.0f, y1 - y0);

        int rotate = ParseRotateHelper(pageDict);
        if (rotate == 0) {
            uint32_t parentObj = 0;
            if (parser.FindIndirectRef(pageDict, "/Parent", parentObj)) {
                std::string parentDict = parser.GetObjectString(parentObj);
                rotate = ParseRotateHelper(parentDict);
            }
        }

        // Build visible correction content stream
        std::string contentStream;
        contentStream += "q\n";

        for (const auto& item : pageItems) {
            float dipLeft = item.pageDipRect.left;
            float dipTop = item.pageDipRect.top;
            float dipRight = item.pageDipRect.right;
            float dipBottom = item.pageDipRect.bottom;
            float dipW = dipRight - dipLeft;
            float dipH = dipBottom - dipTop;
            if (dipW <= 0.5f || dipH <= 0.5f) continue;

            float pdfX = 0.0f, pdfY = 0.0f, pdfW = 0.0f, pdfH = 0.0f;
            if (rotate == 90) {
                pdfX = x0 + dipTop * (72.0f / 96.0f);
                pdfY = y0 + dipLeft * (72.0f / 96.0f);
                pdfW = dipH * (72.0f / 96.0f);
                pdfH = dipW * (72.0f / 96.0f);
            } else if (rotate == 180) {
                pdfX = x1 - dipRight * (72.0f / 96.0f);
                pdfY = y0 + dipTop * (72.0f / 96.0f);
                pdfW = dipW * (72.0f / 96.0f);
                pdfH = dipH * (72.0f / 96.0f);
            } else if (rotate == 270) {
                pdfX = x1 - dipBottom * (72.0f / 96.0f);
                pdfY = y1 - dipRight * (72.0f / 96.0f);
                pdfW = dipH * (72.0f / 96.0f);
                pdfH = dipW * (72.0f / 96.0f);
            } else {
                pdfX = x0 + dipLeft * (72.0f / 96.0f);
                pdfY = y0 + (cropH - (dipBottom * (72.0f / 96.0f)));
                pdfW = dipW * (72.0f / 96.0f);
                pdfH = dipH * (72.0f / 96.0f);
            }

            // 1. Draw exact white cover rectangle (no padding to preserve table grid lines)
            float coverH = std::max(1.0f, pdfH - 0.5f);
            char coverBuf[128];
            snprintf(coverBuf, sizeof(coverBuf),
                     "q 1 1 1 rg %.2f %.2f %.2f %.2f re f Q\n",
                     pdfX, pdfY, pdfW, coverH);
            contentStream += coverBuf;

            if (item.newText.empty()) continue;

            // 2. Set color matching original font
            float cr = ((item.color >> 16) & 0xFF) / 255.0f;
            float cg = ((item.color >> 8) & 0xFF) / 255.0f;
            float cb = (item.color & 0xFF) / 255.0f;
            char colBuf[64];
            snprintf(colBuf, sizeof(colBuf), "%.3f %.3f %.3f rg\n", cr, cg, cb);
            contentStream += colBuf;

            // 3. Render mode 0 Tr = fill text (visible!)
            contentStream += "0 Tr\n";

            float fontSize = std::max(2.0f, pdfH * 0.88f);
            float textBaseline = pdfY + 0.20f * fontSize;

            if (anyArabic) {
                if (hasArabicLetters(item.newText)) {
                    auto shaped = ShapeArabicWord(item.newText);
                    struct VisGlyph { uint16_t gid; float w; };
                    std::vector<VisGlyph> glyphs;
                    float totalGlyphW = 0.0f;
                    for (const auto& sg : shaped) {
                        WORD gid = 0;
                        GetGlyphIndicesW(hdc, (LPCWSTR)&sg.shapedChar, 1, &gid, 0);
                        float gw = 500.0f;
                        auto itG = gidTable.find(gid);
                        if (itG != gidTable.end()) gw = itG->second.width;
                        glyphs.push_back({ gid, gw });
                        totalGlyphW += gw;
                    }
                    std::reverse(glyphs.begin(), glyphs.end());

                    float renderedW = (totalGlyphW / 1000.0f) * fontSize;
                    float tz = 100.0f;
                    float startX = pdfX;
                    if (renderedW > pdfW && pdfW > 0.0f) {
                        tz = (pdfW / renderedW) * 100.0f;
                        if (tz < 40.0f) tz = 40.0f;
                    } else if (pdfW > renderedW) {
                        startX = pdfX + (pdfW - renderedW);
                    }

                    std::string hexGIDs;
                    for (const auto& g : glyphs) {
                        char hBuf[8];
                        snprintf(hBuf, sizeof(hBuf), "%04X", g.gid);
                        hexGIDs += hBuf;
                    }

                    char opBuf[256];
                    snprintf(opBuf, sizeof(opBuf),
                             "BT\n/F_EDIT %.2f Tf\n%.2f Tz\n1 0 0 1 %.2f %.2f Tm\n<%s> Tj\nET\n",
                             fontSize, tz, startX, textBaseline, hexGIDs.c_str());
                    contentStream += opBuf;
                } else {
                    struct VisGlyph { uint16_t gid; float w; };
                    std::vector<VisGlyph> glyphs;
                    float totalGlyphW = 0.0f;
                    for (wchar_t ch : item.newText) {
                        WORD gid = 0;
                        uint16_t sc = (uint16_t)ch;
                        GetGlyphIndicesW(hdc, (LPCWSTR)&sc, 1, &gid, 0);
                        float gw = 500.0f;
                        auto itG = gidTable.find(gid);
                        if (itG != gidTable.end()) gw = itG->second.width;
                        glyphs.push_back({ gid, gw });
                        totalGlyphW += gw;
                    }

                    float renderedW = (totalGlyphW / 1000.0f) * fontSize;
                    float tz = 100.0f;
                    if (renderedW > pdfW && pdfW > 0.0f) {
                        tz = (pdfW / renderedW) * 100.0f;
                        if (tz < 40.0f) tz = 40.0f;
                    }

                    std::string hexGIDs;
                    for (const auto& g : glyphs) {
                        char hBuf[8];
                        snprintf(hBuf, sizeof(hBuf), "%04X", g.gid);
                        hexGIDs += hBuf;
                    }

                    char opBuf[256];
                    snprintf(opBuf, sizeof(opBuf),
                             "BT\n/F_EDIT %.2f Tf\n%.2f Tz\n1 0 0 1 %.2f %.2f Tm\n<%s> Tj\nET\n",
                             fontSize, tz, pdfX, textBaseline, hexGIDs.c_str());
                    contentStream += opBuf;
                }
            } else {
                float unscaledW = (float)item.newText.size() * (fontSize * 0.55f);
                float tz = 100.0f;
                if (unscaledW > pdfW && pdfW > 0.0f) {
                    tz = (pdfW / unscaledW) * 100.0f;
                    if (tz < 40.0f) tz = 40.0f;
                }

                std::string hexText;
                for (wchar_t ch : item.newText) {
                    char hexBuf[8];
                    snprintf(hexBuf, sizeof(hexBuf), "%02X", (uint8_t)(ch & 0xFF));
                    hexText += hexBuf;
                }

                char opBuf[256];
                snprintf(opBuf, sizeof(opBuf),
                         "BT\n/F_EDIT %.2f Tf\n%.2f Tz\n1 0 0 1 %.2f %.2f Tm\n<%s> Tj\nET\n",
                         fontSize, tz, pdfX, textBaseline, hexText.c_str());
                contentStream += opBuf;
            }
        }

        contentStream += "Q\n";

        uint32_t objContent = ++maxObjNum;
        appendObject(objContent,
            "<< /Length " + std::to_string(contentStream.size()) + " >>\n"
            "stream\r\n" + contentStream + "\r\nendstream"
        );

        // Update Page dictionary
        std::string updatedPage = pageDict;
        size_t dStart = updatedPage.find("<<");
        size_t dEnd = updatedPage.rfind(">>");
        if (dStart != std::string::npos && dEnd != std::string::npos && dEnd > dStart + 1) {
            std::string body = updatedPage.substr(dStart + 2, dEnd - dStart - 2);

            // Update /Contents
            size_t cPos = body.find("/Contents");
            if (cPos != std::string::npos) {
                size_t afterC = body.find_first_not_of(" \t\r\n", cPos + 9);
                if (afterC != std::string::npos) {
                    if (body[afterC] == '[') {
                        size_t bClose = body.find(']', afterC);
                        if (bClose != std::string::npos) {
                            body.insert(bClose, " " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ");
                            body.insert(afterC + 1, " " + std::to_string(objQOpen) + " 0 R ");
                        }
                    } else if (body[afterC] >= '0' && body[afterC] <= '9') {
                        size_t rPos = body.find('R', afterC);
                        if (rPos != std::string::npos) {
                            size_t refEnd = rPos + 1;
                            std::string oldRef = body.substr(afterC, refEnd - afterC);
                            std::string newArray = "[ " + std::to_string(objQOpen) + " 0 R " + oldRef + " " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ]";
                            body.replace(afterC, refEnd - afterC, newArray);
                        }
                    }
                }
            } else {
                body += " /Contents [ " + std::to_string(objQOpen) + " 0 R " + std::to_string(objQClose) + " 0 R " + std::to_string(objContent) + " 0 R ] ";
            }

            // Update /Resources
            uint32_t resObjNum = 0;
            if (parser.FindIndirectRef(pageDict, "/Resources", resObjNum) && resObjNum > 0) {
                if (updatedResourceDicts.find(resObjNum) == updatedResourceDicts.end()) {
                    std::string resStr = parser.GetObjectString(resObjNum);
                    size_t resDStart = resStr.find("<<");
                    size_t resDEnd = resStr.rfind(">>");
                    if (resDStart != std::string::npos && resDEnd != std::string::npos) {
                        std::string resBody = resStr.substr(resDStart + 2, resDEnd - resDStart - 2);
                        size_t fontPos = resBody.find("/Font");
                        if (fontPos != std::string::npos) {
                            size_t fDictStart = resBody.find("<<", fontPos);
                            if (fDictStart != std::string::npos) {
                                resBody.insert(fDictStart + 2, " /F_EDIT " + std::to_string(fontObjToUse) + " 0 R ");
                            }
                        } else {
                            resBody += " /Font << /F_EDIT " + std::to_string(fontObjToUse) + " 0 R >> ";
                        }
                        std::string newResStr = "<< " + resBody + " >>";
                        updatedResourceDicts[resObjNum] = newResStr;
                    }
                }
            } else {
                size_t resPos = body.find("/Resources");
                if (resPos != std::string::npos) {
                    size_t rDictStart = body.find("<<", resPos);
                    if (rDictStart != std::string::npos) {
                        size_t fontPos = body.find("/Font", rDictStart);
                        if (fontPos != std::string::npos) {
                            size_t fDictStart = body.find("<<", fontPos);
                            if (fDictStart != std::string::npos) {
                                body.insert(fDictStart + 2, " /F_EDIT " + std::to_string(fontObjToUse) + " 0 R ");
                            }
                        } else {
                            body.insert(rDictStart + 2, " /Font << /F_EDIT " + std::to_string(fontObjToUse) + " 0 R >> ");
                        }
                    }
                } else {
                    body += " /Resources << /Font << /F_EDIT " + std::to_string(fontObjToUse) + " 0 R >> >> ";
                }
            }

            updatedPage = "<< " + body + " >>";
        }

        appendObject(pageObjNum, updatedPage);

        processedPages++;
        if (progressCallback) {
            float pct = 0.2f + 0.6f * ((float)processedPages / (float)pageMap.size());
            progressCallback(pct, L"Injecting word corrections...");
        }
    }

    if (hdc) {
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        hdc = NULL;
    }

    for (const auto& kv : updatedResourceDicts) {
        appendObject(kv.first, kv.second);
    }

    // Classic XRef table and Trailer
    size_t startXrefOffset = origFileSize + incUpdate.size();
    incUpdate += "xref\n";

    std::vector<uint32_t> modObjs;
    for (const auto& kv : newOffsets) {
        modObjs.push_back(kv.first);
    }
    std::sort(modObjs.begin(), modObjs.end());

    size_t idx = 0;
    while (idx < modObjs.size()) {
        uint32_t first = modObjs[idx];
        size_t count = 1;
        while (idx + count < modObjs.size() && modObjs[idx + count] == first + count) {
            count++;
        }
        incUpdate += std::to_string(first) + " " + std::to_string(count) + "\n";
        for (size_t k = 0; k < count; ++k) {
            char entryBuf[32];
            snprintf(entryBuf, sizeof(entryBuf), "%010llu 00000 n \n", (unsigned long long)newOffsets[first + k]);
            incUpdate += entryBuf;
        }
        idx += count;
    }

    incUpdate += "trailer\n<<\n";
    incUpdate += "  /Size " + std::to_string(maxObjNum + 1) + "\n";
    if (rootObjNum > 0) {
        incUpdate += "  /Root " + std::to_string(rootObjNum) + " 0 R\n";
    }
    if (infoObjNum > 0) {
        incUpdate += "  /Info " + std::to_string(infoObjNum) + " 0 R\n";
    }
    if (oldStartXref > 0) {
        incUpdate += "  /Prev " + std::to_string(oldStartXref) + "\n";
    }
    if (!idStr.empty()) {
        incUpdate += "  /ID " + idStr + "\n";
    }
    incUpdate += ">>\n";
    incUpdate += "startxref\n";
    incUpdate += std::to_string(startXrefOffset) + "\n";
    incUpdate += "%%EOF\n";

    bool isOverwrite = (_wcsicmp(srcPdfPath.c_str(), dstPdfPath.c_str()) == 0);
    std::wstring writeTarget = isOverwrite ? (srcPdfPath + L".tmp") : dstPdfPath;

    if (progressCallback) progressCallback(0.9f, L"Writing corrected PDF...");

    {
        std::ofstream out(writeTarget, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        out.write(parser.m_bufferView.data(), parser.m_bufferView.size());
        out.write(incUpdate.data(), incUpdate.size());
        out.close();
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(writeTarget.c_str(), GetFileExInfoStandard, &fad) ||
        fad.nFileSizeLow < origFileSize + incUpdate.size()) {
        if (isOverwrite) DeleteFileW(writeTarget.c_str());
        return false;
    }

    parser.Close();

    if (isOverwrite) {
        if (!ReplaceFileW(srcPdfPath.c_str(), writeTarget.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            if (!MoveFileExW(writeTarget.c_str(), srcPdfPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(writeTarget.c_str());
                return false;
            }
        }
    }

    if (progressCallback) progressCallback(1.0f, L"Done");
    return true;
}

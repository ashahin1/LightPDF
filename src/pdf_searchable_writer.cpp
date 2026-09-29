#include "pdf_searchable_writer.hpp"
#include "pdf_parser.hpp"
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cmath>

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
                            body.insert(bClose, " " + std::to_string(objContent) + " 0 R ");
                        }
                    } else if (body[afterC] >= '0' && body[afterC] <= '9') {
                        // Indirect ref "12 0 R"
                        size_t rPos = body.find('R', afterC);
                        if (rPos != std::string::npos) {
                            size_t refEnd = rPos + 1;
                            std::string oldRef = body.substr(afterC, refEnd - afterC);
                            std::string newArray = "[ " + oldRef + " " + std::to_string(objContent) + " 0 R ]";
                            body.replace(afterC, refEnd - afterC, newArray);
                        }
                    }
                }
            } else {
                body += " /Contents " + std::to_string(objContent) + " 0 R ";
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
            snprintf(entryBuf, sizeof(entryBuf), "%010llu 00000 n \n", (unsigned long long)newOffsets[static_cast<uint32_t>(first + k)]);
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

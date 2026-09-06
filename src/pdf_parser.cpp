#include "pdf_parser.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace MiniZlib {
    struct BitStream {
        const uint8_t* in;
        size_t in_len;
        size_t in_pos;
        uint32_t bit_buf;
        int bit_cnt;

        BitStream(const uint8_t* data, size_t len) : in(data), in_len(len), in_pos(0), bit_buf(0), bit_cnt(0) {}

        uint32_t get_bits(int n) {
            while (bit_cnt < n) {
                if (in_pos < in_len) {
                    bit_buf |= ((uint32_t)in[in_pos++]) << bit_cnt;
                }
                bit_cnt += 8;
            }
            uint32_t val = bit_buf & ((1U << n) - 1);
            bit_buf >>= n;
            bit_cnt -= n;
            return val;
        }
    };

    struct Huffman {
        uint16_t count[16];
        uint16_t symbol[288];

        bool build(const uint8_t* lengths, int num) {
            memset(count, 0, sizeof(count));
            for (int i = 0; i < num; ++i) count[lengths[i]]++;
            count[0] = 0;

            uint16_t offs[16];
            offs[1] = 0;
            for (int i = 1; i < 15; ++i) offs[i + 1] = offs[i] + count[i];

            for (int i = 0; i < num; ++i) {
                if (lengths[i]) {
                    symbol[offs[lengths[i]]++] = (uint16_t)i;
                }
            }
            return true;
        }

        int decode(BitStream& bs) {
            uint16_t code = 0;
            uint16_t first = 0;
            uint16_t index = 0;
            for (int len = 1; len <= 15; ++len) {
                code |= (uint16_t)bs.get_bits(1);
                uint16_t count_len = count[len];
                if (code < first + count_len) {
                    return symbol[index + (code - first)];
                }
                index += count_len;
                first += count_len;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }
    };

    static const uint8_t fixed_lit_lengths[288] = {
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        8,8,8,8,8,8,8,8
    };
    static const uint8_t fixed_dist_lengths[32] = {
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
    };
    static const uint16_t len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const uint8_t len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const uint16_t dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const uint8_t dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    bool Decompress(const uint8_t* in_data, size_t in_size, std::vector<uint8_t>& out) {
        if (!in_data || in_size < 2) return false;

        size_t start = 0;
        if ((in_data[0] == 0x78) && ((in_data[0] * 256 + in_data[1]) % 31 == 0)) {
            start = 2; // Skip 2-byte zlib header
        }

        BitStream bs(in_data + start, in_size - start);
        bool bfinal = false;

        while (!bfinal) {
            bfinal = bs.get_bits(1) != 0;
            int btype = bs.get_bits(2);

            if (btype == 0) { // Uncompressed block
                bs.bit_buf = 0;
                bs.bit_cnt = 0;
                if (bs.in_pos + 4 > bs.in_len) return false;
                uint16_t len = bs.in[bs.in_pos] | (bs.in[bs.in_pos + 1] << 8);
                bs.in_pos += 4;
                if (bs.in_pos + len > bs.in_len) return false;
                out.insert(out.end(), bs.in + bs.in_pos, bs.in + bs.in_pos + len);
                bs.in_pos += len;
            } else if (btype == 1 || btype == 2) {
                Huffman lit_huff, dist_huff;
                if (btype == 1) {
                    lit_huff.build(fixed_lit_lengths, 288);
                    dist_huff.build(fixed_dist_lengths, 32);
                } else {
                    int hlit = bs.get_bits(5) + 257;
                    int hdist = bs.get_bits(5) + 1;
                    int hclen = bs.get_bits(4) + 4;

                    uint8_t code_lengths[19] = { 0 };
                    for (int i = 0; i < hclen; ++i) code_lengths[order[i]] = (uint8_t)bs.get_bits(3);

                    Huffman code_huff;
                    code_huff.build(code_lengths, 19);

                    uint8_t lengths[288 + 32] = { 0 };
                    int total = hlit + hdist;
                    int idx = 0;
                    while (idx < total) {
                        int sym = code_huff.decode(bs);
                        if (sym < 16) {
                            lengths[idx++] = (uint8_t)sym;
                        } else if (sym == 16) {
                            if (idx == 0) return false;
                            int rep = bs.get_bits(2) + 3;
                            uint8_t val = lengths[idx - 1];
                            while (rep-- > 0 && idx < total) lengths[idx++] = val;
                        } else if (sym == 17) {
                            int rep = bs.get_bits(3) + 3;
                            while (rep-- > 0 && idx < total) lengths[idx++] = 0;
                        } else if (sym == 18) {
                            int rep = bs.get_bits(7) + 11;
                            while (rep-- > 0 && idx < total) lengths[idx++] = 0;
                        } else {
                            return false;
                        }
                    }
                    lit_huff.build(lengths, hlit);
                    dist_huff.build(lengths + hlit, hdist);
                }

                while (true) {
                    int sym = lit_huff.decode(bs);
                    if (sym < 0 || sym > 285) return false;
                    if (sym < 256) {
                        out.push_back((uint8_t)sym);
                    } else if (sym == 256) {
                        break;
                    } else {
                        int len_idx = sym - 257;
                        int length = len_base[len_idx] + bs.get_bits(len_extra[len_idx]);
                        int dist_sym = dist_huff.decode(bs);
                        if (dist_sym < 0 || dist_sym >= 30) return false;
                        int distance = dist_base[dist_sym] + bs.get_bits(dist_extra[dist_sym]);

                        if ((size_t)distance > out.size()) return false;
                        size_t src_offset = out.size() - distance;
                        for (int k = 0; k < length; ++k) {
                            out.push_back(out[src_offset + k]);
                        }
                    }
                }
            } else {
                return false;
            }
        }
        return true;
    }
}

bool PdfParser::InflateStream(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData) {
    return MiniZlib::Decompress(inData, inSize, outData);
}

bool PdfParser::Load(const std::wstring& filePath) {
    Close();

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < 32) return false;

    m_buffer.resize(fileSize);
    file.read((char*)m_buffer.data(), fileSize);
    if (!file) return false;

    const char* pData = (const char*)m_buffer.data();

    // 1. Index all indirect objects: "N 0 obj" -> offset
    size_t pos = 0;
    while (pos < fileSize) {
        const char* pObj = (const char*)memchr(pData + pos, 'o', fileSize - pos);
        if (!pObj) break;

        pos = pObj - pData;
        if (pos + 3 <= fileSize && pObj[1] == 'b' && pObj[2] == 'j') {
            // Check preceding digits: e.g. "12 0 obj"
            size_t back = pos;
            while (back > 0 && (pData[back - 1] == ' ' || pData[back - 1] == '\t')) back--;
            if (back > 0 && pData[back - 1] == '0') {
                back--;
                while (back > 0 && (pData[back - 1] == ' ' || pData[back - 1] == '\t')) back--;
                size_t numEnd = back;
                while (back > 0 && pData[back - 1] >= '0' && pData[back - 1] <= '9') back--;
                if (numEnd > back) {
                    uint32_t objNum = (uint32_t)strtoul(pData + back, nullptr, 10);
                    if (objNum > 0) {
                        m_objectOffsets[objNum] = back;
                    }
                }
            }
        }
        pos += 3;
    }

    // 2. Discover all page objects
    // Look for "/Type /Page" or "/Type/Page" (excluding "/Pages")
    pos = 0;
    std::string s(m_buffer.begin(), m_buffer.end());

    // First try page tree navigation from Root
    size_t rootPos = s.find("/Root");
    uint32_t rootObj = 0;
    if (rootPos != std::string::npos) {
        rootObj = (uint32_t)strtoul(s.c_str() + rootPos + 5, nullptr, 10);
    }

    // Direct page scanning
    pos = 0;
    while ((pos = s.find("/Type", pos)) != std::string::npos) {
        size_t afterType = pos + 5;
        while (afterType < s.size() && (s[afterType] == ' ' || s[afterType] == '\t' || s[afterType] == '\r' || s[afterType] == '\n')) {
            afterType++;
        }
        if (afterType < s.size() && s[afterType] == '/') afterType++;

        if (afterType + 4 <= s.size() && s.compare(afterType, 4, "Page") == 0) {
            // Ensure not "Pages"
            if (afterType + 4 >= s.size() || s[afterType + 4] != 's') {
                // Find enclosing object start
                size_t objStart = s.rfind(" obj", pos);
                if (objStart != std::string::npos) {
                    size_t lineStart = s.rfind('\n', objStart);
                    size_t actualStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
                    m_pageObjectOffsets.push_back(actualStart);
                }
            }
        }
        pos += 5;
    }

    // Sort page offsets in document order
    std::sort(m_pageObjectOffsets.begin(), m_pageObjectOffsets.end());
    m_pageObjectOffsets.erase(std::unique(m_pageObjectOffsets.begin(), m_pageObjectOffsets.end()), m_pageObjectOffsets.end());

    return !m_pageObjectOffsets.empty();
}

void PdfParser::Close() {
    m_buffer.clear();
    m_pageObjectOffsets.clear();
    m_objectOffsets.clear();
}

std::map<uint32_t, wchar_t> PdfParser::ParseToUnicodeCMap(const std::vector<uint8_t>& streamData) {
    std::map<uint32_t, wchar_t> cmap;
    if (streamData.empty()) return cmap;

    std::string s((const char*)streamData.data(), streamData.size());

    // 1. Parse beginbfchar ... endbfchar
    size_t pos = 0;
    while ((pos = s.find("beginbfchar", pos)) != std::string::npos) {
        size_t endPos = s.find("endbfchar", pos);
        if (endPos == std::string::npos) break;

        size_t cur = pos + 11;
        while (cur < endPos) {
            size_t k1 = s.find('<', cur);
            if (k1 == std::string::npos || k1 >= endPos) break;
            size_t k2 = s.find('>', k1);
            if (k2 == std::string::npos || k2 >= endPos) break;

            size_t v1 = s.find('<', k2);
            if (v1 == std::string::npos || v1 >= endPos) break;
            size_t v2 = s.find('>', v1);
            if (v2 == std::string::npos || v2 >= endPos) break;

            uint32_t srcCode = (uint32_t)strtoul(s.substr(k1 + 1, k2 - k1 - 1).c_str(), nullptr, 16);
            uint32_t dstCode = (uint32_t)strtoul(s.substr(v1 + 1, v2 - v1 - 1).c_str(), nullptr, 16);
            cmap[srcCode] = (wchar_t)dstCode;

            cur = v2 + 1;
        }
        pos = endPos + 9;
    }

    // 2. Parse beginbfrange ... endbfrange
    pos = 0;
    while ((pos = s.find("beginbfrange", pos)) != std::string::npos) {
        size_t endPos = s.find("endbfrange", pos);
        if (endPos == std::string::npos) break;

        size_t cur = pos + 12;
        while (cur < endPos) {
            size_t k1 = s.find('<', cur);
            if (k1 == std::string::npos || k1 >= endPos) break;
            size_t k2 = s.find('>', k1);
            if (k2 == std::string::npos || k2 >= endPos) break;

            size_t k3 = s.find('<', k2);
            if (k3 == std::string::npos || k3 >= endPos) break;
            size_t k4 = s.find('>', k3);
            if (k4 == std::string::npos || k4 >= endPos) break;

            size_t v1 = s.find('<', k4);
            if (v1 == std::string::npos || v1 >= endPos) break;
            size_t v2 = s.find('>', v1);
            if (v2 == std::string::npos || v2 >= endPos) break;

            uint32_t startCode = (uint32_t)strtoul(s.substr(k1 + 1, k2 - k1 - 1).c_str(), nullptr, 16);
            uint32_t endCode = (uint32_t)strtoul(s.substr(k3 + 1, k4 - k3 - 1).c_str(), nullptr, 16);
            uint32_t dstCode = (uint32_t)strtoul(s.substr(v1 + 1, v2 - v1 - 1).c_str(), nullptr, 16);

            for (uint32_t code = startCode; code <= endCode && code <= startCode + 1000; ++code) {
                cmap[code] = (wchar_t)(dstCode + (code - startCode));
            }

            cur = v2 + 1;
        }
        pos = endPos + 10;
    }

    return cmap;
}

bool PdfParser::ExtractPageText(uint32_t pageIndex, PdfPageText& outPage) {
    if (pageIndex >= m_pageObjectOffsets.size() || m_buffer.empty()) return false;

    outPage.pageIndex = pageIndex;
    outPage.fullText.clear();
    outPage.chars.clear();
    outPage.hasDigitalText = false;

    size_t pageOffset = m_pageObjectOffsets[pageIndex];
    std::string s((const char*)m_buffer.data(), m_buffer.size());

    // Find end of page object
    size_t endObj = s.find("endobj", pageOffset);
    if (endObj == std::string::npos) endObj = s.size();

    std::string pageDict = s.substr(pageOffset, endObj - pageOffset);

    // 1. Extract MediaBox dimensions
    float mediaW = 595.28f;
    float mediaH = 841.89f;
    size_t mbPos = pageDict.find("/MediaBox");
    if (mbPos != std::string::npos) {
        size_t b1 = pageDict.find('[', mbPos);
        size_t b2 = pageDict.find(']', b1);
        if (b1 != std::string::npos && b2 != std::string::npos) {
            std::string mbStr = pageDict.substr(b1 + 1, b2 - b1 - 1);
            std::stringstream ss(mbStr);
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (ss >> x0 >> y0 >> x1 >> y1) {
                mediaW = std::abs(x1 - x0);
                mediaH = std::abs(y1 - y0);
            }
        }
    }
    outPage.pageWidth = mediaW;
    outPage.pageHeight = mediaH;

    // 2. Discover Font Resources & CMaps
    std::map<std::string, std::map<uint32_t, wchar_t>> fontCMaps;
    size_t resPos = pageDict.find("/Resources");
    if (resPos != std::string::npos) {
        size_t fontPos = pageDict.find("/Font", resPos);
        if (fontPos != std::string::npos) {
            // Find fonts in dictionary or referenced object
            size_t fStart = pageDict.find("<<", fontPos);
            size_t fEnd = pageDict.find(">>", fStart);
            if (fStart != std::string::npos && fEnd != std::string::npos) {
                std::string fontList = pageDict.substr(fStart + 2, fEnd - fStart - 2);
                std::stringstream fss(fontList);
                std::string token;
                while (fss >> token) {
                    if (token[0] == '/') {
                        std::string fontName = token.substr(1);
                        uint32_t fontObj = 0;
                        if (fss >> fontObj) {
                            if (m_objectOffsets.count(fontObj)) {
                                size_t fOff = m_objectOffsets[fontObj];
                                size_t fEndObj = s.find("endobj", fOff);
                                if (fEndObj != std::string::npos) {
                                    std::string fDef = s.substr(fOff, fEndObj - fOff);
                                    size_t tuPos = fDef.find("/ToUnicode");
                                    if (tuPos != std::string::npos) {
                                        uint32_t tuObj = (uint32_t)strtoul(fDef.c_str() + tuPos + 10, nullptr, 10);
                                        if (tuObj > 0 && m_objectOffsets.count(tuObj)) {
                                            size_t tuOff = m_objectOffsets[tuObj];
                                            size_t stStart = s.find("stream", tuOff);
                                            if (stStart != std::string::npos) {
                                                size_t dStart = s.find('\n', stStart) + 1;
                                                size_t dEnd = s.find("endstream", dStart);
                                                if (dEnd != std::string::npos) {
                                                    while (dEnd > dStart && (s[dEnd - 1] == '\r' || s[dEnd - 1] == '\n')) dEnd--;
                                                    std::vector<uint8_t> decompCMap;
                                                    if (InflateStream(m_buffer.data() + dStart, dEnd - dStart, decompCMap)) {
                                                        fontCMaps[fontName] = ParseToUnicodeCMap(decompCMap);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Extract /Contents objects
    std::vector<uint32_t> contentObjs;
    size_t cPos = pageDict.find("/Contents");
    if (cPos != std::string::npos) {
        size_t afterC = cPos + 9;
        while (afterC < pageDict.size() && (pageDict[afterC] == ' ' || pageDict[afterC] == '\t')) afterC++;

        if (afterC < pageDict.size() && pageDict[afterC] == '[') {
            // Array of content objects
            size_t closeB = pageDict.find(']', afterC);
            if (closeB != std::string::npos) {
                std::string arrStr = pageDict.substr(afterC + 1, closeB - afterC - 1);
                std::stringstream css(arrStr);
                uint32_t cId = 0;
                std::string rTok;
                while (css >> cId >> rTok) {
                    if (rTok == "0" || rTok == "R") {
                        if (rTok == "0") css >> rTok; // eat 'R'
                        contentObjs.push_back(cId);
                    }
                }
            }
        } else {
            // Single content object reference: "N 0 R"
            uint32_t cId = (uint32_t)strtoul(pageDict.c_str() + afterC, nullptr, 10);
            if (cId > 0) contentObjs.push_back(cId);
        }
    }

    // 4. Decompress and parse all content streams in sequence
    for (uint32_t cId : contentObjs) {
        if (!m_objectOffsets.count(cId)) continue;
        size_t cOff = m_objectOffsets[cId];
        size_t stStart = s.find("stream", cOff);
        if (stStart == std::string::npos) continue;

        size_t dStart = s.find('\n', stStart) + 1;
        size_t dEnd = s.find("endstream", dStart);
        if (dEnd == std::string::npos) continue;

        while (dEnd > dStart && (s[dEnd - 1] == '\r' || s[dEnd - 1] == '\n')) dEnd--;

        // Check if FlateDecode compressed
        std::string objHeader = s.substr(cOff, stStart - cOff);
        bool isFlate = (objHeader.find("FlateDecode") != std::string::npos);

        std::vector<uint8_t> streamBytes;
        if (isFlate) {
            InflateStream(m_buffer.data() + dStart, dEnd - dStart, streamBytes);
        } else {
            streamBytes.assign(m_buffer.begin() + dStart, m_buffer.begin() + dEnd);
        }

        if (!streamBytes.empty()) {
            ParseContentStream(streamBytes, mediaH, fontCMaps, outPage);
        }
    }

    outPage.hasDigitalText = !outPage.chars.empty();
    return true;
}

void PdfParser::ParseContentStream(
    const std::vector<uint8_t>& streamBytes,
    float pageHeight,
    const std::map<std::string, std::map<uint32_t, wchar_t>>& fontCMaps,
    PdfPageText& outPage
) {
    if (streamBytes.empty()) return;

    std::string s((const char*)streamBytes.data(), streamBytes.size());
    size_t len = s.size();
    size_t i = 0;

    bool inText = false;
    float curFontSize = 12.0f;
    std::string curFontName = "";
    float tm_a = 1.0f, tm_b = 0.0f, tm_c = 0.0f, tm_d = 1.0f;
    float tm_e = 0.0f, tm_f = 0.0f; // Text matrix origin
    float curX = 0.0f, curY = 0.0f;

    auto EmitString = [&](const std::string& rawStr, bool isHex) {
        if (rawStr.empty()) return;

        std::wstring decoded;
        const auto* pCmap = fontCMaps.count(curFontName) ? &fontCMaps.at(curFontName) : nullptr;

        if (isHex) {
            // Hex encoded string
            for (size_t h = 0; h + 1 < rawStr.size(); h += 2) {
                char hbuf[3] = { rawStr[h], rawStr[h + 1], 0 };
                uint32_t val = (uint32_t)strtoul(hbuf, nullptr, 16);
                if (h + 3 < rawStr.size() && pCmap) {
                    // Try 2-byte code lookup in CMap
                    char hbuf4[5] = { rawStr[h], rawStr[h + 1], rawStr[h + 2], rawStr[h + 3], 0 };
                    uint32_t val4 = (uint32_t)strtoul(hbuf4, nullptr, 16);
                    if (pCmap->count(val4)) {
                        decoded.push_back(pCmap->at(val4));
                        h += 2;
                        continue;
                    }
                }
                if (pCmap && pCmap->count(val)) {
                    decoded.push_back(pCmap->at(val));
                } else if (val >= 32 && val <= 126) {
                    decoded.push_back((wchar_t)val);
                }
            }
        } else {
            // Literal ASCII / WinAnsi string
            for (size_t c = 0; c < rawStr.size(); ++c) {
                uint8_t ch = (uint8_t)rawStr[c];
                if (pCmap && pCmap->count(ch)) {
                    decoded.push_back(pCmap->at(ch));
                } else {
                    decoded.push_back((wchar_t)ch);
                }
            }
        }

        if (decoded.empty()) return;

        // Calculate layout coordinates
        float glyphW = curFontSize * 0.52f;
        float glyphH = curFontSize;

        for (wchar_t wch : decoded) {
            float d2dX = curX;
            float d2dY = pageHeight - (curY + glyphH * 0.85f);

            PdfTextChar tc;
            tc.ch = wch;
            tc.rect = D2D1::RectF(d2dX, d2dY, d2dX + glyphW, d2dY + glyphH);

            outPage.fullText.push_back(wch);
            outPage.chars.push_back(tc);

            curX += glyphW;
        }
    };

    while (i < len) {
        // Skip whitespace
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        if (i >= len) break;

        // Check operators
        if (s[i] == 'B' && i + 1 < len && s[i + 1] == 'T') {
            inText = true;
            tm_e = 0.0f;
            tm_f = 0.0f;
            curX = 0.0f;
            curY = 0.0f;
            i += 2;
            continue;
        }
        if (s[i] == 'E' && i + 1 < len && s[i + 1] == 'T') {
            inText = false;
            // Add word separator space
            if (!outPage.fullText.empty() && outPage.fullText.back() != L' ') {
                outPage.fullText.push_back(L' ');
                outPage.chars.push_back({ L' ', D2D1::RectF(curX, 0, curX + 4.0f, 0) });
            }
            i += 2;
            continue;
        }

        if (inText) {
            // Font operator: "/FontName Size Tf"
            if (s[i] == '/') {
                size_t fnEnd = s.find_first_of(" \t\r\n", i);
                if (fnEnd != std::string::npos) {
                    std::string fName = s.substr(i + 1, fnEnd - i - 1);
                    size_t szStart = s.find_first_not_of(" \t\r\n", fnEnd);
                    if (szStart != std::string::npos) {
                        float fSize = (float)atof(s.c_str() + szStart);
                        if (fSize > 0.1f) {
                            curFontName = fName;
                            curFontSize = fSize;
                        }
                    }
                }
            }

            // Text matrix: "a b c d e f Tm"
            if (i + 2 <= len && s.compare(i, 2, "Tm") == 0) {
                size_t lineStart = s.rfind('\n', i);
                if (lineStart == std::string::npos) lineStart = 0;
                std::string mLine = s.substr(lineStart, i - lineStart);
                std::stringstream mss(mLine);
                std::vector<float> vals;
                float v = 0;
                while (mss >> v) vals.push_back(v);
                if (vals.size() >= 6) {
                    tm_a = vals[vals.size() - 6];
                    tm_b = vals[vals.size() - 5];
                    tm_c = vals[vals.size() - 4];
                    tm_d = vals[vals.size() - 3];
                    tm_e = vals[vals.size() - 2];
                    tm_f = vals[vals.size() - 1];
                    curX = tm_e;
                    curY = tm_f;
                }
                i += 2;
                continue;
            }

            // Translation operator: "x y Td" or "x y TD"
            if (i + 2 <= len && (s.compare(i, 2, "Td") == 0 || s.compare(i, 2, "TD") == 0)) {
                size_t lineStart = s.rfind('\n', i);
                if (lineStart == std::string::npos) lineStart = 0;
                std::string mLine = s.substr(lineStart, i - lineStart);
                std::stringstream mss(mLine);
                std::vector<float> vals;
                float v = 0;
                while (mss >> v) vals.push_back(v);
                if (vals.size() >= 2) {
                    float dx = vals[vals.size() - 2];
                    float dy = vals[vals.size() - 1];
                    curX += dx;
                    curY += dy;
                }
                i += 2;
                continue;
            }

            // Literal string Tj: "(text) Tj"
            if (s[i] == '(') {
                size_t strStart = i + 1;
                size_t strEnd = strStart;
                int parenDepth = 1;
                while (strEnd < len && parenDepth > 0) {
                    if (s[strEnd] == '\\' && strEnd + 1 < len) {
                        strEnd += 2;
                        continue;
                    }
                    if (s[strEnd] == '(') parenDepth++;
                    else if (s[strEnd] == ')') parenDepth--;
                    strEnd++;
                }
                std::string litStr = s.substr(strStart, strEnd - strStart - 1);
                size_t opPos = s.find_first_not_of(" \t\r\n", strEnd);
                if (opPos != std::string::npos && opPos + 2 <= len && s.compare(opPos, 2, "Tj") == 0) {
                    EmitString(litStr, false);
                    i = opPos + 2;
                    continue;
                }
                i = strEnd;
                continue;
            }

            // Hex string Tj: "<hex> Tj"
            if (s[i] == '<' && i + 1 < len && s[i + 1] != '<') {
                size_t hexStart = i + 1;
                size_t hexEnd = s.find('>', hexStart);
                if (hexEnd != std::string::npos) {
                    std::string hexStr = s.substr(hexStart, hexEnd - hexStart);
                    size_t opPos = s.find_first_not_of(" \t\r\n", hexEnd + 1);
                    if (opPos != std::string::npos && opPos + 2 <= len && s.compare(opPos, 2, "Tj") == 0) {
                        EmitString(hexStr, true);
                        i = opPos + 2;
                        continue;
                    }
                }
            }

            // Array string TJ: "[ ... ] TJ"
            if (s[i] == '[') {
                size_t arrStart = i + 1;
                size_t arrEnd = s.find(']', arrStart);
                if (arrEnd != std::string::npos) {
                    size_t opPos = s.find_first_not_of(" \t\r\n", arrEnd + 1);
                    if (opPos != std::string::npos && opPos + 2 <= len && s.compare(opPos, 2, "TJ") == 0) {
                        size_t k = arrStart;
                        while (k < arrEnd) {
                            while (k < arrEnd && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r' || s[k] == '\n')) k++;
                            if (k >= arrEnd) break;

                            if (s[k] == '(') {
                                size_t pStart = k + 1;
                                size_t pEnd = pStart;
                                int depth = 1;
                                while (pEnd < arrEnd && depth > 0) {
                                    if (s[pEnd] == '\\' && pEnd + 1 < arrEnd) {
                                        pEnd += 2;
                                        continue;
                                    }
                                    if (s[pEnd] == '(') depth++;
                                    else if (s[pEnd] == ')') depth--;
                                    pEnd++;
                                }
                                std::string itemStr = s.substr(pStart, pEnd - pStart - 1);
                                EmitString(itemStr, false);
                                k = pEnd;
                            } else if (s[k] == '<' && k + 1 < arrEnd && s[k + 1] != '<') {
                                size_t hStart = k + 1;
                                size_t hEnd = s.find('>', hStart);
                                if (hEnd != std::string::npos && hEnd <= arrEnd) {
                                    std::string itemHex = s.substr(hStart, hEnd - hStart);
                                    EmitString(itemHex, true);
                                    k = hEnd + 1;
                                } else {
                                    break;
                                }
                            } else {
                                // Kerning offset float (e.g. -20, 15)
                                size_t nextTok = s.find_first_of(" ([<\t\r\n", k);
                                if (nextTok == std::string::npos || nextTok > arrEnd) nextTok = arrEnd;
                                float kern = (float)atof(s.substr(k, nextTok - k).c_str());
                                if (std::abs(kern) > 150.0f) {
                                    // Space gap
                                    if (!outPage.fullText.empty() && outPage.fullText.back() != L' ') {
                                        outPage.fullText.push_back(L' ');
                                        outPage.chars.push_back({ L' ', D2D1::RectF(curX, 0, curX + 4.0f, 0) });
                                    }
                                }
                                k = nextTok;
                            }
                        }
                        i = opPos + 2;
                        continue;
                    }
                }
            }
        }

        i++;
    }
}

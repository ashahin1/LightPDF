#include "pdf_parser.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

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

static uint8_t PaethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

bool PdfParser::DecodePredictor(const std::vector<uint8_t>& inData, int predictor, int columns, int colors, int bpc, std::vector<uint8_t>& outData) {
    if (predictor < 10) {
        outData = inData;
        return true;
    }

    size_t bpp = ((size_t)colors * bpc + 7) / 8;
    if (bpp == 0) bpp = 1;
    size_t rowBytes = ((size_t)columns * colors * bpc + 7) / 8;
    if (rowBytes == 0) return false;
    size_t stride = 1 + rowBytes;

    size_t numRows = inData.size() / stride;
    if (numRows == 0) return false;

    outData.clear();
    outData.resize(numRows * rowBytes);

    for (size_t r = 0; r < numRows; ++r) {
        size_t srcRow = r * stride;
        uint8_t filter = inData[srcRow];
        size_t dstRow = r * rowBytes;

        for (size_t c = 0; c < rowBytes; ++c) {
            uint8_t raw = inData[srcRow + 1 + c];
            uint8_t left = (c >= bpp) ? outData[dstRow + c - bpp] : 0;
            uint8_t up = (r > 0) ? outData[(r - 1) * rowBytes + c] : 0;
            uint8_t upLeft = (r > 0 && c >= bpp) ? outData[(r - 1) * rowBytes + c - bpp] : 0;

            uint8_t val = 0;
            switch (filter) {
            case 0: // None
                val = raw;
                break;
            case 1: // Sub
                val = (uint8_t)(raw + left);
                break;
            case 2: // Up
                val = (uint8_t)(raw + up);
                break;
            case 3: // Average
                val = (uint8_t)(raw + ((int)left + (int)up) / 2);
                break;
            case 4: // Paeth
                val = (uint8_t)(raw + PaethPredictor(left, up, upLeft));
                break;
            default:
                val = raw;
                break;
            }
            outData[dstRow + c] = val;
        }
    }
    return true;
}

static const uint16_t HELVETICA_WIDTHS[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, 0, 0, 260, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
    400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
    667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
    722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
    556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500
};

static const uint16_t TIMES_WIDTHS[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, 0, 0, 260, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
    921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
    556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
    333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
    500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    250, 333, 500, 500, 500, 500, 200, 500, 333, 760, 276, 500, 564, 333, 760, 333,
    400, 564, 300, 300, 333, 500, 453, 250, 333, 300, 310, 500, 750, 750, 750, 444,
    722, 722, 722, 722, 722, 722, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
    722, 722, 722, 722, 722, 722, 722, 564, 722, 722, 722, 722, 722, 722, 556, 500,
    444, 444, 444, 444, 444, 444, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
    500, 500, 500, 500, 500, 500, 500, 564, 500, 500, 500, 500, 500, 500, 500, 500
};

float PdfFontInfo::GetCharWidth(uint32_t charCode) const {
    if (!cidWidths.empty()) {
        auto it = cidWidths.find(charCode);
        if (it != cidWidths.end()) {
            return it->second;
        }
        return defaultWidth;
    }

    if (!widths.empty() && charCode >= (uint32_t)firstChar && charCode <= (uint32_t)lastChar) {
        size_t idx = charCode - firstChar;
        if (idx < widths.size() && widths[idx] > 0.0f) {
            return widths[idx];
        }
    }

    if (baseFont.find("Courier") != std::string::npos) {
        return 600.0f;
    }
    if (baseFont.find("Times") != std::string::npos) {
        if (charCode < 256) return (float)TIMES_WIDTHS[charCode];
        return 500.0f;
    }

    if (charCode < 256) {
        uint16_t w = HELVETICA_WIDTHS[charCode];
        if (w > 0) return (float)w;
    }

    return defaultWidth;
}

std::wstring PdfFontInfo::DecodeString(uint32_t charCode) const {
    if (!toUnicode.empty()) {
        auto it = toUnicode.find(charCode);
        if (it != toUnicode.end()) {
            return it->second;
        }
    }
    return std::wstring(1, (wchar_t)charCode);
}

wchar_t PdfFontInfo::DecodeChar(uint32_t charCode) const {
    if (!toUnicode.empty()) {
        auto it = toUnicode.find(charCode);
        if (it != toUnicode.end() && !it->second.empty()) {
            return it->second[0];
        }
    }
    return (wchar_t)charCode;
}

void PdfParser::DecodeObjStream(uint32_t stmObjNum) const {
    if (m_objStmCache.find(stmObjNum) != m_objStmCache.end()) return;

    std::vector<uint8_t> stmData;
    if (!GetObjectStreamData(stmObjNum, stmData) || stmData.empty()) {
        m_objStmCache[stmObjNum] = {};
        return;
    }

    auto it = m_xref.find(stmObjNum);
    if (it == m_xref.end() || it->second.type != 1) {
        m_objStmCache[stmObjNum] = {};
        return;
    }

    size_t start = it->second.offsetOrStm;
    size_t stStart = m_bufferStr.find("stream", start);
    if (stStart == std::string::npos) {
        m_objStmCache[stmObjNum] = {};
        return;
    }

    std::string dictStr = m_bufferStr.substr(start, stStart - start);

    size_t firstPos = dictStr.find("/First");
    size_t nPos = dictStr.find("/N");
    if (firstPos == std::string::npos || nPos == std::string::npos) {
        m_objStmCache[stmObjNum] = {};
        return;
    }

    size_t firstOffset = (size_t)strtoull(dictStr.c_str() + firstPos + 6, nullptr, 10);
    uint32_t nObjs = (uint32_t)strtoul(dictStr.c_str() + nPos + 2, nullptr, 10);
    if (firstOffset >= stmData.size() || nObjs == 0) {
        m_objStmCache[stmObjNum] = {};
        return;
    }

    std::string headerStr((const char*)stmData.data(), firstOffset);
    std::stringstream ss(headerStr);
    std::vector<std::pair<uint32_t, size_t>> objOffsets;
    objOffsets.reserve(nObjs);
    for (uint32_t i = 0; i < nObjs; ++i) {
        uint32_t oNum = 0;
        size_t oOff = 0;
        if (ss >> oNum >> oOff) {
            objOffsets.push_back({ oNum, oOff });
        } else {
            break;
        }
    }

    const char* pBody = (const char*)stmData.data() + firstOffset;
    size_t bodyLen = stmData.size() - firstOffset;

    std::map<uint32_t, std::string> objs;
    for (size_t i = 0; i < objOffsets.size(); ++i) {
        uint32_t oNum = objOffsets[i].first;
        size_t oStart = objOffsets[i].second;
        if (oStart >= bodyLen) continue;
        size_t oEnd = (i + 1 < objOffsets.size()) ? objOffsets[i + 1].second : bodyLen;
        if (oEnd > bodyLen) oEnd = bodyLen;
        if (oEnd >= oStart) {
            objs[oNum] = std::string(pBody + oStart, oEnd - oStart);
        }
    }

    m_objStmCache[stmObjNum] = std::move(objs);
}

std::string PdfParser::GetObjectString(uint32_t objNum) const {
    auto it = m_xref.find(objNum);
    if (it == m_xref.end() || m_bufferStr.empty()) return {};

    if (it->second.type == 1) {
        size_t start = it->second.offsetOrStm;
        if (start >= m_bufferStr.size()) return {};
        size_t end = m_bufferStr.find("endobj", start);
        if (end == std::string::npos) end = m_bufferStr.size();
        return m_bufferStr.substr(start, end - start);
    } else if (it->second.type == 2) {
        uint32_t stmObjNum = it->second.offsetOrStm;
        DecodeObjStream(stmObjNum);
        auto sIt = m_objStmCache.find(stmObjNum);
        if (sIt != m_objStmCache.end()) {
            auto oIt = sIt->second.find(objNum);
            if (oIt != sIt->second.end()) {
                return oIt->second;
            }
        }
    }
    return {};
}

bool PdfParser::GetObjectStreamData(uint32_t objNum, std::vector<uint8_t>& outData) const {
    auto it = m_xref.find(objNum);
    if (it == m_xref.end() || it->second.type != 1) return false;
    size_t start = it->second.offsetOrStm;
    if (start >= m_bufferStr.size()) return false;
    size_t stStart = m_bufferStr.find("stream", start);
    if (stStart == std::string::npos) return false;

    size_t dStart = m_bufferStr.find('\n', stStart);
    if (dStart == std::string::npos) return false;
    dStart++;
    size_t dEnd = m_bufferStr.find("endstream", dStart);
    if (dEnd == std::string::npos) return false;

    while (dEnd > dStart && (m_bufferStr[dEnd - 1] == '\r' || m_bufferStr[dEnd - 1] == '\n')) dEnd--;

    std::string header = m_bufferStr.substr(start, stStart - start);
    bool isFlate = (header.find("FlateDecode") != std::string::npos);

    if (isFlate) {
        std::vector<uint8_t> rawDecomp;
        if (!InflateStream(m_buffer.data() + dStart, dEnd - dStart, rawDecomp)) {
            return false;
        }

        size_t dpPos = header.find("/DecodeParms");
        if (dpPos != std::string::npos) {
            int predictor = 1, columns = 1, colors = 1, bpc = 8;
            size_t prPos = header.find("/Predictor", dpPos);
            if (prPos != std::string::npos) predictor = (int)strtol(header.c_str() + prPos + 10, nullptr, 10);
            size_t colPos = header.find("/Columns", dpPos);
            if (colPos != std::string::npos) columns = (int)strtol(header.c_str() + colPos + 8, nullptr, 10);
            size_t clrPos = header.find("/Colors", dpPos);
            if (clrPos != std::string::npos) colors = (int)strtol(header.c_str() + clrPos + 7, nullptr, 10);
            size_t bpcPos = header.find("/BitsPerComponent", dpPos);
            if (bpcPos != std::string::npos) bpc = (int)strtol(header.c_str() + bpcPos + 17, nullptr, 10);

            if (predictor >= 10 && columns > 0) {
                if (DecodePredictor(rawDecomp, predictor, columns, colors, bpc, outData)) {
                    return true;
                }
            }
        }

        outData = std::move(rawDecomp);
        return true;
    } else {
        outData.assign(m_buffer.begin() + dStart, m_buffer.begin() + dEnd);
        return true;
    }
}

bool PdfParser::FindIndirectRef(const std::string& dictStr, const std::string& key, uint32_t& outObjNum) const {
    size_t searchPos = 0;
    while ((searchPos = dictStr.find(key, searchPos)) != std::string::npos) {
        size_t pos = searchPos + key.size();
        while (pos < dictStr.size() && (dictStr[pos] == ' ' || dictStr[pos] == '\t' || dictStr[pos] == '\r' || dictStr[pos] == '\n')) pos++;
        if (pos < dictStr.size() && dictStr[pos] >= '0' && dictStr[pos] <= '9') {
            char* endPtr = nullptr;
            uint32_t num = (uint32_t)strtoul(dictStr.c_str() + pos, &endPtr, 10);
            if (num > 0 && endPtr) {
                while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\r' || *endPtr == '\n') endPtr++;
                if (*endPtr >= '0' && *endPtr <= '9') {
                    while (*endPtr >= '0' && *endPtr <= '9') endPtr++;
                    while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\r' || *endPtr == '\n') endPtr++;
                    if (*endPtr == 'R') {
                        outObjNum = num;
                        return true;
                    }
                }
            }
        }
        searchPos = pos;
    }
    return false;
}

std::string PdfParser::ResolveDict(const std::string& parentDict, const std::string& key) const {
    size_t searchPos = 0;
    while ((searchPos = parentDict.find(key, searchPos)) != std::string::npos) {
        size_t pos = searchPos + key.size();
        while (pos < parentDict.size() && (parentDict[pos] == ' ' || parentDict[pos] == '\t' || parentDict[pos] == '\r' || parentDict[pos] == '\n')) pos++;
        if (pos >= parentDict.size()) break;

        if (parentDict.compare(pos, 2, "<<") == 0) {
            int depth = 0;
            size_t i = pos;
            while (i < parentDict.size()) {
                if (parentDict.compare(i, 2, "<<") == 0) {
                    depth++;
                    i += 2;
                } else if (parentDict.compare(i, 2, ">>") == 0) {
                    depth--;
                    i += 2;
                    if (depth == 0) {
                        return parentDict.substr(pos, i - pos);
                    }
                } else {
                    i++;
                }
            }
        } else if (parentDict[pos] >= '0' && parentDict[pos] <= '9') {
            uint32_t objNum = 0;
            if (FindIndirectRef(parentDict.substr(searchPos), key, objNum)) {
                return GetObjectString(objNum);
            }
        }
        searchPos = pos;
    }
    return {};
}

bool PdfParser::ParseXRefStream(size_t offset, std::string& outTrailerDict) {
    if (offset >= m_bufferStr.size()) return false;

    size_t curOffset = offset;
    std::vector<size_t> visitedOffsets;

    while (curOffset < m_bufferStr.size()) {
        if (std::find(visitedOffsets.begin(), visitedOffsets.end(), curOffset) != visitedOffsets.end()) break;
        visitedOffsets.push_back(curOffset);

        size_t stStart = m_bufferStr.find("stream", curOffset);
        if (stStart == std::string::npos) break;

        std::string dictStr = m_bufferStr.substr(curOffset, stStart - curOffset);
        if (outTrailerDict.empty()) {
            outTrailerDict = dictStr;
        }

        // Parse objNum of this xref stream so we can index it in m_xref
        size_t objKw = m_bufferStr.find("obj", curOffset);
        if (objKw != std::string::npos && objKw < stStart) {
            uint32_t myObjNum = (uint32_t)strtoul(m_bufferStr.c_str() + curOffset, nullptr, 10);
            if (myObjNum > 0 && m_xref.find(myObjNum) == m_xref.end()) {
                m_xref[myObjNum] = { 1, (uint32_t)curOffset, 0 };
            }
        }

        // Parse /Size
        size_t sizePos = dictStr.find("/Size");
        uint32_t sizeVal = 0;
        if (sizePos != std::string::npos) {
            sizeVal = (uint32_t)strtoul(dictStr.c_str() + sizePos + 5, nullptr, 10);
        }

        // Parse /W [w1 w2 w3]
        size_t wPos = dictStr.find("/W");
        if (wPos == std::string::npos) break;
        size_t wb1 = dictStr.find('[', wPos);
        size_t wb2 = dictStr.find(']', wb1);
        if (wb1 == std::string::npos || wb2 == std::string::npos) break;

        std::stringstream wss(dictStr.substr(wb1 + 1, wb2 - wb1 - 1));
        int w1 = 0, w2 = 0, w3 = 0;
        if (!(wss >> w1 >> w2 >> w3)) break;
        int entryLen = w1 + w2 + w3;
        if (entryLen <= 0) break;

        // Parse /Index [first1 count1 first2 count2 ...]
        std::vector<std::pair<uint32_t, uint32_t>> subsections;
        size_t idxPos = dictStr.find("/Index");
        if (idxPos != std::string::npos) {
            size_t ib1 = dictStr.find('[', idxPos);
            size_t ib2 = dictStr.find(']', ib1);
            if (ib1 != std::string::npos && ib2 != std::string::npos) {
                std::stringstream iss(dictStr.substr(ib1 + 1, ib2 - ib1 - 1));
                uint32_t f = 0, c = 0;
                while (iss >> f >> c) {
                    subsections.push_back({ f, c });
                }
            }
        }
        if (subsections.empty()) {
            subsections.push_back({ 0, sizeVal });
        }

        // Stream data extraction
        size_t dStart = m_bufferStr.find('\n', stStart);
        if (dStart == std::string::npos) break;
        dStart++;
        size_t dEnd = m_bufferStr.find("endstream", dStart);
        if (dEnd == std::string::npos) break;
        while (dEnd > dStart && (m_bufferStr[dEnd - 1] == '\r' || m_bufferStr[dEnd - 1] == '\n')) dEnd--;

        std::vector<uint8_t> decomp;
        if (!InflateStream(m_buffer.data() + dStart, dEnd - dStart, decomp)) {
            break;
        }

        // Decode predictor if present
        size_t dpPos = dictStr.find("/DecodeParms");
        if (dpPos != std::string::npos) {
            int predictor = 1, columns = 1, colors = 1, bpc = 8;
            size_t prPos = dictStr.find("/Predictor", dpPos);
            if (prPos != std::string::npos) predictor = (int)strtol(dictStr.c_str() + prPos + 10, nullptr, 10);
            size_t colPos = dictStr.find("/Columns", dpPos);
            if (colPos != std::string::npos) columns = (int)strtol(dictStr.c_str() + colPos + 8, nullptr, 10);
            size_t clrPos = dictStr.find("/Colors", dpPos);
            if (clrPos != std::string::npos) colors = (int)strtol(dictStr.c_str() + clrPos + 7, nullptr, 10);
            size_t bpcPos = dictStr.find("/BitsPerComponent", dpPos);
            if (bpcPos != std::string::npos) bpc = (int)strtol(dictStr.c_str() + bpcPos + 17, nullptr, 10);

            if (predictor >= 10) {
                std::vector<uint8_t> predOut;
                if (DecodePredictor(decomp, predictor, columns > 0 ? columns : entryLen, colors, bpc, predOut)) {
                    decomp = std::move(predOut);
                }
            }
        }

        // Parse entries
        size_t curByte = 0;
        for (const auto& sub : subsections) {
            uint32_t startNum = sub.first;
            uint32_t count = sub.second;
            for (uint32_t i = 0; i < count; ++i) {
                if (curByte + entryLen > decomp.size()) break;
                const uint8_t* pEntry = decomp.data() + curByte;

                int t = 1;
                if (w1 > 0) {
                    uint32_t v = 0;
                    for (int k = 0; k < w1; ++k) v = (v << 8) | pEntry[k];
                    t = (int)v;
                }
                uint32_t f2 = 0;
                if (w2 > 0) {
                    for (int k = 0; k < w2; ++k) f2 = (f2 << 8) | pEntry[w1 + k];
                }
                uint32_t f3 = 0;
                if (w3 > 0) {
                    for (int k = 0; k < w3; ++k) f3 = (f3 << 8) | pEntry[w1 + w2 + k];
                }

                uint32_t objNum = startNum + i;
                if (m_xref.find(objNum) == m_xref.end()) {
                    m_xref[objNum] = { t, f2, f3 };
                }

                curByte += entryLen;
            }
        }

        // Check for /Prev
        size_t prevPos = dictStr.find("/Prev");
        if (prevPos != std::string::npos) {
            curOffset = (size_t)strtoull(dictStr.c_str() + prevPos + 5, nullptr, 10);
        } else {
            break;
        }
    }

    return !m_xref.empty();
}

bool PdfParser::ParseClassicXRef(size_t offset, std::string& outTrailerDict) {
    if (offset >= m_bufferStr.size()) return false;

    size_t curOffset = offset;
    std::vector<size_t> visitedOffsets;

    while (curOffset < m_bufferStr.size()) {
        if (std::find(visitedOffsets.begin(), visitedOffsets.end(), curOffset) != visitedOffsets.end()) break;
        visitedOffsets.push_back(curOffset);

        const char* p = m_bufferStr.c_str() + curOffset;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (strncmp(p, "xref", 4) != 0) break;
        p += 4;

        while (p < m_bufferStr.c_str() + m_bufferStr.size()) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p < '0' || *p > '9') break;

            char* nextPtr = nullptr;
            uint32_t firstObj = (uint32_t)strtoul(p, &nextPtr, 10);
            if (!nextPtr) break;
            p = nextPtr;

            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            uint32_t count = (uint32_t)strtoul(p, &nextPtr, 10);
            if (!nextPtr) break;
            p = nextPtr;

            for (uint32_t i = 0; i < count; ++i) {
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                uint32_t oOffset = (uint32_t)strtoul(p, &nextPtr, 10);
                if (!nextPtr) break;
                p = nextPtr;

                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                uint32_t gen = (uint32_t)strtoul(p, &nextPtr, 10);
                if (!nextPtr) break;
                p = nextPtr;

                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                char typeChar = *p;
                if (*p) p++;

                uint32_t objNum = firstObj + i;
                if (typeChar == 'n' || typeChar == 'N') {
                    if (m_xref.find(objNum) == m_xref.end()) {
                        m_xref[objNum] = { 1, oOffset, gen };
                    }
                }
            }
        }

        size_t pOffset = p - m_bufferStr.c_str();
        size_t trPos = m_bufferStr.find("trailer", pOffset);
        if (trPos == std::string::npos) break;

        size_t dictStart = m_bufferStr.find("<<", trPos);
        if (dictStart == std::string::npos) break;

        std::string trDict = ResolveDict(m_bufferStr.substr(trPos), "<<");
        if (outTrailerDict.empty()) {
            outTrailerDict = trDict;
        }

        size_t prevPos = trDict.find("/Prev");
        if (prevPos != std::string::npos) {
            curOffset = (size_t)strtoull(trDict.c_str() + prevPos + 5, nullptr, 10);
        } else {
            break;
        }
    }

    return !m_xref.empty();
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

    m_bufferStr.assign((const char*)m_buffer.data(), fileSize);

    // 1. Locate startxref from EOF
    size_t sxPos = m_bufferStr.rfind("startxref");
    std::string trailerDict;

    if (sxPos != std::string::npos) {
        size_t numPos = sxPos + 9;
        while (numPos < fileSize && (m_bufferStr[numPos] == ' ' || m_bufferStr[numPos] == '\t' || m_bufferStr[numPos] == '\r' || m_bufferStr[numPos] == '\n')) numPos++;
        if (numPos < fileSize && m_bufferStr[numPos] >= '0' && m_bufferStr[numPos] <= '9') {
            size_t xrefOffset = (size_t)strtoull(m_bufferStr.c_str() + numPos, nullptr, 10);
            if (xrefOffset < fileSize) {
                size_t p = xrefOffset;
                while (p < fileSize && (m_bufferStr[p] == ' ' || m_bufferStr[p] == '\t' || m_bufferStr[p] == '\r' || m_bufferStr[p] == '\n')) p++;
                if (p + 4 <= fileSize && m_bufferStr.compare(p, 4, "xref") == 0) {
                    ParseClassicXRef(p, trailerDict);
                } else if (p < fileSize && m_bufferStr[p] >= '0' && m_bufferStr[p] <= '9') {
                    ParseXRefStream(p, trailerDict);
                }
            }
        }
    }

    // 2. Fallback: linear scanner for "N 0 obj" if xref parsing failed or found no entries
    if (m_xref.empty()) {
        const char* pData = m_bufferStr.data();
        size_t pos = 0;
        while (pos < fileSize) {
            const char* pObj = (const char*)memchr(pData + pos, 'o', fileSize - pos);
            if (!pObj) break;

            pos = pObj - pData;
            if (pos + 3 <= fileSize && pObj[1] == 'b' && pObj[2] == 'j') {
                size_t back = pos;
                while (back > 0 && (pData[back - 1] == ' ' || pData[back - 1] == '\t')) back--;
                if (back > 0 && pData[back - 1] == '0') {
                    back--;
                    while (back > 0 && (pData[back - 1] == ' ' || pData[back - 1] == '\t')) back--;
                    size_t numEnd = back;
                    while (back > 0 && pData[back - 1] >= '0' && pData[back - 1] <= '9') back--;
                    if (numEnd > back) {
                        uint32_t objNum = (uint32_t)strtoul(pData + back, nullptr, 10);
                        if (objNum > 0 && m_xref.find(objNum) == m_xref.end()) {
                            m_xref[objNum] = { 1, (uint32_t)back, 0 };
                        }
                    }
                }
            }
            pos += 3;
        }
    }

    // 3. Find /Root catalog object
    uint32_t rootObj = 0;
    if (!trailerDict.empty()) {
        FindIndirectRef(trailerDict, "/Root", rootObj);
    }
    if (rootObj == 0) {
        size_t rootPos = m_bufferStr.find("/Root");
        if (rootPos != std::string::npos) {
            size_t afterRoot = rootPos + 5;
            while (afterRoot < m_bufferStr.size() && (m_bufferStr[afterRoot] == ' ' || m_bufferStr[afterRoot] == '\t' || m_bufferStr[afterRoot] == '\r' || m_bufferStr[afterRoot] == '\n')) afterRoot++;
            rootObj = (uint32_t)strtoul(m_bufferStr.c_str() + afterRoot, nullptr, 10);
        }
    }

    // 4. Discover page objects in canonical tree order from /Root
    if (rootObj > 0) {
        std::string rootStr = GetObjectString(rootObj);
        uint32_t pagesObj = 0;
        if (FindIndirectRef(rootStr, "/Pages", pagesObj)) {
            std::vector<uint32_t> visited;
            std::function<void(uint32_t)> traversePages = [&](uint32_t objNum) {
                if (std::find(visited.begin(), visited.end(), objNum) != visited.end()) return;
                visited.push_back(objNum);

                std::string objText = GetObjectString(objNum);
                if (objText.find("/Pages") != std::string::npos && objText.find("/Kids") != std::string::npos) {
                    size_t kPos = objText.find("/Kids");
                    size_t b1 = objText.find('[', kPos);
                    size_t b2 = objText.find(']', b1);
                    if (b1 != std::string::npos && b2 != std::string::npos) {
                        std::string kStr = objText.substr(b1 + 1, b2 - b1 - 1);
                        std::stringstream ss(kStr);
                        uint32_t childObj = 0;
                        std::string rTok;
                        while (ss >> childObj >> rTok) {
                            if (rTok == "0" || rTok == "R") {
                                if (rTok == "0") ss >> rTok;
                                traversePages(childObj);
                            }
                        }
                    }
                } else if (objText.find("/Page") != std::string::npos) {
                    m_pageObjectNums.push_back(objNum);
                }
            };
            traversePages(pagesObj);
        }
    }

    // Fallback: direct page scanning if tree traversal produced no pages
    if (m_pageObjectNums.empty()) {
        for (const auto& entry : m_xref) {
            std::string objText = GetObjectString(entry.first);
            size_t tPos = objText.find("/Type");
            if (tPos != std::string::npos) {
                size_t pPos = objText.find("/Page", tPos);
                if (pPos != std::string::npos && (pPos + 5 >= objText.size() || objText[pPos + 5] != 's')) {
                    m_pageObjectNums.push_back(entry.first);
                }
            }
        }
        std::sort(m_pageObjectNums.begin(), m_pageObjectNums.end());
        m_pageObjectNums.erase(std::unique(m_pageObjectNums.begin(), m_pageObjectNums.end()), m_pageObjectNums.end());
    }

    return !m_pageObjectNums.empty();
}

void PdfParser::Close() {
    m_buffer.clear();
    m_bufferStr.clear();
    m_pageObjectNums.clear();
    m_xref.clear();
    m_objStmCache.clear();
}

std::map<uint32_t, std::wstring> PdfParser::ParseToUnicodeCMap(const std::vector<uint8_t>& streamData) {
    std::map<uint32_t, std::wstring> cmap;
    if (streamData.empty()) return cmap;

    std::string s((const char*)streamData.data(), streamData.size());

    auto parseHexToWString = [](const std::string& hex) -> std::wstring {
        std::wstring res;
        for (size_t i = 0; i + 3 < hex.size(); i += 4) {
            uint32_t val = (uint32_t)strtoul(hex.substr(i, 4).c_str(), nullptr, 16);
            res.push_back((wchar_t)val);
        }
        if (res.empty() && !hex.empty()) {
            uint32_t val = (uint32_t)strtoul(hex.c_str(), nullptr, 16);
            res.push_back((wchar_t)val);
        }
        return res;
    };

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
            std::string hexDst = s.substr(v1 + 1, v2 - v1 - 1);
            cmap[srcCode] = parseHexToWString(hexDst);

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

            uint32_t startCode = (uint32_t)strtoul(s.substr(k1 + 1, k2 - k1 - 1).c_str(), nullptr, 16);
            uint32_t endCode = (uint32_t)strtoul(s.substr(k3 + 1, k4 - k3 - 1).c_str(), nullptr, 16);

            size_t afterK4 = s.find_first_not_of(" \t\r\n", k4 + 1);
            if (afterK4 != std::string::npos && afterK4 < endPos) {
                if (s[afterK4] == '<') {
                    size_t v2 = s.find('>', afterK4);
                    if (v2 != std::string::npos && v2 < endPos) {
                        std::string hexDst = s.substr(afterK4 + 1, v2 - afterK4 - 1);
                        uint32_t dstCode = (uint32_t)strtoul(hexDst.c_str(), nullptr, 16);
                        for (uint32_t code = startCode; code <= endCode && code <= startCode + 10000; ++code) {
                            cmap[code] = std::wstring(1, (wchar_t)(dstCode + (code - startCode)));
                        }
                        cur = v2 + 1;
                        continue;
                    }
                } else if (s[afterK4] == '[') {
                    size_t bEnd = s.find(']', afterK4);
                    if (bEnd != std::string::npos && bEnd < endPos) {
                        size_t bCur = afterK4 + 1;
                        uint32_t code = startCode;
                        while (bCur < bEnd && code <= endCode) {
                            size_t h1 = s.find('<', bCur);
                            if (h1 == std::string::npos || h1 >= bEnd) break;
                            size_t h2 = s.find('>', h1);
                            if (h2 == std::string::npos || h2 >= bEnd) break;
                            std::string hexDst = s.substr(h1 + 1, h2 - h1 - 1);
                            cmap[code++] = parseHexToWString(hexDst);
                            bCur = h2 + 1;
                        }
                        cur = bEnd + 1;
                        continue;
                    }
                }
            }

            cur = k4 + 1;
        }
        pos = endPos + 10;
    }

    return cmap;
}

std::map<std::string, PdfFontInfo> PdfParser::ExtractPageFonts(const std::string& pageDict) {
    std::map<std::string, PdfFontInfo> fonts;

    std::string resDict;
    uint32_t resObj = 0;
    if (FindIndirectRef(pageDict, "/Resources", resObj)) {
        resDict = GetObjectString(resObj);
    } else {
        resDict = ResolveDict(pageDict, "/Resources");
        if (resDict.empty()) {
            uint32_t curParent = 0;
            if (FindIndirectRef(pageDict, "/Parent", curParent)) {
                for (int depth = 0; depth < 5 && curParent > 0; ++depth) {
                    std::string parentStr = GetObjectString(curParent);
                    if (FindIndirectRef(parentStr, "/Resources", resObj)) {
                        resDict = GetObjectString(resObj);
                        break;
                    } else {
                        resDict = ResolveDict(parentStr, "/Resources");
                        if (!resDict.empty()) break;
                    }
                    uint32_t nextParent = 0;
                    if (!FindIndirectRef(parentStr, "/Parent", nextParent)) break;
                    curParent = nextParent;
                }
            }
        }
    }

    if (resDict.empty()) return fonts;

    std::string fontDict;
    uint32_t fontDictObj = 0;
    if (FindIndirectRef(resDict, "/Font", fontDictObj)) {
        fontDict = GetObjectString(fontDictObj);
    } else {
        fontDict = ResolveDict(resDict, "/Font");
    }

    if (fontDict.empty()) return fonts;

    size_t pos = 0;
    while (pos < fontDict.size()) {
        size_t slash = fontDict.find('/', pos);
        if (slash == std::string::npos) break;

        size_t nameEnd = fontDict.find_first_of(" \t\r\n/<>[", slash + 1);
        if (nameEnd == std::string::npos) nameEnd = fontDict.size();

        std::string fKey = fontDict.substr(slash + 1, nameEnd - slash - 1);
        if (fKey.empty()) { pos = slash + 1; continue; }

        size_t valStart = fontDict.find_first_not_of(" \t\r\n", nameEnd);
        if (valStart == std::string::npos) break;

        std::string fontDef;
        if (fontDict[valStart] >= '0' && fontDict[valStart] <= '9') {
            char* endPtr = nullptr;
            uint32_t fObj = (uint32_t)strtoul(fontDict.c_str() + valStart, &endPtr, 10);
            if (fObj > 0) {
                fontDef = GetObjectString(fObj);
            }
            pos = (endPtr ? (endPtr - fontDict.c_str()) : valStart + 1);
        } else if (fontDict.compare(valStart, 2, "<<") == 0) {
            int depth = 0;
            size_t endDict = valStart;
            while (endDict < fontDict.size()) {
                if (fontDict.compare(endDict, 2, "<<") == 0) {
                    depth++;
                    endDict += 2;
                } else if (fontDict.compare(endDict, 2, ">>") == 0) {
                    depth--;
                    endDict += 2;
                    if (depth == 0) break;
                } else {
                    endDict++;
                }
            }
            if (depth == 0) {
                fontDef = fontDict.substr(valStart, endDict - valStart);
                pos = endDict;
            } else {
                pos = valStart + 2;
            }
        } else {
            pos = valStart + 1;
        }

        if (fontDef.empty()) continue;

        PdfFontInfo fontInfo;
        fontInfo.fontName = fKey;

        // /Subtype
        size_t stPos = fontDef.find("/Subtype");
        if (stPos != std::string::npos) {
            size_t stSlash = fontDef.find('/', stPos + 8);
            if (stSlash != std::string::npos) {
                size_t stEnd = fontDef.find_first_of(" \t\r\n/>", stSlash + 1);
                if (stEnd != std::string::npos) {
                    fontInfo.subtype = fontDef.substr(stSlash + 1, stEnd - stSlash - 1);
                }
            }
        }

        // /BaseFont
        size_t bfPos = fontDef.find("/BaseFont");
        if (bfPos != std::string::npos) {
            size_t bfSlash = fontDef.find('/', bfPos + 9);
            if (bfSlash != std::string::npos) {
                size_t bfEnd = fontDef.find_first_of(" \t\r\n/>", bfSlash + 1);
                if (bfEnd != std::string::npos) {
                    fontInfo.baseFont = fontDef.substr(bfSlash + 1, bfEnd - bfSlash - 1);
                }
            }
        }

        // /FirstChar
        size_t fcPos = fontDef.find("/FirstChar");
        if (fcPos != std::string::npos) {
            fontInfo.firstChar = (int)strtol(fontDef.c_str() + fcPos + 10, nullptr, 10);
        }

        // /LastChar
        size_t lcPos = fontDef.find("/LastChar");
        if (lcPos != std::string::npos) {
            fontInfo.lastChar = (int)strtol(fontDef.c_str() + lcPos + 9, nullptr, 10);
        }

        // /Widths
        uint32_t wObj = 0;
        std::string wStr;
        if (FindIndirectRef(fontDef, "/Widths", wObj)) {
            wStr = GetObjectString(wObj);
        } else {
            size_t wPos = fontDef.find("/Widths");
            if (wPos != std::string::npos) {
                size_t b1 = fontDef.find('[', wPos);
                size_t b2 = fontDef.find(']', b1);
                if (b1 != std::string::npos && b2 != std::string::npos) {
                    wStr = fontDef.substr(b1, b2 - b1 + 1);
                }
            }
        }
        if (!wStr.empty()) {
            size_t b1 = wStr.find('[');
            size_t b2 = wStr.find(']', b1);
            if (b1 != std::string::npos && b2 != std::string::npos) {
                std::stringstream wss(wStr.substr(b1 + 1, b2 - b1 - 1));
                float wVal = 0.0f;
                while (wss >> wVal) {
                    fontInfo.widths.push_back(wVal);
                }
            }
        }

        // /ToUnicode
        uint32_t tuObj = 0;
        if (FindIndirectRef(fontDef, "/ToUnicode", tuObj)) {
            std::vector<uint8_t> tuBytes;
            if (GetObjectStreamData(tuObj, tuBytes)) {
                fontInfo.toUnicode = ParseToUnicodeCMap(tuBytes);
            }
        }

        // If Type0, parse /DescendantFonts
        if (fontInfo.subtype == "Type0" || fontDef.find("/DescendantFonts") != std::string::npos) {
            size_t dfPos = fontDef.find("/DescendantFonts");
            if (dfPos != std::string::npos) {
                size_t b1 = fontDef.find('[', dfPos);
                size_t b2 = fontDef.find(']', b1);
                if (b1 != std::string::npos && b2 != std::string::npos) {
                    uint32_t descObj = 0;
                    std::string descStr = fontDef.substr(b1 + 1, b2 - b1 - 1);
                    std::stringstream dss(descStr);
                    if (dss >> descObj) {
                        std::string descDef = GetObjectString(descObj);
                        size_t dwPos = descDef.find("/DW");
                        if (dwPos != std::string::npos) {
                            fontInfo.defaultWidth = (float)strtod(descDef.c_str() + dwPos + 3, nullptr);
                            if (fontInfo.defaultWidth <= 0.0f) fontInfo.defaultWidth = 1000.0f;
                        }
                        uint32_t cidWObj = 0;
                        std::string cidWStr;
                        if (FindIndirectRef(descDef, "/W", cidWObj)) {
                            cidWStr = GetObjectString(cidWObj);
                        } else {
                            size_t wPos = descDef.find("/W");
                            if (wPos != std::string::npos) {
                                size_t wb1 = descDef.find('[', wPos);
                                size_t wb2 = descDef.find(']', wb1);
                                if (wb1 != std::string::npos && wb2 != std::string::npos) {
                                    cidWStr = descDef.substr(wb1, wb2 - wb1 + 1);
                                }
                            }
                        }
                        if (!cidWStr.empty()) {
                            size_t k = cidWStr.find('[');
                            size_t kEnd = cidWStr.rfind(']');
                            if (k != std::string::npos && kEnd != std::string::npos) {
                                k++;
                                while (k < kEnd) {
                                    while (k < kEnd && (cidWStr[k] == ' ' || cidWStr[k] == '\t' || cidWStr[k] == '\r' || cidWStr[k] == '\n')) k++;
                                    if (k >= kEnd) break;
                                    if (cidWStr[k] == '[' || cidWStr[k] == ']') { k++; continue; }

                                    char* pEnd = nullptr;
                                    long c1 = strtol(cidWStr.c_str() + k, &pEnd, 10);
                                    if (pEnd == cidWStr.c_str() + k) { k++; continue; }
                                    k = pEnd - cidWStr.c_str();

                                    while (k < kEnd && (cidWStr[k] == ' ' || cidWStr[k] == '\t' || cidWStr[k] == '\r' || cidWStr[k] == '\n')) k++;
                                    if (k >= kEnd) break;

                                    if (cidWStr[k] == '[') {
                                        k++;
                                        uint32_t curCid = (uint32_t)c1;
                                        while (k < kEnd && cidWStr[k] != ']') {
                                            while (k < kEnd && (cidWStr[k] == ' ' || cidWStr[k] == '\t' || cidWStr[k] == '\r' || cidWStr[k] == '\n')) k++;
                                            if (k >= kEnd || cidWStr[k] == ']') break;
                                            float w = (float)strtod(cidWStr.c_str() + k, &pEnd);
                                            if (pEnd == cidWStr.c_str() + k) { k++; break; }
                                            fontInfo.cidWidths[curCid++] = w;
                                            k = pEnd - cidWStr.c_str();
                                        }
                                        if (k < kEnd && cidWStr[k] == ']') k++;
                                    } else {
                                        long c2 = strtol(cidWStr.c_str() + k, &pEnd, 10);
                                        if (pEnd != cidWStr.c_str() + k) {
                                            k = pEnd - cidWStr.c_str();
                                            while (k < kEnd && (cidWStr[k] == ' ' || cidWStr[k] == '\t' || cidWStr[k] == '\r' || cidWStr[k] == '\n')) k++;
                                            float w = (float)strtod(cidWStr.c_str() + k, &pEnd);
                                            if (pEnd != cidWStr.c_str() + k) {
                                                k = pEnd - cidWStr.c_str();
                                                for (uint32_t c = (uint32_t)c1; c <= (uint32_t)c2 && c <= (uint32_t)c1 + 10000; ++c) {
                                                    fontInfo.cidWidths[c] = w;
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

        fonts[fKey] = std::move(fontInfo);
    }

    return fonts;
}

std::map<std::string, uint32_t> PdfParser::ExtractPageXObjects(const std::string& pageDict) {
    std::map<std::string, uint32_t> xobjects;

    std::string resDict;
    uint32_t resObj = 0;
    if (FindIndirectRef(pageDict, "/Resources", resObj)) {
        resDict = GetObjectString(resObj);
    } else {
        resDict = ResolveDict(pageDict, "/Resources");
        if (resDict.empty()) {
            uint32_t curParent = 0;
            if (FindIndirectRef(pageDict, "/Parent", curParent)) {
                for (int depth = 0; depth < 5 && curParent > 0; ++depth) {
                    std::string parentStr = GetObjectString(curParent);
                    if (FindIndirectRef(parentStr, "/Resources", resObj)) {
                        resDict = GetObjectString(resObj);
                        break;
                    } else {
                        resDict = ResolveDict(parentStr, "/Resources");
                        if (!resDict.empty()) break;
                    }
                    uint32_t nextParent = 0;
                    if (!FindIndirectRef(parentStr, "/Parent", nextParent)) break;
                    curParent = nextParent;
                }
            }
        }
    }

    if (resDict.empty()) return xobjects;

    std::string xobjDict;
    uint32_t xobjDictObj = 0;
    if (FindIndirectRef(resDict, "/XObject", xobjDictObj)) {
        xobjDict = GetObjectString(xobjDictObj);
    } else {
        xobjDict = ResolveDict(resDict, "/XObject");
    }

    if (xobjDict.empty()) return xobjects;

    size_t pos = 0;
    while (pos < xobjDict.size()) {
        size_t slash = xobjDict.find('/', pos);
        if (slash == std::string::npos) break;

        size_t nameEnd = xobjDict.find_first_of(" \t\r\n/<>[", slash + 1);
        if (nameEnd == std::string::npos) break;

        std::string xName = xobjDict.substr(slash + 1, nameEnd - slash - 1);
        uint32_t targetObj = 0;
        if (FindIndirectRef(xobjDict.substr(slash), "/" + xName, targetObj)) {
            xobjects[xName] = targetObj;
        }
        pos = nameEnd;
    }

    return xobjects;
}

bool PdfParser::ExtractPageText(uint32_t pageIndex, PdfPageText& outPage) {
    if (pageIndex >= m_pageObjectNums.size() || m_buffer.empty()) return false;

    outPage.pageIndex = pageIndex;
    outPage.fullText.clear();
    outPage.chars.clear();
    outPage.hasDigitalText = false;

    uint32_t pageObj = m_pageObjectNums[pageIndex];
    std::string pageDict = GetObjectString(pageObj);

    // 1. Extract CropBox or MediaBox dimensions and origin
    float x0 = 0.0f, y0 = 0.0f, x1 = 595.28f, y1 = 841.89f;

    auto parseBox = [](const std::string& dict, const std::string& key, float& rx0, float& ry0, float& rx1, float& ry1) -> bool {
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
    };

    if (!parseBox(pageDict, "/CropBox", x0, y0, x1, y1)) {
        if (!parseBox(pageDict, "/MediaBox", x0, y0, x1, y1)) {
            uint32_t curParent = 0;
            if (FindIndirectRef(pageDict, "/Parent", curParent)) {
                for (int depth = 0; depth < 5 && curParent > 0; ++depth) {
                    std::string parentDict = GetObjectString(curParent);
                    if (parseBox(parentDict, "/CropBox", x0, y0, x1, y1) || parseBox(parentDict, "/MediaBox", x0, y0, x1, y1)) {
                        break;
                    }
                    uint32_t nextParent = 0;
                    if (!FindIndirectRef(parentDict, "/Parent", nextParent)) break;
                    curParent = nextParent;
                }
            }
        }
    }

    float cropW = std::max(1.0f, x1 - x0);
    float cropH = std::max(1.0f, y1 - y0);

    // Check Rotate attribute
    int rotate = 0;
    auto parseRotate = [](const std::string& dict, int& rotOut) -> bool {
        size_t pos = dict.find("/Rotate");
        if (pos != std::string::npos) {
            size_t valPos = pos + 7;
            while (valPos < dict.size() && (dict[valPos] == ' ' || dict[valPos] == '\t' || dict[valPos] == '\r' || dict[valPos] == '\n')) valPos++;
            if (valPos < dict.size()) {
                rotOut = (int)strtol(dict.c_str() + valPos, nullptr, 10);
                rotOut = ((rotOut % 360) + 360) % 360;
                return true;
            }
        }
        return false;
    };

    if (!parseRotate(pageDict, rotate)) {
        uint32_t curParent = 0;
        if (FindIndirectRef(pageDict, "/Parent", curParent)) {
            for (int depth = 0; depth < 5 && curParent > 0; ++depth) {
                std::string parentDict = GetObjectString(curParent);
                if (parseRotate(parentDict, rotate)) break;
                uint32_t nextParent = 0;
                if (!FindIndirectRef(parentDict, "/Parent", nextParent)) break;
                curParent = nextParent;
            }
        }
    }

    outPage.rotation = rotate;
    float dispW = cropW * PDF_POINT_TO_DIP;
    float dispH = cropH * PDF_POINT_TO_DIP;
    if (rotate == 90 || rotate == 270) {
        std::swap(dispW, dispH);
    }
    outPage.pageWidth = dispW;
    outPage.pageHeight = dispH;

    // 2. Discover Font Resources & Metrics
    auto fonts = ExtractPageFonts(pageDict);
    auto xobjects = ExtractPageXObjects(pageDict);

    // 3. Extract /Contents objects
    std::vector<uint32_t> contentObjs;
    size_t cPos = pageDict.find("/Contents");
    if (cPos != std::string::npos) {
        size_t afterC = cPos + 9;
        while (afterC < pageDict.size() && (pageDict[afterC] == ' ' || pageDict[afterC] == '\t' || pageDict[afterC] == '\r' || pageDict[afterC] == '\n')) afterC++;

        if (afterC < pageDict.size() && pageDict[afterC] == '[') {
            size_t closeB = pageDict.find(']', afterC);
            if (closeB != std::string::npos) {
                std::string arrStr = pageDict.substr(afterC + 1, closeB - afterC - 1);
                std::stringstream css(arrStr);
                uint32_t cId = 0;
                std::string rTok;
                while (css >> cId >> rTok) {
                    if (rTok == "0" || rTok == "R") {
                        if (rTok == "0") css >> rTok;
                        contentObjs.push_back(cId);
                    }
                }
            }
        } else {
            uint32_t cId = (uint32_t)strtoul(pageDict.c_str() + afterC, nullptr, 10);
            if (cId > 0) contentObjs.push_back(cId);
        }
    }

    // 4. Decompress and parse all content streams in sequence
    for (uint32_t cId : contentObjs) {
        std::vector<uint8_t> streamBytes;
        if (GetObjectStreamData(cId, streamBytes) && !streamBytes.empty()) {
            ParseContentStream(streamBytes, x0, y0, cropW, cropH, rotate, fonts, outPage, xobjects);
        }
    }

    outPage.hasDigitalText = !outPage.chars.empty();
    return true;
}

void PdfParser::ParseContentStream(
    const std::vector<uint8_t>& streamBytes,
    float cropX0,
    float cropY0,
    float cropW,
    float cropH,
    int rotate,
    const std::map<std::string, PdfFontInfo>& fonts,
    PdfPageText& outPage,
    const std::map<std::string, uint32_t>& xobjects,
    Matrix2D initialCtm,
    int recursionDepth,
    D2D1_RECT_F localClip
) {
    if (streamBytes.empty()) return;

    const char* pStream = (const char*)streamBytes.data();
    size_t len = streamBytes.size();
    size_t i = 0;

    bool inText = false;
    float curFontSize = 12.0f;
    std::string curFontName = "";
    float curCharSpace = 0.0f;
    float curWordSpace = 0.0f;
    float curHScale = 100.0f;
    float curLeading = 12.0f;

    std::vector<Matrix2D> ctmStack;
    Matrix2D ctm = initialCtm;

    Matrix2D tm = Matrix2D::Identity();
    Matrix2D tlm = Matrix2D::Identity();
    float curX = 0.0f, curY = 0.0f;

    auto EmitBytes = [&](const std::vector<uint8_t>& rawBytes, bool isHex) {
        if (rawBytes.empty()) return;

        const PdfFontInfo* pFont = fonts.count(curFontName) ? &fonts.at(curFontName) : nullptr;
        bool is2Byte = false;
        if (pFont) {
            if (pFont->subtype == "Type0" || !pFont->cidWidths.empty() || pFont->baseFont.find("Identity") != std::string::npos) {
                is2Byte = true;
            }
        }

        float fontScaleX = curFontSize * (curHScale / 100.0f) * std::hypot(tm.a, tm.b) * std::hypot(ctm.a, ctm.b);
        float fontScaleY = curFontSize * std::hypot(tm.d, tm.c) * std::hypot(ctm.d, ctm.c);
        if (fontScaleX <= 0.0f) fontScaleX = curFontSize;
        if (fontScaleY <= 0.0f) fontScaleY = curFontSize;

        size_t bIdx = 0;
        while (bIdx < rawBytes.size()) {
            uint32_t charCode = 0;
            if (is2Byte) {
                if (bIdx + 1 < rawBytes.size()) {
                    charCode = ((uint32_t)rawBytes[bIdx] << 8) | (uint32_t)rawBytes[bIdx + 1];
                    bIdx += 2;
                } else {
                    charCode = rawBytes[bIdx++];
                }
            } else {
                charCode = rawBytes[bIdx++];
            }

            std::wstring decodedStr;
            if (pFont) {
                decodedStr = pFont->DecodeString(charCode);
            } else if (charCode >= 32 && charCode <= 126) {
                decodedStr = std::wstring(1, (wchar_t)charCode);
            } else {
                decodedStr = L"?";
            }

            float charWidthUnits = pFont ? pFont->GetCharWidth(charCode) : 500.0f;
            if (charWidthUnits <= 0.0f) charWidthUnits = 500.0f;

            float glyphW = (charWidthUnits / 1000.0f) * fontScaleX;
            float glyphH = fontScaleY;

            float userX = 0.0f, userY = 0.0f;
            ctm.Transform(curX, curY, userX, userY);

            // 1. Position relative to CropBox / MediaBox origin (in points)
            float relX = userX - cropX0;
            float relY = userY - cropY0;
            float yDownwards = cropH - relY;

            // 2. Convert from PDF points (72 DPI) to Direct2D DIPs (96 DPI)
            float dipX = relX * PDF_POINT_TO_DIP;
            float baselineY = yDownwards * PDF_POINT_TO_DIP;
            float dipW = glyphW * PDF_POINT_TO_DIP;
            float dipH = glyphH * PDF_POINT_TO_DIP;

            float top = baselineY - dipH * 0.88f;
            float bottom = baselineY + dipH * 0.22f;
            float left = dipX;
            float right = dipX + dipW;

            // 3. Handle Page Rotation if page is rotated
            D2D1_RECT_F charRect;
            float unrotW = cropW * PDF_POINT_TO_DIP;
            float unrotH = cropH * PDF_POINT_TO_DIP;

            if (rotate == 90) {
                // Clockwise 90 degrees
                charRect = D2D1::RectF(
                    unrotH - bottom,
                    left,
                    unrotH - top,
                    right
                );
            } else if (rotate == 180) {
                // 180 degrees
                charRect = D2D1::RectF(
                    unrotW - right,
                    unrotH - bottom,
                    unrotW - left,
                    unrotH - top
                );
            } else if (rotate == 270) {
                // Clockwise 270 degrees
                charRect = D2D1::RectF(
                    top,
                    unrotW - right,
                    bottom,
                    unrotW - left
                );
            } else {
                charRect = D2D1::RectF(left, top, right, bottom);
            }

            float charAdv = (charWidthUnits / 1000.0f) * curFontSize * (curHScale / 100.0f) + curCharSpace;
            if (!decodedStr.empty() && decodedStr[0] == L' ') {
                charAdv += curWordSpace;
            }

            // Skip characters clipped out by Form XObject BBox or too microscopic (< 2 DIPs)
            if (curX < localClip.left || curX > localClip.right || curY < localClip.top || curY > localClip.bottom || glyphH < 2.0f || glyphW <= 0.0f) {
                curX += charAdv;
                continue;
            }

            for (size_t dIdx = 0; dIdx < decodedStr.size(); ++dIdx) {
                wchar_t wch = decodedStr[dIdx];
                PdfTextChar tc;
                tc.ch = wch;
                tc.rect = charRect;

                outPage.fullText.push_back(wch);
                outPage.chars.push_back(tc);
            }

            curX += charAdv;
        }
    };

    while (i < len) {
        while (i < len && (pStream[i] == ' ' || pStream[i] == '\t' || pStream[i] == '\r' || pStream[i] == '\n')) i++;
        if (i >= len) break;

        // Save graphics state 'q'
        if (pStream[i] == 'q' && (i + 1 >= len || pStream[i + 1] == ' ' || pStream[i + 1] == '\t' || pStream[i + 1] == '\r' || pStream[i + 1] == '\n')) {
            ctmStack.push_back(ctm);
            i++;
            continue;
        }

        // Restore graphics state 'Q'
        if (pStream[i] == 'Q' && (i + 1 >= len || pStream[i + 1] == ' ' || pStream[i + 1] == '\t' || pStream[i + 1] == '\r' || pStream[i + 1] == '\n')) {
            if (!ctmStack.empty()) {
                ctm = ctmStack.back();
                ctmStack.pop_back();
            }
            i++;
            continue;
        }

        // Matrix concatenation 'cm'
        if (i + 2 <= len && pStream[i] == 'c' && pStream[i + 1] == 'm' && (i + 2 >= len || pStream[i + 2] == ' ' || pStream[i + 2] == '\t' || pStream[i + 2] == '\r' || pStream[i + 2] == '\n')) {
            size_t lineStart = i;
            while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
            std::string mLine(pStream + lineStart, i - lineStart);
            std::stringstream mss(mLine);
            std::vector<float> vals;
            float v = 0.0f;
            while (mss >> v) vals.push_back(v);
            if (vals.size() >= 6) {
                Matrix2D m{
                    vals[vals.size() - 6],
                    vals[vals.size() - 5],
                    vals[vals.size() - 4],
                    vals[vals.size() - 3],
                    vals[vals.size() - 2],
                    vals[vals.size() - 1]
                };
                ctm = m.Multiply(ctm);
            }
            i += 2;
            continue;
        }

        // Begin text object 'BT'
        if (pStream[i] == 'B' && i + 1 < len && pStream[i + 1] == 'T') {
            inText = true;
            tm = Matrix2D::Identity();
            tlm = Matrix2D::Identity();
            curX = 0.0f;
            curY = 0.0f;
            i += 2;
            continue;
        }

        // End text object 'ET'
        if (pStream[i] == 'E' && i + 1 < len && pStream[i + 1] == 'T') {
            inText = false;
            if (!outPage.fullText.empty() && outPage.fullText.back() != L' ') {
                outPage.fullText.push_back(L' ');
                outPage.chars.push_back({ L' ', D2D1::RectF(curX, 0, curX, 0) });
            }
            i += 2;
            continue;
        }

        // XObject invocation: "/Name Do"
        if (!inText && pStream[i] == '/' && recursionDepth < 4) {
            size_t nStart = i + 1;
            size_t nEnd = nStart;
            while (nEnd < len && pStream[nEnd] != ' ' && pStream[nEnd] != '\t' && pStream[nEnd] != '\r' && pStream[nEnd] != '\n' && pStream[nEnd] != '/') nEnd++;
            std::string xName(pStream + nStart, nEnd - nStart);
            size_t opPos = nEnd;
            while (opPos < len && (pStream[opPos] == ' ' || pStream[opPos] == '\t' || pStream[opPos] == '\r' || pStream[opPos] == '\n')) opPos++;
            if (opPos + 2 <= len && pStream[opPos] == 'D' && pStream[opPos + 1] == 'o' &&
                (opPos + 2 >= len || pStream[opPos + 2] == ' ' || pStream[opPos + 2] == '\t' || pStream[opPos + 2] == '\r' || pStream[opPos + 2] == '\n')) {
                auto itX = xobjects.find(xName);
                if (itX != xobjects.end()) {
                    uint32_t xObjId = itX->second;
                    std::string xDict = GetObjectString(xObjId);
                    if (xDict.find("/Subtype /Form") != std::string::npos || xDict.find("/Subtype/Form") != std::string::npos) {
                        // Check for Form /Matrix
                        Matrix2D formMat = Matrix2D::Identity();
                        size_t matPos = xDict.find("/Matrix");
                        if (matPos != std::string::npos) {
                            size_t b1 = xDict.find('[', matPos);
                            size_t b2 = xDict.find(']', b1);
                            if (b1 != std::string::npos && b2 != std::string::npos) {
                                std::string mStr = xDict.substr(b1 + 1, b2 - b1 - 1);
                                std::stringstream mss(mStr);
                                float ma = 1, mb = 0, mc = 0, md = 1, me = 0, mf = 0;
                                if (mss >> ma >> mb >> mc >> md >> me >> mf) {
                                    formMat = Matrix2D{ ma, mb, mc, md, me, mf };
                                }
                            }
                        }
                        Matrix2D formCtm = formMat.Multiply(ctm);

                        // Read Form BBox if present
                        D2D1_RECT_F formBBox = { -1e9f, -1e9f, 1e9f, 1e9f };
                        size_t bbPos = xDict.find("/BBox");
                        if (bbPos != std::string::npos) {
                            size_t b1 = xDict.find('[', bbPos);
                            size_t b2 = xDict.find(']', b1);
                            if (b1 != std::string::npos && b2 != std::string::npos) {
                                std::string bbStr = xDict.substr(b1 + 1, b2 - b1 - 1);
                                std::stringstream bss(bbStr);
                                float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
                                if (bss >> bx0 >> by0 >> bx1 >> by1) {
                                    formBBox = D2D1::RectF(
                                        std::min(bx0, bx1) - 1.0f,
                                        std::min(by0, by1) - 1.0f,
                                        std::max(bx0, bx1) + 1.0f,
                                        std::max(by0, by1) + 1.0f
                                    );
                                }
                            }
                        }

                        // Inherit fonts and merge with Form XObject fonts
                        auto formFonts = fonts;
                        auto childFonts = ExtractPageFonts(xDict);
                        for (auto& cf : childFonts) {
                            formFonts[cf.first] = std::move(cf.second);
                        }

                        // Child XObjects
                        auto childXObjects = ExtractPageXObjects(xDict);

                        std::vector<uint8_t> formStream;
                        if (GetObjectStreamData(xObjId, formStream) && !formStream.empty()) {
                            ParseContentStream(formStream, cropX0, cropY0, cropW, cropH, rotate, formFonts, outPage, childXObjects, formCtm, recursionDepth + 1, formBBox);
                        }
                    }
                }
                i = opPos + 2;
                continue;
            }
        }

        if (inText) {
            // Font operator: "/FontName Size Tf"
            if (pStream[i] == '/') {
                size_t fnStart = i + 1;
                size_t fnEnd = fnStart;
                while (fnEnd < len && pStream[fnEnd] != ' ' && pStream[fnEnd] != '\t' && pStream[fnEnd] != '\r' && pStream[fnEnd] != '\n') fnEnd++;
                std::string fName(pStream + fnStart, fnEnd - fnStart);
                size_t szStart = fnEnd;
                while (szStart < len && (pStream[szStart] == ' ' || pStream[szStart] == '\t' || pStream[szStart] == '\r' || pStream[szStart] == '\n')) szStart++;
                char* pEnd = nullptr;
                float fSize = (float)strtod(pStream + szStart, &pEnd);
                if (pEnd && pEnd != pStream + szStart) {
                    size_t opPos = pEnd - pStream;
                    while (opPos < len && (pStream[opPos] == ' ' || pStream[opPos] == '\t' || pStream[opPos] == '\r' || pStream[opPos] == '\n')) opPos++;
                    if (opPos + 2 <= len && pStream[opPos] == 'T' && pStream[opPos + 1] == 'f') {
                        curFontName = fName;
                        if (fSize > 0.1f) curFontSize = fSize;
                        i = opPos + 2;
                        continue;
                    }
                }
            }

            // Text matrix: "a b c d e f Tm"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == 'm') {
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                std::vector<float> vals;
                float v = 0.0f;
                while (mss >> v) vals.push_back(v);
                if (vals.size() >= 6) {
                    tm.a = vals[vals.size() - 6];
                    tm.b = vals[vals.size() - 5];
                    tm.c = vals[vals.size() - 4];
                    tm.d = vals[vals.size() - 3];
                    tm.e = vals[vals.size() - 2];
                    tm.f = vals[vals.size() - 1];
                    tlm = tm;
                    curX = tm.e;
                    curY = tm.f;
                }
                i += 2;
                continue;
            }

            // Translation operator: "tx ty Td" or "tx ty TD"
            if (i + 2 <= len && pStream[i] == 'T' && (pStream[i + 1] == 'd' || pStream[i + 1] == 'D')) {
                bool isTD = (pStream[i + 1] == 'D');
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                std::vector<float> vals;
                float v = 0.0f;
                while (mss >> v) vals.push_back(v);
                if (vals.size() >= 2) {
                    float tx = vals[vals.size() - 2];
                    float ty = vals[vals.size() - 1];
                    if (isTD) curLeading = -ty;

                    float newE = tx * tlm.a + ty * tlm.c + tlm.e;
                    float newF = tx * tlm.b + ty * tlm.d + tlm.f;
                    tlm.e = newE;
                    tlm.f = newF;
                    tm = tlm;
                    curX = tlm.e;
                    curY = tlm.f;
                }
                i += 2;
                continue;
            }

            // Move to start of next line: "T*"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == '*') {
                float newE = (-curLeading) * tlm.c + tlm.e;
                float newF = (-curLeading) * tlm.d + tlm.f;
                tlm.e = newE;
                tlm.f = newF;
                tm = tlm;
                curX = tlm.e;
                curY = tlm.f;
                i += 2;
                continue;
            }

            // Character spacing: "charSpace Tc"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == 'c') {
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                float val = 0.0f;
                while (mss >> val) curCharSpace = val;
                i += 2;
                continue;
            }

            // Word spacing: "wordSpace Tw"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == 'w') {
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                float val = 0.0f;
                while (mss >> val) curWordSpace = val;
                i += 2;
                continue;
            }

            // Horizontal scaling: "scale Tz"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == 'z') {
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                float val = 100.0f;
                while (mss >> val) curHScale = val;
                i += 2;
                continue;
            }

            // Text leading: "leading TL"
            if (i + 2 <= len && pStream[i] == 'T' && pStream[i + 1] == 'L') {
                size_t lineStart = i;
                while (lineStart > 0 && pStream[lineStart - 1] != '\n' && pStream[lineStart - 1] != '\r') lineStart--;
                std::string mLine(pStream + lineStart, i - lineStart);
                std::stringstream mss(mLine);
                float val = 12.0f;
                while (mss >> val) curLeading = val;
                i += 2;
                continue;
            }

            // Literal string Tj: "(text) Tj"
            if (pStream[i] == '(') {
                size_t strStart = i + 1;
                size_t strEnd = strStart;
                int parenDepth = 1;
                while (strEnd < len && parenDepth > 0) {
                    if (pStream[strEnd] == '\\' && strEnd + 1 < len) {
                        strEnd += 2;
                        continue;
                    }
                    if (pStream[strEnd] == '(') parenDepth++;
                    else if (pStream[strEnd] == ')') parenDepth--;
                    strEnd++;
                }

                size_t opPos = strEnd;
                while (opPos < len && (pStream[opPos] == ' ' || pStream[opPos] == '\t' || pStream[opPos] == '\r' || pStream[opPos] == '\n')) opPos++;
                if (opPos + 2 <= len && pStream[opPos] == 'T' && pStream[opPos + 1] == 'j') {
                    // Unescape literal bytes
                    std::vector<uint8_t> rawBytes;
                    for (size_t k = strStart; k + 1 < strEnd; ++k) {
                        if (pStream[k] == '\\' && k + 1 < strEnd - 1) {
                            k++;
                            char ec = pStream[k];
                            if (ec == 'n') rawBytes.push_back('\n');
                            else if (ec == 'r') rawBytes.push_back('\r');
                            else if (ec == 't') rawBytes.push_back('\t');
                            else if (ec == 'b') rawBytes.push_back('\b');
                            else if (ec == 'f') rawBytes.push_back('\f');
                            else if (ec == '(' || ec == ')' || ec == '\\') rawBytes.push_back((uint8_t)ec);
                            else if (ec >= '0' && ec <= '7') {
                                int oct = ec - '0';
                                if (k + 1 < strEnd - 1 && pStream[k + 1] >= '0' && pStream[k + 1] <= '7') {
                                    oct = oct * 8 + (pStream[++k] - '0');
                                    if (k + 1 < strEnd - 1 && pStream[k + 1] >= '0' && pStream[k + 1] <= '7') {
                                        oct = oct * 8 + (pStream[++k] - '0');
                                    }
                                }
                                rawBytes.push_back((uint8_t)oct);
                            } else {
                                rawBytes.push_back((uint8_t)ec);
                            }
                        } else {
                            rawBytes.push_back((uint8_t)pStream[k]);
                        }
                    }
                    EmitBytes(rawBytes, false);
                    i = opPos + 2;
                    continue;
                }
            }

            // Hex string Tj: "<hex> Tj"
            if (pStream[i] == '<' && i + 1 < len && pStream[i + 1] != '<') {
                size_t hexStart = i + 1;
                size_t hexEnd = hexStart;
                while (hexEnd < len && pStream[hexEnd] != '>') hexEnd++;
                if (hexEnd < len) {
                    size_t opPos = hexEnd + 1;
                    while (opPos < len && (pStream[opPos] == ' ' || pStream[opPos] == '\t' || pStream[opPos] == '\r' || pStream[opPos] == '\n')) opPos++;
                    if (opPos + 2 <= len && pStream[opPos] == 'T' && pStream[opPos + 1] == 'j') {
                        std::vector<uint8_t> rawBytes;
                        std::string hexStr;
                        for (size_t h = hexStart; h < hexEnd; ++h) {
                            if (!isspace((unsigned char)pStream[h])) hexStr.push_back(pStream[h]);
                        }
                        if (hexStr.size() % 2 != 0) hexStr.push_back('0');
                        for (size_t h = 0; h + 1 < hexStr.size(); h += 2) {
                            char hbuf[3] = { hexStr[h], hexStr[h + 1], 0 };
                            rawBytes.push_back((uint8_t)strtoul(hbuf, nullptr, 16));
                        }
                        EmitBytes(rawBytes, true);
                        i = opPos + 2;
                        continue;
                    }
                }
            }

            // Array string TJ: "[ ... ] TJ"
            if (pStream[i] == '[') {
                size_t arrStart = i + 1;
                size_t arrEnd = arrStart;
                int bDepth = 1;
                while (arrEnd < len && bDepth > 0) {
                    if (pStream[arrEnd] == '[') bDepth++;
                    else if (pStream[arrEnd] == ']') bDepth--;
                    else if (pStream[arrEnd] == '(') {
                        arrEnd++;
                        int pDepth = 1;
                        while (arrEnd < len && pDepth > 0) {
                            if (pStream[arrEnd] == '\\' && arrEnd + 1 < len) { arrEnd += 2; continue; }
                            if (pStream[arrEnd] == '(') pDepth++;
                            else if (pStream[arrEnd] == ')') pDepth--;
                            arrEnd++;
                        }
                        continue;
                    }
                    arrEnd++;
                }

                size_t opPos = arrEnd;
                while (opPos < len && (pStream[opPos] == ' ' || pStream[opPos] == '\t' || pStream[opPos] == '\r' || pStream[opPos] == '\n')) opPos++;
                if (opPos + 2 <= len && pStream[opPos] == 'T' && pStream[opPos + 1] == 'J') {
                    size_t k = arrStart;
                    while (k < arrEnd - 1) {
                        while (k < arrEnd - 1 && (pStream[k] == ' ' || pStream[k] == '\t' || pStream[k] == '\r' || pStream[k] == '\n')) k++;
                        if (k >= arrEnd - 1) break;

                        if (pStream[k] == '(') {
                            size_t pStart = k + 1;
                            size_t pEnd = pStart;
                            int pDepth = 1;
                            while (pEnd < arrEnd - 1 && pDepth > 0) {
                                if (pStream[pEnd] == '\\' && pEnd + 1 < arrEnd - 1) { pEnd += 2; continue; }
                                if (pStream[pEnd] == '(') pDepth++;
                                else if (pStream[pEnd] == ')') pDepth--;
                                pEnd++;
                            }
                            std::vector<uint8_t> rawBytes;
                            for (size_t c = pStart; c + 1 < pEnd; ++c) {
                                if (pStream[c] == '\\' && c + 1 < pEnd - 1) {
                                    c++;
                                    char ec = pStream[c];
                                    if (ec == 'n') rawBytes.push_back('\n');
                                    else if (ec == 'r') rawBytes.push_back('\r');
                                    else if (ec == 't') rawBytes.push_back('\t');
                                    else if (ec == 'b') rawBytes.push_back('\b');
                                    else if (ec == 'f') rawBytes.push_back('\f');
                                    else if (ec == '(' || ec == ')' || ec == '\\') rawBytes.push_back((uint8_t)ec);
                                    else if (ec >= '0' && ec <= '7') {
                                        int oct = ec - '0';
                                        if (c + 1 < pEnd - 1 && pStream[c + 1] >= '0' && pStream[c + 1] <= '7') {
                                            oct = oct * 8 + (pStream[++c] - '0');
                                            if (c + 1 < pEnd - 1 && pStream[c + 1] >= '0' && pStream[c + 1] <= '7') {
                                                oct = oct * 8 + (pStream[++c] - '0');
                                            }
                                        }
                                        rawBytes.push_back((uint8_t)oct);
                                    } else {
                                        rawBytes.push_back((uint8_t)ec);
                                    }
                                } else {
                                    rawBytes.push_back((uint8_t)pStream[c]);
                                }
                            }
                            EmitBytes(rawBytes, false);
                            k = pEnd;
                        } else if (pStream[k] == '<' && k + 1 < arrEnd - 1 && pStream[k + 1] != '<') {
                            size_t hStart = k + 1;
                            size_t hEnd = hStart;
                            while (hEnd < arrEnd - 1 && pStream[hEnd] != '>') hEnd++;
                            std::vector<uint8_t> rawBytes;
                            std::string hexStr;
                            for (size_t h = hStart; h < hEnd; ++h) {
                                if (!isspace((unsigned char)pStream[h])) hexStr.push_back(pStream[h]);
                            }
                            if (hexStr.size() % 2 != 0) hexStr.push_back('0');
                            for (size_t h = 0; h + 1 < hexStr.size(); h += 2) {
                                char hbuf[3] = { hexStr[h], hexStr[h + 1], 0 };
                                rawBytes.push_back((uint8_t)strtoul(hbuf, nullptr, 16));
                            }
                            EmitBytes(rawBytes, true);
                            k = hEnd + 1;
                        } else {
                            char* pEnd = nullptr;
                            float kern = (float)strtod(pStream + k, &pEnd);
                            if (pEnd && pEnd != pStream + k) {
                                curX -= (kern / 1000.0f) * curFontSize * (curHScale / 100.0f);
                                if (kern < -250.0f && !outPage.fullText.empty() && outPage.fullText.back() != L' ') {
                                    outPage.fullText.push_back(L' ');
                                    outPage.chars.push_back({ L' ', D2D1::RectF(curX, 0, curX, 0) });
                                }
                                k = pEnd - pStream;
                            } else {
                                k++;
                            }
                        }
                    }
                    i = opPos + 2;
                    continue;
                }
            }
        }

        i++;
    }
}


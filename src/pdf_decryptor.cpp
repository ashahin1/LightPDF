/**
 * @file pdf_decryptor.cpp
 * @brief Lossless PDF decryption and unencrypted PDF serialization engine.
 */

#include "pdf_decryptor.hpp"
#include "pdf_parser.hpp"
#include "common_utils.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <map>
#include <atomic>

#pragma comment(lib, "bcrypt.lib")

// Standard PDF password padding string (32 bytes) per ISO 32000-1 §7.6.3.3
static const uint8_t s_passwordPadding[32] = {
    0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41,
    0x64, 0x00, 0x4e, 0x56, 0xff, 0xfa, 0x01, 0x08,
    0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80,
    0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a
};

// ============================================================================
// Cryptographic Helper Functions
// ============================================================================

static std::vector<uint8_t> ComputeBCryptHash(LPCWSTR algId, size_t digestSize, const uint8_t* data, size_t len) {
    std::vector<uint8_t> digest(digestSize, 0);
    BCRYPT_ALG_HANDLE hAlg = NULL;
    if (BCryptOpenAlgorithmProvider(&hAlg, algId, NULL, 0) == 0) {
        BCRYPT_HASH_HANDLE hHash = NULL;
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            BCryptHashData(hHash, (PUCHAR)data, (ULONG)len, 0);
            BCryptFinishHash(hHash, digest.data(), (ULONG)digest.size(), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return digest;
}

static std::vector<uint8_t> ComputeMD5(const uint8_t* data, size_t len) {
    return ComputeBCryptHash(BCRYPT_MD5_ALGORITHM, 16, data, len);
}

static std::vector<uint8_t> ComputeSHA256(const uint8_t* data, size_t len) {
    return ComputeBCryptHash(BCRYPT_SHA256_ALGORITHM, 32, data, len);
}

static void Rc4Crypt(const uint8_t* key, size_t keyLen, const uint8_t* in, size_t inLen, uint8_t* out) {
    if (!key || keyLen == 0 || !in || inLen == 0 || !out) return;
    uint8_t s[256];
    for (int k = 0; k < 256; ++k) s[k] = (uint8_t)k;
    uint8_t j = 0;
    for (int k = 0; k < 256; ++k) {
        j = (uint8_t)(j + s[k] + key[k % keyLen]);
        std::swap(s[k], s[j]);
    }
    uint8_t i = 0;
    j = 0;
    for (size_t k = 0; k < inLen; ++k) {
        i = (uint8_t)(i + 1);
        j = (uint8_t)(j + s[i]);
        std::swap(s[i], s[j]);
        out[k] = in[k] ^ s[(uint8_t)(s[i] + s[j])];
    }
}

static bool AesCbcDecrypt(
    const uint8_t* key, size_t keyLen,
    const uint8_t* iv,
    const uint8_t* in, size_t inLen,
    std::vector<uint8_t>& out
) {
    if (inLen == 0 || inLen % 16 != 0) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) return false;

    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCRYPT_KEY_HANDLE hKey = NULL;
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key, (ULONG)keyLen, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    uint8_t ivBuf[16];
    memcpy(ivBuf, iv, 16);

    out.resize(inLen);
    ULONG cbResult = 0;
    NTSTATUS status = BCryptDecrypt(hKey, (PUCHAR)in, (ULONG)inLen, NULL, ivBuf, 16, out.data(), (ULONG)out.size(), &cbResult, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status != 0 || cbResult == 0) return false;

    out.resize(cbResult);
    // Remove PKCS#7 padding
    uint8_t pad = out.back();
    if (pad > 0 && pad <= 16 && pad <= out.size()) {
        bool validPad = true;
        for (size_t p = out.size() - pad; p < out.size(); ++p) {
            if (out[p] != pad) { validPad = false; break; }
        }
        if (validPad) {
            out.resize(out.size() - pad);
        }
    }
    return true;
}

// Generate unpredictable temporary path in same directory to prevent symlink attacks (SEC-03)
static std::wstring MakeUniqueTempPath(const std::wstring& targetPath) {
    static std::atomic<uint32_t> s_tempCounter{ 0 };
    uint32_t c = ++s_tempCounter;
    DWORD pid = GetCurrentProcessId();
    ULONGLONG ticks = GetTickCount64();
    return targetPath + L"." + std::to_wstring(pid) + L"_" + std::to_wstring(ticks) + L"_" + std::to_wstring(c) + L".tmp";
}

static std::string HexEncodeBytes(const uint8_t* data, size_t len) {
    static const char hexChars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        out.push_back(hexChars[data[i] & 0x0F]);
    }
    return out;
}

static std::vector<uint8_t> HexDecodeStr(const char* data, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len / 2);
    int hi = -1;
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        int val = -1;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        else if (c == '>') break;
        else break;

        if (hi < 0) {
            hi = val;
        } else {
            out.push_back(static_cast<uint8_t>((hi << 4) | val));
            hi = -1;
        }
    }
    if (hi >= 0) {
        out.push_back(static_cast<uint8_t>(hi << 4));
    }
    return out;
}

// Decrypt literal and hex string literals within a non-stream object according to ISO 32000-1 §7.6.2 (SEC-12)
static std::string DecryptNonStreamStrings(const std::string& objText, const std::vector<uint8_t>& objKey, bool isAES) {
    if (objKey.empty() || objText.empty()) return objText;

    std::string result;
    result.reserve(objText.size());
    size_t i = 0;
    size_t len = objText.size();

    while (i < len) {
        // Hex string: starts with '<' and not '<<'
        if (objText[i] == '<' && (i + 1 < len && objText[i + 1] != '<')) {
            size_t closePos = objText.find('>', i + 1);
            if (closePos != std::string::npos) {
                std::string hexBody = objText.substr(i + 1, closePos - i - 1);
                std::vector<uint8_t> cipherBytes = HexDecodeStr(hexBody.data(), hexBody.size());
                std::vector<uint8_t> plainBytes;
                bool decOk = false;
                if (isAES) {
                    if (cipherBytes.size() >= 16) {
                        const uint8_t* iv = cipherBytes.data();
                        const uint8_t* cipher = cipherBytes.data() + 16;
                        size_t cipherLen = cipherBytes.size() - 16;
                        AesCbcDecrypt(objKey.data(), objKey.size(), iv, cipher, cipherLen, plainBytes);
                        decOk = !plainBytes.empty();
                    }
                } else {
                    if (!cipherBytes.empty()) {
                        plainBytes.resize(cipherBytes.size());
                        Rc4Crypt(objKey.data(), objKey.size(), cipherBytes.data(), cipherBytes.size(), plainBytes.data());
                        decOk = true;
                    }
                }

                if (decOk) {
                    result.push_back('<');
                    result.append(HexEncodeBytes(plainBytes.data(), plainBytes.size()));
                    result.push_back('>');
                } else {
                    result.append(objText.substr(i, closePos - i + 1));
                }
                i = closePos + 1;
                continue;
            }
        }

        // Literal string: starts with '('
        if (objText[i] == '(') {
            size_t startStr = i;
            size_t depth = 1;
            std::vector<uint8_t> rawBytes;
            i++;
            bool escape = false;
            while (i < len && depth > 0) {
                char ch = objText[i];
                if (escape) {
                    if (ch == 'n') rawBytes.push_back('\n');
                    else if (ch == 'r') rawBytes.push_back('\r');
                    else if (ch == 't') rawBytes.push_back('\t');
                    else if (ch == 'b') rawBytes.push_back('\b');
                    else if (ch == 'f') rawBytes.push_back('\f');
                    else if (ch == '(') rawBytes.push_back('(');
                    else if (ch == ')') rawBytes.push_back(')');
                    else if (ch == '\\') rawBytes.push_back('\\');
                    else if (ch >= '0' && ch <= '7') {
                        int oct = ch - '0';
                        if (i + 1 < len && objText[i + 1] >= '0' && objText[i + 1] <= '7') {
                            oct = oct * 8 + (objText[++i] - '0');
                            if (i + 1 < len && objText[i + 1] >= '0' && objText[i + 1] <= '7') {
                                oct = oct * 8 + (objText[++i] - '0');
                            }
                        }
                        rawBytes.push_back(static_cast<uint8_t>(oct & 0xFF));
                    } else {
                        rawBytes.push_back(static_cast<uint8_t>(ch));
                    }
                    escape = false;
                } else {
                    if (ch == '\\') {
                        escape = true;
                    } else if (ch == '(') {
                        depth++;
                        rawBytes.push_back(static_cast<uint8_t>(ch));
                    } else if (ch == ')') {
                        depth--;
                        if (depth > 0) {
                            rawBytes.push_back(static_cast<uint8_t>(ch));
                        }
                    } else {
                        rawBytes.push_back(static_cast<uint8_t>(ch));
                    }
                }
                i++;
            }

            if (depth == 0) {
                std::vector<uint8_t> plainBytes;
                bool decOk = false;
                if (isAES) {
                    if (rawBytes.size() >= 16) {
                        const uint8_t* iv = rawBytes.data();
                        const uint8_t* cipher = rawBytes.data() + 16;
                        size_t cipherLen = rawBytes.size() - 16;
                        AesCbcDecrypt(objKey.data(), objKey.size(), iv, cipher, cipherLen, plainBytes);
                        decOk = !plainBytes.empty();
                    }
                } else {
                    if (!rawBytes.empty()) {
                        plainBytes.resize(rawBytes.size());
                        Rc4Crypt(objKey.data(), objKey.size(), rawBytes.data(), rawBytes.size(), plainBytes.data());
                        decOk = true;
                    }
                }

                if (decOk) {
                    result.push_back('<');
                    result.append(HexEncodeBytes(plainBytes.data(), plainBytes.size()));
                    result.push_back('>');
                } else {
                    result.append(objText.substr(startStr, i - startStr));
                }
                continue;
            } else {
                result.append(objText.substr(startStr, i - startStr));
                continue;
            }
        }

        result.push_back(objText[i++]);
    }

    return result;
}

// ============================================================================
// PDF Decryption Parser & Serializer
// ============================================================================

struct EncryptInfo {
    int v = 1;
    int r = 2;
    int length = 40;
    int64_t p = 0;
    std::vector<uint8_t> o;
    std::vector<uint8_t> u;
    std::vector<uint8_t> oe;
    std::vector<uint8_t> ue;
    std::vector<uint8_t> perms;
    std::vector<uint8_t> fileId;
    bool encryptMetadata = true;
    bool isAES = false;
    uint32_t encryptObjNum = 0;
};

static std::vector<uint8_t> ParseHexBytes(const std::string& str) {
    std::vector<uint8_t> bytes;
    int hi = -1;
    for (char c : str) {
        int val = -1;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        if (val >= 0) {
            if (hi == -1) hi = val;
            else {
                bytes.push_back(static_cast<uint8_t>((hi << 4) | val));
                hi = -1;
            }
        }
    }
    if (hi >= 0) {
        bytes.push_back(static_cast<uint8_t>(hi << 4));
    }
    return bytes;
}

static std::string ExtractStringBetween(const std::string& src, size_t start, char openCh, char closeCh, size_t& outEnd) {
    size_t openPos = src.find(openCh, start);
    if (openPos == std::string::npos) return "";
    size_t pos = openPos + 1;
    int depth = 1;
    while (pos < src.size() && depth > 0) {
        if (src[pos] == '\\' && openCh == '(') {
            pos += 2;
            continue;
        }
        if (src[pos] == openCh) depth++;
        else if (src[pos] == closeCh) depth--;
        pos++;
    }
    outEnd = pos;
    if (depth == 0) {
        return src.substr(openPos + 1, (pos - 1) - (openPos + 1));
    }
    return "";
}

static bool ParseEncryptDict(
    const std::string& fileContent,
    EncryptInfo& outInfo
) {
    // 1. Locate trailer
    size_t trailerPos = fileContent.rfind("trailer");
    if (trailerPos == std::string::npos) {
        // Try searching forward if rfind missed
        trailerPos = fileContent.find("trailer");
    }
    if (trailerPos == std::string::npos) return false;

    // 2. Extract /ID array
    size_t idPos = fileContent.find("/ID", trailerPos);
    if (idPos != std::string::npos) {
        size_t idEnd = 0;
        std::string idContent = ExtractStringBetween(fileContent, idPos, '[', ']', idEnd);
        if (!idContent.empty()) {
            size_t hexEnd = 0;
            std::string id0Hex = ExtractStringBetween(idContent, 0, '<', '>', hexEnd);
            if (!id0Hex.empty()) {
                outInfo.fileId = ParseHexBytes(id0Hex);
            } else {
                size_t strEnd = 0;
                std::string id0Str = ExtractStringBetween(idContent, 0, '(', ')', strEnd);
                outInfo.fileId.assign(id0Str.begin(), id0Str.end());
            }
        }
    }

    // 3. Locate /Encrypt
    size_t encPos = fileContent.find("/Encrypt", trailerPos);
    if (encPos == std::string::npos) return false;

    // Check if indirect reference e.g. /Encrypt 6 0 R
    size_t p = encPos + 8;
    while (p < fileContent.size() && (fileContent[p] == ' ' || fileContent[p] == '\t' || fileContent[p] == '\r' || fileContent[p] == '\n')) p++;

    std::string encDictStr;
    if (p < fileContent.size() && fileContent[p] >= '0' && fileContent[p] <= '9') {
        // Indirect object reference
        char* endPtr = nullptr;
        unsigned long objNum = strtoul(&fileContent[p], &endPtr, 10);
        outInfo.encryptObjNum = static_cast<uint32_t>(objNum);

        // Find obj definition
        std::string objHeader = std::to_string(objNum) + " ";
        size_t searchPos = 0;
        size_t objPos = std::string::npos;
        while ((objPos = fileContent.find(objHeader, searchPos)) != std::string::npos) {
            size_t afterNum = objPos + objHeader.size();
            while (afterNum < fileContent.size() && (fileContent[afterNum] >= '0' && fileContent[afterNum] <= '9')) afterNum++;
            while (afterNum < fileContent.size() && (fileContent[afterNum] == ' ' || fileContent[afterNum] == '\t')) afterNum++;
            if (afterNum + 3 <= fileContent.size() && fileContent.compare(afterNum, 3, "obj") == 0) {
                break;
            }
            searchPos = objPos + 1;
        }

        if (objPos != std::string::npos) {
            size_t endObj = fileContent.find("endobj", objPos);
            if (endObj != std::string::npos) {
                encDictStr = fileContent.substr(objPos, endObj - objPos);
            }
        }
    } else if (fileContent.compare(p, 2, "<<") == 0) {
        size_t dictEnd = 0;
        encDictStr = ExtractStringBetween(fileContent, p, '<', '>', dictEnd);
    }

    if (encDictStr.empty()) return false;

    // Parse V
    size_t vPos = encDictStr.find("/V");
    if (vPos != std::string::npos) {
        char* endPtr = nullptr;
        outInfo.v = strtol(&encDictStr[vPos + 2], &endPtr, 10);
    }

    // Parse R
    size_t rPos = encDictStr.find("/R");
    if (rPos != std::string::npos) {
        char* endPtr = nullptr;
        outInfo.r = strtol(&encDictStr[rPos + 2], &endPtr, 10);
    }

    // Parse Length
    size_t lPos = encDictStr.find("/Length");
    if (lPos != std::string::npos) {
        char* endPtr = nullptr;
        outInfo.length = strtol(&encDictStr[lPos + 7], &endPtr, 10);
    } else {
        outInfo.length = (outInfo.v == 1) ? 40 : 128;
    }

    // Parse P
    size_t pPos = encDictStr.find("/P");
    if (pPos != std::string::npos) {
        char* endPtr = nullptr;
        outInfo.p = strtoll(&encDictStr[pPos + 2], &endPtr, 10);
    }

    // Parse EncryptMetadata
    size_t emPos = encDictStr.find("/EncryptMetadata");
    if (emPos != std::string::npos) {
        size_t checkPos = emPos + 16;
        while (checkPos < encDictStr.size() && (encDictStr[checkPos] == ' ' || encDictStr[checkPos] == '\t')) checkPos++;
        if (encDictStr.compare(checkPos, 5, "false") == 0) {
            outInfo.encryptMetadata = false;
        }
    }

    // Check AES
    if (encDictStr.find("/AESV2") != std::string::npos || encDictStr.find("/AESV3") != std::string::npos) {
        outInfo.isAES = true;
    }
    if (outInfo.v == 4 && outInfo.r == 4) {
        outInfo.isAES = true;
    }
    if (outInfo.v == 5 || outInfo.r >= 5) {
        outInfo.isAES = true;
        outInfo.length = 256;
    }

    // Parse O
    size_t oPos = encDictStr.find("/O");
    if (oPos != std::string::npos && (oPos + 2 < encDictStr.size()) && (encDictStr[oPos + 2] == ' ' || encDictStr[oPos + 2] == '<' || encDictStr[oPos + 2] == '(')) {
        size_t endO = 0;
        size_t hexStart = encDictStr.find('<', oPos);
        size_t strStart = encDictStr.find('(', oPos);
        if (hexStart != std::string::npos && (strStart == std::string::npos || hexStart < strStart)) {
            std::string oHex = ExtractStringBetween(encDictStr, hexStart, '<', '>', endO);
            outInfo.o = ParseHexBytes(oHex);
        } else if (strStart != std::string::npos) {
            std::string oStr = ExtractStringBetween(encDictStr, strStart, '(', ')', endO);
            outInfo.o.assign(oStr.begin(), oStr.end());
        }
    }

    // Parse U
    size_t uPos = encDictStr.find("/U");
    if (uPos != std::string::npos && (uPos + 2 < encDictStr.size()) && (encDictStr[uPos + 2] == ' ' || encDictStr[uPos + 2] == '<' || encDictStr[uPos + 2] == '(')) {
        size_t endU = 0;
        size_t hexStart = encDictStr.find('<', uPos);
        size_t strStart = encDictStr.find('(', uPos);
        if (hexStart != std::string::npos && (strStart == std::string::npos || hexStart < strStart)) {
            std::string uHex = ExtractStringBetween(encDictStr, hexStart, '<', '>', endU);
            outInfo.u = ParseHexBytes(uHex);
        } else if (strStart != std::string::npos) {
            std::string uStr = ExtractStringBetween(encDictStr, strStart, '(', ')', endU);
            outInfo.u.assign(uStr.begin(), uStr.end());
        }
    }

    // Parse OE & UE for AES-256 (R=5)
    size_t oePos = encDictStr.find("/OE");
    if (oePos != std::string::npos) {
        size_t endOE = 0;
        std::string oeHex = ExtractStringBetween(encDictStr, oePos, '<', '>', endOE);
        outInfo.oe = ParseHexBytes(oeHex);
    }
    size_t uePos = encDictStr.find("/UE");
    if (uePos != std::string::npos) {
        size_t endUE = 0;
        std::string ueHex = ExtractStringBetween(encDictStr, uePos, '<', '>', endUE);
        outInfo.ue = ParseHexBytes(ueHex);
    }
    size_t permsPos = encDictStr.find("/Perms");
    if (permsPos != std::string::npos) {
        size_t endPerms = 0;
        std::string permsHex = ExtractStringBetween(encDictStr, permsPos, '<', '>', endPerms);
        outInfo.perms = ParseHexBytes(permsHex);
    }

    return (!outInfo.o.empty() && !outInfo.u.empty());
}

static std::vector<uint8_t> DeriveFileKey(
    const EncryptInfo& info,
    const std::string& password
) {
    if (info.r == 5 && !info.ue.empty()) {
        // Algorithm 5: AES-256 key derivation
        std::vector<uint8_t> hashBuf;
        size_t passLen = (std::min)(password.size(), (size_t)127);
        hashBuf.insert(hashBuf.end(), password.begin(), password.begin() + passLen);
        if (info.u.size() >= 40) {
            hashBuf.insert(hashBuf.end(), info.u.begin() + 32, info.u.begin() + 40);
            hashBuf.insert(hashBuf.end(), info.u.begin(), info.u.begin() + 32);
        }
        std::vector<uint8_t> k = ComputeSHA256(hashBuf.data(), hashBuf.size());

        uint8_t zeroIv[16] = { 0 };
        std::vector<uint8_t> fek;
        if (AesCbcDecrypt(k.data(), 32, zeroIv, info.ue.data(), info.ue.size(), fek)) {
            if (fek.size() == 32) return fek;
        }
    }

    // Algorithm 2: Standard MD5-based key derivation (R=2, R=3, R=4)
    uint8_t paddedPass[32];
    size_t passLen = (std::min)(password.size(), (size_t)32);
    memcpy(paddedPass, password.data(), passLen);
    if (passLen < 32) {
        memcpy(paddedPass + passLen, s_passwordPadding, 32 - passLen);
    }

    std::vector<uint8_t> hashBuf;
    hashBuf.insert(hashBuf.end(), paddedPass, paddedPass + 32);
    hashBuf.insert(hashBuf.end(), info.o.begin(), info.o.begin() + (std::min)((size_t)32, info.o.size()));

    uint32_t pVal = static_cast<uint32_t>(info.p);
    hashBuf.push_back(static_cast<uint8_t>(pVal & 0xFF));
    hashBuf.push_back(static_cast<uint8_t>((pVal >> 8) & 0xFF));
    hashBuf.push_back(static_cast<uint8_t>((pVal >> 16) & 0xFF));
    hashBuf.push_back(static_cast<uint8_t>((pVal >> 24) & 0xFF));

    hashBuf.insert(hashBuf.end(), info.fileId.begin(), info.fileId.end());

    if (info.r >= 4 && !info.encryptMetadata) {
        hashBuf.push_back(0xFF); hashBuf.push_back(0xFF); hashBuf.push_back(0xFF); hashBuf.push_back(0xFF);
    }

    std::vector<uint8_t> digest = ComputeMD5(hashBuf.data(), hashBuf.size());

    size_t keyBytes = info.length / 8;
    if (keyBytes == 0) keyBytes = 16;
    if (keyBytes > 16 && info.r <= 4) keyBytes = 16;

    if (info.r >= 3) {
        for (int i = 0; i < 50; ++i) {
            digest = ComputeMD5(digest.data(), keyBytes);
        }
    }

    return std::vector<uint8_t>(digest.begin(), digest.begin() + keyBytes);
}

static std::vector<uint8_t> DeriveObjectKey(
    const std::vector<uint8_t>& fek,
    uint32_t objNum,
    uint32_t genNum,
    bool isAES
) {
    if (fek.size() == 32 && isAES) {
        // AES-256 uses FEK directly
        return fek;
    }

    std::vector<uint8_t> objHashBuf;
    objHashBuf.insert(objHashBuf.end(), fek.begin(), fek.end());
    objHashBuf.push_back(static_cast<uint8_t>(objNum & 0xFF));
    objHashBuf.push_back(static_cast<uint8_t>((objNum >> 8) & 0xFF));
    objHashBuf.push_back(static_cast<uint8_t>((objNum >> 16) & 0xFF));
    objHashBuf.push_back(static_cast<uint8_t>(genNum & 0xFF));
    objHashBuf.push_back(static_cast<uint8_t>((genNum >> 8) & 0xFF));

    if (isAES) {
        // Append 4-byte salt "sAlT"
        objHashBuf.push_back(0x73);
        objHashBuf.push_back(0x41);
        objHashBuf.push_back(0x6c);
        objHashBuf.push_back(0x54);
    }

    std::vector<uint8_t> digest = ComputeMD5(objHashBuf.data(), objHashBuf.size());
    size_t keyLen = (std::min)(fek.size() + 5, (size_t)16);
    return std::vector<uint8_t>(digest.begin(), digest.begin() + keyLen);
}

// ============================================================================
// Main Decryption Implementation
// ============================================================================

PdfDecryptResult PdfDecryptor::DecryptAndSave(
    const std::wstring& srcPath,
    const std::wstring& dstPath,
    const std::wstring& password,
    std::function<void(float progress, const std::wstring& status)> progressCallback
) {
    PdfDecryptResult result;
    result.success = false;

    // Read source file into memory
    std::ifstream inFile(srcPath, std::ios::binary);
    if (!inFile.is_open()) {
        result.errorMessage = L"Unable to open source file: " + srcPath;
        return result;
    }

    inFile.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(inFile.tellg());
    inFile.seekg(0, std::ios::beg);

    if (fileSize > 2ULL * 1024ULL * 1024ULL * 1024ULL) {
        result.errorMessage = L"File exceeds 2 GB size limit";
        return result;
    }

    std::string fileContent;
    fileContent.resize(fileSize);
    inFile.read(&fileContent[0], fileSize);
    inFile.close();

    if (progressCallback) progressCallback(0.1f, L"Parsing encryption metadata...");

    EncryptInfo info;
    if (!ParseEncryptDict(fileContent, info)) {
        result.errorMessage = L"Failed to parse PDF /Encrypt dictionary";
        return result;
    }

    // Convert password wstring to utf8 string
    std::string passUtf8 = StringUtils::ToUtf8(password);

    std::vector<uint8_t> fek = DeriveFileKey(info, passUtf8);
    if (fek.empty()) {
        result.errorMessage = L"Failed to derive File Encryption Key";
        return result;
    }

    if (progressCallback) progressCallback(0.3f, L"Decrypting document objects...");

    // Object scan & rewrite
    struct PdfObjectEntry {
        uint32_t objNum = 0;
        uint32_t genNum = 0;
        size_t startPos = 0;
        size_t endPos = 0;
        bool hasStream = false;
        size_t streamStart = 0;
        size_t streamEnd = 0;
    };

    std::vector<PdfObjectEntry> objects;
    size_t searchPos = 0;
    while (searchPos < fileContent.size()) {
        size_t objPos = fileContent.find(" obj", searchPos);
        if (objPos == std::string::npos) break;

        // Trace back to find objNum and genNum
        size_t lineStart = objPos;
        while (lineStart > 0 && fileContent[lineStart - 1] != '\n' && fileContent[lineStart - 1] != '\r') {
            lineStart--;
        }

        std::string header = fileContent.substr(lineStart, objPos + 4 - lineStart);
        uint32_t oNum = 0, gNum = 0;
        char tag[16] = { 0 };
        if (sscanf_s(header.c_str(), "%u %u %15s", &oNum, &gNum, tag, (unsigned)_countof(tag)) >= 2 && strcmp(tag, "obj") == 0) {
            size_t endObj = fileContent.find("endobj", objPos + 4);
            if (endObj != std::string::npos) {
                PdfObjectEntry entry;
                entry.objNum = oNum;
                entry.genNum = gNum;
                entry.startPos = lineStart;
                entry.endPos = endObj + 6;

                // Check for stream
                size_t streamKw = fileContent.find("stream", objPos + 4);
                if (streamKw != std::string::npos && streamKw < endObj) {
                    size_t sData = streamKw + 6;
                    if (sData < endObj && fileContent[sData] == '\r') sData++;
                    if (sData < endObj && fileContent[sData] == '\n') sData++;

                    size_t endStream = fileContent.find("endstream", sData);
                    if (endStream != std::string::npos && endStream <= endObj) {
                        entry.hasStream = true;
                        entry.streamStart = sData;
                        entry.streamEnd = endStream;
                    }
                }

                objects.push_back(entry);
                searchPos = endObj + 6;
                continue;
            }
        }
        searchPos = objPos + 4;
    }

    if (objects.empty()) {
        result.errorMessage = L"No PDF objects found to decrypt";
        return result;
    }

    // Output buffer
    std::string outPdf;
    outPdf.reserve(fileSize + 4096);

    // Write original PDF header up to first object
    size_t firstObjPos = objects.front().startPos;
    outPdf.append(fileContent.substr(0, firstObjPos));

    std::map<uint32_t, size_t> newXrefOffsets;

    for (size_t i = 0; i < objects.size(); ++i) {
        const auto& obj = objects[i];

        if (progressCallback && i % 10 == 0) {
            float p = 0.3f + 0.5f * (static_cast<float>(i) / objects.size());
            progressCallback(p, L"Decrypting object " + std::to_wstring(obj.objNum) + L"...");
        }

        // Omit the /Encrypt dictionary object
        if (obj.objNum == info.encryptObjNum) {
            continue;
        }

        newXrefOffsets[obj.objNum] = outPdf.size();

        std::vector<uint8_t> objKey = DeriveObjectKey(fek, obj.objNum, obj.genNum, info.isAES);

        if (obj.hasStream) {
            // Decrypt stream data
            const uint8_t* rawStream = reinterpret_cast<const uint8_t*>(&fileContent[obj.streamStart]);
            size_t rawLen = obj.streamEnd - obj.streamStart;

            std::vector<uint8_t> decryptedData;
            if (info.isAES) {
                if (rawLen >= 16) {
                    const uint8_t* iv = rawStream;
                    const uint8_t* cipher = rawStream + 16;
                    size_t cipherLen = rawLen - 16;
                    AesCbcDecrypt(objKey.data(), objKey.size(), iv, cipher, cipherLen, decryptedData);
                }
            } else {
                decryptedData.resize(rawLen);
                Rc4Crypt(objKey.data(), objKey.size(), rawStream, rawLen, decryptedData.data());
            }

            if (decryptedData.empty() && rawLen > 0) {
                // Keep raw stream if decryption produced 0 bytes as safety fallback
                decryptedData.assign(rawStream, rawStream + rawLen);
            }

            // Dict portion before stream
            std::string dictPart = fileContent.substr(obj.startPos, (obj.streamStart - 6) - obj.startPos);
            // If stream had CRLF, trim properly
            size_t sIdx = dictPart.rfind("stream");
            if (sIdx != std::string::npos) {
                dictPart = dictPart.substr(0, sIdx);
            }

            // Update /Length
            size_t lenIdx = dictPart.find("/Length");
            if (lenIdx != std::string::npos) {
                size_t valStart = lenIdx + 7;
                while (valStart < dictPart.size() && (dictPart[valStart] == ' ' || dictPart[valStart] == '\t')) valStart++;
                size_t valEnd = valStart;
                while (valEnd < dictPart.size() && dictPart[valEnd] >= '0' && dictPart[valEnd] <= '9') valEnd++;
                if (valEnd > valStart) {
                    dictPart = dictPart.substr(0, valStart) + std::to_string(decryptedData.size()) + dictPart.substr(valEnd);
                }
            }

            outPdf.append(dictPart);
            outPdf.append("stream\r\n");
            outPdf.append(reinterpret_cast<const char*>(decryptedData.data()), decryptedData.size());
            outPdf.append("\r\nendstream\r\nendobj\r\n");

            result.streamsDecrypted++;
            result.objectsDecrypted++;
        } else {
            // Non-stream object: check for literal or hex strings to decrypt (SEC-12)
            std::string objText = fileContent.substr(obj.startPos, obj.endPos - obj.startPos);
            std::string decText = DecryptNonStreamStrings(objText, objKey, info.isAES);
            outPdf.append(decText);
            outPdf.append("\r\n");
            result.objectsDecrypted++;
        }
    }

    if (progressCallback) progressCallback(0.85f, L"Writing unencrypted cross-reference table...");

    // Build new xref section
    size_t startXrefPos = outPdf.size();
    uint32_t maxObjNum = newXrefOffsets.empty() ? 0 : newXrefOffsets.rbegin()->first;
    if (maxObjNum > 1000000 || (newXrefOffsets.size() > 0 && maxObjNum > newXrefOffsets.size() * 10 + 1024)) {
        result.errorMessage = L"Sparse object ID distribution exceeds safe limits";
        return result;
    }
    uint32_t xrefSize = maxObjNum + 1;

    outPdf.append("xref\r\n");
    outPdf.append("0 " + std::to_string(xrefSize) + "\r\n");

    // Object 0 (head of free list)
    char lineBuf[32];
    sprintf_s(lineBuf, "%010u 65535 f \r\n", 0);
    outPdf.append(lineBuf);

    for (uint32_t o = 1; o < xrefSize; ++o) {
        auto it = newXrefOffsets.find(o);
        if (it != newXrefOffsets.end()) {
            sprintf_s(lineBuf, "%010zu 00000 n \r\n", it->second);
            outPdf.append(lineBuf);
        } else {
            sprintf_s(lineBuf, "%010u 00000 f \r\n", 0);
            outPdf.append(lineBuf);
        }
    }

    // Build trailer
    size_t trailerPos = fileContent.rfind("trailer");
    std::string trailerDict;
    if (trailerPos != std::string::npos) {
        size_t endTrailer = fileContent.find("startxref", trailerPos);
        if (endTrailer != std::string::npos) {
            trailerDict = fileContent.substr(trailerPos, endTrailer - trailerPos);
        }
    }

    if (trailerDict.empty()) {
        trailerDict = "trailer\r\n<< /Size " + std::to_string(xrefSize) + " >>\r\n";
    }

    // Remove /Encrypt entry from trailer
    size_t encTrPos = trailerDict.find("/Encrypt");
    if (encTrPos != std::string::npos) {
        size_t encTrEnd = encTrPos + 8;
        while (encTrEnd < trailerDict.size() && (trailerDict[encTrEnd] == ' ' || trailerDict[encTrEnd] == '\t')) encTrEnd++;
        while (encTrEnd < trailerDict.size() && trailerDict[encTrEnd] != '/' && trailerDict[encTrEnd] != '>' && trailerDict[encTrEnd] != '\r' && trailerDict[encTrEnd] != '\n') {
            encTrEnd++;
        }
        trailerDict = trailerDict.substr(0, encTrPos) + trailerDict.substr(encTrEnd);
    }

    // Update /Size in trailer
    size_t sizePos = trailerDict.find("/Size");
    if (sizePos != std::string::npos) {
        size_t valS = sizePos + 5;
        while (valS < trailerDict.size() && (trailerDict[valS] == ' ' || trailerDict[valS] == '\t')) valS++;
        size_t valE = valS;
        while (valE < trailerDict.size() && trailerDict[valE] >= '0' && trailerDict[valE] <= '9') valE++;
        if (valE > valS) {
            trailerDict = trailerDict.substr(0, valS) + std::to_string(xrefSize) + trailerDict.substr(valE);
        }
    }

    outPdf.append(trailerDict);
    if (outPdf.back() != '\n') outPdf.append("\r\n");
    outPdf.append("startxref\r\n");
    outPdf.append(std::to_string(startXrefPos) + "\r\n");
    outPdf.append("%%EOF\r\n");

    if (progressCallback) progressCallback(0.95f, L"Saving unencrypted PDF file...");

    bool isOverwrite = (_wcsicmp(srcPath.c_str(), dstPath.c_str()) == 0);
    std::wstring writeTarget = isOverwrite ? MakeUniqueTempPath(dstPath) : dstPath;

    // Write to destination file (or temporary swap file if overwriting in-place)
    std::ofstream outFile(writeTarget, std::ios::binary);
    if (!outFile.is_open()) {
        result.errorMessage = L"Failed to create destination file: " + writeTarget;
        return result;
    }
    outFile.write(outPdf.data(), outPdf.size());
    outFile.close();

    // Verify written file is unencrypted
    if (PdfParser::QuickCheckFileEncrypted(writeTarget)) {
        if (isOverwrite) DeleteFileW(writeTarget.c_str());
        result.errorMessage = L"Resulting PDF still contains /Encrypt signature";
        return result;
    }

    if (isOverwrite) {
        if (!ReplaceFileW(dstPath.c_str(), writeTarget.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            if (!MoveFileExW(writeTarget.c_str(), dstPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(writeTarget.c_str());
                result.errorMessage = L"Failed to overwrite destination file with decrypted copy.";
                result.success = false;
                return result;
            }
        }
    }

    if (progressCallback) progressCallback(1.0f, L"Document successfully saved without password!");

    result.success = true;
    return result;
}

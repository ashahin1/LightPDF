#include "dictionary_engine.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cwctype>

DictionaryEngine::DictionaryEngine() = default;

DictionaryEngine::~DictionaryEngine() {
    Close();
}

void DictionaryEngine::Close() {
    if (m_mappedData) {
        UnmapViewOfFile(m_mappedData);
        m_mappedData = nullptr;
    }
    if (m_mapHandle) {
        CloseHandle(m_mapHandle);
        m_mapHandle = nullptr;
    }
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
    }
    m_entryCount = 0;
    m_indexRecords = nullptr;
    m_textPool = nullptr;
    m_fileSize = 0;
    m_initialized = false;
}

std::wstring DictionaryEngine::FindDictionaryFile() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring pathStr(exePath);
    size_t lastSlash = pathStr.find_last_of(L"\\/");
    std::wstring exeDir = (lastSlash != std::wstring::npos) ? pathStr.substr(0, lastSlash) : L".";

    // 1. Check exeDir\dict\en-ar.dat
    std::wstring cand1 = exeDir + L"\\dict\\en-ar.dat";
    if (GetFileAttributesW(cand1.c_str()) != INVALID_FILE_ATTRIBUTES) return cand1;

    // 2. Check exeDir\..\dict\en-ar.dat (when running from bin/)
    std::wstring cand2 = exeDir + L"\\..\\dict\\en-ar.dat";
    if (GetFileAttributesW(cand2.c_str()) != INVALID_FILE_ATTRIBUTES) return cand2;

    // 3. Check current working directory dict\en-ar.dat
    std::wstring cand3 = L"dict\\en-ar.dat";
    if (GetFileAttributesW(cand3.c_str()) != INVALID_FILE_ATTRIBUTES) return cand3;

    // 4. Check en-ar.dat in exe directory
    std::wstring cand4 = exeDir + L"\\en-ar.dat";
    if (GetFileAttributesW(cand4.c_str()) != INVALID_FILE_ATTRIBUTES) return cand4;

    return L"";
}

std::wstring DictionaryEngine::FindUserTermsFile() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring pathStr(exePath);
    size_t lastSlash = pathStr.find_last_of(L"\\/");
    std::wstring exeDir = (lastSlash != std::wstring::npos) ? pathStr.substr(0, lastSlash) : L".";

    // 1. Check exeDir\dict\user_terms.txt
    std::wstring cand1 = exeDir + L"\\dict\\user_terms.txt";
    if (GetFileAttributesW(cand1.c_str()) != INVALID_FILE_ATTRIBUTES) return cand1;

    // 2. Check exeDir\..\dict\user_terms.txt
    std::wstring cand2 = exeDir + L"\\..\\dict\\user_terms.txt";
    if (GetFileAttributesW(cand2.c_str()) != INVALID_FILE_ATTRIBUTES) return cand2;

    // 3. Check current working directory dict\user_terms.txt
    std::wstring cand3 = L"dict\\user_terms.txt";
    if (GetFileAttributesW(cand3.c_str()) != INVALID_FILE_ATTRIBUTES) return cand3;

    return L"";
}

void DictionaryEngine::LoadUserTerms(const std::wstring& userTermsPath) {
    if (userTermsPath.empty()) return;
    std::ifstream file(userTermsPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Strip BOM and whitespace
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        // Trim whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            start++;
        }
        if (start > 0) line = line.substr(start);
        if (line.empty()) continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string rawTerm = line.substr(0, eqPos);
        std::string rawVal = line.substr(eqPos + 1);

        // Trim term and val
        auto trimStr = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        trimStr(rawTerm);
        trimStr(rawVal);
        if (rawTerm.empty() || rawVal.empty()) continue;

        std::wstring wTerm = Utf8ToUtf16(rawTerm.c_str(), rawTerm.size());
        std::wstring wVal = Utf8ToUtf16(rawVal.c_str(), rawVal.size());

        std::wstring lowerKey = wTerm;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::towlower);

        DictionaryResult res;
        res.word = wTerm;
        res.definition = wVal;
        res.category = DictionaryCategory::AcademicAccreditation; // Default custom user tag

        m_customUserTerms[lowerKey] = std::move(res);
    }
}

bool DictionaryEngine::Initialize(const std::wstring& explicitPath) {
    if (m_initialized && m_mappedData) return true;
    m_initialized = true;

    std::wstring path = explicitPath.empty() ? FindDictionaryFile() : explicitPath;
    if (path.empty()) return false;

    m_fileHandle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_fileHandle == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(m_fileHandle, &size) || size.QuadPart < sizeof(DictHeader)) {
        Close();
        return false;
    }
    m_fileSize = (uint64_t)size.QuadPart;

    m_mapHandle = CreateFileMappingW(m_fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m_mapHandle) {
        Close();
        return false;
    }

    m_mappedData = (const uint8_t*)MapViewOfFile(m_mapHandle, FILE_MAP_READ, 0, 0, 0);
    if (!m_mappedData) {
        Close();
        return false;
    }

    const auto* header = (const DictHeader*)m_mappedData;
    if (memcmp(header->magic, "LPDICT01", 8) != 0) {
        Close();
        return false;
    }

    m_entryCount = header->entryCount;
    if (m_entryCount == 0 ||
        header->indexOffset + (uint64_t)m_entryCount * sizeof(DictRecord) > m_fileSize ||
        header->textOffset > m_fileSize) {
        Close();
        return false;
    }

    m_indexRecords = (const DictRecord*)(m_mappedData + header->indexOffset);
    m_textPool = (const char*)(m_mappedData + header->textOffset);

    // Load custom user terms
    LoadUserTerms(FindUserTermsFile());

    m_initialized = true;
    return true;
}

std::wstring DictionaryEngine::CleanQuery(const std::wstring& rawQuery) const {
    std::wstring result = rawQuery;

    // Strip leading punctuation and whitespace
    while (!result.empty()) {
        wchar_t c = result.front();
        if (std::iswspace(c) || c == L'"' || c == L'\'' || c == L'(' || c == L'[' || c == L'{' || c == L'«' || c == L'“' || c == L'`') {
            result.erase(result.begin());
        } else {
            break;
        }
    }

    // Strip trailing punctuation and whitespace
    while (!result.empty()) {
        wchar_t c = result.back();
        if (std::iswspace(c) || c == L'"' || c == L'\'' || c == L')' || c == L']' || c == L'}' ||
            c == L',' || c == L'.' || c == L';' || c == L':' || c == L'!' || c == L'?' ||
            c == L'»' || c == L'”' || c == L'\'') {
            result.pop_back();
        } else {
            break;
        }
    }

    return result;
}

std::string DictionaryEngine::Utf16ToUtf8(const std::wstring& wstr) const {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], len, nullptr, nullptr);
    return str;
}

std::wstring DictionaryEngine::Utf8ToUtf16(const char* utf8, size_t len) const {
    if (!utf8 || len == 0) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, &wstr[0], wlen);
    return wstr;
}

bool DictionaryEngine::BinarySearch(const std::string& targetLower, DictionaryResult& outResult) {
    if (!m_indexRecords || !m_textPool || m_entryCount == 0) return false;

    int left = 0;
    int right = (int)m_entryCount - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        const auto& rec = m_indexRecords[mid];

        const char* wordPtr = m_textPool + rec.wordOffset;
        size_t wordLen = rec.wordLen;

        // Compare case-insensitively using standard lowercase ASCII comparison
        std::string midWordLower(wordPtr, wordLen);
        std::transform(midWordLower.begin(), midWordLower.end(), midWordLower.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });

        int cmp = targetLower.compare(midWordLower);
        if (cmp == 0) {
            outResult.word = Utf8ToUtf16(wordPtr, wordLen);
            const char* defPtr = m_textPool + rec.defOffset;
            outResult.definition = Utf8ToUtf16(defPtr, rec.defLen);
            outResult.category = (DictionaryCategory)rec.category;
            return true;
        } else if (cmp < 0) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    return false;
}

bool DictionaryEngine::Lookup(const std::wstring& rawQuery, DictionaryResult& outResult) {
    if (!m_initialized) {
        Initialize();
    }

    std::wstring cleaned = CleanQuery(rawQuery);
    if (cleaned.empty()) return false;

    std::wstring lowerKey = cleaned;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::towlower);

    // 1. Check custom user terms first (immediate priority)
    auto itUser = m_customUserTerms.find(lowerKey);
    if (itUser != m_customUserTerms.end()) {
        outResult = itUser->second;
        return true;
    }

    if (!IsLoaded()) return false;

    std::string targetUtf8 = Utf16ToUtf8(lowerKey);

    // 2. Exact match in binary dictionary
    if (BinarySearch(targetUtf8, outResult)) {
        return true;
    }

    // 2b. Hyphen / Space interchangeability check
    if (lowerKey.find(L' ') != std::wstring::npos) {
        std::wstring hyphenated = lowerKey;
        std::replace(hyphenated.begin(), hyphenated.end(), L' ', L'-');
        if (BinarySearch(Utf16ToUtf8(hyphenated), outResult)) return true;
    } else if (lowerKey.find(L'-') != std::wstring::npos) {
        std::wstring spaced = lowerKey;
        std::replace(spaced.begin(), spaced.end(), L'-', L' ');
        if (BinarySearch(Utf16ToUtf8(spaced), outResult)) return true;
    }

    // 3. Multi-word phrase plural handling
    if (lowerKey.find(L' ') != std::wstring::npos) {
        if (lowerKey.ends_with(L"s")) {
            std::wstring singular = lowerKey.substr(0, lowerKey.size() - 1);
            if (BinarySearch(Utf16ToUtf8(singular), outResult)) return true;
            std::replace(singular.begin(), singular.end(), L' ', L'-');
            if (BinarySearch(Utf16ToUtf8(singular), outResult)) return true;
        }
        if (lowerKey.ends_with(L"es")) {
            std::wstring singular = lowerKey.substr(0, lowerKey.size() - 2);
            if (BinarySearch(Utf16ToUtf8(singular), outResult)) return true;
            std::replace(singular.begin(), singular.end(), L' ', L'-');
            if (BinarySearch(Utf16ToUtf8(singular), outResult)) return true;
        }
        return false;
    }

    // 4. Single-word morphological stemming fallbacks:
    // (a) Possessive: term's or term’s -> term
    if (lowerKey.size() > 2 && (lowerKey.ends_with(L"'s") || lowerKey.ends_with(L"’s"))) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 2));
        if (BinarySearch(stem, outResult)) return true;
    }

    // (b) Plural: -ies -> -y (e.g. technologies -> technology)
    if (lowerKey.size() > 4 && lowerKey.ends_with(L"ies")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 3) + L"y");
        if (BinarySearch(stem, outResult)) return true;
    }

    // (c) Plural: -ices -> -ix (e.g. matrices -> matrix)
    if (lowerKey.size() > 5 && lowerKey.ends_with(L"ices")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 4) + L"ix");
        if (BinarySearch(stem, outResult)) return true;
    }

    // (d) Plural: -es -> strip es or s (e.g. processes -> process, branches -> branch)
    if (lowerKey.size() > 4 && lowerKey.ends_with(L"es")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 2));
        if (BinarySearch(stem, outResult)) return true;
        stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 1));
        if (BinarySearch(stem, outResult)) return true;
    }

    // (e) Plural: -s (e.g. pipelines -> pipeline, registers -> register)
    if (lowerKey.size() > 3 && lowerKey.ends_with(L"s")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 1));
        if (BinarySearch(stem, outResult)) return true;
    }

    // (f) Verb: -ing (e.g. pipelining -> pipeline or route)
    if (lowerKey.size() > 5 && lowerKey.ends_with(L"ing")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 3));
        if (BinarySearch(stem, outResult)) return true;
        stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 3) + L"e");
        if (BinarySearch(stem, outResult)) return true;
    }

    // (g) Verb: -ed (e.g. compiled -> compile, evaluated -> evaluate)
    if (lowerKey.size() > 4 && lowerKey.ends_with(L"ed")) {
        std::string stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 2));
        if (BinarySearch(stem, outResult)) return true;
        stem = Utf16ToUtf8(lowerKey.substr(0, lowerKey.size() - 1));
        if (BinarySearch(stem, outResult)) return true;
    }

    return false;
}

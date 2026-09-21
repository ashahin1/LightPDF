#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

enum class DictionaryCategory : uint16_t {
    General = 0,
    Architecture = 1,
    NetworksIoT = 2,
    AIMachineLearning = 3,
    Cybersecurity = 4,
    AcademicAccreditation = 5,
    QualityAssurance = 6,
    AlgorithmsOptimization = 7
};

struct DictionaryResult {
    std::wstring word;
    std::wstring definition;
    DictionaryCategory category = DictionaryCategory::General;

    std::wstring GetCategoryName() const {
        switch (category) {
        case DictionaryCategory::Architecture:
            return L"Architecture & Hardware";
        case DictionaryCategory::NetworksIoT:
            return L"Networks, IoT & WSN";
        case DictionaryCategory::AIMachineLearning:
            return L"AI, ML & Vision";
        case DictionaryCategory::Cybersecurity:
            return L"Cybersecurity & Crypto";
        case DictionaryCategory::AcademicAccreditation:
            return L"Academic · ABET / NCAAA";
        case DictionaryCategory::QualityAssurance:
            return L"Quality Assurance & Testing";
        case DictionaryCategory::AlgorithmsOptimization:
            return L"Algorithms & Optimization";
        default:
            return L"General Lexicon";
        }
    }
};

class DictionaryEngine {
public:
    DictionaryEngine();
    ~DictionaryEngine();

    // Initializes dictionary file. If path is empty, searches default locations.
    // Returns false if file not found (graceful degradation, zero crash).
    bool Initialize(const std::wstring& explicitPath = L"");
    void Close();

    bool IsLoaded() const { return m_mappedData != nullptr && m_entryCount > 0; }
    uint32_t GetEntryCount() const { return m_entryCount; }

    // Looks up a word or phrase with automatic normalization and stemming.
    bool Lookup(const std::wstring& query, DictionaryResult& outResult);

    // Static helper to find standard dictionary file path relative to executable or working dir
    static std::wstring FindDictionaryFile();
    static std::wstring FindUserTermsFile();

private:
#pragma pack(push, 1)
    struct DictHeader {
        char magic[8];          // 'LPDICT01'
        uint32_t entryCount;
        uint32_t indexOffset;
        uint32_t textOffset;
        uint8_t reserved[12];
    };

    struct DictRecord {
        uint32_t wordOffset;
        uint16_t wordLen;
        uint16_t category;
        uint32_t defOffset;
        uint16_t defLen;
        uint16_t reserved;
    };
#pragma pack(pop)

    bool BinarySearch(const std::string& targetLower, DictionaryResult& outResult);
    void LoadUserTerms(const std::wstring& userTermsPath);
    std::wstring CleanQuery(const std::wstring& rawQuery) const;
    std::string Utf16ToUtf8(const std::wstring& wstr) const;
    std::wstring Utf8ToUtf16(const char* utf8, size_t len) const;

    HANDLE m_fileHandle = INVALID_HANDLE_VALUE;
    HANDLE m_mapHandle = nullptr;
    const uint8_t* m_mappedData = nullptr;
    uint64_t m_fileSize = 0;

    uint32_t m_entryCount = 0;
    const DictRecord* m_indexRecords = nullptr;
    const char* m_textPool = nullptr;

    // User custom terms loaded from user_terms.txt
    std::unordered_map<std::wstring, DictionaryResult> m_customUserTerms;
    bool m_initialized = false;
};

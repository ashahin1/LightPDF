/**
 * @file dictionary_engine.hpp
 * @brief Zero-overhead memory-mapped offline dictionary and translation engine.
 */

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

/// @brief Technical domain categories for dictionary entries.
enum class DictionaryCategory : uint16_t {
    General = 0,                ///< General computing and general lexicon
    Architecture = 1,           ///< Computer architecture and hardware systems
    NetworksIoT = 2,            ///< Computer networks, IoT, and wireless sensor networks
    AIMachineLearning = 3,      ///< Artificial intelligence, ML, and computer vision
    Cybersecurity = 4,          ///< Cybersecurity, cryptography, and network defense
    AcademicAccreditation = 5,  ///< Academic terminology (ABET / NCAAA standards)
    QualityAssurance = 6,       ///< Software testing and quality assurance
    AlgorithmsOptimization = 7  ///< Algorithms, data structures, and computational optimization
};

/// @brief Represents a successful dictionary query lookup result.
struct DictionaryResult {
    std::wstring word;                                          ///< Target word or stemmed form
    std::wstring definition;                                    ///< Arabic definition / translation text
    DictionaryCategory category = DictionaryCategory::General;  ///< Technical domain classification

    /// @brief Human-readable localized category display label.
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

/**
 * @class DictionaryEngine
 * @brief High-speed binary search dictionary engine backed by memory-mapped storage.
 */
class DictionaryEngine {
public:
    DictionaryEngine();
    ~DictionaryEngine();

    /**
     * @brief Initializes dictionary file.
     * @details If path is empty, searches default locations (relative to executable and working dir).
     *          Provides graceful degradation: returns false without crashing if dictionary file is missing.
     * @param explicitPath Optional explicit file path to dictionary binary (.dat).
     * @return true if dictionary mapped successfully, false otherwise.
     */
    bool Initialize(const std::wstring& explicitPath = L"");

    /**
     * @brief Closes memory mapping and file handles.
     */
    void Close();

    /// @brief Returns whether a dictionary is actively loaded and mapped.
    bool IsLoaded() const { return m_mappedData != nullptr && m_entryCount > 0; }

    /// @brief Total number of indexed entries in the active dictionary.
    uint32_t GetEntryCount() const { return m_entryCount; }

    /**
     * @brief Looks up a word or phrase with automatic normalization, stemming, and fallback.
     * @param query Word or phrase to look up.
     * @param outResult Populated with definition, category, and matched term if found.
     * @return true if a definition was found; false otherwise.
     */
    bool Lookup(const std::wstring& query, DictionaryResult& outResult);

    /// @brief Locates the standard dictionary binary file relative to executable or working directory.
    static std::wstring FindDictionaryFile();

    /// @brief Locates custom user terms file (user_terms.txt) if available.
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

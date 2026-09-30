/**
 * @file tts_engine.cpp
 * @brief Implementation of native Win32 Speech API (SAPI) COM text-to-speech engine.
 */

#include "tts_engine.hpp"
#include <algorithm>
#include <cwchar>

TtsEngine::~TtsEngine() {
    Shutdown();
}

bool TtsEngine::Init(HWND hwndNotify, UINT msgNotify) {
    if (m_pVoice) return true;

    HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
    if (FAILED(hr) || !m_pVoice) {
        m_pVoice = nullptr;
        return false;
    }

    // Set event interest for word boundaries and stream completion
    ULONGLONG interest = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM);
    m_pVoice->SetInterest(interest, interest);

    if (hwndNotify) {
        m_pVoice->SetNotifyWindowMessage(hwndNotify, msgNotify, 0, 0);
    }

    EnumerateVoices();

    // Default to the first available voice if any
    if (!m_voices.empty()) {
        SelectVoice(0);
    }

    m_paused = false;
    return true;
}

void TtsEngine::Shutdown() {
    if (m_pVoice) {
        Stop();
        m_pVoice->Release();
        m_pVoice = nullptr;
    }

    for (auto* pToken : m_voiceTokens) {
        if (pToken) pToken->Release();
    }
    m_voiceTokens.clear();
    m_voices.clear();
    m_currentVoiceIndex = 0;
    m_paused = false;
}

void TtsEngine::EnumerateVoices() {
    for (auto* pToken : m_voiceTokens) {
        if (pToken) pToken->Release();
    }
    m_voiceTokens.clear();
    m_voices.clear();

    // 1. Enumerate modern OneCore voices first (preferred quality and Arabic availability)
    EnumerateFromRegistry(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices");

    // 2. Enumerate classic SAPI desktop voices
    EnumerateFromRegistry(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices");
}

void TtsEngine::EnumerateFromRegistry(const wchar_t* regPath) {
    ISpObjectTokenCategory* pCat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, IID_ISpObjectTokenCategory, (void**)&pCat);
    if (FAILED(hr) || !pCat) return;

    hr = pCat->SetId(regPath, FALSE);
    if (FAILED(hr)) {
        pCat->Release();
        return;
    }

    IEnumSpObjectTokens* pEnum = nullptr;
    hr = pCat->EnumTokens(NULL, NULL, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        ULONG count = 0;
        if (SUCCEEDED(pEnum->GetCount(&count))) {
            for (ULONG i = 0; i < count; ++i) {
                ISpObjectToken* pToken = nullptr;
                if (SUCCEEDED(pEnum->Next(1, &pToken, NULL)) && pToken) {
                    WCHAR* pId = nullptr;
                    pToken->GetId(&pId);

                    std::wstring voiceName;
                    LANGID langId = 0;

                    ISpDataKey* pKey = nullptr;
                    if (SUCCEEDED(pToken->OpenKey(L"Attributes", &pKey)) && pKey) {
                        WCHAR* pName = nullptr;
                        if (SUCCEEDED(pKey->GetStringValue(L"Name", &pName)) && pName) {
                            voiceName = pName;
                            ::CoTaskMemFree(pName);
                        }

                        WCHAR* pLang = nullptr;
                        if (SUCCEEDED(pKey->GetStringValue(L"Language", &pLang)) && pLang) {
                            // Language is usually stored as hex string (e.g. "401", "409")
                            wchar_t* endPtr = nullptr;
                            long val = wcstol(pLang, &endPtr, 16);
                            if (val > 0) langId = (LANGID)val;
                            ::CoTaskMemFree(pLang);
                        }
                        pKey->Release();
                    }

                    if (voiceName.empty()) {
                        voiceName = pId ? pId : L"Unknown Voice";
                    }

                    // Check for duplicate names (prevent showing Microsoft David twice if in both registries)
                    bool duplicate = false;
                    for (const auto& existing : m_voices) {
                        if (_wcsicmp(existing.name.c_str(), voiceName.c_str()) == 0) {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate) {
                        TtsVoiceInfo info;
                        info.id = pId ? pId : L"";
                        info.name = voiceName;
                        info.langId = langId;
                        // PRIMARYLANGID(0x0401) == LANG_ARABIC (0x01)
                        info.isArabic = ((langId & 0xFF) == 0x01);

                        m_voices.push_back(info);
                        m_voiceTokens.push_back(pToken); // keep reference
                    } else {
                        pToken->Release();
                    }

                    if (pId) ::CoTaskMemFree(pId);
                }
            }
        }
        pEnum->Release();
    }
    pCat->Release();
}

bool TtsEngine::SelectVoice(size_t index) {
    if (!m_pVoice || index >= m_voiceTokens.size() || !m_voiceTokens[index]) {
        return false;
    }

    HRESULT hr = m_pVoice->SetVoice(m_voiceTokens[index]);
    if (SUCCEEDED(hr)) {
        m_currentVoiceIndex = index;
        return true;
    }
    return false;
}

bool TtsEngine::SelectVoiceForLanguage(bool isArabic) {
    if (m_voices.empty()) return false;

    // Check if current voice already matches the required language
    if (m_currentVoiceIndex < m_voices.size() && m_voices[m_currentVoiceIndex].isArabic == isArabic) {
        return true;
    }

    // Find the first voice matching the language
    for (size_t i = 0; i < m_voices.size(); ++i) {
        if (m_voices[i].isArabic == isArabic) {
            return SelectVoice(i);
        }
    }

    // Fallback: keep current voice
    return false;
}

bool TtsEngine::SpeakAsync(const std::wstring& text) {
    if (!m_pVoice) return false;
    m_paused = false;

    HRESULT hr = m_pVoice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    return SUCCEEDED(hr);
}

void TtsEngine::Pause() {
    if (m_pVoice && !m_paused) {
        m_pVoice->Pause();
        m_paused = true;
    }
}

void TtsEngine::Resume() {
    if (m_pVoice && m_paused) {
        m_pVoice->Resume();
        m_paused = false;
    }
}

void TtsEngine::Stop() {
    if (m_pVoice) {
        m_pVoice->Speak(NULL, SPF_PURGEBEFORESPEAK, NULL);
        if (m_paused) {
            m_pVoice->Resume();
            m_paused = false;
        }
    }
}

void TtsEngine::SetRate(int rate) {
    m_rate = std::clamp(rate, -5, 5);
    if (m_pVoice) {
        m_pVoice->SetRate(m_rate);
    }
}

std::wstring TtsEngine::GetRateLabel() const {
    switch (m_rate) {
    case -5: return L"0.5x";
    case -4: return L"0.6x";
    case -3: return L"0.7x";
    case -2: return L"0.8x";
    case -1: return L"0.9x";
    case 0:  return L"1.0x";
    case 1:  return L"1.2x";
    case 2:  return L"1.4x";
    case 3:  return L"1.6x";
    case 4:  return L"1.8x";
    case 5:  return L"2.0x";
    default: return L"1.0x";
    }
}

TtsEvent TtsEngine::ProcessEvent() {
    TtsEvent result;
    if (!m_pVoice) return result;

    SPEVENT event;
    ULONG fetched = 0;
    while (m_pVoice->GetEvents(1, &event, &fetched) == S_OK && fetched > 0) {
        if (event.eEventId == SPEI_WORD_BOUNDARY) {
            result.type = TtsEvent::WordBoundary;
            result.charOffset = (ULONG)event.lParam;
            result.charLength = (ULONG)event.wParam;
            return result;
        } else if (event.eEventId == SPEI_END_INPUT_STREAM) {
            result.type = TtsEvent::EndStream;
            return result;
        }
    }

    return result;
}

/**
 * @file tts_engine.hpp
 * @brief Native Win32 Speech API (SAPI) COM text-to-speech engine with OneCore voice support.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <sapi.h>
#include <string>
#include <vector>

/// @brief Metadata for a discovered system TTS voice.
struct TtsVoiceInfo {
    std::wstring id;        ///< Full registry token ID or path
    std::wstring name;      ///< Display name (e.g. "Microsoft Naayf", "Microsoft David")
    LANGID langId = 0;      ///< Primary language (0x0401 = Arabic, 0x0409 = English)
    bool isArabic = false;  ///< Pre-computed from langId
};

/// @brief Event data extracted from SAPI word-boundary or end-of-stream notifications.
struct TtsEvent {
    enum Type {
        None,
        WordBoundary,
        EndStream
    };

    Type type = None;
    ULONG charOffset = 0;   ///< Character offset into current spoken text chunk
    ULONG charLength = 0;   ///< Length of current word in characters
};

/**
 * @class TtsEngine
 * @brief Encapsulates Windows SAPI ISpVoice with automatic OneCore & Desktop voice discovery.
 */
class TtsEngine {
public:
    TtsEngine() = default;
    ~TtsEngine();

    TtsEngine(const TtsEngine&) = delete;
    TtsEngine& operator=(const TtsEngine&) = delete;

    /**
     * @brief Initializes COM voice on calling thread and registers notification window.
     * @param hwndNotify Window to receive WM_APP_TTS_EVENT.
     * @param msgNotify Message identifier (WM_APP_TTS_EVENT).
     * @return true if ISpVoice created and initialized successfully.
     */
    bool Init(HWND hwndNotify, UINT msgNotify);

    /// @brief Closes COM voice and frees all allocated voice tokens.
    void Shutdown();

    /// @brief Returns list of discovered voices (both Desktop and OneCore).
    const std::vector<TtsVoiceInfo>& GetVoices() const { return m_voices; }

    /// @brief Selects active voice by index into GetVoices().
    bool SelectVoice(size_t index);

    /// @brief Automatically selects optimal voice for Arabic or English.
    bool SelectVoiceForLanguage(bool isArabic);

    /// @brief Returns index of currently selected voice.
    size_t GetCurrentVoiceIndex() const { return m_currentVoiceIndex; }

    /**
     * @brief Speaks text asynchronously, interrupting any previous utterance.
     * @param text Wide string to speak.
     * @return true if speech task was queued to SAPI worker thread.
     */
    bool SpeakAsync(const std::wstring& text);

    /// @brief Pauses speech synthesis audio stream.
    void Pause();

    /// @brief Resumes paused speech synthesis audio stream.
    void Resume();

    /// @brief Stops playback immediately and clears audio queue.
    void Stop();

    /**
     * @brief Sets speech rate from -5 (0.5x) to +5 (2.0x).
     * @param rate SAPI rate adjustment.
     */
    void SetRate(int rate);

    /// @brief Returns current rate value (-5 to +5).
    int GetRate() const { return m_rate; }

    /// @brief Returns human-readable speed label (e.g. "1.0x", "1.5x").
    std::wstring GetRateLabel() const;

    /// @brief Returns whether speech is currently paused.
    bool IsPaused() const { return m_paused; }

    /// @brief Returns whether COM voice interface is valid.
    bool IsInitialized() const { return m_pVoice != nullptr; }

    /**
     * @brief Polls pending events from ISpVoice.
     * @return TtsEvent containing event type and word offsets.
     */
    TtsEvent ProcessEvent();

private:
    void EnumerateVoices();
    void EnumerateFromRegistry(const wchar_t* regPath);

    ISpVoice* m_pVoice = nullptr;
    std::vector<TtsVoiceInfo> m_voices;
    std::vector<ISpObjectToken*> m_voiceTokens;
    size_t m_currentVoiceIndex = 0;
    int m_rate = 0;
    bool m_paused = false;
};

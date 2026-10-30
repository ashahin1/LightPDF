/**
 * @file settings_controller.cpp
 * @brief Implementation of SettingsController registry persistence and validation.
 */

#include "settings_controller.hpp"

SettingsController::SettingsController(const std::wstring& regSubKey)
    : m_regSubKey(regSubKey) {
}

void SettingsController::LoadFromRegistry() {
    m_settings = AppSettings{};
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, m_regSubKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return;
    }

    auto readDword = [&](const wchar_t* name, DWORD defaultVal) -> DWORD {
        DWORD val = defaultVal;
        DWORD size = sizeof(val);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(hKey, name, nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS && type == REG_DWORD) {
            return val;
        }
        return defaultVal;
    };

    uint32_t zoom = readDword(L"DefaultZoomMode", 0);
    m_settings.defaultZoomMode = (zoom <= 2) ? zoom : 0;

    m_settings.defaultContinuous = (readDword(L"DefaultContinuous", 0) != 0);

    uint32_t theme = readDword(L"PageTheme", 0);
    m_settings.pageTheme = (theme <= 2) ? static_cast<PageTheme>(theme) : PageTheme::Original;

    m_settings.rememberLastPage     = (readDword(L"RememberLastPage", 1) != 0);
    m_settings.doubleClickLookup    = (readDword(L"DoubleClickLookup", 1) != 0);
    m_settings.rightClickAutoSelect = (readDword(L"RightClickAutoSelect", 0) != 0);

    int32_t ttsRate = static_cast<int32_t>(readDword(L"DefaultTtsRate", 0));
    if (ttsRate != -2 && ttsRate != 0 && ttsRate != 1 && ttsRate != 2 && ttsRate != 4) {
        ttsRate = 0;
    }
    m_settings.defaultTtsRate = ttsRate;

    m_settings.autoOcrSearch     = (readDword(L"AutoOcrSearch", 1) != 0);
    m_settings.keepRecentHistory = (readDword(L"KeepRecentHistory", 1) != 0);

    RegCloseKey(hKey);
}

void SettingsController::SaveToRegistry() const {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, m_regSubKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return;
    }

    auto writeDword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
    };

    writeDword(L"DefaultZoomMode",      m_settings.defaultZoomMode);
    writeDword(L"DefaultContinuous",    m_settings.defaultContinuous ? 1u : 0u);
    writeDword(L"PageTheme",            static_cast<DWORD>(m_settings.pageTheme));
    writeDword(L"RememberLastPage",     m_settings.rememberLastPage ? 1u : 0u);
    writeDword(L"DoubleClickLookup",    m_settings.doubleClickLookup ? 1u : 0u);
    writeDword(L"RightClickAutoSelect", m_settings.rightClickAutoSelect ? 1u : 0u);
    writeDword(L"DefaultTtsRate",       static_cast<DWORD>(m_settings.defaultTtsRate));
    writeDword(L"AutoOcrSearch",        m_settings.autoOcrSearch ? 1u : 0u);
    writeDword(L"KeepRecentHistory",    m_settings.keepRecentHistory ? 1u : 0u);

    RegCloseKey(hKey);
}

void SettingsController::ResetToDefaults() {
    m_settings = AppSettings{};
    SaveToRegistry();
}

void SettingsController::SetDefaultZoomMode(uint32_t zoomMode) {
    m_settings.defaultZoomMode = (zoomMode <= 2) ? zoomMode : 0;
    SaveToRegistry();
}

void SettingsController::SetDefaultContinuous(bool continuous) {
    m_settings.defaultContinuous = continuous;
    SaveToRegistry();
}

PageTheme SettingsController::CyclePageTheme() {
    uint32_t next = (static_cast<uint32_t>(m_settings.pageTheme) + 1) % 3;
    m_settings.pageTheme = static_cast<PageTheme>(next);
    SaveToRegistry();
    return m_settings.pageTheme;
}

const wchar_t* SettingsController::GetThemeName(PageTheme theme) {
    switch (theme) {
    case PageTheme::Night: return L"Night Dark";
    case PageTheme::Sepia: return L"Sepia Paper";
    default:               return L"Original";
    }
}

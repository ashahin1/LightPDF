/**
 * @file pch.hpp
 * @brief Precompiled header for LightPDF including Win32, Direct2D, WinRT, and STL headers.
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Windows Desktop APIs
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shcore.h>

// Direct2D, Direct3D 11 & DirectWrite
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>

// WinRT C++/WinRT Projections
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>

// Standard C++20 Library
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>

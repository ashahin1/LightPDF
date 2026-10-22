# Contributing to LightPDF

Thank you for your interest in contributing to **LightPDF**!

## Core Principles & Design Constraints

Before submitting contributions, please keep in mind the fundamental design goals and architectural constraints of the project:

1. **Extreme Performance**: Sub-200ms cold launch (baseline ~159 ms), instantaneous tab switching (< 60 ms via `WM_COPYDATA`), smooth continuous scrolling, and low memory profile (< 10 MB private RAM).
2. **Zero External Runtime Dependencies**: Standalone single executable utilizing only native Windows 10/11 Win32, Direct2D 1.1, Direct3D 11, DirectWrite, and C++/WinRT (`Windows.Data.Pdf`, `Windows.Storage`, `Windows.Media.Ocr`) system APIs. **No external runtime DLLs**.
3. **Strict Size Budget**: Total executable size must strictly remain under **1 MB** (1,024 KB / 1,048,576 bytes), targeting a 100 KB safety margin (<= 924 KB). Every byte counts.
4. **Clean Code & Toolchain**: Pure C++20 (`/std:c++20`), static CRT linking (`/MT`), warning-free compilation under `/W4`, and whole-program optimization (`/GL` + `/LTCG`).

---

## Development Setup

### Prerequisites
- **Windows 10 (Build 1809+)** or **Windows 11** (64-bit).
- **MSVC Compiler** supporting C++20 (Visual Studio 2022, VS Build Tools, or portable MSVC).
- **PowerShell 5.1+** or Command Prompt.

### Building Locally

```cmd
:: Fast incremental build (uses PCH and /MP multi-core compilation)
build.bat

:: Full clean rebuild
build.bat /rebuild

:: Clean intermediate build artifacts
build.bat /clean
```

*(PowerShell equivalents: `.\build.ps1`, `.\build.ps1 -Rebuild`, `.\build.ps1 -Clean`).*

---

## Testing & Verification

Always run the automated test suite before opening a pull request:

```cmd
:: Run the unified test suite (21 test cases, 228 assertions)
cmd.exe /c tests\run_tests.bat
```

```powershell
:: Run multi-document benchmark test script
powershell -ExecutionPolicy Bypass -File tests\test_samples.ps1

:: Verify Doxygen documentation builds without warnings
cmd.exe /c generate_docs.bat
```

---

## Coding Guidelines

- **Code Formatting**: Format all C++ code using the repository's root `.clang-format` rules (4 spaces, WebKit/Chromium-aligned).
- **High-DPI Awareness**: Maintain `PerMonitorV2` DPI scaling accuracy for all Direct2D drawing, font sizes, HUD overlays, and hit-testing coordinates.
- **Resource Management**:
  - Always use `Microsoft::WRL::ComPtr` or RAII wrappers for COM / DirectX resources.
  - Release Direct2D bitmaps and WinRT stream buffers promptly to prevent memory leaks and file locking.
- **Bilingual & Unicode Support**:
  - Ensure all string operations use UTF-8 (`/utf-8`) or native `std::wstring`.
  - When modifying text search or parsing, preserve Arabic script normalization, diacritic stripping, and bidirectional matching.
- **Zero Warnings**: Compile with `/W4`. No compiler warnings or lint errors are permitted.

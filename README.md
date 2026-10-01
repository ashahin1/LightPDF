<h1 align="center">
  LightPDF
</h1>

<p align="center">
  <b>An ultra-fast, minimalist, hardware-accelerated PDF viewer for Windows.</b><br>
  <sub>799 KB · 159 ms cold launch · 8.7 MB RAM · Zero runtime DLLs · C++20 · Direct2D/Direct3D 11</sub>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/binary-799_KB-brightgreen?style=flat-square" alt="Size">
  <img src="https://img.shields.io/badge/startup-159_ms-2196F3?style=flat-square" alt="Startup">
  <img src="https://img.shields.io/badge/RAM-8.7_MB-7B1FA2?style=flat-square" alt="RAM">
  <img src="https://img.shields.io/badge/C%2B%2B20-MSVC_14.44-E65100?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/platform-Windows_10%2F11-0078D6?style=flat-square" alt="Windows">
  <img src="https://img.shields.io/badge/DLLs-zero-4CAF50?style=flat-square" alt="Dependencies">
  <img src="https://img.shields.io/badge/license-MIT-9E9E9E?style=flat-square" alt="License">
</p>

---

## Quick Start

```cmd
:: Run — no installation needed
LightPDF.exe

:: Open a PDF directly
LightPDF.exe "C:\path\to\document.pdf"

:: Register as default PDF viewer (per-user, no admin required)
LightPDF.exe /register /silent
```

---

## Features

| | |
|---|---|
| ⚡ **Instant Launch** | Cold starts in **159 ms** &mdash; faster than any mainstream PDF reader |
| 🪶 **Sub-Megabyte Footprint** | **799 KB** single `.exe`, **8.7 MB** private memory, **zero external DLLs** |
| 🗣️ **Native Read Aloud (TTS)** | `Ctrl+R` high-fidelity bilingual text-to-speech with real-time visual word tracking. Automatic Arabic (`Microsoft Naayf`) and English voice discovery via Windows OneCore & SAPI. Floating frosted HUD with rate tuning (`Ctrl+[` / `]`), sentence skip, and automatic continuous multi-page reading |
| 📄 **Native Searchable PDF Export** | `Ctrl+S` / `Ctrl+Shift+S`: bakes an invisible text layer (`3 Tr` PDF sandwich) into scanned files via `Windows.Media.Ocr`. Atomic in-place overwrite or Save As new file with zero image quality degradation |
| 🎯 **Presentation & Laser Pointer** | Press `L` to activate an optical glowing laser pointer; cycle colors (`C`) through Red, Green, Cyan, and Gold. Automatically dismisses upon switching to navigation tools |
| 📖 **Offline Technical Dictionary** | Double-click or press `D`: instant English-Arabic translation and definitions for Computer Engineering, Architecture, Networks, Cybersecurity, and ABET terms &mdash; 100% offline |
| 🔍 **Bilingual Search Engine** | `Ctrl+F` real-time search with diacritics stripping, Alef/Taa Marbuta normalization, Eastern & Western Arabic numeral unification, and bidirectional matching |
| 📜 **Continuous & Horizontal Scroll** | `Ctrl+3` continuous layout; native touchpad 2-finger horizontal swipe (`WM_MOUSEHWHEEL`), mouse tilt-wheel scrolling, `Shift + Wheel`, and an auto-fading scrub scrollbar |
| 🎨 **Chromeless Design** | No toolbars, ribbons, side panels, or telemetry &mdash; just your document |
| 🗂️ **Smart Multi-Tabs** | Auto-hiding tab bar: invisible with 1 doc, appears with 2+, vanishes again when closed down to 1 |
| 🚀 **Single Instance** | Opening PDFs from Explorer forwards into new tabs of the existing window in **54 ms** via `WM_COPYDATA` |
| ✋ **Hand & Selection Tools** | Seamlessly toggle between Hand Pan (`H`) and Text Selection (`V` / `S`) with automatic laser dismissal and clipboard copy (`Ctrl+C`) |
| 🖨️ **300 DPI Streaming Printing** | Native `PrintDlgExW` &mdash; All Pages, Current Page, or Custom Ranges &mdash; streamed page-by-page |
| ℹ️ **Document Properties** | `Ctrl+D` instant inspection dialog for title, author, producer, page count, and PDF version |
| 🎮 **GPU Hardware Rendering** | Direct3D 11.1 + Direct2D 1.1 zero-copy pipeline via `IPdfRendererNative` + DXGI Flip Discard |
| 🔎 **Smooth Zooming** | Cursor-centered `Ctrl+Wheel`, plus `Ctrl+0` Fit Page, `Ctrl+1` 100%, and `Ctrl+2` Fit Width |
| 🖥️ **HiDPI Native** | PerMonitorV2 DPI &mdash; crisp on 4K, 8K, and mixed multi-monitor setups |
| 🔗 **Zero-Admin Association** | `LightPDF.exe /register` &mdash; adds to Windows "Open with" & Default Apps without admin |

---

## Benchmarks

Measured on Windows 11 x64 with cold execution and hardware performance counters.

| Metric | LightPDF | SumatraPDF | Adobe Acrobat | Microsoft Edge | Electron Viewers |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Binary Size** | **799 KB** | ~14 MB | ~250 MB | ~180 MB | ~120 MB |
| **External DLLs** | **0** | 0 | 80+ | 50+ | 60+ |
| **Cold Startup** | **159 ms** | ~180 ms | ~1,800 ms | ~950 ms | ~1,400 ms |
| **Tab Forwarding** | **54 ms** | ~120 ms | ~1,200 ms | ~400 ms | ~850 ms |
| **RAM — 1 Doc** | **8.7 MB** | ~28 MB | 180 MB+ | 120 MB+ | 220 MB+ |
| **RAM — 3 Docs** | **24.8 MB** | ~65 MB | 350 MB+ | 280 MB+ | 450 MB+ |
| **Admin Required** | **No** | No | Yes | Yes | Optional |

---

## Keyboard & Mouse Shortcuts

Press **`F1`** in-app to display the interactive, categorized Direct2D shortcuts overlay.

### Navigation

| Shortcut | Description |
| :--- | :--- |
| `Page Down` / `Space` | Advance to next page |
| `Page Up` / `Shift+Space` | Go to previous page |
| `→` / `←` | Next / previous page (or skip sentence in Read Aloud) |
| `Home` / `End` | Jump to first / last page |
| `Mouse Wheel` | Scroll page vertically |
| `Touchpad 2-Finger Swipe` / `Tilt Wheel` | Scroll page horizontally (when zoomed in) |
| `Shift + Mouse Wheel` | Scroll page horizontally (when zoomed in) |
| `Middle Drag` / `Space + Drag` | Smooth canvas panning |
| `H` | Hand Tool (toggle drag pan mode; dismisses laser) |
| `V` / `S` | Text Selection Tool (dismisses laser) |

### Zoom & View

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + Wheel` / `+` / `-` | Zoom in / out centered on cursor |
| `Ctrl + 0` | Fit full page to window |
| `Ctrl + 1` | Actual size (100% zoom) |
| `Ctrl + 2` | Fit page width to window |
| `Ctrl + 3` | Toggle continuous vertical scroll |
| `Double Click` | Fit Page / Fit Width toggle (or Open file if canvas empty) |
| `F11` | Toggle borderless fullscreen |
| `Scrollbar Drag` | Scrub through document pages with live preview tooltip |

### Read Aloud (Text-to-Speech)

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + R` | Toggle Read Aloud playback & floating frosted HUD |
| `Space` | Pause / resume speech playback (when Read Aloud active) |
| `Ctrl + [` / `Ctrl + ]` | Decrease / increase speech reading speed |
| `←` / `→` | Skip backward / forward by sentence |
| `Esc` | Stop speech playback and dismiss Read Aloud HUD |

### Tabs & Files

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + O` / `Ctrl + T` | Open PDF document in new tab |
| `Ctrl + S` | Save Searchable PDF prompt (Overwrite original or Save As new copy) |
| `Ctrl + Shift + S` | Save Searchable PDF directly to a new file destination |
| `Ctrl + W` | Close active tab |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Switch to next / previous tab |
| `Alt + 1` … `Alt + 9` | Jump directly to tabs 1 through 9 |
| `Middle Click Tab` | Close clicked tab |
| `Drag & Drop` | Drop one or multiple PDF files to open as tabs |

### Search & Tools

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + F` | Find text in document (floating search bar) |
| `F3` / `Shift + F3` | Next / previous search match |
| `Enter` / `Shift + Enter` | Next / previous match in search bar |
| `L` | Toggle Presentation Laser Pointer |
| `C` | Cycle Laser Pointer Color (Red, Green, Cyan, Gold) |
| `Ctrl + C` | Copy selected text to clipboard |
| `D` / `Double Click` | Look up selected word in offline English-Arabic technical dictionary |
| `Ctrl + P` | Print document (All / Current / Page Range) |
| `Ctrl + G` | Jump to specific page number |
| `Ctrl + D` | Document properties (metadata, page count, PDF version) |
| `F1` / `Esc` | Toggle / dismiss help overlay or cancel operations |

---

## Searchable PDF Baking Architecture

```
Scanned / Non-Digital PDF
           │
  [Ctrl+S / Ctrl+Shift+S]
           │
           ▼
Windows.Media.Ocr (WinRT)
  ├── Prioritizes Arabic (ar-SA) with English fallback
  ├── Renders page to high-DPI software bitmap
  └── Runs on dedicated background STA worker thread (HUD progress)
           │
           ▼
PdfSearchableWriter (Native Incremental Update)
  ├── Synthesizes Type 0 Composite Font + CIDFontType2
  ├── Emits Adobe UCS /ToUnicode CMap (Identity-H UTF-16BE)
  ├── Invisible Content Stream: q 3 Tr BT ... ET Q
  │     ├── RTL character positioning for Arabic words
  │     └── LTR word advance for Latin words & digits
  ├── Appends incremental objects & updates trailer with /Prev
  └── Zero re-compression: original images & vectors preserved 100%
           │
    ┌──────┴────────────────────────┐
    ▼                               ▼
[Save As New Copy]       [Overwrite Original]
Native Save Picker       Atomic ReplaceFileW via .tmp
Original preserved       Hot-reloads open tab seamlessly
```

---

## File Association & Shell Integration

Register LightPDF as your default PDF viewer without administrator privileges:

```cmd
LightPDF.exe /register            :: Interactive confirmation dialog
LightPDF.exe /register /silent    :: Silent — for scripts & automated setup
LightPDF.exe /unregister          :: Cleanly removes all registry entries
```

This writes directly to `HKCU\Software\Classes` (per-user scope only) and calls `SHChangeNotify` so Windows Explorer updates immediately. After registering:
- Double-clicking any `.pdf` opens LightPDF instantly.
- If LightPDF is already running, the document opens as a **new tab** in the active window (tab forwarding via named mutex + `WM_COPYDATA`).

---

## Project Structure

```
LightPDF/
├── bin/
│   ├── LightPDF.exe              # ~799 KB standalone release binary (zero DLLs)
│   ├── obj/                      # Intermediate build artifacts (ignored by git)
│   └── dict/                     # Offline technical dictionary database
│       └── en-ar.dat
├── dict/
│   ├── en-ar.dat                 # Binary English-Arabic technical terminology
│   └── user_terms.txt            # Custom additions source list
├── resources/
│   ├── app.ico                   # Multi-resolution icon (16×16 → 256×256)
│   ├── app.rc                    # Windows resource script
│   └── app.manifest              # PerMonitorV2 + Win10/11 compatibility manifest
├── src/
│   ├── main.cpp                  # Process entry, CLI parsing, single-instance mutex
│   ├── app_window.hpp/.cpp       # Window lifecycle, thread coordination, message routing
│   ├── tab_controller.hpp/.cpp   # Document tabs, multi-tab layout, continuous scroll metrics
│   ├── search_controller.hpp/.cpp# Interactive find in page, match navigation & debounce
│   ├── selection_controller.hpp/.cpp# Text selection, dictionary lookup card lifecycle
│   ├── ui_views.hpp/.cpp         # Modular D2D views: TabStrip, ScrollBar, Search, Overlays
│   ├── d2d_renderer.hpp/.cpp     # Direct2D 1.1 / D3D11 zero-copy hardware render pipeline
│   ├── pdf_document.hpp/.cpp     # WinRT Windows.Data.Pdf document wrapper & prefetch
│   ├── pdf_parser.hpp/.cpp       # High-speed PDF parser, CMap decoder, xref /Prev traversal
│   ├── pdf_search.hpp/.cpp       # Multilingual search engine, normalization, numerals
│   ├── pdf_searchable_writer.hpp/.cpp # Incremental PDF update engine, OCR sandwich
│   ├── tts_engine.hpp/.cpp       # Pure COM SAPI voice engine, Desktop & OneCore voice discovery
│   ├── read_aloud_controller.hpp/.cpp # Bilingual chunking, script routing, word-to-glyph bounding mapping
│   └── dictionary_engine.hpp/.cpp# Offline translation & technical dictionary engine
├── tests/
│   ├── fixtures/                 # Minimal synthetic PDF fixtures committed to git
│   │   ├── sample_1page.pdf
│   │   └── sample_2page.pdf
│   ├── doctest.h                 # Zero-overhead C++20 unit testing framework
│   ├── unified_tests.cpp         # Complete test suite (16 test cases, 162 assertions, <35ms runtime)
│   ├── run_tests.bat             # Test compilation & execution script (/W4)
│   ├── test_samples.ps1          # Automated multi-document benchmark test script
│   └── legacy/                   # Archived one-off interactive test harnesses
├── tools/
│   └── msvc/                     # Portable MSVC 14.44 + Windows SDK 10.0.26100
├── build.ps1                     # PowerShell optimized release build script (/W4 /O2 /MT)
├── build.bat                     # CMD batch release build script (/W4 /O2 /MT)
├── Doxyfile                      # Doxygen API documentation configuration
├── generate_docs.bat             # Automated Doxygen HTML documentation generator
├── .editorconfig                 # Standard cross-editor styling configuration
├── .clang-format                 # Chromium/Google C++ 4-space formatting specification
├── .git-blame-ignore-revs        # Blame exclusion for formatting revisions
├── LICENSE                       # MIT License
└── README.md
```

---

## Building from Source

### Prerequisites

- **Windows 10 or 11** (x64)
- **PowerShell** or **Command Prompt**
- **MSVC C++ Compiler** supporting C++20 (MSVC v143 / 14.4x recommended)

#### Option A: Visual Studio (if installed)
Open **Developer PowerShell for VS 2022** and run `.\build.ps1` or `build.bat`.

#### Option B: Portable MSVC (Zero Install, No Admin)
If you don't have Visual Studio installed, bootstrap a portable compiler into `tools/msvc/` using [portable-msvc](https://github.com/mmozeiko/portable-msvc):

```powershell
python portable-msvc.py
Move-Item -Path msvc -Destination tools\msvc
```

The build scripts (`build.ps1` / `build.bat`) automatically detect and activate `tools\msvc\activate.ps1`.

### Build Command

```powershell
# PowerShell (Recommended)
.\build.ps1
```

```cmd
:: Command Prompt
build.bat
```

Output: `bin\LightPDF.exe` (**~799 KB**)

### Compiler Flags

```
/O2 /MT /std:c++20 /GL /Gy /Gw /EHsc /utf-8 /permissive- /DNOMINMAX /W4
/Fo"bin\obj\\" /Fd"bin\obj\\"
/link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
      /MANIFEST:EMBED /MANIFESTINPUT:resources\app.manifest
```

| Flag | Purpose |
| :--- | :--- |
| `/O2` | Aggressive speed optimization for release code |
| `/W4` | Level 4 compiler diagnostics &mdash; strict zero-warning policy enforced |
| `/MT` | Static CRT linking &mdash; eliminates `VCRUNTIME140.dll` / `MSVCP140.dll` dependencies |
| `/GL` + `/LTCG` | Whole-program link-time code generation for maximum inter-procedural inlining |
| `/OPT:REF` + `/OPT:ICF` | Dead-code elimination and identical COMDAT folding for minimal binary size |
| `/Gy` + `/Gw` | Function-level and global data-level linking for granular dead-code removal |

---

## Generating Documentation (Doxygen)

LightPDF source headers are annotated with Doxygen docstrings (`///` and `/** ... */`). You can generate complete, searchable HTML documentation:

### 1. Install Doxygen (if not already installed)
```cmd
winget install DimitrivanHeesch.Doxygen
```
*(Or download the installer from [doxygen.nl/download.html](https://www.doxygen.nl/download.html)).*

### 2. Run the Generator
```cmd
generate_docs.bat
```
*(Or run `doxygen Doxyfile` directly).*

### 3. View Output
Open `docs\html\index.html` in any web browser to view the interactive API reference, class hierarchy, data structures, and architectural documentation.

---

## License

[MIT](LICENSE)

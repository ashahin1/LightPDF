<div align="center" style="text-align: center;">

# LightPDF

<p>
  <b>An ultra-fast, minimalist, hardware-accelerated PDF viewer for Windows.</b><br>
  <sub>1,008 KB · 159 ms cold launch · 8.7 MB RAM · Zero runtime DLLs · C++20 · Direct2D/Direct3D 11</sub>
</p>

<p>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square" alt="C++20"></a>
  <a href="https://www.microsoft.com/windows"><img src="https://img.shields.io/badge/platform-Windows_10%2F11-0078D6?style=flat-square" alt="Windows"></a>
  <a href="https://github.com/ashahin1/LightPDF/releases"><img src="https://img.shields.io/badge/release-v2.4.1-blue?style=flat-square" alt="Release"></a>
  <a href="https://github.com/ashahin1/LightPDF/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/ashahin1/LightPDF/build.yml?branch=main&label=CI&style=flat-square" alt="CI"></a>
  <img src="https://img.shields.io/badge/binary-1008_KB-brightgreen?style=flat-square" alt="Size">
  <img src="https://img.shields.io/badge/startup-159_ms-2196F3?style=flat-square" alt="Startup">
  <img src="https://img.shields.io/badge/RAM-8.7_MB-7B1FA2?style=flat-square" alt="RAM">
  <img src="https://img.shields.io/badge/tests-31%20passed%20%7C%20451%20assertions-success?style=flat-square" alt="Tests">
  <img src="https://img.shields.io/badge/DLLs-zero-4CAF50?style=flat-square" alt="Dependencies">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-9E9E9E?style=flat-square" alt="License"></a>
</p>

<p>
  <a href="https://github.com/ashahin1/LightPDF/releases/latest"><b>⬇️ Download Latest Release (LightPDF.exe)</b></a>
</p>

</div>

---

## Table of Contents

- [Quick Start & Command Line](#quick-start--command-line)
  - [Basic Usage](#basic-usage)
  - [Shell Integration & File Associations](#shell-integration--file-associations)
  - [Supported Formats](#supported-formats)
- [Performance Benchmarks](#performance-benchmarks)
- [Core Features](#core-features)
  - [⚡ Ultra-Fast Hardware-Accelerated Engine](#-ultra-fast-hardware-accelerated-engine)
  - [🗂️ Smart Tab Strip & Jump History](#️-smart-tab-strip--jump-history)
  - [🌐 Interactive Hyperlinks & Smart URLs](#-interactive-hyperlinks--smart-urls)
  - [🔒 Password-Protected PDFs & Decrypted Copy Export](#-password-protected-pdfs--decrypted-copy-export)
  - [🖱️ Smart Context-Sensitive Right-Click Menus](#️-smart-context-sensitive-right-click-menus)
  - [🗣️ Native Read Aloud (TTS) & Technical Dictionary](#️-native-read-aloud-tts--technical-dictionary)
  - [📄 Native Searchable PDF Baking & Multilingual Search](#-native-searchable-pdf-baking--multilingual-search)
  - [⚙️ Options & Preferences Dialog and Page Color Themes](#️-options--preferences-dialog-and-page-color-themes)
  - [💾 Context-Aware Save & Decrypted Save As Pipeline](#-context-aware-save--decrypted-save-as-pipeline)
- [Keyboard & Mouse Shortcuts](#keyboard--mouse-shortcuts)
- [Architecture & Technical Design](#architecture--technical-design)
  - [Searchable PDF Baking Pipeline](#searchable-pdf-baking-pipeline)
  - [Zero-Copy Direct2D / D3D11 Pipeline](#zero-copy-direct2d--d3d11-pipeline)
- [Building from Source](#building-from-source)
  - [Prerequisites](#prerequisites)
  - [Build Commands & Incremental Caching](#build-commands--incremental-caching)
  - [Compiler & Linker Flags](#compiler--linker-flags)
- [Automated Testing & Verification](#automated-testing--verification)
- [Code Signing & Colleague Distribution](#code-signing--colleague-distribution)
- [Developer API Documentation (Doxygen)](#developer-api-documentation-doxygen)
- [Project Structure](#project-structure)
- [License](#license)

---

## Quick Start & Command Line

### Basic Usage

LightPDF is completely portable and requires zero installation, admin privileges, or external dependencies:

```cmd
:: Launch empty viewer (shows interactive Recent Documents dashboard)
LightPDF.exe

:: Open a PDF file directly
LightPDF.exe "C:\Documents\Paper.pdf"

:: Open multiple PDF files simultaneously into tabs
LightPDF.exe "doc1.pdf" "doc2.pdf" "doc3.pdf"
```

### Shell Integration & File Associations

LightPDF includes complete per-user shell association capabilities (`HKCU`), allowing clean integration into Windows without requiring administrator rights:

```cmd
:: Register file associations, ProgID, and Default Apps capabilities
LightPDF.exe /register

:: Register silently (no graphical message box, ideal for automation scripts)
LightPDF.exe /register /silent

:: Cleanly remove all registered shell associations and capabilities
LightPDF.exe /unregister

:: Unregister silently
LightPDF.exe /unregister /silent
```

> [!NOTE]
> **Windows Default Apps Integration**: Running `/register` registers `ProgID` (`LightPDF.Document`), `OpenWithProgids`, `Applications\LightPDF.exe`, and registers capabilities under `HKCU\Software\LightPDF\Capabilities` and `RegisteredApplications`. To make LightPDF your default viewer, select it in **Windows Settings > Apps > Default apps**, or right-click any PDF and pick **Open with > Choose another app > Always**.

After registering:
- Double-clicking any `.pdf` opens LightPDF instantly.
- LightPDF appears in Windows **Default apps** and Explorer **Open with** menus.
- If LightPDF is already running, documents open as a **new tab** in the active window (single-instance IPC in **54 ms**).

### Supported Formats

LightPDF maintains a single unified source of truth for supported document formats (`src/supported_formats.hpp`), powering the Open/Save dialog filters, multi-file selection, drag-and-drop validation, shell context menus, and Windows Default Apps registration:

| Format | Extensions | Wildcard Pattern | Engine / Notes |
| :--- | :--- | :--- | :--- |
| **PDF Documents** | `.pdf` | `*.pdf` | Native hardware-accelerated rendering via Direct2D & `Windows.Data.Pdf` |
| **All Files** | Any extension (`*.*`) | `*.*` | Fallback filter in Open/Save dialogs and Drag & Drop |

---

## Performance Benchmarks

Measured on Windows 11 x64 with cold execution and hardware performance counters.

| Metric | LightPDF | SumatraPDF | Adobe Acrobat | Microsoft Edge | Electron Viewers |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Binary Size** | **880 KB** | ~14 MB | ~250 MB | ~180 MB | ~120 MB |
| **External DLLs** | **0** | 0 | 80+ | 50+ | 60+ |
| **Cold Startup** | **159 ms** | ~180 ms | ~1,800 ms | ~950 ms | ~1,400 ms |
| **Tab Forwarding** | **54 ms** | ~120 ms | ~1,200 ms | ~400 ms | ~850 ms |
| **RAM — 1 Doc** | **8.7 MB** | ~28 MB | 180 MB+ | 120 MB+ | 220 MB+ |
| **RAM — 3 Docs** | **24.8 MB** | ~65 MB | 350 MB+ | 280 MB+ | 450 MB+ |
| **Admin Required** | **No** | No | Yes | Yes | Optional |

---

## Core Features

### ⚡ Ultra-Fast Hardware-Accelerated Engine
- **Instant Launch**: Cold starts in **159 ms** &mdash; faster than any mainstream PDF reader.
- **Sub-Megabyte Footprint**: Single **880 KB** standalone executable with zero external DLL dependencies and **8.7 MB** initial RAM.
- **GPU Hardware Rendering**: Direct3D 11.1 + Direct2D 1.1 zero-copy pipeline via `IPdfRendererNative` + DXGI Flip Discard model.
- **Chromeless Design**: No toolbars, ribbons, side panels, or telemetry &mdash; pure focus on your document.
- **HiDPI Native**: Full PerMonitorV2 DPI awareness &mdash; razor-sharp on 4K, 8K, and mixed-scaling multi-monitor environments.

### 🗂️ Smart Tab Strip & Jump History
- **Smart Multi-Tabs**: Auto-hiding tab bar that remains invisible with 1 open document, appears smoothly with 2+, and vanishes when closed down to 1.
- **Tab Forwarding**: Single-instance mutex; opening documents from Explorer forwards them into new tabs of the running window in **54 ms** via `WM_COPYDATA`.
- **Closed Tab Recovery**: Press `Ctrl+Shift+T` to instantly restore recently closed tabs with exact page index, zoom factor, and continuous scroll position.
- **Recent Documents & Switcher**: Interactive empty-state dashboard with numeric hotkeys `1`&ndash;`9`, hover removal, and MRU registry persistence (`HKCU\Software\LightPDF\RecentFiles`). Press `Ctrl+H` for a centered quick switcher modal.
- **Document Jump History**: Internal 50-entry LIFO navigation stack &mdash; navigate back and forward with `Alt + ← / →` or mouse side buttons (`XBUTTON1`/`XBUTTON2`).
- **Hand & Selection Tools**: Seamlessly toggle between Hand Pan (`H` or `Middle Drag`) and Text Selection (`V` / `S`) with automatic laser dismissal and clipboard copy (`Ctrl+C`).

### 🌐 Interactive Hyperlinks & Smart URLs
- **ISO 32000-1 `/Annots` Parsing**: Extracts both external web/email links (`/Action /S /URI`) and internal page jumps (`/Action /S /GoTo` with `/D` arrays or direct `/Dest`).
- **Full Rotation & Geometry Mapping**: Maps coordinates from bottom-left PDF point space to top-left Direct2D DIP space across CropBox/MediaBox offsets and all rotations (0°, 90°, 180°, 270°).
- **Smart URL & Email Auto-Detection**: Scans digital text and OCR layers for `https://`, `http://`, `www.`, and `mailto:` links, unifying glyph bounding boxes across multi-line wraps with automatic `/Annots` de-duplication.
- **Sleek Bottom-Left Preview Pill**: Hovering reveals an `IDC_HAND` cursor, subtle on-page highlight + accent underline, and a floating URL preview status pill dynamically constrained so it never collides with the centered HUD pill.
- **Strict Scheme Whitelisting**: Strictly restricts external launching to `https://`, `http://`, and `mailto:` &mdash; blocking dangerous schemes (`file:`, `javascript:`, `cmd:`, `powershell:`, and UNC paths).

### 🔒 Password-Protected PDFs & Decrypted Copy Export
- **Fast Encrypted Document Handling**: Native detection and unlocking of password-protected PDF files using `Windows.Data.Pdf`.
- **Sleek Direct2D Modal Prompt**: Dark glass card with masked password input, Show/Hide password toggle, clipboard paste (`Ctrl+V`), and error shake feedback.
- **Zero RAM Plaintext Residue**: Clears password buffers immediately using `SecureZeroMemory` after authentication.
- **Lossless Decrypted Copy Export**: Once unlocked, press `Ctrl+S` / `Ctrl+Shift+S` to export an unencrypted, password-free PDF copy without re-rendering or image recompression. Built on Windows CNG (`bcrypt.dll`), supporting Standard Security Handler Algorithm 2 (RC4-128, AES-128) and Algorithm 5 (AES-256) with automatic cross-reference reconstruction.
- **On-Demand OCR Fallback**: Enables full text selection, offline dictionary lookup, and search even on encrypted PDFs whose security flags disable standard digital text extraction.

### 🖱️ Smart Context-Sensitive Right-Click Menus
- **Four Dynamic Interaction Domains**: Native Win32 menus that automatically adapt based on cursor context with automatic Windows 10/11 dark mode styling:
  - **Tab Strip Menu**: Close Tab (`Ctrl+W`), Close Other Tabs, Close Tabs to Right, Reopen Closed Tab (`Ctrl+Shift+T`), Copy Full File Path, and Reveal in File Explorer (`/select`).
  - **Hyperlink Menu**: Default bold Open Link action, Copy Link Address, Fit Page (`Ctrl+0`), Actual Size (`Ctrl+1`), Fit Width (`Ctrl+2`), and Print (`Ctrl+P`).
  - **Text Selection Menu**: Copy (`Ctrl+C`), Look up in Dictionary (`D`), Read Aloud Selection, Search for Selection (`Ctrl+F`), Select All (`Ctrl+A`), and Deselect (`Esc`). Appears seamlessly whenever text is selected prior to right-clicking.
  - **Document Canvas Menu**: Page navigation (Next/Prev/Go to), View Modes (Fit Page `Ctrl+0`, Actual Size `Ctrl+1`, Fit Width `Ctrl+2`, Continuous Scrolling `Ctrl+3` toggle checkmark), Read Aloud, Find, Print, Save Decrypted Copy / Save Searchable PDF, and Document Properties (`Ctrl+D`).

### 🗣️ Native Read Aloud (TTS) & Technical Dictionary
- **Bilingual Text-to-Speech**: Press `Ctrl+R` for high-fidelity speech playback with real-time visual word highlighting. Automatically routes Arabic to `Microsoft Naayf` and English to system voices via Windows OneCore & SAPI.
- **Frosted Playback HUD**: Floating control bar with reading speed tuning (`Ctrl+[` / `]`), pause/resume (`Space`), sentence skipping (`←` / `→`), and automatic continuous multi-page reading.
- **Offline Technical Dictionary**: Double-click or press `D` on any selected word for instant English-Arabic translation and definitions covering Computer Engineering, Architecture, Networks, Cybersecurity, and ABET terms &mdash; 100% offline.

### 📄 Native Searchable PDF Baking & Multilingual Search
- **Searchable PDF Sandwich Export**: Press `Ctrl+S` / `Ctrl+Shift+S` to bake an invisible OCR text layer (`3 Tr` PDF sandwich) into scanned files via `Windows.Media.Ocr`. Supports atomic in-place overwrite or Save As with zero image recompression.
- **Bilingual Search Engine**: Real-time search (`Ctrl+F`) with diacritics stripping, Alef/Taa Marbuta normalization, Eastern & Western Arabic numeral unification, and bidirectional matching.
- **Presentation Laser Pointer**: Press `L` to activate an optical glowing laser pointer; cycle colors (`C`) through Red, Green, Cyan, and Gold.
- **300 DPI Streaming Printing**: Native `PrintDlgExW` &mdash; All Pages, Current Page, or Custom Ranges &mdash; streamed page-by-page directly to the printer spooler.

### ⚙️ Options & Preferences Dialog and Page Color Themes
- **Tabbed Direct2D Options Modal (`Ctrl+,`)**: Centered, keyboard-accessible options dialog with categories for **View & Reading**, **Tools & Behavior**, and **System & History**.
  - Default Zoom Mode (Fit Page, Fit Width, 100% Zoom).
  - Continuous vertical scroll preference.
  - Page Color Theme selection with instant live preview.
  - Page memory toggle (remember and restore exact page position upon document reload).
  - Double-click word dictionary lookup toggle.
  - Right-click auto-select toggle (approach 1: context menu appears when text is selected; approach 2: auto-select nearest word).
  - Auto-OCR scanned search trigger toggle.
  - Default speech rate slider (0.75x to 2.0x).
  - Recent files history retention toggle with registry cleanup.
- **Hardware-Accelerated Page Color Themes (`Ctrl+I`)**: Direct2D Color Matrix Effect filters providing high-contrast, eye-friendly document reading:
  - **Original**: Standard document rendering.
  - **Night Dark**: Contrast-balanced inversion transforming white pages into dark charcoal (`#1c1f24`) and black text into soft warm white (`#ececee`), with theme-matched underlay and crisp outlines.
  - **Sepia Paper**: Warm, vintage cream tint (`#f5e8cf`) reducing harsh blue light for long reading sessions.

### 💾 Context-Aware Save & Decrypted Save As Pipeline
- **Smart Pipeline Routing**: Pressing `Ctrl+S` or `Ctrl+Shift+S` automatically detects the document state:
  - **Encrypted / Decrypted PDF**: Seamlessly exports an unencrypted, password-free PDF copy without recompressing page raster images.
  - **Scanned Document**: Bakes an invisible, searchable OCR text layer (`3 Tr` PDF sandwich) into the file.
- **In-Place Decryption & File Replacement**: Allows replacing the encrypted file in-place on disk or exporting to a new filename.
- **Post-Save Tab Switching**: Automatically binds the newly saved file to the active tab, updates tab captions, and keeps navigation fluid.

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
| `Alt + ←` / `Alt + →` | Navigate backward / forward in document jump history |
| `Mouse Back / Forward` | Navigate backward / forward across internal document links |
| `Click on Link / URL` | Open external web link safely or jump to internal target page |
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
| `Ctrl + I` | Cycle page theme filter (Original, Night Dark, Sepia) |
| `Double Click` | Fit Page / Fit Width toggle (or Open file if canvas empty) |
| `F11` | Toggle borderless fullscreen |
| `Scrollbar Drag` | Scrub through document pages with live preview tooltip |

### Tabs & Document Management

| Shortcut | Description |
| :--- | :--- |
| `Right Click Tab` | Tab Context Menu (Close, Close Others, Close Right, Reopen, Copy Path, Reveal in Explorer) |
| `Ctrl + O` / `Ctrl + T` | Open PDF document in new tab |
| `Ctrl + Shift + T` | Reopen last closed tab (restoring exact page, zoom, scroll position) |
| `Ctrl + H` | Toggle Recent Documents Quick Switcher modal |
| `1` … `9` | Jump directly to recent document (in Quick Switcher or Dashboard) |
| `Ctrl + W` | Close active tab |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Switch to next / previous tab |
| `Alt + 1` … `Alt + 9` | Jump directly to tabs 1 through 9 |
| `Middle Click Tab` | Close clicked tab |
| `Drag & Drop` | Drop one or multiple PDF files to open as tabs |

### Read Aloud (Text-to-Speech)

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + R` | Toggle Read Aloud playback & floating frosted HUD |
| `Space` | Pause / resume speech playback (when Read Aloud active) |
| `Ctrl + [` / `Ctrl + ]` | Decrease / increase speech reading speed |
| `←` / `→` | Skip backward / forward by sentence |
| `Esc` | Stop speech playback and dismiss Read Aloud HUD |

### Search & Document Tools

| Shortcut | Description |
| :--- | :--- |
| `Ctrl + F` | Find text in document (floating search bar) |
| `F3` / `Shift + F3` | Next / previous search match |
| `Enter` / `Shift + Enter` | Next / previous match in search bar |
| `Ctrl + S` | Save Searchable PDF prompt (Overwrite original or Save As new copy) |
| `Ctrl + Shift + S` | Save Searchable PDF directly to a new file destination |
| `L` | Toggle Presentation Laser Pointer |
| `C` | Cycle Laser Pointer Color (Red, Green, Cyan, Gold) |
| `Right Click Canvas` | Document Canvas Menu (Navigation, View Modes, Scroll Toggle, Search, Print, Properties) |
| `Right Click Selection` | Text Selection Menu (Copy, Dictionary, Read Aloud, Search Selection, Select All, Deselect) |
| `Right Click Link` | Hyperlink Menu (Open Link, Copy Link Address, View Modes, Print) |
| `Ctrl + A` | Select all text on current page |
| `Ctrl + C` | Copy selected text to clipboard |
| `D` / `Double Click` | Look up selected word in offline English-Arabic technical dictionary |
| `Ctrl + P` | Print document (All / Current / Page Range) |
| `Ctrl + G` | Jump to specific page number |
| `Ctrl + D` | Document properties (metadata, page count, PDF version) |
| `Ctrl + ,` | Options & Preferences dialog (Tabbed Direct2D modal) |
| `F1` / `Esc` | Toggle / dismiss help overlay or cancel operations |

---

## Architecture & Technical Design

### Searchable PDF Baking Pipeline

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

### Zero-Copy Direct2D / D3D11 Pipeline

```
     PDF Document (WinRT / Native)
                  │
                  ▼
   DXGI 1.2 SwapChain (Flip Discard)
                  │
    ┌─────────────┴─────────────┐
    ▼                           ▼
Direct2D Context         DirectWrite Factory
  ├── PDF Hardware Raster  ├── Tab Strip & HUD
  ├── Selection Highlight  ├── URL Preview Pill
  └── Laser Glow Halo      └── Modal Dialogs
                  │
                  ▼
   Desktop Window Manager (DWM)
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
If you do not have Visual Studio installed, bootstrap a portable compiler into `tools/msvc/` using [portable-msvc](https://github.com/mmozeiko/portable-msvc):

```powershell
python portable-msvc.py
Move-Item -Path msvc -Destination tools\msvc
```

The build scripts (`build.ps1` / `build.bat`) automatically detect and activate `tools\msvc\activate.ps1`.

### Build Commands & Incremental Caching

The build system features an **intelligent incremental compilation and caching engine** using MSVC Precompiled Headers (PCH), `/MP` multi-processor parallel compilation, and recursive `#include` dependency tracking:

```powershell
# Fast incremental build (compiles only modified files and dependent headers)
.\build.ps1

# Full clean rebuild (rebuilds PCH and all translation units from scratch)
.\build.ps1 -Rebuild

# Clean all build artifacts (bin\obj, bin\LightPDF.exe, resources\app.res)
.\build.ps1 -Clean
```

```cmd
:: Command Prompt equivalents
build.bat            :: Smart incremental build (< 0.5s when up to date)
build.bat /rebuild   :: Full clean rebuild
build.bat /clean     :: Clean build artifacts
```

#### Build Performance Benchmarks

| Build Scenario | Baseline (Monolithic) | LightPDF Incremental + PCH | Speedup |
| :--- | :---: | :---: | :---: |
| **No Changes (Up-to-Date)** | 106.75 s | **0.47 s** | **> 200× faster** |
| **Single File Modified** | 106.75 s | **13.17 s** | **8× faster** |
| **Full Clean Rebuild** | 106.75 s | **21.39 s** | **5× faster** |

Output: `bin\LightPDF.exe` (**880 KB** unsigned / **~881 KB** signed)

### Compiler & Linker Flags

```
/MP /O2 /MT /std:c++20 /GL /Gy /Gw /EHsc /utf-8 /permissive- /DNOMINMAX /W4
/Yu"pch.hpp" /FI"pch.hpp" /Fp"bin\obj\pch.pch"
/Fo"bin\obj/" /Fd"bin\obj/"
/link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
      /MANIFEST:EMBED /MANIFESTINPUT:resources\app.manifest
```

| Flag | Purpose |
| :--- | :--- |
| `/MP` | Multi-processor compilation &mdash; compiles translation units concurrently across all logical CPU cores |
| `/Yu` + `/FI` | Precompiled headers with forced inclusion &mdash; parses >1.5M lines of WinRT/D2D/STL headers once into `pch.pch` |
| `/O2` | Aggressive speed optimization for release code |
| `/W4` | Level 4 compiler diagnostics &mdash; strict zero-warning policy enforced |
| `/MT` | Static CRT linking &mdash; eliminates `VCRUNTIME140.dll` / `MSVCP140.dll` dependencies |
| `/GL` + `/LTCG` | Whole-program link-time code generation for maximum inter-procedural inlining |
| `/OPT:REF` + `/OPT:ICF` | Dead-code elimination and identical COMDAT folding for minimal binary size |
| `/Gy` + `/Gw` | Function-level and global data-level linking for granular dead-code removal |

---

## Automated Testing & Verification

LightPDF includes a unified automated test suite built on the zero-overhead `doctest` framework:

```cmd
:: Run the complete automated test suite
tests\run_tests.bat
```

```
===============================================================================
[LightPDF Test Runner] Executing 26 test cases...
===============================================================================
  Test Cases: 26 passed | 0 failed | 26 total
  Assertions: 388 passed | 0 failed | 388 total
  Elapsed:    ~3.8 s
===============================================================================
>>> ALL TESTS PASSED SUCCESSFULLY! <<<
```

The test suite exercises all critical components:
- **PDF Core Parser & Streams**: Incremental xref `/Prev` traversal, stream decompression (Flate / ASCII85), and font dictionary extraction.
- **CMap & Font Metrics**: Composite Type 0 fonts, CIDFontType2, and Adobe UCS `/ToUnicode` mapping.
- **Multilingual Search**: Arabic diacritic stripping, Alef/Taa Marbuta normalization, Eastern-to-Western Arabic numeral normalization, and bidirectional text matching.
- **Interactive Hyperlinks**: ISO 32000-1 `/Annots` parsing (`/URI`, `/GoTo`), plaintext URL regex extraction, single/continuous hit-testing, and navigation history stacks.
- **Security & Password Protection**: Encrypted PDF status transitions, modal hit-testing, memory clearing, and lossless decryption export (sample_encrypted.pdf).
- **Context Menus & Batch Tabs**: Context menu state assembly across all 4 domains, truncation logic, and TabController CloseOtherTabs / CloseTabsToTheRight batch operations.
- **Scanned Document OCR Guard**: Verifies GetOrExtractPageText never triggers eager synchronous OCR during document load or hover.
- **Read Aloud (TTS)**: Bilingual text chunking, script-based voice routing, and glyph bounding box mapping.
- **Tab & Recent State**: Closed tab LIFO recovery, MRU registry persistence, and Quick Switcher hit-testing.

---

## Code Signing & Colleague Distribution

To distribute LightPDF to colleagues without triggering Windows SmartScreen (*"unrecognized app"*) warnings:

### 1. Generate Signed Distribution Package
After building the executable (`build.bat` or `build.ps1`), run:

```cmd
tools\sign.bat
```

This automatically:
- Creates a dedicated 5-year Authenticode Code Signing certificate for `Ahmed Amer Shahin (EtherHome)` (if not already present).
- Digitally signs `bin\LightPDF.exe` with SHA-256 Authenticode.
- Prepares a self-contained distribution bundle in `dist\`.

### 2. Share `dist\` with Colleagues
Zip or share the generated `dist\` folder with your colleagues:
1. They run `install_certificate.bat` **once** (registers the certificate into their Windows Trusted Publishers store and clears Internet download flags).
2. They launch `LightPDF.exe` immediately with zero SmartScreen or Defender warnings!

---

## Developer API Documentation (Doxygen)

LightPDF source headers are annotated with Doxygen docstrings (`///` and `/** ... */`). You can generate complete, searchable HTML documentation:

### 1. Install Doxygen (if not already installed)
```cmd
winget install DimitrivanHeesch.Doxygen
```
*(Or download the installer from [doxygen.nl/download.html](https://www.doxygen.nl/download.html)).*

### 2. Run the Generator
```cmd
:: Using PowerShell
powershell -ExecutionPolicy Bypass -File generate_docs.ps1

:: Or using Command Prompt
generate_docs.bat
```
*(Or run `doxygen Doxyfile` directly).*

### 3. View Output
Open `docs\html\index.html` in any web browser to view the interactive API reference, class hierarchy, data structures, and architectural documentation.

---

## Project Structure

```
LightPDF/
├── bin/
│   ├── LightPDF.exe              # ~944 KB standalone binary (zero external DLLs, <1 MB budget)
│   ├── obj/                      # Intermediate build artifacts (ignored by git)
│   └── dict/                     # Offline technical dictionary database
│       └── en-ar.dat
├── dict/
│   ├── en-ar.dat                 # Binary English-Arabic technical terminology
│   └── user_terms.txt            # Custom additions source list
├── resources/
│   ├── app.ico                   # Multi-resolution icon (16×16 → 256×256)
│   ├── app.rc                    # Windows resource script (App Icon + VS_VERSION_INFO)
│   └── app.manifest              # PerMonitorV2 + Win10/11 compatibility manifest
├── src/
│   ├── pch.hpp/.cpp              # Precompiled header (Win32, Direct2D, WinRT, STL) for ~5x faster incremental builds
│   ├── main.cpp                  # Process entry, CLI parsing, single-instance mutex
│   ├── app_window.hpp/.cpp       # Window lifecycle, thread coordination, message routing
│   ├── tab_controller.hpp/.cpp   # Document tabs, multi-tab layout, continuous scroll metrics, jump history
│   ├── search_controller.hpp/.cpp# Interactive find in page, match navigation & debounce
│   ├── selection_controller.hpp/.cpp# Text selection, link hit-testing, dictionary card lifecycle
│   ├── ui_views.hpp/.cpp         # Modular D2D views: TabStrip, ScrollBar, Search, Overlays, Password, Recent
│   ├── d2d_renderer.hpp/.cpp     # Direct2D 1.1 / D3D11 zero-copy hardware render pipeline, URL preview pill
│   ├── pdf_document.hpp/.cpp     # WinRT Windows.Data.Pdf document wrapper, password decryption & prefetch
│   ├── pdf_parser.hpp/.cpp       # High-speed PDF parser, CMap decoder, xref /Prev traversal, /Annots link extraction
│   ├── pdf_decryptor.hpp/.cpp    # Lossless PDF decryption engine, CNG BCrypt AES-128/256 & RC4-128 /Encrypt removal
│   ├── context_menu_controller.hpp/.cpp # Native Win32 context-sensitive menus, dark mode, accelerator routing
│   ├── pdf_search.hpp/.cpp       # Multilingual search engine, normalization, numerals, OCR fallback
│   ├── pdf_searchable_writer.hpp/.cpp # Incremental PDF update engine, OCR sandwich layer
│   ├── tts_engine.hpp/.cpp       # Pure COM SAPI voice engine, Desktop & OneCore voice discovery
│   ├── read_aloud_controller.hpp/.cpp # Bilingual chunking, script routing, word-to-glyph bounding mapping
│   ├── recent_files_controller.hpp/.cpp # MRU history persistence, closed tab stack, quick switcher layout & hit-testing
│   ├── settings_controller.hpp/.cpp # Persistent settings model, registry serialization, options layout & hit-testing
│   ├── common_utils.hpp          # Centralized header-only utilities (DpiUtils, PathUtils, StringUtils, ClipboardUtils)
│   ├── supported_formats.hpp     # Single source of truth for supported document extensions & dialog filters
│   └── dictionary_engine.hpp/.cpp# Offline translation & technical dictionary engine
├── tests/
│   ├── fixtures/                 # Minimal synthetic PDF fixtures committed to git
│   │   ├── sample_1page.pdf
│   │   ├── sample_2page.pdf
│   │   └── sample_encrypted.pdf
│   ├── doctest.h                 # Zero-overhead C++20 unit testing framework
│   ├── unified_tests.cpp         # Complete test suite (27 test cases, 433 assertions, <5s runtime)
│   ├── run_tests.bat             # Test compilation & execution script (/W4)
│   ├── test_samples.ps1          # Automated multi-document benchmark test script
│   └── legacy/                   # Archived one-off interactive test harnesses
├── tools/
│   ├── sign.bat                  # 1-click Authenticode code signing & bundle generator
│   ├── sign.ps1                  # PowerShell script to sign binary & build dist package
│   └── msvc/                     # Portable MSVC 14.44 + Windows SDK 10.0.26100
├── build.ps1                     # PowerShell optimized release build script (/W4 /O2 /MT)
├── build.bat                     # CMD batch release build script (/W4 /O2 /MT)
├── Doxyfile                      # Doxygen API documentation configuration
├── generate_docs.ps1 / .bat      # Automated Doxygen HTML API documentation generators
├── .editorconfig                 # Standard cross-editor styling configuration
├── .clang-format                 # Chromium/Google C++ 4-space formatting specification
├── .git-blame-ignore-revs        # Blame exclusion for formatting revisions
├── LICENSE                       # MIT License
└── README.md
```

---

## License

[MIT](LICENSE)

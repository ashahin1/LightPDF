<h1 align="center">
  LightPDF
</h1>

<p align="center">
  <b>An ultra-fast, minimalist, hardware-accelerated PDF viewer for Windows.</b><br>
  <sub>270 KB · 159 ms cold launch · 8.7 MB RAM · Zero dependencies · C++20 · Direct2D/Direct3D 11</sub>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/binary-270_KB-brightgreen?style=flat-square" alt="Size">
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

:: Register as default PDF viewer (per-user, no admin)
LightPDF.exe /register /silent
```

---

## Features

| | |
|---|---|
| ⚡ **Instant Launch** | Cold starts in **159 ms** — faster than any mainstream PDF reader |
| 🪶 **Tiny Footprint** | **492 KB** single `.exe`, **8.7 MB** private memory, **zero DLLs** |
| 🔍 **Ultra-Fast Search** | `Ctrl+F` text search with floating UI, live highlights, match counter, case matching, & OCR fallback |
| 🎨 **Chromeless Design** | No toolbars, ribbons, side panels, or telemetry — just your document |
| 🗂️ **Smart Tabs** | Auto-hiding tab bar: invisible with 1 doc, appears with 2+, vanishes again when closed down to 1 |
| 🚀 **Single Instance** | Opening PDFs from Explorer forwards into new tabs of the existing window in **54 ms** |
| 🖨️ **300 DPI Printing** | Native `PrintDlgExW` — All Pages, Current Page, or Custom Ranges — streamed page-by-page |
| 🎮 **GPU Rendering** | Direct3D 11.1 + Direct2D 1.1 zero-copy pipeline via `IPdfRendererNative` + DXGI Flip Discard |
| 🔎 **Smooth Zoom** | Cursor-centered `Ctrl+Wheel`, plus `Ctrl+0` Fit Page / `Ctrl+1` 100% / `Ctrl+2` Fit Width |
| 🖥️ **HiDPI Native** | PerMonitorV2 DPI — crisp on 4K, 8K, and mixed multi-monitor setups |
| 🔗 **Easy Association** | `LightPDF.exe /register` — adds to Windows "Open with" & Default Apps without admin |

---

## Benchmarks

Measured on Windows 11 x64 with cold execution and hardware performance counters.

| Metric | LightPDF | Adobe Acrobat | Microsoft Edge | Electron Viewers |
| :--- | :---: | :---: | :---: | :---: |
| **Binary Size** | **270 KB** | ~250 MB | ~180 MB | ~120 MB |
| **External DLLs** | **0** | 80+ | 50+ | 60+ |
| **Cold Startup** | **159 ms** | ~1,800 ms | ~950 ms | ~1,400 ms |
| **Tab Forwarding** | **54 ms** | ~1,200 ms | ~400 ms | ~850 ms |
| **RAM — 1 Doc** | **8.7 MB** | 180 MB+ | 120 MB+ | 220 MB+ |
| **RAM — 3 Docs** | **24.8 MB** | 350 MB+ | 280 MB+ | 450 MB+ |
| **Admin Required** | **No** | Yes | Yes | Optional |

---

## Keyboard & Mouse Shortcuts

Press **`F1`** in-app to display the translucent Direct2D shortcuts overlay.

### Navigation & Zoom

| Action | Control |
| :--- | :--- |
| Open file | `Ctrl+O`, double-click empty canvas, drag-and-drop, or CLI argument |
| Print | `Ctrl+P` |
| Go to Page | `Ctrl+G` or click HUD pill (type page number + `Enter`) |
| Next / Previous page | `PgDn` / `PgUp`, `Space` / `Shift+Space`, `→` / `←`, or scroll wheel |
| First / Last page | `Home` / `End` |
| Continuous Scroll | `Ctrl+3` (toggle continuous vertical flow) |
| Scrollbar Scrubbing | Hover right edge & drag thumb with live page tooltip |
| Zoom in / out | `Ctrl+Wheel`, `+` / `-` |
| Fit Page | `Ctrl+0` or double-click canvas (when document is open) |
| Actual Size (100%) | `Ctrl+1` |
| Fit Width | `Ctrl+2` |
| Pan canvas | Left-drag or middle-drag |
| Toggle fullscreen | `F11` |
| Exit fullscreen / overlay | `Esc` |

### Search

| Action | Control |
| :--- | :--- |
| Find in document | `Ctrl+F` (opens floating search bar at top-right) |
| Next match | `Enter`, `F3`, or click **`▼`** |
| Previous match | `Shift+Enter`, `Shift+F3`, or click **`▲`** |
| Toggle Case Sensitivity | Click **`Aa`** button in search bar |
| Toggle OCR Fallback | Click **`OCR`** button (for scanned PDFs) |
| Dismiss search | `Esc` or click **`✕`** |

### Tabs

| Action | Control |
| :--- | :--- |
| New tab | `Ctrl+T` or click **`+`** |
| Close tab | `Ctrl+W`, click **`×`**, or middle-click tab |
| Next / Previous tab | `Ctrl+Tab` / `Ctrl+Shift+Tab` |
| Jump to tab 1–9 | `Alt+1` … `Alt+9` |

---

## File Association

Register LightPDF as your default PDF viewer without admin privileges:

```cmd
LightPDF.exe /register            :: Interactive confirmation dialog
LightPDF.exe /register /silent    :: Silent — for scripts & automation
LightPDF.exe /unregister          :: Clean up all registry entries
```

This writes to `HKCU\Software\Classes` (per-user scope only) and calls `SHChangeNotify` so Explorer picks up the change immediately. After registering, LightPDF appears in **"Open with"** and **Settings → Default Apps**.

When LightPDF is the default viewer:
- Double-clicking a `.pdf` opens it in LightPDF.
- If LightPDF is already running, the file opens as a **new tab** in the existing window (single-instance forwarding via named mutex + `WM_COPYDATA`).

---

## Architecture

```
LightPDF/
├── bin/
│   └── LightPDF.exe          # 492 KB standalone release binary (zero DLLs)
├── src/
│   ├── main.cpp               # wWinMain, DPI init, single-instance mutex, /register CLI
│   ├── app_window.hpp/.cpp    # Win32 window, multi-tab engine, search input dispatch, printing
│   ├── d2d_renderer.hpp/.cpp  # D3D11 + D2D1 renderer, search bar, highlights, tab bar, HUD
│   ├── pdf_document.hpp/.cpp  # Windows.Data.Pdf wrapper, page cache
│   ├── pdf_parser.hpp/.cpp    # Built-in deflate, PDF operator text parser, CMap decoder
│   └── pdf_search.hpp/.cpp    # Background threaded search engine, WinRT OCR fallback
├── resources/
│   ├── app.ico                # Multi-resolution icon (16×16 → 256×256)
│   ├── app.rc                 # Resource script
│   └── app.manifest           # PerMonitorV2 + Win10/11 compatibility manifest
├── tests/
│   ├── benchmark_startup.cpp  # Cold-launch & memory profiler
│   └── test_samples.ps1       # Automated multi-document test suite
├── tools/
│   └── msvc/                  # Portable MSVC 14.44 + Windows SDK 10.0.26100
├── build.ps1                  # PowerShell build script
├── build.bat                  # Batch build script
├── LICENSE                    # MIT License
└── README.md
```

### Rendering Pipeline

```
PDF File
  │
  ▼
Windows.Data.Pdf (WinRT)   ──→   IPdfRendererNative
  │                                     │
  ▼                                     ▼
PdfPage Object  ──────────────→  ID2D1DeviceContext
                                        │
                                        ▼
                               DXGI Flip-Model Swap Chain
                              (DXGI_SWAP_EFFECT_FLIP_DISCARD)
                                        │
                                        ▼
                                   Desktop Window
```

**Key design decisions:**

- **Zero-copy rendering** — PDF vector commands go directly to the GPU device context; no intermediate bitmaps or temp files.
- **Auto-hiding tab bar** — The 34-DIP Direct2D tab strip only participates in layout when `tabs.size() > 1`, keeping single-document mode pixel-perfect.
- **Isolated STA threads** — Shell file dialogs (`IFileOpenDialog`) and print sheets (`PrintDlgExW`) each run on their own Single-Threaded Apartment worker threads, preventing any UI freeze.
- **Streaming printer** — Pages are rasterized at 300 DPI and spooled one at a time; a 500-page document uses no more memory than a 1-page document.

---

## Building from Source

### Prerequisites

- **Windows 10 or 11** (x64)
- **PowerShell** or **Command Prompt**
- **MSVC C++ compiler** — any of the three options below:

#### Option A: Visual Studio (if already installed)

If you have Visual Studio 2022 (or 2019+) with the **"Desktop development with C++"** workload, you're ready. Open a **Developer Command Prompt** or **Developer PowerShell** and skip to [Build](#build).

#### Option B: VS Build Tools (no IDE)

Download the free [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) installer, select the **"Desktop development with C++"** workload, and install. This gives you `cl.exe`, `link.exe`, and the Windows SDK without the full IDE.

#### Option C: Portable MSVC (zero install, no admin)

If you don't have Visual Studio at all and don't want to install anything system-wide, you can bootstrap a fully portable MSVC compiler + Windows SDK into the `tools/msvc/` directory using [**portable-msvc**](https://github.com/mmozeiko/portable-msvc) by mmozeiko. This downloads the toolchain directly from Microsoft's CDN — no admin privileges, no registry modifications, no Visual Studio installer.

**Requirements:** Python 3.6+

```powershell
# 1. Download the portable-msvc script
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/mmozeiko/portable-msvc/main/portable-msvc.py" -OutFile portable-msvc.py

# 2. Run it — downloads MSVC compiler + Windows SDK into ./msvc/
python portable-msvc.py

# 3. Move the output into the project's tools directory
Move-Item -Path msvc -Destination tools\msvc

# 4. Clean up
Remove-Item portable-msvc.py
```

Or equivalently with `curl`:

```cmd
:: Download
curl -L -o portable-msvc.py https://raw.githubusercontent.com/mmozeiko/portable-msvc/main/portable-msvc.py

:: Run (downloads ~1.5 GB, extracts MSVC 14.x + SDK 10.x into ./msvc/)
python portable-msvc.py

:: Move into project
move msvc tools\msvc

:: Clean up
del portable-msvc.py
```

After this, `tools/msvc/` will contain:
```
tools/msvc/
├── VC/                         # MSVC compiler, linker, headers, libs
│   └── Tools/MSVC/14.x.xxxxx/
├── Windows Kits/               # Windows SDK headers + libs
│   └── 10/
├── activate.ps1                # PowerShell environment activation
├── activate.cmd                # CMD environment activation
├── setup_x64.bat               # x64 native tools setup
└── env.json                    # PATH, INCLUDE, LIB configuration
```

The build scripts (`build.ps1` / `build.bat`) automatically detect and activate this portable toolchain via `tools\msvc\activate.ps1` when `cl.exe` is not already on your `PATH`.

### Build

```powershell
# PowerShell
.\build.ps1
```

```cmd
:: Command Prompt
build.bat
```

Output: `bin\LightPDF.exe` (270 KB)

### Compiler Flags

```
/O2 /MT /std:c++20 /GL /Gy /Gw /EHsc /utf-8 /permissive- /DNOMINMAX
/link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
      /MANIFEST:EMBED /MANIFESTINPUT:resources\app.manifest
```

| Flag | Purpose |
| :--- | :--- |
| `/O2` | Maximum speed optimization |
| `/MT` | Static CRT link — eliminates `VCRUNTIME140.dll` / `MSVCP140.dll` |
| `/GL` + `/LTCG` | Whole-program link-time code generation for cross-file inlining |
| `/OPT:REF` + `/OPT:ICF` | Dead-code elimination + identical COMDAT folding → 270 KB binary |

---

## License

[MIT](LICENSE)

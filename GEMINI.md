# LightPDF Workspace Rules & Development Standards

## 1. Binary Footprint & Size Budget
- **Absolute Ceiling:** 1.5 MB MAX (1,572,864 bytes).
- **Preferred Target:** Under 1 MB (1,048,576 bytes) whenever possible.
- **Exploit Mitigations:** Control Flow Guard (`/guard:cf`, `/GUARD:CF`) and Shadow Stack (`/CETCOMPAT`) must remain enabled unless the 1.5 MB limit is breached.
- Measure size using `Get-Item bin\LightPDF.exe` after clean builds.
- Keep footprint thresholds synchronized in `.github/workflows/build.yml`, `CONTRIBUTING.md`, and `.github/PULL_REQUEST_TEMPLATE.md`.

## 2. Release & Git Remote Synchronization
- Committing and tagging a release locally is not complete until pushed.
- Whenever preparing a release or bumping versions, execute `git push origin main --tags` so GitHub commits, release tags, and automated GitHub Actions workflows (`.github/workflows/release.yml`) remain fully synchronized.

## 3. Documentation, Template & Metadata Consistency
- `README.md` is a customer-facing performance showcase, and sibling repo metadata must stay aligned with it. When code, UI behavior, versions, tests, or compiler flags change:
  - Synchronize header badges and subtitles (version tag, binary size in KB, test counts).
  - Update the Performance Benchmarks table with exact measured values.
  - Update the Compiler & Linker Flags table with all active flags (e.g. `/guard:cf`, `/CETCOMPAT`).
  - Update the Automated Testing section with exact doctest test case and assertion counts.
  - Update the Project Structure tree annotations, Options/feature descriptions, and TOC links.
  - Synchronize `Doxyfile` (`PROJECT_NUMBER`), `CONTRIBUTING.md`, `.github/PULL_REQUEST_TEMPLATE.md`, `.github/ISSUE_TEMPLATE/bug_report.md`, and `.github/workflows/release.yml` fallback tags.

## 4. Doxygen & Code Quality Standards
- Strict Doxygen configuration is enforced (`WARN_IF_DOC_ERROR = YES`, `WARN_NO_PARAMDOC = YES`).
- Every new or modified function, struct, class, and method must have complete Doxygen annotations (`@brief`, `@param`, `@return`).
- Verify zero warnings by running `.\generate_docs.ps1` and checking captured stdout/stderr for `warning:` (Doxygen exits `0` on warnings).
- In `README.md` headings linked from the Table of Contents, avoid multi-codepoint variation-selector emojis (e.g. `ℹ️` / `U+FE0F`) that break Doxygen `\ref` anchor resolution; use single-codepoint emojis so both GitHub and Doxygen resolve `#-...` anchors cleanly.
- Enforce Level 4 compiler diagnostics (`/W4 /permissive-`) with zero compiler warnings across both release builds and unified test builds (`build.ps1`, `tests\run_test.ps1`, `tests\run_tests.bat`).
- All unit tests in `.\tests\run_test.ps1` must pass 100%.

## 5. Build Script Parity & Toolchain Standards
- **Dual Script Parity:** Paired `.ps1`/`.bat` entrypoints (`build.ps1` ↔ `build.bat`, `tests\run_test.ps1` ↔ `tests\run_tests.bat`, `generate_docs.ps1` ↔ `generate_docs.bat`) must maintain toolchain, flag (`/W4 /permissive-`), and working-directory parity (`Set-Location $PSScriptRoot` / `cd /d "%~dp0"`). Any enhancement (Visual Studio auto-detection, MSVC flag updates, resource compiler paths, AVX2 support, CFG/CET security flags) added to one must be mirrored in its counterpart.
- **Safe Packaging Scripts:** `tools\sign.ps1` and release scripts must target exact subdirectories cleanly (`dist\dict\*`) and remove certificates by actual Common Name (`CN`) as well as display labels.
- **Fast Multicore Tooling:** All build and test runners (`build.ps1`, `build.bat`, `tests\run_test.ps1`) must employ multicore compilation (`/MP`) and incremental object caching to avoid unneeded full rebuilds.

## 6. Execution Laziness & UI Responsiveness
- **Zero Eager Heavy Compute:** Intensive processing (OCR, deep content parsing, technical dictionary bulk indexing, searchable PDF baking) must never be executed eagerly during file load or page scroll.
- **Strictly On-Demand:** OCR and deep indexing run only when explicitly requested (e.g., Search trigger or Save As Searchable PDF) and must remain off the primary Direct2D rendering thread.

## 7. UI Shortcuts & Configurable Interaction Semantics
- **Shortcut & Overlay Synchronization:** Keyboard shortcuts are dispatched in `AppWindow::HandleKeyDown` / `WM_SYSKEYDOWN` (`src\app_window.cpp`), not via an `ACCELERATORS` table in `resources\app.rc`. Shortcut labels in `ContextMenuController`, `HelpOverlayView::Render`, and `README.md` must match `src\app_window.cpp`, and `HelpOverlayView` tab count labels and `HelpOverlayLayout::GetCategoryTabRect` widths must stay synchronized with the `ShortcutItem` arrays.
- **Configurable Interaction Semantics:** Interaction behaviors with multiple valid user preferences (such as `rightClickAutoSelect` expanding right-clicks to words vs. opening the document canvas menu) must respect `SettingsController` and remain cleanly configurable via the Options Dialog (`OPT_HIT_RCLICK_SELECT`).

## 8. Implementation Plan Rigor
- When formulating `/plan` artifacts, create detailed, step-by-step blueprints specifying file paths, method signatures, error handling branches, and exact verification criteria to enable flawless execution by any model or subagent.

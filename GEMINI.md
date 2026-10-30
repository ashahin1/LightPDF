# LightPDF Workspace Rules & Development Standards

## 1. Binary Footprint & Size Budget
- **Absolute Ceiling:** 1.5 MB MAX (1,572,864 bytes).
- **Preferred Target:** Under 1 MB (1,048,576 bytes) whenever possible.
- **Exploit Mitigations:** Control Flow Guard (`/guard:cf`, `/GUARD:CF`) and Shadow Stack (`/CETCOMPAT`) must remain enabled unless the 1.5 MB limit is breached.
- Measure size using `Get-Item bin\LightPDF.exe` after clean builds.

## 2. Release & Git Remote Synchronization
- Committing and tagging a release locally is not complete until pushed.
- Whenever preparing a release or bumping versions, execute `git push origin main --tags` so GitHub commits, release tags, and automated GitHub Actions workflows (`.github/workflows/release.yml`) remain fully synchronized.

## 3. Documentation & README Consistency
- `README.md` is a customer-facing performance showcase. When code, tests, or compiler flags change:
  - Synchronize header badges and subtitles (version tag, binary size in KB, test counts).
  - Update the Performance Benchmarks table with exact measured values.
  - Update the Compiler & Linker Flags table with all active flags (e.g. `/guard:cf`, `/CETCOMPAT`).
  - Update the Automated Testing section with exact doctest test case and assertion counts.
  - Update the Project Structure tree annotations.

## 4. Doxygen & Code Quality Standards
- Strict Doxygen configuration is enforced (`WARN_IF_DOC_ERROR = YES`, `WARN_NO_PARAMDOC = YES`).
- Every new or modified function, struct, class, and method must have complete Doxygen annotations (`@brief`, `@param`, `@return`).
- Verify zero warnings by running `.\generate_docs.ps1`.
- Enforce Level 4 compiler diagnostics (`/W4 /permissive-`) with zero compiler warnings.
- All unit tests in `.\tests\run_test.ps1` must pass 100%.

## 5. Build Script Parity & Toolchain Standards
- **Dual Script Parity:** `build.ps1` and `build.bat` must maintain 100% functional and toolchain parity. Any enhancement (Visual Studio auto-detection, MSVC flag updates, resource compiler paths, AVX2 support, CFG/CET security flags) added to one must be mirrored in the other.
- **Fast Multicore Tooling:** All build and test runners (`build.ps1`, `build.bat`, `tests\run_test.ps1`) must employ multicore compilation (`/MP`) and incremental object caching to avoid unneeded full rebuilds.

## 6. Execution Laziness & UI Responsiveness
- **Zero Eager Heavy Compute:** Intensive processing (OCR, deep content parsing, technical dictionary bulk indexing, searchable PDF baking) must never be executed eagerly during file load or page scroll.
- **Strictly On-Demand:** OCR and deep indexing run only when explicitly requested (e.g., Search trigger or Save As Searchable PDF) and must remain off the primary Direct2D rendering thread.

## 7. UI Shortcuts & Configurable Interaction Semantics
- **Accelerator Synchronization:** Shortcut labels displayed in Direct2D custom context menus, toolbars, and help overlays must strictly reflect actual Win32 accelerator definitions in `resources\app.rc`.
- **Configurable Interaction Semantics:** Interaction behaviors with multiple valid user preferences (such as `rightClickAutoSelect` expanding right-clicks to words vs. opening the document canvas menu) must respect `SettingsController` and remain cleanly configurable via the Options Dialog (`OPT_HIT_RCLICK_SELECT`).

## 8. Implementation Plan Rigor
- When formulating `/plan` artifacts, create detailed, step-by-step blueprints specifying file paths, method signatures, error handling branches, and exact verification criteria to enable flawless execution by any model or subagent.

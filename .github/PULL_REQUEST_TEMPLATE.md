## Description
Briefly explain the goal of this pull request and the changes made.

## Type of Change
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Performance optimization / Refactoring
- [ ] Documentation update

## Verification & Testing Checklist
Describe the tests you ran to verify your changes:
- [ ] Rebuilt successfully with `build.bat /rebuild` (0 compiler warnings under `/W4`)
- [ ] Ran automated unified test suite via `cmd.exe /c tests\run_tests.bat` (all 21 test cases passed)
- [ ] Verified standalone binary size remains strictly within budget (< 832 KB)
- [ ] Verified zero external runtime DLL dependencies (only native Windows APIs)
- [ ] Tested on High-DPI monitor (100%, 125%, 150%, or 200%)
- [ ] Verified bilingual compatibility (English & Arabic text rendering / search)

## Additional Notes
Add any other context, performance benchmarks, or before/after measurements if applicable.

$ErrorActionPreference = "Stop"
. '.\tools\msvc\activate.ps1'

$srcs = @(
    "tests\verify_searchable_pdf.cpp",
    "src\pdf_parser.cpp",
    "src\pdf_searchable_writer.cpp",
    "src\pdf_search.cpp"
)

$compileArgs = @(
    "/nologo",
    "/std:c++20",
    "/EHsc",
    "/utf-8",
    "/DNOMINMAX",
    "/I", "src"
) + $srcs + @(
    "/link",
    "/MANIFEST:EMBED",
    "/MANIFESTINPUT:resources\app.manifest",
    "windows.data.pdf.lib",
    "windowsapp.lib",
    "user32.lib",
    "gdi32.lib",
    "shell32.lib",
    "comctl32.lib",
    "/OUT:tests\verify_searchable_pdf.exe"
)

& cl.exe $compileArgs
if ($LASTEXITCODE -eq 0) {
    & ".\tests\verify_searchable_pdf.exe"
}

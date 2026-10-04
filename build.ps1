param([switch]$Preview)

$ErrorActionPreference = 'Stop'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw "Visual Studio Developer Command Prompt not found: $vsDevCmd" }
New-Item -ItemType Directory -Force -Path 'build' | Out-Null
$source = 'src\main.cpp src\app.cpp src\win32_ui.cpp src\backtest_service.cpp src\analytics.cpp src\data.cpp src\strategy.cpp src\json.cpp'
$previewDefine = if ($Preview) { '/DARBITRAGE_UI_PREVIEW' } else { '' }
$output = if ($Preview) { 'build\ArbitrageToolPreview.exe' } else { 'build\ArbitrageTool.exe' }
$cmd = "call `"$vsDevCmd`" -arch=x64 & cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS $previewDefine $source /I src /Fe:$output /link /SUBSYSTEM:WINDOWS winhttp.lib comctl32.lib shlwapi.lib user32.lib gdi32.lib ole32.lib shell32.lib uxtheme.lib"
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw "MSVC build failed with exit code $LASTEXITCODE" }

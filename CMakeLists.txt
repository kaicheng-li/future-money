cmake_minimum_required(VERSION 3.25)
project(ArbitrageTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_UNICODE TRUE)

add_executable(ArbitrageTool WIN32
    src/main.cpp
    src/app.cpp
    src/win32_ui.cpp
    src/backtest_service.cpp
    src/analytics.cpp
    src/data.cpp
    src/strategy.cpp
    src/json.cpp
)

target_compile_definitions(ArbitrageTool PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(ArbitrageTool PRIVATE winhttp comctl32 shlwapi user32 gdi32 ole32 shell32 uxtheme)

if (MSVC)
    target_compile_options(ArbitrageTool PRIVATE /W4 /permissive-)
endif()

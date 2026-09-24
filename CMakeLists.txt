cmake_minimum_required(VERSION 3.24)

project(
    MyProject
    VERSION 0.1.0
    LANGUAGES CXX
)

# =========================
# C++ 标准
# =========================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()
# =========================
# 获取 wxWidgets
# =========================

include(FetchContent)

FetchContent_Declare(
    wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.11/wxWidgets-3.2.11.zip
    TLS_VERIFY OFF   # 忽略 SSL 证书验证错误
)
FetchContent_MakeAvailable(wxWidgets)


# =========================
# 你的程序
# =========================

add_executable(
    MyProject
    WIN32
    src/main.cpp
    src/ExplorerPane.cpp
)


# =========================
# 链接 wxWidgets
# =========================

target_link_libraries(
    MyProject

    PRIVATE

    wx::core
    wx::base
)
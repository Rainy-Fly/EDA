set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  #在build下生成compile_commands.json文件,使得VScode识别到<wx/wx.h>头文件,避免谎报军情
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


# =========================
# 获取 wxWidgets
# =========================

include(FetchContent)

FetchContent_Declare(
    wxWidgets

    GIT_REPOSITORY
        https://github.com/wxWidgets/wxWidgets.git

    GIT_TAG
        v3.2.11
)

FetchContent_MakeAvailable(wxWidgets)


# =========================
# 识别源文件
# =========================

file(GLOB_RECURSE SOURCES "src/*.cpp" )  #识别所有cpp文件

add_executable(
    TinyEDA

    ${SOURCES}
)


# =========================
# 链接 wxWidgets
# =========================

target_link_libraries(
    TinyEDA

    PRIVATE

    wx::core
    wx::base
)
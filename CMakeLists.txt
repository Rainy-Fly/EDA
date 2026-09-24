cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(
    TinyEDA
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
# 获取第三方依赖
# =========================

include(FetchContent)

# wxWidgets 自己的 CMake 版本规则问题
set(CMAKE_POLICY_DEFAULT_CMP0219 OLD)


# ---------- wxWidgets ----------

FetchContent_Declare(
    wxWidgets

    GIT_REPOSITORY
        https://github.com/wxWidgets/wxWidgets.git

    GIT_TAG
        v3.2.11
)


# ---------- JsonCpp ----------

FetchContent_Declare(
    jsoncpp

    GIT_REPOSITORY
        https://github.com/open-source-parsers/jsoncpp.git

    GIT_TAG
        1.9.6
)


FetchContent_MakeAvailable(
    wxWidgets
    jsoncpp
)


# =========================
# 源文件
# =========================

option(
    TEST
    "build test files rather than src"
    OFF
)

if(TEST)

    file(
        GLOB_RECURSE SOURCES
        "tests/*.cpp"
    )

else()

    file(
        GLOB_RECURSE SOURCES
        "src/*.cpp"
    )

endif()


# =========================
# TinyEDA
# =========================

add_executable(
    TinyEDA
    ${SOURCES}
)


# =========================
# 链接第三方库
# =========================

target_link_libraries(
    TinyEDA

    PRIVATE

    wx::core
    wx::base
    jsoncpp_lib
)
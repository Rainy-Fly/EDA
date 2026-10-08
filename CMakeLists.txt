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

# 源码是UTF-8(无BOM)且含大量中文字面量：不指定时MSVC按本地代码页解析，会乱码/报警告
if(MSVC)
    add_compile_options(/utf-8)
endif()


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

file(
    GLOB_RECURSE SOURCES
    "src/*.cpp"
)

# 本机工作区里保留的文件（不进版本库、或已由远端删除）需要排除，否则会与其他文件重复定义符号：
#   /ExplorerBak/  本地备份目录（里面是 ExplorerPane 的一份副本，与 src/Explorer/ 下的重复）
#   /main\\.cpp$   本地测试用入口（与 demoApp.cpp 里的 wxIMPLEMENT_APP 重复）
list(FILTER SOURCES EXCLUDE REGEX "/ExplorerBak/")
list(FILTER SOURCES EXCLUDE REGEX "/main\\.cpp$")

# =========================
# TinyEDA
# =========================



add_executable(
    TinyEDA
    # WIN32：wxWidgets 在MSW下提供的是 WinMain（wxIMPLEMENT_APP），
    # 不加这一项会按控制台子系统链接，报 LNK2019 无法解析的外部符号 main，
    # 同时程序运行时会多弹一个控制台窗口
    WIN32
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
    # 项目文件（.xml）读写用 wxXmlDocument；wxWidgets 把 XML 支持放在独立的 xml 组件里，
    # 不链这个库会出现 wxXmlDocument/wxXmlNode 的 LNK2019 无法解析的外部符号
    wx::xml
    jsoncpp_lib
)


# ============================================================================
# 项目文件（XML）读写自检程序：只用于验证 src/io/CircuitFile 的读写实现。
# 控制台程序（不加 WIN32）：自己提供 main，因此排除定义了 wxIMPLEMENT_APP 的 demoApp.cpp。
# 自检源码 tests/xml_selftest.cpp 被 .gitignore 忽略（属本地验证工具，不在版本库里），
# 所以这里用 EXISTS 护栏：文件不存在时（队友克隆下来的环境）直接跳过，不会让 cmake 配置失败。
# 用法：cmake --build build --config Debug --target xml_selftest 然后运行
#       build/Debug/xml_selftest.exe（需要把 wx 的 DLL 目录加进 PATH）
# ============================================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/xml_selftest.cpp")

    file(
        GLOB_RECURSE SELFTEST_SOURCES
        "src/*.cpp"
    )

    list(FILTER SELFTEST_SOURCES EXCLUDE REGEX "/demoApp\\.cpp$")

    add_executable(
        xml_selftest
        ${SELFTEST_SOURCES}
        tests/xml_selftest.cpp
    )

    target_link_libraries(
        xml_selftest

        PRIVATE

        wx::core
        wx::base
        wx::xml
        jsoncpp_lib
    )

endif()
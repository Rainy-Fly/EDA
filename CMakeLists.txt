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


# ---------- md4c（Markdown 解析，AI 回复渲染成 HTML） ----------
# md4c-html 把 Markdown 转成 HTML 片段，喂给聊天气泡的 wxHtmlWindow。
# 用法：链接 md4c-html（自动带上 md4c），#include <md4c-html.h>，调 md_html()

FetchContent_Declare(
    md4c

    GIT_REPOSITORY
        https://github.com/mity/md4c.git

    GIT_TAG
        release-0.5.3
)


FetchContent_MakeAvailable(
    wxWidgets
    jsoncpp
    md4c
)


# =========================
# 源文件
# =========================

file(
    # CONFIGURE_DEPENDS：让构建前自动重新扫描目录。
    # 不加的话，GLOB 只在“配置”那一刻扫一次，之后新增/删除的 .cpp 不会被收进工程，
    # 表现为链接时报找不到符号或功能静默缺失（必须手动再跑一次 cmake -S . -B build）
    GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

# 以下文件要排除，否则会与其他文件重复定义符号、链接失败：
#   /ExplorerBak/      本机工作区的备份目录（ExplorerPane 的副本，与 src/Explorer/ 下的重复）
#   /main\\.cpp$       本机测试用入口（与 demoApp.cpp 里的 wxIMPLEMENT_APP 重复）
#   /UI_frame_1\\.cpp$ 队友提交里的分步教程式示例：自带 wxIMPLEMENT_APP(MyApp) 与 MyFrame，
#                      与 demoApp.cpp 的应用入口、demoMainFrame 的主窗口重复。
#                      它是否取代 demoApp/demoMainFrame 需要团队决定；在决定前先不参与构建，
#                      文件仍保留在版本库里（需要时删掉这一行即可启用）
list(FILTER SOURCES EXCLUDE REGEX "/ExplorerBak/")
list(FILTER SOURCES EXCLUDE REGEX "/main\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "/UI_frame_1\\.cpp$")

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
    # 聊天界面用 wxHtmlWindow（Agent 面板），HTML 渲染在独立的 html 组件里
    wx::html
    jsoncpp_lib
    curl      # Agent 的 HTTPS 通信（DeepSeek /chat/completions）
    md4c-html # AI 回复的 Markdown -> HTML 渲染（ChatPanel）
)


# ============================================================================
# 项目文件（XML）读写自检程序：验证 src/DealProjectXML 的读写实现是否符合 README 的规范，
# 以及 src/Settings 的配置文件读取与本机路径是否正常（只读用户配置，不会改写）。
# 控制台程序（不加 WIN32）：自己提供 main，因此排除定义了 wxIMPLEMENT_APP 的 demoApp.cpp。
# 自检源码 tests/xml_selftest.cpp 被 .gitignore 忽略（属本地验证工具，不在版本库里），
# 所以这里用 EXISTS 护栏：文件不存在时（队友克隆下来的环境）直接跳过，不会让 cmake 配置失败。
# 用法：cmake --build build --config Debug --target xml_selftest 然后运行
#       build/Debug/xml_selftest.exe（需要把 wx 的 DLL 目录加进 PATH）
# ============================================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/xml_selftest.cpp")

    # 复用主程序那份源文件列表（它已经排除了本机的备份/测试入口等文件），
    # 只再排除定义 wxIMPLEMENT_APP 的 demoApp.cpp（自检程序自带 main）
    set(SELFTEST_SOURCES ${SOURCES})
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
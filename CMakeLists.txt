cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(
    TinyEDA
    VERSION 0.1.0
    LANGUAGES CXX
)

# Windows 下要往 exe 里嵌一个应用程序清单（src/TinyEDA.rc → src/TinyEDA.manifest），
# 声明依赖 comctl32 v6：控件才用现代主题外观，wxButton::SetBitmap 依赖的
# BCM_SETIMAGELIST 才被支持（否则左侧调色板的图标不显示）。所以要启用 RC 语言。
if(WIN32)
    enable_language(RC)
endif()

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


# ---------- libcurl（Agent 的 HTTPS 通信，仅 Windows 需要自己拉源码编） ----------
# Windows 上用的是独立安装的 MinGW-w64（C:\mingw64），它只带编译器：
# 既没有 /usr/include/curl/curl.h，也没有 libcurl.a/dll.a。所以原来链接列表里的
# `curl` 在 Linux 上靠系统装的 libcurl 能过，到 Windows 就直接
# “fatal error: curl/curl.h: No such file or directory”。
# 这里在 Windows 下用 FetchContent 拉 curl 源码自己编一个**静态** libcurl：
# TLS 用 Windows 自带的 Schannel（走系统证书库），不需要 OpenSSL/zlib，
# 也不用装 MSYS2/vcpkg，静态库还省掉了运行时要找 libcurl.dll 的麻烦。
# Linux/macOS 保持原样：仍然用系统安装的 libcurl（-lcurl），不改动队友的构建方式。
if(WIN32)

    # 下面这些是给 curl 子工程看的**普通变量**（不写进 CMake 缓存）：
    # 只编静态库，不要 curl 命令行工具、测试、示例、man 文档。
    # 特意不用 CACHE ... FORCE：BUILD_SHARED_LIBS 是全局缓存变量，
    # 一旦写进缓存，下一次 configure 时上面 wxWidgets/jsoncpp（默认跟着它走）
    # 会跟着变成静态库，触发一次毫无必要的全量重编。
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_USE_LIBPSL OFF)     # 本机没有 libpsl，关掉省得去找
    set(CURL_USE_SCHANNEL ON)    # 用 Windows 原生 TLS，证书直接读系统证书库
    set(CURL_WINDOWS_SSPI ON)

    FetchContent_Declare(
        curl

        GIT_REPOSITORY
            https://github.com/curl/curl.git

        GIT_TAG
            curl-8_23_0

        GIT_SHALLOW
            TRUE

        # curl 仓库里没有子模块，置空可以跳过 clone 之后那次
        # `git submodule update --recursive --init`（少拉起一个 shell 子进程）
        GIT_SUBMODULES
            ""
    )

    FetchContent_MakeAvailable(curl)

    # curl 各版本导出的目标名不完全一致（CURL::libcurl / libcurl / libcurl_static），
    # 取第一个存在的，避免换个版本就链接失败
    if(TARGET CURL::libcurl)
        set(TINYEDA_CURL_TARGET CURL::libcurl)
    elseif(TARGET libcurl)
        set(TINYEDA_CURL_TARGET libcurl)
    elseif(TARGET libcurl_static)
        set(TINYEDA_CURL_TARGET libcurl_static)
    else()
        set(TINYEDA_CURL_TARGET curl)
    endif()

else()

    # Linux/macOS：系统包管理器装的 libcurl（原来就是这么链的）
    set(TINYEDA_CURL_TARGET curl)

endif()


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

if(WIN32)

    # 资源脚本：只往 exe 里嵌 comctl32 v6 清单（见 src/TinyEDA.rc / src/TinyEDA.manifest）。
    # -I 指向 src/ 是让 rc 编译器能按名字找到清单文件（rc.exe 和 windres 都接受 -I）
    target_sources(TinyEDA PRIVATE src/TinyEDA.rc)
    set_source_files_properties(src/TinyEDA.rc PROPERTIES
        COMPILE_FLAGS "-I\"${CMAKE_CURRENT_SOURCE_DIR}/src\"")

endif()


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
    # Agent 的 HTTPS 通信（DeepSeek /chat/completions）：
    # Windows 下是上面 FetchContent 编出来的静态 libcurl，Linux 下是系统 libcurl
    ${TINYEDA_CURL_TARGET}
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

# 网表 JSON 的独立契约自检：不依赖 GUI 或项目 XML 实现。
include(CTest)
if(BUILD_TESTING)
    add_executable(
        netlist_selftest
        src/Netlist/NetlistJson.cpp
        src/Netlist/NetlistValidator.cpp
        src/Netlist/NetlistBuilder.cpp
        tests/netlist_selftest.cpp
    )
    target_link_libraries(netlist_selftest PRIVATE jsoncpp_lib)
    add_test(NAME netlist_selftest COMMAND netlist_selftest)
endif()
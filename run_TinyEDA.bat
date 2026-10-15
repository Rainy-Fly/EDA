@echo off
rem ============================================================
rem  TinyEDA 启动脚本（在资源管理器里双击本文件即可）
rem
rem  它做三件事：
rem   1) 把工作目录切到仓库根。程序按「当前目录」或「exe 目录的上一级」查找
rem      src/metadata/*.json 与元件 SVG；直接在资源管理器里双击
rem      build\Debug\TinyEDA.exe 时工作目录是 build\Debug，两处都落空，
rem      表现为「没有元件图标、属性栏没有元数据」（程序静默容错，不报错，不容易发现）。
rem   2) exe 旁边若缺 wxWidgets / jsoncpp / md4c 的 DLL（重新 clone 或清理构建目录后常见），
rem      自动从 build\_deps 里补一份。
rem   3) 启动 exe。
rem
rem  也可以在命令行里运行本脚本，效果一样。
rem  注意：本文件必须保存为 GBK/ANSI 编码，cmd 才能正确解析里面的中文。
rem ============================================================

rem 切到本脚本所在目录 = 仓库根
cd /d "%~dp0"

rem ---- 找可执行文件 ----
rem 多配置生成器（Visual Studio 等）的产物在 Debug/Release 子目录下；
rem 单配置生成器（MinGW Makefiles、Ninja）的产物直接在 build\ 下
set "EXE=build\Debug\TinyEDA.exe"
set "JSONCFG=Debug"
if not exist "%EXE%" (
    set "EXE=build\Release\TinyEDA.exe"
    set "JSONCFG=Release"
)
if not exist "%EXE%" (
    set "EXE=build\TinyEDA.exe"
    set "JSONCFG="
)

if not exist "%EXE%" (
    echo [错误] 还没构建过，找不到 TinyEDA.exe
    echo        多配置生成器（VS）在 build\Debug\ 或 build\Release\ 下
    echo        单配置生成器（MinGW/Ninja）在 build\ 下
    echo        请先在仓库根执行：
    echo            cmake -S . -B build
    echo            cmake --build build --target TinyEDA
    echo        多配置生成器再加 --config Debug（放在 --build build 后面）
    echo.
    pause
    exit /b 1
)

rem exe 所在目录（带结尾反斜杠）
for %%I in ("%EXE%") do set "EXEDIR=%%~dpI"

rem ---- 补齐 DLL：exe 旁边没有 wxbase 就认为没复制过 ----
rem 两种工具链的 DLL 目录名不同：MSVC 是 lib\vc_x64_dll，MinGW-w64 是 lib\gcc_x64_dll
set "WXDIR="
if exist "build\_deps\wxwidgets-build\lib\vc_x64_dll\wxbase32ud_vc_x64_custom.dll" set "WXDIR=build\_deps\wxwidgets-build\lib\vc_x64_dll"
if exist "build\_deps\wxwidgets-build\lib\gcc_x64_dll\wxbase32ud_gcc_x64_custom.dll" set "WXDIR=build\_deps\wxwidgets-build\lib\gcc_x64_dll"
if not exist "%EXEDIR%wxbase32ud_vc_x64_custom.dll" if not exist "%EXEDIR%wxbase32ud_gcc_x64_custom.dll" (
    if defined WXDIR (
        echo 正在复制 wxWidgets 的 DLL 到 %EXEDIR%
        copy /y "%WXDIR%\*.dll" "%EXEDIR%" >nul
    ) else (
        echo [警告] 没找到 wxWidgets 的 DLL，程序可能启动失败
    )
)

rem jsoncpp：MSVC 多配置在 lib_json\<Debug|Release>\jsoncpp.dll，MinGW 单配置在 lib_json\libjsoncpp.dll
set "JSONDLL="
if defined JSONCFG if exist "build\_deps\jsoncpp-build\src\lib_json\%JSONCFG%\jsoncpp.dll" set "JSONDLL=build\_deps\jsoncpp-build\src\lib_json\%JSONCFG%\jsoncpp.dll"
if exist "build\_deps\jsoncpp-build\src\lib_json\libjsoncpp.dll" set "JSONDLL=build\_deps\jsoncpp-build\src\lib_json\libjsoncpp.dll"
if not exist "%EXEDIR%jsoncpp.dll" if not exist "%EXEDIR%libjsoncpp.dll" (
    if defined JSONDLL copy /y "%JSONDLL%" "%EXEDIR%" >nul
)

rem md4c：Agent 聊天面板把 Markdown 渲染成 HTML 用的（MSVC 是 md4c-html.dll，MinGW 是 libmd4c-html.dll）
set "MD4CDIR="
if exist "build\_deps\md4c-build\src\libmd4c-html.dll" set "MD4CDIR=build\_deps\md4c-build\src"
if exist "build\_deps\md4c-build\src\Debug\md4c-html.dll" set "MD4CDIR=build\_deps\md4c-build\src\Debug"
if exist "build\_deps\md4c-build\src\Release\md4c-html.dll" set "MD4CDIR=build\_deps\md4c-build\src\Release"
if not exist "%EXEDIR%libmd4c-html.dll" if not exist "%EXEDIR%md4c-html.dll" (
    if defined MD4CDIR (
        copy /y "%MD4CDIR%\*md4c*.dll" "%EXEDIR%" >nul
    )
)

echo 启动 %EXE%（工作目录：%CD%）
start "" "%~dp0%EXE%"
exit /b 0

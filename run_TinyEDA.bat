@echo off
rem ============================================================
rem  TinyEDA 启动脚本（在资源管理器里双击本文件即可）
rem
rem  它做三件事：
rem   1) 把工作目录切到仓库根。程序按「当前目录」或「exe 目录的上一级」查找
rem      src/metadata/*.json 与元件 SVG；直接在资源管理器里双击
rem      build\Debug\TinyEDA.exe 时工作目录是 build\Debug，两处都落空，
rem      表现为「没有元件图标、属性栏没有元数据」（程序静默容错，不报错，不容易发现）。
rem   2) exe 旁边若缺 wxWidgets / jsoncpp 的 DLL（重新 clone 或清理构建目录后常见），
rem      自动从 build\_deps 里补一份。
rem   3) 启动 exe（优先 Debug，其次 Release）。
rem
rem  也可以在命令行里运行本脚本，效果一样。
rem  注意：本文件必须保存为 GBK/ANSI 编码，cmd 才能正确解析里面的中文。
rem ============================================================

rem 切到本脚本所在目录 = 仓库根
cd /d "%~dp0"

rem ---- 找可执行文件：优先 Debug，其次 Release ----
set "EXE=build\Debug\TinyEDA.exe"
set "JSONCFG=Debug"
if not exist "%EXE%" (
    set "EXE=build\Release\TinyEDA.exe"
    set "JSONCFG=Release"
)

if not exist "%EXE%" (
    echo [错误] 还没构建过，找不到 build\Debug\TinyEDA.exe 或 build\Release\TinyEDA.exe
    echo        请先在仓库根执行：
    echo            cmake -S . -B build
    echo            cmake --build build --config Debug --target TinyEDA
    echo.
    pause
    exit /b 1
)

rem exe 所在目录（带结尾反斜杠）
for %%I in ("%EXE%") do set "EXEDIR=%%~dpI"

rem ---- 补齐 DLL：exe 旁边没有 wxbase 就认为没复制过 ----
if not exist "%EXEDIR%wxbase32ud_vc_x64_custom.dll" (
    if exist "build\_deps\wxwidgets-build\lib\vc_x64_dll\*.dll" (
        echo 正在复制 wxWidgets 的 DLL 到 %EXEDIR%
        copy /y "build\_deps\wxwidgets-build\lib\vc_x64_dll\*.dll" "%EXEDIR%" >nul
    ) else (
        echo [警告] 没找到 wxWidgets 的 DLL，程序可能启动失败
    )
)
if not exist "%EXEDIR%jsoncpp.dll" (
    if exist "build\_deps\jsoncpp-build\src\lib_json\%JSONCFG%\jsoncpp.dll" (
        copy /y "build\_deps\jsoncpp-build\src\lib_json\%JSONCFG%\jsoncpp.dll" "%EXEDIR%" >nul
    )
)

echo 启动 %EXE%（工作目录：%CD%）
start "" "%~dp0%EXE%"
exit /b 0

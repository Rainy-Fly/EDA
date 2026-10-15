# 临时脚本（验证用，用完会删）：后台完整构建 TinyEDA（含 wxWidgets 依赖），
# 日志与退出码写到文件，便于构建期间不占用前台终端。
Set-Location 'C:\Users\86199\Desktop\QD-LEARNING\EDA'
if (Test-Path build_done.txt) { Remove-Item build_done.txt -Force }
& cmake --build build --parallel 6 2>&1 | Out-File -Encoding utf8 build_log.txt
"EXIT=$LASTEXITCODE" | Out-File -Encoding ascii build_done.txt

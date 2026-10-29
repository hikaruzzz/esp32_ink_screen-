# 本项目固定的 ESP-IDF 环境激活脚本。
# 用法（PowerShell）: . G:\tools\workbuddy\esp32\tools\idf-env.ps1
# 之后在本会话中直接使用 idf.py。
#
# 背景：ESP-IDF 装在 C:\Espressif（非默认 %USERPROFILE%\.espressif），
# 且本项目由 Git Bash 调起 PowerShell（会带入 MSYSTEM，IDF 6.x 拒绝运行）。

$env:IDF_PATH = 'C:\Espressif\v6.1\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'

Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue

. 'C:\Espressif\v6.1\esp-idf\export.ps1'

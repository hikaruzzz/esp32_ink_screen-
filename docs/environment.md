# 开发环境

记录日期：2026-10-30

## 已确认配置

| 项 | 值 |
| --- | --- |
| 操作系统 | Windows 10 Pro 10.0.19045 |
| ESP-IDF | **v6.1**（官方 release 包，对应 GitHub tag `v6.1`，commit `fff9895c82d744c7237be8847347bdd1b07c6643`） |
| IDF 路径 | `C:\Espressif\v6.1\esp-idf`（无 `.git`，版本以 `tools/cmake/version.cmake` = 6.1.0 为准） |
| 工具路径 | `C:\Espressif\tools`（`IDF_TOOLS_PATH`，**不是**默认的 `%USERPROFILE%\.espressif`） |
| Python 环境 | `C:\Espressif\tools\python_env\idf6.1_py3.14_env`（系统 Python 3.14.3 创建，EIM 检测通过） |
| 安装方式 | 官方 v6.1 完整发布包（含全部子模块）→ 解压；`idf_tools.py install` 装工具链 |
| 下载通道 | 包与工具均走乐鑫 CDN `dl.espressif.com/github_assets`；PyPI 用清华镜像 |
| 激活方式 | PowerShell：`. G:\tools\workbuddy\esp32\tools\idf-env.ps1` |
| 板卡串口 | **COM4**（原生 USB Serial/JTAG，`USB\VID_303A&PID_1001`） |
| Flash 下载方式 | USB-Serial-JTAG，esptool 自动复位，无需按键 |
| 构建配置 | 主控制台 = USB Serial/JTAG（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`），Flash 4 MB |

## VS Code 扩展

- 扩展：`espressif.esp-idf-extension` **v2.3.0**（2026-10-30 安装）
- 扩展通过 EIM 注册表识别环境：`C:\Espressif\tools\eim_idf.json`
  （已修正为 `status: finished`、`version: v6.1`，`idfSelectedId` 指向该条目）
- 用户设置（`%APPDATA%\Code\User\settings.json`）：
  - `idf.eimIdfJsonPath` = `C:\Espressif\tools\eim_idf.json`
  - `idf.currentSetup` = `C:\Espressif\v6.1\esp-idf`
  - `idf.port` = `COM4`
- 扩展只在打开 IDF 工程目录时启用构建/烧录面板；M0 工程位于 `C:\esp\c3-ai-led-demo`。
  作为参考项目仓库打开时（本仓库无 `sdkconfig`）日志中的 `Cannot access sdkconfig` 属预期无害。

## 本机网络约束（重要）

- `github.com` 的 git 连接超时（21120 ms），EIM 默认在线安装会失败；
  EIM 的 gix 克隆子模块还因 `C:\Program Files` 路径含空格而失败。
- 已验证替代通道：Gitee 官方镜像（commit 与 GitHub 一致）、乐鑫 CDN 资产代理、
  清华 PyPI。均只用于安装，不影响固件来源校验（release 包来自乐鑫官方 CDN）。
- Git Bash 会设置 `MSYSTEM`，ESP-IDF 6.x 明确拒绝在 MSys/Mingw 下运行；
  `tools/idf-env.ps1` 已在内部清除。所有 `idf.py` 命令必须经该脚本在 PowerShell 中执行。

## 使用约定

- AI 执行编译/烧录必须走 `tools/idf-env.ps1`，与 VS Code 扩展使用同一套 IDF 与工具链。
- 不为了修编译错误更换 SDK 主版本、目标芯片或驱动体系。
- `set-target esp32c3` 只在初次设置或有意切换芯片时执行。
- 串口由单个进程独占；烧录前关闭 monitor；日志采集必须有结束条件（`tools/capture.py`）。

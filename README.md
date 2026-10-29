# ESP32-C3 联网传感器终端（开发中）

项目唯一人工入口。开发方案见 [esp32c3_ai_development_plan.md](esp32c3_ai_development_plan.md)。

## 当前阶段

**M0 环境与板卡（2026-10-30 完成）**

- [x] 板卡建档 —— [docs/board.md](docs/board.md)
- [x] 固定 ESP-IDF 版本（v6.1.0）—— [docs/environment.md](docs/environment.md)
- [x] 引脚确认（LED/USB 已实测，I2C 待定）—— [docs/pinmap.md](docs/pinmap.md)
- [x] 官方 hello_world 编译 / 烧录 / 复位启动
- [x] 启动日志与证据归档 —— [evidence/M0/summary.md](evidence/M0/summary.md)
- [x] LED GPIO8 500 ms 翻转 + 状态日志（物理目视待确认）
- [x] VS Code ESP-IDF 扩展（v2.3.0，指向现有环境）
- [x] Git 基线（`76f9e52`）

## 目录

| 路径 | 内容 |
| --- | --- |
| AGENTS.md | 给 AI 编码工具的项目约束（新会话先读它） |
| docs/ | 需求、硬件、环境、引脚、验收、当前状态 |
| docs/current_state.md | 当前有效状态：最近可用提交、已过/未过检查、下一步 |
| tools/ | 构建、烧录、限时日志采集、测试入口（M3 建立） |
| tests/ | 主机测试（host/）与实板测试（hil/） |
| firmware | 固件工程（M1 建立；M0 基线用独立目录，见下） |

## 构建与烧录

环境：ESP-IDF v6.1 @ `C:\Espressif\v6.1\esp-idf`，工具 @ `C:\Espressif\tools`，
Python env `idf6.1_py3.14_env`。板卡串口 **COM4**（原生 USB Serial/JTAG）。

所有命令在 PowerShell 中执行，先激活项目环境：

```powershell
. G:\tools\workbuddy\esp32\tools\idf-env.ps1
```

M0 基线：官方 `hello_world` 副本 @ `C:\esp\c3-ai-demo`（已加 `sdkconfig.defaults`：
USB Serial/JTAG 控制台 + 4 MB Flash）。

```powershell
Set-Location C:\esp\c3-ai-demo
idf.py build
idf.py -p COM4 flash
```

LED 验证工程：`C:\esp\c3-ai-led-demo`（GPIO8 每 500 ms 翻转，状态日志走 USB 控制台）。

限时日志采集（不占用交互式 monitor；`--reset` 用 RTS/DTR 复位后再采集）：

```powershell
python G:\tools\workbuddy\esp32\tools\capture.py --port COM4 --reset --seconds 8 --out <日志路径>
```

证据目录：`evidence/M0/`，验证结论见 [evidence/M0/summary.md](evidence/M0/summary.md)。

## 文档导航

- 需求：[docs/requirements.md](docs/requirements.md)（待产品功能确认后建立）
- 开发板：[docs/board.md](docs/board.md)
- 引脚：[docs/pinmap.md](docs/pinmap.md)
- 环境：[docs/environment.md](docs/environment.md)
- 验收：[docs/acceptance.md](docs/acceptance.md)（M1 前建立）
- 当前状态：[docs/current_state.md](docs/current_state.md)

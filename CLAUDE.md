# 项目上下文入口（Claude Code 自动加载）

权威状态见 [docs/current_state.md](docs/current_state.md)，方案见
[esp32c3_ai_development_plan.md](esp32c3_ai_development_plan.md)。

## 开始任务前必读

1. [README.md](README.md)、[AGENTS.md](AGENTS.md)、[docs/current_state.md](docs/current_state.md)
2. 涉及硬件： [docs/board.md](docs/board.md)、[docs/pinmap.md](docs/pinmap.md)
3. 涉及环境/构建： [docs/environment.md](docs/environment.md)

## 环境（必须遵守）

- 所有 `idf.py` 命令在 **PowerShell** 中执行，先激活：
  `. G:\tools\workbuddy\esp32\tools\idf-env.ps1`
- ESP-IDF v6.1 @ `C:\Espressif\v6.1\esp-idf`，工具 @ `C:\Espressif\tools`
- 板卡串口 **COM4**（原生 USB Serial/JTAG；重新插拔后编号可能变化，以设备枚举为准）
- 主机测试：`powershell -ExecutionPolicy Bypass -File tools\test-host.ps1`
- 不要用 Git Bash 直接跑 `idf.py`（`MSYSTEM` 会被 IDF 拒绝）
- **不得猜测 GPIO**：引脚只来自 docs/pinmap.md，未知标“待确认”
- 网络限制与镜像用法见 docs/environment.md

## 工作方式

- 小步实现；编译通过、主机测试、实板测试分开报告
- 修故障先给“观察事实—根因假设—区分检查—最小修复”
- 每次结束更新 docs/current_state.md 并建立 Git 检查点

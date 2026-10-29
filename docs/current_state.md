# 当前状态

更新：2026-10-30

- 最后可运行提交：`aa092a6`（本次传感器框架与主机测试随后提交）
- 固件工程：仓库根目录；项目名 `c3_ai`；分层 `board` / `app_logic` / `sensor` / `main`
- 固件身份（当前构建）：
  - `build/c3_ai.bin` `ac7859b0398a3ebefa4e02107d16133a2393a0b948952fcdb9bc70254b831b80`
  - `build/c3_ai.elf` `b6b4a06c5d1aa995359a10ea2d787c87aa29c2312774c3c271bc2278dfb4d6e2`
- 已通过检查：
  - M0 全部（`evidence/M0/summary.md`）
  - M1 按键与模式、心跳（`evidence/M1/`、`docs/acceptance.md`）
  - M1-08 主机测试 2/2（app_mode、sensor_mock）—— `tools/test-host.ps1`
  - M1-10 虚拟传感器 2 s 采样；M1-11 传感器缺失时 `UNSUPPORTED` 且本地功能继续
- 未运行 / 阻塞：
  - M1-04 去抖抖动测试、M1-06 过短按压（<20 ms）
  - M1-09 1 小时长稳
  - 真实 I2C 驱动 — 阻塞：传感器型号与 I2C 引脚未确认（`board.h` 中为 -1）
- 下一步：
  1. 用户确定传感器型号与 I2C 引脚 → 实现 `sensor_i2c.c`（总线初始化/探测/读取）
  2. 补 M1-04 / M1-06 测试
  3. 执行 1 小时长稳并归档心跳曲线

# 当前状态

更新：2026-10-30

- 最后可运行提交：无（Git 尚未初始化）
- 固件身份：
  - hello_world.bin `2630c77a020fadb39cebde0eb9f29c5274b381734c3f43763933434821c48a6d`
  - led_blink.bin `58985bb243118b5b9610930b443b1b0c83f54a7e60ab3b54607feb0b1f280b23`
- 已通过检查：
  - 环境：ESP-IDF v6.1.0 安装并激活（`tools/idf-env.ps1`）
  - 板卡：COM4 原生 USB Serial/JTAG；ESP32-C3 QFN32 v0.4；内嵌 4 MB XMC Flash；MAC 48:f6:ee:15:78:c8
  - hello_world：编译、烧录（哈希校验）、USB 启动日志 —— PASS
  - LED（GPIO8，低有效）：编译、烧录、500 ms 翻转日志（8 s 内 19 条事件，无 panic/watchdog）、用户目视闪烁确认 —— PASS
  - 证据：`evidence/M0/`（含 summary.md）
- 未运行 / 阻塞：无（M0 完成）
- 下一步：
  1. M1：按键（GPIO9 需先实测）→ I2C 传感器选型与接线确认
  2. 扩展功能前先保持 Git 检查点习惯

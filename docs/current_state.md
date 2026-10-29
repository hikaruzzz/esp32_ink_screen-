# 当前状态

更新：2026-10-30

- 最后可运行提交：`983b7e4`（本次迁移随工作区一起提交）
- 固件工程：**仓库根目录**（`CMakeLists.txt` + `main/led_blink.c`），
  由 `C:\esp\c3-ai-led-demo` 迁入；旧目录已删除
- 固件身份（仓库根目录构建）：
  - led_blink.bin `bbadcaaf84efa866f86ed61aecb564b324e65f67f45132445111ac92b49104d4`
  - led_blink.elf `61918e38d569414f289eb0250f55c4907aa6326b1c30093b1fdc26fe8f826258`
- 已通过检查：
  - M0 全部检查（见 `evidence/M0/summary.md`，含 M0-09 迁移后回归）
  - 迁移后 `set-target` / `build` / `flash` / USB 日志均通过，500 ms 翻转时序不变
- 未运行 / 阻塞：无
- 下一步（M1）：
  1. 实测 BOOT 按键 GPIO9（按下电平、去抖），回填 docs/pinmap.md
  2. 确定 I2C 传感器型号与引脚（先核实 SuperMini 排针引出），建立 docs/requirements.md 与 acceptance.md
  3. 按方案第 6 节补齐 `components/` 分层（板级、传感器、业务）时再拆项目结构

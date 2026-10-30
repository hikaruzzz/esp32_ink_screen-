# 当前状态

更新：2026-10-30

## 下次恢复（关机/新会话后）

1. 打开 VS Code 目录 `G:\tools\workbuddy\esp32`，启动 Claude Code 新会话
   （会自动加载 [CLAUDE.md](../CLAUDE.md)，让它再读本文件与 docs/pinmap.md）
2. 接回开发板（USB 数据线），确认串口：设备管理器中 `USB 串行设备`，
   默认仍是 COM4；若编号变化，更新 docs/environment.md 与命令中的端口
3. 激活环境：`. G:\tools\workbuddy\esp32\tools\idf-env.ps1`
4. 构建/烧录：`idf.py build` / `idf.py -p COM4 flash`
5. 测试：`tools/test-host.ps1`（主机）；`tools/capture.py --reset`（实板日志）
6. 待办：M1-09 1 小时长稳；真实 I2C 驱动（需传感器型号与 I2C 引脚）

- 最后可运行提交：`0f56fd4`（去抖状态机与测试随后提交）
- 固件工程：仓库根目录；项目名 `c3_ai`；分层 `board` / `app_logic` / `sensor` / `main`
- 固件身份（当前构建）：
  - `build/c3_ai.bin` `5d3cbedb06e26f697d09538dff667ce6c446817fa7f0a6bcd7e9800f4e863f02`
  - `build/c3_ai.elf` `dad669c662dbeb13d76aa1b6b704da2c65943103186c9dc819bed489fc914b03`
- 已通过检查：
  - M0 全部（`evidence/M0/summary.md`）
  - M1-01 ~ M1-08、M1-10、M1-11（`docs/acceptance.md` 执行记录）
  - 按键去抖/短按/长按：实机 + 主机 `button_fsm`
  - 主机测试 3/3：app_mode、sensor_mock、button_fsm
- 未运行 / 阻塞：
  - M1-09 1 小时长稳 — 未执行
  - 真实 I2C 驱动 — 阻塞：传感器型号与 I2C 引脚未确认（`board.h` 中为 -1）
- 下一步：
  1. 执行 M1-09：烧录当前固件挂机 1 小时，采集心跳并检查复位/堆趋势
  2. 用户确定传感器型号与 I2C 引脚 → 实现 `sensor_i2c.c`（总线初始化/探测/读取），关闭 `CONFIG_APP_SENSOR_MOCK`

## 诊断工程：gpio_probe（2026-10-31）

- 位置：`tools/gpio_probe/`（独立 ESP-IDF 工程，不修改根目录固件）
- 目的：ReinkCase 四色墨水屏接线电平观察（屏板丝印 `DC24_NFC_V1.0CX`）
- 接线（用户提供，未实测）：DIN=GPIO7、CLK=GPIO6、CS=GPIO10、DC=GPIO3、RST=GPIO4、BUSY=GPIO5
- 行为：六脚全部配置为输入、禁用内部上拉/下拉；每秒打印运行计数与电平；
  **不发送任何屏幕初始化或刷新指令**
- 构建：`idf.py -C tools/gpio_probe build`（ESP-IDF v6.1，target esp32c3，USB Serial/JTAG 控制台）
- 固件：`tools/gpio_probe/build/gpio_probe.bin`，147840 B，sha256 `7711f910b333ff2bdb3907b43f1be037b7e15e9c4083cfe6d35af7c9664d4a77`
- 结果：编译通过（无源码告警）；烧录与实板观察未执行
- 烧录：`idf.py -C tools/gpio_probe -p COM4 flash`

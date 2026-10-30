# 当前状态

更新：2026-10-31

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
- 固件：`tools/gpio_probe/build/gpio_probe.bin`（第二版增加每秒 500 ms 高频边沿计数）
- 实板结果（2026-10-31，COM4）：
  - `RST=1`、`DIN=0`、`CLK=0`、`CS=0`、`BUSY=0`，30 s 内零边沿 → 被外部拉住或驱动
  - `DC` 报告值恒为 1，但 500 ms 窗口内有 50–96 次边沿（≈50 Hz）→ 悬空拾噪，
    第一次 1 s 采样看到的 9 s/15 s 电平变化是悬空节点偏置漂移
  - 结论：屏板已供电、控制器处于空闲态；未观察到 SPI 刷新突发，原主控未在刷屏
  - 日志：`evidence/M1/screen_probe_boot.log`、`evidence/M1/screen_probe_edges.log`
- 风险：`DIN/CLK/CS` 若仍被原手机壳 MCU 推挽驱动，直接驱动会冲突；
  需实物确认原 MCU 是否已拆除（拆机资料做法是拆掉 MCU 后飞线）
- 烧录：`idf.py -C tools/gpio_probe -p COM4 flash`

## 墨水屏驱动验证：epd_hello（2026-10-31）

- 位置：`tools/epd_hello/`（独立工程；驱动 `epd_se0398nz07.c` 暂放 `main/`，验证后抽到 `components/`）
- 参考：社区驱动 `epd3in98.py`（SE0398NZ07-FNG，A0/A1，768×552，BWRY 2bpp，全刷）
- 时序：`0x00=0x0B` / `0x61=768×600` / `0x04` 上电 / 逐行 `0x83`+`0x10`(192 B) /
  `0x12 0x01` 刷新 / `0x02` 关电；BUSY 低=忙
- 实板（COM4）：`ready(reset)=1`、`ready(power-on)=1`；552 行传输完成；
  刷新 `15015 ms`；结果 `ESP_OK`；空闲堆 216420 B
- 画面：白底 + 黄色边框 + 黑色 `Hello World` + 红色色条（四色映射验证）
- 日志：`evidence/M1/epd_hello.log`，后续标定日志见 `evidence/M1/epd_hello_*.log`
- **行映射标定结论（2026-10-31，A0 面板实测）**：
  - 物理行 `p <= 299` → 偶数 RAM 行 `y = 2p`（偶数区驱动物理 0~299）
  - 物理行 `p >= 300` → 奇数 RAM 行 `y = 2p - 599`（奇数区驱动物理 300~599，其中 552~599 不可见）
  - 社区驱动 `y = 2p if p<=275 else 1103-2p` 的分界点在 275/276，实为错误：
    下半区整体偏移 24 行、最后 24 行内容被挤出屏幕，多出的 24 行黑带由偶数 RAM 552~598 旧数据驱动
  - 面板**不做**水平镜像、**不做**旋转；早期“下半区镜像”的判断是误判（自加镜像导致文字跑偏）
  - 证据链：对称文字测试（伪影不随内容移动）→ 单黑带测试出现两条 → 数字标尺回读 →
    上段 RAM 探针把杂色带变黑 → 推出奇偶栅极分组。证据见 `evidence/M1/epd_hello_upperram.log` 等
- 已知特性：横屏看上下半区亮度略有差异（上半区稍暗）；两次刷新只能轻微改善，已作为面板特性接受
- 待确认：A1 批次面板的栅极寻址（`EPD_PANEL_IS_A1` 未验证）

## 墨水屏相框：epd_photo（2026-10-31）

- 位置：`tools/epd_photo/`（独立工程，`idf.py -C tools/epd_photo build`）
- 功能：开机建 SoftAP 热点 → 手机连上后打开 `http://192.168.4.1` 选图上传 →
  设备解码成四色并刷屏；BOOT 键短按切换热点开关，热点打开时屏幕上显示 SSID 与密码
- 配置：`main/app_config_local.h`（编译期默认值，已 gitignore，模板见 `.example`）+ NVS；
  网页“高级设置”也能改，重启后生效；`POST /config/reset` 恢复默认
- 代码结构：
  - `epd_se0398nz07.c/h` 面板驱动（与 epd_hello 相同标定）
  - `font8x11.c/h` ASCII 8x11 点阵（主机侧生成，覆盖 0x20~0x7E）
  - `screen_ui.c` 热点状态页（SSID / 密码 / 地址 / 按键提示）
  - `image_decode.c` tjpgd 逐 MCU 解码 → cover 裁切 → 四色量化（4x4 Bayer 抖动）
  - `app_jobs.c` 显示队列：重刷 / 上传解码 / 切换热点全部串行，独占面板与 SPI
  - `web_server.c` HTTP：`GET /`、`GET /status`、`POST /upload`(裸 JPEG)、`/config`、`/config/reset`、`/reboot`
  - `wifi_ap.c` AP 启停；`button.c` BOOT 键短按（GPIO9）
  - `main/web/index.html` 被 EMBED_TXTFILES 内嵌；浏览器端 cover 裁切 + 四色预览 + 压到 40 KB 以内
- 关键约束：
  - C3 的 ROM 带 TJpgDec，但 ROM 版 `jd_decomp` 多一个 format 参数且不导出 `tjpgd.h`；
    `sdkconfig.defaults` 里固定 `CONFIG_JD_USE_ROM=n`，用组件自带 tjpgd（逐 MCU 回调，RAM 占用低）
  - 上传上限 48 KB（Wi-Fi + httpd 起来后实测空闲堆约 82 KB），网页端压到 40 KB 以内
- 已实测（COM4，2026-10-31）：
  - 编译通过；固件 `epd_photo.bin` 0xe1670 字节，分区余量 55%
  - 开机：热点 `EPD-Photo`（WPA2）、`http://192.168.4.1`、开机状态页刷新 15064 ms
  - `GET /` 200；`GET /status` 返回 state/msg/src/heap/wifi/ssid
  - `POST /upload` 38094 B → 200，0.16 s 收完；解码 768×552 用时约 2.9 s；刷新 15 s 后回到 `idle`
  - 日志：`evidence/M2/boot2.log`、`evidence/M2/upload_toggle.log`
- 待确认：
  - 目视画面（开机状态页、上传照片的四色效果）
  - BOOT 键切换热点实机验证（关→开 后手机需重连）
  - 手机浏览器端完整流程（选图/预览/压缩/进度）
- 备注：`tools/capture.py` 打开端口时原本会拉高 RTS/DTR，等于把 C3 按在复位里
  （表现为串口静默、热点消失）；已改为先置低再 open，需要复位时用 `--reset`

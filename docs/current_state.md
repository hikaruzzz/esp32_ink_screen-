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

> 上传链路已改成「手机端渲染 → 设备只存帧」：见文末
> [手机端渲染与原始帧上传](#手机端渲染与原始帧上传2026-10-31-晚)。
> 下面的 JPEG 解码路径仍然保留，作为脚本/兼容入口。

- 位置：`tools/epd_photo/`（独立工程，`idf.py -C tools/epd_photo build`）
- 功能：开机建 SoftAP 热点 → 手机连上后打开 `http://192.168.4.1` 选图上传 →
  设备解码成四色并刷屏；BOOT 键短按切换热点开关，热点打开时屏幕上显示 SSID 与密码
- 配置：`main/app_config_local.h`（编译期默认值，已 gitignore，模板见 `.example`）+ NVS；
  网页“高级设置”也能改，重启后生效；`POST /config/reset` 恢复默认
- 代码结构：
  - `epd_se0398nz07.c/h` 面板驱动（与 epd_hello 相同标定）
  - `font8x11.c/h` ASCII 8x11 点阵（主机侧生成，覆盖 0x20~0x7E）
  - `screen_ui.c` 热点状态页（SSID / 密码 / 地址 / 按键提示）
  - `image_decode.c` tjpgd 逐 MCU 解码 → cover 裁切 → 交给 `image_pipeline.c` 量化
  - `image_pipeline.c/h` 四色量化：色调 LUT（曝光/对比度/gamma）→ 线性光下投影到
    四色凸包最合适的边 → 按蓝噪声阈值选两色之一；参数由网页画质滑块下发
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

## 显示效果优化（2026-10-31 晚，未上板验证）

用户反馈：功能正常，但画面偏黑失真，整屏有白色格栅。

- 观察事实：`tools/epd_preview.py --algo bayer4 --grid-bug` 可稳定复现
  「16 像素周期白色格栅 + 整体偏黑」，与实拍一致（`evidence/M2/image_quality.png`）
- 根因 1（格栅）：`image_decode.c` 里判断像素是否落在 MCU 块内写成
  `sy > rect->bottom`，但块覆盖的是连续区间 `[top, bottom+1)`，像素中心是
  `bottom+0.5` → 每个 16x16 块最后一行/列被丢弃；帧缓冲预填白 → 全屏白格栅。
  浏览器上传的图正好是 768x552（s_scale=1），必然触发。
  修复：按连续区间判断，越界索引用夹取兜底
- 根因 2（偏黑失真）：理想色板 {0,0,0}/{255,255,255} + 每通道 ±40 的 4x4 Bayer
  → 中间调按 sRGB 50/50 撒黑白点，墨水屏黑点视觉权重大，再叠高频噪点
- 新管线（`image_pipeline.c`，与 `tools/epd_preview.py`、网页 JS 三处同算法）：
  1. 实测色板近似：黑(32,32,32) 白(204,204,198) 黄(222,190,74) 红(166,62,52)
     —— 用理想白 255 会让抖动持续追一个屏幕给不出的亮度
  2. 色调曲线：饱和度 → 线性域曝光 → 绕中灰对比度 → gamma（合成 256 项 LUT）
  3. 把像素投影到四色凸包的 6 条边上，取残差最小的一条，得到两色混合比例 w
  4. 16x16 蓝噪声（void-and-cluster）阈值 < w 选 a 端，否则 b 端；
     线性光下混合，纸面亮度才正确，且没有 Bayer 的规则格点
- 可调参数（网页“画质”卡片，随 `/upload?expo=&gamma=&con=&sat=` 下发）：
  曝光 0.6~2.2（默认 1.35）、明暗 0.7~1.8（1.10）、对比度 0.7~1.4（1.05）、
  饱和度 0~1.8（1.25）；浏览器 localStorage 记住上次取值
- 主机侧工具：`python tools/epd_preview.py 图片.jpg [-o out.png]`
  可用 `--algo bayer4 --grid-bug` 复现旧问题，`--exposure/--gamma/--contrast/--sat`
  调参，输出 PNG 直接看效果，不必烧录等 15 秒刷屏
- 一致性验证：把 `web/index.html` 的 `<script>` 抠出来在 node 里跑，
  与 numpy 参考实现逐像素比对一致率 99.92%（差异只来自浮点取整阈值）
- 已编译：`epd_photo.bin` 0xe5030 字节，分区余量 54%；**未烧录**（板子已断开）
- 下次上板要确认：
  - 白格栅是否消失
  - 默认参数下的亮度是否合适，不合适用画质滑块往「亮度」方向调
  - 色板四项数值是否贴近实屏（可先目视对比，再改 `image_pipeline.c` 的
    `k_pal_srgb` 与网页 `PAL`，两处必须同步）
  - 解码耗时是否明显变长（6 条边 × 每像素浮点运算，预计比原来多几百毫秒到 1 秒）
- 备注：`tools/capture.py` 打开端口时原本会拉高 RTS/DTR，等于把 C3 按在复位里
  （表现为串口静默、热点消失）；已改为先置低再 open，需要复位时用 `--reset`

## 手机端渲染与原始帧上传（2026-10-31 晚）

用户问「解码是不是可以不在开发板上跑，手机浏览器算好再传给开发板」——
可以，而且更好。已改成这条路：

- 观察事实：C3 无硬件浮点，板上解 JPEG + 量化要几十秒；网页为了塞进 48 KB
  把图压到 40 KB（0.8 bpp），这才是「像素很低、颜色像丢了」的主因
- 新分工：
  - 手机浏览器：cover 裁切 → 饱和度/曝光/对比度/gamma → **Oklab 色域映射到
    四色凸包** → **Floyd–Steinberg 误差扩散** → 打包成 768×552 2bpp 原始帧
    （105,984 B，每字节 4 像素、左侧像素在高位，与 `EPD_FB_BYTES` 完全一致）
  - 开发板：`POST /upload/raw` 分块收（4 KB/块）→ 写 photo 分区 → 读回帧缓冲
    → 刷屏。**不碰 JPEG、不做任何图像计算**
- 为什么换掉蓝噪声：有序抖动在平坦区有规则纹理，误差扩散才接近别人彩墨屏的观感；
  Oklab 里做是因为线性 RGB 里蓝色亮度极低，直接投影会变成一堵黑墙
- 设备侧改动：
  - `epd_se0398nz07.h/.c` 新增 `EPD_ROW_BYTES`/`EPD_FB_BYTES`/`epd_framebuffer()`
  - `photo_store.c/h` 文件头升到 "EPD3"：magic+len+seq+**fmt**+4 float（32 B）；
    `PHOTO_FMT_RAW2BPP` / `PHOTO_FMT_JPEG`；A/B 双槽、commit 时才写 magic 不变
  - `app_jobs.c`：原始帧直接整块读进帧缓冲；JPEG 分支保留
  - `web_server.c`：新增 `POST /upload/raw`（长度必须 105,984）；`/upload`(JPEG)
    改为流式写 flash，上限由分区决定（单槽 262,116 B）
  - `partitions.csv`：`photo` 由 64 KB 扩到 512 KB（两个 256 KB 槽）
- 网页侧：`index.html` 的量化改成上面的 fsgm 算法，预览直接用打包结果还原，
  **所见即所得**；去掉 40 KB 压缩和蓝噪声表
- 主机工具：
  - `python tools/epd_preview.py 图.jpg --algo fsgm`（新增 `fs`/`fsok` 对照）
  - `python tools/fb_crosscheck.py 图.png`：Node 跑网页里的同一段脚本 vs numpy
    参考实现，逐像素比对。当前 **99.82% 像素一致**（差异来自 JS float32 与
    numpy float64 的舍入；JS 渲染 768×552 约 0.5 s）
  - 对照图：`evidence/M2/{tp,photo}_{edge,fs,fsok,fsgm}.png`
- 已实测（COM4）：编译通过（`epd_photo.bin` 0xee340，余量 52%）；
  开机日志确认 `photo 分区 524288 B @0x200000，2 个槽，每槽 262144 B`；
  开机状态页刷新 15064 ms（`evidence/M2/raw_path_boot.log`）
- 待确认（需手机上实测，主机不进热点）：
  - 手机浏览器的渲染耗时与发热（预计 0.5~1.5 s）
  - 105,984 B 上传在 SoftAP 下的耗时
  - 实屏观感是否比蓝噪声版本细腻，色板数值是否还需微调
  - 关热点（BOOT 键）后是否能还原上一张照片

## 色温 / 色相调整（2026-10-31 晚）

用户反馈「还是有点偏色」，加了色温和色相两条滑块。

- 网页「画质」卡片新增：
  - **色温** `qtemp` −1..1（默认 0）：在 Oklab 的 b 轴上平移，
    +1 对应 b +0.06（偏黄、暖），−1 偏蓝、冷
  - **色相** `qhue` −60..60°（默认 0）：在 Oklab 色度平面上旋转 (a,b)，
    L 不变，整体转色偏（画面发绿就往红的方向转）
- 两者都在**色域映射之前**生效，所以改完直接改变最终 2bpp 帧；
  只影响手机端渲染，固件逻辑没变（只是内嵌了新版 `index.html`）
- 主机侧对应：`python tools/epd_preview.py 图.jpg --algo fsgm --temp 0.5 --hue 15`
- 一致性：`tools/fb_crosscheck.py` 仍为 99.82% 像素一致
  （注意：Node 桩里的滑块默认值要与页面一致，否则算出来差分很大）
- 已编译并烧录：`epd_photo.bin` 0xee7d0；开机正常（`evidence/M2/hue_temp_boot.log`）
- 待确认：实屏上哪个组合最接近原图；若普遍偏冷，可把默认 `qtemp` 设为 +0.2 左右

## 上电默认关热点 + 抖动颗粒（2026-10-31 深夜）

用户要求「上电后默认是 Wi-Fi 关闭」，并问「为什么画面感觉很多白噪点」。

- 启动流程改动：
  - `wifi_ap_start()` 改名 `wifi_ap_init()`：netif / 默认事件循环 / `esp_wifi_init`
    / `esp_wifi_set_mode` / `esp_wifi_set_config` 全做，**不再调
    `esp_wifi_start()`**，`s_ap_on = false`
  - `app_main()`：初始化后先 `photo_store_info()` 探测分区；有照片就
    `app_jobs_submit_decode()` 直接像画框一样显示，没有照片才画
    「按 BOOT 打开热点」的提示页
  - BOOT 短按仍走 `JOB_TOGGLE_WIFI`：开 → `esp_wifi_set_enabled(true)` 起射频
    并画 SSID/密码页；再按 → 关射频并把照片解码回屏
  - 实机（COM4，`evidence/M2/boot_wifioff.log`）：
    `热点已配置但未启动：SSID="EPD-Photo" 加密方式=WPA2（按 BOOT 打开）`
    → `上电显示分区里的照片（105984 B）` → 刷屏 15064 ms
- 白噪点成因（结论：不是分辨率问题，是四色 2bpp 的物理限制）：
  - 面板只有黑白黄红 4 种颜料，浅灰/米白只能用白+黄+红的小点混；
    1×1 误差扩散下这些点是**单像素**的，密度又高（本图米白卡片约
    61% 白 + 11% 黄 + 11% 红 + 16% 黑），远看发「沙」，近看就是一屏噪点
  - 饱和度默认 1.25 会放大这一点：相机白平衡偏暖的卡片被推得更黄，黄点更多
- 新增「颗粒」滑块 `qgrain` 1..3（默认 **2**）：
  - 线性光下按 g×g 块取平均 → Oklab → 色域映射 → FS（块分辨率 384×276 /
    256×184）→ 每个块复制成 g×g 像素再打包；768、552 都能被 1/2/3 整除
  - 1× 最细（细节最好、噪点最多）；2× 连成小块更像印刷网点；3× 最干净最糊
  - `epd_preview.py` 新增 `--grain`；`fb_crosscheck.py` 新增 `--grain`
  - 一致性：grain=1/2/3 三档 **字节 100% 一致**（网页 JS vs numpy）
  - 对照图：`evidence/M2/grain_{1,2,3}.png`、`evidence/M2/grain_cmp.png`
- 不想改代码时的降噪手段：颗粒调大、饱和度调低（把暖白压回白）、
  亮度调高（浅色区直接饱和成纯白，不参与抖动）
- 已编译并烧录：`epd_photo.bin` 0xee850（余量 52%）
- 待用户实测（主机不进热点）：BOOT 开/关热点、`/upload/raw` 上传、
  颗粒默认 2 的实屏观感

## 实屏色卡（2026-10-31 深夜）

- `tools/gen_color_card.py`：生成 768×552 四色色卡帧（黑/白/黄/红四个 150×180
  纯色块 + 四角 20×20 黑定位标），输出 `evidence/M2/color_card.{png,2bpp}`
- 写屏方式（不连热点）：把 `header(32 B) + 帧(105,984 B)` 用 esptool 直接写进
  `photo` 分区槽 0（0x200000，seq=3），重启即刷出；槽 1（seq=2）保留原照片做回退
  ```
  python -m esptool --chip esp32c3 -p COM4 -b 460800 write_flash 0x200000 <slot0.bin>
  ```
- 已确认：重启后刷屏 15014 ms（`evidence/M2/color_card_boot.log`）
- 拍照后处理：用四角黑标做透视校正，按脚本打印的色块坐标取样，和
  `k_pal_srgb`/网页 `PAL` 的 (33,32,34)/(204,204,198)/(217,133,9)/(109,31,22) 比
- 注意：手机端上传一张新照片会占用较旧的槽；再传一张就会覆盖色卡

## 按实拍色卡重标定色板（2026-10-31 深夜）

用户拍回色卡照片并确认颜色可信，据此重标定四色板：

| 颜料 | 旧值 | 新值 |
|------|------|------|
| 黑 | 33,32,34 | **54,53,58** |
| 白 | 204,204,198 | 204,204,198（假设值） |
| 黄 | 217,133,9 | **216,154,93** |
| 红 | 109,31,22 | **121,67,79** |

- 取样：`python tools/check_color_card.py docs/微信图片_20261031133845_63_2.jpg -o 对照图.png`
  （自动找旋转方向与三个大色块，白块取黑/黄中点；详见 docs/calibration.md）
- 三处同步：`image_pipeline.c` 的 `k_pal_srgb`、网页 `PAL`、`epd_preview.py` 的
  `PALETTE`；`pal_lut.h` 已用 `python tools/gen_pal_lut.py` 重新生成
  （自检与浮点参考实现一致率 97.67%，偏差在 5:5:5 量化格内）
- 精度修正：色板点变近后，网页的 float32 缓冲与 numpy float64 在色域边界上
  分歧被误差扩散放大（逐像素一致率掉到 86%）；把 `index.html` 的 L/A/B 与
  扩散缓冲改为 `Float64Array` 后回到 **100%**（grain 1/2/3 均为 100%）
- 已编译并烧录（`epd_photo.bin`）：并把新色板渲染的示例帧写进槽 1（seq=4），
  重启后屏上直接可看；色卡仍在槽 0（seq=3）
- 待用户确认：实屏观感是否比旧色板更接近原图（尤其黑场是否发灰）

## 色板回归白天实测 + 对照上游开源驱动（2026-11-02）

用户反馈「显示效果还是很差，颜色很大问题」，并要求对照 GitHub 上的开源实现。

- 对照对象：**ZGQ-inc/3.98-epd**（同一块 SE0398NZ07-FNG 面板的 ESP-IDF Rust 工程，
  `src/display/driver.rs`、`src/display/framebuffer.rs`、`cf_pages/js/dither.js`）
- 逐项对比结论：
  | 项目 | 上游 | 本仓库 | 结论 |
  |------|------|--------|------|
  | 初始化 | `0x00=0x0B` / `0x61=768×600` / `0x04` 上电 | 相同 | 一致 |
  | 写入 | 逐行 `0x83`(9 字节窗口) + `0x10`(192 B) | 相同 | 一致 |
  | 刷新 | `0x12 0x01` → 等 BUSY ≈15 s → `0x02 0x00` | 相同 | 一致 |
  | A0 行映射 | `y=2s (s≤275)` / `y=1103-2s` | `y=2s (p≤299)` / `y=2p-599` | **不同**；实拍色卡下半区文字不镜像且位置正确，仍按本机实测映射 |
  | 2bpp 编码 | 每字节 4 像素、左侧高位；0黑/1白/2黄/3红 | 相同 | 一致 |
  | 抖动 | 理想色板 + 加权 RGB 最近色 + FS（另有 9 种算法） | Oklab 凸包投影 + FS | 保留本仓库实现（主机盲测 dE 更低） |
  | 色板 | 理想值 0/255/#F4C430/#D32F2F | 见下 | **本仓库色板来源有误，已修正** |
- 观察事实：设备上实际存着的用户照片（photo 槽 0，seq=9）取出来看，整幅偏橙褐，
  黄码占 20.5%、白码只占 18%（`evidence/M2/palette_ab/user_slot0_oldpal.png`）
- 根因假设：2026-10-31 深夜的标定照片（粉光 + 相机暗部抬升）把黄/红的蓝绿通道
  抬高、把黑抬到纸白的 0.27；量化器误以为「黄=浅土黄、红=玫红」，于是把大量
  本该黑/白抖动的中性亮区判给黄色颜料，实屏上的金黄点 → 整幅橙褐发白
- 区分检查：同一面板在**白天自然光**照片（`docs/微信图片_20261031020115_59_2.jpg`）
  里的实测比值：黑 0.16、黄 1.13/0.74/0.05、红 0.58/0.18/0.13 —— 与夜间照片
  （黑 0.27、黄 1.06/0.76/0.47、红 0.59/0.33/0.40）在 B/G 通道上差一个量级
- 最小修复：色板改回白天实测值（三处同步 + 重生成 `pal_lut.h`）

  | 颜料 | 夜间（弃用） | 白天（当前） |
  |------|--------------|--------------|
  | 黑 | 54,53,58 | **32,33,35** |
  | 白 | 204,204,198 | 204,204,198（假设值） |
  | 黄 | 216,154,93 | **231,152,10** |
  | 红 | 121,67,79 | **117,36,26** |

- 验证（主机侧）：
  - 网页 JS vs numpy 逐字节一致率 **100%**（grain 1/2/3，`tools/fb_crosscheck.py`）
  - `gen_pal_lut.py` 自检与浮点参考一致率 97.76%（5:5:5 格内）
  - 对照图：`evidence/M2/palette_ab/old_vs_new_bench.png`（左旧右新）、
    `bench_newpal.png`、`ab_user_photo.png`（用户照片上下对照：上=旧码，下=重映射）
- 构建/烧录：`epd_photo.bin` 0xef490（分区余量 52%），已烧录 COM4；
  重启后 15014 ms 正常刷出（`evidence/M2/newpal_day_boot.log`）
- 待用户确认：重新上传同一张照片，看橙褐/发白是否消失；若不满意，下一个可调项是
  默认饱和度（主机盲测 sat 1.0 略优于 1.25）与颗粒
- 未采用：上游的行映射公式与理想色板；理由：本机实测行映射正确，理想色板在
  盲测里 dE 反而更高（`evidence/M2/palette_ab/`）

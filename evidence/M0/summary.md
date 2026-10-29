# M0 验证记录

日期：2026-10-30
板卡：HW-466AB ESP32-C3 SuperMini
串口：COM4（原生 USB Serial/JTAG）

## 环境

| 项 | 值 |
| --- | --- |
| ESP-IDF | v6.1.0（release tag `v6.1`，commit `fff9895c82d744c7237be8847347bdd1b07c6643`） |
| IDF 路径 | `C:\Espressif\v6.1\esp-idf` |
| 工具路径 | `C:\Espressif\tools` |
| Python 环境 | `idf6.1_py3.14_env` |
| 激活 | `. G:\tools\workbuddy\esp32\tools\idf-env.ps1` |

## 板卡实测（esptool v5.6.0）

- 芯片：ESP32-C3 (QFN32)，revision v0.4，Wi-Fi/BT5(LE)，160 MHz，晶振 40 MHz
- Flash：内嵌 4 MB（XMC，Manufacturer 0x46 / Device 0x4016）
- USB 模式：USB-Serial/JTAG
- MAC：`48:f6:ee:15:78:c8`
- 复位启动：`rst:0x15 (USB_UART_CHIP_RESET), boot:0xd (SPI_FAST_FLASH_BOOT)`

## 检查项

| ID | 检查 | 结果 | 证据 |
| --- | --- | --- | --- |
| M0-01 | hello_world 编译（esp32c3，USB 控制台配置） | PASS（exit 0） | `hello_world-build.log` |
| M0-02 | hello_world 烧录 | PASS（Hash of data verified） | `hello_world-flash.log` |
| M0-03 | hello_world USB 启动日志 | PASS（`Hello world!`、ESP-IDF v6.1.0、4MB flash 识别） | `hello_world-boot.log` |
| M0-04 | LED 工程编译 | PASS（exit 0） | `led_build.log` |
| M0-05 | LED 工程烧录 | PASS | `led_flash.log` |
| M0-06 | LED 日志 500 ms 翻转（8 s 窗口 19 条事件，无 panic/watchdog） | PASS（时间戳 83/583/1083/1583…） | `led-boot.log` |
| M0-07 | LED 物理目视确认（GPIO8 低电平点亮） | PASS（用户目视：500 ms 正常闪烁） | 用户确认，2026-10-30 |
| M0-08 | VS Code ESP-IDF 扩展 | **NOT_RUN** | 未安装 |

## 固件身份

| 固件 | SHA256 |
| --- | --- |
| hello_world.bin | `2630c77a020fadb39cebde0eb9f29c5274b381734c3f43763933434821c48a6d` |
| led_blink.bin | `58985bb243118b5b9610930b443b1b0c83f54a7e60ab3b54607feb0b1f280b23` |
| led_blink.elf | `6438c123506356dcfdb052e69df4c01f1c550f876d0e91c27620ba31cdcd7f83`（与启动日志 `ELF file SHA256: 6438c1235...` 一致） |

## 现场记录（环境问题，均不阻塞）

1. `github.com` git 连接超时（21120 ms）。EIM 在线安装不可用；改用 Gitee 官方镜像
   （commit 与 GitHub 标签一致）与乐鑫 CDN 资产代理完成安装。
2. EIM 的 gix 克隆因子模块与 `C:\Program Files` 路径含空格失败，随后回退卡在
   Gitee 子模块凭证提示；终止后改用官方 v6.1 完整发布包（含全部子模块）。
3. Git Bash 的 `MSYSTEM` 被 ESP-IDF 6.x 拒绝；`tools/idf-env.ps1` 内清除。
4. 构建日志通过 PowerShell 管道出现一条 Unicode 控制台警告（非构建错误），
   构建产物与烧录校验均正常；日志已转存 UTF-8。

## 文件清单

- `hello_world-build.log` / `hello_world-flash.log` / `hello_world-boot.log`
- `led_build.log` / `led_flash.log` / `led-boot.log`
- `led_set-target.log`
- `chip-flash-info.log`

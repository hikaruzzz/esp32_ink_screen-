# 开发板信息

状态：**M0 建档完成**（2026-10-30）—— 芯片/Flash/USB 已实测；LED 由固件日志验证，
物理目视确认待补；BOOT 按键未实测

| 项 | 值 | 来源 / 证据 |
| --- | --- | --- |
| 开发板完整型号 | HW-466AB ESP32-C3 SuperMini | 用户提供；实物丝印照片待补 |
| 芯片 | ESP32-C3 (QFN32)，revision v0.4 | esptool 实测，`evidence/M0/chip-flash-info.log` |
| 模组型号 | 内嵌 Flash 版（推测 ESP32-C3FN4，待丝印确认） | esptool：Embedded Flash 4MB |
| Flash 容量 | **4 MB（XMC，0x46/0x4016），内嵌** | esptool 实测 |
| 晶振 | 40 MHz | esptool 实测 |
| MAC | 48:f6:ee:15:78:c8 | esptool 实测 |
| 串口方案 | 芯片原生 USB Serial/JTAG，无 USB-UART 桥 | 枚举 `USB\VID_303A&PID_1001` → COM4 |
| USB 口 | 单口 USB-C，原生 USB Serial/JTAG | 枚举与烧录实测 |
| 板上 LED | GPIO8，低电平点亮 | 用户提供；固件日志确认 GPIO8 翻转（目视待确认） |
| BOOT 按键 | GPIO9，按下拉低 | 实测确认（2026-10-30，原始电平与短按事件日志） |
| 启动绑带引脚 | GPIO2 / GPIO8 / GPIO9 | 芯片特性；GPIO8 低有效 LED 熄灭态为高，与正常启动电平一致 |
| 原理图链接 | 待补（HW-466AB 卖家资料） | |

## 证据

- 串口枚举（2026-10-29）：`USB 串行设备 (COM4)`，`USB\VID_303A&PID_1001&MI_00`
- 芯片/Flash：`evidence/M0/chip-flash-info.log`
- 启动记录：`evidence/M0/hello_world-boot.log`、`evidence/M0/led-boot.log`
- 复位类型：`rst:0x15 (USB_UART_CHIP_RESET), boot:0xd (SPI_FAST_FLASH_BOOT)`

## 备注

- 原生 USB 占用 GPIO18/19，不得再分配给其他功能。
- M0 之后新增引脚结论必须注明来源（原理图 / 实测 / 用户提供）与验证状态。
